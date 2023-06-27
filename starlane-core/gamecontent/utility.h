#pragma once

#ifndef SLC_UTILITY_H
#define SLC_UTILITY_H

#include <string>
#include "../random.h"
#include <vector>

namespace Starlane::Util {

struct Control {
	enum class Action {
		Start,
		Stop,
		Pause,
		Resume
	};
	static Action ParseAction(const char *txt);

	enum class Condition {
		Completion,
		Uncompletion
	};
	static Condition ParseCondition(const char *txt);

	Action action;
	Condition condition;
	std::string taskName;
};

// For use with "Event takes X to Y turns", in which the actual length is chosen
// to be a random value between these two boundaries (both inclusive).
struct Range {
	// Default constructor: doesn't create a "useful" object, but required in order to
	// default-construct objects that use Ranges.
	Range() : min((uint32_t) -1), max((uint32_t) -1), value((uint32_t) -1) {}
	// For when there's only one value
	explicit Range(uint32_t val) : min(val), max(val), value(val) {}
	// Proper ranges with an upper and lower bound.
	Range(uint32_t min_, uint32_t max_) : min(min_), max(max_), value(min_ == max_ ? min : (uint32_t) -1) {}
	// Parse a range value from a string like "1 to 10"
	Range(const char *txt);

	uint32_t Value() {
		if (value != (uint32_t) -1)
			return value;
		value = RandomInt(min, max);
		return value;
	}

	void Reset() {
		if (min != max)
			value = (uint32_t) -1;
	}

	// Non-desctructively get the current value, for the purpose of save-games.
	uint32_t CurrentState() const { return value; }
	// ... and to restore it:
	void RestoreState(uint32_t val) { value = val; }

private:
	uint32_t min;
	uint32_t max;
	uint32_t value;
};


// Determine whether a word is a reference. I'm not sure why five was chosen as the limit
// for each type of reference, but here we are.
// (We can optimize this some more later by using a map or strcmp if it turns out to be necessary.)
static inline bool IsReference(const std::string &o) {
	// put the most likely ones at the top
	return
		o == "%Player%" || o == "ReferencedObject" || o == "ReferencedObjects" ||
		o == "ReferencedDirection" || o == "ReferencedCharacter" || o == "ReferencedLocation" ||
		o == "ReferencedItem" || o == "ReferencedNumber" ||
		// and now the whole deluge of numbered possibilities
		o == "ReferencedObject1" || o == "ReferencedObject2" || o == "ReferencedObject3" ||
		o == "ReferencedObject4" || o == "ReferencedObject5" ||
		o == "ReferencedDirection1" || o == "ReferencedDirection2" || o == "ReferencedDirection3" ||
		o == "ReferencedDirection4" || o == "ReferencedDirection5" ||
		o == "ReferencedCharacter1" || o == "ReferencedCharacter2" || o == "ReferencedCharacter3" ||
		o == "ReferencedCharacter4" || o == "ReferencedCharacter5" ||
		o == "ReferencedLocation1" || o == "ReferencedLocation2" || o == "ReferencedLocation3" ||
		o == "ReferencedLocation4" || o == "ReferencedLocation5" ||
		o == "ReferencedItem1" || o == "ReferencedItem2" || o == "ReferencedItem3" ||
		o == "ReferencedItem4" || o == "ReferencedItem5" ||
		o == "ReferencedNumber1" || o == "ReferencedNumber2" || o == "ReferencedNumber3" ||
		o == "ReferencedNumber4" || o == "ReferencedNumber5"
		;
}

static inline bool IsList(const std::string &o) {
	for (size_t i = 0; i < o.size(); i++)
		if (o[i] == '|') return true;
	return false;
}

// Take an ADRIFT-style textual list (e.g., "foo|bar|baz") and turn it into a vector of strings.
std::vector<std::string> SplitList(const std::string &lst);

}

#endif  // !SLC_UTILITY_H
