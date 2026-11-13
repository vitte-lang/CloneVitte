/*
 * Vitte Compiler
 * src/backend/c17/module.c
 *
 * C17 backend module layer.
 *
 * A module represents one logical Vitte compilation unit prepared for
 * deterministic emission through the C17 backend.
 *
 * Responsibilities:
 *
 *   - module lifecycle
 *   - module identity
 *   - source identity
 *   - dependency collection
 *   - include collection
 *   - symbol registration
 *   - duplicate detection
 *   - runtime requirement collection
 *   - feature collection
 *   - generated fragments grouped by C17 section
 *   - deterministic ordering
 *   - validation
 *   - statistics
 *   - final emission through vitte_c17_backend_t
 *
 * This file deliberately does not:
 *
 *   - parse Vitte source
 *   - own AST/HIR/IR nodes
 *   - perform semantic analysis
 *   - implement expression lowering
 *   - implement statement lowering
 *   - invoke a native C compiler
 *
 * Ownership:
 *
 *   vitte_c17_module_t owns all strings and dynamic arrays stored in it.
 *   Pointers supplied by callers are copied unless explicitly documented.
 */

#include "module.h"

#include "backend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

#ifndef VITTE_C17_MODULE_MAGIC
#define VITTE_C17_MODULE_MAGIC UINT64_C(0x56495454454D4F44)
#endif

#ifndef VITTE_C17_MODULE_DEAD_MAGIC
#define VITTE_C17_MODULE_DEAD_MAGIC UINT64_C(0x444541444D4F4421)
#endif

#ifndef VITTE_C17_MODULE_INITIAL_CAPACITY
#define VITTE_C17_MODULE_INITIAL_CAPACITY ((size_t)8u)
#endif

#ifndef VITTE_C17_MODULE_INITIAL_FRAGMENT_CAPACITY
#define VITTE_C17_MODULE_INITIAL_FRAGMENT_CAPACITY ((size_t)16u)
#endif

#ifndef VITTE_C17_MODULE_MAX_NAME_BYTES
#define VITTE_C17_MODULE_MAX_NAME_BYTES ((size_t)4096u)
#endif

#ifndef VITTE_C17_MODULE_MAX_PATH_BYTES
#define VITTE_C17_MODULE_MAX_PATH_BYTES ((size_t)(1024u * 1024u))
#endif

#ifndef VITTE_C17_MODULE_MAX_FRAGMENT_BYTES
#define VITTE_C17_MODULE_MAX_FRAGMENT_BYTES SIZE_MAX
#endif

/* ========================================================================= */
/* Internal arithmetic                                                       */
/* ========================================================================= */

static bool
vitte_c17_module_add_overflow(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (left > SIZE_MAX - right) {
        return true;
    }

    *result = left + right;
    return false;
}

static bool
vitte_c17_module_mul_overflow(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (left != 0u &&
        right > SIZE_MAX / left) {
        return true;
    }

    *result = left * right;
    return false;
}

static size_t
vitte_c17_module_saturating_add(
    size_t left,
    size_t right)
{
    size_t result;

    if (vitte_c17_module_add_overflow(
            left,
            right,
            &result)) {
        return SIZE_MAX;
    }

    return result;
}

/* ========================================================================= */
/* String helpers                                                            */
/* ========================================================================= */

static char *
vitte_c17_module_copy_string_n(
    const char *text,
    size_t length)
{
    char *copy;
    size_t bytes;

    if (text == NULL) {
        return NULL;
    }

    if (vitte_c17_module_add_overflow(
            length,
            1u,
            &bytes)) {
        return NULL;
    }

    copy =
        (char *)malloc(bytes);

    if (copy == NULL) {
        return NULL;
    }

    if (length != 0u) {
        memcpy(
            copy,
            text,
            length);
    }

    copy[length] = '\0';

    return copy;
}

static bool
vitte_c17_module_string_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length)
{
    if (left == NULL ||
        right == NULL ||
        left_length != right_length) {
        return false;
    }

    if (left_length == 0u) {
        return true;
    }

    return
        memcmp(
            left,
            right,
            left_length) == 0;
}

static int
vitte_c17_module_string_compare(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length)
{
    size_t minimum;
    int comparison;

    minimum =
        left_length < right_length
            ? left_length
            : right_length;

    comparison = 0;

    if (minimum != 0u) {
        comparison =
            memcmp(
                left,
                right,
                minimum);
    }

    if (comparison != 0) {
        return comparison;
    }

    if (left_length < right_length) {
        return -1;
    }

    if (left_length > right_length) {
        return 1;
    }

    return 0;
}

/* ========================================================================= */
/* Generic growth                                                            */
/* ========================================================================= */

static bool
vitte_c17_module_next_capacity(
    size_t current,
    size_t minimum,
    size_t *result)
{
    size_t capacity;

    if (result == NULL) {
        return false;
    }

    if (minimum == 0u) {
        *result = current;
        return true;
    }

    capacity =
        current != 0u
            ? current
            : VITTE_C17_MODULE_INITIAL_CAPACITY;

    while (capacity < minimum) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = minimum;
            break;
        }

        capacity *= 2u;
    }

    if (capacity < minimum) {
        return false;
    }

    *result = capacity;

    return true;
}

static bool
vitte_c17_module_reserve_array(
    void **storage,
    size_t *capacity,
    size_t minimum,
    size_t element_size)
{
    size_t new_capacity;
    size_t bytes;
    void *replacement;

    if (storage == NULL ||
        capacity == NULL ||
        element_size == 0u) {
        return false;
    }

    if (minimum <= *capacity) {
        return true;
    }

    if (!vitte_c17_module_next_capacity(
            *capacity,
            minimum,
            &new_capacity)) {
        return false;
    }

    if (vitte_c17_module_mul_overflow(
            new_capacity,
            element_size,
            &bytes)) {
        return false;
    }

    replacement =
        realloc(
            *storage,
            bytes);

    if (replacement == NULL) {
        return false;
    }

    *storage = replacement;
    *capacity = new_capacity;

    return true;
}

/* ========================================================================= */
/* Error handling                                                            */
/* ========================================================================= */

const char *
vitte_c17_module_error_name(
    vitte_c17_module_error_t error)
{
    switch (error) {
        case VITTE_C17_MODULE_ERROR_NONE:
            return "none";

        case VITTE_C17_MODULE_ERROR_INVALID_MODULE:
            return "invalid-module";

        case VITTE_C17_MODULE_ERROR_INVALID_ARGUMENT:
            return "invalid-argument";

        case VITTE_C17_MODULE_ERROR_INVALID_STATE:
            return "invalid-state";

        case VITTE_C17_MODULE_ERROR_OVERFLOW:
            return "overflow";

        case VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY:
            return "out-of-memory";

        case VITTE_C17_MODULE_ERROR_DUPLICATE:
            return "duplicate";

        case VITTE_C17_MODULE_ERROR_CONFLICT:
            return "conflict";

        case VITTE_C17_MODULE_ERROR_INVALID_SECTION:
            return "invalid-section";

        case VITTE_C17_MODULE_ERROR_OUTPUT:
            return "output";

        case VITTE_C17_MODULE_ERROR_CORRUPTION:
            return "corruption";

        case VITTE_C17_MODULE_ERROR_COUNT:
            return "count";

        default:
            return "unknown";
    }
}

static bool
vitte_c17_module_fail(
    vitte_c17_module_t *module,
    vitte_c17_module_error_t error)
{
    if (module != NULL &&
        module->magic ==
            VITTE_C17_MODULE_MAGIC) {
        module->last_error = error;
    }

    return false;
}

/* ========================================================================= */
/* State                                                                     */
/* ========================================================================= */

const char *
vitte_c17_module_state_name(
    vitte_c17_module_state_t state)
{
    switch (state) {
        case VITTE_C17_MODULE_STATE_UNINITIALIZED:
            return "uninitialized";

        case VITTE_C17_MODULE_STATE_BUILDING:
            return "building";

        case VITTE_C17_MODULE_STATE_SEALED:
            return "sealed";

        case VITTE_C17_MODULE_STATE_EMITTING:
            return "emitting";

        case VITTE_C17_MODULE_STATE_EMITTED:
            return "emitted";

        case VITTE_C17_MODULE_STATE_FAILED:
            return "failed";

        case VITTE_C17_MODULE_STATE_DESTROYED:
            return "destroyed";

        case VITTE_C17_MODULE_STATE_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Include kind                                                              */
/* ========================================================================= */

const char *
vitte_c17_include_kind_name(
    vitte_c17_include_kind_t kind)
{
    switch (kind) {
        case VITTE_C17_INCLUDE_SYSTEM:
            return "system";

        case VITTE_C17_INCLUDE_LOCAL:
            return "local";

        case VITTE_C17_INCLUDE_INVALID:
            return "invalid";

        case VITTE_C17_INCLUDE_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Symbol kind                                                               */
/* ========================================================================= */

const char *
vitte_c17_symbol_kind_name(
    vitte_c17_symbol_kind_t kind)
{
    switch (kind) {
        case VITTE_C17_SYMBOL_TYPE:
            return "type";

        case VITTE_C17_SYMBOL_GLOBAL:
            return "global";

        case VITTE_C17_SYMBOL_FUNCTION:
            return "function";

        case VITTE_C17_SYMBOL_RUNTIME:
            return "runtime";

        case VITTE_C17_SYMBOL_INTERNAL:
            return "internal";

        case VITTE_C17_SYMBOL_INVALID:
            return "invalid";

        case VITTE_C17_SYMBOL_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Runtime requirement                                                       */
/* ========================================================================= */

const char *
vitte_c17_runtime_requirement_name(
    vitte_c17_runtime_requirement_t requirement)
{
    switch (requirement) {
        case VITTE_C17_RUNTIME_NONE:
            return "none";

        case VITTE_C17_RUNTIME_ALLOC:
            return "alloc";

        case VITTE_C17_RUNTIME_STRING:
            return "string";

        case VITTE_C17_RUNTIME_ARRAY:
            return "array";

        case VITTE_C17_RUNTIME_SLICE:
            return "slice";

        case VITTE_C17_RUNTIME_MAP:
            return "map";

        case VITTE_C17_RUNTIME_PANIC:
            return "panic";

        case VITTE_C17_RUNTIME_ASSERT:
            return "assert";

        case VITTE_C17_RUNTIME_IO:
            return "io";

        case VITTE_C17_RUNTIME_MATH:
            return "math";

        case VITTE_C17_RUNTIME_ASYNC:
            return "async";

        case VITTE_C17_RUNTIME_COUNT:
            return "count";

        default:
            return "unknown";
    }
}

/* ========================================================================= */
/* Validity                                                                  */
/* ========================================================================= */

bool
vitte_c17_module_is_valid(
    const vitte_c17_module_t *module)
{
    if (module == NULL) {
        return false;
    }

    if (module->magic !=
        VITTE_C17_MODULE_MAGIC) {
        return false;
    }

    if (module->name == NULL ||
        module->name_length == 0u) {
        return false;
    }

    if (module->state <=
            VITTE_C17_MODULE_STATE_UNINITIALIZED ||
        module->state >=
            VITTE_C17_MODULE_STATE_DESTROYED) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

vitte_c17_module_config_t
vitte_c17_module_config_default(void)
{
    vitte_c17_module_config_t config;

    memset(
        &config,
        0,
        sizeof(config));

    config.deterministic = true;
    config.deduplicate_dependencies = true;
    config.deduplicate_includes = true;
    config.reject_duplicate_symbols = true;
    config.reject_duplicate_fragments = false;
    config.emit_empty_sections = false;

    return config;
}

/* ========================================================================= */
/* Initialization                                                            */
/* ========================================================================= */

bool
vitte_c17_module_init(
    vitte_c17_module_t *module,
    const char *name,
    size_t name_length,
    const vitte_c17_module_config_t *config)
{
    vitte_c17_module_config_t effective;
    char *name_copy;

    if (module == NULL ||
        name == NULL ||
        name_length == 0u ||
        name_length >
            VITTE_C17_MODULE_MAX_NAME_BYTES) {
        return false;
    }

    effective =
        config != NULL
            ? *config
            : vitte_c17_module_config_default();

    name_copy =
        vitte_c17_module_copy_string_n(
            name,
            name_length);

    if (name_copy == NULL) {
        return false;
    }

    memset(
        module,
        0,
        sizeof(*module));

    module->magic =
        VITTE_C17_MODULE_MAGIC;

    module->state =
        VITTE_C17_MODULE_STATE_BUILDING;

    module->last_error =
        VITTE_C17_MODULE_ERROR_NONE;

    module->config = effective;

    module->name = name_copy;
    module->name_length = name_length;

    module->source_file_id = 0u;

    return true;
}

/* ========================================================================= */
/* Destruction helpers                                                       */
/* ========================================================================= */

static void
vitte_c17_module_destroy_dependencies(
    vitte_c17_module_t *module)
{
    size_t index;

    for (index = 0u;
         index < module->dependency_count;
         ++index) {
        free(
            module->dependencies[index].name);
    }

    free(module->dependencies);

    module->dependencies = NULL;
    module->dependency_count = 0u;
    module->dependency_capacity = 0u;
}

static void
vitte_c17_module_destroy_includes(
    vitte_c17_module_t *module)
{
    size_t index;

    for (index = 0u;
         index < module->include_count;
         ++index) {
        free(
            module->includes[index].path);
    }

    free(module->includes);

    module->includes = NULL;
    module->include_count = 0u;
    module->include_capacity = 0u;
}

static void
vitte_c17_module_destroy_symbols(
    vitte_c17_module_t *module)
{
    size_t index;

    for (index = 0u;
         index < module->symbol_count;
         ++index) {
        free(
            module->symbols[index].name);
    }

    free(module->symbols);

    module->symbols = NULL;
    module->symbol_count = 0u;
    module->symbol_capacity = 0u;
}

static void
vitte_c17_module_destroy_features(
    vitte_c17_module_t *module)
{
    size_t index;

    for (index = 0u;
         index < module->feature_count;
         ++index) {
        free(
            module->features[index].name);
    }

    free(module->features);

    module->features = NULL;
    module->feature_count = 0u;
    module->feature_capacity = 0u;
}

static void
vitte_c17_module_destroy_fragments(
    vitte_c17_module_t *module)
{
    size_t index;

    for (index = 0u;
         index < module->fragment_count;
         ++index) {
        free(
            module->fragments[index].text);
    }

    free(module->fragments);

    module->fragments = NULL;
    module->fragment_count = 0u;
    module->fragment_capacity = 0u;
}

/* ========================================================================= */
/* Destruction                                                               */
/* ========================================================================= */

void
vitte_c17_module_destroy(
    vitte_c17_module_t *module)
{
    if (module == NULL ||
        module->magic !=
            VITTE_C17_MODULE_MAGIC) {
        return;
    }

    vitte_c17_module_destroy_dependencies(
        module);

    vitte_c17_module_destroy_includes(
        module);

    vitte_c17_module_destroy_symbols(
        module);

    vitte_c17_module_destroy_features(
        module);

    vitte_c17_module_destroy_fragments(
        module);

    free(module->name);
    free(module->source_path);

    module->name = NULL;
    module->source_path = NULL;

    module->name_length = 0u;
    module->source_path_length = 0u;

    module->runtime_requirements = 0u;

    module->state =
        VITTE_C17_MODULE_STATE_DESTROYED;

    module->magic =
        VITTE_C17_MODULE_DEAD_MAGIC;
}

/* ========================================================================= */
/* Source identity                                                           */
/* ========================================================================= */

bool
vitte_c17_module_set_source(
    vitte_c17_module_t *module,
    uint32_t file_id,
    const char *path,
    size_t path_length)
{
    char *copy;

    if (!vitte_c17_module_is_valid(
            module) ||
        module->state !=
            VITTE_C17_MODULE_STATE_BUILDING ||
        path == NULL ||
        path_length >
            VITTE_C17_MODULE_MAX_PATH_BYTES) {
        return false;
    }

    copy =
        vitte_c17_module_copy_string_n(
            path,
            path_length);

    if (copy == NULL) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    free(module->source_path);

    module->source_path = copy;
    module->source_path_length =
        path_length;

    module->source_file_id =
        file_id;

    return true;
}

/* ========================================================================= */
/* Dependencies                                                              */
/* ========================================================================= */

static ptrdiff_t
vitte_c17_module_find_dependency_index(
    const vitte_c17_module_t *module,
    const char *name,
    size_t length)
{
    size_t index;

    for (index = 0u;
         index < module->dependency_count;
         ++index) {
        if (vitte_c17_module_string_equal(
                module->dependencies[index].name,
                module->dependencies[index].name_length,
                name,
                length)) {
            return (ptrdiff_t)index;
        }
    }

    return -1;
}

bool
vitte_c17_module_add_dependency(
    vitte_c17_module_t *module,
    const char *name,
    size_t name_length,
    bool public_dependency)
{
    vitte_c17_module_dependency_t *dependency;
    char *copy;
    ptrdiff_t existing;

    if (!vitte_c17_module_is_valid(
            module) ||
        module->state !=
            VITTE_C17_MODULE_STATE_BUILDING ||
        name == NULL ||
        name_length == 0u ||
        name_length >
            VITTE_C17_MODULE_MAX_NAME_BYTES) {
        return false;
    }

    existing =
        vitte_c17_module_find_dependency_index(
            module,
            name,
            name_length);

    if (existing >= 0) {
        dependency =
            &module->dependencies[
                (size_t)existing];

        if (public_dependency) {
            dependency->public_dependency =
                true;
        }

        if (module->config.deduplicate_dependencies) {
            return true;
        }

        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_DUPLICATE);
    }

    if (module->dependency_count ==
        SIZE_MAX) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OVERFLOW);
    }

    if (!vitte_c17_module_reserve_array(
            (void **)&module->dependencies,
            &module->dependency_capacity,
            module->dependency_count + 1u,
            sizeof(module->dependencies[0]))) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    copy =
        vitte_c17_module_copy_string_n(
            name,
            name_length);

    if (copy == NULL) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    dependency =
        &module->dependencies[
            module->dependency_count];

    memset(
        dependency,
        0,
        sizeof(*dependency));

    dependency->name = copy;
    dependency->name_length =
        name_length;

    dependency->public_dependency =
        public_dependency;

    ++module->dependency_count;

    return true;
}

/* ========================================================================= */
/* Includes                                                                  */
/* ========================================================================= */

static ptrdiff_t
vitte_c17_module_find_include_index(
    const vitte_c17_module_t *module,
    vitte_c17_include_kind_t kind,
    const char *path,
    size_t path_length)
{
    size_t index;

    for (index = 0u;
         index < module->include_count;
         ++index) {
        const vitte_c17_module_include_t *include;

        include =
            &module->includes[index];

        if (include->kind == kind &&
            vitte_c17_module_string_equal(
                include->path,
                include->path_length,
                path,
                path_length)) {
            return (ptrdiff_t)index;
        }
    }

    return -1;
}

bool
vitte_c17_module_add_include(
    vitte_c17_module_t *module,
    vitte_c17_include_kind_t kind,
    const char *path,
    size_t path_length)
{
    vitte_c17_module_include_t *include;
    char *copy;

    if (!vitte_c17_module_is_valid(
            module) ||
        module->state !=
            VITTE_C17_MODULE_STATE_BUILDING ||
        path == NULL ||
        path_length == 0u ||
        path_length >
            VITTE_C17_MODULE_MAX_PATH_BYTES ||
        (kind != VITTE_C17_INCLUDE_SYSTEM &&
         kind != VITTE_C17_INCLUDE_LOCAL)) {
        return false;
    }

    if (vitte_c17_module_find_include_index(
            module,
            kind,
            path,
            path_length) >= 0) {
        if (module->config.deduplicate_includes) {
            return true;
        }

        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_DUPLICATE);
    }

    if (module->include_count ==
        SIZE_MAX) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OVERFLOW);
    }

    if (!vitte_c17_module_reserve_array(
            (void **)&module->includes,
            &module->include_capacity,
            module->include_count + 1u,
            sizeof(module->includes[0]))) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    copy =
        vitte_c17_module_copy_string_n(
            path,
            path_length);

    if (copy == NULL) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    include =
        &module->includes[
            module->include_count];

    memset(
        include,
        0,
        sizeof(*include));

    include->kind = kind;
    include->path = copy;
    include->path_length =
        path_length;

    ++module->include_count;

    return true;
}

/* ========================================================================= */
/* Symbols                                                                   */
/* ========================================================================= */

static ptrdiff_t
vitte_c17_module_find_symbol_index(
    const vitte_c17_module_t *module,
    const char *name,
    size_t name_length)
{
    size_t index;

    for (index = 0u;
         index < module->symbol_count;
         ++index) {
        if (vitte_c17_module_string_equal(
                module->symbols[index].name,
                module->symbols[index].name_length,
                name,
                name_length)) {
            return (ptrdiff_t)index;
        }
    }

    return -1;
}

bool
vitte_c17_module_add_symbol(
    vitte_c17_module_t *module,
    vitte_c17_symbol_kind_t kind,
    const char *name,
    size_t name_length,
    bool exported,
    uint64_t source_id)
{
    vitte_c17_module_symbol_t *symbol;
    ptrdiff_t existing;
    char *copy;

    if (!vitte_c17_module_is_valid(
            module) ||
        module->state !=
            VITTE_C17_MODULE_STATE_BUILDING ||
        name == NULL ||
        name_length == 0u ||
        name_length >
            VITTE_C17_MODULE_MAX_NAME_BYTES ||
        kind <= VITTE_C17_SYMBOL_INVALID ||
        kind >= VITTE_C17_SYMBOL_COUNT) {
        return false;
    }

    existing =
        vitte_c17_module_find_symbol_index(
            module,
            name,
            name_length);

    if (existing >= 0) {
        symbol =
            &module->symbols[
                (size_t)existing];

        if (symbol->kind == kind &&
            symbol->source_id == source_id) {
            if (exported) {
                symbol->exported = true;
            }

            return true;
        }

        if (module->config.reject_duplicate_symbols) {
            return
                vitte_c17_module_fail(
                    module,
                    VITTE_C17_MODULE_ERROR_CONFLICT);
        }
    }

    if (module->symbol_count ==
        SIZE_MAX) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OVERFLOW);
    }

    if (!vitte_c17_module_reserve_array(
            (void **)&module->symbols,
            &module->symbol_capacity,
            module->symbol_count + 1u,
            sizeof(module->symbols[0]))) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    copy =
        vitte_c17_module_copy_string_n(
            name,
            name_length);

    if (copy == NULL) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    symbol =
        &module->symbols[
            module->symbol_count];

    memset(
        symbol,
        0,
        sizeof(*symbol));

    symbol->kind = kind;

    symbol->name = copy;
    symbol->name_length =
        name_length;

    symbol->exported = exported;
    symbol->source_id = source_id;

    ++module->symbol_count;

    return true;
}

/* ========================================================================= */
/* Features                                                                  */
/* ========================================================================= */

static ptrdiff_t
vitte_c17_module_find_feature_index(
    const vitte_c17_module_t *module,
    const char *name,
    size_t name_length)
{
    size_t index;

    for (index = 0u;
         index < module->feature_count;
         ++index) {
        if (vitte_c17_module_string_equal(
                module->features[index].name,
                module->features[index].name_length,
                name,
                name_length)) {
            return (ptrdiff_t)index;
        }
    }

    return -1;
}

bool
vitte_c17_module_require_feature(
    vitte_c17_module_t *module,
    const char *name,
    size_t name_length)
{
    vitte_c17_module_feature_t *feature;
    char *copy;

    if (!vitte_c17_module_is_valid(
            module) ||
        module->state !=
            VITTE_C17_MODULE_STATE_BUILDING ||
        name == NULL ||
        name_length == 0u ||
        name_length >
            VITTE_C17_MODULE_MAX_NAME_BYTES) {
        return false;
    }

    if (vitte_c17_module_find_feature_index(
            module,
            name,
            name_length) >= 0) {
        return true;
    }

    if (module->feature_count ==
        SIZE_MAX) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OVERFLOW);
    }

    if (!vitte_c17_module_reserve_array(
            (void **)&module->features,
            &module->feature_capacity,
            module->feature_count + 1u,
            sizeof(module->features[0]))) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    copy =
        vitte_c17_module_copy_string_n(
            name,
            name_length);

    if (copy == NULL) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    feature =
        &module->features[
            module->feature_count];

    memset(
        feature,
        0,
        sizeof(*feature));

    feature->name = copy;
    feature->name_length =
        name_length;

    ++module->feature_count;

    return true;
}

/* ========================================================================= */
/* Runtime requirements                                                      */
/* ========================================================================= */

bool
vitte_c17_module_require_runtime(
    vitte_c17_module_t *module,
    vitte_c17_runtime_requirement_t requirement)
{
    uint64_t bit;

    if (!vitte_c17_module_is_valid(
            module) ||
        module->state !=
            VITTE_C17_MODULE_STATE_BUILDING ||
        requirement <= VITTE_C17_RUNTIME_NONE ||
        requirement >= VITTE_C17_RUNTIME_COUNT) {
        return false;
    }

    if ((unsigned)requirement >= 64u) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OVERFLOW);
    }

    bit =
        UINT64_C(1) <<
        (unsigned)requirement;

    module->runtime_requirements |= bit;

    return true;
}

bool
vitte_c17_module_requires_runtime(
    const vitte_c17_module_t *module,
    vitte_c17_runtime_requirement_t requirement)
{
    uint64_t bit;

    if (!vitte_c17_module_is_valid(
            module) ||
        requirement <= VITTE_C17_RUNTIME_NONE ||
        requirement >= VITTE_C17_RUNTIME_COUNT ||
        (unsigned)requirement >= 64u) {
        return false;
    }

    bit =
        UINT64_C(1) <<
        (unsigned)requirement;

    return
        (module->runtime_requirements & bit) !=
        0u;
}

/* ========================================================================= */
/* Fragments                                                                 */
/* ========================================================================= */

static bool
vitte_c17_module_fragment_equal(
    const vitte_c17_module_fragment_t *fragment,
    vitte_c17_section_t section,
    const char *text,
    size_t length)
{
    return
        fragment != NULL &&
        fragment->section == section &&
        vitte_c17_module_string_equal(
            fragment->text,
            fragment->length,
            text,
            length);
}

bool
vitte_c17_module_add_fragment(
    vitte_c17_module_t *module,
    vitte_c17_section_t section,
    const char *text,
    size_t length,
    uint64_t source_id)
{
    vitte_c17_module_fragment_t *fragment;
    size_t index;
    char *copy;

    if (!vitte_c17_module_is_valid(
            module) ||
        module->state !=
            VITTE_C17_MODULE_STATE_BUILDING ||
        section <= VITTE_C17_SECTION_NONE ||
        section >= VITTE_C17_SECTION_COUNT ||
        text == NULL ||
        length >
            VITTE_C17_MODULE_MAX_FRAGMENT_BYTES) {
        return false;
    }

    if (module->config.reject_duplicate_fragments) {
        for (index = 0u;
             index < module->fragment_count;
             ++index) {
            if (vitte_c17_module_fragment_equal(
                    &module->fragments[index],
                    section,
                    text,
                    length)) {
                return
                    vitte_c17_module_fail(
                        module,
                        VITTE_C17_MODULE_ERROR_DUPLICATE);
            }
        }
    }

    if (module->fragment_count ==
        SIZE_MAX) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OVERFLOW);
    }

    if (!vitte_c17_module_reserve_array(
            (void **)&module->fragments,
            &module->fragment_capacity,
            module->fragment_count + 1u,
            sizeof(module->fragments[0]))) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    copy =
        vitte_c17_module_copy_string_n(
            text,
            length);

    if (copy == NULL) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_OUT_OF_MEMORY);
    }

    fragment =
        &module->fragments[
            module->fragment_count];

    memset(
        fragment,
        0,
        sizeof(*fragment));

    fragment->section = section;
    fragment->text = copy;
    fragment->length = length;
    fragment->source_id = source_id;

    /*
     * Preserve insertion order even when deterministic sorting is enabled.
     * It provides a stable final tie breaker.
     */
    fragment->sequence =
        module->next_fragment_sequence++;

    ++module->fragment_count;

    module->fragment_bytes =
        vitte_c17_module_saturating_add(
            module->fragment_bytes,
            length);

    return true;
}

/* ========================================================================= */
/* Deterministic comparators                                                 */
/* ========================================================================= */

static int
vitte_c17_module_dependency_compare(
    const void *left_pointer,
    const void *right_pointer)
{
    const vitte_c17_module_dependency_t *left;
    const vitte_c17_module_dependency_t *right;

    left =
        (const vitte_c17_module_dependency_t *)
            left_pointer;

    right =
        (const vitte_c17_module_dependency_t *)
            right_pointer;

    return
        vitte_c17_module_string_compare(
            left->name,
            left->name_length,
            right->name,
            right->name_length);
}

static int
vitte_c17_module_include_compare(
    const void *left_pointer,
    const void *right_pointer)
{
    const vitte_c17_module_include_t *left;
    const vitte_c17_module_include_t *right;
    int comparison;

    left =
        (const vitte_c17_module_include_t *)
            left_pointer;

    right =
        (const vitte_c17_module_include_t *)
            right_pointer;

    if (left->kind < right->kind) {
        return -1;
    }

    if (left->kind > right->kind) {
        return 1;
    }

    comparison =
        vitte_c17_module_string_compare(
            left->path,
            left->path_length,
            right->path,
            right->path_length);

    return comparison;
}

static int
vitte_c17_module_symbol_compare(
    const void *left_pointer,
    const void *right_pointer)
{
    const vitte_c17_module_symbol_t *left;
    const vitte_c17_module_symbol_t *right;
    int comparison;

    left =
        (const vitte_c17_module_symbol_t *)
            left_pointer;

    right =
        (const vitte_c17_module_symbol_t *)
            right_pointer;

    comparison =
        vitte_c17_module_string_compare(
            left->name,
            left->name_length,
            right->name,
            right->name_length);

    if (comparison != 0) {
        return comparison;
    }

    if (left->kind < right->kind) {
        return -1;
    }

    if (left->kind > right->kind) {
        return 1;
    }

    if (left->source_id < right->source_id) {
        return -1;
    }

    if (left->source_id > right->source_id) {
        return 1;
    }

    return 0;
}

static int
vitte_c17_module_feature_compare(
    const void *left_pointer,
    const void *right_pointer)
{
    const vitte_c17_module_feature_t *left;
    const vitte_c17_module_feature_t *right;

    left =
        (const vitte_c17_module_feature_t *)
            left_pointer;

    right =
        (const vitte_c17_module_feature_t *)
            right_pointer;

    return
        vitte_c17_module_string_compare(
            left->name,
            left->name_length,
            right->name,
            right->name_length);
}

static int
vitte_c17_module_fragment_compare(
    const void *left_pointer,
    const void *right_pointer)
{
    const vitte_c17_module_fragment_t *left;
    const vitte_c17_module_fragment_t *right;

    left =
        (const vitte_c17_module_fragment_t *)
            left_pointer;

    right =
        (const vitte_c17_module_fragment_t *)
            right_pointer;

    if (left->section < right->section) {
        return -1;
    }

    if (left->section > right->section) {
        return 1;
    }

    if (left->source_id < right->source_id) {
        return -1;
    }

    if (left->source_id > right->source_id) {
        return 1;
    }

    if (left->sequence < right->sequence) {
        return -1;
    }

    if (left->sequence > right->sequence) {
        return 1;
    }

    return 0;
}

/* ========================================================================= */
/* Sorting                                                                   */
/* ========================================================================= */

static void
vitte_c17_module_sort(
    vitte_c17_module_t *module)
{
    if (!module->config.deterministic) {
        return;
    }

    if (module->dependency_count > 1u) {
        qsort(
            module->dependencies,
            module->dependency_count,
            sizeof(module->dependencies[0]),
            vitte_c17_module_dependency_compare);
    }

    if (module->include_count > 1u) {
        qsort(
            module->includes,
            module->include_count,
            sizeof(module->includes[0]),
            vitte_c17_module_include_compare);
    }

    if (module->symbol_count > 1u) {
        qsort(
            module->symbols,
            module->symbol_count,
            sizeof(module->symbols[0]),
            vitte_c17_module_symbol_compare);
    }

    if (module->feature_count > 1u) {
        qsort(
            module->features,
            module->feature_count,
            sizeof(module->features[0]),
            vitte_c17_module_feature_compare);
    }

    if (module->fragment_count > 1u) {
        qsort(
            module->fragments,
            module->fragment_count,
            sizeof(module->fragments[0]),
            vitte_c17_module_fragment_compare);
    }
}

/* ========================================================================= */
/* Seal                                                                      */
/* ========================================================================= */

bool
vitte_c17_module_seal(
    vitte_c17_module_t *module)
{
    if (!vitte_c17_module_is_valid(
            module)) {
        return false;
    }

    if (module->state ==
        VITTE_C17_MODULE_STATE_SEALED) {
        return true;
    }

    if (module->state !=
        VITTE_C17_MODULE_STATE_BUILDING) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_INVALID_STATE);
    }

    vitte_c17_module_sort(module);

    module->state =
        VITTE_C17_MODULE_STATE_SEALED;

    module->last_error =
        VITTE_C17_MODULE_ERROR_NONE;

    return true;
}

/* ========================================================================= */
/* Include emission                                                          */
/* ========================================================================= */

static bool
vitte_c17_module_emit_include(
    vitte_c17_backend_t *backend,
    const vitte_c17_module_include_t *include)
{
    if (!vitte_c17_backend_write(
            backend,
            "#include ")) {
        return false;
    }

    if (include->kind ==
        VITTE_C17_INCLUDE_SYSTEM) {
        if (!vitte_c17_backend_write_char(
                backend,
                '<')) {
            return false;
        }

        if (!vitte_c17_backend_write_n(
                backend,
                include->path,
                include->path_length)) {
            return false;
        }

        if (!vitte_c17_backend_write_char(
                backend,
                '>')) {
            return false;
        }
    } else {
        if (!vitte_c17_backend_write_char(
                backend,
                '"')) {
            return false;
        }

        if (!vitte_c17_backend_write_n(
                backend,
                include->path,
                include->path_length)) {
            return false;
        }

        if (!vitte_c17_backend_write_char(
                backend,
                '"')) {
            return false;
        }
    }

    return
        vitte_c17_backend_newline(
            backend);
}

/* ========================================================================= */
/* Feature emission                                                          */
/* ========================================================================= */

static bool
vitte_c17_module_emit_feature(
    vitte_c17_backend_t *backend,
    const vitte_c17_module_feature_t *feature)
{
    size_t index;

    if (!vitte_c17_backend_write(
            backend,
            "#ifndef VITTE_FEATURE_")) {
        return false;
    }

    for (index = 0u;
         index < feature->name_length;
         ++index) {
        unsigned char c;
        char out;

        c =
            (unsigned char)
                feature->name[index];

        if ((c >= 'a' && c <= 'z')) {
            out =
                (char)(c - 'a' + 'A');
        } else if (
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            out = (char)c;
        } else {
            out = '_';
        }

        if (!vitte_c17_backend_write_char(
                backend,
                out)) {
            return false;
        }
    }

    if (!vitte_c17_backend_newline(
            backend)) {
        return false;
    }

    if (!vitte_c17_backend_write(
            backend,
            "#define VITTE_FEATURE_")) {
        return false;
    }

    for (index = 0u;
         index < feature->name_length;
         ++index) {
        unsigned char c;
        char out;

        c =
            (unsigned char)
                feature->name[index];

        if (c >= 'a' && c <= 'z') {
            out =
                (char)(c - 'a' + 'A');
        } else if (
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            out = (char)c;
        } else {
            out = '_';
        }

        if (!vitte_c17_backend_write_char(
                backend,
                out)) {
            return false;
        }
    }

    if (!vitte_c17_backend_write(
            backend,
            " 1\n#endif\n")) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Fragment emission                                                         */
/* ========================================================================= */

static bool
vitte_c17_module_emit_fragment(
    vitte_c17_backend_t *backend,
    const vitte_c17_module_fragment_t *fragment)
{
    if (fragment->length == 0u) {
        return true;
    }

    if (!vitte_c17_backend_write_n(
            backend,
            fragment->text,
            fragment->length)) {
        return false;
    }

    /*
     * Normalize fragment separation without modifying fragment contents.
     */
    if (fragment->text[
            fragment->length - 1u] != '\n') {
        if (!vitte_c17_backend_newline(
                backend)) {
            return false;
        }
    }

    return true;
}

/* ========================================================================= */
/* Section presence                                                          */
/* ========================================================================= */

size_t
vitte_c17_module_section_fragment_count(
    const vitte_c17_module_t *module,
    vitte_c17_section_t section)
{
    size_t index;
    size_t count;

    if (!vitte_c17_module_is_valid(
            module) ||
        section <= VITTE_C17_SECTION_NONE ||
        section >= VITTE_C17_SECTION_COUNT) {
        return 0u;
    }

    count = 0u;

    for (index = 0u;
         index < module->fragment_count;
         ++index) {
        if (module->fragments[index].section ==
            section) {
            count =
                vitte_c17_module_saturating_add(
                    count,
                    1u);
        }
    }

    return count;
}

/* ========================================================================= */
/* Emit special sections                                                     */
/* ========================================================================= */

static bool
vitte_c17_module_emit_feature_section(
    vitte_c17_module_t *module,
    vitte_c17_backend_t *backend)
{
    size_t index;

    if (module->feature_count == 0u &&
        !module->config.emit_empty_sections) {
        return true;
    }

    if (!vitte_c17_backend_begin_section(
            backend,
            VITTE_C17_SECTION_FEATURE_MACROS)) {
        return false;
    }

    for (index = 0u;
         index < module->feature_count;
         ++index) {
        if (!vitte_c17_module_emit_feature(
                backend,
                &module->features[index])) {
            return false;
        }
    }

    return
        vitte_c17_backend_end_section(
            backend);
}

static bool
vitte_c17_module_emit_include_section(
    vitte_c17_module_t *module,
    vitte_c17_backend_t *backend)
{
    size_t index;

    if (module->include_count == 0u &&
        !module->config.emit_empty_sections) {
        return true;
    }

    if (!vitte_c17_backend_begin_section(
            backend,
            VITTE_C17_SECTION_INCLUDES)) {
        return false;
    }

    for (index = 0u;
         index < module->include_count;
         ++index) {
        if (!vitte_c17_module_emit_include(
                backend,
                &module->includes[index])) {
            return false;
        }
    }

    return
        vitte_c17_backend_end_section(
            backend);
}

/* ========================================================================= */
/* Generic section emission                                                  */
/* ========================================================================= */

static bool
vitte_c17_module_emit_fragment_section(
    vitte_c17_module_t *module,
    vitte_c17_backend_t *backend,
    vitte_c17_section_t section)
{
    size_t index;
    size_t count;

    count =
        vitte_c17_module_section_fragment_count(
            module,
            section);

    if (count == 0u &&
        !module->config.emit_empty_sections) {
        return true;
    }

    if (!vitte_c17_backend_begin_section(
            backend,
            section)) {
        return false;
    }

    for (index = 0u;
         index < module->fragment_count;
         ++index) {
        vitte_c17_module_fragment_t *fragment;

        fragment =
            &module->fragments[index];

        if (fragment->section !=
            section) {
            continue;
        }

        if (!vitte_c17_module_emit_fragment(
                backend,
                fragment)) {
            return false;
        }

        module->stats.emitted_fragments =
            vitte_c17_module_saturating_add(
                module->stats.emitted_fragments,
                1u);

        module->stats.emitted_fragment_bytes =
            vitte_c17_module_saturating_add(
                module->stats.emitted_fragment_bytes,
                fragment->length);
    }

    return
        vitte_c17_backend_end_section(
            backend);
}

/* ========================================================================= */
/* Module banner                                                             */
/* ========================================================================= */

static bool
vitte_c17_module_emit_identity(
    vitte_c17_module_t *module,
    vitte_c17_backend_t *backend)
{
    if (!backend->config.emit_comments) {
        return true;
    }

    if (!vitte_c17_backend_comment(
            backend,
            "Vitte module")) {
        return false;
    }

    if (!vitte_c17_backend_newline(
            backend)) {
        return false;
    }

    if (!vitte_c17_backend_write(
            backend,
            "/* module: ")) {
        return false;
    }

    if (!vitte_c17_backend_write_n(
            backend,
            module->name,
            module->name_length)) {
        return false;
    }

    if (!vitte_c17_backend_write(
            backend,
            " */\n")) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Emission                                                                  */
/* ========================================================================= */

bool
vitte_c17_module_emit(
    vitte_c17_module_t *module,
    vitte_c17_backend_t *backend)
{
    static const vitte_c17_section_t fragment_sections[] = {
        VITTE_C17_SECTION_FORWARD_DECLARATIONS,
        VITTE_C17_SECTION_TYPE_DECLARATIONS,
        VITTE_C17_SECTION_GLOBAL_DECLARATIONS,
        VITTE_C17_SECTION_RUNTIME_DECLARATIONS,
        VITTE_C17_SECTION_FUNCTION_DECLARATIONS,
        VITTE_C17_SECTION_RUNTIME_DEFINITIONS,
        VITTE_C17_SECTION_GLOBAL_DEFINITIONS,
        VITTE_C17_SECTION_FUNCTION_DEFINITIONS,
        VITTE_C17_SECTION_ENTRY,
        VITTE_C17_SECTION_EPILOGUE
    };

    size_t index;

    if (!vitte_c17_module_is_valid(
            module) ||
        !vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    if (module->state ==
        VITTE_C17_MODULE_STATE_BUILDING) {
        if (!vitte_c17_module_seal(
                module)) {
            return false;
        }
    }

    if (module->state !=
        VITTE_C17_MODULE_STATE_SEALED) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_INVALID_STATE);
    }

    if (!vitte_c17_backend_is_generating(
            backend)) {
        return
            vitte_c17_module_fail(
                module,
                VITTE_C17_MODULE_ERROR_INVALID_STATE);
    }

    module->state =
        VITTE_C17_MODULE_STATE_EMITTING;

    memset(
        &module->stats,
        0,
        sizeof(module->stats));

    /*
     * backend_begin() may already have emitted the global BANNER section.
     * Do not attempt to open BANNER here.
     */
    if (!vitte_c17_module_emit_identity(
            module,
            backend)) {
        goto output_failure;
    }

    if (!vitte_c17_module_emit_feature_section(
            module,
            backend)) {
        goto output_failure;
    }

    if (!vitte_c17_module_emit_include_section(
            module,
            backend)) {
        goto output_failure;
    }

    for (index = 0u;
         index <
            sizeof(fragment_sections) /
            sizeof(fragment_sections[0]);
         ++index) {
        if (!vitte_c17_module_emit_fragment_section(
                module,
                backend,
                fragment_sections[index])) {
            goto output_failure;
        }
    }

    module->stats.dependency_count =
        module->dependency_count;

    module->stats.include_count =
        module->include_count;

    module->stats.symbol_count =
        module->symbol_count;

    module->stats.feature_count =
        module->feature_count;

    module->stats.fragment_count =
        module->fragment_count;

    module->stats.fragment_bytes =
        module->fragment_bytes;

    module->state =
        VITTE_C17_MODULE_STATE_EMITTED;

    module->last_error =
        VITTE_C17_MODULE_ERROR_NONE;

    return true;

output_failure:

    module->state =
        VITTE_C17_MODULE_STATE_FAILED;

    module->last_error =
        VITTE_C17_MODULE_ERROR_OUTPUT;

    return false;
}

/* ========================================================================= */
/* Backend driver adapter                                                    */
/* ========================================================================= */

bool
vitte_c17_module_emit_adapter(
    vitte_c17_backend_t *backend,
    const void *translation_unit,
    void *user_data)
{
    vitte_c17_module_t *module;

    (void)user_data;

    if (translation_unit == NULL) {
        return false;
    }

    /*
     * The generic backend translation-unit API is intentionally opaque.
     * For this adapter the opaque object is a vitte_c17_module_t.
     */
    module =
        (vitte_c17_module_t *)
            (uintptr_t)translation_unit;

    return
        vitte_c17_module_emit(
            module,
            backend);
}

bool
vitte_c17_module_generate(
    vitte_c17_module_t *module,
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_module_is_valid(
            module) ||
        !vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    return
        vitte_c17_backend_generate(
            backend,
            module,
            vitte_c17_module_emit_adapter,
            NULL);
}

/* ========================================================================= */
/* Lookups                                                                   */
/* ========================================================================= */

const vitte_c17_module_dependency_t *
vitte_c17_module_find_dependency(
    const vitte_c17_module_t *module,
    const char *name,
    size_t name_length)
{
    ptrdiff_t index;

    if (!vitte_c17_module_is_valid(
            module) ||
        name == NULL) {
        return NULL;
    }

    index =
        vitte_c17_module_find_dependency_index(
            module,
            name,
            name_length);

    if (index < 0) {
        return NULL;
    }

    return
        &module->dependencies[
            (size_t)index];
}

const vitte_c17_module_symbol_t *
vitte_c17_module_find_symbol(
    const vitte_c17_module_t *module,
    const char *name,
    size_t name_length)
{
    ptrdiff_t index;

    if (!vitte_c17_module_is_valid(
            module) ||
        name == NULL) {
        return NULL;
    }

    index =
        vitte_c17_module_find_symbol_index(
            module,
            name,
            name_length);

    if (index < 0) {
        return NULL;
    }

    return
        &module->symbols[
            (size_t)index];
}

bool
vitte_c17_module_has_feature(
    const vitte_c17_module_t *module,
    const char *name,
    size_t name_length)
{
    if (!vitte_c17_module_is_valid(
            module) ||
        name == NULL) {
        return false;
    }

    return
        vitte_c17_module_find_feature_index(
            module,
            name,
            name_length) >= 0;
}

/* ========================================================================= */
/* Accessors                                                                 */
/* ========================================================================= */

const char *
vitte_c17_module_name(
    const vitte_c17_module_t *module)
{
    if (!vitte_c17_module_is_valid(
            module)) {
        return NULL;
    }

    return module->name;
}

size_t
vitte_c17_module_name_length(
    const vitte_c17_module_t *module)
{
    if (!vitte_c17_module_is_valid(
            module)) {
        return 0u;
    }

    return module->name_length;
}

const char *
vitte_c17_module_source_path(
    const vitte_c17_module_t *module)
{
    if (!vitte_c17_module_is_valid(
            module)) {
        return NULL;
    }

    return module->source_path;
}

uint32_t
vitte_c17_module_source_file_id(
    const vitte_c17_module_t *module)
{
    if (!vitte_c17_module_is_valid(
            module)) {
        return 0u;
    }

    return module->source_file_id;
}

vitte_c17_module_state_t
vitte_c17_module_state(
    const vitte_c17_module_t *module)
{
    if (module == NULL ||
        module->magic !=
            VITTE_C17_MODULE_MAGIC) {
        return
            VITTE_C17_MODULE_STATE_UNINITIALIZED;
    }

    return module->state;
}

vitte_c17_module_error_t
vitte_c17_module_last_error(
    const vitte_c17_module_t *module)
{
    if (module == NULL ||
        module->magic !=
            VITTE_C17_MODULE_MAGIC) {
        return
            VITTE_C17_MODULE_ERROR_INVALID_MODULE;
    }

    return module->last_error;
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_c17_module_stats_t
vitte_c17_module_stats(
    const vitte_c17_module_t *module)
{
    vitte_c17_module_stats_t stats;

    memset(
        &stats,
        0,
        sizeof(stats));

    if (!vitte_c17_module_is_valid(
            module)) {
        return stats;
    }

    stats = module->stats;

    stats.dependency_count =
        module->dependency_count;

    stats.include_count =
        module->include_count;

    stats.symbol_count =
        module->symbol_count;

    stats.feature_count =
        module->feature_count;

    stats.fragment_count =
        module->fragment_count;

    stats.fragment_bytes =
        module->fragment_bytes;

    return stats;
}

/* ========================================================================= */
/* Validation helpers                                                        */
/* ========================================================================= */

static bool
vitte_c17_module_validate_dependencies(
    const vitte_c17_module_t *module)
{
    size_t index;
    size_t other;

    if (module->dependency_count >
        module->dependency_capacity) {
        return false;
    }

    if (module->dependency_count != 0u &&
        module->dependencies == NULL) {
        return false;
    }

    for (index = 0u;
         index < module->dependency_count;
         ++index) {
        const vitte_c17_module_dependency_t *dependency;

        dependency =
            &module->dependencies[index];

        if (dependency->name == NULL ||
            dependency->name_length == 0u) {
            return false;
        }

        for (other = index + 1u;
             other < module->dependency_count;
             ++other) {
            if (vitte_c17_module_string_equal(
                    dependency->name,
                    dependency->name_length,
                    module->dependencies[other].name,
                    module->dependencies[other].name_length)) {
                return false;
            }
        }
    }

    return true;
}

static bool
vitte_c17_module_validate_includes(
    const vitte_c17_module_t *module)
{
    size_t index;
    size_t other;

    if (module->include_count >
        module->include_capacity) {
        return false;
    }

    if (module->include_count != 0u &&
        module->includes == NULL) {
        return false;
    }

    for (index = 0u;
         index < module->include_count;
         ++index) {
        const vitte_c17_module_include_t *include;

        include =
            &module->includes[index];

        if (include->path == NULL ||
            include->path_length == 0u) {
            return false;
        }

        if (include->kind !=
                VITTE_C17_INCLUDE_SYSTEM &&
            include->kind !=
                VITTE_C17_INCLUDE_LOCAL) {
            return false;
        }

        for (other = index + 1u;
             other < module->include_count;
             ++other) {
            if (include->kind ==
                    module->includes[other].kind &&
                vitte_c17_module_string_equal(
                    include->path,
                    include->path_length,
                    module->includes[other].path,
                    module->includes[other].path_length)) {
                return false;
            }
        }
    }

    return true;
}

static bool
vitte_c17_module_validate_symbols(
    const vitte_c17_module_t *module)
{
    size_t index;

    if (module->symbol_count >
        module->symbol_capacity) {
        return false;
    }

    if (module->symbol_count != 0u &&
        module->symbols == NULL) {
        return false;
    }

    for (index = 0u;
         index < module->symbol_count;
         ++index) {
        const vitte_c17_module_symbol_t *symbol;

        symbol =
            &module->symbols[index];

        if (symbol->name == NULL ||
            symbol->name_length == 0u ||
            symbol->kind <=
                VITTE_C17_SYMBOL_INVALID ||
            symbol->kind >=
                VITTE_C17_SYMBOL_COUNT) {
            return false;
        }
    }

    return true;
}

static bool
vitte_c17_module_validate_features(
    const vitte_c17_module_t *module)
{
    size_t index;
    size_t other;

    if (module->feature_count >
        module->feature_capacity) {
        return false;
    }

    if (module->feature_count != 0u &&
        module->features == NULL) {
        return false;
    }

    for (index = 0u;
         index < module->feature_count;
         ++index) {
        if (module->features[index].name == NULL ||
            module->features[index].name_length == 0u) {
            return false;
        }

        for (other = index + 1u;
             other < module->feature_count;
             ++other) {
            if (vitte_c17_module_string_equal(
                    module->features[index].name,
                    module->features[index].name_length,
                    module->features[other].name,
                    module->features[other].name_length)) {
                return false;
            }
        }
    }

    return true;
}

static bool
vitte_c17_module_validate_fragments(
    const vitte_c17_module_t *module)
{
    size_t index;
    size_t total;

    if (module->fragment_count >
        module->fragment_capacity) {
        return false;
    }

    if (module->fragment_count != 0u &&
        module->fragments == NULL) {
        return false;
    }

    total = 0u;

    for (index = 0u;
         index < module->fragment_count;
         ++index) {
        const vitte_c17_module_fragment_t *fragment;

        fragment =
            &module->fragments[index];

        if (fragment->text == NULL ||
            fragment->section <=
                VITTE_C17_SECTION_NONE ||
            fragment->section >=
                VITTE_C17_SECTION_COUNT) {
            return false;
        }

        if (vitte_c17_module_add_overflow(
                total,
                fragment->length,
                &total)) {
            return false;
        }
    }

    if (module->fragment_bytes != SIZE_MAX &&
        total != module->fragment_bytes) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Deep validation                                                          */
/* ========================================================================= */

bool
vitte_c17_module_validate(
    const vitte_c17_module_t *module)
{
    if (!vitte_c17_module_is_valid(
            module)) {
        return false;
    }

    if (module->name_length >
        VITTE_C17_MODULE_MAX_NAME_BYTES) {
        return false;
    }

    if (module->source_path == NULL &&
        module->source_path_length != 0u) {
        return false;
    }

    if (module->source_path_length >
        VITTE_C17_MODULE_MAX_PATH_BYTES) {
        return false;
    }

    if (!vitte_c17_module_validate_dependencies(
            module)) {
        return false;
    }

    if (!vitte_c17_module_validate_includes(
            module)) {
        return false;
    }

    if (!vitte_c17_module_validate_symbols(
            module)) {
        return false;
    }

    if (!vitte_c17_module_validate_features(
            module)) {
        return false;
    }

    if (!vitte_c17_module_validate_fragments(
            module)) {
        return false;
    }

    if (module->state ==
            VITTE_C17_MODULE_STATE_EMITTED &&
        module->stats.emitted_fragments >
            module->fragment_count) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* End                                                                       */
/* ========================================================================= */
