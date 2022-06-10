#include "valueparsers.h"

// The C++ STL header for `std::string':
#include <string>
// The C library header for `strcmp':
// (Microsoft's STL includes this implicitly, but others don't)
#include <string.h>

#include "gamecontent/description.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/character.h"
#include "gamecontent/property.h"
#include "gamecontent/task.h"

// Convenience macro to insert the stringified name of the type in question,
// for the benefit of IDE auto-renaming of types.
//#define VALERR(T, val) Starlane::ValueError(#T, val)
#define VALERR(T, val) std::runtime_error(std::string("Invalid value for type `" #T "`: ") + val)

namespace Starlane {

bool ParseBool(const char *txt) {
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
	throw VALERR(bool, txt);
}

int64_t ParseInt(const char *txt) {
	return std::stoll(txt);
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
	if (STREQ(txt, "General"))
		return Task::Type::General;
	if (STREQ(txt, "Specific"))
		return Task::Type::Specific;
	if (STREQ(txt, "System"))
		return Task::Type::System;
	throw VALERR(Task::Type, txt);
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

}