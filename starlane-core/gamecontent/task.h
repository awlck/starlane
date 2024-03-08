#pragma once

#ifndef SLC_TASK_H
#define SLC_TASK_H

#include "../slc_private.h"

#include <regex>
#include <string>
#include <vector>

#include "utility.h"

namespace Starlane {

class Task {
public:
	static Task *CreateFromXML(Game *g, const pugi::xml_node &xmlNode);

	enum class Type {
		General,
		Specific,
		System
	};
	static Type ParseType(const char *txt);
	Type GetType() const { return type; }

	struct OverrideType {
		static constexpr int Override = 0;  // execute the specific task only
		static constexpr int ParentText = 0b0001;  // also show parent's text
		static constexpr int ParentActions = 0b0010;  // also run parent's action
		static constexpr int BeforeParent = 0b0100;  // run this task before the specified parent text/actions
		static constexpr int AfterParent = 0b1000;  // run this task after the specified parent text/actions
		OverrideType(uint8_t val) : v(val) {}
		uint8_t v;
	};
	static OverrideType ParseOverrideType(const char *txt);

	[[nodiscard]] const std::string &Key() const { return key; }
	bool Completed() const;
	void Uncomplete();

	// Tentatively decide whether the restrictions for this task are currently satisfied.
	// Restrictions on referenced objects that haven't been determined are ignored.
	std::pair<bool, DescrRef> Eligible() const;
	// Attempt to carry out this task. Returns whether or not the task succeeded,
	// and the success or failure message to be printed.
	std::pair<bool, DescrRef> Execute();

	const std::vector<std::regex> &GetCmdRegexes() const { return commandRegexes; }
	const std::vector<std::vector<std::string>> &GetGroupCoding() const { return groupNumToRef; }

	void RegisterNotification(const std::string &evtKey, Util::Control::Condition cond);

	enum class ActionType {
		MoveToLocation,
		MoveToLocationOf,
		MoveToGroup,
		MoveInsideObj,
		MoveOntoObj,
		MoveInDirection,
		MoveToParent,
		MakeCarriedBy,
		MakeWornBy,
		MakePartOf,
		AddToGroup,
		RemoveFromGroup,
		MakeStandingOn,
		MakeSittingOn,
		MakeLyingOn,
		SetVarTo,
		IncVar,
		DecVar,
		SetPropTo,
		ExecTask,
		UnsetTask,
		SkipTurns,
		ConvoGreet,
		ConvoFarewell,
		ConvoAsk,
		ConvoTell,
		ConvoCmd,
		ConvoEnter,
		ConvoLeave,
		GameWin,
		GameLose,
		GameEndNeutral,
		GameContinue
	};
	enum class ActionRefType {
		SingleObj,  // a particular game object of any type
		ObjsHeldBy,
		ObjsWornBy,
		ObjsInside,
		ObjsOn,
		ObjsWithProp,
		ObjsInGroup,
		ObjsAtLocation,
		CharsInside,
		CharsOn,
		CharsWithProp,
		CharsInGroup,
		CharsAtLocation,
		LocationOf,
		LocationsInGroup,
		LocationsWithProp,
		Task,
		None
	};

private:
	Task() = default;

	void SendCompleteNotifications() const;
	void SendUncompleteNotifications() const;

	class Action {
	public:
		static Action CreateFromXML(const pugi::xml_node &xmlNode);
		// Perform this action. Potentially creates and executes several sub-actions
		// if lhs or rhs hold references to multiple objects.
		void Perform() const;
	private:
		ActionRefType refType;
		ActionType type;
		// for simple moves, the object on the left-hand side (e.g. object to move, object whose children to move)
		// for moves based on boolean, enum, or map properties, the the value that the property must hold
		//  in order for the object to be moved
		std::string lhs;
		// on moves choosing objects by property, the key of the property to check
		std::string prop;
		std::string rhs;

		// there are no reference mode / action combinations that have an expression both
		// on the left and the right side, so we can get away with just one reference here.
		// (for moves based on properties where the property can hold arbitrary strings or integers,
		//  an expression producing a string or integer to check against.)
		ExprRef expr = 0;

		// Actually perform the action for concrete objects/values.
		void PerformImpl() const;
		// Perform a move
		void PerformMoveTo(const std::string &moveTarget) const;
	};

	std::string key;
	//std::string descr;
	uint64_t priority;
	bool repeatable;
	DescrRef completionMsg;
	DescrRef overrideFailMsg = 0;
	RestrRef restrictions;
	std::vector<Action> actions;

	enum class SpType {
		Object,
		Text
	};
	struct SpecificInfo {
		SpType type;
		bool multiple;
		std::string key;
	};
	// Is this a general, specific, or system task?
	Type type;
	// For general tasks, the command string, regex transformed string, and set of references.
	// std::vector<std::string> commandStrs;
	std::vector<std::regex> commandRegexes;
	std::vector<std::vector<std::string>> groupNumToRef;
	// For specific tasks, the key of the general task we are overriding
	std::string overridesTask;
	std::vector<SpecificInfo> specificRefs;
	OverrideType overrideType = 0;

	// Events subscribed to this task being completed.
	std::vector<std::string> completeSubs;
	// Events subscribed to this task being uncompleted.
	std::vector<std::string> uncompleteSubs;

	friend struct TaskPrioLess;
};

struct TaskPrioLess {
	bool operator() (const Task *a, const Task *b) const {
		return a->priority < b->priority;
	};
};

}  // namespace Starlane

#endif  // !SLC_TASK_H
