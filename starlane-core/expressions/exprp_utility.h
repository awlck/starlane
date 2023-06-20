#pragma once

#ifndef SLC_EXPRESSIONS_EXPRP_UTILITY_H
#define SLC_EXPRESSIONS_EXPRP_UTILITY_H

/* This is #include'd by the generated parser, which is in C, so we need to do this terribleness. */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#pragma GCC visibility push(internal)

#ifdef __cplusplus
extern "C" {
#endif

/* poor man's std::string_view */
typedef struct range_tag {
    size_t min; /* the start position (inclusive) */
    size_t max; /* the end position (exclusive) */
} range_t;

typedef struct size_t_array_tag {
    size_t m; /* the allocated length */
    size_t n; /* the actual length */
    size_t *p; /* the buffer */
} size_t_array_t;

/* Really, we want our auxiliary data to be the Starlane::Expression object, but we can't do that
 * in plain C. So we do the next best thing. (And hope we only ever use this type as pointers.) */
typedef void system_t;
typedef struct ast_node_tag ast_node_t;

typedef enum ast_node_type_tag {
    AST_NODE_TYPE_INVALID,
    AST_NODE_TYPE_IDENTIFIER,
    AST_NODE_TYPE_VARIABLE,
    AST_NODE_TYPE_INTEGER,
    AST_NODE_TYPE_STRING,
    AST_NODE_TYPE_OPERATOR_PLUS,
    AST_NODE_TYPE_OPERATOR_MINUS,
    AST_NODE_TYPE_OPERATOR_INV,
    AST_NODE_TYPE_OPERATOR_NOT,
    AST_NODE_TYPE_OPERATOR_ADD,
    AST_NODE_TYPE_OPERATOR_SUB,
    AST_NODE_TYPE_OPERATOR_CONCAT,
    AST_NODE_TYPE_OPERATOR_MUL,
    AST_NODE_TYPE_OPERATOR_DIV,
    AST_NODE_TYPE_OPERATOR_MOD,
    AST_NODE_TYPE_OPERATOR_POW,
    AST_NODE_TYPE_OPERATOR_AND,
    AST_NODE_TYPE_OPERATOR_OR,
    AST_NODE_TYPE_OPERATOR_EQ,
    AST_NODE_TYPE_OPERATOR_NE,
    AST_NODE_TYPE_OPERATOR_LT,
    AST_NODE_TYPE_OPERATOR_LE,
    AST_NODE_TYPE_OPERATOR_GT,
    AST_NODE_TYPE_OPERATOR_GE,
    AST_NODE_TYPE_FUNCCALL,
    AST_NODE_TYPE_ITEMFUNC,
    AST_NODE_TYPE_FUNCARGS,
    AST_NODE_TYPE_TEXTCONTENT,
    AST_NODE_TYPE_ERROR_SKIP,
    AST_NODE_TYPE_UNEXPECTED_TOKEN
} ast_node_type_t;

typedef enum syntax_error_tag {
    SYNTAX_ERROR_MISSING_QUOTEMARK,
    SYNTAX_ERROR_SPURIOUS_COMMA,
    SYNTAX_ERROR_UNKNOWN
} syntax_error_t;

struct ast_node_tag {
    ast_node_type_t type; /* the AST node type */
    range_t range; /* the byte range in the source text */
    size_t arity; /* the number of the child AST nodes */
    ast_node_t *parent; /* the parent AST node */
    struct ast_node_sibling_tag {
        ast_node_t *prev; /* the previous sibling AST node */
        ast_node_t *next; /* the next sibling AST node */
    } sibling;
    struct ast_node_child_tag {
        ast_node_t *first; /* the first child AST node */
        ast_node_t *last; /* the last child AST node */
    } child;
    system_t *system; /* the system that manages this AST node */
    struct ast_node_managed_tag {
        ast_node_t *prev; /* the previous AST node managed by the same system */
        ast_node_t *next; /* the next AST node managed by the same system */
    } managed;
    int64_t intVal; /* integer value of this node, if applicable. */
};


void system__initialize(system_t *ctx);
void system__finalize(system_t *ctx);

void *system__allocate_memory(system_t *ctx, size_t size);
void *system__reallocate_memory(system_t *ctx, void *ptr, size_t size);
void system__deallocate_memory(system_t *ctx, void *ptr);

int system__get_next_char(system_t *ctx);

void system__handle_syntax_error(system_t *obj, syntax_error_t error, range_t range);

ast_node_t *system__create_ast_node_terminal(system_t *obj, ast_node_type_t type, range_t range);
ast_node_t *system__create_ast_node_unary(system_t *obj, ast_node_type_t type, range_t range, ast_node_t *node1);
ast_node_t *system__create_ast_node_binary(system_t *obj, ast_node_type_t type, range_t range, ast_node_t *node1, ast_node_t *node2);
ast_node_t *system__create_ast_node_ternary(system_t *obj, ast_node_type_t type, range_t range, ast_node_t *node1, ast_node_t *node2, ast_node_t *node3);
ast_node_t *system__create_ast_node_variadic(system_t *obj, ast_node_type_t type, range_t range);

void ast_node__prepend_child(ast_node_t *parent, ast_node_t *node);
void ast_node__append_child(ast_node_t *parent, ast_node_t *node);

#ifndef NDEBUG
void ast_node__dump(system_t *ctx, ast_node_t *node, int level);
#endif // !NDEBUG

inline static range_t range__void(void) {
    const range_t obj = { 0, 0 };
    return obj;
}

inline static range_t range__new(size_t min, size_t max) {
    const range_t obj = { min, max };
    return obj;
}

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop

#endif  // !SLC_EXPRESSIONS_EXPRP_UTILITY_H