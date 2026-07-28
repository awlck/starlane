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
		// The <Value> node holds the <Description> segments directly (there can be
		// several, with restrictions), so it is the node to build the description from.
		SetPropValue(propkey, (int64_t) Game::Get()->CreateDescFromXML(xmlNode.child("Value")));
		return;
    case Property::ValueType::ErrorType:
        throw std::runtime_error("Attempted to assign to error-type property.");
    }
}

void PropHolder::ErasePropValue(const std::string &key) {
    switch (Game::Get()->GetPropMeta(key)->Type()) {
    case Property::ValueType::Object:
    case Property::ValueType::Enum:
        // Checking before detaching: erasing something we don't have would otherwise pay for a
        // whole copy-on-write clone and change nothing.
        if (props->strValued.count(key))
            MutableProps().strValued.erase(key);
        break;
    default:
        if (props->intValued.count(key))
            MutableProps().intValued.erase(key);
        break;
    }
}

}