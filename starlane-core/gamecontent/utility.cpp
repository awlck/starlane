#include "utility.h"

#include <cstring>
#include <regex>

namespace {
// Whether `pattern` matches exactly one fixed string -- i.e. contains no ECMAScript regex
// operator -- and if so what that string is. Almost every delimiter this file is asked to split
// on is one of these (" ", ",", "\n", "\\|"), and a literal split beats building and running a
// std::regex by well over an order of magnitude; SplitString sits in the per-turn path via
// SplitList/SplitLines/ContainsWholeWord, so that difference is worth having.
bool LiteralDelimiter(const std::string &pattern, std::string &out) {
	out.clear();
	for (size_t i = 0; i < pattern.size(); i++) {
		const char c = pattern[i];
		if (c == '\\') {
			if (++i >= pattern.size()) return false;
			const unsigned char esc = (unsigned char) pattern[i];
			// Only an escaped punctuation character stands for itself; the alphanumeric escapes
			// are character classes (\s, \d) or control characters (\n as written in a pattern).
			if (isalnum(esc)) return false;
			out += (char) esc;
		} else if (std::strchr("^$.*+?()[]{}|", c) != nullptr) {
			return false;
		} else {
			out += c;
		}
	}
	return !out.empty();
}

std::vector<std::string> SplitByRegex(const std::string &s, const std::regex &re) {
	std::sregex_token_iterator first(s.begin(), s.end(), re, -1), last;
	// implicitly initialise vector from iterator, since this is just what you probably wouldn't expect:
	return { first, last };
}

// A literal-delimiter split reproducing std::sregex_token_iterator's -1 ("everything between the
// matches") behaviour exactly: an empty input yields one empty piece, a leading or doubled
// delimiter yields an empty piece, and a single trailing delimiter does *not* (the iterator
// suppresses an empty suffix).
std::vector<std::string> SplitLiteral(const std::string &s, const std::string &delim) {
	std::vector<std::string> result;
	size_t pos = 0;
	for (;;) {
		const size_t hit = s.find(delim, pos);
		if (hit == std::string::npos) break;
		result.emplace_back(s, pos, hit - pos);
		pos = hit + delim.size();
	}
	result.emplace_back(s, pos, s.size() - pos);
	if (result.size() > 1 && result.back().empty()) result.pop_back();
	return result;
}
}  // namespace

// see: https://stackoverflow.com/a/9437426
std::vector<std::string> Starlane::Util::SplitString(const std::string &s, const std::string &delimRegex) {
	std::string literal;
	if (LiteralDelimiter(delimRegex, literal))
		return SplitLiteral(s, literal);
	return SplitByRegex(s, std::regex(delimRegex));
}

std::vector<std::string> Starlane::Util::SplitObjectList(const std::string &s) {
	// One of the two delimiters in the codebase that really is a regex, and the only one on a
	// per-command path, so it is compiled once rather than per call.
	static const std::regex kSeparator(R"(\s*,\s*|\s+and\s+)");
	std::vector<std::string> result;
	for (auto &piece : SplitByRegex(s, kSeparator)) {
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
			// The first synonym is the direction's display word (ADRIFT's DirectionName takes the
			// part before the first "/"). Keep it as written; lowercasing is the caller's business.
			if (!table.canonicalToDisplay.count(entry.canonical))
				table.canonicalToDisplay[entry.canonical] = syn;
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