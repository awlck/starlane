#include "gameobj.h"

#include <stdexcept>
#include <string.h>

#include <magic_enum.hpp>
#include <pugixml.hpp>

#include "../game.h"
#include "character.h"
#include "description.h"
#include "location.h"
#include "group.h"
#include "../savefiles/writer.h"

namespace Starlane {

GameObj *GameObj::CreateFromXML(const pugi::xml_node &xmlNode) {
	if (strcmp(xmlNode.name(), "Location") == 0)
		return Location::CreateFromXML(xmlNode);
	if (strcmp(xmlNode.name(), "Character") == 0)
		return Character::CreateFromXML(xmlNode);
	if (strcmp(xmlNode.name(), "Object") != 0) {
		throw std::runtime_error(std::string("Unknown object type: ") + xmlNode.name());
	}
	auto result = new GameObj;
	result->MakeCommonValues(xmlNode);
	for (const auto &it : xmlNode.children("Name"))
		result->MutableNouns().emplace_back(it.child_value());
	result->description = Game::Get()->CreateDescFromXML(xmlNode.child("Description"));

    // Extract location data from properties (this is faster than directly navigating the XML tree),
    // taking care to clean up the no-longer-needed properties after ourselves to conserve memory.
    std::string nextProp;
    if (result->GetStrProp("StaticOrDynamic") == "Dynamic") {
        nextProp = "DynamicLocation";
        result->dynamic = true;
    } else {
        nextProp = "StaticLocation";
    }
    result->ErasePropValue("StaticOrDynamic");
    auto ht = ParseHoldingType(result->GetStrProp(nextProp).c_str());
    result->ErasePropValue(nextProp);
    result->relation = ht.first;
    nextProp = ht.second;
    if (!nextProp.empty()) {
        result->parent = result->GetStrProp(nextProp);
        result->ErasePropValue(nextProp);
        // An object that starts out held by the player stores its container as the reference
        // "%Player%" rather than the player's key; resolve it now so the rest of the engine only
        // ever sees a concrete key in `parent`. (The player key is known by this point -- it is
        // determined before objects load.)
        if (Util::IsReference(result->parent))
            result->parent = Game::Get()->GetReference(result->parent);
    }

	result->CompileNameExpressions();
	result->MakeMatchExpr();
	return result;
}

void GameObj::CompileNameExpressions() {
	auto *g = Game::Get();
	// A name component is ordinary text that may have %function% calls embedded in it, exactly
	// like a description -- so let the description machinery deal with it.
	if (prefix.find('%') != std::string::npos)
		prefixExpr = g->CreateDescFromText(prefix);
	if (!nouns->empty() && (*nouns)[0].find('%') != std::string::npos)
		nameExpr = g->CreateDescFromText((*nouns)[0]);
}

std::string GameObj::DisplayPrefix() const {
	if (prefixExpr == 0) return prefix;
	return Game::Get()->GetDescription(prefixExpr)->Build();
}

std::string GameObj::DisplayNoun() const {
	if (nouns->empty()) return "";
	if (nameExpr == 0) return (*nouns)[0];
	return Game::Get()->GetDescription(nameExpr)->Build();
}

std::string GameObj::GetDisplayName(bool defArt) const {
	std::string result;
	if (!article.empty()) {
		result = defArt ? "the" : article;
		result += ' ';
	}
	result += GetBareName();
	return result;
}

std::string GameObj::GetBareName() const {
	std::string result;
	std::string pfx = DisplayPrefix();
	if (!pfx.empty()) {
		result += pfx;
		result += ' ';
	}
	result += DisplayNoun();
	return result;
}

bool GameObj::MatchesNameWord(const std::string &word) const {
	if (Util::ContainsWholeWord(article, word)) return true;
	if (Util::ContainsWholeWord(prefix, word)) return true;
	for (const auto &n : *nouns)
		if (Util::ContainsWholeWord(n, word)) return true;
	return false;
}

const std::string &GameObj::GetLocationKey() const {
	static const std::string kNowhere;
	if (parent.empty()) return parent;
	const GameObj *o = this;
	Game *theGame = Game::Get();
	while (true) {
		// A static object spread across a whole location group (see MoveToGroup) isn't "at"
		// any single location -- Location::HoldsDirectly answers presence per-location for
		// those instead. `parent` here names a Group, not a GameObj, so GetObject would throw
		// on the walk below; bail out before it can.
		if (o->relation == HoldingType::AtLocationGroup) return kNowhere;
		o = theGame->GetObject(o->parent);
		if (o->parent.empty()) break;
	}
	return o->key;
}

const Location *GameObj::GetLocation() const {
	const std::string &lkey = GetLocationKey();
	if (lkey.empty()) return nullptr;
	return AsLocation(Game::Get()->TryGetObject(lkey));
}

const std::string &GameObj::GetVisbilityCeiling() const {
	switch (relation) {
		case HoldingType::InObject:
		{
			auto o = Game::Get()->GetObject(parent);
			if (!o->GetBoolProp("Openable") || o->GetStrProp("OpenStatus") == "Open")
				return o->GetVisbilityCeiling();
			else return o->Key();
		}
		case HoldingType::Worn:
		case HoldingType::PartOf:
		case HoldingType::OnObject:
			return Game::Get()->GetObject(parent)->GetVisbilityCeiling();
		case HoldingType::AtLocation:
		default:
			return parent;
	}
}

void GameObj::MoveTo(const std::string &newParent, HoldingType newRelation) {
	// Make sure we don't move objects (or worse, characters) into non-existing
	// objects, because everything else in the program assumes that a non-empty
	// parent key actually refers to a real object.
	if (newParent == "Hidden")
		parent = "";
	else if (!Game::Get()->ObjectExists(newParent))
		return;
	else
		parent = newParent;

	relation = newRelation;

	// Anyone watching this object arrive at its new position has now seen it. Checking HasSeen
	// before CanSee: the former is a hash lookup and the latter walks the containment chain, and
	// most characters have seen most things by the time a game is under way.
	auto *g = Game::Get();
	for (size_t i = 0; i < g->GetAllObjects().size(); i++) {
		const auto *c = AsCharacter(g->GetAllObjects()[i]);
		// Tested on the read-only pointer, and only then asked for one we may write through.
		if (!c || c->HasSeen(key) || !c->CanSee(key)) continue;
		AsCharacter(g->MutableObject(i))->MarkSeen(key);
	}
}

void GameObj::MakeCommonValues(const pugi::xml_node &xmlNode) {
	key = xmlNode.child_value("Key");
	article = xmlNode.child_value("Article");
	prefix = xmlNode.child_value("Prefix");

	for (const auto &prop: xmlNode.children("Property")) {
		SetPropValueFromXML(prop);
	}
}

GameObj *GameObj::Clone() const {
	return new GameObj(*this);
}

std::string GameObj::GetDescription(bool forDisplay) const {
	auto *g = Game::Get();
	return forDisplay ? g->MutableDescription(description)->BuildAndCommit()
	                  : g->GetDescription(description)->Build();
}

std::string GameObj::GetListOfChildren(GameObj::ChildFilter f1, GameObj::ChildRelFilter f2, bool recurse) const {
	std::string result;
	size_t count = 0;
	auto *g = Game::Get();
	// In the order the game file lists them: that is the order ADRIFT itself writes lists in, and
	// the player sees it ("a set of fatigues and a pair of boots", not the other way around).
	for (const GameObj *child: g->GetAllObjects()) {
		if (child->GetParentKey() != key) continue;
		if (f1 == ChildFilter::Objects && child->IsCharacter())
			continue;
		if (f1 == ChildFilter::Characters && !child->IsCharacter())
			continue;
		switch (f2) {
			case ChildRelFilter::On:
				if (child->relation != HoldingType::OnObject)
					continue;
				break;
			case ChildRelFilter::In:
				if (child->relation != HoldingType::InObject)
					continue;
				break;
			case ChildRelFilter::OnAndIn:
				if (child->relation != HoldingType::InObject && child->relation != HoldingType::OnObject)
					continue;
				break;
		}
		if (count++ > 0)
			result += '|';
		result += child->Key();

		if (recurse) {
			std::string tmp = child->GetListOfChildren(f1, f2, true);
			if (!tmp.empty()) {
				result += '|';
				result += tmp;
			}
		}
	}
	return result;
}

void GameObj::TransferPronounNouns(GameObj &newOwner, const std::vector<std::string> &pronouns) {
	bool changed = false, otherChanged = false;
	std::vector<std::string> &myNouns = MutableNouns();
	for (auto it = myNouns.begin(); it != myNouns.end();) {
		auto pronoun = std::find_if(pronouns.begin(), pronouns.end(), [&](const std::string &p) {
			return Util::ToLower(p) == Util::ToLower(*it);
		});
		if (pronoun == pronouns.end()) { ++it; continue; }
		std::vector<std::string> &theirNouns = newOwner.MutableNouns();
		bool alreadyHasIt = std::any_of(theirNouns.begin(), theirNouns.end(), [&](const std::string &n) {
			return Util::ToLower(n) == Util::ToLower(*pronoun);
		});
		if (!alreadyHasIt) {
			theirNouns.push_back(*pronoun);
			otherChanged = true;
		}
		it = myNouns.erase(it);
		changed = true;
	}
	if (changed) MakeMatchExpr();
	if (otherChanged) newOwner.MakeMatchExpr();
}

void GameObj::WriteState(Save::Writer &writer) const {
	writer.WriteKV("parent", parent);
	writer.WriteKV("dynamic", dynamic);
	writer.WriteKV("holding_type", magic_enum::enum_name(relation));
	writer.WriteSortedKV("groups", groupMembership);
	writer.BeginNamedCompound("properties");
	// make sure not to consult the groups here
	writer.WriteSortedMap(PropHolder::GetAllIntProps());
	writer.WriteSortedMap(PropHolder::GetAllStrProps());
	writer.EndCompound();
}

bool GameObj::RestoreState(const Save::AstNode *node) {
	const auto *parentNode = node->FindChildByName("parent");
	if (!parentNode) return false;
	parent = parentNode->Str;
	const auto *dynamicNode = parentNode->nextSibling;
	if (!dynamicNode) return false;
	dynamic = dynamicNode->sv.Bool;
	const auto *htNode = dynamicNode->nextSibling;
	if (!htNode) return false;
	auto tmpRelation = magic_enum::enum_cast<HoldingType>(htNode->Str);
	if (!tmpRelation.has_value()) return false;
	relation = tmpRelation.value();
	const auto *grpNode = htNode->nextSibling;
	if (!grpNode) return false;
	// Replaced rather than added to: the file lists every group this object is in, so an object
	// that has since left one must not keep it.
	groupMembership.clear();
	for (const auto *grp = grpNode->sv.Child.first; grp; grp = grp->nextSibling)
		groupMembership.insert(grp->Str);
	const auto *propsNode = grpNode->nextSibling;
	if (!propsNode) return false;
	ClearProps();
	ITERATE_CHILDREN(propsNode, prop) {
		if (prop->type == Save::NT_INT)
			SetPropValue(prop->myName, prop->sv.Int);
		else
			SetPropValue(prop->myName, prop->Str);
	}
	return true;
}

const Group *GameObj::GetGroupWithProp(const std::string &k) const {
	auto *g = Game::Get();
	for (const auto &grpKey: groupMembership) {
		auto *grp = g->GetGroup(grpKey);
		if (grp->HasProp(k)) return grp;
	}
	return nullptr;
}

std::optional<std::string> GameObj::SynthesizeLocationProp(const std::string &k) const {
	// Only meaningful once `relation`/`parent` have been derived. During loading the real property
	// still exists and wins (GetStrProp gates this on !HasProp), so we are never asked mid-load.
	const bool parentIsChar = !parent.empty()
		&& AsCharacter(Game::Get()->TryGetObject(parent)) != nullptr;

	if (AsCharacter(this)) {
		// A character's location: the CharacterLocation StateList and its dependent keys.
		if (k == "CharacterLocation") {
			switch (relation) {
				case HoldingType::AtLocation: return "At Location";
				case HoldingType::InObject:   return "In Object";
				case HoldingType::OnObject:   return parentIsChar ? "On Character" : "On Object";
				default:                      return "Hidden";
			}
		}
		if (k == "CharacterAtLocation") return relation == HoldingType::AtLocation ? parent : "";
		if (k == "CharInsideWhat")      return relation == HoldingType::InObject ? parent : "";
		if (k == "CharOnWhat")          return (relation == HoldingType::OnObject && !parentIsChar) ? parent : "";
		if (k == "CharOnWho")           return (relation == HoldingType::OnObject && parentIsChar) ? parent : "";
		return std::nullopt;
	}

	// An object's location. DynamicLocation applies to dynamic objects, StaticLocation to static;
	// asking for the other one gets nothing, as in ADRIFT (it only stores the applicable one).
	if (k == "DynamicLocation") {
		if (!dynamic) return std::nullopt;
		switch (relation) {
			case HoldingType::InObject: return parentIsChar ? "Held By Character" : "Inside Object";
			case HoldingType::OnObject: return "On Object";
			case HoldingType::Worn:     return "Worn By Character";
			case HoldingType::AtLocation: return "In Location";
			default:                    return "Hidden";
		}
	}
	if (k == "StaticLocation") {
		if (dynamic) return std::nullopt;
		switch (relation) {
			case HoldingType::AtLocation:      return "Single Location";
			case HoldingType::AtLocationGroup: return "Location Group";
			case HoldingType::Everywhere:      return "Everywhere";
			case HoldingType::PartOf:          return parentIsChar ? "Part of Character" : "Part of Object";
			default:                           return "Hidden";
		}
	}
	// The dependent key properties: each holds `parent`, but only while the current relation is the
	// one it belongs to. Asked for in any other state it is not set (we do not keep the stale value
	// ADRIFT would carry over from a previous location).
	if (k == "InLocation")      return (relation == HoldingType::AtLocation && dynamic) ? parent : "";
	if (k == "AtLocation")      return (relation == HoldingType::AtLocation && !dynamic) ? parent : "";
	if (k == "AtLocationGroup") return relation == HoldingType::AtLocationGroup ? parent : "";
	if (k == "HeldByWho")       return (relation == HoldingType::InObject && parentIsChar) ? parent : "";
	if (k == "InsideWhat")      return (relation == HoldingType::InObject && !parentIsChar) ? parent : "";
	if (k == "OnWhat")          return relation == HoldingType::OnObject ? parent : "";
	if (k == "WornByWho")       return relation == HoldingType::Worn ? parent : "";
	if (k == "PartOfWho")       return (relation == HoldingType::PartOf && parentIsChar) ? parent : "";
	if (k == "PartOfWhat")      return (relation == HoldingType::PartOf && !parentIsChar) ? parent : "";
	return std::nullopt;
}

std::string GameObj::GetStrProp(const std::string &k) const {
	// A handful of properties are consumed into `dynamic`/`relation`/`parent` at load time and then
	// erased, but restrictions and expressions may still consult them; synthesize them back rather
	// than storing the redundant strings. Only when the object carries no real value -- during
	// loading itself the real property still exists and must win, since the state it feeds has not
	// been derived from it yet.
	if (!HasProp(k)) {
		if (k == "StaticOrDynamic") return dynamic ? "Dynamic" : "Static";
		if (auto syn = SynthesizeLocationProp(k)) return *syn;
	}
	const Group *grp = GetGroupWithProp(k);
	if (grp != nullptr)
		return grp->GetStrProp(k);
	return PropHolder::GetStrProp(k);
}

int64_t GameObj::GetIntProp(const std::string &k) const {
	const Group *grp = GetGroupWithProp(k);
	if (grp != nullptr)
		return grp->GetIntProp(k);
	return PropHolder::GetIntProp(k);
}

bool GameObj::GetBoolProp(const std::string &k) const {
	const Group *grp = GetGroupWithProp(k);
	if (grp != nullptr)
		return grp->GetBoolProp(k);
	return PropHolder::GetBoolProp(k);
}

const std::unordered_map<std::string, std::string> &GameObj::GetAllStrProps() const {
	auto g = Game::Get();
	// Nothing to merge in for an object in no group -- which is most of them -- so hand back our
	// own table rather than copying it into the cache first.
	if (groupMembership.empty()) return PropHolder::GetAllStrProps();
	hackyStrPropCache = PropHolder::GetAllStrProps();
	// The priority of properties between groups defining the same property is undefined.
	for (const auto &grpKey: groupMembership) {
		auto *grp = g->GetGroup(grpKey);
		for (const auto &kv: grp->GetAllStrProps())
			hackyStrPropCache[kv.first] = kv.second;
	}
	return hackyStrPropCache;
}

const std::unordered_map<std::string, int64_t> &GameObj::GetAllIntProps() const {
	auto g = Game::Get();
	if (groupMembership.empty()) return PropHolder::GetAllIntProps();
	hackyIntPropCache = PropHolder::GetAllIntProps();
	// The priority of properties between groups defining the same property is undefined.
	for (const auto &grpKey: groupMembership) {
		auto *grp = g->GetGroup(grpKey);
		for (const auto &kv: grp->GetAllIntProps())
			hackyIntPropCache[kv.first] = kv.second;
	}
	return hackyIntPropCache;
}

// Mirrors ADRIFT's sRegularExpressionString.
std::string ArticleRegexFragment(const std::string &article) {
	std::string result("(?:");
	if (!article.empty() && Util::ToLower(article) != "the") {
		result += article;
		result += " |";
	}
	result += "the )?";
	return result;
}

std::string PrefixRegexFragment(const std::string &prefix) {
	// Each word of the prefix is separately optional, so "the larger alien's corpse" and the
	// shorter "the larger corpse" both name the same thing. (ADRIFT builds it the same way.)
	std::string result;
	for (const auto &word : Util::SplitString(prefix, " ")) {
		if (word.empty()) continue;
		result += "(?:";
		result += word;
		result += " )?";
	}
	return result;
}

void GameObj::MakeMatchExpr() {
	std::string expr(ArticleRegexFragment(article));
	expr += PrefixRegexFragment(prefix);
	expr += "(?:(?:";
	size_t count = 0;
	for (const auto &n : *nouns) {
		if (++count != 1)
			expr += "|";
		expr += n;
	}
	expr += ") ?)+";
	matchRegex = std::make_shared<const std::regex>(expr, std::regex_constants::icase);
}

}