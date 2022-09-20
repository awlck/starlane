#pragma once

#ifndef SLC_DESCRIPTION_H
#define SLC_DESCRIPTION_H

#include "../slc_private.h"

#include <string>
#include <string_view>
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

	void ResolveText();

private:
	enum class Display {
		BeginHere,
		AfterDefault,
		Append
	};
	static Display DisplayValue(const char *txt);

	void HandleSegmentShown(size_t idx);

	struct Segment {
		static Segment CreateFromXML(const pugi::xml_node &xmlNode);
		Segment() = default;

		Display displayWhen;
		size_t restrictionId = 0;
		// At load time, store the entire text here
		std::string text;
		// After resolving references, we get this instead:
		// (And because having a variant of PlainTextRef and ExprRef is hard, because they're
		// both size_t under the hood, we'll do some wild shit with bit-flags instead.)
		std::vector<size_t> content;
		// remember how long the initial text (with expressions) was, as an estimate for how much
		// memory we need to reserve when building the text back.
		size_t initialTextLength = 0;
		bool onceOnly;
		bool returnToDefault;
		bool shown;

		std::string Build() const;

		void ResolveText();
	private:
		void ResolveText(std::string_view);
		void ResolveExpressions(std::string_view);
		void ResolveOO(std::string_view);
	};

	Description() = default;

	std::vector<Segment> segments;
};

}

#endif  // !SLC_DESCRIPTION_H