#include "valueparsers.h"

#include <string>

#include "gamecontent/description.h"
#include "gamecontent/property.h"

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

}