#include "variable.h"

#include <pugixml.hpp>

#include "../valueparsers.h"

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
			std::istringstream i(val);
			std::string v;
			while (std::getline(i, v, ',')) {
				result->intVals.push_back(ParseInt(v.c_str()));
			}
			result->type = Type::IntArray;
		} else if (result->type == Type::String) {
			result->strVals.reserve(result->capacity);
			std::istringstream i(val);
			std::string v;
			while (std::getline(i, v, '\n')) {
				result->strVals.push_back(v);
			}
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