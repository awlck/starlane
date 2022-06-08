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

	GameObj *GetObject(const std::string &key);

	// Save the current game state to the undo list.
	void SaveUndo();
	// If any undo states are available, discard the current state and go back one step.
	bool RestoreUndo();
	// Discard the oldest saved game state.
	// Does nothing if there currently aren't any undo states.
	void DiscardUndo();

private:
	Game() = default;
	Game(const Game &);  // copy constructor -- for undo state saving
	~Game();

	// mutable game state (objects copied for undo state)
	std::unordered_map<std::string, GameObj *> objects;
	std::unordered_map<std::string, Task *> tasks;  // tasks can be set or unset
	std::unordered_map<DescrRef, Description *> descriptions;  // can be shown or not shown
	std::unordered_map<std::string, Event *> events;
	std::unordered_map<std::string, Variable *> variables;
	std::unordered_map<std::string, Group *> groups;

	// immutable content (only exists once)
	std::unordered_map<RestrRef, Restriction *> restrictions;
	std::unordered_map<std::string, Property *> properties;

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
};

}

#endif  // !SLC_GAME_H