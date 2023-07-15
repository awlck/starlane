//
// Created by Adrian Welcker on 15.07.23.
//

#ifndef STARLANE_TEXTOVERRIDE_H
#define STARLANE_TEXTOVERRIDE_H

#include "../slc_private.h"

namespace Starlane {

class TextOverride {
public:
	static TextOverride *CreateFromXML(Game *g, const pugi::xml_node &node);

	const std::string &Key() { return key; }
	const std::string &GetFrom() const { return from; };
	DescrRef GetReplacement() const { return replacement; }

private:
	std::string key;
	std::string from;
	DescrRef replacement;
};

}

#endif  // !STARLANE_TEXTOVERRIDE_H
