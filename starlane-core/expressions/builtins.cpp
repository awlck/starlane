#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "../expression.h"
#include "exprp_utility.h"
#include "../starlane-core.h"


namespace Starlane {
#define CHECK_ARGCOUNT(funcname, cnt) do { if (args->arity != (cnt)) throw std::runtime_error("Wrong number of arguments to built-in function " funcname ": expected " + std::to_string(cnt) + ", got " + std::to_string(args->arity)); } while (0)

Expr::Value Expression::LCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("LCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return { Expr::ValueType::String, 0, std::to_string(theArg.Int) };
	else if (theArg.ty == Expr::ValueType::String) {  // ugh. I hate my life and all of this nonsense.
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

}