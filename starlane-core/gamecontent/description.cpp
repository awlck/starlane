#include "description.h"

#include <limits>
#include <sstream>

#include <pugixml.hpp>

#include "../game.h"
#include "../expression.h"
#include "../valueparsers.h"
#include "restriction.h"

// bit fuckery to tell plain text from expressions
#define TOPBIT (((size_t) 1) << (std::numeric_limits<size_t>::digits - 1))

// "no position": basically just size_t_max
#define NPOS ((size_t) -1)

// A description segment is eligible to be displayed if it passes restrictions
// and has either not been displayed yet, or is allowed to be displayed multiple times.
#define SEGMENT_ELIGIBLE(s) ((!(s).onceOnly || !(s).shown) && RESTRICTION_PASSES((s).restrictionId))

namespace Starlane {

Description *Description::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Description;
	for (const auto &it : xmlNode.children("Description"))
		result->segments.emplace_back(Segment::CreateFromXML(it));
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

void Description::HandleSegmentShown(size_t idx) {
	auto &s = segments.at(idx);
	s.shown = true;
	if (!s.returnToDefault) return;
	// if the current segment is marked "return to default", return all segments to
	// the left of it to non-shown status.
	for (size_t i = 0; i < idx; i++) {
		segments.at(i).shown = false;
	}
}

std::string Description::Build(bool commit) {
	// The ADRIFT Runner's way of handling descriptions goes something like this:
	// "Build the text. If some segment wants to be first and passes restrictions,
	//  throw the already-built text away and start over."
	// This mode of action is probably fine for the ADRIFT Runner since it doesn't
	// evaluate expressions in the text until it is actually sent to the screen, but
	// I think we should take a bit more care since we will evaluate expressions right now.
	// The ADRIFT code also handles the case when a segment's restrictions fail but that
	// restriction failure outputs some text. We won't handle that case here, since the
	// ADRIFT Developer doesn't actually allow you to enter restriction failure text
	// for restrictions on description segments.

	// do nothing íf there is no text
	if (segments.size() == 0) return "";

	// First, find the rightmost segment with "BeginHere" mode that passes restrictions
	size_t beginning = NPOS;
	for (size_t i = segments.size() - 1; i != NPOS; i--) {
		const auto &s = segments.at(i);
		// note that this is guaranteed to exist: the very first segment always fulfills
		// these conditions by definition, the ADRIFT Developer will not allow you to
		// set restrictions on it or change its display mode.
		if (s.displayWhen == Display::BeginHere && SEGMENT_ELIGIBLE(s)) {
			beginning = i;
			break;
		}
	}

	// Second, find the rightmost segment with "AfterDefault" mode that passes restrictions,
	// but no further to the left than the value of 'beginning' we just determined.
	// Confusingly, if this exists we need to set 'beginning' back to 0
	// (meaning we will go back to showing the 'default' description).
	size_t continuation = NPOS;
	for (size_t i = segments.size() - 1; i > beginning; i--) {
		const auto &s = segments.at(i);
		if (s.displayWhen == Display::AfterDefault && SEGMENT_ELIGIBLE(s)) {
			continuation = i;
			beginning = 0;
			break;
		}
	}

	std::string result(segments.at(beginning).Build());
	if (commit) HandleSegmentShown(beginning);
	size_t nextSegment = beginning + 1;
	if (continuation != NPOS) {
		if (NeedSpace(result)) result += ' ';
		result.append(segments.at(continuation).Build());
		if (commit) HandleSegmentShown(continuation);
		nextSegment = continuation + 1;
	}
	for (size_t i = nextSegment; i < segments.size(); i++) {
		const auto &s = segments.at(i);
		if (s.displayWhen == Display::Append && SEGMENT_ELIGIBLE(s)) {
			if (NeedSpace(result)) result += ' ';
			result.append(s.Build());
			if (commit) HandleSegmentShown(i);
		}
	}

	return result;
}

void Description::ResolveText() {
	for (auto &sd : segments) {
		sd.ResolveText();
	}
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

std::string Description::Segment::Build() const {
	// make a single string out of our contents again, consisting of the plain text snippets
	// and expression evaluation results.

	// return our plain text representation if for some reason we haven't been resolved yet
	if (!text.empty()) return text;
	// otherwise build as explained
	std::string result;
	result.reserve(initialTextLength);
	for (auto ref : content) {
		if (ref & TOPBIT) {  // an expression
			result.append(Game::Get()->GetExpression(ref)->EvaluateStr());
		} else {  // a plain text snippet
			result.append(Game::Get()->GetPlainTextSnippet(ref));
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
		if (x == '!' || x == '?' || x == '/' || x == '\n') break;
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
				auto vname = std::string(theText.substr(beginningOfFunc + 1, pos));
				// When there aren't any brackets, the name between the percentage signs must be a
				// known variable or built-in function.
				if (!Game::Get()->VarOfNameExists(vname) && vname != "AloneWithChar" && vname != "ConvCharacter" && vname != "Player" && vname != "CharacterName") {
					// no known name: just some gibberish and not a function/variable after all
					ResolveExpressions(theText.substr(0, pos+1));
					//return ResolveText(theText.substr(pos+1));
					theText = theText.substr(pos + 1);
					goto ResolveText_FakeTailcall;
				}
			}
			// handle all the text prior to the first '%'
			ResolveExpressions(theText.substr(0, beginningOfFunc));
			// check wheter we also need to resolve an OO-style property for the resulting value
			if (pos + 2 < theText.length() && theText[pos + 1] == '.' && theText[pos + 2]) {
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
	for (pos = 0; pos < theText.length(); pos++) {
		if (theText[pos] == ' ' || theText[pos] == '\n') {
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
		if (inWord && theText[pos] == '.') {
			havePeriod = true;
			continue;
		}
		if (havePeriod && theText[pos] != '.' && reallyInExpr == 0) {
			reallyInExpr = 1;
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

}