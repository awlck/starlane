#pragma once

#ifndef SLC_GROUP_H
#define SLC_GROUP_H

#include "../slc_private.h"

#include <string>
#include <set>

#include "propholder.h"

namespace Starlane {

// A group is a collection of objects that can have common properties attached to them.
class Group: public PropHolder {
public:
	static Group *CreateFromXML(const pugi::xml_node &xmlNode);

	const std::string &Key() const { return key; }

	// Make the given object a member of this group.
	void AddObj(const std::string &key);
	// Make the given object a member of this group.
	void AddObj(GameObj *obj);
	// Note that the given object is becoming a member of this group.
	void ReceiveObj(const std::string &key) {
		members.insert(key);
	}
	// Make the given object no longer a member of this group.
	void RemoveObj(const std::string &key);
	void RemoveObj(GameObj *obj);
	// Note that the given member is ceasing to be a member of this group.
	void LetGoOfObj(const std::string &key) {
		members.erase(key);
	}

	bool ContainsObj(const std::string &key) const {
		return members.count(key) > 1;
	}
	const std::set<std::string> &GetAllMembers() const { return members; }

private:
	Group() = default;

	std::string key;
	std::string name;
	std::set<std::string> members;
};

}

#endif  // !SLC_GROUP_H