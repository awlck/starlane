//
// Created by Adrian Welcker on 14.07.23.
//
#pragma once

#ifndef SLC_SYNONYM_H
#define SLC_SYNONYM_H

#include "../slc_private.h"

namespace Starlane {

class Synonym {
public:
	static Synonym *CreateFromXML(const pugi::xml_node &xmlNode);

	const std::string &Key() const { return key; }
	const std::vector<std::string> &GetFrom() const { return from; }
	const std::string &GetReplacement() const { return replacement; }

private:
	std::string key;
	std::vector<std::string> from;
	std::string replacement;
};

}

#endif  // !SLC_SYNONYM_H
