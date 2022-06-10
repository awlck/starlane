#include "propholder.h"

#include <stdexcept>

#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"
#include "property.h"

namespace Starlane {

void PropHolder::SetPropValueFromXML(const pugi::xml_node &xmlNode) {
	std::string propkey = xmlNode.child_value("Key");
	switch (Game::Get()->GetPropMeta(propkey)->Type()) {
	case Property::ValueType::Bool:
		SetPropValue(propkey, true);
		return;
	case Property::ValueType::Enum:
	case Property::ValueType::Object: {
		std::string val = xmlNode.child_value("Value");
		SetPropValue(propkey, val);
		return;
	}
	case Property::ValueType::Int:
	case Property::ValueType::Map:
		SetPropValue(propkey, ParseInt(xmlNode.child_value("Value")));
		return;
	case Property::ValueType::Text:
		SetPropValue(propkey, (int64_t) Game::Get()->CreateDescFromXML(xmlNode.child("Value").child("Description")));
		return;
    case Property::ValueType::ErrorType:
        throw std::runtime_error("Attempted to assign to error-type property.");
    }
}

void PropHolder::ErasePropValue(const std::string &key) {
    switch (Game::Get()->GetPropMeta(key)->Type()) {
    case Property::ValueType::Object:
    case Property::ValueType::Enum:
        if (strValuedProps.count(key))
            strValuedProps.erase(key);
        break;
    default:
        if (intValuedProps.count(key))
            intValuedProps.erase(key);
        break;
    }
}

}