#include "../expression.h"
#include "exprp_utility.h"
#include "exprparser.h"

#include <string_view>
#include <iostream>

namespace Starlane {

Expression::Expression(const std::string &expr) : exprStr(expr) {
	exprp_context_t *ctx = exprp_create(this);
	ast_node_t *root;
	int remaining = exprp_parse(ctx, &root);
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

}