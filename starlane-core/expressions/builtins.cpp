#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "../expression.h"
#include "exprp_utility.h"

#include <algorithm>
#include <locale>

static auto utf8Locale = std::locale("en_US.utf8");

namespace Starlane {
#define CHECK_ARGCOUNT(funcname, cnt) do { if (args->arity != (cnt)) throw std::runtime_error("Wrong number of arguments to built-in function " funcname ": expected " + std::to_string(cnt) + ", got " + std::to_string(args->arity)); } while (0)

// i hate every part of this, and i'm not even sure it makes any sense.
// will probably blow up in interesting ways later.
// (Text loaded from the game file will be utf-8, because that's what Developer outputs, but for
//  user input we just have to hope that the frontend doesn't provide something else.)
// (also codecvt is marked as deprecated but we'll nuke that bridge when we get to it.)

Expr::Value Expression::LCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("LCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return { Expr::ValueType::String, 0, std::to_string(theArg.Int) };
	else if (theArg.ty == Expr::ValueType::String) {  // ugh. I hate my life and all of this nonsense.
		std::wstring_convert<std::codecvt<char32_t, char, std::mbstate_t>, char32_t> converter;
		std::u32string s = converter.from_bytes(theArg.Str);
		std::transform(s.begin(), s.end(), s.begin(), [](char32_t x) { return std::toupper(x, utf8Locale); });
		theArg.Str = converter.to_bytes(s);
		return theArg;  // now transformed
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::UCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("UCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return { Expr::ValueType::String, 0, std::to_string(theArg.Int) };
	else if (theArg.ty == Expr::ValueType::String) {
		std::wstring_convert<std::codecvt<char32_t, char, std::mbstate_t>, char32_t> converter;
		std::u32string s = converter.from_bytes(theArg.Str);
		std::transform(s.begin(), s.end(), s.begin(), [](char32_t x) { return std::toupper(x, utf8Locale); });
		theArg.Str = converter.to_bytes(s);
		return theArg;  // now transformed
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::PCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("PCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return { Expr::ValueType::String, 0, std::to_string(theArg.Int) };
	else if (theArg.ty == Expr::ValueType::String) {
		std::wstring_convert<std::codecvt<char32_t, char, std::mbstate_t>, char32_t> converter;
		std::u32string s = converter.from_bytes(theArg.Str);
		bool beginningOfSentence = true;
		for (size_t pos = 0; pos < theArg.Str.length(); ++pos) {
			auto x = s[pos];
			if (std::isspace(x, utf8Locale)) continue;
			if (x == U'.' || x == U'!' || x == U'?') {
				beginningOfSentence = true;
				continue;
			}
			if (beginningOfSentence) {
				s[pos] = std::toupper(x, utf8Locale);
				beginningOfSentence = false;
				continue;
			}
			s[pos] = std::tolower(x, utf8Locale);
		}
		theArg.Str = converter.to_bytes(s);
		return theArg;  // now transformed
	} else throw std::runtime_error("Invalid value.");
}

}