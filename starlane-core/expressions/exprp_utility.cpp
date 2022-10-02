#include "exprp_utility.h"
#include "../expression.h"
#include "../valueparsers.h"

#include <stdlib.h>
#include <cassert>

namespace {
ast_node_t *CreateNode(system_t *ctx, ast_node_type_t type, range_t range) {
	auto expr = (Starlane::Expression *) ctx;
	auto node = expr->CreateNode();
	node->type = type;
	node->range = range;
    return node;
}
}

int system__get_next_char(system_t *ctx) {
	auto expr = (Starlane::Expression *) ctx;
	return expr->GetNextChar();
}

void *system__allocate_memory(system_t *ctx, size_t size) {
	auto expr = (Starlane::Expression *) ctx;
	return expr->ParserAllocate(size);
}

void *system__reallocate_memory(system_t *ctx, void *ptr, size_t size) {
	auto expr = (Starlane::Expression *) ctx;
	return expr->ParserReallocate(ptr, size);
}

void system__deallocate_memory(system_t *ctx, void *ptr) {
	auto expr = (Starlane::Expression *) ctx;
	expr->ParserFree(ptr);
}

ast_node_t *system__create_ast_node_terminal(system_t *ctx, ast_node_type_t type, range_t range) {
	auto node = CreateNode(ctx, type, range);
	if (type == AST_NODE_TYPE_INTEGER) {  // figure out the integer value of integers immediately
		auto expr = (Starlane::Expression *) ctx;
		auto txt = expr->GetNodeText(node);
		// int64_t can't have more than 19 digits anyways (20 for negative numbers), so any more than this is probably an error:
		assert(txt.length() < 32);
		// need to copy the text to a new buffer because string_view.data is not null-terminated, obviously.
		char *buf = (char *) alloca(txt.length() + 1);
		strncpy(buf, txt.data(), txt.length());
		buf[txt.length()] = '\0';
		node->intVal = Starlane::ParseInt(buf);
	}
	return node;
}

ast_node_t *system__create_ast_node_unary(system_t *ctx, ast_node_type_t type, range_t range, ast_node_t *child) {
	assert(child != nullptr);
	auto node = CreateNode(ctx, type, range);
	ast_node__append_child(node, child);
	return node;
}

ast_node_t *system__create_ast_node_binary(system_t *ctx, ast_node_type_t type, range_t range, ast_node_t *child1, ast_node_t *child2) {
	assert(child1 != nullptr);
	assert(child2 != nullptr);
	auto node = CreateNode(ctx, type, range);
	ast_node__append_child(node, child1);
	ast_node__append_child(node, child2);
    return node;
}

ast_node_t *system__create_ast_node_ternary(system_t *obj, ast_node_type_t type, range_t range, ast_node_t *node1, ast_node_t *node2, ast_node_t *node3) {
	assert(node1 != nullptr);
	assert(node2 != nullptr);
	assert(node3 != nullptr);
	ast_node_t *const node = CreateNode(obj, type, range);
	ast_node__append_child(node, node1);
	ast_node__append_child(node, node2);
	ast_node__append_child(node, node3);
	return node;
}

ast_node_t *system__create_ast_node_variadic(system_t *ctx, ast_node_type_t type, range_t range) {
	return CreateNode(ctx, type, range);
}

void ast_node__prepend_child(ast_node_t *obj, ast_node_t *node) {
    if (node == NULL) return; /* just ignored */
    if (node->parent != NULL) {
        if (node->sibling.prev != NULL) {
            node->sibling.prev->sibling.next = node->sibling.next;
        } else {
            node->parent->child.first = node->sibling.next;
        }
        if (node->sibling.next != NULL) {
            node->sibling.next->sibling.prev = node->sibling.prev;
        } else {
            node->parent->child.last = node->sibling.prev;
        }
        node->parent->arity--;
    }
    node->parent = obj;
    if (obj->child.first != NULL) {
        obj->child.first->sibling.prev = node;
        node->sibling.next = obj->child.first;
        node->sibling.prev = NULL;
        obj->child.first = node;
    } else {
        node->sibling.next = NULL;
        node->sibling.prev = NULL;
        obj->child.first = node;
        obj->child.last = node;
    }
    obj->arity++;
}

void ast_node__append_child(ast_node_t *obj, ast_node_t *node) {
    if (node == NULL) return; /* just ignored */
    if (node->parent != NULL) {
        if (node->sibling.prev != NULL) {
            node->sibling.prev->sibling.next = node->sibling.next;
        } else {
            node->parent->child.first = node->sibling.next;
        }
        if (node->sibling.next != NULL) {
            node->sibling.next->sibling.prev = node->sibling.prev;
        } else {
            node->parent->child.last = node->sibling.prev;
        }
        node->parent->arity--;
    }
    node->parent = obj;
    if (obj->child.last != NULL) {
        obj->child.last->sibling.next = node;
        node->sibling.prev = obj->child.last;
        node->sibling.next = NULL;
        obj->child.last = node;
    } else {
        node->sibling.prev = NULL;
        node->sibling.next = NULL;
        obj->child.last = node;
        obj->child.first = node;
    }
    obj->arity++;
}
