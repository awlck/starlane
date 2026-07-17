#pragma once

#ifndef SLC_LOCATION_H
#define SLC_LOCATION_H

#include "gameobj.h"

#include "../slc_private.h"

namespace Starlane {

class Character;

class Location: public GameObj {
public:
	static Location *CreateFromXML(const pugi::xml_node &xmlNode);
	[[nodiscard]] GameObj *Clone() const override;

	// Gets the display name (i.e. short location description) of this location
	std::string GetDisplayName([[maybe_unused]] bool = false) const override;

	// Builds the long description of this location, followed by a listing of the
	// visible objects here (per their ListDescription/ListDescriptionDynamic
	// properties, falling back to an "Also here is ..." style list).
	std::string GetDescription(bool forDisplay = true) const override;

	// Is the given object directly at this location (i.e. laying around here,
	// not held by a character or inside/on top of another object)?
	bool HoldsDirectly(const GameObj *obj) const;

	// Is the given character visible to someone standing here? Unlike HoldsDirectly,
	// this also covers characters positioned on or inside something here.
	bool IsCharVisibleHere(const Character *ch) const;

	struct ExitSpec {
		std::string destination;
		RestrRef restr;
	};

	const ExitSpec &GetExit(const std::string &dir) const { return exits.at(dir); }
	bool HasExit(const std::string &dir) const { return exits.count(dir) > 0; }
	std::string GetListOfExits() const;

	// Whether some exit of this location leads directly to the location with this key. Restrictions
	// on the exit are ignored, as in ADRIFT: adjacency is about the map's shape, not whether the
	// player could pass. Used by character walks to decide whether a wandering or following step
	// can reach a neighbouring room.
	bool IsAdjacent(const std::string &locKey) const;
	// The direction one would travel from here to reach the adjacent location with this key, phrased
	// the way ADRIFT announces a walking character's movements: "the north", "above", "inside", and
	// so on. Returns "nowhere" if no exit leads there. When two exits lead to the same place, the
	// first in ADRIFT's compass order (N, E, S, W, U, D, In, Out, NE, SE, SW, NW) wins.
	std::string DirectionTo(const std::string &locKey) const;

private:
	Location() = default;

	DescrRef locationName;
	std::unordered_map<std::string, ExitSpec> exits;

protected:
	// Locations can never be matched.
	void MakeMatchExpr() override;
};

}

#endif  // !SLC_LOCATION_H
