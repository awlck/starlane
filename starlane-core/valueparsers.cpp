#include "valueparsers.h"

// The C++ STL header for `std::string':
#include <string>
// The C library header for `strcmp':
// (Microsoft's STL includes this implicitly, but others don't)
#include <string.h>

#include <magic_enum.hpp>

#include "game.h"
#include "gamecontent/description.h"
#include "gamecontent/event.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/character.h"
#include "gamecontent/property.h"
#include "gamecontent/restriction.h"
#include "gamecontent/task.h"
#include "gamecontent/userfunc.h"
#include "gamecontent/utility.h"

// Convenience macro to insert the stringified name of the type in question,
// for the benefit of IDE auto-renaming of types.
#define VALERR(T, val) std::runtime_error(std::string("Invalid value for type `" #T "`: ") + val)


namespace Starlane {

bool ParseBool(const char *txt) {
	if (txt == nullptr) return false;
	if (*txt == '\0') return false;
	if (STREQ(txt, "1")) return true;
	if (STREQ(txt, "yes")) return true;
	if (STREQ(txt, "Yes")) return true;
	if (STREQ(txt, "YES")) return true;
	if (STREQ(txt, "true")) return true;
	if (STREQ(txt, "True")) return true;
	if (STREQ(txt, "TRUE")) return true;
	if (STREQ(txt, "0")) return false;
	if (STREQ(txt, "no")) return false;
	if (STREQ(txt, "No")) return false;
	if (STREQ(txt, "NO")) return false;
	if (STREQ(txt, "false")) return false;
	if (STREQ(txt, "False")) return false;
	if (STREQ(txt, "FALSE")) return false;
	if (STREQ(txt, "<Selected>")) return true;
	if (STREQ(txt, "<Unselected>")) return false;
	throw VALERR(bool, txt);
}

int64_t ParseInt(const char *txt) {
	return std::stoll(txt);
}

uint32_t ParseOleColor(const char *txt) {
	auto raw = (uint32_t) ParseInt(txt);
	uint32_t r = raw & 0xFF;
	uint32_t g = (raw >> 8) & 0xFF;
	uint32_t b = (raw >> 16) & 0xFF;
	return (r << 16) | (g << 8) | b;
}

bool IsDigits(const char *txt) {
	if (!txt || !*txt) return false;
	for (; *txt; ++txt) {
		if (!isdigit(*txt)) return false;
	}
	return true;
}

Description::Display Description::DisplayValue(const char *txt) {
	if (STREQ(txt, "StartDescriptionWithThis"))
		return Description::Display::BeginHere;
	if (STREQ(txt, "StartAfterDefaultDescription"))
		return Description::Display::AfterDefault;
	if (STREQ(txt, "AppendToPreviousDescription"))
		return Description::Display::Append;
	throw VALERR(Description::Display, txt);
}

Property::ValueType Property::ParseValueType(const char *txt) {
	if (STREQ(txt, "SelectionOnly"))
		return Property::ValueType::Bool;
	if (STREQ(txt, "Integer"))
		return Property::ValueType::Int;
	if (STREQ(txt, "Text"))
		return Property::ValueType::Text;
	if (STREQ(txt, "ObjectKey"))
		return Property::ValueType::Object;
	if (STREQ(txt, "CharacterKey"))
		return Property::ValueType::Object;
	if (STREQ(txt, "LocationKey"))
		return Property::ValueType::Object;
	if (STREQ(txt, "LocationGroupKey"))
		return Property::ValueType::Object;
	if (STREQ(txt, "StateList"))
		return Property::ValueType::Enum;
	if (STREQ(txt, "ValueList"))
		return Property::ValueType::Map;
	throw VALERR(Property::ValueType, txt);
}

Task::Type Task::ParseType(const char *txt) {
	auto tmp = magic_enum::enum_cast<Task::Type>(txt);
	if (!tmp.has_value())
		throw VALERR(Task::Type, txt);
	return tmp.value();
}

Task::OverrideType Task::ParseOverrideType(const char *txt) {
	if (STREQ(txt, "BeforeTextAndActions"))
		return OverrideType::BeforeParent | OverrideType::ParentText | OverrideType::ParentActions;
	if (STREQ(txt, "BeforeActionsOnly"))
		return OverrideType::BeforeParent | OverrideType::ParentActions;
	if (STREQ(txt, "BeforeTextOnly"))
		return OverrideType::BeforeParent | OverrideType::ParentText;
	if (STREQ(txt, "Override"))
		return OverrideType::Override;
	if (STREQ(txt, "AfterTextOnly"))
		return OverrideType::AfterParent | OverrideType::ParentText;
	if (STREQ(txt, "AfterActionsOnly"))
		return OverrideType::AfterParent | OverrideType::ParentActions;
	if (STREQ(txt, "AfterTextAndActions"))
		return OverrideType::AfterParent | OverrideType::ParentText | OverrideType::ParentActions;
	throw VALERR(Task::OverrideType, txt);
}

std::pair<GameObj::HoldingType, std::string> GameObj::ParseHoldingType(const char *txt) {
	if (STREQ(txt, "Hidden") || STREQ(txt, "Nowhere"))
		return { GameObj::HoldingType::Hidden, "" };
	if (STREQ(txt, "Single Location"))
		return { GameObj::HoldingType::AtLocation, "AtLocation" };
	if (STREQ(txt, "In Location"))
		return { GameObj::HoldingType::AtLocation, "InLocation" };
	if (STREQ(txt, "Location Group"))
		return { GameObj::HoldingType::AtLocationGroup, "AtLocationGroup" };
	if (STREQ(txt, "Everywhere"))
		return { GameObj::HoldingType::Everywhere, "" };
	if (STREQ(txt, "Inside Object"))
		return { GameObj::HoldingType::InObject, "InsideWhat" };
    if (STREQ(txt, "Held By Character"))
        return { GameObj::HoldingType::InObject, "HeldByWho" };
    if (STREQ(txt, "On Object"))
		return { GameObj::HoldingType::OnObject, "OnWhat" };
	if (STREQ(txt, "Worn By Character"))
		return { GameObj::HoldingType::Worn, "WornByWho" };
	if (STREQ(txt, "Part of Character"))
		return { GameObj::HoldingType::PartOf, "PartOfWho" };
	if (STREQ(txt, "Part of Object"))
		return { GameObj::HoldingType::PartOf, "PartOfWhat" };
	throw VALERR(GameObj::HoldingType, txt);
}

std::pair<GameObj::HoldingType, std::string> Character::ParseHoldingType(const char *txt) {
	if (STREQ(txt, "Hidden") || STREQ(txt, "Nowhere"))
		return { GameObj::HoldingType::Hidden, "" };
	if (STREQ(txt, "At Location"))
		return { GameObj::HoldingType::AtLocation, "CharacterAtLocation" };
	if (STREQ(txt, "In Object"))
		return { GameObj::HoldingType::InObject, "CharInsideWhat" };
	if (STREQ(txt, "On Object"))
		return { GameObj::HoldingType::OnObject, "CharOnWhat" };
	if (STREQ(txt, "On Character"))
		return { GameObj::HoldingType::OnObject, "CharOnWho" };
	throw VALERR(GameObj::HoldingType, txt);
}

Event::StartType Event::ParseStartType(const char *txt) {
	if (STREQ(txt, "Immediately"))
		return StartType::Immediately;
	if (STREQ(txt, "BetweenXandYTurns"))
		return StartType::AfterDelay;
	if (STREQ(txt, "AfterATask"))
		return StartType::TaskBased;
	// In the ADRIFT source code, this enum starts with 1, yet somehow '0' appears
	// in at least one game. I'm assuming that means the event never actually happens?
	if (STREQ(txt, "0"))
		return StartType::Invalid;
	throw VALERR(Event::StartType, txt);
}

Event::TimeType Event::ParseTimeType(const char *txt) {
	// A missing element (empty here) means turn-based, as in ADRIFT: both an event's EventType and
	// a subevent's Measure default to their first enum value -- TurnBased / Turns -- and are only
	// set when the element is actually present. Older games (e.g. Jacaranda Jim) omit it entirely.
	if (!txt || !*txt)
		return TimeType::Turns;
	if (STREQ(txt, "TurnBased") || STREQ(txt, "Turns"))
		return TimeType::Turns;
	if (STREQ(txt, "TimeBased") || STREQ(txt, "Seconds"))
		return TimeType::RealTime;
	throw VALERR(Event::TimeType, txt);
}

Event::SEType Event::ParseSEType(const char *txt) {
	auto tmp = magic_enum::enum_cast<SEType>(txt);
	if (!tmp.has_value())
		throw VALERR(Event::SEType, txt);
	return tmp.value();
}

Event::SERefType Event::ParseSERefType(const char *txt) {
	if (STREQ(txt, "FromLastSubEvent"))
		return SERefType::LastSubEvent;
	if (STREQ(txt, "FromStartOfEvent"))
		return SERefType::EventBegin;
	if (STREQ(txt, "BeforeEndOfEvent"))
		return SERefType::EventEnd;
	throw VALERR(Event::SERefType, txt);
}

Restriction::TargetType Restriction::ParseTargetType(const char *txt) {
	if (STREQ(txt, "Location") || STREQ(txt, "Object") || STREQ(txt, "Character") || STREQ(txt, "Item"))
		return TargetType::Object;
	if (STREQ(txt, "Property"))
		return TargetType::Property;
	if (STREQ(txt, "Task"))
		return TargetType::Task;
	if (STREQ(txt, "Variable"))
		return TargetType::Variable;
	if (STREQ(txt, "Direction"))
		return TargetType::Direction;
	if (STREQ(txt, "Expression"))
		return TargetType::Expression;
	throw VALERR(Restriction::TargetType, txt);
}

UserFunction::ArgType UserFunction::ParseArgType(const char *txt) {
	auto tmp = magic_enum::enum_cast<UserFunction::ArgType>(txt);
	if (!tmp.has_value())
		throw VALERR(UserFunction::ArgType, txt);
	return tmp.value();
}

Util::Range::Range(const char *txt) {
	// Every path below delegates to one of the other constructors, so that all three members are
	// always assigned. The old code only assigned `min`/`max` from inside its parsing loop, which
	// left `max` indeterminate for anything that reached the loop but didn't spell out a full
	// "<min> to <max>" -- and trailing whitespace was enough to get there, since a lone "1 " is
	// not all-digits and splits into just ["1"]. The first Value() call would then hand RandomInt
	// bounds it made up, and walk straight into UB inside uniform_int_distribution. Nothing
	// called Value() on one of these until events learned to run, which is the only reason this
	// never went off.
	std::string s(txt ? txt : "");
	const auto begin = s.find_first_not_of(" \t\r\n");
	s = (begin == std::string::npos) ? std::string()
	                                 : s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);

	if (s.empty()) {
		// An absent <Length>/<When> means "no time at all", which is a perfectly sensible thing
		// for a game to say. (This used to throw.)
		*this = Util::Range((uint32_t) 0);
		return;
	}
	if (IsDigits(s.c_str())) {
		*this = Util::Range((uint32_t) ParseInt(s.c_str()));
		return;
	}
	// The only other shape ADRIFT ever writes is "<min> to <max>".
	auto tokens = Util::SplitString(s, " +");
	if (tokens.size() != 3 || !IsDigits(tokens[0].c_str()) || tokens[1] != "to" ||
			!IsDigits(tokens[2].c_str()))
		throw VALERR(Util::Range, txt);
	auto lo = (uint32_t) ParseInt(tokens[0].c_str());
	auto hi = (uint32_t) ParseInt(tokens[2].c_str());
	// The editor won't write "10 to 1", but a hand-edited game file could, and RandomInt requires
	// its bounds the right way round.
	if (lo > hi) std::swap(lo, hi);
	*this = Util::Range(lo, hi);
}

const char *SkipText(const char *input, const char *toSkip) {
	const char *output = strstr(input, toSkip);
	if (output != input) {
		// `toSkip` not first in input string
		return nullptr;
	}
	// Since the string `input` starts with `toSkip`, we know that `input` must be at least
	// as long as `toSkip`. In other words, this is always safe:
	return output + strlen(toSkip);
}

ReferralPerson Game::ParseReferralPerson(const char *txt) {
	auto tmp = magic_enum::enum_cast<ReferralPerson>(txt);
	if (!tmp.has_value())
		throw VALERR(Game::ReferralPerson, txt);
	return tmp.value();
}

}