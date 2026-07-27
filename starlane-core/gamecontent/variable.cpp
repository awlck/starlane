#include "variable.h"

#include <pugixml.hpp>

#include "../valueparsers.h"
#include "utility.h"

namespace Starlane {

void Variable::CheckIndex(uint32_t idx) const {
	if (idx != 0 && idx <= capacity) return;
	std::string errmsg("Index ");
	errmsg += std::to_string(idx);
	errmsg += " out of range for variable ";
	errmsg += key;
	throw std::runtime_error(errmsg);
}

Variable *Variable::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Variable;
	result->key = xmlNode.child_value("Key");
	result->varName = xmlNode.child_value("Name");

	auto valt = xmlNode.child_value("Type");
	if (STREQ(valt, "Numeric"))
		result->type = Type::Int;
	else if (STREQ(valt, "Text"))
		result->type = Type::String;

	// Built here and moved into the variant below, so that only the kind this variable actually
	// holds is ever constructed.
	const auto &arrl = xmlNode.child("ArrayLength");
	auto val = xmlNode.child_value("InitialValue");
	std::vector<int64_t> ints;
	std::vector<std::string> strs;
	if (arrl.type() != pugi::node_null) {  // this is an array!
		result->capacity = ParseInt(arrl.child_value());
		if (result->type == Type::Int) {
			ints.reserve(result->capacity);
			auto tmp = Util::SplitString(val, ",");
			for (const auto &v: tmp)
				ints.push_back(ParseInt(v.c_str()));
			result->type = Type::IntArray;
		} else if (result->type == Type::String) {
			strs = Util::SplitLines(val);
			result->type = Type::StringArray;
		}
		// The initial value need not spell out every element; the remainder defaults to 0/"".
		// Sizing to capacity here is what lets every index the bounds checks accept actually
		// be addressable.
		if (result->type == Type::IntArray) ints.resize(result->capacity);
		else if (result->type == Type::StringArray) strs.resize(result->capacity);
	} else if (result->type == Type::Int) {
		ints.push_back(ParseInt(val));
	} else if (result->type == Type::String) {
		strs.emplace_back(val);
	}

	if (result->HoldsText())
		result->values = std::make_shared<Values>(std::move(strs));
	else
		result->values = std::make_shared<Values>(std::move(ints));

	return result;
}

}
