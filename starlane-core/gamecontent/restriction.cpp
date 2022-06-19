#include "restriction.h"

#include <pugixml.hpp>

#include "../game.h"

namespace Starlane {

Restriction *Restriction::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Restriction;
	for (const auto &it : xmlNode.children("Restriction")) {
		Single s;
		const auto &txt = it.first_child();
		s.restrText.reserve(strlen(txt.child_value()) + 15);
		s.restrText = txt.name();
		s.restrText.append(" ").append(txt.child_value());
		const auto &msg = it.child("Message");
		if (msg.type() != pugi::node_null)
			s.failureMsg = Game::Get()->CreateDescFromXML(msg);
		result->restrs.emplace_back(std::move(s));
	}
	result->sequence = xmlNode.child_value("BracketSequence");
	return result;
}

std::pair<bool, DescrRef> Restriction::PassRestrictionBlock() const {
	return { true, 0 };
}

}