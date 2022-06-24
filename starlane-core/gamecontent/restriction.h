#pragma once

#ifndef SLC_RESTRICTION_H
#define SLC_RESTRICTION_H

#include "../slc_private.h"

#include <utility>
#include <string>
#include <vector>

#include "../expressions.h"

namespace Starlane {

class Restriction {
public:
	static Restriction *CreateFromXML(const pugi::xml_node &xmlNode);

	std::pair<bool, DescrRef> PassRestrictionBlock() const;

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

	std::pair<bool, DescrRef> PassRestrictionBlock(size_t &tidx, size_t &ridx, size_t brackets) const;

	// A single condition in the larger block of restrictions
	struct Single {
		// Whether or not this condition is fulfilled.
		bool Pass(DescrRef *out) const { return positive == PassImpl(out); };
		// Break the `restrText` into conditions.
		void Translate();

		// The text of the restriction as it appears in the XML file
		std::string restrText;
		// Reference for the message to be displayed if this restriction isn't fulfilled.
		DescrRef failureMsg = 0;

		// True if this is a `must be` restriction, false otherwise.
		bool positive = true;

		TargetType targetType = TargetType::ErrorType;
		std::string lhs;
		// For variables, also store the index
		uint32_t varIdx = 0;
		std::string prop;
		ConditionType cond = ConditionType::ErrorType;
		std::string rhs;

		const Expression *exprContent = nullptr;

		Single() = default;
		// Need to be careful about that pointer...
		~Single() { if (exprContent) delete exprContent; }
		Single(const Single &) = delete;
		Single(Single &&rhs) : restrText(std::move(rhs.restrText)), failureMsg(rhs.failureMsg),
				positive(rhs.positive), targetType(rhs.targetType), lhs(std::move(rhs.lhs)),
				rhs(std::move(rhs.rhs)), varIdx(rhs.varIdx), prop(std::move(rhs.prop)),
				cond(rhs.cond), exprContent(rhs.exprContent)
		{
			rhs.exprContent = nullptr;
		}
		Single &operator=(const Single &) = delete;
		Single &operator=(Single &&) = delete;

	private:
		// Whether the underlying condidion is fulfilled, not accounting for the `positive` flag.
		bool PassImpl(DescrRef *out) const;
	};

	std::vector<Single> restrs;
	std::string sequence;
};

}

#endif  // !SLC_RESTRICTION_H
