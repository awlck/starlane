#include "event.h"

#include <sstream>

#include <pugixml.hpp>

#include "../valueparsers.h"

namespace Starlane {

Event *Event::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Event;
	result->key = xmlNode.child_value("Key");
	result->startType = ParseStartType(xmlNode.child_value("WhenStart"));
	result->timeType = ParseTimeType(xmlNode.child_value("Type"));
	result->repeating = ParseBool(xmlNode.child_value("Repeating"));
	result->duration = Util::Range(xmlNode.child_value("Length"));

	if (result->startType == StartType::TaskBased) {
		for (const auto &it: xmlNode.children("Control")) {
			result->controls.emplace_back(it.child_value());
			// TODO: Pull this apart and get tasks to notify us of these conditions.
		}
	}

	return result;
}

}