#pragma once

#ifndef SLC_DESCRIPTION_H
#define SLC_DESCRIPTION_H

#include "../slc_private.h"

#include <string>
#include <vector>

namespace pugi {
class xml_node;
}

namespace Starlane {

class Description {
public:
	// Create a description object using the given XML node
	static Description *CreateFromXML(Game *g, const pugi::xml_node &xmlNode);
	// Build a string from this description
	std::string Output(const Game *g) const;

private:
	enum class Display {
		BeginHere,
		AfterDefault,
		Append
	};
	static Display DisplayValue(const char *txt);

	class Segment {
	public:
		static Segment CreateFromXML(Game *g, const pugi::xml_node &xmlNode);
	private:
		Segment() = default;

		Display displayWhen;
		size_t restrictionId;
		std::string text;
		bool onceOnly;
	};

	Description() = default;

	std::vector<Segment> segments;
};

}

#endif  // !SLC_DESCRIPTION_H