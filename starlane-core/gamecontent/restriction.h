#pragma once

#ifndef SLC_RESTRICTION_H
#define SLC_RESTRICTION_H

#include "../slc_private.h"

#include <utility>
#include <string>
#include <vector>

namespace Starlane {

class Restriction {
public:
	static Restriction *CreateFromXML(const pugi::xml_node &xmlNode);

	std::pair<bool, DescrRef> PassRestrictionBlock() const;

	enum class TargetType {
		Object,  // Object/Location/Character/Item
		Property,
		Task,
		Variable,
		Direction,
		Expression
	};
	static TargetType ParseTargetType(const char *txt);

	enum class RelationType {
		Exist,
		BeEqualTo,
		// TODO
	};

private:
	Restriction() = default;

	std::pair<bool, DescrRef> PassRestrictionBlock(size_t &tidx, size_t &ridx, size_t brackets) const;

		struct Single {
		bool Pass() const;
		void Translate();

		std::string restrText;
		DescrRef failureMsg = 0;

		// True if this is a `must be` restriction, false otherwise.
		bool positive = true;

		TargetType targetType;
		std::string lhs;
		std::string prop;
		std::string rhs;
	};

	std::vector<Single> restrs;
	std::string sequence;
};

}

#endif  // !SLC_RESTRICTION_H
