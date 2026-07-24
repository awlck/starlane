#pragma once

#ifndef SLC_RESTRICTION_H
#define SLC_RESTRICTION_H

#include "../slc_private.h"

#include <utility>
#include <string>
#include <vector>

#define RESTRICTION_PASSES(id) ((id) == 0 || Game::Get()->GetRestriction((id))->PassRestrictionBlock().first)
#define RESTRICTION_RESULT(id) ((id) == 0 ? { true, 0 } : Game::Get()->GetRestriction((id))->PassRestrictionBlock())

namespace Starlane {

class Restriction {
public:
	static Restriction *CreateFromXML(const pugi::xml_node &xmlNode);

	std::pair<bool, DescrRef> PassRestrictionBlock(bool ignoreUnsetRefs = false) const;

	enum class TargetType {
		ErrorType,
		Object,  // Object/Location/Character/Item
		Property,
		Task,
		Variable,
		Direction,
		Expression
	};
	static TargetType ParseTargetType(const char *txt);

	enum class ConditionType {
		ErrorType,
		EqualTo,
		GreaterThan,
		GreaterOrEqual,
		LessThan,
		LessOrEqual,
		ContainText,
		Exist,
		SeenByChar,
		InGroup,  // be member of that group
		WithinGroup,  // in a location that is a member of that group
		HaveProp,
		AtLocation,
		InSameLocationAs,
		InObject,
		OnObject,
		OfType,
		Alone,  // is only character in the location
		AloneWith,  // lhs and rhs are only chars in location
		InConversationWith,
		HaveRoute,
		OfGender,
		LyingOn,
		SittingOn,
		StandingOn,
		InPosition,
		VisibleTo,
		BeHidden,
		HeldBy,
		WornBy,
		InState,
		PartOf,
		// on tasks:
		Complete
	};

private:
	Restriction() = default;

	std::pair<bool, DescrRef> PassRestrictionBlock(size_t &tidx, size_t &ridx, size_t brackets, bool ignoreUnsetRefs) const;

	// A single condition in the larger block of restrictions
	struct Single {
		// Whether or not this condition is fulfilled.
		bool Pass(DescrRef *out, bool ignoreUnsetRefs) const { return positive == PassImpl(out, ignoreUnsetRefs); };
		// Break the `restrText` into conditions.
		void Translate();

		// The text of the restriction as it appears in the XML file
		std::string restrText;
		// Reference for the message to be displayed if this restriction isn't fulfilled.
		DescrRef failureMsg = 0;

		// True if this is a `must be` restriction, false otherwise.
		bool positive = true;

		// Set when this restriction couldn't be parsed at load time (a malformed expression,
		// an unrecognized condition). A faulty restriction is never evaluated and always fails,
		// mirroring how comparing NaN always fails -- see PassRestrictionBlock.
		bool faulty = false;

		TargetType targetType = TargetType::ErrorType;
		std::string lhs;
		// For variables, also store the index
		uint32_t varIdx = 0;
		// Whether the left-hand object is really a reference to the input.
		bool lhsIsRef = false;
		std::string prop;
		ConditionType cond = ConditionType::ErrorType;
		std::string rhs;
		// Whether the right-hand object is really a reference to the input.
		bool rhsIsRef = false;

		ExprRef exprContent = 0;

	private:
		// Whether the underlying condidion is fulfilled, not accounting for the `positive` flag.
		bool PassImpl(DescrRef *out, bool ignoreUnsetRefs) const;
		// Whether an object condition (the TargetType::Object cases) is fulfilled for these
		// operands. Expands the standard library's "AnyObject"/"AnyCharacter" quantifiers
		// existentially: the condition passes if any object of that kind fulfills it.
		bool PassObjectCond(const std::string &lhs, const std::string &rhs, DescrRef *out) const;
	};

	std::vector<Single> restrs;
	std::string sequence;
};

}

#endif  // !SLC_RESTRICTION_H
