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

private:
	Location() = default;

	DescrRef locationName;
};

}

#endif  // !SLC_LOCATION_H
