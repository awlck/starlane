#pragma once

#ifndef SLC_EXPRESSIONS_H
#define SLC_EXPRESSIONS_H

#include <string>

namespace Starlane {

struct Expression {
	// TODO: Parse expression.
	Expression (const std::string &expr) : exprStr(expr) {}

	std::string exprStr;

	// TODO
	bool EvaluateBool() const { return false; };
	int64_t EvaluateInt() const { return 0; };
	std::string EvaluateStr() const { return exprStr; };
};

}

#endif  // !SLC_EXPRESSIONS_H