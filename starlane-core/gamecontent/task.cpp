#include "task.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

#include <pugixml.hpp>

#include "../error.h"
#include "../game.h"
#include "../expression.h"
#include "../random.h"
#include "../valueparsers.h"
#include "character.h"
#include "event.h"
#include "group.h"
#include "location.h"
#include "property.h"
#include "restriction.h"
#include "variable.h"

namespace Starlane {

namespace {
// Returns the regex fragment (a single capturing group) that a "%name%" / "%name1%".."%name5%"
// placeholder in a task's Command text should compile to. %direction% (and its numbered variants)
// is restricted to actual direction words/abbreviations; everything else (objects, characters,
// text, locations, items) is a generic non-greedy capture that gets resolved to a specific
// game object -- or kept as raw text -- once the command has actually matched.
std::string RegexFragmentForRef(const std::string &token) {
	std::string family = token.substr(1, token.size() - 2);  // strip surrounding '%'
	if (!family.empty() && std::isdigit((unsigned char) family.back()))
		family.pop_back();  // strip trailing 1-5, if any
	family = Util::ToLower(family);

	if (family == "direction")
		return "(" + Game::Get()->GetDirectionTable().regexAlternation + ")";
	if (family == "number")
		return "(-?[0-9]+)";
	return "(.+?)";
}

bool MaybeIsExpr(const std::string &s) {
	size_t pos = 0;
	for (; pos < s.length(); ++pos) {
		auto x = s[pos];
		if (x == '%' || x == '"' || x == '.') return true;
		if (x == ' ') return false;
	}
	return false;
}

// Escape characters that have a special meaning in Regex.
std::string EscapeForRegex(const std::string_view &block) {
	std::string result;
	for (char c: block) {
		switch (c) {
			case '\\':
			case '(':
			case ')':
			case '.':
			case '?':
			case '*':
			case '+':
			case '|':
			case '^':
			case '$':
			case '<':
			case '>':
				result += '\\';
				[[fallthrough]];
			default:
				result += c;
		}
	}
	return result;
}

// https://github.com/jcwild/ADRIFT-5/blob/1163031574aab161f1abc28f96354cc64eec9070/ADRIFT/clsUserSession.vb#L8342
std::string_view GetNextSubBlock(const std::string_view &block) {
	size_t depth = 0;

	for (size_t count = 0; count < block.size(); count++) {
		switch (block[count]) {
			case '{':
			case '[':
				if (depth++ == 0 && count != 0)
					return block.substr(0, count);
				break;
			case '}':
			case ']':
				if (--depth == 0)
					return block.substr(0, count+1);
				break;
			case '/':
				if (depth == 0)
					return block.substr(0, count+1);
				break;
			default:
				continue;
		}
	}

	return block;
}

bool ContainsMandatoryText(const std::string_view &block) {
	size_t depth = 0;
	for (char c: block) {
		switch (c) {
			case ' ':
				continue;
			case '{':
				depth += 1;
				break;
			case '}':
				depth -= 1;
				break;
			default:
				if (depth == 0)
					return true;
				break;
		}
	}
	return false;
}

// Sentinel characters standing in for regex fragments that a wildcard expands to. They survive
// EscapeForRegex untouched (unlike the fragments themselves), and are swapped back in afterwards.
constexpr char kWcOpen = '\x01';   // "(?:"
constexpr char kWcClose = '\x02';  // ")?"
constexpr char kWcAny = '\x03';    // ".*?"

void ReplaceAll(std::string &s, const std::string &from, const std::string &to) {
	for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
		s.replace(pos, from.size(), to);
}

// Index just past the group starting at `open` (which must be a '('), or npos if it never closes.
size_t SkipRegexGroup(const std::string &p, size_t open) {
	size_t depth = 0;
	for (size_t i = open; i < p.size(); i++) {
		if (p[i] == '\\') { i++; continue; }
		if (p[i] == '(') depth++;
		else if (p[i] == ')' && --depth == 0) return i + 1;
	}
	return std::string::npos;
}

// The longest run of characters that every string matching `pattern` must contain literally and
// contiguously, folded to lower case -- or "" when there is no such run, or when the pattern uses
// anything this doesn't model. Matching the player's input against a game's several thousand
// command patterns is the most expensive thing the parser does; a substring test that rules a
// pattern out before std::regex is ever asked is worth a great deal, so long as it never rules out
// a pattern that would have matched. Hence the deliberately narrow reading below: anything not
// positively understood ends the current run or abandons the pattern entirely.
std::string LongestRequiredLiteral(const std::string &pattern) {
	// Character classes and counted repeats aren't part of what the conversion above ever emits,
	// and reading them wrongly would be exactly the kind of mistake that costs a real match.
	if (pattern.find_first_of("[]{}") != std::string::npos) return "";

	std::string best, run;
	auto flush = [&]() {
		// A run has to be worth a search: one character (or nothing but spaces) rules out nothing.
		if (run.size() > best.size() && run.size() >= 2 &&
				run.find_first_not_of(' ') != std::string::npos)
			best = run;
		run.clear();
	};
	for (size_t i = 0; i < pattern.size();) {
		const char c = pattern[i];
		if (c == '(') {
			flush();
			i = SkipRegexGroup(pattern, i);
			if (i == std::string::npos) return "";  // unbalanced: don't guess
			if (i < pattern.size() && (pattern[i] == '?' || pattern[i] == '*' || pattern[i] == '+'))
				i++;
			continue;
		}
		if (c == '|') return "";  // the whole pattern is alternatives; nothing is required of all of them
		if (c == ')') return "";  // unbalanced
		if (c == '.') {  // a wildcard run, and any quantifier on it
			flush();
			i++;
			if (i < pattern.size() && (pattern[i] == '?' || pattern[i] == '*' || pattern[i] == '+')) {
				i++;
				if (i < pattern.size() && pattern[i] == '?') i++;  // the lazy form, ".*?"
			}
			continue;
		}
		if (c == '^' || c == '$') { flush(); i++; continue; }
		char literal;
		if (c == '\\') {
			if (i + 1 >= pattern.size()) return "";
			const unsigned char escaped = (unsigned char) pattern[i + 1];
			i += 2;
			if (isalnum(escaped)) {  // \s, \d, \n and friends: a class or control character, not a literal
				flush();
				if (i < pattern.size() && (pattern[i] == '?' || pattern[i] == '*' || pattern[i] == '+')) i++;
				continue;
			}
			literal = (char) escaped;
		} else if (c == '*' || c == '+' || c == '?') {
			return "";  // a quantifier where we didn't expect one: stop trusting our reading
		} else {
			literal = c;
			i++;
		}
		const char next = i < pattern.size() ? pattern[i] : '\0';
		if (next == '?' || next == '*') {  // this character is optional after all
			flush();
			i++;
			continue;
		}
		// Non-ASCII bytes are left out: the input was case-folded by the frontend, which knows about
		// languages, and folding them here byte-by-byte could disagree with it.
		if ((unsigned char) literal >= 0x80) { flush(); continue; }
		run += (literal >= 'A' && literal <= 'Z') ? (char) (literal + ('a' - 'A')) : literal;
		if (next == '+') {  // one or more: required, but a repeat would break up the run
			i++;
			flush();
		}
	}
	flush();
	return best;
}

// Expand ADRIFT's command wildcards (`*`, matching any run of words, and `_`, an explicit space)
// the way ConvertToRE does: as a plain textual substitution over the whole command, before it is
// broken into blocks. A wildcard next to a space swallows that space when it matches nothing, so
// "listen *" accepts a bare "listen".
std::string ExpandWildcards(const std::string &cmd) {
	std::string s = cmd;
	const std::string open(1, kWcOpen), close(1, kWcClose), any(1, kWcAny);
	ReplaceAll(s, " * ", " " + open + any + " " + close);
	ReplaceAll(s, "* ", open + any + " " + close);
	ReplaceAll(s, " *", open + " " + any + close);
	ReplaceAll(s, "*", any);
	ReplaceAll(s, "_", " ");
	return s;
}

void RestoreWildcards(std::string &s) {
	ReplaceAll(s, std::string(1, kWcOpen), "(?:");
	ReplaceAll(s, std::string(1, kWcClose), ")?");
	ReplaceAll(s, std::string(1, kWcAny), ".*?");
}

bool EndsWith(const std::string &s, const char *suffix) {
	size_t n = std::strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string ProcessBlock(std::string_view block) {  //NOLINT(misc-no-recursion)
	std::string_view nextBlock;
	std::string result;

	do {
		nextBlock = GetNextSubBlock(block);
		block = block.substr(nextBlock.size());
		if (nextBlock.empty()) continue;

		if (nextBlock[0] == '{') {
			std::string transformedBlock;
			// not at all intuitive algorithm that I don't quite understand but that I've also ported to
			// C++ and adapted to output regex. fml.
			bool containsMandatory = ContainsMandatoryText(block);
			if (containsMandatory && !block.empty() && block[0] == ' ') {
				// ADRIFT asks whether what it has built so far ends in "}", i.e. in an optional
				// group -- it assembles the whole pattern in the game's own brace syntax and only
				// converts to a regex at the end, whereas this converts as it goes. The equivalent
				// question here is whether `result` ends with the ")?" such a group turns into.
				// (Getting this wrong strands the space *outside* a following optional group, so
				// "[get/take] {down} {a/the} {woollen} blanket" stops matching a plain "get blanket".)
				if (result.empty() || result[result.size()-1] == ' ' || EndsWith(result, ")?")) {
					if (nextBlock.find('/') != std::string_view::npos) {
						transformedBlock = "{[";
						transformedBlock += nextBlock.substr(1, nextBlock.size()-2);
						transformedBlock += "] }";
					} else {
						transformedBlock = nextBlock.substr(0, nextBlock.size()-1);
						transformedBlock += " }";
					}
					block = block.substr(1, block.size()-1);
				}
			} else if (!containsMandatory && !result.empty() && result[result.size()-1] == ' ') {
				if (nextBlock.find('/') != std::string_view::npos) {
					transformedBlock = "{ [";
					transformedBlock += nextBlock.substr(1, nextBlock.size()-2);
					transformedBlock += "]}";
				} else {
					transformedBlock = "{ ";
					transformedBlock += nextBlock.substr(1);
				}
				result = result.substr(0, result.size()-1);
			}

			if (!result.empty() && result[result.size()-1] == ' ' && block.empty()) {
				if (nextBlock.find('/') != std::string_view::npos) {
					transformedBlock = "{ [";
					transformedBlock += nextBlock.substr(1, nextBlock.size()-2);
					transformedBlock += "]}";
				} else {
					transformedBlock = "{ ";
					transformedBlock += nextBlock.substr(1);
				}
				result = result.substr(0, result.size()-1);
			}

			if (transformedBlock.empty()) transformedBlock = nextBlock;
			result += "(?:";
			result += ProcessBlock(std::string_view(transformedBlock).substr(1, transformedBlock.size()-2));
			result += ")?";
		} else if (nextBlock[0] == '[') {
			result += "(?:";
			result += ProcessBlock(nextBlock.substr(1, nextBlock.size()-2));
			result += ')';
		} else if (nextBlock[nextBlock.size()-1] == '/') {
			result += ProcessBlock(nextBlock.substr(0, nextBlock.size()-1));
			result += '|';
		} else {
			result += EscapeForRegex(nextBlock);
		}
	} while (!block.empty());
	return result;
}

/* ADRIFT 5 stores the location of an object in a whole conglomerate of properties, where
 * the current relation to the parent dictates which property is read to determine what
 * the parent actually is.  This means that if an object goes from laying around in a
 * location to being held by a character, the `AtLocation` property continues to hold the
 * location the object was taken from, while `HeldByWho` holds the character now holding
 * the object, and the property `DynamicLocation` changes to "Held By Character" to indicate
 * the relationship. This also means that manually changing the `DynamicLocation` property
 * back to "Single Location" (which the developer allows you to do through the Set Property action)
 * would move the object back to the last location it was laying around in.
 * 
 * Because that's a right mess and would be really inconvenient to reimplement, we altogether
 * disallow setting the location properties directly, meaning we reject all of the following as
 * errors, at least for now:
 */
// The argument list of an `Execute <task> (<arg>|<arg>...)` action: everything between the
// outermost parentheses, or "" if the action names a task without one. Taken as a substring
// rather than from the space-split tokens, because an argument may contain spaces of its own.
std::string_view TaskCallArgs(const std::string &actionText) {
	size_t open = actionText.find('(');
	size_t close = actionText.rfind(')');
	if (open == std::string::npos || close == std::string::npos || close < open)
		return {};
	return std::string_view(actionText).substr(open + 1, close - open - 1);
}

std::string TrimWs(std::string_view s) {
	size_t first = s.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) return "";
	return std::string(s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1));
}

// Whether a bare "%...%"/"Referenced..." argument just passes one of the caller's own references
// straight through, as opposed to naming something we have to work out.
bool IsPlainRefName(const std::string &param) {
	return Util::IsReference(param);
}

void DisallowSettingLocationProperties(const std::string &prop) {
	if (prop == "StaticLocation"
		|| prop == "DynamicLocation"
		|| prop == "AtLocation"
		|| prop == "InLocation"
		|| prop == "AtLocationGroup"
		|| prop == "InsideWhat"
		|| prop == "InsideWho"
		|| prop == "HeldByWho"
		|| prop == "OnWhat"
		|| prop == "HeldByWho"
		|| prop == "OnWhat"
		|| prop == "WornByWho"
		|| prop == "PartOfWho"
		|| prop == "PartOfWhat"
		|| prop == "CharacterAtLocation"
		|| prop == "CharInsideWhat"
		|| prop == "CharOnWhat"
		|| prop == "CharOnWho") {
		throw std::logic_error("Task attempted to set location property " + prop);
	}
}
}  // anonymous namespace

Task *Task::CreateFromXML(Game *g, const pugi::xml_node &xmlNode) {
	auto result = new Task;
	result->key = xmlNode.child_value("Key");
	result->priority = ParseInt(xmlNode.child_value("Priority"));
	result->type = Task::ParseType(xmlNode.child_value("Type"));
	//result->descr = xmlNode.child_value("Description");
	result->completionMsg = Game::Get()->CreateDescFromXML(xmlNode.child("CompletionMessage"));
	result->repeatable = ParseBool(xmlNode.child_value("Repeatable"));
	result->restrictions = g->CreateRestrictionsFromXML(xmlNode.child("Restrictions"));
	{
		const auto &foNode = xmlNode.child("FailOverride");
		if (foNode.type() != pugi::node_null)
			result->overrideFailMsg = Game::Get()->CreateDescFromXML(foNode);
	}
	// ADRIFT 5 only ever writes this tag out for the non-default "After" case, so its absence
	// means "Before" -- not the "After" that the in-editor default suggests.
	if (STREQ(xmlNode.child_value("MessageBeforeOrAfter"), "After"))
		result->messagePlacement = MessagePlacement::After;
	// In ADRIFT 5 this is a plain boolean ("keep looking at lower-priority tasks even though this
	// one ran"), despite the enum-looking values it carries over from version 4: only
	// "ContinueAlways" means anything, and the tag is only written out in that case.
	result->alwaysContinue = STREQ(xmlNode.child_value("Continue"), "ContinueAlways");
	// "Aggregate output" defaults on; ADRIFT only writes the tag for the non-default false case, so
	// an absent tag means true (mirroring the DisplayOnce/ReturnToDefault handling in Description).
	{
		const auto &aggNode = xmlNode.child("Aggregate");
		result->aggregateOutput = aggNode.type() == pugi::node_null || ParseBool(aggNode.child_value());
	}
	if (result->type == Type::System) {
		// Both are only written out when set, and only mean anything on a System task -- which is
		// the one kind with no command of its own to be matched against.
		result->locationTrigger = xmlNode.child_value("LocationTrigger");
		result->runImmediately = ParseBool(xmlNode.child_value("RunImmediately"));
	}
	if (result->type == Type::Specific) {
		result->overridesTask = xmlNode.child_value("GeneralTask");
		for (const auto &spcNode: xmlNode.children("Specific")) {
			SpecificInfo spi;
			spi.type = STREQ(spcNode.child_value("Type"), "Text") ? SpType::Text : SpType::Object;
			spi.multiple = ParseBool(spcNode.child_value("Multiple"));
			if (spi.type != SpType::Text) {
				// Usually one <Key> (or one empty <Key/> for a wildcard); a Specific reference
				// with `Multiple` set can instead name several keys at once, requiring this
				// reference to have named exactly that set of objects together (see
				// SpecificTaskMatches). An empty key never combines with real ones, so filter it
				// out rather than counting it as "must also match nothing".
				for (const auto &keyNode: spcNode.children("Key")) {
					std::string k = keyNode.child_value();
					if (!k.empty()) spi.keys.push_back(std::move(k));
				}
			}
			result->specificRefs.push_back(std::move(spi));
		}
		result->overrideType = ParseOverrideType(xmlNode.child_value("SpecificOverrideType"));
	} else if (result->type == Type::General) {
		std::regex translateRefs("%.+?%", std::regex_constants::icase);
		std::vector<std::string> commandStrs;
		for (const auto &cmdNode: xmlNode.children("Command")) {
			auto lines = Util::SplitLines(cmdNode.child_value());
			commandStrs.insert(commandStrs.end(), lines.begin(), lines.end());
		}
		result->commandRegexes.reserve(commandStrs.size());
		for (const auto &cmd: commandStrs) {
			auto transformed = ProcessBlock(ExpandWildcards(cmd));
			RestoreWildcards(transformed);
#ifndef NDEBUG
			std::cout << "Converted \"" << cmd << "\" to \"" << transformed << "\".\n";
#endif
			std::sregex_iterator itBegin(transformed.begin(), transformed.end(), translateRefs);
			std::sregex_iterator itEnd;
			std::vector<std::string> matches;
			for (auto it = itBegin; it != itEnd; it++) {
				const std::smatch &match = *it;
				matches.emplace_back(match.str());
			}
			if (!matches.empty()) {
				std::string rebuilt;
				rebuilt.reserve(transformed.size());
				size_t lastEnd = 0;
				for (auto it = itBegin; it != itEnd; it++) {
					rebuilt.append(transformed, lastEnd, it->position() - lastEnd);
					rebuilt += RegexFragmentForRef(it->str());
					lastEnd = it->position() + it->length();
				}
				rebuilt.append(transformed, lastEnd, transformed.size() - lastEnd);
				transformed = std::move(rebuilt);
			}
			result->commandRegexes.push_back(std::regex(transformed, std::regex_constants::icase));
			result->commandLiterals.push_back(LongestRequiredLiteral(transformed));
			result->groupNumToRef.emplace_back(std::move(matches));
		}
	}

	for (const auto &it: xmlNode.child("Actions").children()) {
		// A malformed action (an argument expression that won't compile, say) must not abort the
		// whole load. Record it as a faulty placeholder that RunActions skips over.
		try {
			result->actions.push_back(Action::CreateFromXML(it));
		} catch (const std::exception &e) {
			LogError("Faulty action in task '" + result->key + "' (" + e.what() + "); it will be skipped.");
			Action faultyAct;
			faultyAct.faulty = true;
			result->actions.push_back(std::move(faultyAct));
		}
	}

	return result;
}

Task::Action Task::Action::CreateFromXML(const pugi::xml_node &xmlNode) {
	Action result;
	std::string name = xmlNode.name();
	std::vector<std::string> tokens = Util::SplitString(xmlNode.child_value(), " ");

	// Deal with, e.g., "move every object with property x set to y to location z"
	if (tokens[0] == "EverythingWithProperty" || tokens[0] == "EveryoneWithProperty") {
		// special cases. love it.
		result.prop = tokens[1];
		std::string temp;
		for (size_t idx = 2; idx < tokens.size()-2; idx++) {
			if (idx != 2)
				temp += ' ';
			temp += tokens[idx];
		}
		// make an expression if necessary
		switch (Game::Get()->GetPropMeta(result.prop)->Type()) {
		case Property::ValueType::Object:
		case Property::ValueType::Enum:
		case Property::ValueType::Map:
			// A field of its own: the per-action-name parsing below re-assigns lhs/rhs from the
			// (now-collapsed) token list without knowing an EverythingWithProperty prefix already
			// claimed a value, so storing this in lhs/rhs would silently lose it.
			result.propCmpValue = temp;
			break;
		case Property::ValueType::Bool:
			// this means that the property must be "set" (i.e., true)
			break;
		default:
			result.expr = Game::Get()->CreateExpression(temp);
		}
		// hackily remove the stuff we've handled so the rest of the function doesn't throw up
		tokens.erase(tokens.begin() + 2, tokens.end() - 2);
	}

	if (name == "MoveObject" || name == "MoveCharacter") {
		result.lhs = tokens[1];
		if (tokens[2] == "ToParentLocation") {
			result.type = ActionType::MoveToParent;
		} else {
			result.rhs = tokens[3];
		}
		if (tokens[2] == "ToLocation") {
			result.type = ActionType::MoveToLocation;
		} else if (tokens[2] == "ToSameLocationAs") {
			result.type = ActionType::MoveToLocationOf;
		} else if (tokens[2] == "ToLocationGroup") {
			result.type = ActionType::MoveToGroup;
		} else if (tokens[2] == "InsideObject") {
			result.type = ActionType::MoveInsideObj;
		} else if (tokens[2] == "OntoObject" || tokens[2] == "OntoCharacter") {
			result.type = ActionType::MoveOntoObj;
		} else if (tokens[2] == "ToCarriedBy") {
			result.type = ActionType::MakeCarriedBy;
		} else if (tokens[2] == "ToWornBy") {
			result.type = ActionType::MakeWornBy;
		} else if (tokens[2] == "ToPartOfCharacter" || tokens[2] == "ToPartOfObject") {
			result.type = ActionType::MakePartOf;
		} else if (tokens[2] == "ToParentLocation") {
			result.type = ActionType::MoveToParent;
		} else if (tokens[2] == "InDirection") {
			result.type = ActionType::MoveInDirection;
		} else if (tokens[2] == "ToStandingOn") {
			result.type = ActionType::MakeStandingOn;
		} else if (tokens[2] == "ToSittingOn") {
			result.type = ActionType::MakeSittingOn;
		} else if (tokens[2] == "ToLyingOn") {
			result.type = ActionType::MakeLyingOn;
		} else if (tokens[2] == "ToSwitchWith") {
			result.type = ActionType::MoveToSwitchWith;
		} else throw std::runtime_error(std::string("Unknown movement command: ") + tokens[2]);
		// reference type determined below.
	} else if (name == "AddObjectToGroup" || name == "AddCharacterToGroup" || name == "AddLocationToGroup") {
		result.type = ActionType::AddToGroup;
		result.lhs = tokens[1];
		result.rhs = tokens[3];
		// reference type determined below.
	} else if (name == "RemoveObjectFromGroup" || name == "RemoveCharacterFromGroup" || name == "RemoveLocationFromGroup") {
		result.type = ActionType::RemoveFromGroup;
		result.lhs = tokens[1];
		result.rhs = tokens[3];
		// reference type determined below.
	} else if (name == "SetProperty") {
		result.lhs = tokens[0];
		result.refType = ActionRefType::SingleObj;
		if (tokens[1] == "StaticOrDynamic") {
			// this action wants to change the object type (i.e. the "StaticOrDynamic" property).
			// we don't treat this as a regular property, so we need to special-case assigning to it.
			result.type = ActionType::SpecialSetDynamic;
			if (tokens[2] == "Dynamic")
				result.rhs = "yes";
			else if (tokens[2] == "Static")
				result.rhs = "no";
			else
				throw std::runtime_error("Task action tried to set StaticOrDynamic to an invalid value: " + tokens[2]);
		} else {  // normal property
			DisallowSettingLocationProperties(tokens[1]);
			result.type = ActionType::SetPropTo;
			result.prop = tokens[1];
			std::string temp;
			for (size_t idx = 2; idx < tokens.size(); idx++) {
				if (idx != 2)
					temp += ' ';
				temp += tokens[idx];
			}
			// make an expression if necessary
			switch (Game::Get()->GetPropMeta(result.prop)->Type()) {
			case Property::ValueType::Object:
			case Property::ValueType::Enum:
			case Property::ValueType::Bool:
				result.rhs = temp;
				break;
			default:
				result.expr = Game::Get()->CreateExpression(temp);
			}
		}
		return result;
	} else if (name == "SetTasks") {
		size_t offset = 0;
		if (tokens[0] == "FOR") {
			// FOR <var> = <from> TO <to> : Execute/Unset <key>[(<args>)] : NEXT <var>
			// The loop variable itself is decorative -- ADRIFT never substitutes it into the
			// executed task's key or arguments -- so we only need the bounds.
			if (tokens.size() < 8 || tokens[2] != "=" || tokens[4] != "TO" || tokens[6] != ":")
				throw std::runtime_error(std::string("Malformed looped SetTasks action: ") + xmlNode.child_value());
			result.loopFrom = ParseInt(tokens[3].c_str());
			result.loopTo = ParseInt(tokens[5].c_str());
			offset = 7;
		}
		if (tokens[offset] == "Execute")
			result.type = ActionType::ExecTask;
		else result.type = ActionType::UnsetTask;
		result.refType = ActionRefType::Task;
		result.lhs = tokens[offset + 1];
		// An Execute action may hand the called task the values for its own %ref%s, e.g.
		// `Execute MoveOutObject (%Player%.Parent)`. Without these the called task's
		// restrictions test references the player never named, and reject it.
		if (result.type == ActionType::ExecTask) {
			for (const auto &raw : Util::SplitList(std::string(TaskCallArgs(xmlNode.child_value())))) {
				std::string param = TrimWs(raw);
				if (param.empty()) continue;
				TaskParam tp;
				if (IsPlainRefName(param)) {
					tp.kind = TaskParam::Kind::Ref;
					tp.text = std::move(param);
				} else if (param.front() == '%' || Game::Get()->ObjectExists(param.substr(0, param.find('.')))) {
					// Either a reference/function to work out ("%ParentOf[%objects%]%") or an
					// object key with properties hung off it ("cl_RelStatObj.StaticLocation").
					// Anything else -- a bare "South", or a key this game doesn't have -- is
					// deliberately not handed to the expression engine, which would try to read
					// it as a variable name and not survive finding no such variable.
					tp.kind = TaskParam::Kind::Expr;
					tp.text = param;
					try {
						tp.expr = Game::Get()->CreateExpression(param);
					} catch (const std::exception &) {
						// Leave expr at 0: an argument we cannot compile resolves to nothing,
						// which costs this one task call rather than the whole game's load.
					}
				} else {
					const size_t dot = param.find('.');
					const std::string prefix = param.substr(0, dot);
					// A genuine bool property after the dot ("BlankCards.ObjectIsAC") already
					// works through the Expr path once %Foo% syntax reaches EvalItemfunc: a Group
					// on the left expands to its members and the bool property filters them down
					// to the ones it holds for, yielding exactly the keys we want. But "Gender" and
					// "StaticOrDynamic" (Npcs.Gender, Everything.StaticOrDynamic) aren't filters at
					// all -- Gender is an enum every character has some value for, and
					// StaticOrDynamic isn't even a property our engine evaluates generically -- so
					// neither takes that path. They are ADRIFT's idiom for "run the called task once
					// per member of this group", and only reach here because `prefix` names a Group,
					// not an Object.
					const Property *suffixProp = dot != std::string::npos
						? Game::Get()->GetPropMeta(param.substr(dot + 1)) : nullptr;
					const bool suffixIsBoolFilter = suffixProp && suffixProp->Type() == Property::ValueType::Bool;
					if (!suffixIsBoolFilter && Game::Get()->GroupExists(prefix)) {
						tp.kind = TaskParam::Kind::GroupIter;
						tp.text = prefix;
					} else {
						tp.kind = TaskParam::Kind::Literal;
						tp.text = std::move(param);
					}
				}
				result.taskParams.push_back(std::move(tp));
			}
		}
		return result;
	} else if (name == "Time") {
		// `Skip "<expr>" turns`. The expression sits between the leading "Skip" and the trailing
		// "turns", and may well contain spaces of its own (`Skip "%delay% + 1" turns`), so
		// everything in between has to be rejoined. Taking tokens[1] alone truncated any such
		// expression to garbage -- `%delay% + 1` came out as `%delay`. No test game has a Time
		// action, which is the only reason that never showed up.
		result.type = ActionType::SkipTurns;
		result.refType = ActionRefType::None;
		if (tokens.size() < 3)
			throw std::runtime_error(std::string("Malformed Time action: ") + xmlNode.child_value());
		std::string temp(tokens[1]);
		for (size_t i = 2; i + 1 < tokens.size(); i++)
			temp += ' ' + tokens[i];
		if (temp.size() >= 2 && temp.front() == '"' && temp.back() == '"')
			temp = temp.substr(1, temp.size() - 2);
		// An expression rather than `lhs`: ADRIFT evaluates it, and routing it through `lhs`
		// would subject it to Perform()'s reference/list expansion instead.
		result.expr = Game::Get()->CreateExpression(temp);
		return result;
	} else if (name == "SetVariable" || name == "IncVariable" || name == "DecVariable") {
		if (tokens[0] == "FOR")
			throw std::runtime_error("Looped variable setting is currently unsupported.");
		if (name == "SetVariable") result.type = ActionType::SetVarTo;
		else if (name == "IncVariable") result.type = ActionType::IncVar;
		else result.type = ActionType::DecVar;
		result.refType = ActionRefType::None;
		result.lhs = tokens[0];
		// ignore tokens[1]
		std::string temp(tokens[2]);
		for (size_t idx = 3; idx < tokens.size(); idx++) {
			temp += ' ' + tokens[idx];
		}
		// remove extraneous quotation marks that Adrift adds here
		if (!temp.empty() && temp[0] == '"' && temp[temp.length() - 1] == '"') {
			std::string varnameToSearch;
			size_t l;
			if ((l = tokens[0].find('[')) != std::string::npos) {
				varnameToSearch = tokens[0].substr(0, l);
			} else varnameToSearch = tokens[0];
			Variable::Type vartype = Game::Get()->GetVariable(varnameToSearch)->GetType();
			auto temp2 = temp.substr(1, temp.length() - 2);
			if (vartype != Variable::Type::String || MaybeIsExpr(temp2))
				temp2.swap(temp);
		}
		try {
			result.expr = Game::Get()->CreateExpression(temp);
		} catch (std::runtime_error &e) {
			throw std::runtime_error(std::string("Unable to process Task action: ") + name + ": " + xmlNode.child_value());
		}
		return result;
	} else if (name == "Conversation") {
		result.refType = ActionRefType::None;
		tokens[0] = Util::ToUpper(tokens[0]);
		if (tokens[0] == "GREET" || tokens[0] == "FAREWELL" || tokens[0] == "ASK" || tokens[0] == "TELL") {
			if (tokens[0] == "GREET") result.type = ActionType::ConvoGreet;
			else if (tokens[0] == "FAREWELL") result.type = ActionType::ConvoFarewell;
			else if (tokens[0] == "ASK") result.type = ActionType::ConvoAsk;
			else result.type = ActionType::ConvoTell;
			result.lhs = tokens[1];
			if (tokens.size() >= 3) {
				// ignore tokens[2]
				for (size_t idx = 3; idx < tokens.size(); idx++) {
					result.rhs += (idx == 3 ? "" : " ") + tokens[idx];
				}
			}
		} else if (tokens[0] == "SAY") {
			result.type = ActionType::ConvoCmd;
			for (size_t idx = 1; idx < tokens.size()-3; idx++) {
				result.rhs += (idx == 1 ? "" : " ") + tokens[idx];
			}
			result.lhs = tokens[tokens.size()-1];
		} else if (tokens[0] == "ENTERWITH") {
			result.type = ActionType::ConvoEnter;
			result.lhs = tokens[1];
		} else if (tokens[0] == "LEAVEWITH") {
			result.type = ActionType::ConvoLeave;
			result.lhs = tokens[1];
		}
		return result;
	} else if (name == "EndGame") {
		result.refType = ActionRefType::None;
		if (tokens[0] == "Neutral")
			result.type = ActionType::GameEndNeutral;
		else if (tokens[0] == "Win")
			result.type = ActionType::GameWin;
		else if (tokens[0] == "Lose")
			result.type = ActionType::GameLose;
		else
			result.type = ActionType::GameContinue;
		return result;
	}

	if (tokens[0] == "Object" || tokens[0] == "Character" || tokens[0] == "Location")
		result.refType = ActionRefType::SingleObj;
	else if (tokens[0] == "EverythingHeldBy")
		result.refType = ActionRefType::ObjsHeldBy;
	else if (tokens[0] == "EverythingWornBy")
		result.refType = ActionRefType::ObjsWornBy;
	else if (tokens[0] == "EverythingInside")
		result.refType = ActionRefType::ObjsInside;
	else if (tokens[0] == "EverythingOn")
		result.refType = ActionRefType::ObjsOn;
	else if (tokens[0] == "EverythingWithProperty")
		result.refType = ActionRefType::ObjsWithProp;
	else if (tokens[0] == "EverythingInGroup")
		result.refType = ActionRefType::ObjsInGroup;
	else if (tokens[0] == "EverythingAtLocation")
		result.refType = ActionRefType::ObjsAtLocation;
	else if (tokens[0] == "EveryoneInside")
		result.refType = ActionRefType::CharsInside;
	else if (tokens[0] == "EveryoneOn")
		result.refType = ActionRefType::CharsOn;
	else if (tokens[0] == "EveryoneWithProperty")
		result.refType = ActionRefType::CharsWithProp;
	else if (tokens[0] == "EveryoneInGroup")
		result.refType = ActionRefType::CharsInGroup;
	else if (tokens[0] == "EveryoneAtLocation")
		result.refType = ActionRefType::CharsAtLocation;
	else if (tokens[0] == "LocationOf")
		result.refType = ActionRefType::LocationOf;
	else if (tokens[0] == "EverywhereInGroup")
		result.refType = ActionRefType::LocationsInGroup;
	else if (tokens[0] == "EverywhereWithProperty")
		result.refType = ActionRefType::LocationsWithProp;
	else throw std::runtime_error(std::string("Unknown reference mode: ") + tokens[0]);

	return result;
}

bool Task::Completed() const {
	return Game::Get()->GetIsTaskCompleted(key);
}

std::pair<bool, DescrRef> Task::Eligible() const {
	if (restrictions == 0)  // i.e., no restrictions set
		return { true, 0 };
	return Game::Get()->GetRestriction(restrictions)->PassRestrictionBlock(true);
}

std::pair<bool, DescrRef> Task::CheckRestrictions() const {
	// A completed, non-repeatable task can never run again.
	if (Completed() && !repeatable) return { false, 0 };
	if (restrictions == 0)  // i.e., no restrictions set
		return { true, 0 };
	return Game::Get()->GetRestriction(restrictions)->PassRestrictionBlock();
}

void Task::RunActions() {
	for (const auto &act : actions) {
		// A faulty action (unparseable at load) is skipped outright. An action that throws mid-way
		// -- a reference to a nonexistent object, say -- is logged and skipped too; the remaining
		// actions still run, and the top-level backstop rolls the turn back if the partial state
		// left behind matters. See starlane-core.cpp.
		if (act.faulty)
			continue;
		try {
			act.Perform();
		} catch (const std::exception &e) {
			LogError("Task '" + key + "' action failed (" + std::string(e.what()) + "); skipped.");
		}
	}
}

void Task::RegisterNotification(const std::string &evtKey, Util::Control::Condition cond) {
	switch (cond) {
		case Util::Control::Condition::Completion:
			completeSubs.emplace_back(evtKey);
			break;
		case Util::Control::Condition::Uncompletion:
			uncompleteSubs.push_back(evtKey);
			break;
	}
}

void Task::RegisterWalkNotification(const std::string &charKey, int32_t walkIdx, Util::Control::Condition cond) {
	switch (cond) {
		case Util::Control::Condition::Completion:
			walkCompleteSubs.emplace_back(charKey, walkIdx);
			break;
		case Util::Control::Condition::Uncompletion:
			walkUncompleteSubs.emplace_back(charKey, walkIdx);
			break;
	}
}

void Task::Uncomplete() {
	if (Completed()) {
		// Clear the flag before telling anyone, the way MarkCompleted does: an event woken by
		// this notification may run a task restricted on "task X must not be complete", and it
		// deserves to see the world as it is by then rather than as it was a moment ago.
		Game::Get()->SetTaskCompleted(key, false);
		SendUncompleteNotifications();
	}
}

void Task::MarkCompleted() {
	if (!Completed()) {
		Game::Get()->SetTaskCompleted(key, true);
		SendCompleteNotifications();
	}
}

// Subscribers are stored as event keys rather than pointers, and resolved here, at notification
// time: tasks live in the shared static data and are never copied, whereas events belong to a
// particular Game instance and are cloned wholesale for every undo state. A pointer taken at load
// time would be aimed at whichever Game happened to be current back then.
void Task::SendCompleteNotifications() const {
	for (const auto &it: completeSubs) {
		if (auto *evt = Game::Get()->MutableEvent(it))
			evt->ReceiveTaskNotification(Util::Control::Condition::Completion, key);
	}
	// Walks resolve the same way, and for the same reason: they belong to a Game instance that gets
	// cloned for undo, so the character is looked up afresh through the live Game each time.
	for (const auto &it: walkCompleteSubs) {
		if (auto *c = AsCharacter(Game::Get()->MutableObject(it.first)))
			c->NotifyWalk(it.second, Util::Control::Condition::Completion, key);
	}
}

void Task::SendUncompleteNotifications() const {
	for (const auto &it: uncompleteSubs) {
		if (auto *evt = Game::Get()->MutableEvent(it))
			evt->ReceiveTaskNotification(Util::Control::Condition::Uncompletion, key);
	}
	for (const auto &it: walkUncompleteSubs) {
		if (auto *c = AsCharacter(Game::Get()->MutableObject(it.first)))
			c->NotifyWalk(it.second, Util::Control::Condition::Uncompletion, key);
	}
}

std::string Task::Action::ResolveParam(const TaskParam &p) {
	switch (p.kind) {
	case TaskParam::Kind::Ref:
		return Game::Get()->GetReference(p.text);
	case TaskParam::Kind::Expr:
		if (p.expr == 0) return "";
		try {
			return Game::Get()->GetExpression(p.expr)->EvaluateStr();
		} catch (const std::exception &) {
			// An argument that cannot be worked out right now (an unimplemented function, a
			// property the object turns out not to have) leaves the called task's reference
			// empty, for its own restrictions to reject as they see fit.
			return "";
		}
	case TaskParam::Kind::Literal:
	case TaskParam::Kind::GroupIter:
		// ExecTask's PerformImpl handles GroupIter itself (it needs to loop, not resolve to a
		// single value); this fallback only matters if some future caller resolves one directly.
		break;
	}
	return p.text;
}

void Task::Action::Perform() const {
	std::string lhs_ = Util::IsReference(lhs) ? Game::Get()->GetReference(lhs) : lhs;
	std::string rhs_ = Util::IsReference(rhs) ? Game::Get()->GetReference(rhs) : rhs;

	// ugly hack is ugly, but I'm not certain what else to do here
	// still beats modifying `this' and hoping that we don't forget to undo the modification
	// before returning.
	if (Util::IsList(lhs_)) {
		Task::Action act(*this);
		act.rhs = rhs_;
		// yet another impromptu string splitting implementation, yay me!
		// remind me why again I thought C++ would be a good language for working with lots of text?
		size_t saveidx = 0;
		for (size_t i = 0; i < lhs_.size(); i++) {
			if (lhs_[i] != '|') continue;
			act.lhs = lhs_.substr(saveidx, i - saveidx - 1);
			act.Perform();
		}
	} else if (Util::IsList(rhs_)) {
		Task::Action act(*this);
		act.lhs = lhs_;
		size_t saveidx = 0;
		for (size_t i = 0; i < lhs_.size(); i++) {
			if (rhs_[i] != '|') continue;
			act.rhs = rhs_.substr(saveidx, i - saveidx - 1);
			act.Perform();
		}
	} else if (lhs != lhs_ || rhs != rhs_) {
		Task::Action act(*this);
		act.lhs = lhs_;
		act.rhs = rhs_;
		act.PerformImpl();
	} else return PerformImpl();
}

// discount lookup tables
static inline constexpr GameObj::HoldingType ActionTypeToHoldingType(Task::ActionType t) {
	switch (t) {
	case Task::ActionType::MoveToLocation:
	case Task::ActionType::MoveToLocationOf:
		return GameObj::HoldingType::AtLocation;
	case Task::ActionType::MoveInsideObj:
	case Task::ActionType::MakeCarriedBy:
	case Task::ActionType::MoveToGroup:
		return GameObj::HoldingType::InObject;
	case Task::ActionType::MoveOntoObj:
		return GameObj::HoldingType::OnObject;
	case Task::ActionType::MakeWornBy:
		return GameObj::HoldingType::Worn;
	case Task::ActionType::MakePartOf:
		return GameObj::HoldingType::PartOf;
	default:
		throw std::logic_error("Invalid shortcut in action handling.");
	}
}

static inline constexpr const char *ActionTypeToPosture(Task::ActionType t) {
	switch (t) {
	case Task::ActionType::MakeLyingOn:
		return "Lying";
	case Task::ActionType::MakeSittingOn:
		return "Sitting";
	case Task::ActionType::MakeStandingOn:
		return "Standing";
	default:
		throw std::logic_error("Invalid posture in posture action handling.");
	}
}

static inline constexpr GameObj::HoldingType ActionRefToHoldingType(Task::ActionRefType t) {
	switch (t) {
	case Task::ActionRefType::ObjsHeldBy:
	case Task::ActionRefType::ObjsInside:
	case Task::ActionRefType::CharsInside:
		return GameObj::HoldingType::InObject;
	case Task::ActionRefType::ObjsAtLocation:
	case Task::ActionRefType::CharsAtLocation:
		return GameObj::HoldingType::AtLocation;
	case Task::ActionRefType::ObjsOn:
	case Task::ActionRefType::CharsOn:
		return GameObj::HoldingType::OnObject;
	case Task::ActionRefType::ObjsWornBy:
		return GameObj::HoldingType::Worn;
	default:
		throw std::logic_error("Invalid shortcut in action ref handling.");
	}
}

static inline constexpr bool ObjIsAppropriate(Task::ActionRefType t, const GameObj *o) {
	switch (t) {
	case Task::ActionRefType::ObjsHeldBy:
	case Task::ActionRefType::ObjsInside:
	case Task::ActionRefType::ObjsWornBy:
	case Task::ActionRefType::ObjsOn:
	case Task::ActionRefType::ObjsAtLocation:
	case Task::ActionRefType::ObjsWithProp:
	case Task::ActionRefType::ObjsInGroup:
		return !AsCharacter(o) && !AsLocation(o);
	case Task::ActionRefType::CharsInside:
	case Task::ActionRefType::CharsOn:
	case Task::ActionRefType::CharsAtLocation:
	case Task::ActionRefType::CharsWithProp:
	case Task::ActionRefType::CharsInGroup:
		return AsCharacter(o);
	case Task::ActionRefType::LocationOf:
	case Task::ActionRefType::LocationsInGroup:
	case Task::ActionRefType::LocationsWithProp:
		return AsLocation(o);
	default:
		return false;
	}
}

void Task::Action::PerformImpl() const {
	// at this stage, all references and lists are resolved.
	Game *g = Game::Get();
	switch (type) {
	case ActionType::MoveToLocation:
	case ActionType::MoveInsideObj:
	case ActionType::MoveOntoObj:
	case ActionType::MakeCarriedBy:
	case ActionType::MakeWornBy:
	case ActionType::MakePartOf:
		PerformMoveTo(rhs);
		break;
	case ActionType::MoveToLocationOf: {
		// "to the same location as X" copies X's whole position, not merely the room it works out
		// to: ADRIFT assigns `dest = obDest.Location.Copy`, so the thing lands held by the same
		// character, or inside the same container, as X is. Games rely on that to swap one object
		// for another in the player's hands (Alyas swaps a plain strip of cloth for a scented one).
		const GameObj *other = g->TryGetObject(rhs);
		if (!other) break;
		// Hidden has no position to copy; neither has a static object, which is placed rather than
		// held, so that one settles for the room it stands in.
		if (other->GetParentKey().empty()) {
			PerformMoveTo("Hidden", GameObj::HoldingType::Hidden);
			break;
		}
		if (const Character *ch = AsCharacter(other)) {
			// A character in or on something takes the object with them, but only if that thing
			// can hold objects that way; otherwise the object goes to the room around them.
			auto rel = ch->GetParentRelation();
			bool inside = rel == GameObj::HoldingType::InObject;
			if (inside || rel == GameObj::HoldingType::OnObject) {
				const GameObj *seat = g->TryGetObject(ch->GetParentKey());
				if (seat && seat->HasProp(inside ? "Container" : "Surface")) {
					PerformMoveTo(ch->GetParentKey(), rel);
					break;
				}
			}
			PerformMoveTo(ch->GetLocationKey(), GameObj::HoldingType::AtLocation);
			break;
		}
		if (!other->IsDynamic()) {
			PerformMoveTo(other->GetLocationKey(), GameObj::HoldingType::AtLocation);
			break;
		}
		PerformMoveTo(other->GetParentKey(), other->GetParentRelation());
		break;
	}
	case ActionType::MoveToParent:  // moving a character "up" one level
		{
			// Where it lands is one thing, but so is *how* it is held there, and that can only
			// come from the parent: stepping out of a duct that sits in a room leaves you in the
			// room, out of a duct inside a crate, inside the crate. PerformMoveTo can't help
			// here -- it reads the relation off the action's own type, which for this action
			// says nothing about where the object is coming from.
			GameObj *self = g->MutableObject(lhs);
			const GameObj *parent = self ? g->TryGetObject(self->GetParentKey()) : nullptr;
			if (parent && !parent->GetParentKey().empty())
				self->MoveTo(parent->GetParentKey(), parent->GetParentRelation());
		}
		break;
	case Starlane::Task::ActionType::MoveToGroup: {
		// A character (the player included -- "MoveCharacter ... ToLocationGroup" moves the
		// player themselves) or a dynamic (takeable) object picks one member of the location
		// group at random and sits there like any other single-location object. A static
		// object instead sits in *every* location the group names at once -- e.g. scenery
		// that should be seen throughout a whole region -- which Location::HoldsDirectly
		// already knows how to answer for the AtLocationGroup relation; there is no single
		// member to pick, and unlike an object, a character can never be spread across a
		// whole group of locations at once.
		GameObj *mover = g->MutableObject(lhs);
		if (!mover) break;
		if (!mover->IsDynamic() && !AsCharacter(mover)) {
			mover->MoveTo(rhs, GameObj::HoldingType::AtLocationGroup);
			break;
		}
		const Group *grp = g->GetGroup(rhs);
		if (!grp) break;
		const auto &members = grp->GetAllMembers();
		if (members.empty()) break;
		auto it = members.begin();
		std::advance(it, RandomInt((uint32_t) members.size() - 1));
		mover->MoveTo(*it, GameObj::HoldingType::AtLocation);
		break;
	}
	case Starlane::Task::ActionType::MoveInDirection: {
		// Move the character named by `lhs` through the exit in direction `rhs` (a
		// canonical direction like "North"). Movement-related tasks generally validate
		// the route in their own restrictions first, but we re-check here so that
		// running this action directly can never teleport a character through a
		// nonexistent or blocked (e.g. closed-door) exit.
		auto *mover = AsCharacter(g->MutableObject(lhs));
		if (!mover)
			break;
		const auto *loc = mover->GetLocation();
		if (!loc || !loc->HasExit(rhs) || !mover->HasRoute(rhs).first)
			break;
		std::string dest = loc->GetExit(rhs).destination;
		// A movement destination may name a location group, in which case a member is
		// picked at random (mirroring the original runner).
		if (g->GroupExists(dest)) {
			const auto &members = g->GetGroup(dest)->GetAllMembers();
			if (members.empty())
				break;
			auto it = members.begin();
			std::advance(it, RandomInt((uint32_t) members.size() - 1));
			dest = *it;
		}
		mover->MoveTo(dest, GameObj::HoldingType::AtLocation);
		break;
	}
	case ActionType::AddToGroup:
	case ActionType::RemoveFromGroup:
	{
		// The only difference between these two types of actions is which function we call on the group,
		// so shorten this by storing the appropriate pointer now and then using it later:
		auto addOrRemove = type == ActionType::AddToGroup ? static_cast<void (Group:: *)(GameObj *)>(&Group::AddObj) : static_cast<void (Group:: *)(GameObj *)>(&Group::RemoveObj);
		auto grp = g->MutableGroup(rhs);
		switch (refType) {
		case ActionRefType::SingleObj:
			if (g->TryGetObject(lhs)) (grp->*addOrRemove)(g->MutableObject(lhs));
			break;
		case ActionRefType::ObjsHeldBy:
		case ActionRefType::ObjsInside:
		case ActionRefType::ObjsWornBy:
		case ActionRefType::ObjsOn:
		case ActionRefType::ObjsAtLocation:
		case ActionRefType::CharsInside:
		case ActionRefType::CharsOn:
		case ActionRefType::CharsAtLocation: {
			auto &allObjs = g->GetAllObjects();
			auto ht = ActionRefToHoldingType(refType);
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (ObjIsAppropriate(refType, o) && o->GetParentKey() == lhs && o->GetParentRelation() == ht)
					(grp->*addOrRemove)(g->MutableObject(oi));
			}
		}
			break;
		case ActionRefType::ObjsWithProp:
		case ActionRefType::CharsWithProp: {
			auto &allObjs = g->GetAllObjects();
			auto propType = g->GetPropMeta(prop)->Type();
			switch (propType) {
			case Property::ValueType::Bool:
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (ObjIsAppropriate(refType, o) && o->GetBoolProp(prop))
						(grp->*addOrRemove)(g->MutableObject(oi));
				}
				break;
			// The comparison value lives in propCmpValue, not rhs -- rhs holds the destination
			// group's key here (set once, above, for every case of this switch).
			case Property::ValueType::Object:
			case Property::ValueType::Enum:
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (ObjIsAppropriate(refType, o) && o->GetStrProp(prop) == propCmpValue)
						(grp->*addOrRemove)(g->MutableObject(oi));
				}
				break;
			case Property::ValueType::Map:
			case Property::ValueType::Int: {
				auto tmpInt = propType == Property::ValueType::Map ? ParseInt(propCmpValue.c_str()) : g->GetExpression(expr)->EvaluateInt();
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (ObjIsAppropriate(refType, o) && o->GetIntProp(prop) == tmpInt)
						(grp->*addOrRemove)(g->MutableObject(oi));
				}
				break;
			}
			case Property::ValueType::Text: {
				std::string tmpTxt(g->GetExpression(expr)->EvaluateStr());
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (ObjIsAppropriate(refType, o) && o->GetStrProp(prop) == tmpTxt)
						(grp->*addOrRemove)(g->MutableObject(oi));
				}
				break;
			}
			case Property::ValueType::ErrorType:
				UNREACHABLE();
			}
			break;
		}
		case ActionRefType::ObjsInGroup:
		case ActionRefType::CharsInGroup:
		case ActionRefType::LocationOf:
		case ActionRefType::LocationsInGroup:
		case ActionRefType::LocationsWithProp: {
			auto &allObjs = g->GetAllObjects();
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (ObjIsAppropriate(refType, o) && o->IsMemberOfGroup(lhs))
					(grp->*addOrRemove)(g->MutableObject(oi));
			}
		}
			break;
		case ActionRefType::Task:
			throw std::runtime_error("Task tried to move a task.");
		case ActionRefType::None:
			throw std::runtime_error("Task tried to move nothing.");
		default:
			break;
		}
		break;
	}
	case ActionType::MakeStandingOn:
	case ActionType::MakeSittingOn:
	case ActionType::MakeLyingOn:
		switch (refType) {
		case ActionRefType::SingleObj: {
			GameObj *theObj = Game::Get()->MutableObjectChecked(lhs);
			Character *c;
			if (!(c = AsCharacter(theObj)))
				throw std::runtime_error("Tried to set the posture of a non-character");
			c->MakePosture(rhs, ActionTypeToPosture(type));
		}
			break;
		case ActionRefType::CharsAtLocation:
		case ActionRefType::CharsInside:
		case ActionRefType::CharsOn: {
			auto &allObjs = g->GetAllObjects();
			auto ht = ActionRefToHoldingType(refType);
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (AsCharacter(o) && o->GetParentKey() == lhs && o->GetParentRelation() == ht)
					AsCharacter(g->MutableObject(oi))->MakePosture(rhs, ActionTypeToPosture(type));
			}
		}
			break;
		case ActionRefType::CharsWithProp: {
			auto &allObjs = g->GetAllObjects();
			auto propType = g->GetPropMeta(prop)->Type();
			switch (propType) {
			case Property::ValueType::Bool:
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (AsCharacter(o) && o->GetBoolProp(prop))
						AsCharacter(g->MutableObject(oi))->MakePosture(rhs, ActionTypeToPosture(type));
				}
				break;
			case Property::ValueType::Object:
			case Property::ValueType::Enum:
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (AsCharacter(o) && o->GetStrProp(prop) == lhs)
						AsCharacter(g->MutableObject(oi))->MakePosture(rhs, ActionTypeToPosture(type));
				}
				break;
			case Property::ValueType::Map:
			case Property::ValueType::Int: {
				auto tmpInt = propType == Property::ValueType::Map ? ParseInt(lhs.c_str()) : g->GetExpression(expr)->EvaluateInt();
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (AsCharacter(o) && o->GetIntProp(prop) == tmpInt)
						AsCharacter(g->MutableObject(oi))->MakePosture(rhs, ActionTypeToPosture(type));
				}
				break;
			}
			case Property::ValueType::Text: {
				std::string tmpTxt(g->GetExpression(expr)->EvaluateStr());
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (AsCharacter(o) && o->GetStrProp(prop) == tmpTxt)
						AsCharacter(g->MutableObject(oi))->MakePosture(rhs, ActionTypeToPosture(type));
				}
				break;
			}
			case Property::ValueType::ErrorType:
				UNREACHABLE();
			}
			break;
		}
		case ActionRefType::CharsInGroup: {
			auto &allObjs = g->GetAllObjects();
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (AsCharacter(o) && o->IsMemberOfGroup(lhs))
					AsCharacter(g->MutableObject(oi))->MakePosture(rhs, ActionTypeToPosture(type));
			}
		}
			break;
		default:
			throw std::runtime_error("Tried to change the posture of a non-character.");
		}
		break;
	case ActionType::MoveToSwitchWith:
		switch (refType) {
		case ActionRefType::SingleObj:
			PerformSwitchWith(lhs);
			break;
		case ActionRefType::CharsAtLocation:
		case ActionRefType::CharsInside:
		case ActionRefType::CharsOn: {
			auto &allObjs = g->GetAllObjects();
			auto ht = ActionRefToHoldingType(refType);
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (AsCharacter(o) && o->GetParentKey() == lhs && o->GetParentRelation() == ht)
					PerformSwitchWith(o->Key());
			}
		}
			break;
		case ActionRefType::CharsWithProp: {
			auto &allObjs = g->GetAllObjects();
			auto propType = g->GetPropMeta(prop)->Type();
			switch (propType) {
			case Property::ValueType::Bool:
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (AsCharacter(o) && o->GetBoolProp(prop))
						PerformSwitchWith(o->Key());
				}
				break;
			case Property::ValueType::Object:
			case Property::ValueType::Enum:
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (AsCharacter(o) && o->GetStrProp(prop) == lhs)
						PerformSwitchWith(o->Key());
				}
				break;
			case Property::ValueType::Map:
			case Property::ValueType::Int: {
				auto tmpInt = propType == Property::ValueType::Map ? ParseInt(lhs.c_str()) : g->GetExpression(expr)->EvaluateInt();
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (AsCharacter(o) && o->GetIntProp(prop) == tmpInt)
						PerformSwitchWith(o->Key());
				}
				break;
			}
			case Property::ValueType::Text: {
				std::string tmpTxt(g->GetExpression(expr)->EvaluateStr());
				for (size_t oi = 0; oi < allObjs.size(); oi++) {
					const GameObj *o = allObjs[oi];
					if (AsCharacter(o) && o->GetStrProp(prop) == tmpTxt)
						PerformSwitchWith(o->Key());
				}
				break;
			}
			case Property::ValueType::ErrorType:
				UNREACHABLE();
			}
			break;
		}
		case ActionRefType::CharsInGroup: {
			auto &allObjs = g->GetAllObjects();
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (AsCharacter(o) && o->IsMemberOfGroup(lhs))
					PerformSwitchWith(o->Key());
			}
		}
			break;
		default:
			throw std::runtime_error("Tried to switch places with a non-character.");
		}
		break;
	case ActionType::SetVarTo:
	case ActionType::IncVar:
	case ActionType::DecVar: {
		Variable *var;
		uint32_t idx = 1;
		if (size_t bracket = lhs.find_first_of('['); bracket != std::string::npos) {
			var = g->MutableVariable(lhs.substr(0, bracket));
			auto idxStr = lhs.substr(bracket+1, lhs.length() - (bracket+2));
			// The index itself can be a variable name rather than a literal integer.
			if (IsDigits(idxStr.c_str())) {
				idx = ParseInt(idxStr.c_str());
			} else {
				idx = g->GetVariable(idxStr)->GetValue<int64_t>();
			}
		} else var = g->MutableVariable(lhs);
		
		if (type == ActionType::SetVarTo) {
			switch (var->GetType()) {
			case Variable::Type::Int:
			case Variable::Type::IntArray:
				var->SetValue(g->GetExpression(expr)->EvaluateInt(), idx);
				break;
			case Variable::Type::String:
			case Variable::Type::StringArray:
				var->SetValue(g->GetExpression(expr)->EvaluateStr(), idx);
				break;
			}
			break;
		}

		if (var->GetType() != Variable::Type::Int && var->GetType() != Variable::Type::IntArray)
			throw std::runtime_error("Trying to increment/decrement a text variable.");
		if (type == ActionType::IncVar)
			var->SetValue(var->GetValue<int64_t>() + g->GetExpression(expr)->EvaluateInt(), idx);
		else if (type == ActionType::DecVar)
			var->SetValue(var->GetValue<int64_t>() - g->GetExpression(expr)->EvaluateInt(), idx);
	}
		break;
	case ActionType::SetPropTo: {
		auto *target = g->MutableObjectChecked(lhs);
		switch (Game::Get()->GetPropMeta(prop)->Type()) {
		case Property::ValueType::Object:
		case Property::ValueType::Enum:
			target->SetPropValue(prop, rhs);
			break;
		case Property::ValueType::Bool:
			target->SetPropValue(prop, ParseBool(rhs.c_str()));
			break;
		case Property::ValueType::Int:
			target->SetPropValue(prop, g->GetExpression(expr)->EvaluateInt());
			break;
		case Property::ValueType::Text:
			target->SetPropValue(prop, g->GetExpression(expr)->EvaluateStr());
			break;
		case Property::ValueType::ErrorType:
			UNREACHABLE();
			break;  // to satisfy MSVC code analysis, which would otherwise assume that this falls through.
		default:
			target->SetPropValue(prop, rhs);  // meh
			break;
		}
	}
		break;
	case Starlane::Task::ActionType::ExecTask: {
		// A GroupIter param ("Npcs.Gender") makes this an iterating call: unlike every other
		// param kind, which resolves to one value shared by every call, it runs the task once
		// per member of the named group, with that member bound in its place.
		size_t groupParamIdx = taskParams.size();
		for (size_t i = 0; i < taskParams.size(); i++) {
			if (taskParams[i].kind == TaskParam::Kind::GroupIter) { groupParamIdx = i; break; }
		}
		const Group *grp = groupParamIdx < taskParams.size() ? g->GetGroup(taskParams[groupParamIdx].text) : nullptr;
		if (groupParamIdx < taskParams.size() && !grp) break;
		const auto singleDummyMember = std::set<std::string>{std::string()};
		const auto &members = grp ? grp->GetAllMembers() : singleDummyMember;
		for (const auto &member : members) {
			std::vector<std::string> args;
			args.reserve(taskParams.size());
			for (size_t i = 0; i < taskParams.size(); i++)
				args.push_back(i == groupParamIdx ? member : ResolveParam(taskParams[i]));
			for (int64_t i = loopFrom; i <= loopTo; i++)
				g->ExecuteTaskByKey(lhs, args);
		}
		break;
	}
	case Starlane::Task::ActionType::UnsetTask:
		for (int64_t i = loopFrom; i <= loopTo; i++)
			if (Task *t = g->GetTask(lhs))
				t->Uncomplete();
		break;
	case Starlane::Task::ActionType::SkipTurns: {
		// "Skip N turns": let that many turns' worth of events run here and now, part-way through
		// this task's list of actions, as ADRIFT does. Note that the turn the command itself costs
		// comes on top of these -- ProcessInput ticks once more once this task is done -- so
		// "skip 3 turns" moves the world on by four. That is the reference's arithmetic, not a
		// slip of ours.
		auto n = g->GetExpression(expr)->EvaluateInt();
		for (int64_t i = 0; i < n; i++)
			g->TurnTick();
		break;
	}
	case Starlane::Task::ActionType::ConvoGreet:
		break;
	case Starlane::Task::ActionType::ConvoFarewell:
		break;
	case Starlane::Task::ActionType::ConvoAsk:
		break;
	case Starlane::Task::ActionType::ConvoTell:
		break;
	case Starlane::Task::ActionType::ConvoCmd:
		break;
	case Starlane::Task::ActionType::ConvoEnter:
		break;
	case Starlane::Task::ActionType::ConvoLeave:
		break;
	case Starlane::Task::ActionType::GameWin:
		g->EndGame(Game::Ending::Win);
		break;
	case Starlane::Task::ActionType::GameLose:
		g->EndGame(Game::Ending::Lose);
		break;
	case Starlane::Task::ActionType::GameEndNeutral:
		g->EndGame(Game::Ending::Neutral);
		break;
	case Starlane::Task::ActionType::GameContinue:
		break;
	case ActionType::SpecialSetDynamic: {
		auto *target = g->MutableObjectChecked(lhs);
		target->SetDynamic(ParseBool(rhs.c_str()));
	}
		break;
	default:
		break;
	}
}

void Task::Action::PerformMoveTo(const std::string &moveTarget,
                                 std::optional<GameObj::HoldingType> relation) const {
	auto *g = Game::Get();
	const GameObj::HoldingType landsAs = relation ? *relation : ActionTypeToHoldingType(type);
	switch (refType) {
	case ActionRefType::SingleObj:
		g->MutableObjectChecked(lhs)->MoveTo(moveTarget, landsAs);
		break;
	case ActionRefType::ObjsHeldBy:
	case ActionRefType::ObjsInside:
	case ActionRefType::ObjsWornBy:
	case ActionRefType::ObjsOn:
	case ActionRefType::ObjsAtLocation:
	case ActionRefType::CharsInside:
	case ActionRefType::CharsOn:
	case ActionRefType::CharsAtLocation: {
		auto &allObjs = g->GetAllObjects();
		auto ht = ActionRefToHoldingType(refType);
		for (size_t oi = 0; oi < allObjs.size(); oi++) {
			const GameObj *o = allObjs[oi];
			if (ObjIsAppropriate(refType, o) && o->GetParentKey() == lhs && o->GetParentRelation() == ht)
				g->MutableObject(oi)->MoveTo(moveTarget, landsAs);
		}
	}
		break;
	case ActionRefType::ObjsWithProp:
	case ActionRefType::CharsWithProp: {
		auto &allObjs = g->GetAllObjects();
		auto propType = g->GetPropMeta(prop)->Type();
		switch (propType) {
		case Property::ValueType::Bool:
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (ObjIsAppropriate(refType, o) && o->GetBoolProp(prop))
					g->MutableObject(oi)->MoveTo(moveTarget, landsAs);
			}
			break;
		case Property::ValueType::Object:
		case Property::ValueType::Enum:
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (ObjIsAppropriate(refType, o) && o->GetStrProp(prop) == propCmpValue)
					g->MutableObject(oi)->MoveTo(moveTarget, landsAs);
			}
			break;
		case Property::ValueType::Map:
		case Property::ValueType::Int: {
			auto tmpInt = propType == Property::ValueType::Map ? ParseInt(propCmpValue.c_str()) : g->GetExpression(expr)->EvaluateInt();
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (ObjIsAppropriate(refType, o) && o->GetIntProp(prop) == tmpInt)
					g->MutableObject(oi)->MoveTo(moveTarget, landsAs);
			}
			break;
		}
		case Property::ValueType::Text: {
			std::string tmpTxt(g->GetExpression(expr)->EvaluateStr());
			for (size_t oi = 0; oi < allObjs.size(); oi++) {
				const GameObj *o = allObjs[oi];
				if (ObjIsAppropriate(refType, o) && o->GetStrProp(prop) == tmpTxt)
					g->MutableObject(oi)->MoveTo(moveTarget, landsAs);
			}
			break;
		}
		case Property::ValueType::ErrorType:
			UNREACHABLE();
		}
		break;
	}
	case ActionRefType::ObjsInGroup:
	case ActionRefType::CharsInGroup: {
		auto &allObjs = g->GetAllObjects();
		for (size_t oi = 0; oi < allObjs.size(); oi++) {
			const GameObj *o = allObjs[oi];
			if (ObjIsAppropriate(refType, o) && o->IsMemberOfGroup(lhs))
				g->MutableObject(oi)->MoveTo(moveTarget, landsAs);
		}
	}
		break;
	case ActionRefType::LocationOf:
	case ActionRefType::LocationsInGroup:
	case ActionRefType::LocationsWithProp:
		throw std::runtime_error("Task tried to move a location.");
	case ActionRefType::Task:
		throw std::runtime_error("Task tried to move a task.");
	case ActionRefType::None:
		throw std::runtime_error("Task tried to move nothing.");
	default:
		break;
	}
}

void Task::Action::PerformSwitchWith(const std::string &chKey) const {
	Game *g = Game::Get();
	// If either side is the player, nobody actually moves -- the player just starts playing as
	// whichever of the two they weren't already.
	if (chKey == g->GetPlayerKey() || rhs == g->GetPlayerKey()) {
		g->SwitchPlayerCharacter(chKey == g->GetPlayerKey() ? rhs : chKey);
		return;
	}
	// Otherwise, per the original ADRIFT runner: only the second character (`rhs`) actually
	// moves, teleporting to wherever `chKey` is via a raw location copy with none of the arrival
	// bookkeeping a proper move would do. `chKey` itself never goes anywhere -- ADRIFT's own
	// implementation swaps both characters' locations and then immediately moves `chKey` back to
	// where it started, which nets out to exactly this.
	const GameObj *ch = g->TryGetObject(chKey);
	GameObj *other = g->MutableObject(rhs);
	if (!ch || !other) return;
	other->CopyLocationFrom(*ch);
	other->SetPropValue("CharacterPosition", ch->GetStrProp("CharacterPosition"));
}

}
