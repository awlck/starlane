#include "game.h"

#include <iostream>

#include <pugixml.hpp>

namespace Starlane {

Game *Game::LoadFromXML(const std::string &gameTxt) {
	pugi::xml_document doc;
	auto parseResult = doc.load_string(gameTxt.c_str());
	auto theGame = doc.child("Adventure");

	auto result = new Game;
	result->gameTitle = theGame.child("Title").child_value();
	result->gameAuthor = theGame.child("Author").child_value();
	result->gameAdriftVersion = theGame.child("Version").child_value();

	return result;
}

}