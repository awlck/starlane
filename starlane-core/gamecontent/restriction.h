#pragma once

#ifndef SLC_RESTRICTION_H
#define SLC_RESTRICTION_H

#include "../slc_private.h"

#include <utility>
#include <string>
#include <vector>

#include "../mechanus.h"

namespace Starlane {

class Restriction {
public:
	static Restriction *CreateFromXML(const pugi::xml_node &xmlNode);

	std::pair<bool, DescrRef> PassRestrictionBlock() const;

private:
	Restriction() = default;

	struct Single {
		std::string restrText;
		DescrRef failureMsg = 0;
	};

	std::vector<Single> restrs;
	std::string sequence;
};

}

#endif  // !SLC_RESTRICTION_H
