//
// Created by Adrian Welcker on 25.05.23.
//
#pragma once

#ifndef SLC_USERFUNC_H
#define SLC_USERFUNC_H

#include "../slc_private.h"
#include "../expression.h"

#include <vector>
#include <map>

namespace Starlane {

class UserFunction {
public:
	static UserFunction *CreateFromXML(const pugi::xml_node &xmlNode);

	const std::string &Key() const { return key; }
	const std::string &Name() const { return name; }

	enum class ArgType {
		Object,
		Character,
		Location,
		Number,
		Text
	};
	struct ArgSpec {
		std::string name;
		ArgType ty;
	};
	const std::vector<ArgSpec> &Signature() const { return signature; }

	std::string Evaluate(const UserFuncContext &args) const;

private:
	std::string key;
	std::string name;
	DescrRef output;
	std::vector<ArgSpec> signature;

	static ArgType ParseArgType(const char *txt);
};

}  // namespace Starlane

#endif  // !SLC_USERFUNC_H
