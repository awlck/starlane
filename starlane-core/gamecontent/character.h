#pragma once

#ifndef SLC_CHARACTER_H
#define SLC_CHARACTER_H

#include "gameobj.h"

#include <string>

#include "../slc_private.h"

namespace Starlane {

class Character: public GameObj {
public:
	static Character *CreateFromXML(const pugi::xml_node &xmlNode);
	[[nodiscard]] GameObj *Clone() const override;

	// Characters have their own set of properties storing their location. Brilliant!
	static std::pair<HoldingType, std::string> ParseHoldingType(const char *txt);
private:
	Character() = default;

	std::string properName;
};

}

#endif  // !SLC_CHARACTER_H
