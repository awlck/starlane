#pragma once

#ifndef SLC_UTILITY_H
#define SLC_UTILITY_H

#include <string>
#include "../random.h"

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

private:
	uint32_t min;
	uint32_t max;
	uint32_t value;
};

}

#endif  // !SLC_UTILITY_H
