#pragma once

#ifndef SLC_GAMEOBJ_H
#define SLC_GAMEOBJ_H

#include "../slc_private.h"

#include <string>
#include <vector>

namespace Starlane {

// GameObj is the base for everything that exists within the game world
// (physical or otherwise).
class GameObj {
public:
	static GameObj *CreateFromXML(const pugi::xml_node &xmlNode);
	virtual GameObj *Clone() const;  // sort of a copy constructor that respects subclassing.

	[[nodiscard]] const std::string &Key() const { return key; }
	std::string GetDisplayName();
	const std::string &GetParentKey() const { return parent; }
private:
	std::string key;
	std::string parent;
	std::string article;
	std::string prefix;
	std::vector<std::string> descriptors;
};

}

#endif  // !SLC_GAMEOBJ_H