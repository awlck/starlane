#pragma once

#ifndef SLC_VARIABLE_H
#define SLC_VARIABLE_H

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
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
	// Whether this variable holds text rather than numbers. Which of the two it is is settled
	// when the game loads and never changes.
	bool HoldsText() const { return type == Type::String || type == Type::StringArray; }

	template <typename T> T GetValue(uint32_t idx = 1) const {
		CheckIndex(idx);
		if constexpr (std::is_integral_v<T>) {
			return (T) Ints()[idx - 1];
		} else return Strs()[idx - 1];
	}

	void SetValue(int64_t val, uint32_t idx = 1);
	void SetValue(const std::string &val, uint32_t idx = 1);

	// For saving the game:
	const std::vector<int64_t> &GetIntArray() const { return Ints(); }
	const std::vector<std::string> &GetStrArray() const { return Strs(); }
	void ClearAll() {
		if (HoldsText()) {
			auto &v = MutableStrs();
			v.clear();
			v.resize(capacity);
		} else {
			auto &v = MutableInts();
			v.clear();
			v.resize(capacity);
		}
	}

private:
	Variable() = default;

	// Bounds-check a 1-based index as a game writes it, throwing if this variable hasn't got one.
	void CheckIndex(uint32_t idx) const;

	// A variable holds numbers or text, never both -- unlike an object, whose properties can be of
	// either kind -- so the two are one variant rather than two vectors, and the kind a given
	// variable isn't costs nothing at all.
	//
	// Asking for the wrong kind throws std::bad_variant_access where indexing the (empty) unused
	// vector used to throw std::out_of_range. That keeps such a mistake in the same hands as
	// before: neither is a std::runtime_error, so both fall past the local "treat as failed"
	// catches in restriction and task evaluation to the top-level backstop.
	using Values = std::variant<std::vector<int64_t>, std::vector<std::string>>;
	// Copy-on-write, for the same reason as PropHolder's property tables: a variable that a turn
	// changes is cloned into that turn's undo record, and this keeps the clone to a pointer copy
	// for the (common) case of a value nothing has touched since.
	std::shared_ptr<Values> values = std::make_shared<Values>();

	const std::vector<int64_t> &Ints() const { return std::get<std::vector<int64_t>>(*values); }
	const std::vector<std::string> &Strs() const { return std::get<std::vector<std::string>>(*values); }
	std::vector<int64_t> &MutableInts() { return std::get<std::vector<int64_t>>(Detach()); }
	std::vector<std::string> &MutableStrs() { return std::get<std::vector<std::string>>(Detach()); }
	// The values to write to: our own if no snapshot is sharing them, otherwise a private copy.
	Values &Detach() {
		if (values.use_count() > 1)
			values = std::make_shared<Values>(*values);
		return *values;
	}

	std::string key;
	std::string varName;
	// Initialized so that a <Type> the loader doesn't recognize leaves a coherent variable behind
	// (an empty numeric one) rather than reading whatever was on the stack.
	Type type = Type::Int;
	uint32_t capacity = 1;
	bool everChanged = false;
};

}

#endif  // !SLC_VARIABLE_H
