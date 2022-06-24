#include "game.h"

#include "gamecontent/event.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/group.h"
#include "gamecontent/task.h"
#include "gamecontent/property.h"
#include "gamecontent/description.h"
#include "gamecontent/restriction.h"
#include "gamecontent/variable.h"

namespace Starlane {

/* Copy constructor for game instances. Needs to create copies of all
 * the mutable game state objects.
 */
Game::Game(const Game &rhs) {
	// For objects, we also need to respect subclassing...
	for (const auto &it : rhs.objects)
		objects[it.first] = it.second->Clone();
	for (const auto &it : rhs.events)
		events[it.first] = new Event(*it.second);
	for (const auto &it : rhs.variables)
		variables[it.first] = new Variable(*it.second);
	for (const auto &it : rhs.groups)
		groups[it.first] = new Group(*it.second);
	for (const auto &it : rhs.descriptions) {
		descriptions[it.first] = new Description(*it.second);
	}

	// For restrictions (immutable), it's enough to copy the references.
	restrictions = rhs.restrictions;
	properties = rhs.properties;
	tasks = rhs.tasks;
	varNames = rhs.varNames;
	// just bools, so a vector copy is sufficient.
	taskCompletedStorage = rhs.taskCompletedStorage;

	// Finally, the simple data copies.
	gameTitle = rhs.gameTitle;
	gameAuthor = rhs.gameAuthor;
	gameAdriftVersion = rhs.gameAdriftVersion;
	gameStatusLine = rhs.gameStatusLine;
	showFirstLocation = rhs.showFirstLocation;
	showExits = rhs.showExits;
	gameIntro = rhs.gameIntro;
	descriptionsSoFar = rhs.descriptionsSoFar;
	restrictionsSoFar = rhs.restrictionsSoFar;
}

/* Destruct Game instance. This requires a bit of extra attention,
 * since we must not destruct any of the non-modifiable game content
 * (descriptions, restrictions) when we are not the currently-used
 * game state object.
 */
Game::~Game() {
	// destroy mutable game state
	for (const auto &it : objects)
		delete it.second;
	for (const auto &it : properties)
		delete it.second;
	for (const auto &it : events)
		delete it.second;
	for (const auto &it : variables)
		delete it.second;
	for (const auto &it : groups)
		delete it.second;

	if (theGame == this) {
		// We are the current (presumably last) game instance -- destroy everything.
		// (Should really only happen when shutting down the interpreter / loading a new game.)
		for (const auto &it : restrictions)
			delete it.second;
		for (const auto &it : descriptions)
			delete it.second;
		for (const auto &it : tasks)
			delete it.second;
	}
}

void Game::SaveUndo() {
	auto storedGame = new Game(*this);
	undoStates.push_back(storedGame);
}

bool Game::RestoreUndo() {
	if (undoStates.empty())
		return false;
	auto gameToBe = undoStates.back();
	theGame = gameToBe;
	undoStates.pop_back();
	delete this;
	return true;
}

void Game::DiscardUndo() {
	if (undoStates.empty())
		return;
	auto gameToDelete = undoStates.front();
	delete gameToDelete;
	undoStates.pop_front();
}

void Game::Restart() {
	theGame = startupState;
	for (auto i : undoStates)
		delete i;
	delete this;
	theGame->Begin();
}

void Game::Begin() {
	if (!startupState)
		startupState = new Game(*this);
	for (const auto &it: tasks)
		taskCompletedStorage[it.first] = false;
	for (const auto &it: events) {
		if (it.second->GetStartType() == Event::StartType::Immediately)
			it.second->Start();
	}
	gameHasBegun = true;
}

void Game::Tick() {
	// TODO
}

}