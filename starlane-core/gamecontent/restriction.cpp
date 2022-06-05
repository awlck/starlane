#include "restriction.h"

namespace Starlane {

Restriction *Restriction::CreateFromXML(Game *g, const pugi::xml_node &xmlNode) {
	auto result = new Restriction;
	return result;
}

}