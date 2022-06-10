#include "character.h"

#include <pugixml.hpp>

#include "../game.h"

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

GameObj *Character::Clone() const {
	return new Character(*this);
}

}