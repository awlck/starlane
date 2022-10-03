#include "exprp_utility.h"
#include "../expression.h"
#include "../valueparsers.h"

#include <stdlib.h>
#include <cassert>
#include <iostream>

namespace {

ast_node_t *CreateNode(system_t *ctx, ast_node_type_t type, range_t range) {
	auto expr = (Starlane::Expression *) ctx;
	auto node = expr->CreateNode();
	node->type = type;
	node->range = range;
    return node;
}

void FormatError(const std::string &fullText, range_t errRange, const char *errdesc) {
    std::cerr << "Syntax error parsing expression: " << errdesc << ":\n  ";
    std::string_view errtxt;
    bool ellipsisStart = false;
    bool ellipsisEnd = false;
    if (fullText.length() < 78) {
        errtxt = fullText;
    } else if (errRange.max - errRange.min >= 78) {
        int start = 0;
        int stop = 0;
        start = errRange.min > 5 ? errRange.min - 3 : 0;
        stop = errRange.max < fullText.length() - 6 ? errRange.max + 3 : fullText.length();
        ellipsisStart = errRange.min != 0;
        ellipsisEnd = errRange.max != fullText.length();
        errtxt = std::string_view(fullText).substr(start, stop - start);
        errRange.min -= start;
        errRange.max -= start;
    } else {
        // full text is more than 78 chars but the erroneous portion isn't
        int start = 0;
        int stop = 0;
        // add 10 characters of context on each side, if available (no matter the resulting length
        start = errRange.min > 10 ? errRange.min - 10 : 0;
        stop = errRange.max < fullText.length() - 10 ? errRange.max + 10 : errRange.max;
        while (stop - start < 78) {
            if (start > 0) --start;
            if (stop < fullText.length()) ++stop;
        }
        ellipsisStart = errRange.min != 0;
        ellipsisEnd = errRange.max != fullText.length();
        errtxt = std::string_view(fullText).substr(start, stop - start);
        errRange.min -= start;
        errRange.max -= start;
    }
    if (ellipsisStart)
        std::cerr << "...";
    std::cerr << errtxt;
    if (ellipsisEnd)
        std::cerr << "...";
    std::cerr << "\n  ";
    if (ellipsisStart)
        std::cerr << "   ";
    for (int i = 0; i < errRange.min; i++)
        std::cerr << ' ';
    for (int i = errRange.min; i < errRange.max; i++)
        std::cerr << '^';
    std::cerr << std::endl;
}

}  // anonymous namespace

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

void system__handle_syntax_error(system_t *obj, syntax_error_t error, range_t range) {
    auto expr = (Starlane::Expression *) obj;
    switch (error) {
    case SYNTAX_ERROR_MISSING_QUOTEMARK:
        FormatError(expr->exprStr, range, "missing quotation mark");
        //std::cerr << "Syntax error in expression: missing quotation mark: '" << std::string_view(expr->exprStr).substr(range.min, range.max - range.min)
        //    << "', continuing anyways.\n";
        return;
    case SYNTAX_ERROR_SPURIOUS_COMMA:
        FormatError(expr->exprStr, range, "spurious comma");
        //std::cerr << "Syntax error in expression: spurious comma: '" << std::string_view(expr->exprStr).substr(range.min, range.max - range.min)
        //    << "', continuing anyways.\n";
        return;
    default:
        std::cerr << "General syntax error parsing expression:\n  " << expr->exprStr << std::endl;
        throw std::runtime_error("Syntax error.");
    }
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

#ifndef NDEBUG
void ast_node__dump(system_t *ctx, ast_node_t *node, int level) {
    const char *type = "UNKNOWN";
    switch (node->type) {
    case AST_NODE_TYPE_INVALID:             type = "INVALID";             break;
    case AST_NODE_TYPE_IDENTIFIER:          type = "IDENTIFIER";          break;
    case AST_NODE_TYPE_VARIABLE:            type = "VARIABLE";            break;
    case AST_NODE_TYPE_INTEGER:             type = "INTEGER";             break;
    case AST_NODE_TYPE_STRING:              type = "STRING";              break;
    case AST_NODE_TYPE_OPERATOR_PLUS:       type = "OPERATOR_PLUS";       break;
    case AST_NODE_TYPE_OPERATOR_MINUS:      type = "OPERATOR_MINUS";      break;
    case AST_NODE_TYPE_OPERATOR_INV:        type = "OPERATOR_INV";        break;
    case AST_NODE_TYPE_OPERATOR_NOT:        type = "OPERATOR_NOT";        break;
    case AST_NODE_TYPE_OPERATOR_ADD:        type = "OPERATOR_ADD";        break;
    case AST_NODE_TYPE_OPERATOR_SUB:        type = "OPERATOR_SUB";        break;
    case AST_NODE_TYPE_OPERATOR_MUL:        type = "OPERATOR_MUL";        break;
    case AST_NODE_TYPE_OPERATOR_DIV:        type = "OPERATOR_DIV";        break;
    case AST_NODE_TYPE_OPERATOR_MOD:        type = "OPERATOR_MOD";        break;
    case AST_NODE_TYPE_OPERATOR_AND:        type = "OPERATOR_AND";        break;
    case AST_NODE_TYPE_OPERATOR_OR:         type = "OPERATOR_OR";         break;
    case AST_NODE_TYPE_OPERATOR_EQ:         type = "OPERATOR_EQ";         break;
    case AST_NODE_TYPE_OPERATOR_NE:         type = "OPERATOR_NE";         break;
    case AST_NODE_TYPE_OPERATOR_LT:         type = "OPERATOR_LT";         break;
    case AST_NODE_TYPE_OPERATOR_LE:         type = "OPERATOR_LE";         break;
    case AST_NODE_TYPE_OPERATOR_GT:         type = "OPERATOR_GT";         break;
    case AST_NODE_TYPE_OPERATOR_GE:         type = "OPERATOR_GE";         break;
    case AST_NODE_TYPE_OPERATOR_COND:       type = "OPERATOR_COND";       break;
    case AST_NODE_TYPE_OPERATOR_COMMA:      type = "OPERATOR_COMMA";      break;
    case AST_NODE_TYPE_OPERATOR_ASSIGN:     type = "OPERATOR_ASSIGN";     break;
    case AST_NODE_TYPE_FUNCCALL:            type = "FUNCCALL";            break;
    case AST_NODE_TYPE_ERROR_SKIP:          type = "ERROR_SKIP";          break;
    case AST_NODE_TYPE_UNEXPECTED_TOKEN:    type = "UNEXPECTED_TOKEN";    break;
    default: break;
    }
    if (node->arity > 0 ) {
        printf("%*s%s: arity = %zu\n", 2 * level, "", type, node->arity);
        for (ast_node_t *p = node->child.first; p != NULL; p = p->sibling.next) {
            ast_node__dump(ctx, p, level + 1);
        }
    } else {
        auto expr = (Starlane::Expression *) ctx;
        for (size_t i = 0; i < level; i++)
            std::cout << "  " ;
        std::cout << type << ": value = '" << expr->GetNodeText(node) << "'\n";

    }
}
#endif
