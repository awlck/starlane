#include "game.h"

#include <iostream>

#include <pugixml.hpp>

#include "starlane-core.h"
#include "valueparsers.h"
#include "gamecontent/description.h"
#include "gamecontent/property.h"
#include "gamecontent/restriction.h"

namespace Starlane {

Game *Game::LoadFromXML(const std::string &gameTxt) {
	pugi::xml_document doc;
	auto parseResult = doc.load_string(gameTxt.c_str());
	if (parseResult.status != pugi::status_ok) {
		SLFrontend::FatalError((std::string("Unable to load game: ") + parseResult.description()).c_str());
		return nullptr;
	}
	auto theGame = doc.child("Adventure");

	auto result = new Game;
	result->gameTitle = theGame.child_value("Title");
	result->gameAuthor = theGame.child_value("Author");
	result->gameAdriftVersion = theGame.child_value("Version");
	result->gameStatusLine = theGame.child_value("UserStatus");
	result->showFirstLocation = ParseBool(theGame.child("ShowFirstLocation").child_value());
	result->showExits = ParseBool(theGame.child("ShowExits").child_value());
	result->gameIntro = result->CreateDescFromXML(theGame.child("Introduction"));

	for (const auto &it: theGame.children("Property"))
		result->CreatePropertyFromXML(it);

	return result;
}

size_t Game::CreateDescFromXML(const pugi::xml_node &descNode) {
	descriptions[++descriptionsSoFar] = Description::CreateFromXML(this, descNode);
	return descriptionsSoFar;
}

size_t Game::CreateRestrictionsFromXML(const pugi::xml_node &restrNode) {
	restrictions[++restrictionsSoFar] = Restriction::CreateFromXML(this, restrNode);
	return restrictionsSoFar;
}

void Game::CreatePropertyFromXML(const pugi::xml_node &propNode) {
	auto result = Property::CreateFromXML(propNode);
	properties[result->Name()] = result;
}

}  // namespace Starlane