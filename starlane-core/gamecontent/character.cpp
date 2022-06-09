#include "character.h"

#include <pugixml.hpp>

#include "../game.h"

namespace Starlane {

Character *Character::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Character;
	result->MakeCommonValues(xmlNode);
	result->properName = xmlNode.child_value("Name");
	result->description = Game::Get()->CreateDescFromXML(xmlNode.child("Description"));
	return result;
}

GameObj *Character::Clone() const {
	return new Character(*this);
}

}