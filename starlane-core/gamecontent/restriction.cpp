#include "restriction.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string.h>

#include <pugixml.hpp>

#include "../error.h"
#include "../expression.h"
#include "../game.h"
#include "../valueparsers.h"
#include "character.h"
#include "description.h"
#include "group.h"
#include "location.h"
#include "property.h"
#include "utility.h"
#include "variable.h"

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
		switch (seq[tidx++]) {
		case '(':
			b++;
			break;
		case ')':
			b--;
			break;
		case '#':
			ridx++;
			break;
		case 'A':
		case 'O':
			break;
		default:
			UNREACHABLE();
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

	// skip multiple spaces
	while (**str && isspace((unsigned char) **str)) (*str)++;
	const char *tokenStart = *str;
	// advance past all consecutive characters that aren't spaces (this correctly
	// stops at either a space or the string's terminating '\0')
	while (**str && !isspace((unsigned char) **str)) (*str)++;
	return std::string(tokenStart, *str);
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
		// A restriction the Runner would only have choked on if it ever evaluated (a malformed
		// expression, say) must not abort the whole load here. Mark it faulty and carry on; it
		// will simply always fail at runtime.
		try {
			s.Translate();
		} catch (const std::exception &e) {
			LogError(std::string("Faulty restriction (") + e.what() + "); it will always fail.");
			s.faulty = true;
		}
		result->restrs.emplace_back(std::move(s));
	}
	std::string sequence(xmlNode.child_value("BracketSequence"));
	// ADRIFT abbreviates doubled brackets in sequences: '[' stands for "((" and ']' for "))".
	for (size_t i = 0; i < sequence.size(); i++) {
		if (sequence[i] == '[') sequence.replace(i, 1, "((");
		else if (sequence[i] == ']') sequence.replace(i, 1, "))");
	}
	TransformRestrictionSequence(sequence);
	result->sequence = sequence;
	return result;
}

std::pair<bool, DescrRef> Restriction::PassRestrictionBlock(bool ignoreUnsetRefs) const {
	size_t tidx = 0;
	size_t ridx = 0;
	return PassRestrictionBlock(tidx, ridx, 0, ignoreUnsetRefs);
}

std::pair<bool, DescrRef> Restriction::PassRestrictionBlock(size_t &tidx, size_t &ridx, size_t brackets, bool ignoreUnsetRefs) const {
	// An empty sequence means there are no restrictions to check (e.g. a task/description with
	// no <Restrictions> at all), which should trivially pass rather than fail.
	std::pair<bool, DescrRef> state{true, 0};
	while (tidx < sequence.size()) {
		if (sequence[tidx] == '(') {
			state = PassRestrictionBlock(++tidx, ridx, brackets + 1, ignoreUnsetRefs);
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
			
			// Allow the restriction-processing mechanism to change the failure message. This is only
			// applicable to the "has route in direction" restriction, so doing this is more convenient
			// than returning a pair.
			DescrRef txt = 0;
			// A faulty restriction (one that wouldn't parse) always fails, as does one whose
			// evaluation throws -- a comparison against a nonexistent object, a NaN, and so on.
			// Either way the enclosing task/description is denied, and we log the runtime kind.
			bool passed;
			if (restrs[ridx].faulty) {
				passed = false;
			} else {
				try {
					passed = restrs[ridx].Pass(&txt, ignoreUnsetRefs);
				} catch (const std::exception &e) {
					LogError(std::string("Restriction failed to evaluate (") + e.what()
					         + "); treating as failed.");
					passed = false;
				}
			}
			if (passed) {
				state = { true, 0 };
			} else if (txt && !Game::Get()->GetDescription(txt)->Build().empty()) {
				// If the failure message was overridden and the override message doesn't
				// come out empty, use that.
				state = { false, txt };
				// ... but still mark the original text as displayed.
				(void) Game::Get()->MutableDescription(restrs[ridx].failureMsg)->BuildAndCommit();
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

bool Restriction::Single::PassImpl(DescrRef *out, bool ignoreUnsetRefs) const {
	// Yes, this shadows the fields `lhs` and `rhs`. That's sort of the point.
	// (The "unset reference" skips must only apply to actual references: e.g. property
	//  restrictions always leave `rhs` empty, but still need to be evaluated.)
	// "ReferencedObjects Must BeExactText All" asks about the *words the player typed*, not about
	// whatever object the reference ended up bound to: it is how the standard library tells "put all
	// in bag" from "put the ball in bag". ADRIFT special-cases it the same way (clsUserSession's
	// restriction evaluator consults the reference's sCommandReference), because by this point the
	// word ALL has already been expanded into the things it stood for.
	if (lhsIsRef && cond == ConditionType::EqualTo && Util::ToLower(this->rhs) == "all")
		return Game::Get()->ReferenceWasAll(Util::CanonicalizeRefName(this->lhs));

	const std::string &lhs = lhsIsRef ? Game::Get()->GetReference(this->lhs) : this->lhs;
	if (ignoreUnsetRefs && lhsIsRef && lhs.empty())
		return true;
	const std::string &rhs = rhsIsRef ? Game::Get()->GetReference(this->rhs) : this->rhs;
	if (ignoreUnsetRefs && rhsIsRef && ConditionHasRHS(cond) && rhs.empty())
		return true;

	switch (targetType) {
	case TargetType::Object:
		// Handled below.
		break;
	case TargetType::Task:
		if (cond == ConditionType::Complete)
			return Game::Get()->GetIsTaskCompleted(lhs);
		else
			throw std::runtime_error("Invalid condition on tasks while evaluating restriction.");
	case TargetType::Expression:
		return Game::Get()->GetExpression(exprContent)->EvaluateBool();
	case TargetType::Property:
	case TargetType::Variable:
	{
		int64_t intVal;
		std::string strVal;
		bool isInt = false;
		if (targetType == TargetType::Variable) {
			if (lhsIsRef) {
				// The left-hand side doesn't name a variable, but is a command reference
				// (e.g. "ReferencedText"/"ReferencedNumber") whose captured value has
				// already been substituted into `lhs` above.
				bool numeric = this->lhs.compare(0, sizeof("ReferencedNumber") - 1, "ReferencedNumber") == 0
					|| this->lhs.compare(0, sizeof("%number") - 1, "%number") == 0;
				if (numeric) {
					intVal = ParseInt(lhs.c_str());
					isInt = true;
				} else {
					strVal = lhs;
				}
			} else {
				const Variable *theVar = Game::Get()->GetVariable(lhs);
				if (!theVar)
					throw std::runtime_error("Restriction references unknown variable: " + lhs);
				if (theVar->GetType() <= Variable::Type::IntArray) {
					intVal = theVar->GetValue<int64_t>(varIdx);
					isInt = true;
				} else {
					strVal = theVar->GetValue<std::string>(varIdx);
				}
			}
		} else {  // Property
			const Property *meta = Game::Get()->GetPropMeta(prop);
			const GameObj *obj = Game::Get()->GetObject(lhs);
			switch (meta->Type()) {
			case Property::ValueType::Int:
			case Property::ValueType::Map:
			case Property::ValueType::Bool:  // Should never happen here, but just to be sure...
				intVal = obj->GetIntProp(prop);
				isInt = true;
				break;
			case Property::ValueType::Object:
			case Property::ValueType::Enum:
				strVal = obj->GetStrProp(prop);
				break;
			case Property::ValueType::Text:
				strVal = Game::Get()->GetDescription(obj->GetIntProp(prop))->Build();
				break;
			default:
				throw std::runtime_error("Invalid property type while evaluating restriction.");
			}
		}

		if (isInt) {
			int64_t result = Game::Get()->GetExpression(exprContent)->EvaluateInt();
			switch (cond) {
			case ConditionType::EqualTo:
				return intVal == result;
			case ConditionType::GreaterThan:
				return intVal > result;
			case ConditionType::GreaterOrEqual:
				return intVal >= result;
			case ConditionType::LessThan:
				return intVal < result;
			case ConditionType::LessOrEqual:
				return intVal <= result;
			default:
				throw std::runtime_error("Invalid int operation while evaluating restriction.");
			}
		} else {
			// An enum state name ("At Location") or an object-valued property's target is a
			// literal held in `rhs`, not compiled into an expression; exprContent == 0 (never a
			// real id, since CreateExpression only ever hands out negative ones) marks that case.
			// Everything else -- variables, text properties -- is a genuine expression.
			std::string result = exprContent != 0
				? Game::Get()->GetExpression(exprContent)->EvaluateStr()
				: rhs;
			// Guard against doubly-quoted strings which would never match otherwise:
			if (result.size() > 2 && result[0] == '\'' && result[result.size() - 1] == '\'') {
				result = result.substr(1, result.size() - 2);
			}
			if (cond == ConditionType::EqualTo)
				return strVal == result;
			else if (cond == ConditionType::ContainText)
				return strstr(strVal.c_str(), result.c_str()) != nullptr;
			else
				throw std::runtime_error("Invalid string operation while evaluating restriction.");
		}
	}
	case TargetType::Direction: {
		// A direction restriction (only valid on movement-related tasks) checks the
		// direction the player is actually moving -- captured from the command into the
		// ReferencedDirection reference -- against the direction the restriction names
		// (held in `lhs`). It passes when they are the same direction.
		const std::string &movedDir = Game::Get()->GetReference("ReferencedDirection");
		// While merely testing a task's applicability (no command matched yet), an unset
		// direction shouldn't veto the task; leave that to the real restriction check.
		if (ignoreUnsetRefs && movedDir.empty())
			return true;
		return lhs == movedDir;
	}
	case TargetType::ErrorType:
		throw std::runtime_error("Unknown restriction type while evaluating restriction.");
	}

	return PassObjectCond(lhs, rhs, out);
}

bool Restriction::Single::PassObjectCond(const std::string &lhs, const std::string &rhs, DescrRef *out) const {
	Game *g = Game::Get();

	// The standard library quantifies some conditions over "AnyObject" or "AnyCharacter"
	// rather than naming a specific thing. Expand these existentially: the condition
	// passes if some object of the requested kind fulfills it. (For a `MustNot`
	// restriction this means no such object may fulfill it, since the negation
	// happens in Pass(), outside of us.)
	// "NoObject"/"NoCharacter" are the same quantifier negated -- the condition holds precisely when
	// nothing of that kind fulfills it, which is how the library asks "is the player carrying
	// nothing at all?" ("NoObject Must BeHeldByCharacter Player"). Treated here rather than left to
	// the Must/MustNot wrapper, which negates the whole restriction and would give "not everything".
	for (const std::string *side: { &lhs, &rhs }) {
		const bool anyForm = *side == "AnyObject" || *side == "AnyCharacter";
		const bool noneForm = *side == "NoObject" || *side == "NoCharacter";
		if (!anyForm && !noneForm) continue;
		bool wantChar = (*side == "AnyCharacter" || *side == "NoCharacter");
		for (const GameObj *o: g->GetAllObjects()) {
			if (o->IsLocation()) continue;
			if (o->IsCharacter() != wantChar) continue;
			bool pass = (side == &lhs) ? PassObjectCond(o->Key(), rhs, out)
			                           : PassObjectCond(lhs, o->Key(), out);
			if (pass) return anyForm;
		}
		return noneForm;
	}

	// And now for all the various restrictions on objects...
	switch (cond) {
	case ConditionType::EqualTo:
		return lhs == rhs;
	case ConditionType::Exist:
		return g->ObjectExists(lhs);
	case ConditionType::SeenByChar:
	{
		const Character *c = AsCharacter(g->GetObject(rhs));
		// The ADRIFT Developer application shouldn't generate files in which
		// rhs is not of type character, but better safe than sorry...
		// We'll have to see whether we should throw an error here, or just silently return false.
		if (!c)
			throw std::runtime_error("Restriction on characters references an object which isn't a character: " + rhs);
		return c->HasSeen(lhs);
	}
	case ConditionType::VisibleTo:
	{
		const Character *c = AsCharacter(g->GetObject(rhs));
		if (!c)
			throw std::runtime_error("Restriction on characters references an object which isn't a character: " + rhs);
		return c->CanSee(lhs);
	}
	case ConditionType::InGroup:
		return g->GetObject(lhs)->IsMemberOfGroup(rhs);
	case ConditionType::WithinGroup:
	{
		const GameObj *loc = g->GetObject(lhs)->GetLocation();
		if (!loc) return false;
		return loc->IsMemberOfGroup(rhs);
	}
	case ConditionType::HaveProp:
	{
		const GameObj *l = g->GetObject(lhs);
		switch (g->GetPropMeta(rhs)->Type()) {
		case Property::ValueType::ErrorType:
			throw std::runtime_error("Invalid type for property " + rhs + " while evaluating restriction.");
		case Property::ValueType::Bool:
			return l->GetBoolProp(rhs);
		case Property::ValueType::Int:
		case Property::ValueType::Text:
		case Property::ValueType::Map:
			return l->GetAllIntProps().count(rhs) > 0;
		case Property::ValueType::Object:
		case Property::ValueType::Enum:
			return l->GetAllStrProps().count(rhs) > 0;
		}
		// should never be able to get here since the above enum is exhaustive, but gcc apparently thinks we can...
		return false;
	}
	case ConditionType::AtLocation: {
		const GameObj *l = g->GetObject(lhs);
		if (l->GetLocationKey() == rhs) return true;
		// A static object spread over a whole location group is at every one of those locations at
		// once and so has no single location key of its own; the location itself is what can answer
		// for it. (Alyas moves the oak tree's door "to location group" and then asks whether it is
		// at the particular clearing the player is standing in.)
		const Location *loc = AsLocation(g->TryGetObject(rhs));
		return loc && loc->HoldsDirectly(l);
	}
	case ConditionType::InSameLocationAs:
	{
		const std::string &here = g->GetObject(lhs)->GetLocationKey();
		// The thing compared against may be a group standing in for its members, as ADRIFT's
		// CanSeeObject expands one: "in the same location as <group>" holds when lhs shares a
		// location with any member. (Jacaranda Jim tests the player against a "LightSources"
		// group of objects.)
		if (const Group *grp = g->GetGroup(rhs)) {
			for (const auto &member: grp->GetAllMembers()) {
				const GameObj *m = g->GetObject(member);
				if (m && m->GetLocationKey() == here)
					return true;
			}
			return false;
		}
		const GameObj *r = g->GetObject(rhs);
		return r && r->GetLocationKey() == here;
	}
	case ConditionType::InObject:
	case ConditionType::HeldBy:  // Starlane treats `held by` simply as `in`.
	{
		const GameObj *l = g->GetObject(lhs);
		return l->GetParentKey() == rhs && l->GetParentRelation() == GameObj::HoldingType::InObject;
	}
	case ConditionType::OnObject:
	{
		const GameObj *l = g->GetObject(lhs);
		return l->GetParentKey() == rhs && l->GetParentRelation() == GameObj::HoldingType::OnObject;
	}
	case Starlane::Restriction::ConditionType::OfType:  // ?
		break;
	case ConditionType::Alone: {
		// "Alone" meaning "no other character is in the same location as the lhs"
		const auto *lhsLocation = g->GetObject(lhs)->GetLocation();
		return !std::any_of(g->GetAllObjects().cbegin(), g->GetAllObjects().cend(), [&lhs, &lhsLocation](const GameObj *o) {
			return o->Key() != lhs && o->IsCharacter()
				&& o->GetLocation() == lhsLocation;
		});
	}
	case ConditionType::AloneWith: {
		// "Alone with" meaning "no other character except rhs is in the same location as the lhs"
		const auto *lhsLocation = g->GetObject(lhs)->GetLocation();
		if (lhsLocation != g->GetObject(rhs)->GetLocation()) return false;
		return !std::any_of(g->GetAllObjects().cbegin(), g->GetAllObjects().cend(), [&lhs, &rhs, &lhsLocation](const GameObj *o) {
			return o->Key() != lhs && o->Key() != rhs && o->IsCharacter()
				&& lhsLocation == Game::Get()->GetObject(lhs)->GetLocation();
		});
	}
	case Starlane::Restriction::ConditionType::InConversationWith:
		break;  // TODO (once the conversations system is in place)
	case ConditionType::HaveRoute:
	{
		auto ch = AsCharacter(g->GetObject(lhs));
		if (!ch)
			throw std::runtime_error("Restriction on characters references an object which isn't a character: " + lhs);
		auto result = ch->HasRoute(rhs);
		*out = result.second;
		return result.first;
	}
	case ConditionType::OfGender:
		// This is stored as a mandatory library property rather than as an attribute
		// of the character object itself. We can't just turn these into a C++ enum in
		// Starlane's code because someone could amend the list of possible values for their game.
		return g->GetObject(lhs)->GetStrProp("Gender") == rhs;
	case ConditionType::LyingOn:
	{
		auto l = g->GetObject(lhs);
		return l->GetParentKey() == rhs
			&& l->GetParentRelation() == GameObj::HoldingType::OnObject
			&& l->HasProp("CharacterPosition") && l->GetStrProp("CharacterPosition") == "Lying";
	}
	case ConditionType::SittingOn:
	{
		auto l = g->GetObject(lhs);
		return l->GetParentKey() == rhs
			&& l->GetParentRelation() == GameObj::HoldingType::OnObject
			&& l->HasProp("CharacterPosition") && l->GetStrProp("CharacterPosition") == "Sitting";
	}
	case ConditionType::StandingOn:
	{
		auto l = g->GetObject(lhs);
		return l->GetParentKey() == rhs
			&& l->GetParentRelation() == GameObj::HoldingType::OnObject
			&& l->HasProp("CharacterPosition") && l->GetStrProp("CharacterPosition") == "Standing";
	}
	case ConditionType::InPosition:
	{
		// Unlike most of the "mandatory" library properties, this one is runtime state a character
		// doesn't have until they actually sit/stand/lie down somewhere -- ADRIFT itself guards
		// every read of it with HasProperty (clsCharacter.vb/clsUserSession.vb) rather than assuming
		// it is always set, so a character who never has is simply not "in" any position yet.
		auto obj = g->GetObject(lhs);
		return obj->HasProp("CharacterPosition") && obj->GetStrProp("CharacterPosition") == rhs;
	}
	case ConditionType::BeHidden:
		return g->GetObject(lhs)->GetLocationKey().empty();
	case ConditionType::WornBy:
	{
		const GameObj *l = g->GetObject(lhs);
		return l->GetParentKey() == rhs && l->GetParentRelation() == GameObj::HoldingType::Worn;
	}
	case ConditionType::InState:
	{	// This can be the value of any Enum property, without actually naming the property we need to check...
		// ...except one that merely appends its states to another's: "LockStatus" holds "Locked"
		// on every object that has it, whether or not the object is actually locked, because the
		// state that matters lives in "OpenStatus". ADRIFT skips those here for the same reason.
		const GameObj *l = g->GetObject(lhs);
		// One call, one reference: GetAllStrProps rebuilds the cache it hands back, so taking
		// begin() and end() from separate calls would compare iterators into different containers.
		const auto &props = l->GetAllStrProps();
		return std::any_of(props.cbegin(), props.cend(), [&](const auto &p) {
			const Property *meta = Game::Get()->GetPropMeta(p.first);
			return meta && meta->Type() == Property::ValueType::Enum && meta->AppendsTo().empty()
				&& p.second == rhs;
		});
	}
	case ConditionType::PartOf:
	{
		const GameObj *l = g->GetObject(lhs);
		return l->GetParentKey() == rhs && l->GetParentRelation() == GameObj::HoldingType::PartOf;
	}
	default:
		break;
	}

	return true;
}

#define GET_TOKEN do { if ((tok = NextToken(&x)).empty()) throw std::runtime_error("Unable to handle restriction text: " + restrText); } while (0)

void Restriction::Single::Translate() {
	if (targetType == TargetType::Expression) {
		exprContent = Game::Get()->CreateExpression(restrText);
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
		lhsIsRef = Util::IsReference(lhs);
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
	// The object/variable in question.
	if (targetType == TargetType::Variable) {
		// Find array index for variable, if necessary.
		auto idxiter = std::find(tok.cbegin(), tok.cend(), '[');
		if (idxiter != tok.cend()) {
			lhs = tok.substr(0, std::distance(tok.cbegin(), idxiter));
			++idxiter;
			while (isdigit(*idxiter)) {
				varIdx *= 10;
				varIdx += (*idxiter) - '0';
			}
		} else {
			varIdx = 1;
			lhs = tok;
		}
	} else lhs = tok;
	lhsIsRef = Util::IsReference(lhs);

	// Handle must / must not
	GET_TOKEN;
	if (tok == "Must") positive = true;
	else if (tok == "MustNot") positive = false;
	else throw std::runtime_error("Unable to handle restriction text: " + restrText);

	// The property's metadata, when this restriction targets one -- and null when `prop` names no
	// registered property, which does happen: a restriction on a nonexistent property still loads
	// (it just never passes), and games ship such dead restrictions inside never-run tasks. So
	// every use below must guard against null rather than dereferencing blindly.
	const Property *propMeta = targetType == TargetType::Property ? Game::Get()->GetPropMeta(prop) : nullptr;

	// Figure out the condition we're dealing with.
	bool reverseConditionSides = false;
	const char *condStart = x;
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
	} else if (tok == "BeGreaterThanOrEqualTo" || tok == "GreaterThanOrEqualTo") {
		cond = ConditionType::GreaterOrEqual;
	} else if (tok == "BeLessThan" || tok == "LessThan") {
		cond = ConditionType::LessThan;
	} else if (tok == "BeLessThanOrEqualTo" || tok == "LessThanOrEqualTo") {
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
	} else if (tok == "BeComplete" || tok == "BeCompleted") {
		cond = ConditionType::Complete;
	} else if (targetType == TargetType::Property) {
		// ADRIFT only writes an explicit comparison keyword for property restrictions when it is
		// something other than "equals"; "prop lhs Must/MustNot rhs" implies "must/must not equal".
		// Rewind so the value below sees the whole remaining text, not just its tail.
		cond = ConditionType::EqualTo;
		x = condStart;
	} else {
		throw std::runtime_error("Unable to handle restriction text: " + restrText);
	}

	// For conditions that don't have a right-hand side, we are done.
	if (!ConditionHasRHS(cond))
		return;

	// An enum-valued (StateList) property is compared against a literal state name, which may
	// contain spaces ("At Location", "Can be looked under") and is not an expression. Take the
	// remaining text verbatim rather than handing it to the expression parser below. (Object-
	// valued properties are already caught further up, before any condition keyword is matched;
	// enum ones reach here because they carry an explicit "EqualTo".)
	// The same goes for an object-valued property, which is compared against an object key.
	if (propMeta && (propMeta->Type() == Property::ValueType::Enum || propMeta->Type() == Property::ValueType::Object)) {
		while (*x && isspace((unsigned char) *x)) x++;
		rhs = x;
		while (!rhs.empty() && isspace((unsigned char) rhs.back())) rhs.pop_back();
		rhsIsRef = Util::IsReference(rhs);
		return;
	}

	if (targetType == TargetType::Variable || targetType == TargetType::Property) {
		// ADRIFT distinguishes a restriction value that is an *expression* from one that is a
		// *string constant* by the quotes around it (see FileIO.vb): single quotes wrap an
		// expression, double quotes a string constant, and both are stripped on load. So a value
		// like '%var_A%+%var_B%' must be compiled as the arithmetic expression %var_A%+%var_B%
		// (yielding a number), not read as the literal string "%var_A%+%var_B%" -- which is what
		// our grammar's single-quoted-string rule would otherwise produce, leaving a value like
		// "1+2" that no numeric comparison can use. Strip the single quotes and compile the inside;
		// if the inside isn't a valid expression (e.g. a multi-word string literal), fall back to
		// compiling the value verbatim, letting the grammar treat it as the string it is.
		std::string val = x;
		while (!val.empty() && isspace((unsigned char) val.front())) val.erase(val.begin());
		while (!val.empty() && isspace((unsigned char) val.back())) val.pop_back();
		if (val.size() >= 2 && val.front() == '\'' && val.back() == '\'') {
			try {
				exprContent = Game::Get()->CreateExpression(val.substr(1, val.size() - 2));
				return;
			} catch (const std::exception &) {
				// Not a valid expression; fall through and compile the value as written.
			}
		}
		try {
			exprContent = Game::Get()->CreateExpression(x);
		} catch (std::runtime_error& e) {
			throw std::runtime_error("Unable to handle restriction text: " + restrText + "\n(Encountered expression parse error.)");
		}
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

	lhsIsRef = Util::IsReference(lhs);
	rhsIsRef = Util::IsReference(rhs);
}

}
