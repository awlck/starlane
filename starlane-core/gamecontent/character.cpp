#include "character.h"

#include <algorithm>
#include <pugixml.hpp>

#include "../game.h"
#include "group.h"
#include "location.h"
#include "restriction.h"
#include "../savefiles/writer.h"

namespace Starlane {

Character *Character::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Character;
	result->MakeCommonValues(xmlNode);
	result->properName = xmlNode.child_value("Name");
	// A character's parser nouns are its descriptors ("me", "myself", "guard"); unlike an object,
	// its <Name> is the proper name, handled separately.
	for (const auto &it : xmlNode.children("Descriptor"))
		result->nouns.emplace_back(it.child_value());
	result->description = Game::Get()->CreateDescFromXML(xmlNode.child("Description"));

	auto ht = ParseHoldingType(result->GetStrProp("CharacterLocation").c_str());
    result->ErasePropValue("CharacterLocation");
	result->relation = ht.first;
	std::string nextProp(ht.second);
	if (result->relation != GameObj::HoldingType::Hidden) {
        result->parent = result->GetStrProp(nextProp);
        result->ErasePropValue(nextProp);
        // Resolve a "%Player%" container reference to the player's key up front, as GameObj does.
        if (Util::IsReference(result->parent))
            result->parent = Game::Get()->GetReference(result->parent);
    }

	for (const auto &w : xmlNode.children("Walk"))
		result->walks.push_back(Walk::CreateFromXML(w, result->key));
    // TODO: Conversation

	result->CompileNameExpressions();
	result->MakeMatchExpr();
	return result;
}

std::string Character::GetDisplayName(bool defArt) const {
	if (!Game::Get()->PropExists("Known") || GetBoolProp("Known"))
		return GetProperName();
	return GameObj::GetDisplayName(defArt);
}

bool Character::MatchesNameWord(const std::string &word) const {
	if (GameObj::MatchesNameWord(word)) return true;
	return Util::ContainsWholeWord(properName, word);
}

std::string Character::GetDescription(bool forDisplay) const {
	Game::Get()->SetInternalReference("referral-character", key);
	auto result = GameObj::GetDescription(forDisplay);
	Game::Get()->ClearInternalReference("referral-character");
	return result;
}

void Character::SetPropValue(const std::string &key, const std::string &value) {
	if (key == "CharacterProperName") {
		properName = value;
		MakeMatchExpr();
	}
	GameObj::SetPropValue(key, value);
}

std::pair<bool, DescrRef> Character::HasRoute(const std::string &dir) const {
	auto *loc = GetLocation();
	if (!loc || !loc->HasExit(dir))
		return { false, 0 };
	const auto &exit = loc->GetExit(dir);
	if (exit.restr == 0)
		return { true, 0 };
	return Game::Get()->GetRestriction(exit.restr)->PassRestrictionBlock();
}

bool Character::CanSee(const std::string &key) const {
	const auto &myCeiling = GetVisbilityCeiling();

	// By definition we can't see anything when we're hidden.
	if (myCeiling.empty()) return false;

	// seeing a group means seeing any member of the group
	if (const auto *grp = Game::Get()->GetGroup(myCeiling); grp) {
		const auto &allMembers = grp->GetAllMembers();
		return std::any_of(allMembers.cbegin(), allMembers.cend(),[this](const auto &o) {
			return CanSee(o);
		});
	}

	const auto &otherCeiling = Game::Get()->GetObject(key)->GetVisbilityCeiling();
	// If the other object's ceiling is a group, we can see it if our own visibility ceiling is also a member of that group.
	if (const auto *grp = Game::Get()->GetGroup(otherCeiling); grp) {
		return grp->GetAllMembers().count(myCeiling) > 0;
	}

	// Otherwise, we can see the object if our visibility ceilings are the same,
	// or otherwise if that object is our visibility ceiling. (The latter case allows
	// the player to open the container they're in.)
	return myCeiling == otherCeiling || myCeiling == key;
}

GameObj *Character::Clone() const {
	return new Character(*this);
}

void Character::MarkVisibleAsSeen() {
	if (GetVisbilityCeiling().empty()) return;  // hidden characters see nothing
	MarkSeen(key);  // we can always see ourselves
	const auto &objs = Game::Get()->GetAllObjects();
	std::for_each(objs.begin(), objs.end(), [this](const auto &o) {
		// CanSee also covers the location itself: when we stand in a location,
		// that location is our visibility ceiling.
		if (CanSee(o.first))
			MarkSeen(o.first);
	});
}

void Character::MoveTo(const std::string &newParent, HoldingType newRelation) {
	// Compared by location rather than by parent: getting out of a chair changes what holds the
	// player without moving them anywhere, and arriving is what a System task waits for.
	// Copied, not referenced -- the move below is about to change what it names.
	const std::string cameFrom = GetLocationKey();
	GameObj::MoveTo(newParent, newRelation);
	MarkVisibleAsSeen();
	// Only the player's arrival triggers anything, and only a real change of location counts.
	// Compared by key rather than by asking for the player object, which would throw if anything
	// ever moved a character before the game had picked one.
	auto *g = Game::Get();
	if (key != g->GetPlayerKey()) return;
	const std::string &arrivedAt = GetLocationKey();
	if (arrivedAt != cameFrom)
		g->NotePlayerArrived(arrivedAt);
}

void Character::MakePosture(const std::string &newParent, const char *p) {
	MoveTo(newParent, GameObj::HoldingType::OnObject);
	SetPropValue("CharacterPosition", p);
}

std::string Character::GetPossessionsList(Starlane::Character::PossessionFilter pf, bool recurse) const {
	std::string result;
	size_t count = 0;
	auto *g = Game::Get();
	// Load order, not hash order: this list is shown to the player. See GetListOfChildren.
	for (const auto &objKey: g->GetObjectLoadOrder()) {
		GameObj *obj = g->GetObject(objKey);
		if (!obj || obj->GetParentKey() != key) continue;
		if (pf == PossessionFilter::Worn && obj->GetParentRelation() != GameObj::HoldingType::Worn)
			continue;
		if (pf == PossessionFilter::Held && obj->GetParentRelation() != GameObj::HoldingType::InObject)
			continue;
		if (count++ > 0)
			result += '|';
		result += objKey;

		if (recurse) {
			auto tmp = obj->GetListOfChildren(GameObj::ChildFilter::All, GameObj::ChildRelFilter::OnAndIn, true);
			if (!tmp.empty()) {
				result += '|';
				result += tmp;
			}
		}
	}
	return result;
}

void Character::RegisterWalkNotifications() const {
	for (int32_t i = 0; i < (int32_t) walks.size(); i++)
		walks[i].RegisterNotifications(i);
}

void Character::TickWalks() {
	// A walk's sub-walk can run a task that ends the game; stop ticking the moment it does, as the
	// event loop and ADRIFT's own walk loop both do.
	for (auto &w : walks) {
		if (!Game::Get()->IsGameOngoing()) return;
		w.IncrementTimer();
	}
}

void Character::StartActiveWalks() {
	// Forced: this is game start-up placing the walk into its running state, not a task asking it to
	// begin, so there is no tick to defer to.
	for (auto &w : walks)
		if (w.IsStartActive())
			w.Start(/*force =*/ true);
}

void Character::NotifyWalk(int32_t idx, Util::Control::Condition cond, const std::string &taskKey) {
	if (idx >= 0 && idx < (int32_t) walks.size())
		walks[idx].ReceiveTaskNotification(cond, taskKey);
}

void Character::WriteState(Save::Writer &writer) const {
	GameObj::WriteState(writer);
	writer.WriteKV("seen", seenStorage);
	writer.BeginNamedCompound("walks");
	for (size_t i = 0; i < walks.size(); i++) {
		writer.BeginNamedCompound(std::to_string(i).c_str());
		walks[i].WriteState(writer);
		writer.EndCompound();
	}
	writer.EndCompound();
}

bool Character::RestoreState(const Save::AstNode *node) {
	if (!GameObj::RestoreState(node)) return false;
	const auto *seenNode = node->FindChildByName("seen");
	if (!seenNode) return false;
	seenStorage.clear();
	ITERATE_CHILDREN(seenNode, s) {
		seenStorage.insert(s->Str);
	}
	const auto *walksNode = node->FindChildByName("walks");
	if (!walksNode) return false;
	for (int32_t i = 0; i < (int32_t) walks.size(); i++) {
		const auto *w = walksNode->FindChildByName(std::to_string(i).c_str());
		if (!w || !walks[i].RestoreState(w)) return false;
	}
	return true;
}

void Character::MakeMatchExpr() {
	std::string baseExpr(ArticleRegexFragment(article));
	baseExpr += PrefixRegexFragment(prefix);

	// A character answers to its descriptors ("me", "myself", "old man") whether or not it is
	// known; being known only adds the proper name on top. A character with no descriptors at all
	// answers to its proper name regardless, or it could not be referred to.
	auto buildExpr = [&](const std::vector<std::string> &names) {
		std::string expr(baseExpr);
		expr += "(?:(?:";
		size_t count = 0;
		for (const auto &n : names) {
			if (++count != 1)
				expr += "|";
			expr += n;
		}
		expr += ") ?)+";
		return std::regex(expr, std::regex_constants::icase);
	};

	auto properNameComponents = Util::SplitString(properName, " ");
	std::vector<std::string> unknownNames(nouns);
	if (unknownNames.empty())
		unknownNames = properNameComponents;
	matchRegex = buildExpr(unknownNames);

	std::vector<std::string> knownNames(nouns);
	knownNames.insert(knownNames.end(), properNameComponents.begin(), properNameComponents.end());
	matchWhenKnownRegex = buildExpr(knownNames);
}

}