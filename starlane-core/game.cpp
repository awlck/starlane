#include "game.h"

#include <algorithm>

#include "starlane-core.h"
#include "gamecontent/character.h"
#include "gamecontent/event.h"
#include "expression.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/group.h"
#include "gamecontent/task.h"
#include "gamecontent/property.h"
#include "gamecontent/description.h"
#include "gamecontent/restriction.h"
#include "gamecontent/synonym.h"
#include "gamecontent/textoverride.h"
#include "gamecontent/userfunc.h"
#include "gamecontent/variable.h"
#include "savefiles/parser.h"
#include "savefiles/writer.h"
#include "valueparsers.h"

namespace Starlane {

Game *Game::theGame = nullptr;
std::deque<Game *> Game::undoStates;
Game *Game::startupState = nullptr;
bool Game::inputInFlight = false;

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
	turnCount = rhs.turnCount;
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
	for (const auto &it: synonyms)
		delete it.second;
	for (const auto &it: textOverrides)
		delete it.second;
}

const std::string &Game::GetPlayerLocationKey() const {
	return GetPlayerChar()->GetLocationKey();
}

void Game::SaveUndo() {
	auto storedGame = new Game(*this);
	undoStates.push_back(storedGame);
	while (undoStates.size() > kMaxUndoStates)
		DiscardUndo();
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
	// The states belonged to the game that just ended: leaving the (now dangling) pointers in
	// the list would have the next UNDO reach straight into freed memory.
	undoStates.clear();
	delete this;
	theGame->Begin();
}

void Game::Begin() {
	if (!startupState)
		startupState = new Game(*this);
	taskCompletedStorage.reserve(staticData->tasks.size());
	for (const auto &it : staticData->tasks)
		taskCompletedStorage[it.first] = false;
	// Must come after taskCompletedStorage is populated above: an immediate, zero-length event
	// fires its subevents from inside Start(), those run tasks, and a task asking whether it has
	// already been completed would find nothing to read.
	for (const auto &key : staticData->eventLoadOrder) {
		Event *evt = events.at(key);
		switch (evt->GetStartType()) {
			case Event::StartType::TaskBased:
				evt->SetNotYetStarted();
				break;
			case Event::StartType::AfterDelay:
				evt->BeginCountdown();
				break;
			case Event::StartType::Immediately:
				// Forced: there is no event loop to be inside of yet, and an event told to start
				// immediately should not have to wait a turn for it.
				evt->Start(/*force =*/ true);
				break;
			case Event::StartType::Invalid:
				// "<WhenStart>0</WhenStart>", which ADRIFT's own enum has no value for and which
				// 36 events across the test games nonetheless carry. Taken to mean the event
				// never runs.
				break;
		}
	}
	// Deliberately a second pass, as in ADRIFT. Start() leaves the "started on this tick" flag
	// set so that an event doesn't also age on the tick it started; carried out of load and into
	// the first real tick, that would have every immediate event sit out turn one.
	for (const auto &key : staticData->eventLoadOrder)
		events.at(key)->ClearJustStarted();
	// Every character has "seen" their initial surroundings.
	for (const auto &it : objects) {
		if (auto *c = dynamic_cast<Character *>(it.second))
			c->MarkVisibleAsSeen();
	}
	gameHasBegun = true;
	if (staticData->gameIntro != 0)
		frontend->OutputText(GetDescription(staticData->gameIntro)->Build().c_str());
}

void Game::TurnTick() {
	// An event's subevent can run a task, and that task can carry a "skip N turns" action, which
	// ticks straight back through here. Depth-limited rather than trusted: a game that manages to
	// tie that knot should misbehave, not take the interpreter down with it. (ADRIFT has no such
	// guard and simply exhausts its stack.)
	static int depth = 0;
	if (depth >= kMaxTickDepth) return;
	depth++;
	turnCount += 1;
	RunEventTick(false);
	depth--;
}

void Game::Tick() {
	// See `inputInFlight`: a command is part-way through and its own turn tick hasn't happened
	// yet, so the world must not move underneath it.
	if (inputInFlight) return;
	RunEventTick(true);
	// Deliberately no SaveUndo(): a real-time tick is the world acting on its own, not the player
	// taking a turn, and ADRIFT doesn't snapshot on one either. UNDO after a few seconds' thought
	// should rewind the last thing the player *did*, not the seconds they spent thinking -- and
	// snapshotting here would grow the undo stack while they sat idle.
}

void Game::RunEventTick(bool realTime) {
	// The game can end part-way through a tick -- an event runs a task that wins or loses it --
	// and the rest of that tick's events then don't happen. ADRIFT bails the same way.
	if (!gameHasBegun) return;
	// TODO: character walks advance here, ahead of the events.
	// A "skip N turns" action can land us back in here while an outer tick is still going, so
	// remember rather than assume what to put back.
	const bool wasRunning = eventsRunning;
	eventsRunning = true;
	for (const auto &key : staticData->eventLoadOrder) {
		if (!gameHasBegun) break;
		Event *evt = events.at(key);
		if (evt->IsRealTime() == realTime) evt->IncrementTimer();
	}
	// Deliberately a second pass over the same events, as in ADRIFT. An event late in the order
	// can run a task that starts one earlier in the order, which has already had its tick; if the
	// "don't age on the turn you started" flag survived into the next tick, that event would sit
	// out a turn it ought to have counted.
	for (const auto &key : staticData->eventLoadOrder) {
		Event *evt = events.at(key);
		if (evt->IsRealTime() == realTime) evt->ClearJustStarted();
	}
	eventsRunning = wasRunning;
}

bool Game::Save() {
	auto hFile = frontend->CreateSaveFile();
	if (!hFile)  // no file -- assume user cancelled
		return false;
	Save::Writer writer(hFile, this);
	writer.WriteKV("player", playerKey);
	writer.WriteKV("turns", turnCount);

	writer.BeginNamedCompound("objects");
	for (const auto &obj: objects) {
		writer.BeginNamedCompound(obj.first.c_str());
		obj.second->WriteState(writer);
		writer.EndCompound();
	}
	writer.EndCompound();

	writer.BeginNamedCompound("events");
	// In load order rather than whatever the map hands us, so that two saves of the same game
	// state produce the same bytes.
	for (const auto &key: staticData->eventLoadOrder) {
		writer.BeginNamedCompound(key.c_str());
		events.at(key)->WriteState(writer);
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
	auto hFile = frontend->OpenSaveFile();
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
		auto *turnsNode = root->FindChildByName("turns");
		if (!turnsNode || turnsNode->type != Save::NT_INT) return RollbackRestore();
		turnCount = (uint32_t) turnsNode->sv.Int;
	}
	{
		auto *objsNode = root->FindChildByName("objects");
		if (!objsNode || !objsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		ITERATE_CHILDREN(objsNode, objN) {
			// A save file naming something this game hasn't got is not one of ours.
			auto *obj = GetObject(objN->myName);
			if (!obj || !obj->RestoreState(objN)) return RollbackRestore();
		}
	}
	{
		auto *evtsNode = root->FindChildByName("events");
		if (!evtsNode || !evtsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		ITERATE_CHILDREN(evtsNode, evtN) {
			auto *evt = GetEvent(evtN->myName);
			if (!evt || !evt->RestoreState(evtN)) return RollbackRestore();
		}
	}
	{
		auto *varsNode = root->FindChildByName("variables");
		if (!varsNode || !varsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		ITERATE_CHILDREN(varsNode, varN) {
			auto *var = GetVariable(varN->myName);
			if (!var) return RollbackRestore();
			size_t counter = 0;
			switch (var->GetType()) {
				case Variable::Type::Int:
				case Variable::Type::IntArray:
					if (!varN->IsCollection(Save::NT_INTLIST)) return RollbackRestore();
					ITERATE_CHILDREN(varN, varV) {
						var->SetValue(varV->sv.Int, ++counter);
					}
					break;
				case Variable::Type::String:
				case Variable::Type::StringArray:
					if (!varN->IsCollection(Save::NT_STRINGLIST)) return RollbackRestore();
					ITERATE_CHILDREN(varN, varV) {
						var->SetValue(varV->Str, ++counter);
					}
					break;
			}
		}
	}
	{
		const auto *grpsNode = root->FindChildByName("groups");
		if (!grpsNode || !grpsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		ITERATE_CHILDREN(grpsNode, grpN) {
			auto *grp = GetGroup(grpN->myName);
			if (!grp || !grp->RestoreState(grpN)) return RollbackRestore();
		}
	}
	{
		const auto *descsNode = root->FindChildByName("descriptions_shown");
		if (!descsNode || !descsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		// Only descriptions with at least one segment shown get written out, so reset the lot
		// to "not shown" and let the file fill in the exceptions. The entries are written out
		// of an unordered_map, so they arrive in no particular order -- hence resetting up
		// front rather than filling the gaps between consecutive entries as we go.
		for (const auto &desc: descriptions)
			desc.second->RestoreState();
		ITERATE_CHILDREN(descsNode, descN) {
			auto desc = descriptions.find(ParseInt(descN->myName.c_str()));
			if (desc == descriptions.end()) return RollbackRestore();  // no such description
			std::vector<bool> state;
			ITERATE_CHILDREN(descN, entry) {
				state.push_back(entry->sv.Bool);
			}
			desc->second->RestoreState(state);
		}
	}
	{
		const auto *tasksCompletedNode = root->FindChildByName("tasks_completed");
		if (!tasksCompletedNode || !tasksCompletedNode->IsCollection(Save::NT_STRINGLIST)) return RollbackRestore();
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

void Game::OutputFiltered(std::string s) const {
	std::string initialText;
	do {
		initialText = s;
		for (const auto &it: staticData->textOverrides) {
			const auto &f = it.second->GetFrom();
			size_t pos;
			while ((pos = s.find(f)) != std::string::npos) {
				std::string replacement(GetDescription(it.second->GetReplacement())->Build());
				s.replace(pos, f.size(), replacement);
			}
		}
	} while (initialText != s);
	frontend->OutputText(s.c_str());
}

}
