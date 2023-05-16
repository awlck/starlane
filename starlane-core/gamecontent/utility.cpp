#include "utility.h"

#include <regex>

// see: https://stackoverflow.com/a/9437426
std::vector<std::string> Starlane::Util::SplitList(const std::string &lst) {
	std::regex re("\\|");
	std::sregex_token_iterator first(lst.begin(), lst.end(), re, -1), last;
	// implicitly initialise vector from iterator, since this is just what you probably wouldn't expect:
	return { first, last };
}
