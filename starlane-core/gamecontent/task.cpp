#include "task.h"

#include <algorithm>
#include <sstream>

#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"
#include "event.h"
#include "restriction.h"

namespace Starlane {

Task *Task::CreateFromXML(Game *g, const pugi::xml_node &xmlNode) {
	auto result = new Task;
	result->key = xmlNode.child_value("Key");
	result->priority = ParseInt(xmlNode.child_value("Priority"));
	result->type = Task::ParseType(xmlNode.child_value("Type"));
	result->command = xmlNode.child_value("Command");
	result->descr = xmlNode.child_value("Description");
	result->completionMsg = Game::Get()->CreateDescFromXML(xmlNode.child("CompletionMessage"));
	result->repeatable = ParseBool(xmlNode.child_value("Repeatable"));
	result->restrictions = g->CreateRestrictionsFromXML(xmlNode.child("Restrictions"));
	{
		const auto &foNode = xmlNode.child("FailOverride");
		if (foNode.type() != pugi::node_null)
			result->overrideFailMsg = Game::Get()->CreateDescFromXML(foNode);
	}
	if (result->type == Type::Specific) {
		for (const auto &spcNode: xmlNode.children("Specific")) {
			SpecificInfo spi;
			spi.type = STREQ(spcNode.child_value("Type"), "Text") ? SpType::Text : SpType::Object;
			spi.multiple = ParseBool(spcNode.child_value("Multiple"));
			if (spi.type != SpType::Text) {
				spi.key = spcNode.child_value("Key");
			}
			if (std::count_if(spcNode.begin(), spcNode.end(), [&](const auto &item) {
				return STREQ(item.name(), "Key");
			}) > 1) throw std::runtime_error(std::string("In task ") + result->key + ": specific tasks with multiple explicitly-named objects in the same reference are currently unsupported.");
		}
		result->overrideType = ParseOverrideType(xmlNode.child_value("SpecificOverrideType"));
	}

	for (const auto &it: xmlNode.child("Actions").children())
		result->actions.push_back(Action::CreateFromXML(it));

	return result;
}

Task::Action Task::Action::CreateFromXML(const pugi::xml_node &xmlNode) {
	Action result;
	std::string name = xmlNode.name();
	std::istringstream strm(std::string(xmlNode.child_value()));
	std::string t;
	std::vector<std::string> tokens;
	while (std::getline(strm, t, ' '))
		tokens.push_back(t);

	if (name == "MoveObject" || name == "MoveCharacter") {
		result.lhs = tokens[1];
		if (tokens[2] == "ToParentLocation") {
			result.type = ActionType::MoveToParent;
		} else {
			result.rhs = tokens[3];
		}
		if (tokens[2] == "ToLocation") {
			result.type = ActionType::MoveToLocation;
		} else if (tokens[2] == "ToSameLocationAs") {
			result.type = ActionType::MoveToLocationOf;
		} else if (tokens[2] == "ToLocationGroup") {
			result.type = ActionType::MoveToGroup;
		} else if (tokens[2] == "InsideObject") {
			result.type = ActionType::MoveInsideObj;
		} else if (tokens[2] == "OntoObject") {
			result.type = ActionType::MoveOntoObj;
		} else if (tokens[2] == "ToCarriedBy") {
			result.type = ActionType::MakeCarriedBy;
		} else if (tokens[2] == "ToWornBy") {
			result.type = ActionType::MakeWornBy;
		} else if (tokens[2] == "ToPartOfCharacter" || tokens[2] == "ToPartOfObject") {
			result.type = ActionType::MakePartOf;
		} else if (tokens[2] == "ToParentLocation") {
			result.type = ActionType::MoveToParent;
		} else if (tokens[2] == "InDirection") {
			result.type = ActionType::MoveInDirection;
		} else if (tokens[2] == "ToStandingOn") {
			result.type = ActionType::MakeStandingOn;
		} else if (tokens[2] == "ToSittingOn") {
			result.type = ActionType::MakeSittingOn;
		} else if (tokens[2] == "ToLyingOn") {
			result.type = ActionType::MakeLyingOn;
		} else throw std::runtime_error(std::string("Unknown movement command: ") + tokens[2]);
		// reference type determined below.
	} else if (name == "AddObjectToGroup" || name == "AddCharacterToGroup" || name == "AddLocationToGroup") {
		result.type = ActionType::AddToGroup;
		result.lhs = tokens[1];
		result.lhs = tokens[3];
		// reference type determined below.
	} else if (name == "RemoveObjectFromGroup" || name == "RemoveCharacterFromGroup" || name == "RemoveLocationFromGroup") {
		result.type = ActionType::RemoveFromGroup;
		result.lhs = tokens[1];
		result.lhs = tokens[3];
		// reference type determined below.
	} else if (name == "SetProperty") {
		result.type = ActionType::SetPropTo;
		result.lhs = tokens[0];
		result.prop = tokens[1];
		result.rhs = tokens[2];
		for (size_t idx = 3; idx < tokens.size(); idx++) {
			result.rhs += " " + tokens[idx];
		}
		result.refType = ActionRefType::None;
		return result;
	} else if (name == "SetTasks") {
		if (tokens[0] == "Execute")
			result.type = ActionType::ExecTask;
		else if (tokens[0] == "FOR")
			throw std::runtime_error("Looped task execution is currenly unsupported.");
		else result.type = ActionType::UnsetTask;
		result.refType = ActionRefType::Task;
		result.lhs = tokens[1];
		return result;
	} else if (name == "Time") {
		result.type = ActionType::SkipTurns;
		result.refType = ActionRefType::None;
		result.lhs = tokens[1];
		if (result.lhs[0] == '"') {
			result.lhs = result.lhs.erase(result.lhs.size()-1, 1).erase(0, 1);
		}
		return result;
	} else if (name == "SetVariable" || name == "IncVariable" || name == "DecVariable") {
		if (tokens[0] == "FOR")
			throw std::runtime_error("Looped variable setting is currently unsupported.");
		if (name == "SetVariable") result.type = ActionType::SetVarTo;
		else if (name == "IncVariable") result.type = ActionType::IncVar;
		else result.type = ActionType::DecVar;
		result.refType = ActionRefType::None;
		result.lhs = tokens[0];
		// ignore tokens[1]
		result.rhs = tokens[2];
		for (size_t idx = 3; idx < tokens.size(); idx++) {
			result.rhs += " " + tokens[idx];
		}
		return result;
	} else if (name == "Conversation") {
		result.refType = ActionRefType::None;
		std::transform(tokens[0].begin(), tokens[0].end(), tokens[0].begin(), [](auto c) { return toupper(c); });
		if (tokens[0] == "GREET" || tokens[0] == "FAREWELL" || tokens[0] == "ASK" || tokens[0] == "TELL") {
			if (tokens[0] == "GREET") result.type = ActionType::ConvoGreet;
			else if (tokens[0] == "FAREWELL") result.type = ActionType::ConvoFarewell;
			else if (tokens[0] == "ASK") result.type = ActionType::ConvoAsk;
			else result.type = ActionType::ConvoTell;
			result.lhs = tokens[1];
			if (tokens.size() >= 3) {
				// ignore tokens[2]
				for (size_t idx = 3; idx < tokens.size(); idx++) {
					result.rhs += (idx == 3 ? "" : " ") + tokens[idx];
				}
			}
		} else if (tokens[0] == "SAY") {
			result.type = ActionType::ConvoCmd;
			for (size_t idx = 1; idx < tokens.size()-3; idx++) {
				result.rhs += (idx == 1 ? "" : " ") + tokens[idx];
			}
			result.lhs = tokens[tokens.size()-1];
		} else if (tokens[0] == "ENTERWITH") {
			result.type = ActionType::ConvoEnter;
			result.lhs = tokens[1];
		} else if (tokens[0] == "LEAVEWITH") {
			result.type = ActionType::ConvoLeave;
			result.lhs = tokens[1];
		}
		return result;
	} else if (name == "EndGame") {
		result.refType = ActionRefType::None;
		if (tokens[0] == "Neutral")
			result.type = ActionType::GameEndNeutral;
		else if (tokens[0] == "Win")
			result.type = ActionType::GameWin;
		else if (tokens[0] == "Lose")
			result.type = ActionType::GameLose;
		else
			result.type = ActionType::GameContinue;
		return result;
	}

	if (tokens[0] == "Object" || tokens[0] == "Character" || tokens[0] == "Location")
		result.refType = ActionRefType::SingleObj;
	else if (tokens[0] == "EverythingHeldBy")
		result.refType = ActionRefType::ObjsHeldBy;
	else if (tokens[0] == "EverythingWornBy")
		result.refType = ActionRefType::ObjsWornBy;
	else if (tokens[0] == "EverythingInside")
		result.refType = ActionRefType::ObjsInside;
	else if (tokens[0] == "EverythingOn")
		result.refType = ActionRefType::ObjsOn;
	else if (tokens[0] == "EverythingWithProperty")
		result.refType = ActionRefType::ObjsWithProp;
	else if (tokens[0] == "EverythingInGroup")
		result.refType = ActionRefType::ObjsInGroup;
	else if (tokens[0] == "EverythingAtLocation")
		result.refType = ActionRefType::ObjsAtLocation;
	else if (tokens[0] == "EveryoneInside")
		result.refType = ActionRefType::CharsInside;
	else if (tokens[0] == "EveryoneOn")
		result.refType = ActionRefType::CharsOn;
	else if (tokens[0] == "EveryoneWithProperty")
		result.refType = ActionRefType::CharsWithProp;
	else if (tokens[0] == "EveryoneInGroup")
		result.refType = ActionRefType::CharsInGroup;
	else if (tokens[0] == "EveryoneAtLocation")
		result.refType = ActionRefType::CharsAtLocation;
	else if (tokens[0] == "LocationOf")
		result.refType = ActionRefType::LocationOf;
	else if (tokens[0] == "EverywhereInGroup")
		result.refType = ActionRefType::LocationsInGroup;
	else if (tokens[0] == "EverywhereWithProperty")
		result.refType = ActionRefType::LocationsWithProp;
	else throw std::runtime_error(std::string("Unknown reference mode: ") + tokens[0]);

	return result;
}

bool Task::Completed() const {
	return Game::Get()->GetIsTaskCompleted(key);
}

std::pair<bool, DescrRef> Task::Eligible() const {
	if (restrictions == 0)  // i.e., no restrictions set
		return { true, 0 };
	return Game::Get()->GetRestriction(restrictions)->PassRestrictionBlock(true);
}

std::pair<bool, DescrRef> Task::Execute() {
	auto elig = Game::Get()->GetRestriction(restrictions)->PassRestrictionBlock();
	if (!elig.first) return elig;
	// TODO
	return { true, completionMsg };
}

void Task::RegisterNotification(const std::string &evtKey, Util::Control::Condition cond) {
	switch (cond) {
		case Util::Control::Condition::Completion:
			completeSubs.emplace_back(evtKey);
			break;
		case Util::Control::Condition::Uncompletion:
			uncompleteSubs.push_back(evtKey);
			break;
	}
}

void Task::Uncomplete() {
	if (Completed()) {
		SendUncompleteNotifications();
		Game::Get()->SetTaskCompleted(key, false);
	}
}

void Task::SendCompleteNotifications() const {
	for (const auto &it: completeSubs) {
		Game::Get()->GetEvent(it)->ReceiveTaskNotification(Util::Control::Condition::Completion, key);
	}
}

void Task::SendUncompleteNotifications() const {
	for (const auto &it: uncompleteSubs) {
		Game::Get()->GetEvent(it)->ReceiveTaskNotification(Util::Control::Condition::Uncompletion, key);
	}
}

}
