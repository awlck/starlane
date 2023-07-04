#include "location.h"

#include <pugixml.hpp>

#include "../game.h"
#include "description.h"
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

}