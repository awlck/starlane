#include "variable.h"

#include <pugixml.hpp>

#include "../valueparsers.h"
#include "utility.h"

namespace Starlane {

Variable *Variable::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Variable;
	result->key = xmlNode.child_value("Key");
	result->varName = xmlNode.child_value("Name");

	auto valt = xmlNode.child_value("Type");
	if (STREQ(valt, "Numeric"))
		result->type = Type::Int;
	else if (STREQ(valt, "Text"))
		result->type = Type::String;
	
	const auto &arrl = xmlNode.child("ArrayLength");
	auto val = xmlNode.child_value("InitialValue");
	if (arrl.type() != pugi::node_null) {  // this is an array!
		result->capacity = ParseInt(arrl.child_value());
		if (result->type == Type::Int) {
			result->intVals.reserve(result->capacity);
			auto tmp = Util::SplitString(val, ",");
			for (const auto &v: tmp)
				result->intVals.push_back(ParseInt(v.c_str()));
			result->type = Type::IntArray;
		} else if (result->type == Type::String) {
			result->strVals = Util::SplitLines(val);
			result->type = Type::StringArray;
		}
	} else if (result->type == Type::Int) {
		result->intVals.push_back(ParseInt(val));
	} else if (result->type == Type::String) {
		result->strVals.emplace_back(val);
	}

	return result;
}

}