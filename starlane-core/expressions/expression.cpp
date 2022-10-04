#include "../expression.h"]
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

Expr::Value Expression::EvalAnyNode(ast_node_tag *node) const {
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
	}

	return { Expr::ValueType::Invalid, 0, 0 };
}

}