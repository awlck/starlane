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
	if (strcmp(txt, "1") == 0) return true;
	if (strcmp(txt, "yes") == 0) return true;
	if (strcmp(txt, "Yes") == 0) return true;
	if (strcmp(txt, "YES") == 0) return true;
	if (strcmp(txt, "true") == 0) return true;
	if (strcmp(txt, "True") == 0) return true;
	if (strcmp(txt, "TRUE") == 0) return true;
	if (strcmp(txt, "0") == 0) return false;
	if (strcmp(txt, "no") == 0) return false;
	if (strcmp(txt, "No") == 0) return false;
	if (strcmp(txt, "NO") == 0) return false;
	if (strcmp(txt, "false") == 0) return false;
	if (strcmp(txt, "False") == 0) return false;
	if (strcmp(txt, "FALSE") == 0) return false;
	throw VALERR(bool, txt);
}

int64_t ParseInt(const char *txt) {
	return std::stoll(txt);
}

Description::Display Description::DisplayValue(const char *txt) {
	if (strcmp(txt, "StartDescriptionWithThis") == 0)
		return Description::Display::BeginHere;
	if (strcmp(txt, "StartAfterDefaultDescription") == 0)
		return Description::Display::AfterDefault;
	if (strcmp(txt, "AppendToPreviousDescription") == 0)
		return Description::Display::Append;
	throw VALERR(Description::Display, txt);
}

Property::ValueType Property::ParseValueType(const char *txt) {
	if (strcmp(txt, "SelectionOnly") == 0)
		return Property::ValueType::Bool;
	if (strcmp(txt, "Integer") == 0)
		return Property::ValueType::Int;
	if (strcmp(txt, "Text") == 0)
		return Property::ValueType::Text;
	if (strcmp(txt, "ObjectKey") == 0)
		return Property::ValueType::Object;
	if (strcmp(txt, "CharacterKey") == 0)
		return Property::ValueType::Object;
	if (strcmp(txt, "LocationKey") == 0)
		return Property::ValueType::Object;
	if (strcmp(txt, "LocationGroupKey") == 0)
		return Property::ValueType::Object;
	if (strcmp(txt, "StateList") == 0)
		return Property::ValueType::Enum;
	if (strcmp(txt, "ValueList") == 0)
		return Property::ValueType::Map;
	throw VALERR(Property::ValueType, txt);
}

Task::Type Task::ParseType(const char *txt) {
	if (strcmp(txt, "General") == 0)
		return Task::Type::General;
	if (strcmp(txt, "Specific") == 0)
		return Task::Type::Specific;
	if (strcmp(txt, "System") == 0)
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
	if (STREQ(txt, "On Object"))
		return { GameObj::HoldingType::OnObject, "OnWhat" };
	if (STREQ(txt, "Worn by Character"))
		return { GameObj::HoldingType::Worn, "WornByWho" };
	if (STREQ(txt, "Part of Character"))
		return { GameObj::HoldingType::PartOf, "PartOfWhat" };
	if (STREQ(txt, "Part of Object"))
		return { GameObj::HoldingType::PartOf, "PartOfWho" };
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