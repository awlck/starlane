#include "gameobj.h"

#include <stdexcept>
#include <string.h>

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
		result->nouns.emplace_back(it.child_value());
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
    }

	return result;
}

std::string GameObj::GetDisplayName(bool defArt) const {
	std::string result;
	if (!article.empty()) {
		result = defArt ? "the" : article;
		result += ' ';
	}
	if (!prefix.empty()) {
		result += prefix;
		result += ' ';
	}
	result += nouns[0];
	return result;
}

const std::string &GameObj::GetLocationKey() const {
	if (parent.empty()) return parent;
	const GameObj *o = this;
	Game *theGame = Game::Get();
	while (!(o = theGame->GetObject(o->parent))->parent.empty());
	return o->parent;
}

Location *GameObj::GetLocation() const {
	const std::string &lkey = GetLocationKey();
	if (lkey.empty()) return nullptr;
	return dynamic_cast<Location *>(Game::Get()->GetObject(lkey));
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
	parent = newParent == "Hidden" ? "" : newParent;
	relation = newRelation;
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
	return Game::Get()->GetDescription(description)->Build(forDisplay);
}

std::string GameObj::GetListOfChildren(GameObj::ChildFilter f1, GameObj::ChildRelFilter f2, bool recurse) const {
	std::string result;
	size_t count = 0;
	auto *g = Game::Get();
	for (const auto &obj: g->GetAllObjects()) {
		if (obj.second->GetParentKey() != key) continue;
		if (f1 == ChildFilter::Objects && dynamic_cast<Character *>(obj.second))
			continue;
		if (f1 == ChildFilter::Characters && !dynamic_cast<Character *>(obj.second))
			continue;
		switch (f2) {
			case ChildRelFilter::On:
				if (obj.second->relation != HoldingType::OnObject)
					continue;
				break;
			case ChildRelFilter::In:
				if (obj.second->relation != HoldingType::InObject)
					continue;
				break;
			case ChildRelFilter::OnAndIn:
				if (obj.second->relation != HoldingType::InObject && obj.second->relation != HoldingType::OnObject)
					continue;
				break;
		}
		if (count++ > 0)
			result += '|';
		result += obj.first;

		if (recurse) {
			std::string tmp = obj.second->GetListOfChildren(f1, f2, true);
			if (!tmp.empty()) {
				result += '|';
				result += tmp;
			}
		}
	}
	return result;
}

void GameObj::WriteState(Save::Writer &writer) {
	writer.WriteKV("parent", parent);
	writer.WriteKV("dynamic", dynamic);
	writer.WriteKV("holding_type", (int) relation);
	writer.WriteKV("groups", groupMembership);
	writer.BeginNamedCompound("properties");
	const auto &intProps = GetAllIntProps();
	for (const auto &p: intProps)
		writer.WriteKV(p.first.c_str(), p.second);
	const auto &strProps = GetAllStrProps();
	for (const auto &p: strProps)
		writer.WriteKV(p.first.c_str(), p.second);
	writer.EndCompound();
}

}