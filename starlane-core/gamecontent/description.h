#pragma once

#ifndef SLC_DESCRIPTION_H
#define SLC_DESCRIPTION_H

#include "../slc_private.h"
#include "../expression.h"

#include <map>
#include <memory>
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
	// Build a string from this description without recording that any of it was shown -- for text
	// wanted only for comparison, or for a name being rendered rather than displayed. Const, and
	// deliberately so: it sits on the hottest read paths in the engine (every object name shown to
	// the player goes through it), and those must not have to treat a description as mutable.
	// `context` is only ever set to anything when this description is the output of a user-defined
	// function, in which case it will hold the function's arguments.
	// `rawExpressions` leaves embedded expressions as their unevaluated source text (only
	// %reference%s and the like symbolic) while still selecting segments by their restrictions --
	// used to build a stable aggregation key that collapses across differing reference bindings.
	// See BuildRawKey.
	[[nodiscard]] std::string Build(const UserFuncContext *context = nullptr,
	                                bool rawExpressions = false) const;
	// The same, for text that is actually being displayed: this records which segments were shown,
	// which is game state (a "display once" segment does not come round again) and so is undone
	// and saved along with everything else.
	[[nodiscard]] std::string BuildAndCommit(const UserFuncContext *context = nullptr);

	// Build the message with restrictions evaluated (segment selection) but embedded expressions left
	// as raw source, without committing shown-state. This is the per-command deduplication key for a
	// task whose "Aggregate output" flag is on: two runs that differ only in their bound references
	// (e.g. "take the ball" vs "take the box") produce the same key and so collapse into one response.
	[[nodiscard]] std::string BuildRawKey() const { return Build(nullptr, true); }

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
	// The one implementation behind Build and BuildAndCommit.
	std::string BuildImpl(bool commit, const UserFuncContext *context, bool rawExpressions);

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
		// ADRIFT decides whether to put a space between one description part and the next by
		// looking at the *unevaluated* text so far -- which, once it contains an expression or an
		// "obj.Prop" chain, essentially always says yes. We evaluate as we resolve, so the answer
		// has to be worked out at load time and remembered. See Description::Build.
		bool rawEndsWithFunc = false;
		bool rawHasPropChain = false;

		// Borrowed from the owning Description for the duration of resolution; null otherwise.
		const std::vector<std::string> *udfArgNames = nullptr;

		std::string Build(const UserFuncContext *context = nullptr, bool rawExpressions = false) const;

		void ResolveText();
	private:
		[[nodiscard]] bool IsUserFuncArgName(const std::string &name) const;
		void ResolveText(std::string_view);
		void ResolveExpressions(std::string_view);
		void ResolveOO(std::string_view);
	};

	Description() = default;

	// Whether segment `idx` has been shown, and setting it. `shown` stays empty until something
	// is actually shown, which for most descriptions is never: every Description in the game is
	// copied into the undo snapshot once per turn, so a description nobody has read costs nothing
	// to copy.
	bool IsShown(size_t idx) const { return idx < shown.size() && shown[idx]; }
	void SetShown(size_t idx, bool value);

	// The segments are fixed once the game has loaded -- only which of them have been shown ever
	// changes -- so snapshots share one list rather than each deep-copying it. Built (and resolved)
	// during load, before the first copy of the Game exists.
	std::shared_ptr<std::vector<Segment>> segments = std::make_shared<std::vector<Segment>>();
	// Parallel to *segments where non-empty; see IsShown/SetShown.
	std::vector<bool> shown;
	std::vector<std::string> udfArgNames;
};

}

#endif  // !SLC_DESCRIPTION_H