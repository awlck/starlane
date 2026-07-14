#include "location.h"

#include <pugixml.hpp>

#include "../game.h"
#include "character.h"
#include "description.h"
#include "group.h"
#include "restriction.h"

namespace Starlane {

Location *Location::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Location;
	auto theGame = Game::Get();
	result->locationName = theGame->CreateDescFromXML(xmlNode.child("ShortDescription"));
	result->description = theGame->CreateDescFromXML(xmlNode.child("LongDescription"));
	result->MakeCommonValues(xmlNode);
	result->relation = HoldingType::Hidden;  // not really, but it also doesn't matter for locations at all.

	for (const auto &m: xmlNode.children("Movement")) {
		std::string direction = m.child_value("Direction");
		std::string destination = m.child_value("Destination");
		RestrRef restrs = 0;
		const auto &r = m.child("Restrictions");
		if (r.type() != pugi::node_null)
			restrs = theGame->CreateRestrictionsFromXML(r);
		result->exits[direction] = { destination, restrs };
	}

	result->MakeMatchExpr();
	return result;
}

std::string Location::GetDisplayName([[maybe_unused]] bool defArt) const {
	if (locationName == (DescrRef) 0)
		return "(BUG: Location without a name.)";
	return Game::Get()->GetDescription(locationName)->Build();
}

GameObj *Location::Clone() const {
	return new Location(*this);
}

// Separate a piece of text being appended to a description from what came before,
// unless the text already ends in whitespace. (The equivalent of pSpace in the
// original implementation, which uses the two-spaces-after-a-sentence convention.)
static void PadForAppend(std::string &s) {
	if (s.empty()) return;
	char last = s.back();
	if (last != ' ' && last != '\n' && last != '\t')
		s += "  ";
}

bool Location::HoldsDirectly(const GameObj *obj) const {
	switch (obj->GetParentRelation()) {
	case HoldingType::AtLocation:
		return obj->GetParentKey() == key;
	case HoldingType::AtLocationGroup: {
		const auto *grp = Game::Get()->GetGroup(obj->GetParentKey());
		return grp && grp->ContainsObj(key);
	}
	case HoldingType::Everywhere:
		return true;
	default:
		return false;
	}
}

std::string Location::GetDescription(bool forDisplay) const {
	auto *theGame = Game::Get();
	std::string result = GameObj::GetDescription(forDisplay);

	// Visible objects are listed after the description proper, mirroring what
	// clsLocation.ViewLocation does in the original implementation: dynamic objects
	// are listed unless explicitly excluded, static objects only when explicitly
	// included. Objects with a list description get it appended verbatim; the
	// remaining listable objects are collected into a single "Also here is ..." /
	// "There is ... here." sentence.
	std::vector<std::string> generalListed;
	for (const auto &objKey: theGame->GetObjectLoadOrder()) {
		const auto *obj = theGame->GetObject(objKey);
		if (dynamic_cast<const Character *>(obj) || dynamic_cast<const Location *>(obj))
			continue;
		if (!HoldsDirectly(obj))
			continue;
		if (obj->IsDynamic() ? obj->GetBoolProp("ExplicitlyExclude") : !obj->GetBoolProp("ExplicitlyList"))
			continue;
		const char *listProp = obj->IsDynamic() ? "ListDescriptionDynamic" : "ListDescription";
		std::string listDesc;
		if (obj->HasProp(listProp))
			listDesc = theGame->GetDescription(obj->GetIntProp(listProp))->Build(forDisplay);
		if (listDesc.empty()) {
			generalListed.push_back(obj->GetDisplayName());
		} else {
			PadForAppend(result);
			result += listDesc;
		}
	}

	if (!generalListed.empty()) {
		std::string list;
		for (size_t i = 0; i < generalListed.size(); i++) {
			if (i > 0)
				list += i == generalListed.size() - 1 ? " and " : ", ";
			list += generalListed[i];
		}
		if (result.empty()) {
			result = "There is " + list + " here.";
		} else {
			PadForAppend(result);
			result += "Also here is " + list + ".";
		}
	}

	return result;
}

std::string Location::GetListOfExits() const {
	std::string result;
	size_t count = 0;
	for (auto &e: exits) {
		// add to result if unrestricted
		if (e.second.restr == 0) {
			if (count++ > 0)
				result += '|';
			result += e.first;
			continue;
		}
		// otherwise check if restriction passes
		const auto *restr = Game::Get()->GetRestriction(e.second.restr);
		if (restr->PassRestrictionBlock().first) {
			if (count++ > 0)
				result += '|';
			result += e.first;
		}
	}
	return result;
}

void Location::MakeMatchExpr() {
	// This expression requires a string to both begin and not begin with the letter x,
	// thus it can never match.
	matchRegex = std::regex("^(?!x)x");
}

}