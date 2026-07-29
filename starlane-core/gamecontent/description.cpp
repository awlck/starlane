#include "description.h"

#include <cstring>
#include <regex>

#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"
#include "restriction.h"
#include "utility.h"

// plain text is positive, expressions are negative:
#define IS_EXPR(x) ((x) < 0)

// "no position": basically just size_t_max
#define NPOS ((size_t) -1)

// A description segment is eligible to be displayed if it passes restrictions
// and has either not been displayed yet, or is allowed to be displayed multiple times.
#define SEGMENT_ELIGIBLE(s, i) ((!(s).onceOnly || !IsShown(i)) && RESTRICTION_PASSES((s).restrictionId))

namespace Starlane {

Description *Description::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Description;
	for (const auto &it : xmlNode.children("Description"))
		result->segments->emplace_back(Segment::CreateFromXML(it));
	return result;
}

Description *Description::CreateFromText(const std::string &text) {
	auto result = new Description;
	Segment s;
	s.text = text;
	s.displayWhen = Display::BeginHere;
	s.onceOnly = false;
	s.returnToDefault = false;
	result->segments->push_back(std::move(s));
	return result;
}

static bool NeedSpace(std::string_view textSoFar) {
	if (textSoFar.empty()) return false;
	size_t lastChar = textSoFar.length() - 1;
	switch (textSoFar[lastChar]) {
	case '.':
	case '?':
	case '!':
		// add a space at the end of a sentence.
		return true;
	default:
		return false;
	}
}

void Description::SetShown(size_t idx, bool value) {
	if (idx >= shown.size()) {
		if (!value) return;  // "not shown" is what an absent entry already means
		shown.resize(segments->size(), false);
	}
	shown[idx] = value;
}

void Description::HandleSegmentShown(size_t idx) {
	SetShown(idx, true);
	if (!segments->at(idx).returnToDefault) return;
	// if the current segment is marked "return to default", return all segments to
	// the left of it to non-shown status.
	for (size_t i = 0; i < idx; i++)
		SetShown(i, false);
}

std::string Description::Build(const UserFuncContext *context, bool rawExpressions) const {
	// BuildImpl only ever touches this object when `commit` is set, and it is not set here, so
	// there is nothing for the cast to make unsafe. The two entry points share one implementation
	// because committing is interleaved with rendering rather than following it: marking a segment
	// shown can clear the flags of segments to its left (see HandleSegmentShown), which changes
	// which of the later ones are still eligible.
	return const_cast<Description *>(this)->BuildImpl(false, context, rawExpressions);
}

std::string Description::BuildAndCommit(const UserFuncContext *context, Description *appended) {
	// A commit reached from inside a measuring build is not a real one: the text is being weighed,
	// not shown, and marking a "display once" part as displayed here would cheat the player out of
	// it. (This happens whenever a message names an object -- "%object%.Description" evaluates the
	// object's own description, which asks to commit -- and the measuring build above it has no way
	// to pass its intent down through the expression evaluator.)
	return BuildImpl(!Game::Get()->IsMeasuringOutput(), context, /*rawExpressions =*/ false, appended);
}

std::string Description::BuildImpl(bool commit, const UserFuncContext *context, bool rawExpressions,
                                   Description *appended) {
	// One left-to-right pass over the segments, exactly as ADRIFT's Description.ToString does it.
	// Each eligible segment either replaces what has been built so far (BeginHere), replaces it
	// with the default description plus itself (AfterDefault), or is appended to it (Append) -- so
	// the last eligible BeginHere naturally wins, without needing to be sought out in advance.
	// What does need the single ordered pass is DisplayOnce: a segment marked that way ends the
	// description there and then, so an unshown one earlier in the list beats every later segment,
	// however unrestricted those are. ("You find an access card." on the first search, "nothing
	// left" on every one after.)
	//
	// Unlike ADRIFT this evaluates each segment's expressions as it goes rather than at print time,
	// so a segment that turns out to be discarded has still been evaluated. That is only a concern
	// for expressions with side effects, of which there are none.
	//
	// The ADRIFT code also handles the case when a segment's restrictions fail but that
	// restriction failure outputs some text. We won't handle that case here, since the
	// ADRIFT Developer doesn't actually allow you to enter restriction failure text
	// for restrictions on description segments.

	// do nothing if there is no text
	if (segments->empty()) return "";

	// `appended`, if given, continues this description's segment list -- so index `i` past our own
	// count names one of its segments, and its shown-state is recorded against it rather than us.
	const size_t ownCount = segments->size();
	const size_t total = ownCount + (appended ? appended->segments->size() : 0);
	auto segmentAt = [&](size_t i) -> const Segment & {
		return i < ownCount ? segments->at(i) : appended->segments->at(i - ownCount);
	};
	auto segmentShown = [&](size_t i) {
		return i < ownCount ? IsShown(i) : appended->IsShown(i - ownCount);
	};
	auto noteSegmentShown = [&](size_t i) {
		if (i < ownCount) {
			HandleSegmentShown(i);
			return;
		}
		appended->HandleSegmentShown(i - ownCount);
		// "Return to default" clears the shown flags of everything to the left, which for an
		// appended segment reaches back across the seam into our own list.
		if (appended->segments->at(i - ownCount).returnToDefault) {
			for (size_t j = 0; j < ownCount; j++)
				SetShown(j, false);
		}
	};

	// A commit=false pass is a throwaway measurement (see this function's declaration) -- so besides
	// not committing segment shown-state below, it must not let any %CharacterName%/character.Name
	// evaluated along the way commit its mention either, or the real, printed Build() that follows
	// would wrongly see the character as already named this turn and print a pronoun instead of
	// their name.
	Game::MeasuringOutputGuard measuringGuard(Game::Get(), !commit);

	std::string result;
	// Whether anything shown so far would make ADRIFT's AddSpace say yes on grounds the finished
	// text no longer shows -- see Description::Segment.
	bool rawWantsSpace = false;
	auto wantsSpace = [](const Segment &s) { return s.rawEndsWithFunc || s.rawHasPropChain; };
	auto joinSpace = [&](const Segment &s, bool mark) {
		if (NeedSpace(result) || (rawWantsSpace && !result.empty()
				&& result.back() != ' ' && result.back() != '\n'))
			result += ' ';
		// ADRIFT marks the seam before an *appended* part with an empty tag. The frontend drops it,
		// but while it is there it keeps auto-capitalisation from treating the appended part as the
		// start of a new sentence -- "...to the south.  an airlock." stays lowercase. A part that
		// continues the default description gets no such marker, and does get capitalised.
		if (mark) result += "<>";
		rawWantsSpace = rawWantsSpace || wantsSpace(s);
	};

	for (size_t i = 0; i < total; i++) {
		const auto &s = segmentAt(i);
		// A DisplayOnce segment already shown is passed over entirely -- it neither contributes
		// text nor ends the pass.
		if (s.onceOnly && segmentShown(i)) continue;
		bool passes = RESTRICTION_PASSES(s.restrictionId);
		if (!passes) {
			// A DisplayOnce segment ends the description whether or not it got to speak: ADRIFT
			// returns from inside its DisplayOnce branch without consulting the restriction result.
			if (s.onceOnly) break;
			continue;
		}

		switch (s.displayWhen) {
		case Display::Append:
			joinSpace(s, true);
			result.append(s.Build(context, rawExpressions));
			break;
		case Display::AfterDefault:
			// "Start after the default description": the text so far is discarded in favour of
			// segment 0 followed by this one. (Segment 0 being itself an AfterDefault segment is
			// possible and means simply "start here".)
			if (i == 0) {
				result = s.Build(context, rawExpressions);
				rawWantsSpace = wantsSpace(s);
			} else {
				const auto &def = segmentAt(0);
				result = def.Build(context, rawExpressions);
				rawWantsSpace = wantsSpace(def);
				joinSpace(s, false);
				result.append(s.Build(context, rawExpressions));
			}
			break;
		case Display::BeginHere:
			result = s.Build(context, rawExpressions);
			rawWantsSpace = wantsSpace(s);
			break;
		}

		// Only a DisplayOnce segment records having been shown -- that flag is what stops it coming
		// round again, and nothing else consults it. (ReturnToDefault, which clears the flags to the
		// left, likewise only applies to one, matching where ADRIFT handles it.)
		if (commit && s.onceOnly) { noteSegmentShown(i); }
		// A DisplayOnce segment that did display ends the description here: nothing further down
		// the list gets a say this time round.
		if (s.onceOnly) break;
	}

	return result;
}

void Description::ResolveText() {
	for (auto &sd : *segments) {
		sd.udfArgNames = udfArgNames.empty() ? nullptr : &udfArgNames;
		sd.ResolveText();
		sd.udfArgNames = nullptr;
	}
}

bool Description::Segment::IsUserFuncArgName(const std::string &name) const {
	if (!udfArgNames) return false;
	return std::any_of(udfArgNames->cbegin(), udfArgNames->cend(), [&](const std::string &a) {
		return Util::ToLower(a) == Util::ToLower(name);
	});
}

Description::Segment Description::Segment::CreateFromXML(const pugi::xml_node &xmlNode) {
	Description::Segment result;
	result.text = xmlNode.child_value("Text");
	result.displayWhen = Description::DisplayValue(xmlNode.child_value("DisplayWhen"));
	auto once = xmlNode.child("DisplayOnce");
	result.onceOnly = once.type() == pugi::node_null ? false : ParseBool(once.child_value());
	auto retDefault = xmlNode.child("ReturnToDefault");
	result.returnToDefault = retDefault.type() == pugi::node_null ? false : ParseBool(retDefault.child_value());
	auto restr = xmlNode.child("Restrictions");
	if (restr.type() != pugi::node_null) {
		result.restrictionId = Game::Get()->CreateRestrictionsFromXML(restr);
	}
	return result;
}

std::string Description::Segment::Build(const UserFuncContext *context, bool rawExpressions) const {
	// make a single string out of our contents again, consisting of the plain text snippets
	// and expression evaluation results.

	// return our plain text representation if for some reason we haven't been resolved yet
	if (!text.empty()) return text;
	// otherwise build as explained
	std::string result;
	result.reserve(initialTextLength);
	for (auto ref : content) {
		if (IS_EXPR(ref)) {  // an expression
			// For an aggregation key we want the *unevaluated* source, so that two runs differing only
			// in their reference bindings hash identically. Everything else evaluates as normal.
			if (rawExpressions)
				result.append(Game::Get()->GetExpression(ref)->exprStr);
			else
				result.append(Game::Get()->GetExpression(ref)->EvaluateStr(context));
		} else {  // a plain text snippet
			// We also need to deal with alternatives like '[am/are/is]' at this stage.
			// Note that this is liable to break when the alternatives contain an expression,
			// but it will work for now...
			const char *str = Game::Get()->GetPlainTextSnippet(ref);
			// Only a snippet that actually has a '[' in it can carry an alternative, and the vast
			// majority don't -- so check for one before handing the text to the regex engine.
			// (Building this pattern per snippet, as this used to, was one of the more expensive
			// things a turn did.)
			if (std::strchr(str, '[') == nullptr) {
				result.append(str);
				continue;
			}
			static const std::regex matchEx(R"(\[(.*?)\/(.*?)\/(.*?)\])");
			const char *replacement;
			switch (Game::Get()->GetCurrentReferralPerson()) {
			case ReferralPerson::FirstPerson:
				replacement = "$1";
				break;
			case ReferralPerson::SecondPerson:
				replacement = "$2";
				break;
			case ReferralPerson::ThirdPerson:
				replacement = "$3";
				break;
			default:
				UNREACHABLE();
			}
			result.append(std::regex_replace(str, matchEx, replacement));
		}
	}
	return result;
}

static size_t SkipSingleOOExpression(std::string_view theText) {
	// this function assumes that `theText` points to the beginnig of an OO expression like:
	// Obj1.Held.Weight.Sum
	// ^
	// c points here
	// 
	// ... or somewhere in the middle:
	// %player%.Held.Weight.Sum
	//         ^
	//         c points here

	// go to the first space:
	size_t pos;
	bool openParens = false;
	for (pos = 0; pos < theText.length(); pos++) {
		char x = theText[pos];
		// Sentence and quote punctuation ends the property chain: property names are identifiers, so
		// none of these can be part of one, and they are just text following the expression. Without
		// them, "%object%.Name: ..." / "%object%.name."..." swallow the trailing punctuation into the
		// expression, which is then handed to the parser and fails (Grandma's Flying Saucer).
		if (x == '!' || x == '?' || x == '/' || x == '<' || x == '\n'
				|| x == ':' || x == ';' || x == '"' || x == '\'') break;
		if (x == '(') openParens = true;
		if ((x == ',' || x == ' ') && !openParens) break;
		if (x == ')') {
			if (openParens) openParens = false;
			else break;
		}
		// because certain people hate spaces, a fix for 'foo...bar'
		if (x == '.' && pos > 0 && theText[pos - 1] == '.') break;
	}
	// but make sure not to include any periods that are simply part of the text
	while (pos > 0 && theText[pos-1] == '.') --pos;
	// return the number of characters to skip so the calling functions can continue
	// with its business
	return pos;
}

void Description::Segment::ResolveText() {
	// Mirrors ADRIFT's AddSpace, which runs over the raw description text (see description.h).
	// ADRIFT uses a regex for this; a scan for "<identifier>.<identifier>" accepts the same texts
	// without the pathological backtracking that regex shows on long prose.
	auto isIdent = [](unsigned char c) { return isalnum(c) || c == '_' || c == '-' || c == '%' || c == '|'; };
	rawEndsWithFunc = !text.empty() && text.back() == '%';
	rawHasPropChain = false;
	for (size_t i = 1; i + 1 < text.size(); i++) {
		if (text[i] != '.') continue;
		if (!isIdent((unsigned char) text[i - 1])) continue;
		const unsigned char next = (unsigned char) text[i + 1];
		if (isalpha(next) || next == '%') {
			rawHasPropChain = true;
			break;
		}
	}
	ResolveText(text);
	initialTextLength = text.length();
	// `content` now holds references to everything we need to display,
	// so we can delete our copy of the original text.
	text.clear();
	// (string.clear doesn't actually release memory, so...)
	std::string().swap(text);
}

void Description::Segment::ResolveText(std::string_view theText) {
ResolveText_FakeTailcall:
	// deal with %functions% first
	int bracketDepth = 0;
	int percents = 0;
	size_t pos;
	bool functionFound = false;
	bool nonstopmode = false;
	bool anybrackets = false;
	size_t beginningOfFunc = std::string_view::npos;
	for (pos = 0; pos < theText.length(); pos++) {
		switch (theText[pos]) {
		case '[':
			if (nonstopmode) continue;
			++bracketDepth;
			if (functionFound) anybrackets = true;
			continue;
		case ']':
			if (nonstopmode) continue;
			--bracketDepth;
			continue;
		case '<':
			if (pos + 1 < theText.length() && theText[pos + 1] == '#') {
				// found an embedded expression, will not handle contained %function%s.
				nonstopmode = true;
			}
			continue;
		case '>':
			if (nonstopmode && theText[pos - 1] == '#') {
				// end of an embedded expression
				nonstopmode = false;
			}
			continue;
		case '%':
			if (nonstopmode) continue;
			if (!functionFound && bracketDepth == 0) {
				// (A certain someone decided to write '[%]: %academic%', and I hate them for it.)
				functionFound = true;
				beginningOfFunc = pos;
			}
			if (functionFound) percents += 1;
			break;  // and continue below.
		case '\n':
			if (functionFound) {  // if we find a line break while scanning a %function%, give up and treat as text instead
				functionFound = false;
				beginningOfFunc = std::string_view::npos;
				percents = 0;
			}
		}
		if (percents % 2 == 0 && bracketDepth == 0 && functionFound) {  // at end of this function call
			if (!anybrackets) {
				auto vname = std::string(theText.substr(beginningOfFunc + 1, pos - beginningOfFunc - 1));
				// When there aren't any brackets, the name between the percentage signs must be a
				// known variable or built-in function.
				// Deliberately a hand-written list rather than a lookup in
				// tableOfBuiltInFunctions: that table also holds every function that takes
				// arguments, and naming one of those here would promote "%Left%" from harmless
				// gibberish that prints as-is into a zero-argument call that throws.
				if (!Game::Get()->VarOfNameExists(vname) && vname != "AloneWithChar" && vname != "ConvCharacter" && vname != "Player" && vname != "CharacterName"
						&& vname != "Turns" && vname != "turns" && vname != "TURNS"
						&& !Util::IsCommandRefName('%' + vname + '%') && !IsUserFuncArgName(vname)
					&& !Game::Get()->GetUserFuncByName(vname)) {
					// no known name: just some gibberish and not a function/variable after all
					ResolveExpressions(theText.substr(0, pos+1));
					//return ResolveText(theText.substr(pos+1));
					theText = theText.substr(pos + 1);
					goto ResolveText_FakeTailcall;
				}
			}
			// handle all the text prior to the first '%'
			ResolveExpressions(theText.substr(0, beginningOfFunc));
			// check whether we also need to resolve an OO-style property for the resulting value
			// (property names start with a letter; anything else -- like a tag in '%func%.</font>' --
			// means the period is just ordinary end-of-sentence text)
			if (pos + 2 < theText.length() && theText[pos + 1] == '.' && isalpha((unsigned char) theText[pos + 2])) {
				pos += SkipSingleOOExpression(theText.substr(pos));
			} else {
				pos += 1;
			}
			// no, beginningOfFunc cannot be null; it is always set when functionFound is true.
			content.push_back(Game::Get()->CreateExpression(std::string(theText.substr(beginningOfFunc, pos - beginningOfFunc))));
			// deal with the remainder of the text "recursively":
			//return ResolveText(theText.substr(pos));
			theText = theText.substr(pos);
			goto ResolveText_FakeTailcall;
		}
	}
	// If we got here then we didn't find any (complete) function invocations, so continue
	// processing this text as a whole.
	return ResolveExpressions(theText);
}

void Description::Segment::ResolveExpressions(std::string_view theText) {
ResolveExpressions_FakeTailcall:
	// deal with <# expressions #> second
	size_t exprBegin;
	if ((exprBegin = theText.find("<#")) != std::string_view::npos) {
		// deal with the plain text in the beginning
		ResolveOO(theText.substr(0, exprBegin));
		exprBegin += 2;
		size_t exprEnd = theText.find("#>", exprBegin);
		content.push_back(Game::Get()->CreateExpression(std::string(theText.substr(exprBegin, exprEnd - exprBegin))));
		//return ResolveExpressions(theText.substr(exprEnd + 2));
		theText = theText.substr(exprEnd + 2);
		goto ResolveExpressions_FakeTailcall;
	}
	// no <# expressions #> found
	return ResolveOO(theText);
}

static std::string_view GetPotentialObjKey(std::string_view word) {
	size_t pos;
	for (pos = 0; pos < word.length() && word[pos] != '.'; pos++);
	return word.substr(0, pos);
}

void Description::Segment::ResolveOO(std::string_view theText) {
ResolveOO_FakeTailcall:
	// A word is an OO expression if it contains a period and the first segment
	// is a known object key.

	size_t pos;
	// whether we are in a word (i.e. the most recently seen character wasn't a space)
	bool inWord = false;
	// this sounds very wrong but it just expresses whether we have recently seen a '.'
	// within a word.
	bool havePeriod = false;
	// Where the current word begun (that is, the position of the character after the most recent space)
	size_t wordBegan = 0;
	// Whether we have also read some more letters since reading the last period
	// (To catch occasions where the last word in the sentence happens to be an object key.)
	int reallyInExpr = 0;
	// the current level of parentheses (only relevant within a potential expression)
	int parensLevel = 0;
	for (pos = 0; pos < theText.length(); pos++) {
		// Sentence and quote punctuation ends a property chain the same way whitespace does --
		// see SkipSingleOOExpression's identical list, added for the same reason (Grandma's Flying
		// Saucer): without it, "Bob.ProperName!" swallows the "!" into the expression text handed
		// to the parser, which then fails on it.
		if (theText[pos] == ' ' || theText[pos] == '\n' || (theText[pos] == ',' && parensLevel == 0) || theText[pos] == ';'
				|| theText[pos] == '!' || theText[pos] == '?' || theText[pos] == '/' || theText[pos] == '<'
				|| theText[pos] == ':' || theText[pos] == '"' || theText[pos] == '\'') {
			inWord = false;
			if (reallyInExpr == 1) {
				auto theKey = GetPotentialObjKey(theText.substr(wordBegan, pos - wordBegan));
				if (Game::Get()->ObjectExists(std::string(theKey))) {  // this is indeed a (usable) OO expression
					// save text prior to this word, if any
					if (wordBegan > 0)
						content.push_back(Game::Get()->StorePlainTextSnippet(theText.substr(0, wordBegan)));
					// walk back any end-of-sentence periods
					while (pos > 0 && theText[pos - 1] == '.') --pos;
					// save expression
					content.push_back(Game::Get()->CreateExpression(std::string(theText.substr(wordBegan, pos - wordBegan))));
					// continue processing recursively
					//return ResolveOO(theText.substr(pos));
					theText = theText.substr(pos);
					goto ResolveOO_FakeTailcall;
				}
			}
			// otherwise do nothing -- this was just a missing space after a period after all.
			havePeriod = false;
			reallyInExpr = 0;
			continue;
		}
		if (havePeriod && theText[pos] == '.' && theText[pos - 1] == '.') {
			// guard against 'someKey...nextsentence', because some people apparently hate spaces
			reallyInExpr = -1;
			continue;
		}
		if (havePeriod && theText[pos] == '"') {
			// similarly, guard against 'I said, "Foo."'
			reallyInExpr = -1;
			continue;
		}
		if (inWord && theText[pos] == '.') {
			havePeriod = true;
			continue;
		}
		if (havePeriod && theText[pos] != '.' && reallyInExpr == 0) {
			reallyInExpr = 1;
			continue;
		}
		if (reallyInExpr && theText[pos] == '(') {
			parensLevel++;
			continue;
		}
		if (reallyInExpr && theText[pos] == ')') {
			parensLevel--;
			continue;
		}
		if (!inWord && (isalnum(theText[pos]) || theText[pos] == '_')) {
			wordBegan = pos;
			inWord = true;
		}
	}
	// no OO expressions in this piece of text -- store it in its entirety.
	content.push_back(Game::Get()->StorePlainTextSnippet(theText));
}

std::vector<bool> Description::GetState() const {
	// Really, the only thing we're interested in is whether each of our segments has been shown.
	std::vector<bool> results;
	results.reserve(segments->size());
	for (size_t i = 0; i < segments->size(); i++)
		results.push_back(IsShown(i));
	return results;
}

void Description::RestoreState() {
	shown.clear();
}

void Description::RestoreState(const std::vector<bool> &state) {
	size_t count = state.size();
	for (size_t i = 0; i < count; i++)
		SetShown(i, state[i]);
}

}