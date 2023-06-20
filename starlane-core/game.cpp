#include "game.h"

#include "starlane-core.h"
#include "gamecontent/event.h"
#include "expression.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/group.h"
#include "gamecontent/task.h"
#include "gamecontent/property.h"
#include "gamecontent/description.h"
#include "gamecontent/restriction.h"
#include "gamecontent/variable.h"
#include "savefiles/writer.h"

namespace Starlane {

/* Copy constructor for game instances. Needs to create copies of all
 * the mutable game state objects.
 */
Game::Game(const Game &rhs) {
	objects.reserve(rhs.objects.size());
	// For objects, we also need to respect subclassing...
	for (const auto &it : rhs.objects)
		objects[it.first] = it.second->Clone();
	events.reserve(rhs.events.size());
	for (const auto &it : rhs.events)
		events[it.first] = new Event(*it.second);
	variables.reserve(rhs.variables.size());
	for (const auto &it : rhs.variables)
		variables[it.first] = new Variable(*it.second);
	groups.reserve(rhs.groups.size());
	for (const auto &it : rhs.groups)
		groups[it.first] = new Group(*it.second);
	descriptions.reserve(rhs.descriptions.size());
	for (const auto &it : rhs.descriptions) {
		descriptions[it.first] = new Description(*it.second);
	}

	// For restrictions (immutable), it's enough to copy the references.
	restrictions = rhs.restrictions;
	properties = rhs.properties;
	tasks = rhs.tasks;
	varNames = rhs.varNames;
	userFunctions = rhs.userFunctions;
	userFuncNames = rhs.userFuncNames;
	expressions = rhs.expressions;
	plainTextSnippets = rhs.plainTextSnippets;
	// just bools, so a vector copy is sufficient.
	taskCompletedStorage = rhs.taskCompletedStorage;

	// Finally, the simple data copies.
	playerKey = rhs.playerKey;
	mostRecentlyMentioned = rhs.mostRecentlyMentioned;
	pcReferralPerson = rhs.pcReferralPerson;
	gameHasBegun = rhs.gameHasBegun;
	gameTitle = rhs.gameTitle;
	gameAuthor = rhs.gameAuthor;
	gameAdriftVersion = rhs.gameAdriftVersion;
	gameStatusLine = rhs.gameStatusLine;
	showFirstLocation = rhs.showFirstLocation;
	showExits = rhs.showExits;
	gameIntro = rhs.gameIntro;
	descriptionsSoFar = rhs.descriptionsSoFar;
	restrictionsSoFar = rhs.restrictionsSoFar;
	textSnippetsSoFar = rhs.textSnippetsSoFar;
	expressionsSoFar = rhs.expressionsSoFar;
	blorbResMap = rhs.blorbResMap;
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
	for (const auto &it : events)
		delete it.second;
	for (const auto &it : variables)
		delete it.second;
	for (const auto &it : groups)
		delete it.second;
	for (const auto &it : descriptions)
		delete it.second;

	if (theGame == this) {
		// We are the current (presumably last) game instance -- destroy everything.
		// (Should really only happen when shutting down the interpreter / loading a new game.)
		if (startupState != this)
			delete startupState;
		for (const auto &it : restrictions)
			delete it.second;
		for (const auto &it : properties)
			delete it.second;
		for (const auto &it : tasks)
			delete it.second;
		for (const auto &it : expressions)
			delete it.second;
		for (const auto &it : plainTextSnippets)
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
	taskCompletedStorage.reserve(tasks.size());
	for (const auto &it : tasks)
		taskCompletedStorage[it.first] = false;
	for (const auto &it : events) {
		if (it.second->GetStartType() == Event::StartType::Immediately)
			it.second->Start();
	}
	gameHasBegun = true;
	if (gameIntro != 0)
		frontend->OutputText(GetDescription(gameIntro)->Build().c_str());
}

void Game::Tick() {
	// TODO
}

void Game::Save() {
	auto hFile = frontend->CreateSaveFile();
	if (!hFile)  // no file -- assume user cancelled
		return;
	Save::Writer writer(hFile, this);
	writer.WriteKV("player", playerKey);

	writer.BeginNamedCompound("objects");
	for (const auto &obj: objects) {
		writer.BeginNamedCompound(obj.first.c_str());
		obj.second->WriteState(writer);
		writer.EndCompound();
	}
	writer.EndCompound();

	writer.BeginNamedCompound("events");
	for (const auto &evt: events) {
		writer.BeginNamedCompound(evt.first.c_str());
		evt.second->WriteState(writer);
		writer.EndCompound();
	}
	writer.EndCompound();

	writer.BeginNamedCompound("variables");
	for (const auto &var: variables) {
		if (!var.second->GetEverChanged()) continue;
		switch (var.second->GetType()) {
		case Variable::Type::Int:
		case Variable::Type::IntArray:
			writer.WriteKV(var.first.c_str(), var.second->GetIntArray());
			break;
		case Variable::Type::String:
		case Variable::Type::StringArray:
			writer.WriteKV(var.first.c_str(), var.second->GetStrArray());
			break;
		}
	}
	writer.EndCompound();

	writer.BeginNamedCompound("groups");
	// TODO: only save groups with at least one property of their own?
	for (const auto &grp: groups) {
		writer.BeginNamedCompound(grp.first.c_str());
		grp.second->WriteState(writer);
		writer.EndCompound();
	}
	writer.EndCompound();

	writer.BeginNamedCompound("descriptions_shown");
	// TODO: save only those where at least one segment even cares?
	for (const auto &desc: descriptions) {
		auto name = std::to_string(desc.first);
		auto state = desc.second->GetState();
		// "not shown" is the initial state, so only save anything for those descriptions where at least one segment has been shown.
		if (std::find(state.cbegin(), state.cend(), true) != state.cend())  // at least one true
			writer.WriteKV(name.c_str(), state);
	}
	writer.EndCompound();

	writer.BeginNamedCompound("tasks_completed", true);
	for (const auto &state: taskCompletedStorage) {
		if (state.second) {
			writer.WriteLiteralString(state.first.c_str());
			writer.WriteUnqouted(" ");
		}
	}
	writer.WriteUnqouted("}");  // sneaky! (Avoiding the trailing space added by `EndCompound`)
}

}