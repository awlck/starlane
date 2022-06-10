#pragma once

#ifndef SLC_GAMEOBJ_H
#define SLC_GAMEOBJ_H

#include "../slc_private.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "propholder.h"

namespace Starlane {

// GameObj is the base for everything that exists within the game world
// (physical or otherwise).
class GameObj: public PropHolder {
public:
	static GameObj *CreateFromXML(const pugi::xml_node &xmlNode);
	virtual GameObj *Clone() const;  // sort of a copy constructor that respects subclassing.
    virtual ~GameObj() = default;

	[[nodiscard]] const std::string &Key() const { return key; }
	std::string GetDisplayName();
	const std::string &GetParentKey() const { return parent; }

	// Note that this object is becoming a member of the given group.
	void BecomeGroupMember(const std::string &grpKey) {
		groupMembership.insert(grpKey);
	}
	// Note that is object is no longer a member of the given group.
	void CeaseBeingGroupMember(const std::string &grpKey) {
		groupMembership.erase(grpKey);
	}

	enum class HoldingType {
		Hidden,
		AtLocation,
		AtLocationGroup,
		Everywhere,
		InObject,
		OnObject,
		Worn,
		PartOf
	};
	// This function not just parses the "holding type" in the file into one of the values above,
	// but also gives the name of the next property to search (since ADRIFT uses more properties
	// to define containment than we use holding types)
	static std::pair<HoldingType, std::string> ParseHoldingType(const char *txt);

protected:
	void MakeCommonValues(const pugi::xml_node &xmlNode);

	std::string key;
	// The immediate parent holding this object.
	std::string parent;
	// The relationship we have with our parent object.
	HoldingType relation;
	std::string article;
	std::string prefix;
	std::vector<std::string> nouns;
	DescrRef description;
	// The keys of all the groups this object is a member of.
	std::set<std::string> groupMembership;

	// Whether or not this object is considered 'dynamic' (i.e., takeable). Characters and
	// locations are never dynamic. Objects have this set from the `StaticOrDynamic` property.
	// (It more convenient to have this attribute directly accessible to the interpreter code
	//  than having to go through a property lookup each time.)
	bool dynamic = false;
};

}

#endif  // !SLC_GAMEOBJ_H