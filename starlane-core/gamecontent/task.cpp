#include "task.h"

#include <sstream>
#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"

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

	if (tokens[0] == "Object") result.refType = ActionRefType::SingleObj;
	result.lhs = tokens[1];

	return result;
}

}