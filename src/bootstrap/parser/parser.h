#ifndef VITTE_PARSER_PARSER_H
#define VITTE_PARSER_PARSER_H

/*
 * Vitte Compiler
 * src/parser/parser.h
 *
 * Public parser and AST contract.
 *
 * Synchronized with parser.c.
 *
 * Design:
 *   - ISO C17;
 *   - lexer.h is the token contract;
 *   - deterministic AST;
 *   - explicit parser lifecycle;
 *   - stable node identifiers;
 *   - source spans preserved;
 *   - bounded AST/diagnostic/recursion resources;
 *   - structured diagnostics;
 *   - AST validation;
 *   - deterministic fingerprinting;
 *   - C/C++ compatible API.
 */

#include "../lexer/lexer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* API version                                                               */
/* ========================================================================= */

#define VITTE_PARSER_API_VERSION_MAJOR 2u
#define VITTE_PARSER_API_VERSION_MINOR 0u
#define VITTE_PARSER_API_VERSION_PATCH 0u

/* ========================================================================= */
/* Constants                                                                 */
/* ========================================================================= */

#define VITTE_PARSER_MAGIC \
    UINT64_C(0x5649545450415253)

#define VITTE_PARSER_DEAD_MAGIC \
    UINT64_C(0x4445414450415253)

#define VITTE_PARSER_FNV_OFFSET \
    UINT64_C(14695981039346656037)

#define VITTE_PARSER_FNV_PRIME \
    UINT64_C(1099511628211)

#define VITTE_PARSER_DEFAULT_INITIAL_CAPACITY \
    ((size_t)64u)

#define VITTE_PARSER_DEFAULT_MAX_NODES \
    ((size_t)16777216u)

#define VITTE_PARSER_DEFAULT_MAX_DIAGNOSTICS \
    ((size_t)4096u)

#define VITTE_PARSER_DEFAULT_MAX_RECURSION_DEPTH \
    ((size_t)1024u)

#define VITTE_PARSER_DEFAULT_MAX_RECOVERY_TOKENS \
    ((size_t)4096u)

#define VITTE_PARSER_NO_TOKEN SIZE_MAX

/* ========================================================================= */
/* AST identifiers                                                           */
/* ========================================================================= */

typedef uint64_t vitte_ast_node_id_t;

#define VITTE_AST_INVALID_ID UINT64_C(0)

/* ========================================================================= */
/* Parser errors                                                             */
/* ========================================================================= */

typedef enum vitte_parser_error {
    VITTE_PARSER_ERROR_NONE = 0,

    VITTE_PARSER_ERROR_INVALID_ARGUMENT,
    VITTE_PARSER_ERROR_INVALID_CONTEXT,
    VITTE_PARSER_ERROR_INVALID_STATE,

    VITTE_PARSER_ERROR_INVALID_TOKEN,
    VITTE_PARSER_ERROR_INVALID_NODE,

    VITTE_PARSER_ERROR_EXPECTED_TOKEN,
    VITTE_PARSER_ERROR_EXPECTED_IDENTIFIER,
    VITTE_PARSER_ERROR_EXPECTED_EXPRESSION,
    VITTE_PARSER_ERROR_EXPECTED_TYPE,
    VITTE_PARSER_ERROR_EXPECTED_STATEMENT,
    VITTE_PARSER_ERROR_EXPECTED_DECLARATION,

    VITTE_PARSER_ERROR_UNEXPECTED_TOKEN,

    VITTE_PARSER_ERROR_NODE_LIMIT,
    VITTE_PARSER_ERROR_DIAGNOSTIC_LIMIT,
    VITTE_PARSER_ERROR_RECURSION_LIMIT,

    VITTE_PARSER_ERROR_OVERFLOW,
    VITTE_PARSER_ERROR_OUT_OF_MEMORY,

    VITTE_PARSER_ERROR_VALIDATION,
    VITTE_PARSER_ERROR_CORRUPTION,
    VITTE_PARSER_ERROR_UNSUPPORTED,
    VITTE_PARSER_ERROR_INTERNAL,

    VITTE_PARSER_ERROR_COUNT
} vitte_parser_error_t;

/* ========================================================================= */
/* Parser states                                                             */
/* ========================================================================= */

typedef enum vitte_parser_state {
    VITTE_PARSER_STATE_INVALID = 0,
    VITTE_PARSER_STATE_READY,
    VITTE_PARSER_STATE_PARSING,
    VITTE_PARSER_STATE_DONE,
    VITTE_PARSER_STATE_FAILED,
    VITTE_PARSER_STATE_DESTROYED,
    VITTE_PARSER_STATE_COUNT
} vitte_parser_state_t;

/* ========================================================================= */
/* Diagnostic kinds                                                          */
/* ========================================================================= */

typedef enum vitte_parser_diagnostic_kind {
    VITTE_PARSER_DIAGNOSTIC_INVALID = 0,
    VITTE_PARSER_DIAGNOSTIC_NOTE,
    VITTE_PARSER_DIAGNOSTIC_HELP,
    VITTE_PARSER_DIAGNOSTIC_WARNING,
    VITTE_PARSER_DIAGNOSTIC_ERROR,
    VITTE_PARSER_DIAGNOSTIC_COUNT
} vitte_parser_diagnostic_kind_t;

/* ========================================================================= */
/* Source span                                                               */
/* ========================================================================= */

typedef struct vitte_parser_span {
    uint32_t file_id;

    size_t begin;
    size_t end;

    size_t line;
    size_t column;

    bool valid;
} vitte_parser_span_t;

/* ========================================================================= */
/* AST node kinds                                                            */
/* ========================================================================= */

typedef enum vitte_ast_node_kind {
    VITTE_AST_NODE_INVALID = 0,

    /* Translation unit / names. */
    VITTE_AST_NODE_TRANSLATION_UNIT,
    VITTE_AST_NODE_IDENTIFIER,
    VITTE_AST_NODE_PATH,

    /* Top-level declarations. */
    VITTE_AST_NODE_SPACE_DECL,
    VITTE_AST_NODE_USE_DECL,

    VITTE_AST_NODE_CONST_DECL,
    VITTE_AST_NODE_STATIC_DECL,

    VITTE_AST_NODE_TYPE_DECL,
    VITTE_AST_NODE_OPAQUE_DECL,

    VITTE_AST_NODE_FORM_DECL,
    VITTE_AST_NODE_FIELD_DECL,

    VITTE_AST_NODE_PICK_DECL,
    VITTE_AST_NODE_VARIANT_DECL,

    VITTE_AST_NODE_TRAIT_DECL,
    VITTE_AST_NODE_IMPL_DECL,

    VITTE_AST_NODE_PROC_DECL,
    VITTE_AST_NODE_EXTERN_PROC_DECL,

    VITTE_AST_NODE_PARAMETER,
    VITTE_AST_NODE_GENERIC_PARAM,

    VITTE_AST_NODE_WHERE_CLAUSE,
    VITTE_AST_NODE_WHERE_PREDICATE,

    /* A syntactic pattern.  Simple bindings keep their existing leaf node;
       this wrapper is used for tuple/list/constructor patterns. */
    VITTE_AST_NODE_PATTERN,

    VITTE_AST_NODE_MACRO_DECL,
    VITTE_AST_NODE_TEST_DECL,

    /* Statements / blocks. */
    VITTE_AST_NODE_BLOCK,

    VITTE_AST_NODE_LET_STMT,
    VITTE_AST_NODE_RETURN_STMT,
    VITTE_AST_NODE_DEFER_STMT,

    VITTE_AST_NODE_IF_STMT,
    VITTE_AST_NODE_IF_EXPR,
    VITTE_AST_NODE_BLOCK_EXPR,
    VITTE_AST_NODE_WHILE_STMT,
    VITTE_AST_NODE_LOOP_STMT,
    VITTE_AST_NODE_FOR_STMT,

    VITTE_AST_NODE_BREAK_STMT,
    VITTE_AST_NODE_CONTINUE_STMT,

    VITTE_AST_NODE_MATCH_STMT,
    VITTE_AST_NODE_MATCH_ARM,

    VITTE_AST_NODE_UNSAFE_BLOCK,
    VITTE_AST_NODE_ASM_STMT,
    VITTE_AST_NODE_ASSERT_STMT,

    VITTE_AST_NODE_EXPR_STMT,

    /* Types. */
    VITTE_AST_NODE_TYPE_EXPR,
    VITTE_AST_NODE_POINTER_TYPE,
    VITTE_AST_NODE_REFERENCE_TYPE,
    VITTE_AST_NODE_ARRAY_TYPE,

    /* Literals. */
    VITTE_AST_NODE_INTEGER_LITERAL,
    VITTE_AST_NODE_FLOAT_LITERAL,
    VITTE_AST_NODE_STRING_LITERAL,
    VITTE_AST_NODE_CHARACTER_LITERAL,
    VITTE_AST_NODE_BOOL_LITERAL,
    VITTE_AST_NODE_NULL_LITERAL,

    /* Expressions. */
    VITTE_AST_NODE_SELF_EXPR,
    VITTE_AST_NODE_GROUP_EXPR,
    VITTE_AST_NODE_ARRAY_EXPR,
    VITTE_AST_NODE_FORM_EXPR,
    VITTE_AST_NODE_FIELD_INIT,

    VITTE_AST_NODE_UNARY_EXPR,
    VITTE_AST_NODE_BINARY_EXPR,
    VITTE_AST_NODE_CAST_EXPR,
    VITTE_AST_NODE_ASSIGN_EXPR,

    VITTE_AST_NODE_CALL_EXPR,
    VITTE_AST_NODE_INDEX_EXPR,
    VITTE_AST_NODE_MEMBER_EXPR,

    VITTE_AST_NODE_AWAIT_EXPR,
    VITTE_AST_NODE_TRY_EXPR,
    VITTE_AST_NODE_SIZEOF_EXPR,
    VITTE_AST_NODE_ALIGNOF_EXPR,

    /* Nullable/optional type prefix (`?T`) used by native compiler modules. */
    VITTE_AST_NODE_OPTIONAL_TYPE,

    VITTE_AST_NODE_COUNT
} vitte_ast_node_kind_t;

/* ========================================================================= */
/* AST flags                                                                 */
/* ========================================================================= */

typedef uint32_t vitte_ast_flags_t;

#define VITTE_AST_FLAG_NONE \
    UINT32_C(0)

#define VITTE_AST_FLAG_PUBLIC \
    (UINT32_C(1) << 0u)

#define VITTE_AST_FLAG_MUTABLE \
    (UINT32_C(1) << 1u)

#define VITTE_AST_FLAG_EXTERN \
    (UINT32_C(1) << 2u)

#define VITTE_AST_FLAG_ASYNC \
    (UINT32_C(1) << 3u)

#define VITTE_AST_FLAG_UNSAFE \
    (UINT32_C(1) << 4u)

#define VITTE_AST_FLAG_COMPTIME \
    (UINT32_C(1) << 5u)

/*
 * Reserved bits are intentionally left available for future declaration
 * modifiers without changing the node structure.
 */
#define VITTE_AST_FLAG_CONTRACT_REQUIRES \
    (UINT32_C(1) << 6u)

#define VITTE_AST_FLAG_CONTRACT_ENSURES \
    (UINT32_C(1) << 7u)

#define VITTE_AST_FLAG_GLOB_IMPORT \
    (UINT32_C(1) << 8u)

#define VITTE_AST_FLAG_HAS_ATTRIBUTES \
    (UINT32_C(1) << 9u)

#define VITTE_AST_FLAG_PATTERN_MUTABLE \
    (UINT32_C(1) << 10u)

#define VITTE_AST_FLAG_PATTERN_REFERENCE \
    (UINT32_C(1) << 11u)

/* Pattern wrappers that carry a constructor or struct head.  The head is
 * syntax, not a local binding; semantic binding skips it when these flags
 * are present. */
#define VITTE_AST_FLAG_PATTERN_HEAD \
    (UINT32_C(1) << 12u)

#define VITTE_AST_FLAG_PATTERN_OR \
    (UINT32_C(1) << 13u)

#define VITTE_AST_FLAG_PATTERN_RANGE \
    (UINT32_C(1) << 14u)

/* TYPE_EXPR nodes with this flag carry a procedure result type as their last
 * child. Procedure type parameters occupy the preceding children. */
#define VITTE_AST_FLAG_FUNCTION_TYPE_HAS_RETURN \
    (UINT32_C(1) << 15u)

#define VITTE_AST_FLAG_TYPE_ALIAS \
    (UINT32_C(1) << 16u)

#define VITTE_AST_FLAG_TYPE_QUERY_TYPE_OPERAND \
    (UINT32_C(1) << 17u)

/* Generic parameter declared with a compile-time value type (`const N: T`). */
#define VITTE_AST_FLAG_CONST_GENERIC \
    (UINT32_C(1) << 18u)

/* ========================================================================= */
/* AST node                                                                  */
/* ========================================================================= */

/* Attribute records retain ranges into the parser's borrowed token stream.
 * Name ranges include `::` separators; argument ranges contain the original
 * positional or `name = value` tokens between parentheses. */
typedef struct vitte_ast_attribute {
    size_t token_start;
    size_t token_count;
    size_t name_start;
    size_t name_count;
    size_t argument_start;
    size_t argument_count;
    bool inner;
} vitte_ast_attribute_t;

typedef struct vitte_ast_node {
    /*
     * Stable 1-based identifier.
     * Zero is VITTE_AST_INVALID_ID.
     */
    vitte_ast_node_id_t id;

    vitte_ast_node_kind_t kind;

    /*
     * Modifier/property flags.
     */
    vitte_ast_flags_t flags;

    /*
     * Operator associated with unary/binary/assignment expressions.
     * VITTE_TOKEN_INVALID for nodes without an operator.
     */
    vitte_token_kind_t operator_kind;

    /*
     * Index into parser->tokens for token-backed leaves/operators.
     * VITTE_PARSER_NO_TOKEN means no directly associated token.
     */
    size_t token_index;

    vitte_parser_span_t span;

    /*
     * Generic ordered AST children.
     *
     * Interpretation depends on node kind, while ownership remains
     * exclusively with the parser context.
     */
    vitte_ast_node_id_t *children;
    size_t child_count;
    size_t child_capacity;

    /* Slice into parser->attributes; empty when attribute_count is zero. */
    size_t attribute_start;
    size_t attribute_count;
} vitte_ast_node_t;

/* ========================================================================= */
/* Diagnostics                                                               */
/* ========================================================================= */

typedef struct vitte_parser_diagnostic {
    vitte_parser_diagnostic_kind_t kind;

    vitte_parser_error_t error;

    vitte_parser_span_t span;

    /*
     * Token expectation information.
     *
     * VITTE_TOKEN_INVALID means the field is not applicable.
     */
    vitte_token_kind_t expected;
    vitte_token_kind_t found;
} vitte_parser_diagnostic_t;

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

typedef struct vitte_parser_stats {
    uint64_t runs;
    uint64_t failed_runs;

    uint64_t tokens_consumed;

    uint64_t nodes_created;
    uint64_t declarations;

    uint64_t diagnostics;
    uint64_t errors;
    uint64_t recoveries;

    uint64_t max_recursion_depth;

    uint64_t validation_runs;
    uint64_t validation_failures;

    uint64_t hash_runs;

    uint64_t allocations;
    uint64_t reallocations;
    uint64_t allocation_failures;
} vitte_parser_stats_t;

/* ========================================================================= */
/* Parser context                                                            */
/* ========================================================================= */

typedef struct vitte_parser {
    uint64_t magic;

    vitte_parser_state_t state;
    vitte_parser_error_t last_error;

    /*
     * Borrowed immutable token stream.
     *
     * The caller must keep this memory alive for the entire parser
     * lifetime or until vitte_parser_reset().
     */
    const vitte_token_t *tokens;
    size_t token_count;
    size_t position;

    /*
     * Parser-owned AST storage.
     */
    vitte_ast_node_t *nodes;
    size_t node_count;
    size_t node_capacity;

    /* Parser-owned attribute records; their ranges reference borrowed tokens. */
    vitte_ast_attribute_t *attributes;
    size_t attribute_count;
    size_t attribute_capacity;

    /*
     * Parser-owned diagnostics.
     */
    vitte_parser_diagnostic_t *diagnostics;
    size_t diagnostic_count;
    size_t diagnostic_capacity;

    /*
     * Root AST node.
     */
    vitte_ast_node_id_t root;

    /*
     * Resource limits.
     */
    size_t max_nodes;
    size_t max_diagnostics;
    size_t max_recursion_depth;
    /* Maximum tokens consumed by one recovery pass before it gives up. */
    size_t max_recovery_tokens;

    /*
     * Current recursive-descent depth.
     */
    size_t recursion_depth;

    /*
     * Suppress cascading syntax errors until synchronization resumes at a
     * separator (`;`, `,`) or an enclosing delimiter (`)`, `]`, `}`).
     */
    bool recovering;

    vitte_parser_stats_t stats;

    /*
     * Incremented on reset so external tooling can detect stale views.
     */
    uint64_t generation;
} vitte_parser_t;

/* ========================================================================= */
/* Names                                                                     */
/* ========================================================================= */

const char *
vitte_parser_error_name(
    vitte_parser_error_t error);

const char *
vitte_parser_state_name(
    vitte_parser_state_t state);

const char *
vitte_ast_node_kind_name(
    vitte_ast_node_kind_t kind);

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

bool
vitte_parser_init(
    vitte_parser_t *parser,
    const vitte_token_t *tokens,
    size_t token_count);

bool
vitte_parser_reset(
    vitte_parser_t *parser,
    const vitte_token_t *tokens,
    size_t token_count);

void
vitte_parser_destroy(
    vitte_parser_t *parser);

bool
vitte_parser_is_valid(
    const vitte_parser_t *parser);

/* ========================================================================= */
/* Parsing                                                                   */
/* ========================================================================= */

bool
vitte_parser_run(
    vitte_parser_t *parser);

/* ========================================================================= */
/* AST access                                                                */
/* ========================================================================= */

const vitte_ast_node_t *
vitte_parser_get_node(
    const vitte_parser_t *parser,
    vitte_ast_node_id_t id);

size_t
vitte_parser_attribute_count(
    const vitte_parser_t *parser,
    vitte_ast_node_id_t node_id);

const vitte_ast_attribute_t *
vitte_parser_get_attribute(
    const vitte_parser_t *parser,
    vitte_ast_node_id_t node_id,
    size_t index);

vitte_ast_node_t *
vitte_parser_get_node_mut(
    vitte_parser_t *parser,
    vitte_ast_node_id_t id);

vitte_ast_node_id_t
vitte_parser_root(
    const vitte_parser_t *parser);

/* ========================================================================= */
/* Diagnostic access                                                         */
/* ========================================================================= */

const vitte_parser_diagnostic_t *
vitte_parser_diagnostic_at(
    const vitte_parser_t *parser,
    size_t index);

size_t
vitte_parser_diagnostic_count(
    const vitte_parser_t *parser);

/* ========================================================================= */
/* Validation / fingerprint                                                  */
/* ========================================================================= */

bool
vitte_parser_validate(
    vitte_parser_t *parser);

uint64_t
vitte_parser_fingerprint(
    vitte_parser_t *parser);

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_parser_stats_t
vitte_parser_stats(
    const vitte_parser_t *parser);

/* ========================================================================= */
/* Limits                                                                    */
/* ========================================================================= */

bool
vitte_parser_set_limits(
    vitte_parser_t *parser,
    size_t max_nodes,
    size_t max_diagnostics,
    size_t max_recursion_depth,
    size_t max_recovery_tokens);

/* ========================================================================= */
/* Translation-unit anchor                                                   */
/* ========================================================================= */

void
vitte_parser_translation_unit_anchor(void);

/* ========================================================================= */
/* Inline span helpers                                                       */
/* ========================================================================= */

static inline vitte_parser_span_t
vitte_parser_span_invalid(void)
{
    vitte_parser_span_t span;

    span.file_id = UINT32_C(0);
    span.begin = 0u;
    span.end = 0u;
    span.line = 0u;
    span.column = 0u;
    span.valid = false;

    return span;
}

static inline vitte_parser_span_t
vitte_parser_span_make(
    uint32_t file_id,
    size_t begin,
    size_t end,
    size_t line,
    size_t column)
{
    vitte_parser_span_t span;

    span.file_id = file_id;
    span.begin = begin;
    span.end = end;
    span.line = line;
    span.column = column;
    span.valid = end >= begin;

    return span;
}

static inline size_t
vitte_parser_span_length(
    vitte_parser_span_t span)
{
    if (!span.valid ||
        span.end < span.begin) {
        return 0u;
    }

    return span.end - span.begin;
}

static inline bool
vitte_parser_span_is_empty(
    vitte_parser_span_t span)
{
    return !span.valid ||
           span.begin == span.end;
}

static inline bool
vitte_parser_span_contains_offset(
    vitte_parser_span_t span,
    size_t offset)
{
    return span.valid &&
           offset >= span.begin &&
           offset < span.end;
}

/* ========================================================================= */
/* Inline node helpers                                                       */
/* ========================================================================= */

static inline bool
vitte_ast_node_id_is_valid(
    vitte_ast_node_id_t id)
{
    return id != VITTE_AST_INVALID_ID;
}

static inline bool
vitte_ast_node_kind_is_declaration(
    vitte_ast_node_kind_t kind)
{
    switch (kind) {
        case VITTE_AST_NODE_SPACE_DECL:
        case VITTE_AST_NODE_USE_DECL:
        case VITTE_AST_NODE_CONST_DECL:
        case VITTE_AST_NODE_STATIC_DECL:
        case VITTE_AST_NODE_TYPE_DECL:
        case VITTE_AST_NODE_OPAQUE_DECL:
        case VITTE_AST_NODE_FORM_DECL:
        case VITTE_AST_NODE_FIELD_DECL:
        case VITTE_AST_NODE_PICK_DECL:
        case VITTE_AST_NODE_VARIANT_DECL:
        case VITTE_AST_NODE_TRAIT_DECL:
        case VITTE_AST_NODE_IMPL_DECL:
        case VITTE_AST_NODE_PROC_DECL:
        case VITTE_AST_NODE_EXTERN_PROC_DECL:
        case VITTE_AST_NODE_PARAMETER:
        case VITTE_AST_NODE_GENERIC_PARAM:
        case VITTE_AST_NODE_MACRO_DECL:
        case VITTE_AST_NODE_TEST_DECL:
            return true;

        default:
            return false;
    }
}

static inline bool
vitte_ast_node_kind_is_statement(
    vitte_ast_node_kind_t kind)
{
    switch (kind) {
        case VITTE_AST_NODE_BLOCK:
        case VITTE_AST_NODE_LET_STMT:
        case VITTE_AST_NODE_RETURN_STMT:
        case VITTE_AST_NODE_DEFER_STMT:
        case VITTE_AST_NODE_IF_STMT:
        case VITTE_AST_NODE_WHILE_STMT:
        case VITTE_AST_NODE_LOOP_STMT:
        case VITTE_AST_NODE_FOR_STMT:
        case VITTE_AST_NODE_BREAK_STMT:
        case VITTE_AST_NODE_CONTINUE_STMT:
        case VITTE_AST_NODE_MATCH_STMT:
        case VITTE_AST_NODE_MATCH_ARM:
        case VITTE_AST_NODE_UNSAFE_BLOCK:
        case VITTE_AST_NODE_ASM_STMT:
        case VITTE_AST_NODE_ASSERT_STMT:
        case VITTE_AST_NODE_EXPR_STMT:
            return true;

        default:
            return false;
    }
}

static inline bool
vitte_ast_node_kind_is_type(
    vitte_ast_node_kind_t kind)
{
    switch (kind) {
        case VITTE_AST_NODE_TYPE_EXPR:
        case VITTE_AST_NODE_POINTER_TYPE:
        case VITTE_AST_NODE_REFERENCE_TYPE:
        case VITTE_AST_NODE_ARRAY_TYPE:
        case VITTE_AST_NODE_OPTIONAL_TYPE:
            return true;

        default:
            return false;
    }
}

static inline bool
vitte_ast_node_kind_is_literal(
    vitte_ast_node_kind_t kind)
{
    switch (kind) {
        case VITTE_AST_NODE_INTEGER_LITERAL:
        case VITTE_AST_NODE_FLOAT_LITERAL:
        case VITTE_AST_NODE_STRING_LITERAL:
        case VITTE_AST_NODE_CHARACTER_LITERAL:
        case VITTE_AST_NODE_BOOL_LITERAL:
        case VITTE_AST_NODE_NULL_LITERAL:
            return true;

        default:
            return false;
    }
}

static inline bool
vitte_ast_node_kind_is_expression(
    vitte_ast_node_kind_t kind)
{
    if (vitte_ast_node_kind_is_literal(kind)) {
        return true;
    }

    switch (kind) {
        case VITTE_AST_NODE_IDENTIFIER:
        case VITTE_AST_NODE_PATH:
        case VITTE_AST_NODE_SELF_EXPR:
        case VITTE_AST_NODE_GROUP_EXPR:
        case VITTE_AST_NODE_IF_EXPR:
        case VITTE_AST_NODE_BLOCK_EXPR:
        case VITTE_AST_NODE_UNARY_EXPR:
        case VITTE_AST_NODE_BINARY_EXPR:
        case VITTE_AST_NODE_CAST_EXPR:
        case VITTE_AST_NODE_ASSIGN_EXPR:
        case VITTE_AST_NODE_CALL_EXPR:
        case VITTE_AST_NODE_INDEX_EXPR:
        case VITTE_AST_NODE_MEMBER_EXPR:
        case VITTE_AST_NODE_FORM_EXPR:
        case VITTE_AST_NODE_AWAIT_EXPR:
        case VITTE_AST_NODE_TRY_EXPR:
        case VITTE_AST_NODE_SIZEOF_EXPR:
        case VITTE_AST_NODE_ALIGNOF_EXPR:
            return true;

        default:
            return false;
    }
}

static inline bool
vitte_ast_node_has_flag(
    const vitte_ast_node_t *node,
    vitte_ast_flags_t flag)
{
    return node != NULL &&
           (node->flags & flag) != 0u;
}

static inline bool
vitte_ast_node_is_public(
    const vitte_ast_node_t *node)
{
    return vitte_ast_node_has_flag(
        node,
        VITTE_AST_FLAG_PUBLIC);
}

static inline bool
vitte_ast_node_is_mutable(
    const vitte_ast_node_t *node)
{
    return vitte_ast_node_has_flag(
        node,
        VITTE_AST_FLAG_MUTABLE);
}

static inline bool
vitte_ast_node_is_external(
    const vitte_ast_node_t *node)
{
    return vitte_ast_node_has_flag(
        node,
        VITTE_AST_FLAG_EXTERN);
}

static inline bool
vitte_ast_node_is_async(
    const vitte_ast_node_t *node)
{
    return vitte_ast_node_has_flag(
        node,
        VITTE_AST_FLAG_ASYNC);
}

static inline bool
vitte_ast_node_is_unsafe(
    const vitte_ast_node_t *node)
{
    return vitte_ast_node_has_flag(
        node,
        VITTE_AST_FLAG_UNSAFE);
}

static inline bool
vitte_ast_node_is_comptime(
    const vitte_ast_node_t *node)
{
    return vitte_ast_node_has_flag(
        node,
        VITTE_AST_FLAG_COMPTIME);
}

static inline bool
vitte_ast_node_has_token(
    const vitte_ast_node_t *node)
{
    return node != NULL &&
           node->token_index !=
               VITTE_PARSER_NO_TOKEN;
}

static inline size_t
vitte_ast_node_child_count(
    const vitte_ast_node_t *node)
{
    return node != NULL
        ? node->child_count
        : 0u;
}

static inline vitte_ast_node_id_t
vitte_ast_node_child_at(
    const vitte_ast_node_t *node,
    size_t index)
{
    if (node == NULL ||
        index >= node->child_count ||
        node->children == NULL) {
        return VITTE_AST_INVALID_ID;
    }

    return node->children[index];
}

/* ========================================================================= */
/* Inline parser helpers                                                     */
/* ========================================================================= */

static inline bool
vitte_parser_is_ready(
    const vitte_parser_t *parser)
{
    return parser != NULL &&
           parser->magic == VITTE_PARSER_MAGIC &&
           parser->state ==
               VITTE_PARSER_STATE_READY;
}

static inline bool
vitte_parser_is_parsing(
    const vitte_parser_t *parser)
{
    return parser != NULL &&
           parser->magic == VITTE_PARSER_MAGIC &&
           parser->state ==
               VITTE_PARSER_STATE_PARSING;
}

static inline bool
vitte_parser_is_done(
    const vitte_parser_t *parser)
{
    return parser != NULL &&
           parser->magic == VITTE_PARSER_MAGIC &&
           parser->state ==
               VITTE_PARSER_STATE_DONE;
}

static inline bool
vitte_parser_has_failed(
    const vitte_parser_t *parser)
{
    return parser != NULL &&
           parser->magic == VITTE_PARSER_MAGIC &&
           parser->state ==
               VITTE_PARSER_STATE_FAILED;
}

static inline vitte_parser_error_t
vitte_parser_last_error(
    const vitte_parser_t *parser)
{
    if (parser == NULL ||
        parser->magic != VITTE_PARSER_MAGIC) {
        return VITTE_PARSER_ERROR_INVALID_CONTEXT;
    }

    return parser->last_error;
}

static inline size_t
vitte_parser_node_count(
    const vitte_parser_t *parser)
{
    if (parser == NULL ||
        parser->magic != VITTE_PARSER_MAGIC) {
        return 0u;
    }

    return parser->node_count;
}

static inline size_t
vitte_parser_token_count(
    const vitte_parser_t *parser)
{
    if (parser == NULL ||
        parser->magic != VITTE_PARSER_MAGIC) {
        return 0u;
    }

    return parser->token_count;
}

static inline size_t
vitte_parser_position(
    const vitte_parser_t *parser)
{
    if (parser == NULL ||
        parser->magic != VITTE_PARSER_MAGIC) {
        return 0u;
    }

    return parser->position;
}

static inline uint64_t
vitte_parser_generation(
    const vitte_parser_t *parser)
{
    if (parser == NULL ||
        parser->magic != VITTE_PARSER_MAGIC) {
        return UINT64_C(0);
    }

    return parser->generation;
}

static inline bool
vitte_parser_has_diagnostics(
    const vitte_parser_t *parser)
{
    return parser != NULL &&
           parser->magic == VITTE_PARSER_MAGIC &&
           parser->diagnostic_count != 0u;
}

static inline bool
vitte_parser_has_root(
    const vitte_parser_t *parser)
{
    return parser != NULL &&
           parser->magic == VITTE_PARSER_MAGIC &&
           parser->root !=
               VITTE_AST_INVALID_ID;
}

static inline const vitte_token_t *
vitte_parser_token_at(
    const vitte_parser_t *parser,
    size_t index)
{
    if (parser == NULL ||
        parser->magic != VITTE_PARSER_MAGIC ||
        parser->tokens == NULL ||
        index >= parser->token_count) {
        return NULL;
    }

    return &parser->tokens[index];
}

static inline const vitte_token_t *
vitte_parser_node_token(
    const vitte_parser_t *parser,
    const vitte_ast_node_t *node)
{
    if (parser == NULL ||
        node == NULL ||
        parser->magic != VITTE_PARSER_MAGIC ||
        node->token_index ==
            VITTE_PARSER_NO_TOKEN ||
        node->token_index >=
            parser->token_count ||
        parser->tokens == NULL) {
        return NULL;
    }

    return &parser->tokens[node->token_index];
}

/* ========================================================================= */
/* ABI / compile-time checks                                                 */
/* ========================================================================= */

#if defined(__cplusplus)

static_assert(
    sizeof(vitte_ast_node_id_t) == 8u,
    "vitte_ast_node_id_t must be 64-bit");

static_assert(
    VITTE_AST_INVALID_ID == UINT64_C(0),
    "AST invalid identifier must be zero");

static_assert(
    VITTE_PARSER_API_VERSION_MAJOR == 2u,
    "unexpected parser API major version");

#else

_Static_assert(
    sizeof(vitte_ast_node_id_t) == 8u,
    "vitte_ast_node_id_t must be 64-bit");

_Static_assert(
    VITTE_AST_INVALID_ID == UINT64_C(0),
    "AST invalid identifier must be zero");

_Static_assert(
    VITTE_PARSER_API_VERSION_MAJOR == 2u,
    "unexpected parser API major version");

#endif

#ifdef __cplusplus
}
#endif

#endif /* VITTE_PARSER_PARSER_H */
