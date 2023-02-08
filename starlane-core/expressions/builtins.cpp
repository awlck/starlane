#include "../expression.h"
#include "exprp_utility.h"
#include "../starlane-core.h"
#include "../game.h"
#include "../valueparsers.h"
#include "../gamecontent/character.h"


namespace Starlane {
#define CHECK_ARGCOUNT(funcname, cnt) do { if (args->arity != (cnt)) throw std::runtime_error("Wrong number of arguments to built-in function " funcname ": expected " #cnt ", got " + std::to_string(args->arity)); } while (0)
#define CHECK_ARGCOUNT_V(fname, min_, max_) do { if (args->arity < (min_) || args->arity > (max_)) throw std::runtime_error("Wrong number of arguments to built-in function " funcname ": expected " #min_ " to " #max_ ", got " + std::to_string(args->arity)); } while (0)

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
}

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
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer) {
		theArg.ty = Expr::ValueType::String;
		theArg.Str = std::to_string(theArg.Int);
	}
	if (theArg.ty == Expr::ValueType::Invalid)
		throw std::runtime_error("Invalid value.");
	auto *theChar = dynamic_cast<Character *>(Game::Get()->GetObject(theArg.Str));
	if (theChar == nullptr)
		return Expr::Value();
	return theChar->GetDescriptor();
}

Expr::Value Expression::CharacterProperImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("CharacterProper", 1);
	auto theArg = EvalAnyNode(args);
	if (theArg.ty == Expr::ValueType::Integer) {
		theArg.ty = Expr::ValueType::String;
		theArg.Str = std::to_string(theArg.Int);
	}
	if (theArg.ty == Expr::ValueType::Invalid)
		throw std::runtime_error("Invalid value.");
	auto *theChar = dynamic_cast<Character *>(Game::Get()->GetObject(theArg.Str));
	if (theChar == nullptr)
		return Expr::Value();
	return theChar->GetProperName();
}

Expr::Value Expression::DisplayObjectImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("DisplayObject/DisplayCharacter", 1);
	auto theArg = EvalAnyNode(args);
	if (theArg.ty == Expr::ValueType::Integer) {
		theArg.ty = Expr::ValueType::String;
		theArg.Str = std::to_string(theArg.Int);
	}
	if (theArg.ty == Expr::ValueType::Invalid)
		throw std::runtime_error("Invalid value.");
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

}