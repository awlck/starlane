#include "game.h"

#include <algorithm>

#include "starlane-core.h"
#include "gamecontent/event.h"
#include "expression.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/group.h"
#include "gamecontent/task.h"
#include "gamecontent/property.h"
#include "gamecontent/description.h"
#include "gamecontent/restriction.h"
#include "gamecontent/userfunc.h"
#include "gamecontent/variable.h"
#include "savefiles/parser.h"
#include "savefiles/writer.h"
#include "valueparsers.h"

namespace Starlane {

Game *Game::theGame = nullptr;
std::deque<Game *> Game::undoStates;
Game *Game::startupState = nullptr;

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

	// just bools, so a vector copy is sufficient.
	taskCompletedStorage = rhs.taskCompletedStorage;

	// Finally, the simple data copies.
	playerKey = rhs.playerKey;
	mostRecentlyMentioned = rhs.mostRecentlyMentioned;
	gameHasBegun = rhs.gameHasBegun;
	descriptionsSoFar = rhs.descriptionsSoFar;
	restrictionsSoFar = rhs.restrictionsSoFar;
	textSnippetsSoFar = rhs.textSnippetsSoFar;
	expressionsSoFar = rhs.expressionsSoFar;
	staticData = rhs.staticData;
}

/* Destruct Game instance. This requires a bit of extra attention,
 * since we must not destruct any of the non-modifiable game content
 * (descriptions, restrictions) when we are not the currently-used
 * game state object.
 */
Game::~Game() {
	// destroy mutable game state
	// (C++ fun fact: it is indeed valid to `delete` a `const Foo *`.)
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
		delete staticData;
	}
}

GameStatic::~GameStatic() {
	for (const auto &it: restrictions)
		delete it.second;
	for (const auto &it: properties)
		delete it.second;
	for (const auto &it: tasks)
		delete it.second;
	for (const auto &it: expressions)
		delete it.second;
	for (const auto &it: userFunctions)
		delete it.second;
	for (const auto &it: plainTextSnippets)
		delete it.second;
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
	theGame = new Game(*startupState);
	for (auto i : undoStates)
		delete i;
	delete this;
	theGame->Begin();
}

void Game::Begin() {
	if (!startupState)
		startupState = new Game(*this);
	taskCompletedStorage.reserve(staticData->tasks.size());
	for (const auto &it : staticData->tasks)
		taskCompletedStorage[it.first] = false;
	for (const auto &it : events) {
		if (it.second->GetStartType() == Event::StartType::Immediately)
			it.second->Start();
	}
	gameHasBegun = true;
	if (staticData->gameIntro != 0)
		frontend->OutputText(GetDescription(staticData->gameIntro)->Build().c_str());
}

void Game::Tick() {
	// TODO
}

bool Game::Save() {
	auto hFile = frontend->CreateSaveFile();
	if (!hFile)  // no file -- assume user cancelled
		return false;
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
	return true;
}

bool Game::Restore() {
	auto hFile = frontend->CreateSaveFile();
	if (!hFile)  // no file -- assume user cancelled
		return false;
	Save::Parser sav(hFile);
	sav.Prepare();
	Save::AstNode *root;
	try {
		root = sav.Parse();
	} catch (Save::SaveFileError &e) {
		frontend->OutputText("<i>Restore failed: the selected save file appears to be invalid.</i>");
		return false;
	}
	if (root == nullptr) {
		frontend->OutputText("<i>Restore failed: the selected save file appears to be invalid.</i>");
		return false;
	}
	{
		auto *metaNode = root->FindChildByName("meta");
		if (!metaNode) return false;
		auto *versionNode = metaNode->FindChildByName("version");
		if (!versionNode) return false;
		if (versionNode->sv.Int != Save::currentSaveFileVer) {
			frontend->OutputText("<i>Restore failed: selected save file appears to have been created using a different version of Starlane.</i>");
			return false;
		}
		auto *gameTitleNode = metaNode->FindChildByName("game_title");
		auto *gameAuthorNode = metaNode->FindChildByName("game_author");
		if (!gameTitleNode || !gameAuthorNode) return false;
		if (gameTitleNode->Str != staticData->gameTitle || gameAuthorNode->Str != staticData->gameAuthor) {
			frontend->OutputText("<i>Restore failed: selected save file appears to belong to a different game.</i>");
			return false;
		}
		auto *gameRevNode = metaNode->FindChildByName("game_revision");
		if (!gameRevNode) return false;
		if (gameRevNode->Str != staticData->gameLastUpdated) {
			// todo: offer player the option to attempt restore anyways.
			frontend->OutputText("<i>Restore failed: selected save file appears to belong to a different revision of this game.</i>");
			return false;
		}
	}
	SaveUndo();  // so we can return in the event of a failure once game state has been modified
	return Game::Get()->ContinueRestore(root);  // in the new instance post-save
}

bool Game::ContinueRestore(const Save::AstNode *root) {
	{
		auto *playerNode = root->FindChildByName("player");
		if (!playerNode || playerNode->type != Save::NT_STRING) return RollbackRestore();
		playerKey = playerNode->Str;
	}
	{
		auto *objsNode = root->FindChildByName("objects");
		if (!objsNode || objsNode->type != Save::NT_COMPOUND) return RollbackRestore();
		ITERATE_CHILDREN(objsNode, objN) {
			if (!GetObject(objN->myName)->RestoreState(objN)) return RollbackRestore();
		}
	}
	{
		auto *evtsNode = root->FindChildByName("events");
		if (!evtsNode || evtsNode->type != Save::NT_COMPOUND) return RollbackRestore();
		ITERATE_CHILDREN(evtsNode, evtN) {
			if (!GetEvent(evtN->myName)->RestoreState(evtN)) return RollbackRestore();
		}
	}
	{
		auto *varsNode = root->FindChildByName("variables");
		if (!varsNode || varsNode->type != Save::NT_COMPOUND) return RollbackRestore();
		ITERATE_CHILDREN(varsNode, varN) {
			auto *var = GetVariable(varN->myName);
			size_t counter = 0;
			switch (var->GetType()) {
				case Variable::Type::Int:
				case Variable::Type::IntArray:
					if (varN->type != Save::NT_INTLIST) return RollbackRestore();
					ITERATE_CHILDREN(varN, varV) {
						var->SetValue(varV->sv.Int, ++counter);
					}
					break;
				case Variable::Type::String:
				case Variable::Type::StringArray:
					if (varN->type != Save::NT_STRINGLIST) return RollbackRestore();
					ITERATE_CHILDREN(varN, varV) {
						var->SetValue(varV->Str, ++counter);
					}
					break;
			}
		}
	}
	{
		const auto *grpsNode = root->FindChildByName("groups");
		if (!grpsNode || grpsNode->type != Save::NT_COMPOUND) return RollbackRestore();
		ITERATE_CHILDREN(grpsNode, grpN) {
			if (!GetGroup(grpN->myName)->RestoreState(grpN)) return RollbackRestore();
		}
	}
	{
		const auto *descsNode = root->FindChildByName("descriptions_shown");
		if (!descsNode || descsNode->type != Save::NT_COMPOUND) return RollbackRestore();
		size_t nextDesc;
		size_t lastDesc = 0;
		ITERATE_CHILDREN(descsNode, descN) {
			nextDesc = ParseInt(descN->myName.c_str());
			for (size_t i = lastDesc+1; i < nextDesc; i++)
				descriptions[i]->RestoreState();
			std::vector<bool> state;
			ITERATE_CHILDREN(descN, entry) {
				state.push_back(entry->sv.Bool);
			}
			descriptions[nextDesc]->RestoreState(state);
			lastDesc = nextDesc;
		}
		for (size_t i = lastDesc+1; i < descriptionsSoFar; i++)
			descriptions[i]->RestoreState();
	}
	{
		const auto *tasksCompletedNode = root->FindChildByName("tasks_completed");
		if (!tasksCompletedNode || tasksCompletedNode->type != Save::NT_STRINGLIST) return RollbackRestore();
		for (auto &elem: taskCompletedStorage)
			elem.second = false;
		ITERATE_CHILDREN(tasksCompletedNode, taskNode) {
			taskCompletedStorage[taskNode->myName] = taskNode->sv.Bool;
		}
	}
	// finally, discard all previous undo states because UNDOing a restore would be a bit silly
	while (Game::UndoAvailable())
		Game::DiscardUndo();
	return true;
}

bool Game::RollbackRestore() {
	RestoreUndo();
	frontend->OutputText("<i>Restore failed: selected save file is invalid.</i>");
	return false;
}

}
