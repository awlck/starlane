#pragma once

#ifndef SLC_CHARACTER_H
#define SLC_CHARACTER_H

#include "gameobj.h"

#include <string>
#include <unordered_set>

#include "../slc_private.h"

namespace Starlane {

class Character: public GameObj {
public:
	static Character *CreateFromXML(const pugi::xml_node &xmlNode);
	[[nodiscard]] GameObj *Clone() const override;

	std::pair<bool, DescrRef> HasRoute(const std::string &dir) const;
	// Whether this character can currently see the object in question.
	bool CanSee(const std::string &key) const;
	// Whether this character has ever seen the object in question.
	bool HasSeen(const std::string &key) const { return seenStorage.count(key) > 0; }

	// Characters have their own set of properties storing their location. Brilliant!
	static std::pair<HoldingType, std::string> ParseHoldingType(const char *txt);

	enum class Posture {
		Standing,
		Sitting,
		Lying
	};
	inline Posture GetPosture() { return posture; }
	void MakePosture(const std::string &newParent, Posture p);
private:
	Character() = default;

	std::string properName;
	std::unordered_set<std::string> seenStorage;
	Posture posture;
};

}

#endif  // !SLC_CHARACTER_H
