#include "restriction.h"

#include <sstream>
#include <stdexcept>

#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"

namespace Starlane {

namespace {

// Find the last occurrence of `c` in `str`, but no later than `maxidx`.
size_t LastIndexOf(const char *str, char c, size_t maxidx) {
	size_t result = (size_t) -1;
	for (size_t i = 0; i < maxidx && str[i]; i++) {
		if (str[i] == c) result = i;
	}
	return result;
}

// In ADRIFT5, like many languages, 'and' binds more strongly than 'or'.
// In order to simplify the interpreter logic below, we transform:
// #A#O# => (#A#)O#
// #A#A#O# => (#A#A#)O#
// (#O#)A#O# => ((#O#)A#)O#
// #A(#A#O#) => #A((#A#)O#)
// see: https://github.com/jcwild/ADRIFT-5/blob/a0042f1750e622eb75cbbf433ff8804f4ca11cc8/ADRIFT/clsUserSession.vb#L6736
void TransformRestrictionSequence(std::string &seq) {
	const char *c;
	while ((c = strstr(seq.c_str(), "A#O"))) {
		size_t i = c - seq.c_str();
		size_t j = LastIndexOf(seq.c_str(), '(', i);
		if (j == (size_t) -1) seq.insert(0, 1, '(');
		else seq.insert(j+1, 1, '(');
		// (i+2 in the original string, but we have inserted one character already.)
		seq.insert(i + 3, 1, ')');
	}
}

void SkipRemainderOfBlock(const std::string &seq, size_t &tidx, size_t &ridx, size_t bracketLevel) {
	size_t b = bracketLevel;
	while (tidx < seq.size() && b >= bracketLevel) {
		switch (seq[++tidx]) {
		case '(':
			b++;
			tidx++;
			break;
		case ')':
			tidx++;
			break;
		case '#':
			ridx++;
			tidx++;
			break;
		case 'A':
		case 'O':
			tidx++;
			break;
		}
	}
}

std::string NextToken(const char **const str) {
	// if the parameter is NULL
	if (!str) return "";
	// if the string pointed to is NULL
	if (!*str) return "";
	// if the first character of the string pointed to is '\0'
	if (!**str) return "";

	size_t i = 0;
	std::string result;
	while (**str && isspace(**str)) (*str)++;
	while ((*str)[i] && !isspace((*str)[i++])) ;
	result.append(*str, i-1);
	(*str) += i-1;
	return result;
}

constexpr bool ConditionHasRHS(Restriction::ConditionType t) {
	switch (t) {
	case Restriction::ConditionType::Alone:
	case Restriction::ConditionType::BeHidden:
	case Restriction::ConditionType::Exist:
	case Restriction::ConditionType::Complete:
		return false;
	default:
		return true;
	}
}

}  // anonymous namespace

Restriction *Restriction::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Restriction;
	for (const auto &it : xmlNode.children("Restriction")) {
		Single s;
		const auto &txt = it.first_child();
		s.targetType = ParseTargetType(txt.name());
		s.restrText = txt.child_value();
		const auto &msg = it.child("Message");
		if (msg.type() != pugi::node_null)
			s.failureMsg = Game::Get()->CreateDescFromXML(msg);
		s.Translate();
		result->restrs.emplace_back(std::move(s));
	}
	std::string sequence(xmlNode.child_value("BracketSequence"));
	TransformRestrictionSequence(sequence);
	result->sequence = sequence;
	return result;
}

std::pair<bool, DescrRef> Restriction::PassRestrictionBlock() const {
	size_t tidx = 0;
	size_t ridx = 0;
	return PassRestrictionBlock(tidx, ridx, 0);
}

std::pair<bool, DescrRef> Restriction::PassRestrictionBlock(size_t &tidx, size_t &ridx, size_t brackets) const {
	std::pair<bool, DescrRef> state;
	while (tidx < sequence.size()) {
		if (sequence[tidx] == '(') {
			state = PassRestrictionBlock(++tidx, ridx, brackets + 1);
			continue;
		} else if (sequence[tidx] == ')') {
			++tidx;
			return state;
		} else if ((sequence[tidx] == 'O' && state.first) || (sequence[tidx] == 'A' && !state.first)) {
			// `true OR any` and `false AND any` are always true or false, respectively. In those
			// cases, we are done with this block. Skip remainder, including any bracketed sub-blocks.
			// The reverse cases of `false OR any` and `true AND any` are not handled specially: they
			// simply continue through the loop.
			SkipRemainderOfBlock(sequence, tidx, ridx, brackets);
			return state;
		} else if (sequence[tidx] == '#') {
			// Check whether the particular restriction in the sequence passes and record the result.
			if (restrs[ridx].Pass()) {
				state = { true, 0 };
			} else {
				state = { false, restrs[ridx].failureMsg };
			}
			ridx++;
		} else if (sequence[tidx] != 'O' && sequence[tidx] != 'A')  {
			throw std::runtime_error("Unrecognized character in restriction sequence: " + sequence);
		}
		tidx++;
	}
	return state;
}

bool Restriction::Single::Pass() const {
	// TODO
	return true;
}

#define GET_TOKEN do { if ((tok = NextToken(&x)).empty()) throw std::runtime_error("Unable to handle restriction text: " + restrText); } while (0)

void Restriction::Single::Translate() {
	if (targetType == TargetType::Expression) {
		// Expresions are not touched at this stage.
		return;
	}
	if (targetType == TargetType::Direction) {
		// This is only valid in restrictions on special tasks referencing
		// the player movement task. It has a special two-token syntax.
		const char *x;
		if ((x = SkipText(restrText.c_str(), "MustNot Be")))
			positive = false;
		else if (!(x = SkipText(restrText.c_str(), "Must Be")))
			throw std::runtime_error("Invalid direction restriction: " + restrText);
		lhs = x;
		return;
	}
	const char *x = restrText.c_str();
	std::string tok;
	GET_TOKEN;
	if (targetType == TargetType::Property) {
		// For restrictions on properties, the property name is first
		prop = tok;
		GET_TOKEN;
	}
	// The object/variable in question
	lhs = tok;

	// Handle must / must not
	GET_TOKEN;
	if (tok == "Must") positive = true;
	else if (tok == "MustNot") positive = false;
	else throw std::runtime_error("Unable to handle restriction text: " + restrText);

	// Figure out the condition we're dealing with.
	bool reverseConditionSides = false;
	GET_TOKEN;
	if (tok == "SeenByCharacter" || tok == "HaveBeenSeenByCharacter") {
		cond = ConditionType::SeenByChar;
	} else if (tok == "BeInGroup" || tok == "BeMemberOfGroup") {
		cond = ConditionType::InGroup;
	} else if (tok == "HaveProperty") {
		cond = ConditionType::HaveProp;
	} else if (tok == "BeLocation" || tok == "BeObject" || tok == "BeCharacter") {
		cond = ConditionType::EqualTo;
	} else if (tok == "Exist") {
		cond = ConditionType::Exist;
	} else if (tok == "EqualTo" || tok == "BeEqualTo" || tok == "BeExactText") {
		cond = ConditionType::EqualTo;
	} else if (tok == "BeGreaterThan") {
		cond = ConditionType::GreaterThan;
	} else if (tok == "BeGreaterThanOrEqualTo") {
		cond = ConditionType::GreaterOrEqual;
	} else if (tok == "BeLessThan") {
		cond = ConditionType::LessOrEqual;
	} else if (tok == "BeLessThanOrEqualTo") {
		cond = ConditionType::LessOrEqual;
	} else if (tok == "BeContain") {
		cond = ConditionType::ContainText;
	} else if (tok == "BeAtLocation") {
		cond = ConditionType::AtLocation;
	} else if (tok == "BeInSameLocationAsCharacter" || tok == "BeInSameLocationAsObject") {
		cond = ConditionType::InSameLocationAs;
	} else if (tok == "BeInsideObject") {
		cond = ConditionType::InObject;
	} else if (tok == "BeOnCharacter" || tok == "BeOnObject") {
		cond = ConditionType::OnObject;
	} else if (tok == "BeType") {
		cond = ConditionType::OfType;
	} else if (tok == "BeAlone") {
		cond = ConditionType::Alone;
	} else if (tok == "BeAloneWith") {
		cond = ConditionType::AloneWith;
	} else if (tok == "BeInConversationWith") {
		cond = ConditionType::InConversationWith;
	} else if (tok == "HaveRouteInDirection") {
		cond = ConditionType::HaveRoute;
	} else if (tok == "HaveSeenCharacter" || tok == "HaveSeenLocation" || tok == "HaveSeenObject") {
		// `x has seen y` == `y has been seen by x`
		reverseConditionSides = true;
		cond = ConditionType::SeenByChar;
	} else if (tok == "BeHoldingObject") {
		// `x is holding y` == `y is held by x`
		reverseConditionSides = true;
		cond = ConditionType::HeldBy;
	} else if (tok == "BeWearingObject") {
		reverseConditionSides = true;
		cond = ConditionType::WornBy;
	} else if (tok == "BeOfGender") {
		cond = ConditionType::OfGender;
	} else if (tok == "BeSittingOnObject") {
		// These should really by two separate checks: "Is on object" and "is in position",
		// but that would require inserting a new ANDed restriction into the block, adjusting
		// the sequencing and all that -- it's just too much hassle.
		cond = ConditionType::SittingOn;
	} else if (tok == "BeStandingOnObject") {
		cond = ConditionType::StandingOn;
	} else if (tok == "BeLyingOnObject") {
		cond = ConditionType::LyingOn;
	} else if (tok == "BeInPosition") {
		cond = ConditionType::InPosition;
	} else if (tok == "BeVisibleToCharacter") {
		// currently visible, rather than ever having been visible
		cond = ConditionType::VisibleTo;
	} else if (tok == "BeHeldByCharacter") {
		cond = ConditionType::HeldBy;
	} else if (tok == "BeWornByCharacter") {
		cond = ConditionType::WornBy;
	} else if (tok == "BeInState") {
		cond = ConditionType::InState;
	} else if (tok == "BeHidden") {
		cond = ConditionType::BeHidden;
	} else if (tok == "BePartOfCharacter" || tok == "BePartOfObject") {
		cond = ConditionType::PartOf;
	} else if (tok == "BeWithinLocationGroup") {
		cond = ConditionType::WithinGroup;
	} else if (tok == "BeComplet") {
		cond = ConditionType::Complete;
	} else {
		throw std::runtime_error("Unable to handle restriction text: " + restrText);
	}

	// For conditions that don't have a right-hand side, we are done.
	if (!ConditionHasRHS(cond))
		return;

	if (targetType == TargetType::Variable || targetType == TargetType::Property) {
		// Conditions on properties and variables have an expression as their right-hand sides.
		rhs = x;
		return;
	}

	// Get the right-hand side of the condition, swapping them if necessary.
	GET_TOKEN;
	if (reverseConditionSides) {
		rhs = lhs;
		lhs = tok;
	} else {
		rhs = tok;
	}
}

}