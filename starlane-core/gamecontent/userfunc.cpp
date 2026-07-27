//
// Created by Adrian Welcker on 25.05.23.
//

#include "userfunc.h"

#include <pugixml.hpp>

#include "../game.h"
#include "description.h"

namespace Starlane {

UserFunction *UserFunction::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new UserFunction;
	result->key = xmlNode.child_value("Key");
	result->name = xmlNode.child_value("Name");
	result->output = Game::Get()->CreateDescFromXML(xmlNode.child("Output"));
	std::vector<std::string> argNames;
	for (const auto &arg : xmlNode.children("Argument")) {
		result->signature.push_back({ arg.child_value("Name"), ParseArgType(arg.child_value("Type")) });
		argNames.push_back(result->signature.back().name);
	}
	// The output text refers to the arguments as `%name%`; without this, resolution has no way of
	// telling those apart from stray percent signs and leaves them in the text verbatim.
	if (!argNames.empty())
		Game::Get()->GetDescription(result->output)->SetUserFuncArgNames(std::move(argNames));
	return result;
}

std::string UserFunction::Evaluate(const UserFuncContext &args) const {
	return Game::Get()->GetDescription(output)->BuildAndCommit(&args);
}

}  // namespace Starlane