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
	static Description *CreateFromXML(const pugi::xml_node &xmlNode);
	// Build a string from this description
	// `commit` should be true when displaying, false when building the text for comparison purposes.
	[[nodiscard]] std::string Build(bool commit = true);

private:
	enum class Display {
		BeginHere,
		AfterDefault,
		Append
	};
	static Display DisplayValue(const char *txt);

	struct Segment {
		static Segment CreateFromXML(const pugi::xml_node &xmlNode);
		Segment() = default;

		Display displayWhen;
		size_t restrictionId;
		std::string text;
		bool onceOnly;
		bool shown;
	};

	Description() = default;

	std::vector<Segment> segments;
};

}

#endif  // !SLC_DESCRIPTION_H