#include "gameobj.h"

#include <stdexcept>
#include <string.h>

#include <pugixml.hpp>

#include "../game.h"
#include "character.h"
#include "location.h"
#include "group.h"

#define KEYEQ(node, val) (strcmp((node).child_value("Key"), (val)) == 0)

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
		result->nouns.emplace_back(xmlNode.child_value());
	result->description = Game::Get()->CreateDescFromXML(xmlNode.child("Description"));

	// Handle location-related properties separately:
	std::string nextProp;
	const auto &sod = xmlNode.select_node("//Property[Key=\"StaticOrDynamic\"]/Value").node();
	if (STREQ(sod.child_value(), "Static")) {
		const auto &sl = xmlNode.select_node("//Property[Key=\"StaticLocation\"]/Value").node();
		auto ht = ParseHoldingType(sl.child_value());
		result->relation = ht.first;
		nextProp = ht.second;
	} else if (STREQ(sod.child_value(), "Dynamic")) {
		const auto &dl = xmlNode.select_node("//Property[Key=\"DynamicLocation\"]/Value").node();
		auto ht = ParseHoldingType(dl.child_value());
		result->relation = ht.first;
		nextProp = ht.second;
		result->dynamic = true;
	} else throw std::runtime_error(std::string("Unknown staticness: ") + sod.child_value());

	switch (result->relation) {
		case HoldingType::Hidden:
		case HoldingType::Everywhere:
			break;
		/*case HoldingType::AtLocation:
			if (result->dynamic)
				result->parent = xmlNode.select_node("//Property[Key=\"InLocation\"]/Value").node().child_value();
			else
				result->parent = xmlNode.select_node("//Property[Key=\"AtLocation\"]/Value").node().child_value();
			break;
		case HoldingType::AtLocationGroup:
			result->parent = xmlNode.select_node("//Property[Key=\"AtLocationGroup\"]/Value").node().child_value();
			break;
		case HoldingType::InObject:
			result->parent = xmlNode.select_node("//Property[Key=\"InsideWhat\"]/Value").node().child_value();
			break;
		case HoldingType::OnObject:
			result->parent = xmlNode.select_node("//Property[Key=\"OnWhat\"]/Value").node().child_value();
			break;
		case HoldingType::Worn:
			result->parent = xmlNode.select_node("//Property[Key=\"WornByWho\"]/Value").node().child_value();
			break;
		case HoldingType::PartOf:
			const auto &t = xmlNode.select_node("//Property[Key=\"PartOfWhat\"]/Value");
			if (t.node().type() != pugi::node_null)
				result->parent = t.node().child_value();
			else
				result->parent = xmlNode.select_node("//Property[Key=\"PartOfWho\"]/Value").node().child_value();
			break;*/
		default:
			result->parent = xmlNode.select_node(("//Property[Key=\"" + nextProp + "\"]/Value").c_str()).node().child_value();
			break;
	}

	return result;
}

void GameObj::MakeCommonValues(const pugi::xml_node &xmlNode) {
	key = xmlNode.child_value("Key");
	article = xmlNode.child_value("Article");
	prefix = xmlNode.child_value("Prefix");

	// Now the remaining properties, carefully ignoring the location-related ones we've already handled.
	for (const auto &prop: xmlNode.children("Property")) {
		if (KEYEQ(prop, "StaticOrDynamic") || KEYEQ(prop, "StaticLocation") || KEYEQ(prop, "DynamicLocation")
			    || KEYEQ(prop, "InLocation") || KEYEQ(prop, "AtLocation") || KEYEQ(prop, "InsideWhat")
			    || KEYEQ(prop, "OnWhat") || KEYEQ(prop, "WornByWho") || KEYEQ(prop, "PartOfWhat")
			    || KEYEQ(prop, "PartOfWho") || KEYEQ(prop, "CharacterAtLocation") || KEYEQ(prop, "CharInsideWhat")
				|| KEYEQ(prop, "CharOnWhat") || KEYEQ(prop, "CharOnWho"))
			continue;

		SetPropValueFromXML(prop);
	}
}

GameObj *GameObj::Clone() const {
	return new GameObj(*this);
}

}