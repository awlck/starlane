#pragma once

#ifndef SLC_GAMEOBJ_H
#define SLC_GAMEOBJ_H

#include "../slc_private.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "propholder.h"
#include "utility.h"
#include "../savefiles/parser.h"

namespace Starlane {
class Location;

// GameObj is the base for everything that exists within the game world
// (physical or otherwise).
// The optional article a player's command may put in front of a thing's name: the thing's own
// ("a rifle") or the definite one, which fits anything ("the rifle").
std::string ArticleRegexFragment(const std::string &article);
// The optional prefix words ("larger alien's"), each one skippable on its own.
std::string PrefixRegexFragment(const std::string &prefix);

class GameObj: public PropHolder {
public:
	// Which of the three kinds of thing this is. GameObj has exactly two subclasses and the
	// engine asks "is this a character?"/"is this a location?" constantly -- on nearly every
	// object in the game, several times a turn -- so the answer is a stored tag rather than a
	// dynamic_cast, which showed up as a real cost in profiles. See AsCharacter/AsLocation.
	enum class Kind : uint8_t {
		Object,
		Character,
		Location
	};
	[[nodiscard]] Kind GetKind() const { return kind; }
	[[nodiscard]] bool IsCharacter() const { return kind == Kind::Character; }
	[[nodiscard]] bool IsLocation() const { return kind == Kind::Location; }

	std::string GetStrProp(const std::string &key) const override;
	int64_t GetIntProp(const std::string &key) const override;
	bool GetBoolProp(const std::string &key) const override;
	const std::unordered_map<std::string, std::string> &GetAllStrProps() const override;
	const std::unordered_map<std::string, int64_t> &GetAllIntProps() const override;
	virtual const std::regex &GetMatchExpr() const { return *matchRegex; }
	// Whether `word` (matched case-insensitively) is one of this object's naming words for
	// disambiguation purposes: its article, one of its prefix (adjective) words, or one of its
	// nouns. Deliberately distinct from GetMatchExpr(), whose pattern requires a noun and so
	// cannot match a bare adjective like "red" -- which is exactly what a player types when
	// answering "Which ball? The red ball or the green ball." Characters also match a known
	// proper name (see Character::MatchesNameWord).
	virtual bool MatchesNameWord(const std::string &word) const;
	static GameObj *CreateFromXML(const pugi::xml_node &xmlNode);
	virtual GameObj *Clone() const;  // sort of a copy constructor that respects subclassing.
    virtual ~GameObj() = default;

	[[nodiscard]] const std::string &Key() const { return key; }
	const std::string &GetParentKey() const { return parent; }

	virtual std::string GetDisplayName(bool defArt = false) const;
	// The same name with no article at all ("cell air duct"), as `obj.Name(none)` asks for.
	[[nodiscard]] std::string GetBareName() const;
	// The article on its own ("a", "the", "" for none), as `obj.Article` asks for.
	[[nodiscard]] const std::string &GetArticle() const { return article; }
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

	// Move this object so that it has this parent and relation.
	// Any character that can see the object at its new position will note it as "seen".
	virtual void MoveTo(const std::string &newParent, HoldingType newRelation);

	// Place this object directly at the given location, as though the game file had specified it
	// there: sets containment without the arrival bookkeeping (and system-task triggering) that
	// MoveTo performs. Intended for load-time fixups only -- see LoadFromXML's hidden-player default.
	void SetInitialLocation(const std::string &locationKey) {
		parent = locationKey;
		relation = HoldingType::AtLocation;
	}

	// Set the `dynamic` state, because of course you can change that through property assignments.
	void SetDynamic(bool dynamic) { this->dynamic = dynamic; }
	bool IsDynamic() const { return dynamic; }

	// Directly overwrite this object's location (parent + relation) with someone else's,
	// bypassing the arrival bookkeeping (and system-task triggering) MoveTo performs. Used by
	// `MoveCharacter ... ToSwitchWith`, which -- per the original ADRIFT runner -- teleports the
	// second character to the first's location this way, without treating it as a proper move.
	void CopyLocationFrom(const GameObj &other) {
		parent = other.parent;
		relation = other.relation;
	}

	// Move any of `pronouns` found (case-insensitively) among this object's nouns over to
	// `newOwner`'s, removing them from here and recompiling both objects' match expressions.
	// Used by `MoveCharacter ... ToSwitchWith` to keep "me"/"myself"/etc. answering to whichever
	// character is currently the player, matching the original ADRIFT runner.
	void TransferPronounNouns(GameObj &newOwner, const std::vector<std::string> &pronouns);

	// Write out mutable object state to a save file
	virtual void WriteState(Save::Writer &writer) const;
	virtual bool RestoreState(const Save::AstNode *node);

protected:
	explicit GameObj(Kind k = Kind::Object) : kind(k) {}
	void MakeCommonValues(const pugi::xml_node &xmlNode);

	// Set once, by the constructor of whichever subclass (if any) this really is.
	Kind kind;
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
	// The words this thing answers to. Copy-on-write: every object in the world is cloned into
	// the undo snapshot once a turn, and this is the only part of an object's naming that is not
	// a short (and so allocation-free) string -- but it changes only when the player switches
	// character (see TransferPronounNouns), so snapshots share one list until then.
	std::shared_ptr<std::vector<std::string>> nouns = std::make_shared<std::vector<std::string>>();
	// The list to write to: our own if nobody else is looking at it, otherwise a private copy.
	std::vector<std::string> &MutableNouns() {
		if (nouns.use_count() > 1) nouns = std::make_shared<std::vector<std::string>>(*nouns);
		return *nouns;
	}
	DescrRef description;
	// The keys of all the groups this object is a member of.
	std::unordered_set<std::string> groupMembership;
	// A regular expression that matches this object's name. Held by pointer and shared rather than
	// held by value: a std::regex copy is a deep copy of the compiled state machine, every object
	// in the world is cloned into the undo snapshot each turn, and this only ever changes when
	// MakeMatchExpr recompiles it (a rename, or the player switching character). Never written
	// through -- MakeMatchExpr replaces the pointer -- so sharing it needs no copy-on-write dance.
	std::shared_ptr<const std::regex> matchRegex = std::make_shared<const std::regex>();
	virtual void MakeMatchExpr();

	// A name component can itself hold a %function% call -- Return to the Stars names its rifle
	// "%riflename%" so that the name can carry an "(unloaded)" suffix. Compiled once at load and
	// evaluated whenever the name is shown; 0 when the component is plain text.
	DescrRef prefixExpr = 0;
	DescrRef nameExpr = 0;
	void CompileNameExpressions();
	// The prefix and first noun as the player should see them, with any such call resolved.
	[[nodiscard]] std::string DisplayPrefix() const;
	[[nodiscard]] std::string DisplayNoun() const;

	const Group *GetGroupWithProp(const std::string &k) const;

	// ADRIFT keeps an object's (or character's) whereabouts in a family of properties -- the
	// StateList DynamicLocation/StaticLocation/CharacterLocation naming the relationship, plus a
	// dependent key property (HeldByWho, InsideWhat, ...) holding the thing it relates to. We
	// consume those into `relation`/`parent`/`dynamic` at load and erase the strings; this
	// reconstructs one of them on demand, so restrictions and expressions that read it still work.
	// Returns nullopt for any property name that isn't one of those.
	std::optional<std::string> SynthesizeLocationProp(const std::string &k) const;

	mutable std::unordered_map<std::string, std::string> hackyStrPropCache;
	mutable std::unordered_map<std::string, int64_t> hackyIntPropCache;
};

}

#endif  // !SLC_GAMEOBJ_H