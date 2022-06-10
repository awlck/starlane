#include "restriction.h"

namespace Starlane {

Restriction *Restriction::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Restriction;
	return result;
}

}