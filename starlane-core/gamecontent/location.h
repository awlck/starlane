#pragma once

#ifndef SLC_LOCATION_H
#define SLC_LOCATION_H

#include "gameobj.h"

#include "../slc_private.h"

namespace Starlane {

class Location: public GameObj {
public:
	static Location *CreateFromXML(const pugi::xml_node &xmlNode);
	[[nodiscard]] GameObj *Clone() const override;

	struct ExitSpec {
		std::string destination;
		RestrRef restr;
	};

private:
	Location() = default;

	DescrRef locationName;
	std::unordered_map<std::string, ExitSpec> exits;
};

}

#endif  // !SLC_LOCATION_H
