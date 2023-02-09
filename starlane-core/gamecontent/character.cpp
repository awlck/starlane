#include "character.h"

#include <algorithm>
#include <pugixml.hpp>

#include "../game.h"
#include "group.h"
#include "location.h"
#include "restriction.h"

namespace Starlane {

Character *Character::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Character;
	result->MakeCommonValues(xmlNode);
	result->properName = xmlNode.child_value("Name");
	result->description = Game::Get()->CreateDescFromXML(xmlNode.child("Description"));

	auto ht = ParseHoldingType(result->GetPropValue<std::string>("CharacterLocation").c_str());
    result->ErasePropValue("CharacterLocation");
	result->relation = ht.first;
	std::string nextProp(ht.second);
	if (result->relation != GameObj::HoldingType::Hidden) {
        result->parent = result->GetPropValue<std::string>(nextProp);
        result->ErasePropValue(nextProp);
    }

	// TODO: Walks
    // TODO: Conversation

	return result;
}

std::string Character::GetDisplayName() const {
	if (!Game::Get()->PropExists("Known") || GetPropValue<bool>("Known"))
		return GetProperName();
	return GameObj::GetDisplayName();
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

void Character::MakePosture(const std::string &newParent, Posture p) {
	MoveTo(newParent, GameObj::HoldingType::OnObject);
	posture = p;
}

}