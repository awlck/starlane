#pragma once

#ifndef SLC_GAME_H
#define SLC_GAME_H

#include "slc_private.h"

#include <deque>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace Starlane {

template<typename K, typename V> V *SafeMapGet(std::unordered_map<K, V *> map, const K &key) {
	auto f = map.find(key);
	return f == map.end() ? nullptr : f->second;
}

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

	Description *GetDescription(DescrRef d) { return descriptions.at(d); }
	Event *GetEvent(const std::string &key) { return SafeMapGet(events, key); }
	Group *GetGroup(const std::string &key) { return SafeMapGet(groups, key); }
	GameObj *GetObject(const std::string &key) { return SafeMapGet(objects, key); }
	const Property *GetPropMeta(const std::string &key) const { return SafeMapGet(properties, key); }
	const Restriction *GetRestriction(RestrRef key) const { return restrictions.at(key); }
	Variable *GetVariable(const std::string &key) { return SafeMapGet(variables, key); }
	Variable *GetVarByName(const std::string &name) { auto f = varNames.find(name); return f == varNames.end() ? nullptr : variables.at(f->second); }
	const UserFunction *GetUserFunction(const std::string &key) { return SafeMapGet(userFunctions, key); }
	const UserFunction *GetUserFuncByName(const std::string &name) {auto f = userFuncNames.find(name); return f == userFuncNames.end() ? nullptr : userFunctions.at(f->second); }
	Expression *GetExpression(ExprRef ref) { return expressions.at(ref); }
	const char *GetPlainTextSnippet(PlainTextRef ref) { return plainTextSnippets.at(ref); }

	bool GroupExists(const std::string &key) const { return groups.find(key) != groups.end(); }
	bool ObjectExists(const std::string &key) const { return objects.find(key) != objects.end(); }
	bool PropExists(const std::string &key) const { return properties.find(key) != properties.end(); }
	bool VarOfNameExists(const std::string &name) const { return varNames.find(name) != varNames.end(); }
	const std::unordered_map<std::string, GameObj *> &GetAllObjects() const { return objects; }
	GameObj *GetPlayerChar() const { return objects.at(playerKey); }

	bool GetIsTaskCompleted(const std::string &key) { return taskCompletedStorage.at(key); }
	void SetTaskCompleted(const std::string &key, bool val) { taskCompletedStorage[key] = val; }
	// Get an object referred to by the input.
	const std::string &GetReference(const std::string &rk) {
		if (rk == "%Player%") return playerKey;
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
	void DiscardUndo();
	// Is there at least one undo state avaiable?
	bool UndoAvailable() const { return !undoStates.empty(); }
	// Restart the game.
	void Restart();

	// This function must be called to start the game. It will start relevant events and
	// output the initial batch of text.
	void Begin();
	// This function should be called once per second to advance real-time-based events.
	void Tick();
	// Save the game. Called by the action-processing machinery when the player types SAVE.
	void Save();

	enum class ReferralPerson {
		FirstPerson,
		SecondPerson,
		ThirdPerson
	};
	ReferralPerson GetCurrentReferralPerson() const {
		if (mostRecentlyMentioned.first.empty() || mostRecentlyMentioned.first == playerKey)
			return pcReferralPerson;
		return ReferralPerson::ThirdPerson;
	}
	ReferralPerson GetPCReferralPerson() const { return pcReferralPerson; }
	const std::pair<std::string, Pronoun> &GetMostRecentlyMentioned() const { return mostRecentlyMentioned; }
	void MentionCharacter(const std::string &key, Pronoun p) { mostRecentlyMentioned = {key, p}; }

	const std::string &GetTitle() const { return gameTitle; }
	const std::string &GetAuthor() const { return gameAuthor; }
	const std::string &GetLastUpdated() const { return gameLastUpdated; }

	bool IsGameOngoing() const { return gameHasBegun; }

private:
	Game() = default;
	Game(const Game &);  // copy constructor -- for undo state saving
	~Game();

	void CreateObjFromXML(const pugi::xml_node &objNode);
	void CreatePropertyFromXML(const pugi::xml_node &propNode);
	void CreateTaskFromXML(const pugi::xml_node &taskNode);
	void CreateEventFromXML(const pugi::xml_node &evtNode);
	void CreateVariableFromXML(const pugi::xml_node &varNode);
	void CreateGroupFromXML(const pugi::xml_node &grpNode);
	void CreateFunctionFromXML(const pugi::xml_node &funcNode);

    void StartupSanityCheck() const;

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

	// immutable content (only exists once)
	std::unordered_map<RestrRef, Restriction *> restrictions;
	std::unordered_map<std::string, Property *> properties;
	std::unordered_map<std::string, Task *> tasks;
	std::unordered_map<std::string, std::string> varNames;
	std::unordered_map<ExprRef, Expression *> expressions;
	std::unordered_map<std::string, UserFunction *> userFunctions;
	std::unordered_map<std::string, std::string> userFuncNames;
	// Snippets of "Plain text", simple strings that do not contain any expressions
	// and can be output as-is. Maintained like this to reduce the amount of text
	// that is unnecessarily duplicated when copying descriptions for undo/save.
	std::unordered_map<PlainTextRef, const char *> plainTextSnippets;
	// The grammatical person by which to refer to the player character
	ReferralPerson pcReferralPerson = ReferralPerson::SecondPerson;

	// transient storage -- only relevant while evaluating commands.
	// Never needs to be retained for UNDO/SAVE.
	std::unordered_map<std::string, std::string> currentRefs;

	// used at load-time to prevent duplicating expressions too much
	std::unordered_map<std::string, ExprRef> knownExprs;

	bool gameHasBegun = false;

	std::string gameTitle;
	std::string gameAuthor;
	std::string gameAdriftVersion;
	std::string gameLastUpdated;
	std::string gameStatusLine;
	bool showFirstLocation = true;
	bool showExits = true;
	DescrRef gameIntro = 0;

	size_t descriptionsSoFar = 0;
	size_t restrictionsSoFar = 0;
	size_t textSnippetsSoFar = 0;
	size_t expressionsSoFar = ((size_t) 1) << (std::numeric_limits<size_t>::digits-1);

	std::unordered_map<std::string, size_t> blorbResMap;

	// The Game instance holding the current state of the game, for the benefit of any
	// functions that might need it (restrictions, descriptions, action processing)
	// [fun fact: static data members need to be declared `inline`, otherwise they function
	//  like an `extern` global declaration]
	inline static Game *theGame = nullptr;
	// The list of former game states maintained for use with the UNDO command.
	inline static std::deque<Game *> undoStates;
	// The initial state right as the game starts. Maintained for the benefit of the
	// `restart` command.
	inline static Game *startupState = nullptr;

	static ReferralPerson ParseReferralPerson(const char *p);
};

}

#endif  // !SLC_GAME_H