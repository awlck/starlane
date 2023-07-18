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

	// Gets the display name (i.e. short location description) of this location
	std::string GetDisplayName([[maybe_unused]] bool = false) const override;

	struct ExitSpec {
		std::string destination;
		RestrRef restr;
	};

	const ExitSpec &GetExit(const std::string &dir) const { return exits.at(dir); }
	bool HasExit(const std::string &dir) const { return exits.count(dir) > 0; }
	std::string GetListOfExits() const;

private:
	Location() = default;

	DescrRef locationName;
	std::unordered_map<std::string, ExitSpec> exits;

	// Locations can never be matched.
	void MakeMatchExpr() override;
};

}

#endif  // !SLC_LOCATION_H
