//
// Created by Adrian Welcker on 25.05.23.
//

#include "userfunc.h"

#include <pugixml.hpp>

#include "../game.h"

namespace Starlane {

UserFunction *UserFunction::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new UserFunction;
	result->key = xmlNode.child_value("Key");
	result->name = xmlNode.child_value("Name");
	result->output = Game::Get()->CreateDescFromXML(xmlNode.child("Output"));
	for (const auto &arg : xmlNode.children("Argument")) {
		result->signature.push_back({ arg.child_value("Name"), ParseArgType(arg.child_value("Type")) });
	}
	return result;
}

}  // namespace Starlane