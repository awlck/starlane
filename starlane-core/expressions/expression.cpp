#include "../expression.h"
#include "../game.h"
#include "../valueparsers.h"
#include "../gamecontent/variable.h"
#include "exprp_utility.h"
#include "exprparser.h"

#include <string_view>
#include <iostream>

namespace Starlane {

Expression::Expression(const std::string &expr) : exprStr(expr) {
	if (expr.empty()) return;
	int ws = 0;
	while (isspace(expr[ws])) ++ws;
	if (IsDigits(expr.c_str()+ws)) {
		constexType = ConstexprType::Int;
		constValInt = ParseInt(expr.c_str()+ws);
		return;
	}
	exprp_context_t *ctx = exprp_create(this);
	int remaining = exprp_parse(ctx, &rootNode);
	exprp_destroy(ctx);
}

Expression::~Expression() {
	for (auto &it : parserMemBlocks) {
		::operator delete(it.first);
	}
	for (ast_node_tag *p = firstNode; p != nullptr; p = p->managed.next) {
		delete p;
	}
}

std::string_view Expression::GetNodeText(const ast_node_tag *node) const {
	return std::string_view(exprStr).substr(node->range.min, node->range.max - node->range.min);
}

ast_node_tag *Expression::CreateNode() {
	ast_node_tag *node = new ast_node_tag;
	memset(node, 0, sizeof(ast_node_tag));
	node->system = this;
	if (lastNode == nullptr) {
		firstNode = node;
		lastNode = node;
	} else {
		node->managed.prev = lastNode;
		lastNode->managed.prev = node;
		lastNode = node;
	}
	return node;
}

std::string Expression::EvaluateStr() const {
	auto result = EvalAnyNode(rootNode);
	if (result.ty == Expr::ValueType::String) return result.Str;
	else if (result.ty == Expr::ValueType::Integer) return std::to_string(result.Int);
	throw std::runtime_error("Invalid expression result");
}

inline bool ValueAsBool(Expr::Value v) {
	switch (v.ty) {
	case Expr::ValueType::Integer:
		return !!v.Int;
	}
}

Expr::Value Expression::EvalAnyNode(const ast_node_tag *node) const {
	// Expr::Value myResult {Expr::ValueType::Invalid, 0, 0};
	switch (node->type) {
	case AST_NODE_TYPE_IDENTIFIER:
	case AST_NODE_TYPE_STRING:
		return { Expr::ValueType::String, 0, std::string(GetNodeText(node)) };
	case AST_NODE_TYPE_VARIABLE: {
		auto theVar = Game::Get()->GetVariable(std::string(GetNodeText(node)));
		auto theType = theVar->GetType();
		if (theType == Variable::Type::String)
			return { Expr::ValueType::String, 0, theVar->GetValue<std::string>() };
		else if (theType == Variable::Type::Int)
			return { Expr::ValueType::Integer, theVar->GetValue<int64_t>(), 0 };
	}
	case AST_NODE_TYPE_INTEGER:
		return { Expr::ValueType::Integer, node->intVal, 0 };
	case AST_NODE_TYPE_OPERATOR_PLUS: {
		auto theVal = EvalAnyNode(node->child.first);
		if (theVal.ty == Expr::ValueType::String && IsDigits(theVal.Str.c_str())) {
			return { Expr::ValueType::Integer, ParseInt(theVal.Str.c_str()), 0};
		}
		if (theVal.ty == Expr::ValueType::Integer) return theVal;
		throw std::runtime_error("Trying to determine the integer value of a non-integer.");
	}
	case AST_NODE_TYPE_OPERATOR_MINUS: {
		auto theVal = EvalAnyNode(node->child.first);
		if (theVal.ty == Expr::ValueType::String && IsDigits(theVal.Str.c_str())) {
			return { Expr::ValueType::Integer, -ParseInt(theVal.Str.c_str()), 0 };
		}
		if (theVal.ty == Expr::ValueType::Integer)
			return { Expr::ValueType::Integer, -theVal.Int, 0 };
		throw std::runtime_error("Trying to determine the negative value of a non-integer.");
	}
	case AST_NODE_TYPE_OPERATOR_NOT: {
		auto theVal = EvalAnyNode(node->child.first);
		return { Expr::ValueType::Integer, !bool(theVal), 0 };
	}
	case AST_NODE_TYPE_OPERATOR_ADD: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::String && rhs.ty == Expr::ValueType::String)
			return { Expr::ValueType::String, 0, lhs.Str + rhs.Str };
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return { Expr::ValueType::Integer, lhs.Int + rhs.Int, 0 };
		throw std::runtime_error("Tried to add disjointed types.");
	}
	case AST_NODE_TYPE_OPERATOR_SUB: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return { Expr::ValueType::Integer, lhs.Int - rhs.Int, 0 };
		throw std::runtime_error("Tried to subtract non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_CONCAT: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::String && rhs.ty == Expr::ValueType::String)
			return { Expr::ValueType::String, 0, lhs.Str + rhs.Str };
		if (lhs.ty == Expr::ValueType::String && rhs.ty == Expr::ValueType::Integer)
			return { Expr::ValueType::String, 0, lhs.Str + std::to_string(rhs.Int) };
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::String)
			return { Expr::ValueType::String, 0, std::to_string(lhs.Int) + rhs.Str };
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return { Expr::ValueType::String, 0, std::to_string(lhs.Int) + std::to_string(rhs.Int) };
		throw std::runtime_error("Tried to concatenate non-values.");
	}
	case AST_NODE_TYPE_OPERATOR_MUL: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return { Expr::ValueType::Integer, lhs.Int * rhs.Int, 0 };
		throw std::runtime_error("Tried to muliply non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_DIV: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer) {
			if (rhs.Int == 0) throw std::runtime_error("Tried to divide by zero.");
			return { Expr::ValueType::Integer, lhs.Int / rhs.Int, 0 };
		}
		throw std::runtime_error("Tried to divide non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_MOD: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer) {
			if (rhs.Int == 0) throw std::runtime_error("Tried to divide by zero.");
			return { Expr::ValueType::Integer, lhs.Int / rhs.Int, 0 };
		}
		throw std::runtime_error("Tried to modulo non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_POW: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return { Expr::ValueType::Integer, (int64_t)pow(lhs.Int, rhs.Int), 0};
		throw std::runtime_error("Tried to exponentiate non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_AND: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return { Expr::ValueType::Integer, lhs && rhs, 0 };
	}
	case AST_NODE_TYPE_OPERATOR_OR: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return { Expr::ValueType::Integer, lhs || rhs, 0 };
	}
	case AST_NODE_TYPE_OPERATOR_EQ: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return { Expr::ValueType::Integer, lhs == rhs, 0 };
	}
	case AST_NODE_TYPE_OPERATOR_NE: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return { Expr::ValueType::Integer, !(lhs == rhs), 0 };
	}
	case AST_NODE_TYPE_OPERATOR_LT: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return { Expr::ValueType::Integer, lhs < rhs, 0 };
	}
	case AST_NODE_TYPE_OPERATOR_LE: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return { Expr::ValueType::Integer, lhs < rhs || lhs == rhs, 0 };
	}
	case AST_NODE_TYPE_OPERATOR_GT: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return { Expr::ValueType::Integer, !(lhs < rhs) && !(lhs == rhs), 0};
	}
	case AST_NODE_TYPE_OPERATOR_GE: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return { Expr::ValueType::Integer, !(lhs < rhs), 0 };
	}
	case AST_NODE_TYPE_FUNCCALL: {
		auto function = EvalAnyNode(node->child.first);
		return EvalFunccall(function, node->child.last);
	}
	case AST_NODE_TYPE_ITEMFUNC: {
		auto object = EvalAnyNode(node->child.first);
		return EvalItemfunc(object, node->child.last);
	}
	case AST_NODE_TYPE_TEXTCONTENT: {
		std::string result;
		for (const auto *p = node->child.first; p != nullptr; p = p->sibling.next) {
			if (p->type == AST_NODE_TYPE_STRING) result += GetNodeText(p);
			else {
				auto v = EvalAnyNode(p);
				if (v.ty == Expr::ValueType::String) result += v.Str;
				else if (v.ty == Expr::ValueType::Integer) result += std::to_string(v.Int);
				else throw std::runtime_error("Invalid result type.");
			}
		}
		return { Expr::ValueType::String, 0, result };
	}
	default:
		throw std::runtime_error("Can't deal with this node type at this time: " + std::to_string(node->type));
	}

	return { Expr::ValueType::Invalid, 0, 0 };
}

}