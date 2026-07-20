#pragma once

#ifndef SLC_PROPERTY_H
#define SLC_PROPERTY_H

#include "../slc_private.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Starlane {

class Property {
public:
	static Property *CreateFromXML(const pugi::xml_node &xmlNode);

	enum class ValueType {
		ErrorType,
		Bool,  // PropStorage<bool>
		Int,  // PropStorage<int32_t>
		Text,  // PropStorage<DescrRef>
		Object,  // PropStorage<std::string>
		Enum,  // PropStorage<std::string>
		Map  // PropStorage<int32_t>
	};
	static ValueType ParseValueType(const char *txt);

	[[nodiscard]] ValueType Type() const { return type; }
	[[nodiscard]] const std::string &Key() const { return key; }
	// The property whose state list this one's states are folded into, if any: "LockStatus" only
	// adds a "Locked" state to "OpenStatus" rather than standing on its own. Such a property is
	// never the answer to "is this object in state X?" -- the property it appends to is.
	[[nodiscard]] const std::string &AppendsTo() const { return appendTo; }

private:
	Property() = default;

	ValueType type = ValueType::ErrorType;
	std::string key;
	std::string desc;
	std::string appendTo;

	std::vector<std::string> enumValues;
	std::unordered_map<int32_t, std::string> mapValues;
};

}

#endif  // !SLC_PROPERTY_H
