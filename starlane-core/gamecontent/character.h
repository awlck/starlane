#pragma once

#ifndef SLC_CHARACTER_H
#define SLC_CHARACTER_H

#include "gameobj.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "../slc_private.h"
#include "utility.h"
#include "walk.h"

namespace Starlane {

class Character: public GameObj {
public:
	static Character *CreateFromXML(const pugi::xml_node &xmlNode);
	[[nodiscard]] GameObj *Clone() const override;

	// Implements `character.Name`.
	// Returns the proper name if the character is known, or the property known doesn't exist.
	// If the property known exists and isn't set, returns the descriptor
	// (i.e., the usual article+prefix+noun combo inherited from GameObj)
	std::string GetDisplayName(bool defArt) const override;
	// Implements `character.ProperName`.
	// Returns the proper name of this character, or 'Anonymous' if that is not set.
	std::string GetProperName() const { return properName.empty() ? "Anonymous" : properName; }
	// Implements `character.Descriptor`
	// Always returns the descriptor, regardless of the status of the `Known` property.
	std::string GetDescriptor() const { return GameObj::GetDisplayName(); }
	std::string GetDescription(bool forDisplay = true) const override;
	const std::regex &GetMatchExpr() const override { return GetBoolProp("Known") ? matchWhenKnownRegex : matchRegex; };
	// Intervene on setting string properties so that we can capture changes to the ProperName property.
	void SetPropValue(const std::string &key, const std::string &value) override;

	std::pair<bool, DescrRef> HasRoute(const std::string &dir) const;
	// Whether this character can currently see the object in question.
	bool CanSee(const std::string &key) const;
	// Whether this character has ever seen the object in question.
	bool HasSeen(const std::string &key) const { return seenStorage.count(key) > 0; }
	// Note that this character has (at some point) seen the object in question.
	void MarkSeen(const std::string &key) { seenStorage.insert(key); }
	// Mark everything this character can currently see (including its location and
	// itself) as seen. Called whenever the character arrives somewhere new.
	void MarkVisibleAsSeen();

	// Moving a character around additionally marks everything at the new position as seen.
	void MoveTo(const std::string &newParent, HoldingType newRelation) override;

	// Characters have their own set of properties storing their location. Brilliant!
	static std::pair<HoldingType, std::string> ParseHoldingType(const char *txt);

	void MakePosture(const std::string &newParent, const char *p);

	enum class PossessionFilter {
		Worn,
		Held,
		WornAndHeld
	};
	std::string GetPossessionsList(PossessionFilter pf = PossessionFilter::WornAndHeld, bool recurse = true) const;

	// Wire each of this character's walks up to the tasks that control them. Deferred to a load pass
	// of its own: characters load before tasks, so the tasks a walk's controls name don't exist yet
	// while the character is being built.
	void RegisterWalkNotifications() const;
	// Advance every walk this character has by one turn. Called from the turn tick, ahead of events.
	void TickWalks();
	// Start any of this character's walks that are marked to begin active. Called once, as the game
	// begins.
	void StartActiveWalks();
	// A task that controls this character's walk number `idx` has completed or uncompleted; hand the
	// notification to that walk.
	void NotifyWalk(int32_t idx, Util::Control::Condition cond, const std::string &taskKey);

	void WriteState(Save::Writer &writer) const override;
	bool RestoreState(const Save::AstNode *node) override;
private:
	Character() = default;

	std::string properName;
	std::unordered_set<std::string> seenStorage;
	std::vector<Walk> walks;

	void MakeMatchExpr() override;
	std::regex matchWhenKnownRegex;
};

}

#endif  // !SLC_CHARACTER_H
