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
	result->description = Game::Get()->CreateDescFromXML(xmlNode.child("Description"));

	auto ht = ParseHoldingType(result->GetStrProp("CharacterLocation").c_str());
    result->ErasePropValue("CharacterLocation");
	result->relation = ht.first;
	std::string nextProp(ht.second);
	if (result->relation != GameObj::HoldingType::Hidden) {
        result->parent = result->GetStrProp(nextProp);
        result->ErasePropValue(nextProp);
    }

	// TODO: Walks
    // TODO: Conversation

	result->MakeMatchExpr();
	return result;
}

std::string Character::GetDisplayName(bool defArt) const {
	if (!Game::Get()->PropExists("Known") || GetBoolProp("Known"))
		return GetProperName();
	return GameObj::GetDisplayName(defArt);
}

std::string Character::GetDescription(bool forDisplay) const {
	Game::Get()->SetInternalReference("referral-character", key);
	return GameObj::GetDescription(forDisplay);
	Game::Get()->ClearInternalReference("referral-character");
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
	if (Game::Get()->GroupExists(key)) {
		const Group *grp = Game::Get()->GetGroup(key);
		return std::any_of(grp->GetAllMembers().begin(), grp->GetAllMembers().end(), [this](const auto &o) {
			return CanSee(o);
		});
	}

	const auto &otherCeiling = Game::Get()->GetObject(key)->GetVisbilityCeiling();
	// If the other object's ceiling is a group, we can see it if our own visibility ceiling is also a member of that group.
	if (Game::Get()->GroupExists(otherCeiling)) {
		const Group *grp = Game::Get()->GetGroup(otherCeiling);
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
	for (const auto &o: Game::Get()->GetAllObjects()) {
		// CanSee also covers the location itself: when we stand in a location,
		// that location is our visibility ceiling.
		if (CanSee(o.first))
			MarkSeen(o.first);
	}
}

void Character::MoveTo(const std::string &newParent, HoldingType newRelation) {
	GameObj::MoveTo(newParent, newRelation);
	MarkVisibleAsSeen();
}

void Character::MakePosture(const std::string &newParent, const char *p) {
	MoveTo(newParent, GameObj::HoldingType::OnObject);
	SetPropValue("CharacterPosition", p);
}

std::string Character::GetPossessionsList(Starlane::Character::PossessionFilter pf, bool recurse) const {
	std::string result;
	size_t count = 0;
	auto *g = Game::Get();
	for (const auto &obj: g->GetAllObjects()) {
		if (obj.second->GetParentKey() != key) continue;
		if (pf == PossessionFilter::Worn && obj.second->GetParentRelation() != GameObj::HoldingType::Worn)
			continue;
		if (pf == PossessionFilter::Held && obj.second->GetParentRelation() != GameObj::HoldingType::InObject)
			continue;
		if (count++ > 0)
			result += '|';
		result += obj.first;

		if (recurse) {
			auto tmp = obj.second->GetListOfChildren(GameObj::ChildFilter::All, GameObj::ChildRelFilter::OnAndIn, true);
			if (!tmp.empty()) {
				result += '|';
				result += tmp;
			}
		}
	}
	return result;
}

void Character::WriteState(Save::Writer &writer) const {
	GameObj::WriteState(writer);
	writer.WriteKV("seen", seenStorage);
}

bool Character::RestoreState(const Save::AstNode *node) {
	if (!GameObj::RestoreState(node)) return false;
	const auto *seenNode = node->FindChildByName("seen");
	if (!seenNode) return false;
	seenStorage.clear();
	ITERATE_CHILDREN(seenNode, s) {
		seenStorage.insert(s->Str);
	}
	return true;
}

void Character::MakeMatchExpr() {
	std::string baseExpr("(?:");
	baseExpr += article;
	baseExpr += " )?";
	if (!prefix.empty()) {
		baseExpr += "(?:";
		auto prefixes = Util::SplitString(prefix, " ");
		size_t count = 0;
		for (const auto &pref : prefixes) {
			if (++count != 1)
				baseExpr += "|";
			baseExpr += pref;
		}
		baseExpr += " )*";
	}

	std::string unknownExpr(baseExpr);
	unknownExpr += "(?:(?:";
	size_t count = 0;
	for (const auto &n : nouns) {
		if (++count != 1)
			unknownExpr += "|";
		unknownExpr += n;
	}
	unknownExpr += ") ?)+";
	matchRegex = std::regex(unknownExpr, std::regex_constants::icase);

	std::string knownExpr(baseExpr);
	knownExpr += "(?:(?:";
	count = 0;
	auto properNameComponents = Util::SplitString(properName, " ");
	for (const auto &n : properNameComponents) {
		if (++count != 1)
			knownExpr += "|";
		knownExpr += n;
	}
	knownExpr += ") ?)+";
	matchWhenKnownRegex = std::regex(knownExpr, std::regex_constants::icase);
}

}