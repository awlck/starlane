#include "task.h"

#include <sstream>
#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"
#include "event.h"

namespace Starlane {

Task *Task::CreateFromXML(Game *g, const pugi::xml_node &xmlNode) {
	auto result = new Task;
	result->key = xmlNode.child_value("Key");
	result->priority = ParseInt(xmlNode.child_value("Priority"));
	result->type = Task::ParseType(xmlNode.child_value("Type"));
	result->command = xmlNode.child_value("Command");
	result->descr = xmlNode.child_value("Description");
	result->completionMsg = g->CreateDescFromXML(xmlNode.child("CompletionMessage"));
	result->repeatable = ParseBool(xmlNode.child_value("Repeatable"));
	result->restrictions = g->CreateRestrictionsFromXML(xmlNode.child("Restrictions"));

	for (auto it: xmlNode.child("Actions").children())
		result->actions.push_back(Action::CreateFromXML(it));

	return result;
}

Task::Action Task::Action::CreateFromXML(const pugi::xml_node &xmlNode) {
	Action result;
	std::string name = xmlNode.name();
	std::istringstream strm(std::string(xmlNode.child_value()));
	std::string t;
	std::vector<std::string> tokens;
	int i = 0;
	while (std::getline(strm, t, ' ') && ++i < 4)
		tokens.push_back(t);

	if (name == "EndGame") {
		result.refType = ActionRefType::Meta;
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

	if (tokens[0] == "Object") result.refType = ActionRefType::SingleObj;
	result.lhs = tokens[1];

	return result;
}

bool Task::Completed() const {
	return Game::Get()->GetIsTaskCompleted(key);
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