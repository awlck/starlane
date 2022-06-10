#pragma once

#ifndef SLC_PROPHOLDER_H
#define SLC_PROPHOLDER_H

#include "../slc_private.h"

#include <string>
#include <type_traits>
#include <unordered_map>

namespace Starlane {

class PropHolder {
public:
	template <typename T> T GetPropValue(const std::string &key) {
		if constexpr (std::is_integral_v<T>) {
			return (T) intValuedProps.at(key);
		} else return strValuedProps.at(key);
	}
	void SetPropValue(const std::string &key, int64_t value) {
		intValuedProps[key] = value;
	}
	void SetPropValue(const std::string &key, const std::string &value) {
		strValuedProps[key] = value;
	}

	void SetPropValueFromXML(const pugi::xml_node &xmlNode);
	
private:
	std::unordered_map<std::string, int64_t> intValuedProps;
	std::unordered_map<std::string, std::string> strValuedProps;
};

}

#endif  // !SLC_PROPHOLDER_H