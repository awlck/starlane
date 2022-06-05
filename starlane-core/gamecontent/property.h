#pragma once

#ifndef SLC_PROPERTY_H
#define SLC_PROPERTY_H

#include "../slc_private.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace pugi {
class xml_node;
}

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
	[[nodiscard]] const std::string &Name() const { return name; }

	[[nodiscard]] bool GetBoolValue(const std::string &objkey) const {
		if (type != ValueType::Text)
			throw std::logic_error(std::string("Can't get bool value of property `") + name + "`!");
		return (bool) intStorage.at(objkey);
	}
	[[nodiscard]] int32_t GetIntValue(const std::string &objkey) const {
		if (type != ValueType::Text)
			throw std::logic_error(std::string("Can't get int value of property `") + name + "`!");
		return (int32_t) intStorage.at(objkey);
	}
	[[nodiscard]] DescrRef GetDescValue(const std::string &objkey) const {
		if (type != ValueType::Text)
			throw std::logic_error(std::string("Can't get description value of property `") + name + "`!");
		return (DescrRef) intStorage.at(objkey);
	}
	[[nodiscard]] const std::string &GetStringValue(const std::string &objkey) const {
		if (type < ValueType::Object)
			throw std::logic_error(std::string("Can't get string value of property `") + name + "`!");
		if (type == ValueType::Map) {
			return GetMappedValue(objkey);
		} else {
			return stringStorage.at(objkey);
		}
	}
	[[nodiscard]] const std::string &GetMappedValue(const std::string &objkey) const {
		if (type != ValueType::Map)
			throw std::logic_error(std::string("Can't get mapped value of non-map property `") + name + "`!");
		return mapValues.at((int32_t) intStorage.at(objkey));
	}

	void SetIntValue(const std::string &objkey, int64_t val) {
		intStorage[objkey] = val;
	}
	void SetStringValue(const std::string &objkey, const std::string &val) {
		stringStorage[objkey] = val;
	}

private:
	Property() = default;

	ValueType type = ValueType::ErrorType;
	std::string name;
	std::string desc;

	std::vector<std::string> enumValues;
	std::unordered_map<int32_t, std::string> mapValues;

	std::unordered_map<std::string, int64_t> intStorage;
	std::unordered_map<std::string, std::string> stringStorage;
};

}

#endif  // !SLC_PROPERTY_H
