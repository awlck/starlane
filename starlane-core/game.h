#pragma once

#ifndef SLC_GAME_H
#define SLC_GAME_H

#include "slc_private.h"

#include <deque>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <string_view>

#include "gamecontent/task.h"

namespace Starlane {

template<typename K, typename V> V *SafeMapGet(std::unordered_map<K, V *> map, const K &key) {
	auto f = map.find(key);
	return f == map.end() ? nullptr : f->second;
}

enum class ReferralPerson {
	FirstPerson,
	SecondPerson,
	ThirdPerson
};

enum class ExecutionPolicy {
	HighestPrio,
	HighestPrioPassing
};

class GameStatic {
	std::string gameTitle;
	std::string gameAuthor;
	std::string gameAdriftVersion;
	std::string gameLastUpdated;
	std::string gameStatusLine;
	bool showFirstLocation = true;
	bool showExits = true;
	DescrRef gameIntro = 0;

	// immutable content (only exists once)
	std::unordered_map<RestrRef, Restriction *> restrictions;
	std::unordered_map<std::string, Property *> properties;
	std::unordered_map<std::string, Task *> tasks;
	std::unordered_map<std::string, std::string> varNames;
	std::unordered_map<ExprRef, Expression *> expressions;
	std::unordered_map<std::string, UserFunction *> userFunctions;
	std::unordered_map<std::string, std::string> userFuncNames;
	std::unordered_map<std::string, Synonym *> synonyms;
	std::unordered_map<std::string, TextOverride *> textOverrides;
	// Snippets of "Plain text", simple strings that do not contain any expressions
	// and can be output as-is. Maintained like this to reduce the amount of text
	// that is unnecessarily duplicated when copying descriptions for undo/save.
	std::unordered_map<PlainTextRef, const char *> plainTextSnippets;
	// The grammatical person by which to refer to the player character
	ReferralPerson pcReferralPerson = ReferralPerson::SecondPerson;
	ExecutionPolicy executionPolicy = ExecutionPolicy::HighestPrio;
	// The mapping of file path -> Blorb resource ID, if applicable.
	std::unordered_map<std::string, uint32_t> blorbResMap;

	// Tasks in priority order
	std::set<Task *, TaskPrioLess> prioOrderedTasks;

	friend class Game;
public:
	~GameStatic();
};

class Game {
public:
	// Gets the current game instance
	static inline Game *Get() { return theGame; }

	static Game *LoadFromXML(const std::string &gameTxt);
	DescrRef CreateDescFromXML(const pugi::xml_node &descNode);
	RestrRef CreateRestrictionsFromXML(const pugi::xml_node &restrNode);
	PlainTextRef StorePlainTextSnippet(const std::string &snip);
	PlainTextRef StorePlainTextSnippet(std::string_view snip);
	ExprRef CreateExpression(const std::string &expr);

	Description *GetDescription(DescrRef d) const { return descriptions.at(d); }
	Event *GetEvent(const std::string &key) { return SafeMapGet(events, key); }
	Group *GetGroup(const std::string &key) { return SafeMapGet(groups, key); }
	GameObj *GetObject(const std::string &key) { return SafeMapGet(objects, key); }
	const Property *GetPropMeta(const std::string &key) const { return SafeMapGet(staticData->properties, key); }
	const Restriction *GetRestriction(RestrRef key) const { return staticData->restrictions.at(key); }
	Variable *GetVariable(const std::string &key) { return SafeMapGet(variables, key); }
	Variable *GetVarByName(const std::string &name) { auto f = staticData->varNames.find(name); return f == staticData->varNames.end() ? nullptr : variables.at(f->second); }
	const UserFunction *GetUserFunction(const std::string &key) { return SafeMapGet(staticData->userFunctions, key); }
	const UserFunction *GetUserFuncByName(const std::string &name) { auto f = staticData->userFuncNames.find(name); return f == staticData->userFuncNames.end() ? nullptr : staticData->userFunctions.at(f->second); }
	Expression *GetExpression(ExprRef ref) { return staticData->expressions.at(ref); }
	const char *GetPlainTextSnippet(PlainTextRef ref) { return staticData->plainTextSnippets.at(ref); }

	bool GroupExists(const std::string &key) const { return groups.find(key) != groups.end(); }
	bool ObjectExists(const std::string &key) const { return objects.find(key) != objects.end(); }
	bool PropExists(const std::string &key) const { return staticData->properties.find(key) != staticData->properties.end(); }
	bool VarOfNameExists(const std::string &name) const { return staticData->varNames.find(name) != staticData->varNames.end(); }
	const std::unordered_map<std::string, GameObj *> &GetAllObjects() const { return objects; }
	GameObj *GetPlayerChar() const { return objects.at(playerKey); }

	bool GetIsTaskCompleted(const std::string &key) { return taskCompletedStorage.at(key); }
	void SetTaskCompleted(const std::string &key, bool val) { taskCompletedStorage[key] = val; }
	// Get an object referred to by the input.
	const std::string &GetReference(const std::string &rk) {
		if (rk == "%Player%" || rk == "Player") return playerKey;
		// This will insert a new element into the map if there is no such entry.
		// Not ideal, but there can only ever be twenty or so references, so I think it's OK.
		return currentRefs[rk];
	}
	// Determine if this reference holds anything.
	bool RefExists(const std::string &rk) const {
		if (rk == "%Player%") return true;
		return currentRefs.count(rk) > 0;
	}

	// Some references are not part of the game, but are used internally to pass some extra
	// context from one part of the engine to another.
	void SetInternalReference(const std::string &ref, const std::string &val) {
		currentRefs["<" + ref + ">"] = val;
	}
	// ...and those will need to be frequently cleared as well
	void ClearInternalReference(const std::string &ref) {
		currentRefs["<" + ref + ">"] = "";
	}

	// Save the current game state to the undo list.
	void SaveUndo();
	// If any undo states are available, discard the current state and go back one step.
	bool RestoreUndo();
	// Discard the oldest saved game state.
	// Does nothing if there currently aren't any undo states.
	static void DiscardUndo();
	// Is there at least one undo state available?
	bool UndoAvailable() const { return !undoStates.empty(); }
	// Restart the game.
	void Restart();

	// This function must be called to start the game. It will start relevant events and
	// output the initial batch of text.
	void Begin();
	// This function should be called once per second to advance real-time-based events.
	void Tick();
	// Save the game. Called by the action-processing machinery when the player types SAVE.
	bool Save();
	// Restore a saved game.
	bool Restore();

	ReferralPerson GetCurrentReferralPerson() const {
		if (mostRecentlyMentioned.first.empty() || mostRecentlyMentioned.first == playerKey)
			return staticData->pcReferralPerson;
		return ReferralPerson::ThirdPerson;
	}
	ReferralPerson GetPCReferralPerson() const { return staticData->pcReferralPerson; }
	const std::pair<std::string, Pronoun> &GetMostRecentlyMentioned() const { return mostRecentlyMentioned; }
	void MentionCharacter(const std::string &key, Pronoun p) { mostRecentlyMentioned = {key, p}; }

	const std::string &GetTitle() const { return staticData->gameTitle; }
	const std::string &GetAuthor() const { return staticData->gameAuthor; }
	const std::string &GetLastUpdated() const { return staticData->gameLastUpdated; }

	bool IsGameOngoing() const { return gameHasBegun; }
	uint32_t GetBlorbResource(const std::string &path) const {
		auto f = staticData->blorbResMap.find(path);
		if (f == staticData->blorbResMap.cend()) return -1;
		return f->second;
	}

	// Submit player input for processing
	void ProcessInput(const std::string &s);

private:
	Game() = default;
	Game(const Game &);  // copy constructor -- for undo state saving
	~Game();

	void CreateObjFromXML(const pugi::xml_node &objNode);
	void CreatePropertyFromXML(const pugi::xml_node &propNode);
	Task *CreateTaskFromXML(const pugi::xml_node &taskNode);
	void CreateEventFromXML(const pugi::xml_node &evtNode);
	void CreateVariableFromXML(const pugi::xml_node &varNode);
	void CreateGroupFromXML(const pugi::xml_node &grpNode);
	void CreateFunctionFromXML(const pugi::xml_node &funcNode);
	void CreateSynonymFromXML(const pugi::xml_node &synoNode);
	void CreateTextOverrideFromXML(const pugi::xml_node &toNode);

    void StartupSanityCheck() const;

	bool ContinueRestore(const Save::AstNode *node);
	bool RollbackRestore();

	// Command parser related internals
	std::string ApplySynonyms(std::string s);
	// Find the general task (if any) whose command matches currentCommand and which is either
	// eligible to run or eligible to fail with a message, trying tasks in priority order.
	// On return, `eligible` holds that task's (tentative) eligibility result, and currentRefs
	// holds the references captured from the matched command.
	Task *FindMatchingTask(std::pair<bool, DescrRef> &eligible);
	// Resolve a single reference's raw matched text (e.g. "the sword") to the keys of all
	// currently known game objects of the given family ("object"/"character"/etc.) that it
	// could refer to. Matches in the narrowest non-empty scope win: objects currently
	// visible to the player beat objects merely seen before, which beat everything else.
	std::vector<std::string> MatchListForReference(const std::string &from, const std::string &refFamily) const;
	// Populate currentRefs from a command match, given the task's reference names for that
	// particular command (e.g. "%direction%", "%object1%") and the corresponding capture groups.
	// Returns false if some reference could not be resolved to anything (e.g. an %object%
	// referring to an object that doesn't exist), meaning the task cannot apply after all.
	bool CaptureReferences(const std::vector<std::string> &refSpecs, const std::smatch &matches);
	bool AttemptMatchSystemCommand();
	void OutputFiltered(std::string s) const;

	// mutable game state (objects copied for undo state)
	std::unordered_map<std::string, GameObj *> objects;
	std::unordered_map<std::string, Event *> events;
	std::unordered_map<std::string, Variable *> variables;
	std::unordered_map<std::string, Group *> groups;
	std::unordered_map<DescrRef, Description *> descriptions;
	// stores the completed-ness of tasks to avoid needing to copy the entire tasks for saves.
	std::unordered_map<std::string, bool> taskCompletedStorage;
	// the current player character
	std::string playerKey;
	// most recently mentioned character and pronoun
	std::pair<std::string, Pronoun> mostRecentlyMentioned;

	// static data lives here for performance and memory usage reasons:
	const GameStatic *staticData;

	// transient storage -- only relevant while evaluating commands.
	// Never needs to be retained for UNDO/SAVE.
	std::unordered_map<std::string, std::string> currentRefs;
	std::string currentCommand;

	// used at load-time to prevent duplicating expressions too much
	std::unordered_map<std::string, ExprRef> knownExprs;

	bool gameHasBegun = false;

	size_t descriptionsSoFar = 0;
	size_t restrictionsSoFar = 0;
	ptrdiff_t textSnippetsSoFar = 0;
	ptrdiff_t expressionsSoFar = 0;

	// The Game instance holding the current state of the game, for the benefit of any
	// functions that might need it (restrictions, descriptions, action processing)
	static Game *theGame;
	// The list of former game states maintained for use with the UNDO command.
	static std::deque<Game *> undoStates;
	// The initial state right as the game starts. Maintained for the benefit of the
	// `restart` command.
	static Game *startupState;

	static ReferralPerson ParseReferralPerson(const char *p);
};

}

#endif  // !SLC_GAME_H