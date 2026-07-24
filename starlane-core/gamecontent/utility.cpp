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

// Mirrors ADRIFT 5's built-in sDirectionsRE table (clsAdventure.vb) -- the default that applies
// unless a game's <DirectionNorth>/etc. element (FileIO.vb) replaces a direction's words wholesale.
struct DirectionEntry {
	const char *canonical;
	const char *synonyms;  // '/'-separated, same format as a <DirectionXxx> override
};

const DirectionEntry kDirections[] = {
	{ "North",     "North/N" },
	{ "NorthEast", "NorthEast/NE/North-East/N-E" },
	{ "East",      "East/E" },
	{ "SouthEast", "SouthEast/SE/South-East/S-E" },
	{ "South",     "South/S" },
	{ "SouthWest", "SouthWest/SW/South-West/S-W" },
	{ "West",      "West/W" },
	{ "NorthWest", "NorthWest/NW/North-West/N-W" },
	{ "In",        "In/Inside" },
	{ "Out",       "Out/O/Outside" },
	{ "Up",        "Up/U" },
	{ "Down",      "Down/D" },
};

}  // anonymous namespace

DirectionTable BuildDirectionTable(const std::unordered_map<std::string, std::string> &overrides) {
	DirectionTable table;
	for (const auto &entry : kDirections) {
		auto it = overrides.find(entry.canonical);
		const std::string &words = it != overrides.end() ? it->second : entry.synonyms;
		for (const auto &syn : SplitString(words, "/")) {
			if (syn.empty()) continue;
			std::string lower = ToLower(syn);
			if (!table.regexAlternation.empty()) table.regexAlternation += '|';
			table.regexAlternation += lower;
			table.synonymToCanonical[lower] = entry.canonical;
		}
	}
	return table;
}

std::string CanonicalizeDirection(const std::string &raw, const DirectionTable &table) {
	auto it = table.synonymToCanonical.find(ToLower(raw));
	return it != table.synonymToCanonical.end() ? it->second : "";
}

}  // namespace Starlane::Util