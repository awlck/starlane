#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "../expression.h"
#include "exprp_utility.h"
#include "../starlane-core.h"
#include "../valueparsers.h"


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
		return { Expr::ValueType::String, 0, std::to_string(theArg.Int) };
	else if (theArg.ty == Expr::ValueType::String) {
		return { Expr::ValueType::String, 0, SLFrontend::Services::StrToLowerCase(theArg.Str) };
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::UCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("UCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return { Expr::ValueType::String, 0, std::to_string(theArg.Int) };
	else if (theArg.ty == Expr::ValueType::String) {
		return { Expr::ValueType::String, 0, SLFrontend::Services::StrToUpperCase(theArg.Str) };
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::PCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("PCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return { Expr::ValueType::String, 0, std::to_string(theArg.Int) };
	else if (theArg.ty == Expr::ValueType::String) {
		return { Expr::ValueType::String, 0, SLFrontend::Services::StrToSentenceCase(theArg.Str) };
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::NumberAsTextImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("NumberAsText", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer) {
		return { Expr::ValueType::String, 0, Expr::LanguageNumber(theArg.Int) };
	} else if (theArg.ty == Expr::ValueType::String) {
		// attempt to convert to integer
		size_t pos = 0;
		while (isspace(theArg.Str[pos])) ++pos;
		if (IsDigits(theArg.Str.c_str() + pos)) {
			return { Expr::ValueType::String, 0, Expr::LanguageNumber(ParseInt(theArg.Str.c_str() + pos)) };
		} else throw std::runtime_error("NumberAsText called for a non-number.");
	} else throw std::runtime_error("Invalid value.");
}

}