#include "propholder.h"

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
		SetPropValue(propkey, Game::Get()->CreateDescFromXML(xmlNode.child("Value").child("Description")));
		return;
	}
}

}