#pragma once

#ifndef SLC_VARIABLE_H
#define SLC_VARIABLE_H

#include <stdexcept>
#include <string>
#include <vector>

#include "../slc_private.h"

namespace Starlane {

class Variable {
public:
	static Variable *CreateFromXML(const pugi::xml_node &xmlNode);

	enum class Type {
		Int,
		IntArray,
		String,
		StringArray
	};

	const std::string &Key() const { return key; }
	const std::string &Name() const { return varName; }
	Type GetType() const { return type; }

	template <typename T> T GetValue(uint32_t idx = 1) const {
		if (idx == 0 || idx > capacity) {
			std::string errmsg("Index ");
			errmsg += std::to_string(idx);
			errmsg += " out of range for variable ";
			errmsg += key;
			throw std::runtime_error(errmsg);
		}
		if constexpr (std::is_integral_v<T>) {
			return (T) intVals.at(idx-1);
		} else return strVals.at(idx-1);
	}

	void SetValue(int64_t val, uint32_t idx = 1) {
		if (idx == 0 || idx > capacity) {
			std::string errmsg("Index ");
			errmsg += std::to_string(idx);
			errmsg += " out of range for variable ";
			errmsg += key;
			throw std::runtime_error(errmsg);
		}
		everChanged = true;
		intVals.at(idx-1) = val;
	}
	void SetValue(const std::string &val, uint32_t idx = 1) {
		if (idx == 0 || idx > capacity) {
			std::string errmsg("Index ");
			errmsg += std::to_string(idx);
			errmsg += " out of range for variable ";
			errmsg += key;
			throw std::runtime_error(errmsg);
		}
		everChanged = true;
		strVals.at(idx-1) = val;
	}

	// For saving the game:
	const std::vector<int64_t> &GetIntArray() const { return intVals; }
	const std::vector<std::string> &GetStrArray() const { return strVals; }
	void ClearAll() {
		if (type == Type::Int || type == Type::IntArray) {
			intVals.clear();
			intVals.resize(capacity);
		} else {
			strVals.clear();
			strVals.resize(capacity);
		}
	}

private:
	Variable() = default;

	std::string key;
	std::string varName;
	Type type;
	std::vector<int64_t> intVals;
	std::vector<std::string> strVals;
	uint32_t capacity = 1;
	bool everChanged = false;
};

}

#endif  // !SLC_VARIABLE_H