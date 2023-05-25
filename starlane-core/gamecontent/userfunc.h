//
// Created by Adrian Welcker on 25.05.23.
//
#pragma once

#ifndef SLC_USERFUNC_H
#define SLC_USERFUNC_H

#include "../slc_private.h"

namespace Starlane {

class UserFunction {
public:
	static UserFunction *CreateFromXML(const pugi::xml_node &xmlNode);

	const std::string &Key() const { return key; }
	const std::string &Name() const { return name; }

	struct ArgSpec {
		std::string name;

	};

private:
	std::string key;
	std::string name;
	std::string definition;
	std::vector<ArgSpec> signature;
};

}  // namespace Starlane

#endif  // !SLC_USERFUNC_H
