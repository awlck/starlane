#include "game.h"

#include <iterator>

#include <pugixml.hpp>

#include "starlane-core.h"
#include "valueparsers.h"
#include "gamecontent/description.h"
#include "gamecontent/event.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/group.h"
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

    result->StartupSanityCheck();
    return result;
}

DescrRef Game::CreateDescFromXML(const pugi::xml_node &descNode) {
	// On 32-bit systems, the maximum number of description objects that can be handled
	// is some 4.3 billion. It's very unlikely that any game will ever run into this
	// limitation (unless programmatically generated specifically to test Starlane),
	// but it's perhaps best to be cautiuous.
	if (descriptionsSoFar == std::numeric_limits<DescrRef>::max()) {
		SLFrontend::FatalError("My brain just exploded: This game has more text than I know how to deal with.");
		return 0;
	}
	descriptions[++descriptionsSoFar] = Description::CreateFromXML(descNode);
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

RestrRef Game::CreateRestrictionsFromXML(const pugi::xml_node &restrNode) {
	if (restrictionsSoFar == std::numeric_limits<RestrRef>::max()) {
		SLFrontend::FatalError("My brain just exploded: This game has more restrictions than I know how to deal with.");
		return 0;
	}
	restrictions[++restrictionsSoFar] = Restriction::CreateFromXML(restrNode);
	return restrictionsSoFar;
}

void Game::CreateTaskFromXML(const pugi::xml_node &propNode) {
	auto result = Task::CreateFromXML(this, propNode);
	tasks[result->Key()] = result;
}

void Game::CreateEventFromXML(const pugi::xml_node &evtNode) {
	auto result = Event::CreateFromXML(evtNode);
	events[result->Key()] = result;
}

void Game::CreateVariableFromXML(const pugi::xml_node &varNode) {
	auto result = Variable::CreateFromXML(varNode);
	variables[result->Key()] = result;
	varNames[result->Name()] = result->Key();
}

void Game::CreateGroupFromXML(const pugi::xml_node &grpNode) {
    auto result = Group::CreateFromXML(grpNode);
    groups[result->Key()] = result;
}

void Game::StartupSanityCheck() const {
    size_t sanityCheck = std::distance(descriptions.begin(), descriptions.end());
    if (sanityCheck != descriptionsSoFar || sanityCheck != descriptions.size() || descriptionsSoFar != descriptions.size())
        SLFrontend::FatalError("Startup sanity check failed: description count mismatch.");
    sanityCheck = std::distance(restrictions.begin(), restrictions.end());
    if (sanityCheck != restrictionsSoFar || sanityCheck != restrictions.size() || restrictionsSoFar != restrictions.size())
        SLFrontend::FatalError("Startup sanity check failed: restriction count mismatch.");

	// `DescrRef` is a `size_t`, but the Mechanus interpreter deals in `int64_t`.
	// This means that, on 64-bit systems, we can create descriptions that Mechanus
	// can't reference. Abort if this is the case.
	// (Still, that's over 9 quintillion -- 9x10^18 -- description objects. It is very
	//  unlikely that any games will ever run into this.)
	if (descriptionsSoFar >= std::numeric_limits<int64_t>::max())
		SLFrontend::FatalError("My brain just exploded: This game has more text than I know how to deal with.");
}

}  // namespace Starlane