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
		[[nodiscard]] bool Has(int flag) const { return (v & flag) != 0; }
		uint8_t v;
	};
	static OverrideType ParseOverrideType(const char *txt);

	// Whether a Specific task's %ref% is restricted to a particular object/character/direction
	// key (SpType::Object) or a particular piece of literal text/number (SpType::Text, compared
	// case-insensitively against what the player actually typed).
	enum class SpType {
		Object,
		Text
	};
	// One entry per %ref% in the general task's Command, in the same order, describing what a
	// Specific task requires of that reference in order to apply. An empty `key` matches any
	// value (i.e. this reference doesn't narrow down which specific task applies).
	struct SpecificInfo {
		SpType type;
		bool multiple;
		std::string key;
	};

	[[nodiscard]] const std::string &Key() const { return key; }
	[[nodiscard]] bool IsRepeatable() const { return repeatable; }
	bool Completed() const;
	void Uncomplete();
	// Mark this task completed and, if it wasn't already, notify any subscribed events.
	void MarkCompleted();

	// Tentatively decide whether the restrictions for this task are currently satisfied.
	// Restrictions on referenced objects that haven't been determined are ignored.
	std::pair<bool, DescrRef> Eligible() const;
	// Actually (non-tentatively) check whether this task's restrictions are satisfied right now.
	// A completed, non-repeatable task always fails this check.
	std::pair<bool, DescrRef> CheckRestrictions() const;
	// Run this task's actions. Assumes the caller has already confirmed CheckRestrictions() passes.
	void RunActions();
	[[nodiscard]] DescrRef GetCompletionMsg() const { return completionMsg; }

	// Whether *this* task's own completion message displays before or after *this* task's own
	// actions run (independent of OverrideType's Before/After, which is about a Specific task's
	// output relative to its parent's). Defaults to Before when the XML doesn't specify it --
	// matching ADRIFT 5's own loader, which only ever writes out the tag for the non-default
	// "After" case.
	enum class MessagePlacement {
		Before,
		After
	};
	[[nodiscard]] MessagePlacement GetMessagePlacement() const { return messagePlacement; }

	// For Specific tasks: the key of the General task this one overrides, and the per-reference
	// constraints that must hold for it to apply. Empty/default for General and System tasks.
	[[nodiscard]] const std::string &OverridesTask() const { return overridesTask; }
	[[nodiscard]] const std::vector<SpecificInfo> &GetSpecificRefs() const { return specificRefs; }
	[[nodiscard]] OverrideType GetOverrideType() const { return overrideType; }

	// For System tasks: the two ways one can run without the player ever asking for it.
	// The key of the location whose arrival runs this task, or "" for none.
	[[nodiscard]] const std::string &LocationTrigger() const { return locationTrigger; }
	// Whether this task runs once, as the game starts.
	[[nodiscard]] bool RunsImmediately() const { return runImmediately; }

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
		GameContinue,
		// special types of actions that arise from directly setting the location properties
		SpecialSetDynamic
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
		// for moves based on boolean, enum, or map properties, the value that the property must hold
		//  in order for the object(s) to be moved
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
	MessagePlacement messagePlacement = MessagePlacement::Before;

	// Is this a general, specific, or system task?
	Type type;
	// For general tasks, the command string, regex transformed string, and set of references.
	// std::vector<std::string> commandStrs;
	std::vector<std::regex> commandRegexes;
	std::vector<std::vector<std::string>> groupNumToRef;
	// For System tasks: what makes this one run, given it has no command to match against.
	// (A System task named by another task's or subevent's "execute" action needs neither.)
	std::string locationTrigger;
	bool runImmediately = false;
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
		// Tie-break equal priorities by key: a std::set would otherwise consider two
		// distinct tasks with the same priority duplicates and silently drop one.
		if (a->priority != b->priority) return a->priority < b->priority;
		return a->key < b->key;
	};
};

}  // namespace Starlane

#endif  // !SLC_TASK_H
