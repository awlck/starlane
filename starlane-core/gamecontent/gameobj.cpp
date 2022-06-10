#include "gameobj.h"

#include <stdexcept>
#include <string.h>

#include <pugixml.hpp>

#include "../game.h"
#include "character.h"
#include "location.h"
#include "group.h"

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
		result->nouns.emplace_back(it.child_value());
	result->description = Game::Get()->CreateDescFromXML(xmlNode.child("Description"));

    // Extract location data from properties (this is faster than directly navigating the XML tree),
    // taking care to clean up the no-longer-needed properties after ourselves to conserve memory.
    std::string nextProp;
    if (result->GetPropValue<std::string>("StaticOrDynamic") == "Dynamic") {
        nextProp = "DynamicLocation";
        result->dynamic = true;
    } else {
        nextProp = "StaticLocation";
    }
    result->ErasePropValue("StaticOrDynamic");
    auto ht = ParseHoldingType(result->GetPropValue<std::string>(nextProp).c_str());
    result->ErasePropValue(nextProp);
    result->relation = ht.first;
    nextProp = ht.second;
    if (!nextProp.empty()) {
        result->parent = result->GetPropValue<std::string>(nextProp);
        result->ErasePropValue(nextProp);
    }

	return result;
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

}