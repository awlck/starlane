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
	virtual std::string GetStrProp(const std::string &key) const {
		return strValuedProps.at(key);
	}

	virtual int64_t GetIntProp(const std::string &key) const {
		return intValuedProps.at(key);
	}

	virtual bool GetBoolProp(const std::string &key) const {
		if (intValuedProps.count(key) == 0) return false;
		return (bool) intValuedProps.at(key);
	}

	virtual const std::unordered_map<std::string, std::string> &GetAllStrProps() const {
		// This is necessary for the "object is in state" test, which can refer
		// to any enum property without naming it explicitly, as well as for
		// saving the game state.
		return strValuedProps;
	}
	virtual const std::unordered_map<std::string, int64_t> &GetAllIntProps() const {
		// Needed for the implementation of the "has property" check and saving.
		return intValuedProps;
	}

	bool HasProp(const std::string &key) const {
		return intValuedProps.count(key) > 0 || strValuedProps.count(key) > 0;
	}

	virtual void SetPropValue(const std::string &key, int64_t value) {
		intValuedProps[key] = value;
	}
	virtual void SetPropValue(const std::string &key, const std::string &value) {
		strValuedProps[key] = value;
	}

	void SetPropValueFromXML(const pugi::xml_node &xmlNode);

protected:
    void ErasePropValue(const std::string &key);
	void ClearProps() noexcept {
		intValuedProps.clear();
		strValuedProps.clear();
	}
	
private:
	std::unordered_map<std::string, int64_t> intValuedProps;
	std::unordered_map<std::string, std::string> strValuedProps;
};

}

#endif  // !SLC_PROPHOLDER_H
