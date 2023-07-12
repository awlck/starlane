#include "utility.h"

#include <regex>

// see: https://stackoverflow.com/a/9437426
std::vector<std::string> Starlane::Util::SplitString(const std::string &s, const std::string &delimRegex) {
	std::regex re(delimRegex);
	std::sregex_token_iterator first(s.begin(), s.end(), re, -1), last;
	// implicitly initialise vector from iterator, since this is just what you probably wouldn't expect:
	return { first, last };
}