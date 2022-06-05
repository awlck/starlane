#include "game.h"

#include <iostream>

#include <pugixml.hpp>

#include "valueparsers.h"
#include "gamecontent/description.h"
#include "gamecontent/restriction.h"

namespace Starlane {

Game *Game::LoadFromXML(const std::string &gameTxt) {
	pugi::xml_document doc;
	auto parseResult = doc.load_string(gameTxt.c_str());
	auto theGame = doc.child("Adventure");

	auto result = new Game;
	result->gameTitle = theGame.child("Title").child_value();
	result->gameAuthor = theGame.child("Author").child_value();
	result->gameAdriftVersion = theGame.child("Version").child_value();
	result->gameStatusLine = theGame.child("UserStatus").child_value();
	result->showFirstLocation = ParseBool(theGame.child("ShowFirstLocation").child_value());
	result->showExits = ParseBool(theGame.child("ShowExits").child_value());
	result->gameIntro = result->CreateDescFromXML(theGame.child("Introduction"));

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

}  // namespace Starlane