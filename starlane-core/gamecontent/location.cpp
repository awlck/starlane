#include "location.h"

#include <pugixml.hpp>

#include "../game.h"

namespace Starlane {

Location *Location::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Location;
	auto theGame = Game::Get();
	result->locationName = theGame->CreateDescFromXML(xmlNode.child("ShortDescription"));
	result->description = theGame->CreateDescFromXML(xmlNode.child("LongDescription"));
	return result;
}

GameObj *Location::Clone() const {
	return new Location(*this);
}

}