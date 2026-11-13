/*
 * Vitte Compiler
 * src/backend/c17/options.c
 *
 * Canonical option layer for the ISO C17 backend.
 *
 * Responsibilities:
 *
 *   - backend option defaults
 *   - option presets
 *   - validation
 *   - normalization
 *   - target-independent policy
 *   - debug/release/size/sanitize profiles
 *   - optimization policy
 *   - output policy
 *   - diagnostics policy
 *   - source-map policy
 *   - runtime policy
 *   - naming policy
 *   - deterministic-build policy
 *   - safety policy
 *   - resource limits
 *   - conversion to vitte_c17_backend_config_t
 *   - stable option hashing
 *   - equality/comparison
 *   - statistics/introspection
 *
 * This layer describes policy. backend.c remains responsible for generation.
 */

#include "options.h"

#include "backend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

#ifndef VITTE_C17_OPTIONS_MAGIC
#define VITTE_C17_OPTIONS_MAGIC UINT64_C(0x56495454454F5054)
#endif

#ifndef VITTE_C17_OPTIONS_DEAD_MAGIC
#define VITTE_C17_OPTIONS_DEAD_MAGIC UINT64_C(0x444541444F505421)
#endif

#ifndef VITTE_C17_OPTIONS_DEFAULT_INDENT_WIDTH
#define VITTE_C17_OPTIONS_DEFAULT_INDENT_WIDTH ((size_t)4u)
#endif

#ifndef VITTE_C17_OPTIONS_DEFAULT_MAX_INDENT_DEPTH
#define VITTE_C17_OPTIONS_DEFAULT_MAX_INDENT_DEPTH ((size_t)4096u)
#endif

#ifndef VITTE_C17_OPTIONS_DEFAULT_MAX_IDENTIFIER_BYTES
#define VITTE_C17_OPTIONS_DEFAULT_MAX_IDENTIFIER_BYTES ((size_t)4096u)
#endif

#ifndef VITTE_C17_OPTIONS_DEFAULT_MAX_ERRORS
#define VITTE_C17_OPTIONS_DEFAULT_MAX_ERRORS ((size_t)100u)
#endif

#ifndef VITTE_C17_OPTIONS_DEFAULT_MAX_OUTPUT_BYTES
#define VITTE_C17_OPTIONS_DEFAULT_MAX_OUTPUT_BYTES SIZE_MAX
#endif

#ifndef VITTE_C17_OPTIONS_MAX_INDENT_WIDTH
#define VITTE_C17_OPTIONS_MAX_INDENT_WIDTH ((size_t)32u)
#endif

#ifndef VITTE_C17_OPTIONS_MAX_INDENT_DEPTH
#define VITTE_C17_OPTIONS_MAX_INDENT_DEPTH ((size_t)(1024u * 1024u))
#endif

#ifndef VITTE_C17_OPTIONS_MAX_IDENTIFIER_BYTES
#define VITTE_C17_OPTIONS_MAX_IDENTIFIER_BYTES ((size_t)(1024u * 1024u))
#endif

#ifndef VITTE_C17_OPTIONS_HASH_OFFSET
#define VITTE_C17_OPTIONS_HASH_OFFSET UINT64_C(14695981039346656037)
#endif

#ifndef VITTE_C17_OPTIONS_HASH_PRIME
#define VITTE_C17_OPTIONS_HASH_PRIME UINT64_C(1099511628211)
#endif

/* ========================================================================= */
/* Internal hash                                                             */
/* ========================================================================= */

static uint64_t
vitte_c17_options_hash_bytes_internal(
    uint64_t hash,
    const void *data,
    size_t length)
{
    const unsigned char *bytes;
    size_t index;

    if (data == NULL) {
        return hash;
    }

    bytes = (const unsigned char *)data;

    for (index = 0u;
         index < length;
         ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= VITTE_C17_OPTIONS_HASH_PRIME;
    }

    return hash;
}

static uint64_t
vitte_c17_options_hash_u64(
    uint64_t hash,
    uint64_t value)
{
    unsigned char bytes[8];
    size_t index;

    /*
     * Explicit little-endian serialization makes the option fingerprint
     * independent of host byte order.
     */
    for (index = 0u;
         index < sizeof(bytes);
         ++index) {
        bytes[index] =
            (unsigned char)(
                (value >> (index * 8u)) &
                UINT64_C(0xff));
    }

    return
        vitte_c17_options_hash_bytes_internal(
            hash,
            bytes,
            sizeof(bytes));
}

static uint64_t
vitte_c17_options_hash_size(
    uint64_t hash,
    size_t value)
{
    return
        vitte_c17_options_hash_u64(
            hash,
            (uint64_t)value);
}

static uint64_t
vitte_c17_options_hash_bool(
    uint64_t hash,
    bool value)
{
    const unsigned char byte =
        value
            ? (unsigned char)1u
            : (unsigned char)0u;

    return
        vitte_c17_options_hash_bytes_internal(
            hash,
            &byte,
            1u);
}

static uint64_t
vitte_c17_options_hash_enum(
    uint64_t hash,
    unsigned value)
{
    return
        vitte_c17_options_hash_u64(
            hash,
            (uint64_t)value);
}

/* ========================================================================= */
/* Error names                                                               */
/* ========================================================================= */

const char *
vitte_c17_options_error_name(
    vitte_c17_options_error_t error)
{
    switch (error) {
        case VITTE_C17_OPTIONS_ERROR_NONE:
            return "none";

        case VITTE_C17_OPTIONS_ERROR_INVALID_OPTIONS:
            return "invalid-options";

        case VITTE_C17_OPTIONS_ERROR_INVALID_ARGUMENT:
            return "invalid-argument";

        case VITTE_C17_OPTIONS_ERROR_INVALID_PROFILE:
            return "invalid-profile";

        case VITTE_C17_OPTIONS_ERROR_INVALID_DIALECT:
            return "invalid-dialect";

        case VITTE_C17_OPTIONS_ERROR_INVALID_OPTIMIZATION:
            return "invalid-optimization";

        case VITTE_C17_OPTIONS_ERROR_INVALID_RUNTIME:
            return "invalid-runtime";

        case VITTE_C17_OPTIONS_ERROR_INVALID_LIMIT:
            return "invalid-limit";

        case VITTE_C17_OPTIONS_ERROR_CONFLICT:
            return "conflict";

        case VITTE_C17_OPTIONS_ERROR_UNSUPPORTED:
            return "unsupported";

        case VITTE_C17_OPTIONS_ERROR_CORRUPTION:
            return "corruption";

        case VITTE_C17_OPTIONS_ERROR_COUNT:
            return "count";

        default:
            return "unknown";
    }
}

/* ========================================================================= */
/* Profile names                                                             */
/* ========================================================================= */

const char *
vitte_c17_profile_name(
    vitte_c17_profile_t profile)
{
    switch (profile) {
        case VITTE_C17_PROFILE_DEFAULT:
            return "default";

        case VITTE_C17_PROFILE_DEBUG:
            return "debug";

        case VITTE_C17_PROFILE_RELEASE:
            return "release";

        case VITTE_C17_PROFILE_SIZE:
            return "size";

        case VITTE_C17_PROFILE_SANITIZE:
            return "sanitize";

        case VITTE_C17_PROFILE_FREESTANDING:
            return "freestanding";

        case VITTE_C17_PROFILE_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Dialect names                                                             */
/* ========================================================================= */

const char *
vitte_c17_dialect_name(
    vitte_c17_dialect_t dialect)
{
    switch (dialect) {
        case VITTE_C17_DIALECT_C17:
            return "c17";

        case VITTE_C17_DIALECT_GNU17:
            return "gnu17";

        case VITTE_C17_DIALECT_INVALID:
            return "invalid";

        case VITTE_C17_DIALECT_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Optimization names                                                        */
/* ========================================================================= */

const char *
vitte_c17_optimization_name(
    vitte_c17_optimization_t optimization)
{
    switch (optimization) {
        case VITTE_C17_OPTIMIZATION_NONE:
            return "none";

        case VITTE_C17_OPTIMIZATION_DEBUG:
            return "debug";

        case VITTE_C17_OPTIMIZATION_BALANCED:
            return "balanced";

        case VITTE_C17_OPTIMIZATION_SPEED:
            return "speed";

        case VITTE_C17_OPTIMIZATION_SIZE:
            return "size";

        case VITTE_C17_OPTIMIZATION_AGGRESSIVE:
            return "aggressive";

        case VITTE_C17_OPTIMIZATION_INVALID:
            return "invalid";

        case VITTE_C17_OPTIMIZATION_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Runtime mode names                                                        */
/* ========================================================================= */

const char *
vitte_c17_runtime_mode_name(
    vitte_c17_runtime_mode_t mode)
{
    switch (mode) {
        case VITTE_C17_RUNTIME_MODE_AUTO:
            return "auto";

        case VITTE_C17_RUNTIME_MODE_EMBEDDED:
            return "embedded";

        case VITTE_C17_RUNTIME_MODE_EXTERNAL:
            return "external";

        case VITTE_C17_RUNTIME_MODE_NONE:
            return "none";

        case VITTE_C17_RUNTIME_MODE_INVALID:
            return "invalid";

        case VITTE_C17_RUNTIME_MODE_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Error helper                                                              */
/* ========================================================================= */

static bool
vitte_c17_options_fail(
    vitte_c17_options_t *options,
    vitte_c17_options_error_t error)
{
    if (options != NULL &&
        options->magic ==
            VITTE_C17_OPTIONS_MAGIC) {
        options->last_error = error;
    }

    return false;
}

/* ========================================================================= */
/* Defaults                                                                  */
/* ========================================================================= */

static void
vitte_c17_options_set_policy_defaults(
    vitte_c17_options_t *options)
{
    options->dialect =
        VITTE_C17_DIALECT_C17;

    options->optimization =
        VITTE_C17_OPTIMIZATION_NONE;

    options->runtime_mode =
        VITTE_C17_RUNTIME_MODE_AUTO;

    /* Formatting */
    options->indent_width =
        VITTE_C17_OPTIONS_DEFAULT_INDENT_WIDTH;

    options->pretty = true;
    options->emit_comments = true;
    options->emit_debug_comments = false;
    options->emit_banner = true;

    /* Language policy */
    options->strict_c17 = true;
    options->allow_extensions = false;
    options->emit_static_assertions = true;

    /* Runtime */
    options->emit_runtime = true;
    options->runtime_checks = true;
    options->bounds_checks = true;
    options->null_checks = true;
    options->overflow_checks = true;
    options->assertions = true;

    /* Debugging / mapping */
    options->debug_info = false;
    options->emit_line_directives = false;
    options->emit_source_map = true;

    /* Diagnostics */
    options->fail_fast = false;
    options->warnings_as_errors = false;
    options->verbose_diagnostics = false;

    /* Determinism */
    options->deterministic = true;
    options->stable_names = true;
    options->stable_order = true;
    options->stable_hashes = true;

    /* Output safety */
    options->trigraph_safe_strings = true;
    options->escape_non_ascii = true;

    /* Native compiler policy */
    options->native_debug_symbols = false;
    options->native_frame_pointer = false;
    options->native_lto = false;
    options->native_pic = false;
    options->native_pie = false;

    /* Sanitizers */
    options->sanitize_address = false;
    options->sanitize_undefined = false;
    options->sanitize_leak = false;
    options->sanitize_thread = false;

    /* Environment */
    options->freestanding = false;
    options->hosted = true;

    /* Limits */
    options->max_output_bytes =
        VITTE_C17_OPTIONS_DEFAULT_MAX_OUTPUT_BYTES;

    options->max_identifier_bytes =
        VITTE_C17_OPTIONS_DEFAULT_MAX_IDENTIFIER_BYTES;

    options->max_indent_depth =
        VITTE_C17_OPTIONS_DEFAULT_MAX_INDENT_DEPTH;

    options->max_errors =
        VITTE_C17_OPTIONS_DEFAULT_MAX_ERRORS;
}

vitte_c17_options_t
vitte_c17_options_default(void)
{
    vitte_c17_options_t options;

    memset(
        &options,
        0,
        sizeof(options));

    options.magic =
        VITTE_C17_OPTIONS_MAGIC;

    options.profile =
        VITTE_C17_PROFILE_DEFAULT;

    options.last_error =
        VITTE_C17_OPTIONS_ERROR_NONE;

    vitte_c17_options_set_policy_defaults(
        &options);

    return options;
}

/* ========================================================================= */
/* Presets                                                                   */
/* ========================================================================= */

static void
vitte_c17_options_apply_debug_profile(
    vitte_c17_options_t *options)
{
    options->optimization =
        VITTE_C17_OPTIMIZATION_DEBUG;

    options->pretty = true;
    options->emit_comments = true;
    options->emit_debug_comments = true;

    options->debug_info = true;
    options->emit_line_directives = true;
    options->emit_source_map = true;

    options->runtime_checks = true;
    options->bounds_checks = true;
    options->null_checks = true;
    options->overflow_checks = true;
    options->assertions = true;

    options->native_debug_symbols = true;
    options->native_frame_pointer = true;
    options->native_lto = false;
}

static void
vitte_c17_options_apply_release_profile(
    vitte_c17_options_t *options)
{
    options->optimization =
        VITTE_C17_OPTIMIZATION_SPEED;

    options->pretty = true;
    options->emit_comments = false;
    options->emit_debug_comments = false;

    options->debug_info = false;
    options->emit_line_directives = false;
    options->emit_source_map = true;

    /*
     * Keep memory-safety checks enabled by default.
     *
     * Release must not silently mean "unsafe".
     */
    options->runtime_checks = true;
    options->bounds_checks = true;
    options->null_checks = true;
    options->overflow_checks = true;

    /*
     * Language-level assertions may be disabled for release.
     */
    options->assertions = false;

    options->native_debug_symbols = false;
    options->native_frame_pointer = false;
    options->native_lto = true;
}

static void
vitte_c17_options_apply_size_profile(
    vitte_c17_options_t *options)
{
    options->optimization =
        VITTE_C17_OPTIMIZATION_SIZE;

    options->pretty = false;
    options->emit_comments = false;
    options->emit_debug_comments = false;

    options->debug_info = false;
    options->emit_line_directives = false;
    options->emit_source_map = true;

    options->runtime_checks = true;
    options->bounds_checks = true;
    options->null_checks = true;
    options->overflow_checks = true;
    options->assertions = false;

    options->native_debug_symbols = false;
    options->native_frame_pointer = false;
    options->native_lto = true;
}

static void
vitte_c17_options_apply_sanitize_profile(
    vitte_c17_options_t *options)
{
    options->optimization =
        VITTE_C17_OPTIMIZATION_DEBUG;

    options->pretty = true;
    options->emit_comments = true;
    options->emit_debug_comments = true;

    options->debug_info = true;
    options->emit_line_directives = true;
    options->emit_source_map = true;

    options->runtime_checks = true;
    options->bounds_checks = true;
    options->null_checks = true;
    options->overflow_checks = true;
    options->assertions = true;

    options->native_debug_symbols = true;
    options->native_frame_pointer = true;
    options->native_lto = false;

    options->sanitize_address = true;
    options->sanitize_undefined = true;
}

static void
vitte_c17_options_apply_freestanding_profile(
    vitte_c17_options_t *options)
{
    options->optimization =
        VITTE_C17_OPTIMIZATION_BALANCED;

    options->freestanding = true;
    options->hosted = false;

    options->runtime_mode =
        VITTE_C17_RUNTIME_MODE_NONE;

    options->emit_runtime = false;

    /*
     * Checks requiring runtime support cannot be assumed to exist in a
     * freestanding translation unit.
     */
    options->runtime_checks = false;
    options->bounds_checks = false;
    options->null_checks = false;
    options->overflow_checks = false;

    options->assertions = false;

    options->native_lto = false;
}

/* ========================================================================= */
/* Apply profile                                                             */
/* ========================================================================= */

bool
vitte_c17_options_apply_profile(
    vitte_c17_options_t *options,
    vitte_c17_profile_t profile)
{
    if (options == NULL ||
        options->magic !=
            VITTE_C17_OPTIONS_MAGIC) {
        return false;
    }

    /*
     * A profile is a complete preset, not a partial mutation.
     *
     * Reset policy fields first so:
     *
     *     debug -> release
     *
     * cannot accidentally preserve sanitizer/debug flags.
     */
    vitte_c17_options_set_policy_defaults(
        options);

    options->profile = profile;

    switch (profile) {
        case VITTE_C17_PROFILE_DEFAULT:
            break;

        case VITTE_C17_PROFILE_DEBUG:
            vitte_c17_options_apply_debug_profile(
                options);
            break;

        case VITTE_C17_PROFILE_RELEASE:
            vitte_c17_options_apply_release_profile(
                options);
            break;

        case VITTE_C17_PROFILE_SIZE:
            vitte_c17_options_apply_size_profile(
                options);
            break;

        case VITTE_C17_PROFILE_SANITIZE:
            vitte_c17_options_apply_sanitize_profile(
                options);
            break;

        case VITTE_C17_PROFILE_FREESTANDING:
            vitte_c17_options_apply_freestanding_profile(
                options);
            break;

        case VITTE_C17_PROFILE_COUNT:
            return
                vitte_c17_options_fail(
                    options,
                    VITTE_C17_OPTIONS_ERROR_INVALID_PROFILE);
    }

    options->last_error =
        VITTE_C17_OPTIONS_ERROR_NONE;

    return true;
}

/* ========================================================================= */
/* Initialization                                                            */
/* ========================================================================= */

bool
vitte_c17_options_init(
    vitte_c17_options_t *options,
    vitte_c17_profile_t profile)
{
    if (options == NULL) {
        return false;
    }

    *options =
        vitte_c17_options_default();

    if (!vitte_c17_options_apply_profile(
            options,
            profile)) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Destroy                                                                   */
/* ========================================================================= */

void
vitte_c17_options_destroy(
    vitte_c17_options_t *options)
{
    if (options == NULL ||
        options->magic !=
            VITTE_C17_OPTIONS_MAGIC) {
        return;
    }

    memset(
        options,
        0,
        sizeof(*options));

    options->magic =
        VITTE_C17_OPTIONS_DEAD_MAGIC;
}

/* ========================================================================= */
/* Validity                                                                  */
/* ========================================================================= */

bool
vitte_c17_options_is_valid(
    const vitte_c17_options_t *options)
{
    if (options == NULL) {
        return false;
    }

    if (options->magic !=
        VITTE_C17_OPTIONS_MAGIC) {
        return false;
    }

    if (options->profile <
            VITTE_C17_PROFILE_DEFAULT ||
        options->profile >=
            VITTE_C17_PROFILE_COUNT) {
        return false;
    }

    if (options->dialect <=
            VITTE_C17_DIALECT_INVALID ||
        options->dialect >=
            VITTE_C17_DIALECT_COUNT) {
        return false;
    }

    if (options->optimization <=
            VITTE_C17_OPTIMIZATION_INVALID ||
        options->optimization >=
            VITTE_C17_OPTIMIZATION_COUNT) {
        return false;
    }

    if (options->runtime_mode <=
            VITTE_C17_RUNTIME_MODE_INVALID ||
        options->runtime_mode >=
            VITTE_C17_RUNTIME_MODE_COUNT) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Validation                                                                */
/* ========================================================================= */

vitte_c17_options_error_t
vitte_c17_options_validate(
    const vitte_c17_options_t *options)
{
    if (!vitte_c17_options_is_valid(
            options)) {
        return
            VITTE_C17_OPTIONS_ERROR_INVALID_OPTIONS;
    }

    if (options->indent_width >
        VITTE_C17_OPTIONS_MAX_INDENT_WIDTH) {
        return
            VITTE_C17_OPTIONS_ERROR_INVALID_LIMIT;
    }

    if (options->max_indent_depth == 0u ||
        options->max_indent_depth >
            VITTE_C17_OPTIONS_MAX_INDENT_DEPTH) {
        return
            VITTE_C17_OPTIONS_ERROR_INVALID_LIMIT;
    }

    if (options->max_identifier_bytes == 0u ||
        options->max_identifier_bytes >
            VITTE_C17_OPTIONS_MAX_IDENTIFIER_BYTES) {
        return
            VITTE_C17_OPTIONS_ERROR_INVALID_LIMIT;
    }

    if (options->max_errors == 0u) {
        return
            VITTE_C17_OPTIONS_ERROR_INVALID_LIMIT;
    }

    /*
     * strict C17 and GNU extensions are contradictory.
     */
    if (options->strict_c17 &&
        options->allow_extensions) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    if (options->dialect ==
            VITTE_C17_DIALECT_C17 &&
        options->allow_extensions) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    if (options->dialect ==
            VITTE_C17_DIALECT_GNU17 &&
        options->strict_c17) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    /*
     * Hosted and freestanding are mutually exclusive.
     */
    if (options->hosted ==
        options->freestanding) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    if (options->freestanding &&
        options->runtime_mode ==
            VITTE_C17_RUNTIME_MODE_EMBEDDED) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    if (options->runtime_mode ==
            VITTE_C17_RUNTIME_MODE_NONE &&
        options->emit_runtime) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    if (options->runtime_mode ==
            VITTE_C17_RUNTIME_MODE_EXTERNAL &&
        options->emit_runtime) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    /*
     * ThreadSanitizer is intentionally exclusive with AddressSanitizer.
     *
     * Toolchains generally do not support combining them.
     */
    if (options->sanitize_thread &&
        options->sanitize_address) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    if (options->sanitize_thread &&
        options->sanitize_leak) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    /*
     * LTO is deliberately disabled for sanitizer profiles by default.
     * Reject explicit combinations here so behavior remains predictable
     * across Clang/GCC versions.
     */
    if (options->native_lto &&
        (options->sanitize_address ||
         options->sanitize_undefined ||
         options->sanitize_leak ||
         options->sanitize_thread)) {
        return
            VITTE_C17_OPTIONS_ERROR_CONFLICT;
    }

    return
        VITTE_C17_OPTIONS_ERROR_NONE;
}

/* ========================================================================= */
/* Normalization                                                             */
/* ========================================================================= */

bool
vitte_c17_options_normalize(
    vitte_c17_options_t *options)
{
    if (options == NULL ||
        options->magic !=
            VITTE_C17_OPTIONS_MAGIC) {
        return false;
    }

    /*
     * Dialect determines extension policy.
     */
    switch (options->dialect) {
        case VITTE_C17_DIALECT_C17:
            options->strict_c17 = true;
            options->allow_extensions = false;
            break;

        case VITTE_C17_DIALECT_GNU17:
            options->strict_c17 = false;
            options->allow_extensions = true;
            break;

        case VITTE_C17_DIALECT_INVALID:
        case VITTE_C17_DIALECT_COUNT:
            return
                vitte_c17_options_fail(
                    options,
                    VITTE_C17_OPTIONS_ERROR_INVALID_DIALECT);
    }

    /*
     * Environment normalization.
     */
    if (options->freestanding) {
        options->hosted = false;
    } else if (!options->hosted) {
        options->hosted = true;
    }

    /*
     * Runtime mode controls whether runtime implementation is emitted.
     */
    switch (options->runtime_mode) {
        case VITTE_C17_RUNTIME_MODE_AUTO:
            /*
             * AUTO retains caller-selected emit_runtime.
             */
            break;

        case VITTE_C17_RUNTIME_MODE_EMBEDDED:
            options->emit_runtime = true;
            break;

        case VITTE_C17_RUNTIME_MODE_EXTERNAL:
        case VITTE_C17_RUNTIME_MODE_NONE:
            options->emit_runtime = false;
            break;

        case VITTE_C17_RUNTIME_MODE_INVALID:
        case VITTE_C17_RUNTIME_MODE_COUNT:
            return
                vitte_c17_options_fail(
                    options,
                    VITTE_C17_OPTIONS_ERROR_INVALID_RUNTIME);
    }

    /*
     * Runtime-free mode cannot promise runtime-backed checks.
     */
    if (options->runtime_mode ==
        VITTE_C17_RUNTIME_MODE_NONE) {
        options->runtime_checks = false;
        options->bounds_checks = false;
        options->null_checks = false;
        options->overflow_checks = false;
    }

    /*
     * Debug info implies native debug symbols.
     */
    if (options->debug_info) {
        options->native_debug_symbols = true;
    }

    /*
     * Sanitizers require useful stack traces.
     */
    if (options->sanitize_address ||
        options->sanitize_undefined ||
        options->sanitize_leak ||
        options->sanitize_thread) {
        options->native_frame_pointer = true;
        options->native_debug_symbols = true;
        options->native_lto = false;
    }

    /*
     * ThreadSanitizer cannot be combined with ASan/LSan.
     *
     * Normalize to thread-only rather than silently producing an invalid
     * native compiler command.
     */
    if (options->sanitize_thread) {
        options->sanitize_address = false;
        options->sanitize_leak = false;
    }

    if (options->indent_width >
        VITTE_C17_OPTIONS_MAX_INDENT_WIDTH) {
        options->indent_width =
            VITTE_C17_OPTIONS_MAX_INDENT_WIDTH;
    }

    if (options->max_indent_depth == 0u) {
        options->max_indent_depth =
            VITTE_C17_OPTIONS_DEFAULT_MAX_INDENT_DEPTH;
    }

    if (options->max_identifier_bytes == 0u) {
        options->max_identifier_bytes =
            VITTE_C17_OPTIONS_DEFAULT_MAX_IDENTIFIER_BYTES;
    }

    if (options->max_errors == 0u) {
        options->max_errors =
            VITTE_C17_OPTIONS_DEFAULT_MAX_ERRORS;
    }

    options->last_error =
        vitte_c17_options_validate(
            options);

    return
        options->last_error ==
        VITTE_C17_OPTIONS_ERROR_NONE;
}

/* ========================================================================= */
/* Backend configuration                                                     */
/* ========================================================================= */

bool
vitte_c17_options_to_backend_config(
    const vitte_c17_options_t *options,
    vitte_c17_backend_config_t *config)
{
    vitte_c17_options_error_t validation;

    if (options == NULL ||
        config == NULL) {
        return false;
    }

    validation =
        vitte_c17_options_validate(
            options);

    if (validation !=
        VITTE_C17_OPTIONS_ERROR_NONE) {
        return false;
    }

    *config =
        vitte_c17_backend_config_default();

    config->indent_width =
        options->indent_width;

    config->max_indent_depth =
        options->max_indent_depth;

    config->max_output_bytes =
        options->max_output_bytes;

    config->max_identifier_bytes =
        options->max_identifier_bytes;

    config->max_errors =
        options->max_errors;

    config->emit_banner =
        options->emit_banner;

    config->emit_line_directives =
        options->emit_line_directives;

    config->emit_source_map =
        options->emit_source_map;

    config->emit_comments =
        options->emit_comments;

    config->emit_debug_comments =
        options->emit_debug_comments;

    config->emit_static_assertions =
        options->emit_static_assertions;

    config->emit_runtime =
        options->emit_runtime;

    config->pretty =
        options->pretty;

    config->deterministic =
        options->deterministic;

    config->strict_c17 =
        options->strict_c17;

    config->fail_fast =
        options->fail_fast;

    config->trigraph_safe_strings =
        options->trigraph_safe_strings;

    config->escape_non_ascii =
        options->escape_non_ascii;

    return true;
}

/* ========================================================================= */
/* Option hash                                                               */
/* ========================================================================= */

uint64_t
vitte_c17_options_hash(
    const vitte_c17_options_t *options)
{
    uint64_t hash;

    if (!vitte_c17_options_is_valid(
            options)) {
        return UINT64_C(0);
    }

    hash =
        VITTE_C17_OPTIONS_HASH_OFFSET;

#define VITTE_HASH_ENUM(field) \
    do { \
        hash = vitte_c17_options_hash_enum( \
            hash, \
            (unsigned)options->field); \
    } while (0)

#define VITTE_HASH_SIZE(field) \
    do { \
        hash = vitte_c17_options_hash_size( \
            hash, \
            options->field); \
    } while (0)

#define VITTE_HASH_BOOL(field) \
    do { \
        hash = vitte_c17_options_hash_bool( \
            hash, \
            options->field); \
    } while (0)

    VITTE_HASH_ENUM(profile);
    VITTE_HASH_ENUM(dialect);
    VITTE_HASH_ENUM(optimization);
    VITTE_HASH_ENUM(runtime_mode);

    VITTE_HASH_SIZE(indent_width);
    VITTE_HASH_SIZE(max_output_bytes);
    VITTE_HASH_SIZE(max_identifier_bytes);
    VITTE_HASH_SIZE(max_indent_depth);
    VITTE_HASH_SIZE(max_errors);

    VITTE_HASH_BOOL(pretty);
    VITTE_HASH_BOOL(emit_comments);
    VITTE_HASH_BOOL(emit_debug_comments);
    VITTE_HASH_BOOL(emit_banner);

    VITTE_HASH_BOOL(strict_c17);
    VITTE_HASH_BOOL(allow_extensions);
    VITTE_HASH_BOOL(emit_static_assertions);

    VITTE_HASH_BOOL(emit_runtime);
    VITTE_HASH_BOOL(runtime_checks);
    VITTE_HASH_BOOL(bounds_checks);
    VITTE_HASH_BOOL(null_checks);
    VITTE_HASH_BOOL(overflow_checks);
    VITTE_HASH_BOOL(assertions);

    VITTE_HASH_BOOL(debug_info);
    VITTE_HASH_BOOL(emit_line_directives);
    VITTE_HASH_BOOL(emit_source_map);

    VITTE_HASH_BOOL(fail_fast);
    VITTE_HASH_BOOL(warnings_as_errors);
    VITTE_HASH_BOOL(verbose_diagnostics);

    VITTE_HASH_BOOL(deterministic);
    VITTE_HASH_BOOL(stable_names);
    VITTE_HASH_BOOL(stable_order);
    VITTE_HASH_BOOL(stable_hashes);

    VITTE_HASH_BOOL(trigraph_safe_strings);
    VITTE_HASH_BOOL(escape_non_ascii);

    VITTE_HASH_BOOL(native_debug_symbols);
    VITTE_HASH_BOOL(native_frame_pointer);
    VITTE_HASH_BOOL(native_lto);
    VITTE_HASH_BOOL(native_pic);
    VITTE_HASH_BOOL(native_pie);

    VITTE_HASH_BOOL(sanitize_address);
    VITTE_HASH_BOOL(sanitize_undefined);
    VITTE_HASH_BOOL(sanitize_leak);
    VITTE_HASH_BOOL(sanitize_thread);

    VITTE_HASH_BOOL(freestanding);
    VITTE_HASH_BOOL(hosted);

#undef VITTE_HASH_BOOL
#undef VITTE_HASH_SIZE
#undef VITTE_HASH_ENUM

    return hash;
}

/* ========================================================================= */
/* Equality                                                                  */
/* ========================================================================= */

bool
vitte_c17_options_equal(
    const vitte_c17_options_t *left,
    const vitte_c17_options_t *right)
{
    if (left == right) {
        return true;
    }

    if (!vitte_c17_options_is_valid(left) ||
        !vitte_c17_options_is_valid(right)) {
        return false;
    }

#define VITTE_COMPARE(field) \
    do { \
        if (left->field != right->field) { \
            return false; \
        } \
    } while (0)

    VITTE_COMPARE(profile);
    VITTE_COMPARE(dialect);
    VITTE_COMPARE(optimization);
    VITTE_COMPARE(runtime_mode);

    VITTE_COMPARE(indent_width);
    VITTE_COMPARE(max_output_bytes);
    VITTE_COMPARE(max_identifier_bytes);
    VITTE_COMPARE(max_indent_depth);
    VITTE_COMPARE(max_errors);

    VITTE_COMPARE(pretty);
    VITTE_COMPARE(emit_comments);
    VITTE_COMPARE(emit_debug_comments);
    VITTE_COMPARE(emit_banner);

    VITTE_COMPARE(strict_c17);
    VITTE_COMPARE(allow_extensions);
    VITTE_COMPARE(emit_static_assertions);

    VITTE_COMPARE(emit_runtime);
    VITTE_COMPARE(runtime_checks);
    VITTE_COMPARE(bounds_checks);
    VITTE_COMPARE(null_checks);
    VITTE_COMPARE(overflow_checks);
    VITTE_COMPARE(assertions);

    VITTE_COMPARE(debug_info);
    VITTE_COMPARE(emit_line_directives);
    VITTE_COMPARE(emit_source_map);

    VITTE_COMPARE(fail_fast);
    VITTE_COMPARE(warnings_as_errors);
    VITTE_COMPARE(verbose_diagnostics);

    VITTE_COMPARE(deterministic);
    VITTE_COMPARE(stable_names);
    VITTE_COMPARE(stable_order);
    VITTE_COMPARE(stable_hashes);

    VITTE_COMPARE(trigraph_safe_strings);
    VITTE_COMPARE(escape_non_ascii);

    VITTE_COMPARE(native_debug_symbols);
    VITTE_COMPARE(native_frame_pointer);
    VITTE_COMPARE(native_lto);
    VITTE_COMPARE(native_pic);
    VITTE_COMPARE(native_pie);

    VITTE_COMPARE(sanitize_address);
    VITTE_COMPARE(sanitize_undefined);
    VITTE_COMPARE(sanitize_leak);
    VITTE_COMPARE(sanitize_thread);

    VITTE_COMPARE(freestanding);
    VITTE_COMPARE(hosted);

#undef VITTE_COMPARE

    return true;
}

/* ========================================================================= */
/* Feature predicates                                                        */
/* ========================================================================= */

bool
vitte_c17_options_has_sanitizer(
    const vitte_c17_options_t *options)
{
    if (!vitte_c17_options_is_valid(
            options)) {
        return false;
    }

    return
        options->sanitize_address ||
        options->sanitize_undefined ||
        options->sanitize_leak ||
        options->sanitize_thread;
}

bool
vitte_c17_options_has_runtime_checks(
    const vitte_c17_options_t *options)
{
    if (!vitte_c17_options_is_valid(
            options)) {
        return false;
    }

    return
        options->runtime_checks ||
        options->bounds_checks ||
        options->null_checks ||
        options->overflow_checks ||
        options->assertions;
}

bool
vitte_c17_options_is_reproducible(
    const vitte_c17_options_t *options)
{
    if (!vitte_c17_options_is_valid(
            options)) {
        return false;
    }

    return
        options->deterministic &&
        options->stable_names &&
        options->stable_order &&
        options->stable_hashes;
}

/* ========================================================================= */
/* Optimization helpers                                                      */
/* ========================================================================= */

bool
vitte_c17_options_optimizes_for_speed(
    const vitte_c17_options_t *options)
{
    if (!vitte_c17_options_is_valid(
            options)) {
        return false;
    }

    return
        options->optimization ==
            VITTE_C17_OPTIMIZATION_SPEED ||
        options->optimization ==
            VITTE_C17_OPTIMIZATION_AGGRESSIVE;
}

bool
vitte_c17_options_optimizes_for_size(
    const vitte_c17_options_t *options)
{
    if (!vitte_c17_options_is_valid(
            options)) {
        return false;
    }

    return
        options->optimization ==
        VITTE_C17_OPTIMIZATION_SIZE;
}

/* ========================================================================= */
/* Native compiler optimization flag                                         */
/* ========================================================================= */

const char *
vitte_c17_options_native_optimization_flag(
    const vitte_c17_options_t *options)
{
    if (!vitte_c17_options_is_valid(
            options)) {
        return NULL;
    }

    switch (options->optimization) {
        case VITTE_C17_OPTIMIZATION_NONE:
            return "-O0";

        case VITTE_C17_OPTIMIZATION_DEBUG:
            return "-Og";

        case VITTE_C17_OPTIMIZATION_BALANCED:
            return "-O2";

        case VITTE_C17_OPTIMIZATION_SPEED:
            return "-O3";

        case VITTE_C17_OPTIMIZATION_SIZE:
            return "-Os";

        case VITTE_C17_OPTIMIZATION_AGGRESSIVE:
            return "-O3";

        case VITTE_C17_OPTIMIZATION_INVALID:
        case VITTE_C17_OPTIMIZATION_COUNT:
            return NULL;

        default:
            return NULL;
    }
}

/* ========================================================================= */
/* Native dialect flag                                                       */
/* ========================================================================= */

const char *
vitte_c17_options_native_dialect_flag(
    const vitte_c17_options_t *options)
{
    if (!vitte_c17_options_is_valid(
            options)) {
        return NULL;
    }

    switch (options->dialect) {
        case VITTE_C17_DIALECT_C17:
            return "-std=c17";

        case VITTE_C17_DIALECT_GNU17:
            return "-std=gnu17";

        case VITTE_C17_DIALECT_INVALID:
        case VITTE_C17_DIALECT_COUNT:
            return NULL;

        default:
            return NULL;
    }
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_c17_options_stats_t
vitte_c17_options_stats(
    const vitte_c17_options_t *options)
{
    vitte_c17_options_stats_t stats;

    memset(
        &stats,
        0,
        sizeof(stats));

    if (!vitte_c17_options_is_valid(
            options)) {
        return stats;
    }

#define VITTE_COUNT_BOOL(field) \
    do { \
        ++stats.boolean_option_count; \
        if (options->field) { \
            ++stats.enabled_boolean_count; \
        } \
    } while (0)

    VITTE_COUNT_BOOL(pretty);
    VITTE_COUNT_BOOL(emit_comments);
    VITTE_COUNT_BOOL(emit_debug_comments);
    VITTE_COUNT_BOOL(emit_banner);

    VITTE_COUNT_BOOL(strict_c17);
    VITTE_COUNT_BOOL(allow_extensions);
    VITTE_COUNT_BOOL(emit_static_assertions);

    VITTE_COUNT_BOOL(emit_runtime);
    VITTE_COUNT_BOOL(runtime_checks);
    VITTE_COUNT_BOOL(bounds_checks);
    VITTE_COUNT_BOOL(null_checks);
    VITTE_COUNT_BOOL(overflow_checks);
    VITTE_COUNT_BOOL(assertions);

    VITTE_COUNT_BOOL(debug_info);
    VITTE_COUNT_BOOL(emit_line_directives);
    VITTE_COUNT_BOOL(emit_source_map);

    VITTE_COUNT_BOOL(fail_fast);
    VITTE_COUNT_BOOL(warnings_as_errors);
    VITTE_COUNT_BOOL(verbose_diagnostics);

    VITTE_COUNT_BOOL(deterministic);
    VITTE_COUNT_BOOL(stable_names);
    VITTE_COUNT_BOOL(stable_order);
    VITTE_COUNT_BOOL(stable_hashes);

    VITTE_COUNT_BOOL(trigraph_safe_strings);
    VITTE_COUNT_BOOL(escape_non_ascii);

    VITTE_COUNT_BOOL(native_debug_symbols);
    VITTE_COUNT_BOOL(native_frame_pointer);
    VITTE_COUNT_BOOL(native_lto);
    VITTE_COUNT_BOOL(native_pic);
    VITTE_COUNT_BOOL(native_pie);

    VITTE_COUNT_BOOL(sanitize_address);
    VITTE_COUNT_BOOL(sanitize_undefined);
    VITTE_COUNT_BOOL(sanitize_leak);
    VITTE_COUNT_BOOL(sanitize_thread);

    VITTE_COUNT_BOOL(freestanding);
    VITTE_COUNT_BOOL(hosted);

#undef VITTE_COUNT_BOOL

    stats.hash =
        vitte_c17_options_hash(
            options);

    stats.valid =
        vitte_c17_options_validate(
            options) ==
        VITTE_C17_OPTIONS_ERROR_NONE;

    stats.has_sanitizer =
        vitte_c17_options_has_sanitizer(
            options);

    stats.has_runtime_checks =
        vitte_c17_options_has_runtime_checks(
            options);

    stats.reproducible =
        vitte_c17_options_is_reproducible(
            options);

    return stats;
}

/* ========================================================================= */
/* Last error                                                                */
/* ========================================================================= */

vitte_c17_options_error_t
vitte_c17_options_last_error(
    const vitte_c17_options_t *options)
{
    if (options == NULL ||
        options->magic !=
            VITTE_C17_OPTIONS_MAGIC) {
        return
            VITTE_C17_OPTIONS_ERROR_INVALID_OPTIONS;
    }

    return options->last_error;
}

void
vitte_c17_options_clear_error(
    vitte_c17_options_t *options)
{
    if (options == NULL ||
        options->magic !=
            VITTE_C17_OPTIONS_MAGIC) {
        return;
    }

    options->last_error =
        VITTE_C17_OPTIONS_ERROR_NONE;
}

/* ========================================================================= */
/* End                                                                       */
/* ========================================================================= */
