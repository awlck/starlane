#pragma once

#ifndef SLC_TASK_H
#define SLC_TASK_H

#include "../slc_private.h"

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

	enum class OverrideType {

	};
	static OverrideType ParseOverrideType(const char *txt);

	[[nodiscard]] const std::string &Key() const { return key; }
	bool Completed() const;

	void RegisterNotification(const std::string &evtKey, Util::Control::Condition cond);

	void Uncomplete();

private:
	Task() = default;

	void SendCompleteNotifications() const;
	void SendUncompleteNotifications() const;

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
		SetPropTo,
		ExecTask,
		UnsetTask,
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
		Variable,
		Meta
	};

	class Action {
	public:
		static Action CreateFromXML(const pugi::xml_node &xmlNode);
		void Perform() {}
	private:
		ActionRefType refType;
		ActionType type;
		std::string lhs;
		std::string prop;
		std::string rhs;
	};

	std::string key;
	std::string command;
	std::string descr;
	Type type;
	uint64_t priority;
	bool repeatable;
	DescrRef completionMsg;
	RestrRef restrictions;
	std::vector<Action> actions;

	std::vector<std::string> completeSubs;
	std::vector<std::string> uncompleteSubs;
};

}

#endif  // !SLC_TASK_H
