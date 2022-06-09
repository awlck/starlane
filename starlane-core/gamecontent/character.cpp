#include "character.h"

#include <pugixml.hpp>

#include "../game.h"

namespace Starlane {

Character *Character::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Character;
	result->MakeCommonValues(xmlNode);
	result->properName = xmlNode.child_value("Name");
	result->description = Game::Get()->CreateDescFromXML(xmlNode.child("Description"));

	const auto &cl = xmlNode.select_node("//Property[Key=\"CharacterLocation\"]/Value").node();
	auto ht = ParseHoldingType(cl.child_value());
	result->relation = ht.first;
	std::string nextProp(ht.second);
	if (result->relation != GameObj::HoldingType::Hidden)
		result->parent = xmlNode.select_node(("//Property[Key=\"" + nextProp + "\"]/Value").c_str()).node().child_value();

	// TODO: Walks

	return result;
}

GameObj *Character::Clone() const {
	return new Character(*this);
}

}