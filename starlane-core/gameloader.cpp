#include "game.h"

#include <pugixml.hpp>

#include "starlane-core.h"
#include "valueparsers.h"
#include "gamecontent/description.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/property.h"
#include "gamecontent/restriction.h"
#include "gamecontent/variable.h"
#include "gamecontent/task.h"

namespace Starlane {

Game *Game::LoadFromXML(const std::string &gameTxt) {
	pugi::xml_document doc;
	auto parseResult = doc.load_string(gameTxt.c_str());
	if (parseResult.status != pugi::status_ok) {
		SLFrontend::FatalError((std::string("Unable to load game: ") + parseResult.description()).c_str());
		return nullptr;
	}
	auto gameNode = doc.child("Adventure");

	auto result = new Game;
	result->gameTitle = gameNode.child_value("Title");
	result->gameAuthor = gameNode.child_value("Author");
	result->gameAdriftVersion = gameNode.child_value("Version");
	result->gameStatusLine = gameNode.child_value("UserStatus");
	result->showFirstLocation = ParseBool(gameNode.child("ShowFirstLocation").child_value());
	result->showExits = ParseBool(gameNode.child("ShowExits").child_value());
	result->gameIntro = result->CreateDescFromXML(gameNode.child("Introduction"));
	Game::theGame = result;

	// It is important that all properties are created before anything tries to use them.
	for (const auto &it: gameNode.children("Property"))
		result->CreatePropertyFromXML(it);

	for (const auto &it : gameNode.children("Location"))
		result->CreateObjFromXML(it);
	for (const auto &it : gameNode.children("Character"))
		result->CreateObjFromXML(it);
	for (const auto &it: gameNode.children("Object"))
		result->CreateObjFromXML(it);

	for (const auto &it: gameNode.children("Task"))
		result->CreateTaskFromXML(it);

	for (const auto &it : gameNode.children("Event"))
		result->CreateEventFromXML(it);
	
	for (const auto &it : gameNode.children("Variable"))
		result->CreateVariableFromXML(it);

	for (const auto &it : gameNode.children("Group"))
		result->CreateGroupFromXML(it);

	return result;
}

size_t Game::CreateDescFromXML(const pugi::xml_node &descNode) {
	descriptions[++descriptionsSoFar] = Description::CreateFromXML(this, descNode);
	return descriptionsSoFar;
}

void Game::CreateObjFromXML(const pugi::xml_node &objNode) {
	auto result = GameObj::CreateFromXML(objNode);
	objects[result->Key()] = result;
}

void Game::CreatePropertyFromXML(const pugi::xml_node &propNode) {
	auto result = Property::CreateFromXML(propNode);
	properties[result->Key()] = result;
}

size_t Game::CreateRestrictionsFromXML(const pugi::xml_node &restrNode) {
	restrictions[++restrictionsSoFar] = Restriction::CreateFromXML(this, restrNode);
	return restrictionsSoFar;
}

void Game::CreateTaskFromXML(const pugi::xml_node &propNode) {
	auto result = Task::CreateFromXML(this, propNode);
	tasks[result->Key()] = result;
}

void Game::CreateEventFromXML(const pugi::xml_node &evtNode) {

}

void Game::CreateVariableFromXML(const pugi::xml_node &varNode) {
	auto result = Variable::CreateFromXML(varNode);
	variables[result->Key()] = result;
	varNames[result->Name()] = result->Key();
}

void Game::CreateGroupFromXML(const pugi::xml_node &grpNode) {

}

}  // namespace Starlane