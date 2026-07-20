#pragma once

#ifndef SLC_DESCRIPTION_H
#define SLC_DESCRIPTION_H

#include "../slc_private.h"
#include "../expression.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Starlane {

class Description {
public:
	// Create a description object using the given XML node
	static Description *CreateFromXML(const pugi::xml_node &xmlNode);
	// A description consisting of one piece of plain text, for text that lives outside the
	// description machinery but may still contain %function% calls -- an object's name, say.
	static Description *CreateFromText(const std::string &text);
	// Build a string from this description
	// `commit` should be true when displaying, false when building the text for comparison purposes.
	// `context` is only ever set to anything when this description is the output of a user-defined function,
	// in which case it will hold the function's arguments.
	[[nodiscard]] std::string Build(bool commit = true, const UserFuncContext *context = nullptr);

	void ResolveText();

	// Names this description may use as `%name%` besides the game's own variables and references:
	// the argument names of the user-defined function whose output this is. Set at load time,
	// before ResolveText runs, which is where the distinction between a call and plain text is made.
	void SetUserFuncArgNames(std::vector<std::string> names) { udfArgNames = std::move(names); }

	std::vector<bool> GetState() const;
	void RestoreState();
	void RestoreState(const std::vector<bool> &state);

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
		// (The easiest way I could come up with to tell PlainTextRefs and ExprRefs apart while still
		//  storing them in a single container is to make sure one is always positive and the other is
		//  always negative.)
		std::vector<ptrdiff_t> content;
		// remember how long the initial text (with expressions) was, as an estimate for how much
		// memory we need to reserve when building the text back.
		size_t initialTextLength = 0;
		bool onceOnly;
		bool returnToDefault;
		bool shown = false;
		// ADRIFT decides whether to put a space between one description part and the next by
		// looking at the *unevaluated* text so far -- which, once it contains an expression or an
		// "obj.Prop" chain, essentially always says yes. We evaluate as we resolve, so the answer
		// has to be worked out at load time and remembered. See Description::Build.
		bool rawEndsWithFunc = false;
		bool rawHasPropChain = false;

		// Borrowed from the owning Description for the duration of resolution; null otherwise.
		const std::vector<std::string> *udfArgNames = nullptr;

		std::string Build(const UserFuncContext *context = nullptr) const;

		void ResolveText();
	private:
		[[nodiscard]] bool IsUserFuncArgName(const std::string &name) const;
		void ResolveText(std::string_view);
		void ResolveExpressions(std::string_view);
		void ResolveOO(std::string_view);
	};

	Description() = default;

	std::vector<Segment> segments;
	std::vector<std::string> udfArgNames;
};

}

#endif  // !SLC_DESCRIPTION_H