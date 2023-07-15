//
// Created by Adrian Welcker on 15.07.23.
//

#include "textoverride.h"

#include <pugixml.hpp>

#include "../game.h"

namespace Starlane {

TextOverride *TextOverride::CreateFromXML(Starlane::Game *g, const pugi::xml_node &node) {
	auto result = new TextOverride;

	result->key = node.child_value("Key");
	result->from = node.child_value("OldText");
	const auto &ntNode = node.child("NewText");
	if (ntNode.type() != pugi::node_null) {
		result->replacement = g->CreateDescFromXML(ntNode);
	}

	return result;
}

}  // namespace Starlane