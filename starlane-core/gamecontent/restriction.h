#pragma once

#ifndef SLC_RESTRICTION_H
#define SLC_RESTRICTION_H

#include "../slc_private.h"

#include <utility>
#include <string>
#include <vector>

namespace Starlane {

class Restriction {
public:
	static Restriction *CreateFromXML(const pugi::xml_node &xmlNode);

	std::pair<bool, DescrRef> PassRestrictionBlock(const Game *g);

	class Single {
	public:
		static Single *CreateFromXML(const pugi::xml_node &xmlNode);

	private:
		Single() = default;

		std::string restrText;
		DescrRef failureMsg;
	};

private:
	Restriction() = default;

	std::vector<Single> restrs;
	std::string ordering;
};

}

#endif  // !SLC_RESTRICTION_H
