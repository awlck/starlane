#include "description.h"

#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"

namespace Starlane {

Description *Description::CreateFromXML(Game *g, const pugi::xml_node &xmlNode) {
    auto result = new Description;
	for (const auto &it: xmlNode.children("Description"))
		result->segments.emplace_back(Segment::CreateFromXML(g, it));
	return result;
}

Description::Segment Description::Segment::CreateFromXML(Game *g, const pugi::xml_node &xmlNode) {
	Description::Segment result;
	result.text = xmlNode.child_value("Text");
	result.displayWhen = Description::DisplayValue(xmlNode.child_value("DisplayWhen"));
	auto once = xmlNode.child("DisplayOnce");
	result.onceOnly = once.type() == pugi::node_null ? false : ParseBool(xmlNode.child_value("DisplayOnce"));
	auto restr = xmlNode.child("Restrictions");
	if (restr.type() != pugi::node_null) {
		result.restrictionId = g->CreateRestrictionsFromXML(restr);
	}
	return result;
}

}