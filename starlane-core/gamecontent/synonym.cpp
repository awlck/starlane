//
// Created by Adrian Welcker on 14.07.23.
//

#include "synonym.h"

#include <pugixml.hpp>

namespace Starlane {

Synonym *Synonym::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Synonym;
	result->key = xmlNode.child_value("Key");
	for (const auto &it: xmlNode.children("From"))
		result->from.emplace_back(it.child_value());
	result->replacement = xmlNode.child_value("To");
	return result;
}

}