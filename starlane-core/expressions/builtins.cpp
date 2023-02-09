#include "../expression.h"
#include "exprp_utility.h"
#include "../starlane-core.h"
#include "../game.h"
#include "../valueparsers.h"
#include "../gamecontent/character.h"
#include "../gamecontent/location.h"


namespace Starlane {
#define CHECK_ARGCOUNT(funcname, cnt) do { if (args->arity != (cnt)) throw std::runtime_error("Wrong number of arguments to built-in function " funcname ": expected " #cnt ", got " + std::to_string(args->arity)); } while (0)
#define CHECK_ARGCOUNT_V(funcname, min_, max_) do { if (args->arity < (min_) || args->arity > (max_)) throw std::runtime_error("Wrong number of arguments to built-in function " funcname ": expected " #min_ " to " #max_ ", got " + std::to_string(args->arity)); } while (0)

#define EXTRACT_STRING_ARG(from, to) \
	auto (to) = EvalAnyNode((from->child.first)); \
	if ((to).ty == Expr::ValueType::Integer) { \
		(to).ty = Expr::ValueType::String; \
		(to).Str = std::to_string((to).Int); \
	} else if ((to).ty == Expr::ValueType::Invalid) { \
		throw std::runtime_error("Invalid value."); \
	}

namespace Expr {

const char *LanguageTens[] = { 0, 0, "twenty", "thirty", "fourty", "fifty", "sixty", "seventy", "eighty", "ninety" };
const char *LanguageOnes[] = { 0, "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };

std::string LanguageNumber(int64_t num, bool f = false) {
	// adapted from Inform's implementation but extended for 64-bit integers
	// https://github.com/ganelson/inform/blob/3ea9ce725f0b06bd58a12f9dec7c6ec3afb0ef01/inform7/Internal/Inter/BasicInformKit/Sections/Paragraphing.i6t#L250
	if (num == 0) return "zero";
	std::string result;
	if (num < 0) {
		result = "minus ";
		num = -num;
	}
	if (num >= 1000000000000000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000000000000000);
		result += " quintillion";
		num /= 1000000000000000000;
		f = true;
	}
	if (num >= 1000000000000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000000000000);
		result += " quadrillion";
		num /= 1000000000000000;
		f = true;
	}
	if (num >= 1000000000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000000000);
		result += " trillion";
		num /= 1000000000000;
		f = true;
	}
	if (num >= 1000000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000000);
		result += " billion";
		num /= 1000000000;
		f = true;
	}
	if (num >= 1000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000);
		result += " million";
		num /= 1000000;
		f = true;
	}
	if (num >= 1000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000);
		result += " thousand";
		num /= 1000;
		f = true;
	}
	if (num >= 100) {
		if (f) result += ", ";
		result += LanguageNumber(num % 100);
		result += " hundred";
		num /= 100;
		f = true;
	}
	if (num == 0) return result;
	if (f) result += " and ";
	if (num >= 20) {
		result += LanguageTens[num / 10];
		if (num%10 != 0) {
			result += ' ';
			result += LanguageNumber(num % 10);
		}
	} else {
		result += LanguageOnes[num];
	}
	return result;
}

enum class ListTransformType {
	None,  // take list entries at face value and don't perform any transformation
	Name   // take list entries as object keys and apply the `object.Name' function to each before formatting
};

// Takes a list in ADRIFT Expression list format (e.g., "foo|bar|baz") and returns
// a textual description, e.g., "foo, bar and baz"
std::string WriteListFrom(const std::string &lst, ListTransformType transform = ListTransformType::None) {
	std::string result;
	std::vector<std::string> entries;
	std::string current;
	current.reserve(64);
	for (auto c : lst) {
		if (c == '|' && !current.empty()) {
			entries.push_back(current);
			current.clear();
			continue;
		}
		current.append(1, c);
	}
	if (!current.empty())
		entries.push_back(current);
	size_t cnt = entries.size();
	for (size_t i = 0; i < cnt; i++) {
		if (i > 0 && i <= cnt - 3)
			result += ", ";
		else if (i == cnt - 2)
			result += " and ";
		switch (transform) {
		case ListTransformType::Name:
			result += Game::Get()->GetObject(entries[i])->GetDisplayName();
			break;
		case ListTransformType::None:
			result += entries[i];
			break;
		}
	}
	return result;
}

std::string ShowPronounForChar(const std::string &key, Pronoun pronoun) {
	auto *g = Game::Get();
	if (key == g->GetReference("%Player%") && g->GetPCReferralPerson() == Game::ReferralPerson::FirstPerson) {
		switch (pronoun) {
		case Pronoun::Subject:
			return "I";
		case Pronoun::Object:
			return "me";
		case Pronoun::Possessive:
			return "my";
		case Pronoun::Reflective:
			return "myself";
		}
	} else if (key == g->GetReference("%Player%") && g->GetPCReferralPerson() == Game::ReferralPerson::SecondPerson) {
		switch (pronoun) {
		case Pronoun::Subject:
		case Pronoun::Object:
			return "you";
		case Pronoun::Possessive:
			return "your";
		case Pronoun::Reflective:
			return "yourself";
		}
	} else {
		auto *c = g->GetObject(key);
		if (c->HasProp("Gender")) {
			if (c->GetPropValue<std::string>("Gender") == "Male") {
				switch (pronoun) {
				case Pronoun::Subject:
					return "he";
				case Pronoun::Object:
					return "him";
				case Pronoun::Possessive:
					return "his";
				case Pronoun::Reflective:
					return "himself";
				}
			} else if (c->GetPropValue<std::string>("Gender") == "Female") {
				switch (pronoun) {
				case Pronoun::Subject:
					return "she";
				case Pronoun::Object:
				case Pronoun::Possessive:
					return "her";
				case Pronoun::Reflective:
					return "herself";
				}
			}
		}
		// no gender property or unknown value
		switch (pronoun) {
		case Pronoun::Subject:
		case Pronoun::Object:
			return "it";
		case Pronoun::Possessive:
			return "its";
		case Pronoun::Reflective:
			return "itself";
		}
	}
	return "<invalid>";
}

}  // namespace Expr

Expr::Value Expression::LCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("LCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return std::to_string(theArg.Int);
	else if (theArg.ty == Expr::ValueType::String) {
		return frontend->StrToLowerCase(theArg.Str);
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::UCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("UCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return std::to_string(theArg.Int);
	else if (theArg.ty == Expr::ValueType::String) {
		return frontend->StrToUpperCase(theArg.Str);
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::PCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("PCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return std::to_string(theArg.Int);
	else if (theArg.ty == Expr::ValueType::String) {
		return frontend->StrToSentenceCase(theArg.Str);
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::NumberAsTextImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("NumberAsText", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer) {
		return Expr::LanguageNumber(theArg.Int);
	} else if (theArg.ty == Expr::ValueType::String) {
		// attempt to convert to integer
		size_t pos = 0;
		while (isspace(theArg.Str[pos])) ++pos;
		if (IsDigits(theArg.Str.c_str() + pos)) {
			return Expr::LanguageNumber(ParseInt(theArg.Str.c_str() + pos));
		} else throw std::runtime_error("NumberAsText called for a non-number.");
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::CharacterDescriptorImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("CharacterDescriptor", 1);
	EXTRACT_STRING_ARG(args, theArg);
	auto *theChar = dynamic_cast<Character *>(Game::Get()->GetObject(theArg.Str));
	if (theChar == nullptr)
		return Expr::Value();
	return theChar->GetDescriptor();
}

Expr::Value Expression::CharacterProperImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("CharacterProper", 1);
	EXTRACT_STRING_ARG(args, theArg);
	auto *theChar = dynamic_cast<Character *>(Game::Get()->GetObject(theArg.Str));
	if (theChar == nullptr)
		return Expr::Value();
	return theChar->GetProperName();
}

Expr::Value Expression::DisplayObjectImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("DisplayObject/DisplayCharacter", 1);
	EXTRACT_STRING_ARG(args, theArg);
	auto *theObj = Game::Get()->GetObject((theArg.Str));
	return theObj->GetDescription();
}

Expr::Value Expression::AloneWithCharImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("AloneWithChar", 0);
	auto g = Game::Get();
	auto player = g->GetPlayerChar();
	for (const auto &objref : g->GetAllObjects()) {
		if (Character *c = dynamic_cast<Character *>(objref.second)) {
			if (objref.second != player && c->GetLocationKey() == player->GetLocationKey())
				return c->Key();
		}
	}
	return Expr::Value();  // invalid
}

Expr::Value Expression::LocationNameImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("LocationName", 1);
	auto g = Game::Get();
	EXTRACT_STRING_ARG(args, theArg);
	auto *loc = dynamic_cast<Location *>(Game::Get()->GetObject(theArg.Str));
	if (!loc)
		return std::string("<invalid location>");
	return loc->GetDisplayName();
}

Expr::Value Expression::TheObjectImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("TheObject(s)", 1);
	EXTRACT_STRING_ARG(args, theArg);
	return Expr::WriteListFrom(theArg.Str, Expr::ListTransformType::Name);
}

Expr::Value Expression::CharacterNameImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT_V("CharacterName", 0, 2);
	auto g = Game::Get();
	std::string toDisplay;
	Pronoun pronoun = Pronoun::Subject;
	const auto &mostRecent = g->GetMostRecentlyMentioned();
	if (args->arity == 0) {
		// if we are displaying a character's description, this reference will be set and we should
		// use that character.
		toDisplay = g->GetReference("<referral-character>");
		if (toDisplay.empty())  // default to the player character
			toDisplay = g->GetReference("%Player%");
	} else if (args->arity == 1) {
		EXTRACT_STRING_ARG(args, theArg);
		toDisplay = theArg.Str;
	} else if (args->arity == 2) {
		EXTRACT_STRING_ARG(args, theChar);
		toDisplay = theChar.Str;
		auto thePronoun = EvalAnyNode(args->child.last);
		if (thePronoun.ty == Expr::ValueType::Integer) {
			thePronoun.ty = Expr::ValueType::String;
			thePronoun.Str = std::to_string(thePronoun.Int);
		} else if (thePronoun.ty == Expr::ValueType::Invalid)
			throw std::runtime_error("Invalid value.");
		if (thePronoun.Str == "object" || thePronoun.Str == "objective" || thePronoun.Str == "target") {
			if (mostRecent.first == toDisplay && mostRecent.second == Pronoun::Subject)
				pronoun = Pronoun::Reflective;
			else pronoun = Pronoun::Object;
		} else if (thePronoun.Str == "possessive")
			pronoun = Pronoun::Possessive;
	}
	if (toDisplay == mostRecent.first || toDisplay == g->GetReference("%Player%"))
		return Expr::ShowPronounForChar(toDisplay, pronoun);
	else
		return g->GetObject(toDisplay)->GetDisplayName();
}

}