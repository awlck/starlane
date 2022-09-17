#include "description.h"

#include <limits>
#include <sstream>

#include <pugixml.hpp>

#include "../game.h"
#include "../expressions.h"
#include "../valueparsers.h"
#include "restriction.h"

// magical bit fuckery
#define TOPBIT (((size_t) 1) << (std::numeric_limits<size_t>::digits - 1))

namespace Starlane {

Description *Description::CreateFromXML(const pugi::xml_node &xmlNode) {
    auto result = new Description;
	for (const auto &it: xmlNode.children("Description"))
		result->segments.emplace_back(Segment::CreateFromXML(it));
	return result;
}

std::string Description::Build(bool commit) {
	// TODO: Handle alternatives, substitutions, etc.
	std::string result;
	for (auto &s : segments) {
		auto pass = Game::Get()->GetRestriction(s.restrictionId)->PassRestrictionBlock();
		if (pass.first && (!s.onceOnly || !s.shown)) {
			if (commit) s.shown = true;
			result.append(s.Build());
		}
	}
	return result;
}

void Description::ResolveText() {
	for (auto &sd: segments) {
		sd.ResolveText();
	}
}

Description::Segment Description::Segment::CreateFromXML(const pugi::xml_node &xmlNode) {
	Description::Segment result;
	result.text = xmlNode.child_value("Text");
	result.displayWhen = Description::DisplayValue(xmlNode.child_value("DisplayWhen"));
	auto once = xmlNode.child("DisplayOnce");
	result.onceOnly = once.type() == pugi::node_null ? false : ParseBool(xmlNode.child_value("DisplayOnce"));
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
	for (pos = 0; pos < theText.length() && theText[pos] != ' '; pos++);
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
	size_t beginningOfFunc = std::string_view::npos;
	for (pos = 0; pos < theText.length(); pos++) {
		switch (theText[pos]) {
		case '[':
			if (nonstopmode) continue;
			++bracketDepth;
			continue;
		case ']':
			if (nonstopmode) continue;
			--bracketDepth;
			continue;
		case '<':
			if (pos+1 < theText.length() && theText[pos+1] == '#') {
				// found an embedded expression, will not handle contained %function%s.
				nonstopmode = true;
			}
			continue;
		case '>':
			if (nonstopmode && theText[pos-1] == '#') {
				// end of an embedded expression
				nonstopmode = false;
			}
			continue;
		case '%':
			if (nonstopmode) continue;
			if (!functionFound) {
				functionFound = true;
				beginningOfFunc = pos;
			}
			percents += 1;
			break;  // and continue below.
		}
		if (percents % 2 == 0 && bracketDepth == 0 && functionFound) {  // at end of this function call
			// handle all the text prior to the first '%'
			//ResolveExpressions(std::string(theText, beginningOfFunc - theText).c_str());
			ResolveExpressions(theText.substr(0, beginningOfFunc));
			// check wheter we also need to resolve an OO-style property for the resulting value
			if (pos+1 < theText.length() && theText[pos+1] == '.') {
				pos += SkipSingleOOExpression(theText.substr(pos));
			} else {
				pos += 1;
			}
			// no, beginningOfFunc cannot be null; it is always set when functionFound is true.
			//content.push_back(Game::Get()->CreateExpression(std::string(beginningOfFunc, beginningOfFunc - c)));
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
	for (pos = 0; pos < theText.length(); pos++) {
		if (theText[pos] == ' ') {
			inWord = false;
			if (havePeriod) {
				auto theKey = GetPotentialObjKey(theText.substr(wordBegan, pos - wordBegan));
				if (Game::Get()->ObjectExists(std::string(theKey))) {  // this is indeed a (usable) OO expression
					// save text prior to this word
					content.push_back(Game::Get()->StorePlainTextSnippet(theText.substr(0, pos)));
					// save expression
					content.push_back(Game::Get()->CreateExpression(std::string(theText.substr(wordBegan, pos - wordBegan))));
					// continue processing recursively
					//return ResolveOO(theText.substr(pos));
					theText = theText.substr(pos);
					goto ResolveOO_FakeTailcall;
				}
				// otherwise do nothing -- this was just a missing space after a period after all.
				havePeriod = false;
			}
			continue;
		}
		if (inWord && theText[pos] == '.') {
			havePeriod = true;
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