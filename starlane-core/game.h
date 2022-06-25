#pragma once

#ifndef SLC_GAME_H
#define SLC_GAME_H

#include "slc_private.h"

#include <deque>
#include <string>
#include <unordered_map>

namespace Starlane {

class Game {
public:
	// Gets the current game instance
	static inline Game *Get() { return theGame; }

	static Game *LoadFromXML(const std::string &gameTxt);
	DescrRef CreateDescFromXML(const pugi::xml_node &descNode);
	void CreateObjFromXML(const pugi::xml_node &objNode);
	void CreatePropertyFromXML(const pugi::xml_node &propNode);
	RestrRef CreateRestrictionsFromXML(const pugi::xml_node &restrNode);
	void CreateTaskFromXML(const pugi::xml_node &taskNode);
	void CreateEventFromXML(const pugi::xml_node &evtNode);
	void CreateVariableFromXML(const pugi::xml_node &varNode);
	void CreateGroupFromXML(const pugi::xml_node &grpNode);

	Description *GetDescription(DescrRef d) { return descriptions.at(d); }
	Event *GetEvent(const std::string &key) { return events.at(key); }
	Group *GetGroup(const std::string &key) { return groups.at(key); }
	GameObj *GetObject(const std::string &key) { return objects.at(key); }
	const Property *GetPropMeta(const std::string &key) const { return properties.at(key); }
	const Restriction *GetRestriction(RestrRef key) const { return restrictions.at(key); }
	Variable *GetVariable(const std::string &key) { return variables.at(key); }
	Variable *GetVarByName(const std::string &name) { return variables.at(varNames.at(name)); }

	bool GroupExists(const std::string &key) const { return groups.count(key) > 0; }
	bool ObjectExists(const std::string &key) const { return objects.count(key) > 0; }
	const std::unordered_map<std::string, GameObj *> &GetAllObjects() const { return objects; }

	bool GetIsTaskCompleted(const std::string &key) { return taskCompletedStorage.at(key); }
	void SetTaskCompleted(const std::string &key, bool val) { taskCompletedStorage[key] = val; }

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

private:
	Game() = default;
	Game(const Game &);  // copy constructor -- for undo state saving
	~Game();

    void StartupSanityCheck() const;

	// mutable game state (objects copied for undo state)
	std::unordered_map<std::string, GameObj *> objects;
	std::unordered_map<std::string, Event *> events;
	std::unordered_map<std::string, Variable *> variables;
	std::unordered_map<std::string, Group *> groups;
	std::unordered_map<DescrRef, Description *> descriptions;
	// stores the completed-ness of tasks to avoid needing to copy the entire tasks for saves.
	std::unordered_map<std::string, bool> taskCompletedStorage;

	// immutable content (only exists once)
	std::unordered_map<RestrRef, Restriction *> restrictions;
	std::unordered_map<std::string, Property *> properties;
	std::unordered_map<std::string, Task *> tasks;
	std::unordered_map<std::string, std::string> varNames;

	bool gameHasBegun = false;

	std::string gameTitle;
	std::string gameAuthor;
	std::string gameAdriftVersion;
	std::string gameStatusLine;
	bool showFirstLocation = true;
	bool showExits = true;
	DescrRef gameIntro = 0;

	size_t descriptionsSoFar = 0;
	size_t restrictionsSoFar = 0;

	// The Game instance holding the current state of the game, for the benefit of any
	// functions that might need it (restrictions, descriptions, action processing)
	// [fun fact: static data members need to be declared `inline`, otherwise they function
	//  like an `extern` global declaration]
	inline static Game *theGame = nullptr;
	// The list of former game states maintained for use with the UNDO command.
	inline static std::deque<Game *> undoStates;
	// The initial state right as the game starts. Maintained for the benefit of the
	// `restart` command.
	Game *startupState = nullptr;
};

}

#endif  // !SLC_GAME_H