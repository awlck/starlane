#include "utility.h"

#include <regex>

// see: https://stackoverflow.com/a/9437426
std::vector<std::string> Starlane::Util::SplitString(const std::string &s, const std::string &delimRegex) {
	std::regex re(delimRegex);
	std::sregex_token_iterator first(s.begin(), s.end(), re, -1), last;
	// implicitly initialise vector from iterator, since this is just what you probably wouldn't expect:
	return { first, last };
}

std::vector<std::string> Starlane::Util::SplitObjectList(const std::string &s) {
	std::vector<std::string> result;
	for (auto &piece : SplitString(s, R"(\s*,\s*|\s+and\s+)")) {
		// A trailing "," before "and" ("a, b, and c") leaves an empty piece behind.
		size_t first = piece.find_first_not_of(" \t");
		if (first == std::string::npos) continue;
		result.push_back(piece.substr(first, piece.find_last_not_of(" \t") - first + 1));
	}
	return result;
}

namespace Starlane::Util {
namespace {

// Mirrors ADRIFT 5's built-in sDirectionsRE table (clsAdventure.vb).
// TODO: support games that rename these via custom direction names.
struct DirectionEntry {
	const char *canonical;
	const char *synonyms[5];  // nullptr-terminated
};

const DirectionEntry kDirections[] = {
	{ "North",     { "north", "n", nullptr } },
	{ "NorthEast", { "northeast", "ne", "north-east", "n-e", nullptr } },
	{ "East",      { "east", "e", nullptr } },
	{ "SouthEast", { "southeast", "se", "south-east", "s-e", nullptr } },
	{ "South",     { "south", "s", nullptr } },
	{ "SouthWest", { "southwest", "sw", "south-west", "s-w", nullptr } },
	{ "West",      { "west", "w", nullptr } },
	{ "NorthWest", { "northwest", "nw", "north-west", "n-w", nullptr } },
	{ "In",        { "in", "inside", nullptr } },
	{ "Out",       { "out", "o", "outside", nullptr } },
	{ "Up",        { "up", "u", nullptr } },
	{ "Down",      { "down", "d", nullptr } },
};

}  // anonymous namespace

const std::string &DirectionsRegexAlternation() {
	static const std::string result = [] {
		std::string r;
		for (const auto &entry : kDirections) {
			for (const char * const *syn = entry.synonyms; *syn; syn++) {
				if (!r.empty()) r += '|';
				r += *syn;
			}
		}
		return r;
	}();
	return result;
}

std::string CanonicalizeDirection(const std::string &raw) {
	std::string lower = ToLower(raw);
	for (const auto &entry : kDirections) {
		for (const char * const *syn = entry.synonyms; *syn; syn++) {
			if (lower == *syn) return entry.canonical;
		}
	}
	return "";
}

}  // namespace Starlane::Util