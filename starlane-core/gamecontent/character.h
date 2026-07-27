#pragma once

#ifndef SLC_CHARACTER_H
#define SLC_CHARACTER_H

#include "gameobj.h"

#include <cstdint>
#include <memory>
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
	const std::regex &GetMatchExpr() const override { return *(GetBoolProp("Known") ? matchWhenKnownRegex : matchRegex); };
	// Besides the article/prefix/nouns the base class checks, a character also answers to its
	// proper name when disambiguating (e.g. "Which guard? George or the other guard.").
	bool MatchesNameWord(const std::string &word) const override;
	// Intervene on setting string properties so that we can capture changes to the ProperName property.
	void SetPropValue(const std::string &key, const std::string &value) override;

	std::pair<bool, DescrRef> HasRoute(const std::string &dir) const;
	// Whether this character can currently see the object in question.
	bool CanSee(const std::string &key) const;
	// Whether this character has ever seen the object in question.
	bool HasSeen(const std::string &key) const { return seenStorage->count(key) > 0; }
	// Note that this character has (at some point) seen the object in question.
	void MarkSeen(const std::string &key) {
		if (seenStorage->count(key) > 0) return;  // nothing to do, and nothing to pay for
		MutableSeen().insert(key);
	}
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
	Character() : GameObj(Kind::Character) {}

	std::string properName;
	// Everything this character has laid eyes on. Copy-on-write: it grows towards one entry per
	// object in the game and is copied into every undo snapshot, but a turn adds to it rarely.
	std::shared_ptr<std::unordered_set<std::string>> seenStorage =
		std::make_shared<std::unordered_set<std::string>>();
	std::unordered_set<std::string> &MutableSeen() {
		if (seenStorage.use_count() > 1)
			seenStorage = std::make_shared<std::unordered_set<std::string>>(*seenStorage);
		return *seenStorage;
	}
	std::vector<Walk> walks;

	void MakeMatchExpr() override;
	// The variant used once this character is Known; shared like GameObj::matchRegex.
	std::shared_ptr<const std::regex> matchWhenKnownRegex = std::make_shared<const std::regex>();
};

// The Character this object is, or nullptr if it is not one. Replaces dynamic_cast for what is
// by far its most common use here -- an "is this a character?" test on every object in the game,
// several times a turn (see GameObj::Kind).
inline Character *AsCharacter(GameObj *o) {
	return o && o->IsCharacter() ? static_cast<Character *>(o) : nullptr;
}
inline const Character *AsCharacter(const GameObj *o) {
	return o && o->IsCharacter() ? static_cast<const Character *>(o) : nullptr;
}

}

#endif  // !SLC_CHARACTER_H
