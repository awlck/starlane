#include "event.h"
#include "../valueparsers.h"

#include <pugixml.hpp>

Starlane::Event *Starlane::Event::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Event;
	result->key = xmlNode.child_value("Key");
	result->startType = ParseStartType(xmlNode.child_value("WhenStart"));
	result->timeType = ParseTimeType(xmlNode.child_value("Type"));
	// TODO: Parse duration value.
	//result->duration = (int32_t) ParseInt(xmlNode.child_value("Length"));
	result->repeating = ParseBool(xmlNode.child_value("Repeating"));

	if (result->startType == StartType::TaskBased) {
		for (const auto &it: xmlNode.children("Control")) {
			result->controls.emplace_back(it.child_value());
			// TODO: Pull this apart and get tasks to notify us of these conditions.
		}
	}

	return result;
}
