#pragma once

#ifndef SLC_UTILITY_H
#define SLC_UTILITY_H

#include <string>
#include <vector>

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
	explicit Range(const char *txt);

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

	// Non-destructively get the current value, for the purpose of save-games.
	uint32_t CurrentState() const { return value; }
	// ... and to restore it:
	void RestoreState(uint32_t val) { value = val; }

private:
	// Initialized here as well as in every constructor: a constructor that forgot one of these
	// left it indeterminate, and Value() would then hand RandomInt whatever was on the stack.
	uint32_t min = (uint32_t) -1;
	uint32_t max = (uint32_t) -1;
	uint32_t value = (uint32_t) -1;
};


// Case folding for ADRIFT's fixed English names -- "%object%", "ReferencedObject", "north".
// Deliberately ASCII-only and locale-independent rather than std::tolower, on two counts. Those
// names have to fold identically on every machine, and std::tolower does not promise that: it
// answers to the locale, and the Qt frontend sets one (see starlane-qt/starlane.cpp), so on a
// Turkish-language system 'I' would not fold to 'i' and "ReferencedItem" would stop resolving.
// It is also the only safe thing to do byte-by-byte to text that may be UTF-8: in a single-byte
// locale std::tolower would happily rewrite the bytes of a multi-byte character into something
// else. Bytes outside ASCII are left exactly as they are, which is what every caller wants --
// none of them are folding prose, only names that were English identifiers to begin with.
inline std::string ToLower(std::string s) {
	for (auto &c : s)
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	return s;
}

// The same, the other way, for the keywords ADRIFT writes into a game file ("GREET", "ASK").
// Text the player sees or types is not this function's business -- that goes to the frontend.
inline std::string ToUpper(std::string s) {
	for (auto &c : s)
		if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
	return s;
}

// The form of a reference name used as a key into the game's table of captured references.
// Reference names are matched case-insensitively -- games spell them "%Player%", "%player%",
// "ReferencedObject" and "referencedobject" interchangeably -- so every name goes through here
// before being stored or looked up, leaving one spelling in the table whichever the game used.
inline std::string CanonicalizeRefName(const std::string &o) { return ToLower(o); }

// Whether `family` -- already case-folded, and already stripped of any 1-5 suffix -- names one
// of the kinds of thing a task's Command can refer to.
inline bool IsRefFamily(const std::string &family) {
	return family == "object" || family == "objects" || family == "character" ||
		family == "direction" || family == "location" || family == "item" ||
		family == "number" || family == "text";
}

// Determine whether a word is the literal name of a command reference, as it appears
// in a task's Command pattern: "%object%", "%object2%", "%text%", and so on. Captured
// references are stored under this name, so restrictions and description text can refer
// to them by it as well.
inline bool IsCommandRefName(const std::string &o) {
	if (o.size() < 3 || o.front() != '%' || o.back() != '%') return false;
	std::string family = ToLower(o.substr(1, o.size() - 2));
	if (family.back() >= '0' && family.back() <= '9') {
		// only the suffixes 1-5 are valid for disambiguating multiple references
		if (family.back() < '1' || family.back() > '5') return false;
		family.pop_back();
	}
	return IsRefFamily(family);
}

// Determine whether a word is a reference: a command reference as spelled in a task's own
// Command ("%object2%"), one of ADRIFT's generic position-based names that library restrictions
// use instead ("ReferencedObject2"), or one of the two standing references to the player.
// I'm not sure why five was chosen as the limit for each type of reference, but here we are.
static inline bool IsReference(const std::string &o) {
	if (IsCommandRefName(o)) return true;
	const std::string l = ToLower(o);
	if (l == "%player%" || l == "playerlocation") return true;
	static constexpr char kGeneric[] = "referenced";
	constexpr size_t kGenericLen = sizeof(kGeneric) - 1;
	if (l.compare(0, kGenericLen, kGeneric) != 0) return false;
	std::string family = l.substr(kGenericLen);
	if (family.empty()) return false;
	if (family.back() >= '0' && family.back() <= '9') {
		// only the suffixes 1-5 are valid for disambiguating multiple references
		if (family.back() < '1' || family.back() > '5') return false;
		family.pop_back();
		// "ReferencedObjects" is the one plural, and has no numbered variants: a command that
		// refers to several objects at once only ever does so the once.
		if (family == "objects") return false;
	}
	return IsRefFamily(family);
}

static inline bool IsList(const std::string &o) {
	for (size_t i = 0; i < o.size(); i++)
		if (o[i] == '|') return true;
	return false;
}

// Split a string with the specified delimiter:
std::vector<std::string> SplitString(const std::string &s, const std::string &delimRegex);

// Whether `word` appears as a whole space-delimited token within `phrase`, compared
// case-insensitively (ASCII fold, as ToLower). Both an object's prefix and its individual
// nouns may be multi-word ("old rusty", "oil lamp"), so a plain equality test won't do.
// Used to test a disambiguation answer word against an object's naming words.
inline bool ContainsWholeWord(const std::string &phrase, const std::string &word) {
	std::string w = ToLower(word);
	if (w.empty()) return false;
	for (const auto &tok : SplitString(phrase, " "))
		if (ToLower(tok) == w) return true;
	return false;
}

// A regex alternation (e.g. "north|n|northeast|ne|...") of every direction word and
// abbreviation ADRIFT recognizes, suitable for embedding in a capturing group.
const std::string &DirectionsRegexAlternation();
// Canonicalize a matched direction word/abbreviation (e.g. "n", "North-East") to the form
// used as a Location's exit key (e.g. "North", "NorthEast"). Returns "" if not recognized.
std::string CanonicalizeDirection(const std::string &raw);

// Take an ADRIFT-style textual list (e.g., "foo|bar|baz") and turn it into a vector of strings.
inline std::vector<std::string> SplitList(const std::string &lst) { return SplitString(lst, "\\|"); }
// and the same for splitting a string at each newline:
inline std::vector<std::string> SplitLines(const std::string &str) { return SplitString(str, "\n"); }

inline bool StringIsNullOrEmpty(const std::string &str) { return str.empty(); }
inline bool StringIsNullOrEmpty(const std::string *str) {
    return str == nullptr || str->empty();
}
inline bool StringIsNullOrEmpty(const char *str) {
    return str == nullptr || *str == 0;
}
inline bool StringIsNullOrWhitespace(std::string_view str) {
    return std::all_of(str.begin(), str.end(), isspace);
}
inline bool StringIsNullOrWhitespace(const std::string *str) {
    if (str == nullptr) return true;
    return StringIsNullOrWhitespace(std::string_view(str->data(), str->size()));
}
inline bool StringIsNullOrWhitespace(const char *str) {
    if (str == nullptr) return true;
    for (const char *p = str; *p; p++) {
        if (!isspace(*p)) return false;
    }
    return true;
}
}

#endif  // !SLC_UTILITY_H
