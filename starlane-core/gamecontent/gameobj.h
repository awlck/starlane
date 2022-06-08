#pragma once

#ifndef SLC_GAMEOBJ_H
#define SLC_GAMEOBJ_H

#include "../slc_private.h"

#include <string>

namespace Starlane {

// GameObj is the base for everything that exists within the game world
// (physical or otherwise).
class GameObj {
public:
	static GameObj *CreateFromXML(const pugi::xml_node &xmlNode);

	[[nodiscard]] const std::string &Key() const { return key; }
private:
	std::string key;
};

}

#endif  // !SLC_GAMEOBJ_H