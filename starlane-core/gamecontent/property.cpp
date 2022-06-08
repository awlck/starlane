#include "property.h"
#include "../valueparsers.h"

#include <pugixml.hpp>

namespace Starlane {

Property *Property::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Property;
	result->key = xmlNode.child_value("Key");
	result->desc = xmlNode.child_value("Description");
	result->type = ParseValueType(xmlNode.child_value("Type"));

	if (result->type == ValueType::Enum) {
		for (const auto &it: xmlNode.children("State")) {
			result->enumValues.emplace_back(it.child_value());
		}
	} else if (result->type == ValueType::Map) {
		for (const auto &it: xmlNode.children("ValueList")) {
			int32_t value;
			std::string label;
			if (std::string(it.first_child().name()) == "Label") {
				label = it.first_child().child_value();
				value = (int32_t) ParseInt(it.last_child().child_value());
			} else {
				label = it.last_child().child_value();
				value = (int32_t) ParseInt(it.first_child().child_value());
			}
			result->mapValues[value] = label;
		}
	}

	return result;
}

}