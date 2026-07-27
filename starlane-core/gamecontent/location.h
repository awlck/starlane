#pragma once

#ifndef SLC_LOCATION_H
#define SLC_LOCATION_H

#include "gameobj.h"

#include <memory>
#include <unordered_map>

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

	const ExitSpec &GetExit(const std::string &dir) const { return exits->at(dir); }
	bool HasExit(const std::string &dir) const { return exits->count(dir) > 0; }
	std::string GetListOfExits() const;

	// The "Exits are north and east." / "An exit leads north." sentence appended to a location
	// description when <ShowExits> is on. Empty when no (unrestricted) exit is currently available.
	// Directions are listed in ADRIFT's compass order and named per the game's own direction table.
	std::string GetExitsLine() const;

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
	Location() : GameObj(Kind::Location) {}

	DescrRef locationName;
	// Fixed once the game has loaded -- a location's exits are part of the map's shape, and
	// nothing changes them at runtime. Held by shared pointer so that the copy of every location
	// in the world that Game::SaveUndo makes each turn shares one table instead of rebuilding it.
	std::shared_ptr<const std::unordered_map<std::string, ExitSpec>> exits =
		std::make_shared<const std::unordered_map<std::string, ExitSpec>>();

protected:
	// Locations can never be matched.
	void MakeMatchExpr() override;
};

// The Location this object is, or nullptr if it is not one. See AsCharacter in character.h.
inline Location *AsLocation(GameObj *o) {
	return o && o->IsLocation() ? static_cast<Location *>(o) : nullptr;
}
inline const Location *AsLocation(const GameObj *o) {
	return o && o->IsLocation() ? static_cast<const Location *>(o) : nullptr;
}

}

#endif  // !SLC_LOCATION_H
