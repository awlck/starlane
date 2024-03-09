#pragma once

#ifndef SLC_GAMEOBJ_H
#define SLC_GAMEOBJ_H

#include "../slc_private.h"

#include <regex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "propholder.h"
#include "../savefiles/parser.h"

namespace Starlane {
class Location;

// GameObj is the base for everything that exists within the game world
// (physical or otherwise).
class GameObj: public PropHolder {
public:
	std::string GetStrProp(const std::string &key) const override;
	int64_t GetIntProp(const std::string &key) const override;
	bool GetBoolProp(const std::string &key) const override;
	const std::unordered_map<std::string, std::string> &GetAllStrProps() const override;
	const std::unordered_map<std::string, int64_t> &GetAllIntProps() const override;
	virtual const std::regex &GetMatchExpr() const { return matchRegex; }
	static GameObj *CreateFromXML(const pugi::xml_node &xmlNode);
	virtual GameObj *Clone() const;  // sort of a copy constructor that respects subclassing.
    virtual ~GameObj() = default;

	[[nodiscard]] const std::string &Key() const { return key; }
	const std::string &GetParentKey() const { return parent; }

	virtual std::string GetDisplayName(bool defArt = false) const;
	virtual std::string GetDescription(bool forDisplay = true) const;

	// Note that this object is becoming a member of the given group.
	void BecomeGroupMember(const std::string &grpKey) {
		groupMembership.insert(grpKey);
	}
	// Note that is object is no longer a member of the given group.
	void CeaseBeingGroupMember(const std::string &grpKey) {
		groupMembership.erase(grpKey);
	}
	// Is this object a member of the given group?
	bool IsMemberOfGroup(const std::string &grpKey) const {
		return groupMembership.count(grpKey) > 0;
	}
	// Gets the location of this object. An empty string means that this object is not in
	// any location (i.e., it is "hidden" in ADRIFT terms).
	const std::string &GetLocationKey() const;
	// Get the location of this object as an object pointer rather than a key.
	// A null pointer is returned if this object is hidden, or if the ultimate location somehow
	// isn't of type `Location` after all.
	Location *GetLocation() const;
	// Get the visibility ceiling (usually the location, but when in a closed container
	// this would be that container).
	const std::string &GetVisbilityCeiling() const;

	enum class ChildFilter {
		All,
		Objects,
		Characters
	};
	enum class ChildRelFilter {
		On,
		In,
		OnAndIn
	};
	// Gets the (textual) list of children, potentially filtered by object type and relation
	std::string GetListOfChildren(ChildFilter f1 = ChildFilter::All, ChildRelFilter f2 = ChildRelFilter::OnAndIn, bool recurse = false) const;

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

	// Gets the relationship this object has to its parent.
	HoldingType GetParentRelation() const { return relation; }

	// Move this object so that it has this parent and relation
	void MoveTo(const std::string &newParent, HoldingType newRelation);

	// Set the `dynamic` state, because of course you can change that through property assignments.
	void SetDynamic(bool dynamic) { this->dynamic = dynamic; }

	// Write out mutable object state to a save file
	virtual void WriteState(Save::Writer &writer) const;
	virtual bool RestoreState(const Save::AstNode *node);

protected:
	void MakeCommonValues(const pugi::xml_node &xmlNode);

	std::string key;
	// The immediate parent holding this object.
	std::string parent;
	// The relationship we have with our parent object.
	HoldingType relation;
	// Whether or not this object is considered 'dynamic' (i.e., takeable). Characters and
	// locations are never dynamic. Objects have this set from the `StaticOrDynamic` property.
	// (It more convenient to have this attribute directly accessible to the interpreter code
	//  than having to go through a property lookup each time.)
	bool dynamic = false;
	std::string article;
	std::string prefix;
	std::vector<std::string> nouns;
	DescrRef description;
	// The keys of all the groups this object is a member of.
	std::unordered_set<std::string> groupMembership;
	// A regular expression that matches this object's name.
	std::regex matchRegex;
	virtual void MakeMatchExpr();

	const Group *GetGroupWithProp(const std::string &k) const;

	mutable std::unordered_map<std::string, std::string> hackyStrPropCache;
	mutable std::unordered_map<std::string, int64_t> hackyIntPropCache;
};

}

#endif  // !SLC_GAMEOBJ_H