#include "location.h"

#include <pugixml.hpp>

#include "../game.h"

namespace Starlane {

Location *Location::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Location;
	auto theGame = Game::Get();
	result->locationName = theGame->CreateDescFromXML(xmlNode.child("ShortDescription"));
	result->description = theGame->CreateDescFromXML(xmlNode.child("LongDescription"));
	result->MakeCommonValues(xmlNode);

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

GameObj *Location::Clone() const {
	return new Location(*this);
}

}