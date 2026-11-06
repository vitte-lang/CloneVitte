#ifndef VITTE_SEMA_SEMA_H
#define VITTE_SEMA_SEMA_H

/*
 * Vitte Compiler
 * src/sema/sema.h
 *
 * Public semantic-analysis contract.
 *
 * Responsibilities:
 *   - semantic context lifecycle;
 *   - lexical scope integration;
 *   - name resolution;
 *   - declaration and symbol binding;
 *   - semantic type representation;
 *   - builtin types;
 *   - type interning;
 *   - expression typing;
 *   - assignment compatibility;
 *   - procedure/call checking;
 *   - control-flow semantic checks;
 *   - constant metadata;
 *   - semantic diagnostics;
 *   - per-AST-node semantic information;
 *   - validation;
 *   - deterministic semantic fingerprints;
 *   - statistics and bounded resource usage.
 *
 * This header is the single public contract for sema.c.
 */

#include "../parser/parser.h"
#include "../scope/scope.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Version                                                                   */
/* ========================================================================= */

#define VITTE_SEMA_API_VERSION_MAJOR 1u
#define VITTE_SEMA_API_VERSION_MINOR 0u
#define VITTE_SEMA_API_VERSION_PATCH 0u

#define VITTE_SEMA_API_VERSION \
    ((VITTE_SEMA_API_VERSION_MAJOR * 10000u) + \
     (VITTE_SEMA_API_VERSION_MINOR * 100u) + \
     VITTE_SEMA_API_VERSION_PATCH)

/* ========================================================================= */
/* Magic                                                                     */
/* ========================================================================= */

#define VITTE_SEMA_MAGIC \
    UINT64_C(0x5649545453454d41)

#define VITTE_SEMA_DEAD_MAGIC \
    UINT64_C(0x4445414453454d41)

/* ========================================================================= */
/* Hashing                                                                   */
/* ========================================================================= */

#define VITTE_SEMA_FNV_OFFSET \
    UINT64_C(14695981039346656037)

#define VITTE_SEMA_FNV_PRIME \
    UINT64_C(1099511628211)

/* ========================================================================= */
/* Limits                                                                    */
/* ========================================================================= */

#define VITTE_SEMA_DEFAULT_INITIAL_TYPE_CAPACITY \
    ((size_t)64u)

#define VITTE_SEMA_DEFAULT_INITIAL_NODE_CAPACITY \
    ((size_t)128u)

#define VITTE_SEMA_DEFAULT_INITIAL_DIAGNOSTIC_CAPACITY \
    ((size_t)32u)

#define VITTE_SEMA_DEFAULT_MAX_TYPES \
    ((size_t)1048576u)

#define VITTE_SEMA_DEFAULT_MAX_DIAGNOSTICS \
    ((size_t)16384u)

#define VITTE_SEMA_DEFAULT_MAX_RECURSION_DEPTH \
    ((size_t)4096u)

/* ========================================================================= */
/* Semantic IDs                                                              */
/* ========================================================================= */

typedef uint64_t vitte_sema_type_id_t;

#define VITTE_SEMA_INVALID_TYPE_ID \
    ((vitte_sema_type_id_t)UINT64_C(0))

/* ========================================================================= */
/* Errors                                                                    */
/* ========================================================================= */

typedef enum vitte_sema_error {
    VITTE_SEMA_ERROR_NONE = 0,

    VITTE_SEMA_ERROR_INVALID_ARGUMENT,
    VITTE_SEMA_ERROR_INVALID_CONTEXT,
    VITTE_SEMA_ERROR_INVALID_STATE,

    VITTE_SEMA_ERROR_INVALID_AST,
    VITTE_SEMA_ERROR_INVALID_SCOPE,
    VITTE_SEMA_ERROR_INVALID_TYPE,

    VITTE_SEMA_ERROR_TYPE_LIMIT,
    VITTE_SEMA_ERROR_DIAGNOSTIC_LIMIT,
    VITTE_SEMA_ERROR_RECURSION_LIMIT,

    VITTE_SEMA_ERROR_OVERFLOW,
    VITTE_SEMA_ERROR_OUT_OF_MEMORY,

    VITTE_SEMA_ERROR_VALIDATION,
    VITTE_SEMA_ERROR_CORRUPTION,

    VITTE_SEMA_ERROR_INTERNAL,

    VITTE_SEMA_ERROR_COUNT
} vitte_sema_error_t;

/* ========================================================================= */
/* State                                                                     */
/* ========================================================================= */

typedef enum vitte_sema_state {
    VITTE_SEMA_STATE_INVALID = 0,

    VITTE_SEMA_STATE_READY,
    VITTE_SEMA_STATE_ANALYZING,
    VITTE_SEMA_STATE_DONE,
    VITTE_SEMA_STATE_FAILED,
    VITTE_SEMA_STATE_DESTROYED,

    VITTE_SEMA_STATE_COUNT
} vitte_sema_state_t;

/* ========================================================================= */
/* Semantic type kinds                                                       */
/* ========================================================================= */

typedef enum vitte_sema_type_kind {
    VITTE_SEMA_TYPE_INVALID = 0,

    /*
     * Recovery type. Operations involving this type should generally avoid
     * emitting cascaded diagnostics.
     */
    VITTE_SEMA_TYPE_ERROR,

    /*
     * Primitive/builtin types.
     */
    VITTE_SEMA_TYPE_VOID,
    VITTE_SEMA_TYPE_BOOL,

    VITTE_SEMA_TYPE_I64,
    VITTE_SEMA_TYPE_U64,
    VITTE_SEMA_TYPE_U8,
    VITTE_SEMA_TYPE_F32,
    VITTE_SEMA_TYPE_F64,

    VITTE_SEMA_TYPE_CHAR,
    VITTE_SEMA_TYPE_STRING,
    VITTE_SEMA_TYPE_NULL,

    /*
     * Constructed types.
     */
    VITTE_SEMA_TYPE_POINTER,
    VITTE_SEMA_TYPE_REFERENCE,
    /* Nullable wrapper; element_type is the value extracted by postfix `?`. */
    VITTE_SEMA_TYPE_OPTIONAL,
    VITTE_SEMA_TYPE_ARRAY,
    VITTE_SEMA_TYPE_SLICE,
    VITTE_SEMA_TYPE_RANGE,
    VITTE_SEMA_TYPE_FUNCTION,

    /*
     * User-declared type.
     */
    VITTE_SEMA_TYPE_NAMED,

    /*
     * Dynamic trait/object type.
     */
    VITTE_SEMA_TYPE_DYN,

    /* A declaration-scoped generic parameter. */
    VITTE_SEMA_TYPE_GENERIC_PARAMETER,

    /* Compiler-generated result wrapper for an async procedure call. */
    VITTE_SEMA_TYPE_TASK,

    VITTE_SEMA_TYPE_COUNT
} vitte_sema_type_kind_t;

/*
 * Result of the central implicit-conversion policy.
 *
 * EXACT and SAFE are accepted by assignments, calls, fields and returns.
 * LOSSY is deliberately rejected for implicit conversions; callers must use
 * an explicit cast so that a possible loss of range or precision is visible.
 */
typedef enum vitte_sema_type_conversion_kind {
    VITTE_SEMA_TYPE_CONVERSION_INCOMPATIBLE = 0,
    VITTE_SEMA_TYPE_CONVERSION_EXACT,
    VITTE_SEMA_TYPE_CONVERSION_SAFE,
    VITTE_SEMA_TYPE_CONVERSION_LOSSY,

    VITTE_SEMA_TYPE_CONVERSION_COUNT
} vitte_sema_type_conversion_kind_t;

/* ========================================================================= */
/* Semantic type                                                             */
/* ========================================================================= */

typedef struct vitte_sema_type {
    /*
     * Stable semantic type ID.
     *
     * IDs are one-based. Zero is always VITTE_SEMA_INVALID_TYPE_ID.
     */
    vitte_sema_type_id_t id;

    vitte_sema_type_kind_t kind;

    /*
     * Element type for:
     *   pointer
     *   reference
     *   optional
     *   array
     *   slice
     *   range
     *
     * VITTE_SEMA_INVALID_TYPE_ID otherwise.
     */
    vitte_sema_type_id_t element_type;

    /*
     * Array extent where known.
     *
     * Zero can represent either an unsized/inferred array or a type for
     * which array_length is irrelevant.
     */
    size_t array_length;

    /*
     * Symbol defining a named/function/dyn type where applicable.
     */
    vitte_symbol_id_t symbol_id;

    /*
     * Function result type.
     *
     * VITTE_SEMA_INVALID_TYPE_ID for non-function types.
     */
    vitte_sema_type_id_t return_type;

    /*
     * Pointer/reference mutability.
     */
    bool is_mutable;

    /*
     * Concrete arguments of a nominal generic instantiation.  The array is
     * owned by the semantic context and is empty for non-generic types and
     * generic declarations before instantiation.
     */
    vitte_sema_type_id_t *generic_arguments;
    size_t generic_argument_count;

    /* Structural signature for procedure types that do not name a procedure
     * declaration (for example, callback aliases). */
    vitte_sema_type_id_t *function_parameter_types;
    size_t function_parameter_count;
} vitte_sema_type_t;

/* ========================================================================= */
/* Constant metadata                                                         */
/* ========================================================================= */

typedef enum vitte_sema_constant_kind {
    VITTE_SEMA_CONSTANT_NONE = 0,

    VITTE_SEMA_CONSTANT_INTEGER,
    VITTE_SEMA_CONSTANT_UNSIGNED_INTEGER,
    VITTE_SEMA_CONSTANT_FLOAT,
    VITTE_SEMA_CONSTANT_BOOLEAN,
    VITTE_SEMA_CONSTANT_CHARACTER,
    VITTE_SEMA_CONSTANT_STRING,
    VITTE_SEMA_CONSTANT_NULL,

    VITTE_SEMA_CONSTANT_COUNT
} vitte_sema_constant_kind_t;

typedef union vitte_sema_constant_value {
    int64_t integer_value;
    uint64_t unsigned_integer_value;
    double float_value;
    bool boolean_value;
    uint32_t character_value;

    struct {
        const char *data;
        size_t length;
    } string_value;
} vitte_sema_constant_value_t;

typedef struct vitte_sema_constant {
    vitte_sema_constant_kind_t kind;
    vitte_sema_constant_value_t value;
} vitte_sema_constant_t;

/* ========================================================================= */
/* Node semantic flags                                                       */
/* ========================================================================= */

typedef uint32_t vitte_sema_node_flags_t;

#define VITTE_SEMA_NODE_FLAG_NONE \
    UINT32_C(0)

#define VITTE_SEMA_NODE_FLAG_RESOLVED \
    (UINT32_C(1) << 0)

#define VITTE_SEMA_NODE_FLAG_DECLARATION \
    (UINT32_C(1) << 1)

#define VITTE_SEMA_NODE_FLAG_LVALUE \
    (UINT32_C(1) << 2)

#define VITTE_SEMA_NODE_FLAG_CONSTANT \
    (UINT32_C(1) << 3)

#define VITTE_SEMA_NODE_FLAG_MUTABLE \
    (UINT32_C(1) << 4)

#define VITTE_SEMA_NODE_FLAG_ADDRESSABLE \
    (UINT32_C(1) << 5)

#define VITTE_SEMA_NODE_FLAG_DIVERGES \
    (UINT32_C(1) << 6)

#define VITTE_SEMA_NODE_FLAG_UNREACHABLE \
    (UINT32_C(1) << 7)

#define VITTE_SEMA_NODE_FLAG_IMPLICIT_CONVERSION \
    (UINT32_C(1) << 8)

#define VITTE_SEMA_NODE_FLAG_UNSAFE \
    (UINT32_C(1) << 9)

#define VITTE_SEMA_NODE_FLAG_BUILTIN \
    (UINT32_C(1) << 10)

#define VITTE_SEMA_NODE_FLAG_PREDECLARED \
    (UINT32_C(1) << 11)

/* ========================================================================= */
/* Per-node semantic information                                             */
/* ========================================================================= */

typedef struct vitte_sema_node_info {
    vitte_ast_node_id_t node_id;

    vitte_sema_type_id_t type_id;

    vitte_symbol_id_t symbol_id;

    vitte_scope_id_t scope_id;

    vitte_sema_node_flags_t flags;

    vitte_sema_constant_t constant;
} vitte_sema_node_info_t;

/* ========================================================================= */
/* Diagnostic kinds                                                          */
/* ========================================================================= */

typedef enum vitte_sema_diagnostic_kind {
    VITTE_SEMA_DIAGNOSTIC_NOTE = 0,
    VITTE_SEMA_DIAGNOSTIC_HELP,
    VITTE_SEMA_DIAGNOSTIC_WARNING,
    VITTE_SEMA_DIAGNOSTIC_ERROR,

    VITTE_SEMA_DIAGNOSTIC_KIND_COUNT
} vitte_sema_diagnostic_kind_t;

/* Structured context for a type diagnostic.  Producers fill this once and
 * renderers can present the same argument/parameter, field/value, return or
 * mutability context in every output format. */
typedef enum vitte_sema_type_context_kind {
    VITTE_SEMA_TYPE_CONTEXT_NONE = 0,
    VITTE_SEMA_TYPE_CONTEXT_EXPRESSION,
    VITTE_SEMA_TYPE_CONTEXT_ARGUMENT,
    VITTE_SEMA_TYPE_CONTEXT_FIELD,
    VITTE_SEMA_TYPE_CONTEXT_RETURN,
    VITTE_SEMA_TYPE_CONTEXT_ASSIGNMENT,
    VITTE_SEMA_TYPE_CONTEXT_CONDITION,

    VITTE_SEMA_TYPE_CONTEXT_COUNT
} vitte_sema_type_context_kind_t;

/* ========================================================================= */
/* Diagnostic codes                                                          */
/* ========================================================================= */

typedef enum vitte_sema_diagnostic_code {
    VITTE_SEMA_DIAGNOSTIC_NONE = 0,

    /*
     * Name resolution.
     */
    VITTE_SEMA_DIAGNOSTIC_UNRESOLVED_NAME,
    VITTE_SEMA_DIAGNOSTIC_PRIVATE_SYMBOL,
    VITTE_SEMA_DIAGNOSTIC_DUPLICATE_DECLARATION,
    VITTE_SEMA_DIAGNOSTIC_SHADOWED_DECLARATION,

    /*
     * Types.
     */
    VITTE_SEMA_DIAGNOSTIC_UNKNOWN_TYPE,
    VITTE_SEMA_DIAGNOSTIC_NOT_A_TYPE,
    VITTE_SEMA_DIAGNOSTIC_TYPE_MISMATCH,

    /*
     * Operators / expressions.
     */
    VITTE_SEMA_DIAGNOSTIC_INVALID_UNARY_OPERAND,
    VITTE_SEMA_DIAGNOSTIC_INVALID_BINARY_OPERANDS,
    VITTE_SEMA_DIAGNOSTIC_NOT_ASSIGNABLE,

    /*
     * Calls.
     */
    VITTE_SEMA_DIAGNOSTIC_NOT_CALLABLE,
    VITTE_SEMA_DIAGNOSTIC_INVALID_ARGUMENT_COUNT,
    VITTE_SEMA_DIAGNOSTIC_INVALID_ARGUMENT_TYPE,

    /*
     * Index/member operations.
     */
    VITTE_SEMA_DIAGNOSTIC_NOT_INDEXABLE,
    VITTE_SEMA_DIAGNOSTIC_INVALID_INDEX_TYPE,
    VITTE_SEMA_DIAGNOSTIC_UNKNOWN_MEMBER,

    /*
     * Conditions/control flow.
     */
    VITTE_SEMA_DIAGNOSTIC_CONDITION_NOT_BOOL,
    VITTE_SEMA_DIAGNOSTIC_RETURN_OUTSIDE_PROC,
    VITTE_SEMA_DIAGNOSTIC_RETURN_TYPE_MISMATCH,
    VITTE_SEMA_DIAGNOSTIC_BREAK_OUTSIDE_LOOP,
    VITTE_SEMA_DIAGNOSTIC_CONTINUE_OUTSIDE_LOOP,
    VITTE_SEMA_DIAGNOSTIC_UNREACHABLE_CODE,

    /*
     * Resource protection.
     */
    VITTE_SEMA_DIAGNOSTIC_RECURSION_LIMIT,
    VITTE_SEMA_DIAGNOSTIC_INVALID_TYPE,

    VITTE_SEMA_DIAGNOSTIC_CODE_COUNT
} vitte_sema_diagnostic_code_t;

/* ========================================================================= */
/* Diagnostic                                                                */
/* ========================================================================= */

typedef struct vitte_sema_diagnostic {
    vitte_sema_diagnostic_kind_t kind;
    vitte_sema_diagnostic_code_t code;

    vitte_parser_span_t span;

    vitte_ast_node_id_t node_id;

    vitte_symbol_id_t symbol_id;

    vitte_sema_type_id_t expected_type;
    vitte_sema_type_id_t found_type;

    /* Structured type mismatch context.  context_index is one-based for
     * arguments and SIZE_MAX when no ordinal applies. */
    vitte_sema_type_context_kind_t type_context;
    size_t context_index;
    vitte_symbol_id_t context_symbol_id;

    /* Evidence from name resolution or the selected procedure signature. */
    char help[512];
} vitte_sema_diagnostic_t;

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

typedef struct vitte_sema_stats {
    /*
     * Complete semantic-analysis runs.
     */
    uint64_t runs;
    uint64_t failed_runs;
    uint64_t failures;

    /*
     * AST traversal.
     */
    uint64_t nodes_visited;
    uint64_t expressions_checked;
    uint64_t declarations;

    /*
     * Name resolution.
     */
    uint64_t names_resolved;
    uint64_t unresolved_names;
    uint64_t duplicate_declarations;
    uint64_t shadowing_declarations;

    /*
     * Type system.
     */
    uint64_t types_created;
    uint64_t type_intern_hits;

    /*
     * Procedure/call checking.
     */
    uint64_t calls_checked;

    /*
     * Control flow.
     */
    uint64_t unreachable_statements;

    /*
     * Diagnostics.
     */
    uint64_t diagnostics;
    uint64_t errors;
    uint64_t warnings;

    /*
     * Traversal limits.
     */
    uint64_t max_recursion_depth;

    /*
     * Validation/hash.
     */
    uint64_t validation_runs;
    uint64_t validation_failures;
    uint64_t hash_runs;

    /*
     * Allocation accounting.
     */
    uint64_t allocations;
    uint64_t reallocations;
    uint64_t allocation_failures;
} vitte_sema_stats_t;

/* ========================================================================= */
/* Semantic context                                                          */
/* ========================================================================= */

typedef struct vitte_sema {
    uint64_t magic;

    vitte_sema_state_t state;
    vitte_sema_error_t last_error;

    /*
     * Parser/AST owner.
     *
     * sema does not own this pointer.
     */
    const vitte_parser_t *parser;

    /*
     * Lexical scopes and symbols.
     *
     * Owned by this semantic context.
     */
    vitte_scope_context_t scopes;

    /*
     * Semantic type table.
     */
    vitte_sema_type_t *types;
    size_t type_count;
    size_t type_capacity;

    /*
     * One semantic-information entry per parser AST node.
     */
    vitte_sema_node_info_t *node_info;
    size_t node_info_count;
    size_t node_info_capacity;

    /*
     * Semantic diagnostics.
     */
    vitte_sema_diagnostic_t *diagnostics;
    size_t diagnostic_count;
    size_t diagnostic_capacity;

    /*
     * Resource limits.
     */
    size_t max_types;
    size_t max_diagnostics;
    size_t max_recursion_depth;

    /*
     * Current recursive traversal state.
     */
    size_t recursion_depth;
    size_t loop_depth;

    /*
     * Current procedure context.
     */
    vitte_sema_type_id_t current_return_type;
    vitte_sema_type_id_t current_self_type;
    vitte_scope_id_t current_proc_scope;
    vitte_ast_node_id_t awaited_async_call_id;

    /* True while checking an ensures clause, where `result` is implicit. */
    bool current_contract_ensures;

    /* Public use/export declarations, indexed once for name lookup. */
    vitte_ast_node_id_t *explicit_exports;
    size_t explicit_export_count;

    /*
     * Cached canonical builtin type IDs.
     */
    vitte_sema_type_id_t type_error;
    vitte_sema_type_id_t type_void;
    vitte_sema_type_id_t type_bool;

    vitte_sema_type_id_t type_i64;
    vitte_sema_type_id_t type_u64;
    vitte_sema_type_id_t type_u8;
    vitte_sema_type_id_t type_f32;
    vitte_sema_type_id_t type_f64;

    vitte_sema_type_id_t type_char;
    vitte_sema_type_id_t type_string;
    vitte_sema_type_id_t type_null;

    /*
     * Statistics.
     */
    vitte_sema_stats_t stats;

    /*
     * Incremented by reset.
     */
    uint64_t generation;
} vitte_sema_t;

/* ========================================================================= */
/* Enum names                                                                */
/* ========================================================================= */

const char *
vitte_sema_error_name(
    vitte_sema_error_t error);

const char *
vitte_sema_state_name(
    vitte_sema_state_t state);

const char *
vitte_sema_type_kind_name(
    vitte_sema_type_kind_t kind);

const char *
vitte_sema_type_conversion_kind_name(
    vitte_sema_type_conversion_kind_t kind);

const char *
vitte_sema_diagnostic_kind_name(
    vitte_sema_diagnostic_kind_t kind);

const char *
vitte_sema_diagnostic_code_name(
    vitte_sema_diagnostic_code_t code);

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

bool
vitte_sema_init(
    vitte_sema_t *context,
    const vitte_parser_t *parser);

bool
vitte_sema_reset(
    vitte_sema_t *context,
    const vitte_parser_t *parser);

void
vitte_sema_destroy(
    vitte_sema_t *context);

/* ========================================================================= */
/* Context state                                                             */
/* ========================================================================= */

bool
vitte_sema_is_valid(
    const vitte_sema_t *context);

vitte_sema_error_t
vitte_sema_last_error(
    const vitte_sema_t *context);

uint64_t
vitte_sema_generation(
    const vitte_sema_t *context);

/* ========================================================================= */
/* Limits                                                                    */
/* ========================================================================= */

bool
vitte_sema_set_limits(
    vitte_sema_t *context,
    size_t max_types,
    size_t max_diagnostics,
    size_t max_recursion_depth);

/* ========================================================================= */
/* Analysis                                                                  */
/* ========================================================================= */

bool
vitte_sema_run(
    vitte_sema_t *context);

/* ========================================================================= */
/* Type queries                                                              */
/* ========================================================================= */

const vitte_sema_type_t *
vitte_sema_get_type(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id);

/* Classify one implicit conversion without applying it. */
vitte_sema_type_conversion_kind_t
vitte_sema_type_conversion(
    const vitte_sema_t *context,
    vitte_sema_type_id_t destination,
    vitte_sema_type_id_t source);

bool
vitte_sema_type_assignable(
    const vitte_sema_t *context,
    vitte_sema_type_id_t destination,
    vitte_sema_type_id_t source);

bool
vitte_sema_type_is_numeric(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id);

bool
vitte_sema_type_is_integer(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id);

bool
vitte_sema_type_is_pointer_like(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id);

/* ========================================================================= */
/* AST semantic queries                                                      */
/* ========================================================================= */

const vitte_sema_node_info_t *
vitte_sema_get_node_info(
    const vitte_sema_t *context,
    vitte_ast_node_id_t node_id);

vitte_sema_type_id_t
vitte_sema_node_type(
    const vitte_sema_t *context,
    vitte_ast_node_id_t node_id);

vitte_symbol_id_t
vitte_sema_node_symbol(
    const vitte_sema_t *context,
    vitte_ast_node_id_t node_id);

vitte_scope_id_t
vitte_sema_node_scope(
    const vitte_sema_t *context,
    vitte_ast_node_id_t node_id);

/* ========================================================================= */
/* Diagnostics                                                               */
/* ========================================================================= */

size_t
vitte_sema_diagnostic_count(
    const vitte_sema_t *context);

const vitte_sema_diagnostic_t *
vitte_sema_diagnostic_at(
    const vitte_sema_t *context,
    size_t index);

bool
vitte_sema_has_errors(
    const vitte_sema_t *context);

/* ========================================================================= */
/* Validation / fingerprint                                                  */
/* ========================================================================= */

bool
vitte_sema_validate(
    vitte_sema_t *context);

uint64_t
vitte_sema_fingerprint(
    vitte_sema_t *context);

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_sema_stats_t
vitte_sema_stats(
    const vitte_sema_t *context);

/* ========================================================================= */
/* Translation-unit anchor                                                   */
/* ========================================================================= */

void
vitte_sema_translation_unit_anchor(void);

/* ========================================================================= */
/* Inline API/version helpers                                                */
/* ========================================================================= */

static inline unsigned int
vitte_sema_api_version_major(void)
{
    return VITTE_SEMA_API_VERSION_MAJOR;
}

static inline unsigned int
vitte_sema_api_version_minor(void)
{
    return VITTE_SEMA_API_VERSION_MINOR;
}

static inline unsigned int
vitte_sema_api_version_patch(void)
{
    return VITTE_SEMA_API_VERSION_PATCH;
}

static inline unsigned int
vitte_sema_api_version(void)
{
    return VITTE_SEMA_API_VERSION;
}

/* ========================================================================= */
/* Inline enum validation                                                    */
/* ========================================================================= */

static inline bool
vitte_sema_error_is_valid(
    vitte_sema_error_t error)
{
    return error >= VITTE_SEMA_ERROR_NONE &&
           error < VITTE_SEMA_ERROR_COUNT;
}

static inline bool
vitte_sema_state_is_valid(
    vitte_sema_state_t state)
{
    return state > VITTE_SEMA_STATE_INVALID &&
           state < VITTE_SEMA_STATE_COUNT;
}

static inline bool
vitte_sema_type_kind_is_valid(
    vitte_sema_type_kind_t kind)
{
    return kind > VITTE_SEMA_TYPE_INVALID &&
           kind < VITTE_SEMA_TYPE_COUNT;
}

static inline bool
vitte_sema_constant_kind_is_valid(
    vitte_sema_constant_kind_t kind)
{
    return kind >= VITTE_SEMA_CONSTANT_NONE &&
           kind < VITTE_SEMA_CONSTANT_COUNT;
}

static inline bool
vitte_sema_diagnostic_kind_is_valid(
    vitte_sema_diagnostic_kind_t kind)
{
    return kind >= VITTE_SEMA_DIAGNOSTIC_NOTE &&
           kind < VITTE_SEMA_DIAGNOSTIC_KIND_COUNT;
}

static inline bool
vitte_sema_diagnostic_code_is_valid(
    vitte_sema_diagnostic_code_t code)
{
    return code >= VITTE_SEMA_DIAGNOSTIC_NONE &&
           code < VITTE_SEMA_DIAGNOSTIC_CODE_COUNT;
}

/* ========================================================================= */
/* Inline context helpers                                                    */
/* ========================================================================= */

static inline bool
vitte_sema_is_ready(
    const vitte_sema_t *context)
{
    return context != NULL &&
           context->magic == VITTE_SEMA_MAGIC &&
           context->state == VITTE_SEMA_STATE_READY;
}

static inline bool
vitte_sema_is_analyzing(
    const vitte_sema_t *context)
{
    return context != NULL &&
           context->magic == VITTE_SEMA_MAGIC &&
           context->state == VITTE_SEMA_STATE_ANALYZING;
}

static inline bool
vitte_sema_is_done(
    const vitte_sema_t *context)
{
    return context != NULL &&
           context->magic == VITTE_SEMA_MAGIC &&
           context->state == VITTE_SEMA_STATE_DONE;
}

static inline bool
vitte_sema_has_failed(
    const vitte_sema_t *context)
{
    return context != NULL &&
           context->magic == VITTE_SEMA_MAGIC &&
           context->state == VITTE_SEMA_STATE_FAILED;
}

static inline bool
vitte_sema_is_destroyed(
    const vitte_sema_t *context)
{
    return context != NULL &&
           context->magic == VITTE_SEMA_DEAD_MAGIC &&
           context->state == VITTE_SEMA_STATE_DESTROYED;
}

/* ========================================================================= */
/* Inline counts                                                             */
/* ========================================================================= */

static inline size_t
vitte_sema_type_count(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_count
        : 0u;
}

static inline size_t
vitte_sema_node_info_count(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->node_info_count
        : 0u;
}

static inline size_t
vitte_sema_error_count(
    const vitte_sema_t *context)
{
    return context != NULL
        ? (size_t)context->stats.errors
        : 0u;
}

static inline size_t
vitte_sema_warning_count(
    const vitte_sema_t *context)
{
    return context != NULL
        ? (size_t)context->stats.warnings
        : 0u;
}

/* ========================================================================= */
/* Inline builtin type access                                                */
/* ========================================================================= */

static inline vitte_sema_type_id_t
vitte_sema_error_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_error
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_void_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_void
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_bool_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_bool
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_i64_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_i64
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_u64_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_u64
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_u8_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_u8
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_f32_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_f32
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_f64_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_f64
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_char_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_char
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_string_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_string
        : VITTE_SEMA_INVALID_TYPE_ID;
}

static inline vitte_sema_type_id_t
vitte_sema_null_type(
    const vitte_sema_t *context)
{
    return context != NULL
        ? context->type_null
        : VITTE_SEMA_INVALID_TYPE_ID;
}

/* ========================================================================= */
/* Inline type helpers                                                       */
/* ========================================================================= */

static inline bool
vitte_sema_type_id_is_valid(
    vitte_sema_type_id_t type_id)
{
    return type_id !=
           VITTE_SEMA_INVALID_TYPE_ID;
}

static inline bool
vitte_sema_type_is_error(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id)
{
    return context != NULL &&
           type_id ==
               context->type_error;
}

static inline bool
vitte_sema_type_is_void(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id)
{
    return context != NULL &&
           type_id ==
               context->type_void;
}

static inline bool
vitte_sema_type_is_bool(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id)
{
    return context != NULL &&
           type_id ==
               context->type_bool;
}

static inline bool
vitte_sema_type_is_string(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id)
{
    return context != NULL &&
           type_id ==
               context->type_string;
}

static inline bool
vitte_sema_type_is_null(
    const vitte_sema_t *context,
    vitte_sema_type_id_t type_id)
{
    return context != NULL &&
           type_id ==
               context->type_null;
}

static inline bool
vitte_sema_type_is_function(
    const vitte_sema_type_t *type)
{
    return type != NULL &&
           type->kind ==
               VITTE_SEMA_TYPE_FUNCTION;
}

static inline bool
vitte_sema_type_is_named(
    const vitte_sema_type_t *type)
{
    return type != NULL &&
           type->kind ==
               VITTE_SEMA_TYPE_NAMED;
}

static inline bool
vitte_sema_type_is_reference(
    const vitte_sema_type_t *type)
{
    return type != NULL &&
           type->kind ==
               VITTE_SEMA_TYPE_REFERENCE;
}

static inline bool
vitte_sema_type_is_pointer(
    const vitte_sema_type_t *type)
{
    return type != NULL &&
           type->kind ==
               VITTE_SEMA_TYPE_POINTER;
}

static inline bool
vitte_sema_type_is_array(
    const vitte_sema_type_t *type)
{
    return type != NULL &&
           type->kind ==
               VITTE_SEMA_TYPE_ARRAY;
}

static inline bool
vitte_sema_type_is_range(
    const vitte_sema_type_t *type)
{
    return type != NULL &&
           type->kind ==
               VITTE_SEMA_TYPE_RANGE;
}

static inline bool
vitte_sema_type_is_dyn(
    const vitte_sema_type_t *type)
{
    return type != NULL &&
           type->kind ==
               VITTE_SEMA_TYPE_DYN;
}

/* ========================================================================= */
/* Inline node-information helpers                                           */
/* ========================================================================= */

static inline bool
vitte_sema_node_is_resolved(
    const vitte_sema_node_info_t *info)
{
    return info != NULL &&
           (info->flags &
            VITTE_SEMA_NODE_FLAG_RESOLVED) != 0u;
}

static inline bool
vitte_sema_node_is_declaration(
    const vitte_sema_node_info_t *info)
{
    return info != NULL &&
           (info->flags &
            VITTE_SEMA_NODE_FLAG_DECLARATION) != 0u;
}

static inline bool
vitte_sema_node_is_lvalue(
    const vitte_sema_node_info_t *info)
{
    return info != NULL &&
           (info->flags &
            VITTE_SEMA_NODE_FLAG_LVALUE) != 0u;
}

static inline bool
vitte_sema_node_is_constant(
    const vitte_sema_node_info_t *info)
{
    return info != NULL &&
           (info->flags &
            VITTE_SEMA_NODE_FLAG_CONSTANT) != 0u;
}

static inline bool
vitte_sema_node_is_mutable(
    const vitte_sema_node_info_t *info)
{
    return info != NULL &&
           (info->flags &
            VITTE_SEMA_NODE_FLAG_MUTABLE) != 0u;
}

static inline bool
vitte_sema_node_is_addressable(
    const vitte_sema_node_info_t *info)
{
    return info != NULL &&
           (info->flags &
            VITTE_SEMA_NODE_FLAG_ADDRESSABLE) != 0u;
}

static inline bool
vitte_sema_node_diverges(
    const vitte_sema_node_info_t *info)
{
    return info != NULL &&
           (info->flags &
            VITTE_SEMA_NODE_FLAG_DIVERGES) != 0u;
}

static inline bool
vitte_sema_node_is_unreachable(
    const vitte_sema_node_info_t *info)
{
    return info != NULL &&
           (info->flags &
            VITTE_SEMA_NODE_FLAG_UNREACHABLE) != 0u;
}

/* ========================================================================= */
/* Inline constant helpers                                                   */
/* ========================================================================= */

static inline bool
vitte_sema_constant_is_valid(
    const vitte_sema_constant_t *constant)
{
    return constant != NULL &&
           constant->kind >
               VITTE_SEMA_CONSTANT_NONE &&
           constant->kind <
               VITTE_SEMA_CONSTANT_COUNT;
}

static inline bool
vitte_sema_constant_is_integer(
    const vitte_sema_constant_t *constant)
{
    return constant != NULL &&
           (constant->kind ==
                VITTE_SEMA_CONSTANT_INTEGER ||
            constant->kind ==
                VITTE_SEMA_CONSTANT_UNSIGNED_INTEGER);
}

static inline bool
vitte_sema_constant_is_numeric(
    const vitte_sema_constant_t *constant)
{
    return constant != NULL &&
           (constant->kind ==
                VITTE_SEMA_CONSTANT_INTEGER ||
            constant->kind ==
                VITTE_SEMA_CONSTANT_UNSIGNED_INTEGER ||
            constant->kind ==
                VITTE_SEMA_CONSTANT_FLOAT);
}

/* ========================================================================= */
/* Inline diagnostic helpers                                                 */
/* ========================================================================= */

static inline bool
vitte_sema_diagnostic_is_error(
    const vitte_sema_diagnostic_t *diagnostic)
{
    return diagnostic != NULL &&
           diagnostic->kind ==
               VITTE_SEMA_DIAGNOSTIC_ERROR;
}

static inline bool
vitte_sema_diagnostic_is_warning(
    const vitte_sema_diagnostic_t *diagnostic)
{
    return diagnostic != NULL &&
           diagnostic->kind ==
               VITTE_SEMA_DIAGNOSTIC_WARNING;
}

static inline bool
vitte_sema_diagnostic_has_symbol(
    const vitte_sema_diagnostic_t *diagnostic)
{
    return diagnostic != NULL &&
           diagnostic->symbol_id !=
               VITTE_SCOPE_INVALID_SYMBOL_ID;
}

static inline bool
vitte_sema_diagnostic_has_expected_type(
    const vitte_sema_diagnostic_t *diagnostic)
{
    return diagnostic != NULL &&
           diagnostic->expected_type !=
               VITTE_SEMA_INVALID_TYPE_ID;
}

static inline bool
vitte_sema_diagnostic_has_found_type(
    const vitte_sema_diagnostic_t *diagnostic)
{
    return diagnostic != NULL &&
           diagnostic->found_type !=
               VITTE_SEMA_INVALID_TYPE_ID;
}

/* ========================================================================= */
/* Compile-time checks                                                       */
/* ========================================================================= */

#if defined(__cplusplus)

static_assert(
    sizeof(vitte_sema_type_id_t) == sizeof(uint64_t),
    "vitte_sema_type_id_t must be 64-bit");

static_assert(
    VITTE_SEMA_INVALID_TYPE_ID == UINT64_C(0),
    "invalid semantic type ID must be zero");

static_assert(
    VITTE_SEMA_TYPE_INVALID == 0,
    "invalid semantic type kind must be zero");

static_assert(
    VITTE_SEMA_STATE_INVALID == 0,
    "invalid semantic state must be zero");

#else

_Static_assert(
    sizeof(vitte_sema_type_id_t) == sizeof(uint64_t),
    "vitte_sema_type_id_t must be 64-bit");

_Static_assert(
    VITTE_SEMA_INVALID_TYPE_ID == UINT64_C(0),
    "invalid semantic type ID must be zero");

_Static_assert(
    VITTE_SEMA_TYPE_INVALID == 0,
    "invalid semantic type kind must be zero");

_Static_assert(
    VITTE_SEMA_STATE_INVALID == 0,
    "invalid semantic state must be zero");

#endif

#ifdef __cplusplus
}
#endif

#endif /* VITTE_SEMA_SEMA_H */
