#pragma once

#ifndef SLC_GAMEOBJ_H
#define SLC_GAMEOBJ_H

#include "../slc_private.h"

#include <set>
#include <string>
#include <vector>

#include "propholder.h"

namespace Starlane {

// GameObj is the base for everything that exists within the game world
// (physical or otherwise).
class GameObj: public PropHolder {
public:
	static GameObj *CreateFromXML(const pugi::xml_node &xmlNode);
	virtual GameObj *Clone() const;  // sort of a copy constructor that respects subclassing.

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

private:
	std::string key;
	std::string parent;
	std::string article;
	std::string prefix;
	std::vector<std::string> descriptors;
	std::set<std::string> groupMembership;
};

}

#endif  // !SLC_GAMEOBJ_H