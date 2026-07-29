#pragma once

#ifndef SLC_PROPHOLDER_H
#define SLC_PROPHOLDER_H

#include "../slc_private.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace Starlane {

class PropHolder {
public:
	virtual std::string GetStrProp(const std::string &key) const {
		auto it = props->strValued.find(key);
		// Named rather than bare: "unordered_map::at: key not found" says nothing about which
		// property, on a path reached from any number of restrictions and expressions.
		if (it == props->strValued.end())
			throw std::out_of_range("no string property '" + key + "' here");
		return it->second;
	}

	virtual int64_t GetIntProp(const std::string &key) const {
		// A property the holder never had set reads as 0, rather than throwing: ADRIFT gives
		// objects a default for every applicable property (an unset Weight/Size still has a
		// value), and games lean on that -- e.g. taking an object whose Weight was left default
		// evaluates "%object%.Weight" against MaxWeight. We don't store per-property defaults, so
		// 0 stands in; it matches the lenient GetBoolProp above, and callers needing to tell
		// "unset" from "zero" have HasProp for it.
		auto it = props->intValued.find(key);
		return it == props->intValued.end() ? 0 : it->second;
	}

	virtual bool GetBoolProp(const std::string &key) const {
		auto it = props->intValued.find(key);
		return it != props->intValued.end() && it->second != 0;
	}

	virtual const std::unordered_map<std::string, std::string> &GetAllStrProps() const {
		// This is necessary for the "object is in state" test, which can refer
		// to any enum property without naming it explicitly, as well as for
		// saving the game state.
		return props->strValued;
	}
	virtual const std::unordered_map<std::string, int64_t> &GetAllIntProps() const {
		// Needed for the implementation of the "has property" check and saving.
		return props->intValued;
	}

	bool HasProp(const std::string &key) const {
		return props->intValued.count(key) > 0 || props->strValued.count(key) > 0;
	}

	// Both check first. Writing a value that is already there would otherwise detach this holder's
	// shared property table (see `props` below) to change nothing at all.
	virtual void SetPropValue(const std::string &key, int64_t value) {
		auto it = props->intValued.find(key);
		if (it != props->intValued.end() && it->second == value) return;
		MutableProps().intValued[key] = value;
	}
	virtual void SetPropValue(const std::string &key, const std::string &value) {
		auto it = props->strValued.find(key);
		if (it != props->strValued.end() && it->second == value) return;
		MutableProps().strValued[key] = value;
	}

	void SetPropValueFromXML(const pugi::xml_node &xmlNode);

protected:
    void ErasePropValue(const std::string &key);
	void ClearProps() noexcept {
		// Nothing shared to detach from if we are only dropping everything: hand back a fresh
		// table rather than deep-copying one just to empty it.
		props = std::make_shared<Properties>();
	}

private:
	struct Properties {
		std::unordered_map<std::string, int64_t> intValued;
		std::unordered_map<std::string, std::string> strValued;
	};

	// Copy-on-write. An object's property tables are the bulk of what an object is, and an object
	// a turn changes is cloned into that turn's undo record -- so sharing the tables until somebody
	// writes keeps that clone to a refcount bump, and charges the deep copy only when the
	// properties themselves change. Correct because a PropHolder only ever mutates through the
	// members below: nothing hands out a non-const reference into the tables.
	std::shared_ptr<Properties> props = std::make_shared<Properties>();

	// The tables to write to: our own if nobody else is looking at them, otherwise a private
	// copy first. Single-threaded by construction, as the whole interpreter core is -- use_count
	// is only an approximation once more than one thread can hold a reference.
	Properties &MutableProps() {
		if (props.use_count() > 1)
			props = std::make_shared<Properties>(*props);
		return *props;
	}
};

}

#endif  // !SLC_PROPHOLDER_H
