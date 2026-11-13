/*
 * Vitte Compiler
 * src/backend/c17/backend.c
 *
 * C17 backend orchestration layer.
 *
 * Responsibilities:
 *
 *   - backend configuration
 *   - target/environment configuration
 *   - translation-unit generation lifecycle
 *   - deterministic section ordering
 *   - output writer abstraction
 *   - indentation/newline management
 *   - source location tracking
 *   - source-map hooks
 *   - diagnostic hooks
 *   - statistics
 *   - cancellation
 *   - output limits
 *   - structural validation
 *   - transactional generation state
 *
 * Non-responsibilities:
 *
 *   This file is intentionally not the implementation of every C17 lowering
 *   rule. Expression, statement, type, declaration and translation-unit
 *   emitters should live in dedicated backend modules and use this context.
 *
 * Design:
 *
 *   HIR / IR
 *       |
 *       v
 *   backend.c
 *       |
 *       +-- type lowering
 *       +-- declaration lowering
 *       +-- expression lowering
 *       +-- statement lowering
 *       +-- runtime requirements
 *       +-- source map
 *       +-- diagnostics
 *       |
 *       v
 *   strict C17 translation unit
 */

#include "backend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

#ifndef VITTE_C17_BACKEND_MAGIC
#define VITTE_C17_BACKEND_MAGIC UINT64_C(0x5649545445433137)
#endif

#ifndef VITTE_C17_BACKEND_DEAD_MAGIC
#define VITTE_C17_BACKEND_DEAD_MAGIC UINT64_C(0x4445414443313721)
#endif

#ifndef VITTE_C17_DEFAULT_INDENT_WIDTH
#define VITTE_C17_DEFAULT_INDENT_WIDTH ((size_t)4u)
#endif

#ifndef VITTE_C17_DEFAULT_MAX_OUTPUT_BYTES
#define VITTE_C17_DEFAULT_MAX_OUTPUT_BYTES SIZE_MAX
#endif

#ifndef VITTE_C17_DEFAULT_MAX_IDENTIFIER_BYTES
#define VITTE_C17_DEFAULT_MAX_IDENTIFIER_BYTES ((size_t)4096u)
#endif

#ifndef VITTE_C17_DEFAULT_MAX_INDENT_DEPTH
#define VITTE_C17_DEFAULT_MAX_INDENT_DEPTH ((size_t)4096u)
#endif

#ifndef VITTE_C17_DEFAULT_MAX_ERRORS
#define VITTE_C17_DEFAULT_MAX_ERRORS ((size_t)100u)
#endif

/* ========================================================================= */
/* Internal arithmetic                                                       */
/* ========================================================================= */

static bool
vitte_c17_add_overflow(
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

static size_t
vitte_c17_saturating_add(
    size_t left,
    size_t right)
{
    size_t result;

    if (vitte_c17_add_overflow(
            left,
            right,
            &result)) {
        return SIZE_MAX;
    }

    return result;
}

/* ========================================================================= */
/* Error names                                                               */
/* ========================================================================= */

const char *
vitte_c17_backend_error_name(
    vitte_c17_backend_error_t error)
{
    switch (error) {
        case VITTE_C17_BACKEND_ERROR_NONE:
            return "none";

        case VITTE_C17_BACKEND_ERROR_INVALID_BACKEND:
            return "invalid-backend";

        case VITTE_C17_BACKEND_ERROR_INVALID_ARGUMENT:
            return "invalid-argument";

        case VITTE_C17_BACKEND_ERROR_INVALID_STATE:
            return "invalid-state";

        case VITTE_C17_BACKEND_ERROR_INVALID_CONFIG:
            return "invalid-config";

        case VITTE_C17_BACKEND_ERROR_INVALID_TARGET:
            return "invalid-target";

        case VITTE_C17_BACKEND_ERROR_OUTPUT:
            return "output";

        case VITTE_C17_BACKEND_ERROR_OUTPUT_LIMIT:
            return "output-limit";

        case VITTE_C17_BACKEND_ERROR_OVERFLOW:
            return "overflow";

        case VITTE_C17_BACKEND_ERROR_OUT_OF_MEMORY:
            return "out-of-memory";

        case VITTE_C17_BACKEND_ERROR_UNSUPPORTED:
            return "unsupported";

        case VITTE_C17_BACKEND_ERROR_CANCELLED:
            return "cancelled";

        case VITTE_C17_BACKEND_ERROR_TOO_MANY_ERRORS:
            return "too-many-errors";

        case VITTE_C17_BACKEND_ERROR_INTERNAL:
            return "internal";

        case VITTE_C17_BACKEND_ERROR_COUNT:
            return "count";

        default:
            return "unknown";
    }
}

/* ========================================================================= */
/* State names                                                               */
/* ========================================================================= */

const char *
vitte_c17_backend_state_name(
    vitte_c17_backend_state_t state)
{
    switch (state) {
        case VITTE_C17_BACKEND_STATE_UNINITIALIZED:
            return "uninitialized";

        case VITTE_C17_BACKEND_STATE_READY:
            return "ready";

        case VITTE_C17_BACKEND_STATE_GENERATING:
            return "generating";

        case VITTE_C17_BACKEND_STATE_FINISHED:
            return "finished";

        case VITTE_C17_BACKEND_STATE_FAILED:
            return "failed";

        case VITTE_C17_BACKEND_STATE_CANCELLED:
            return "cancelled";

        case VITTE_C17_BACKEND_STATE_DESTROYED:
            return "destroyed";

        case VITTE_C17_BACKEND_STATE_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Section names                                                             */
/* ========================================================================= */

const char *
vitte_c17_section_name(
    vitte_c17_section_t section)
{
    switch (section) {
        case VITTE_C17_SECTION_NONE:
            return "none";

        case VITTE_C17_SECTION_BANNER:
            return "banner";

        case VITTE_C17_SECTION_FEATURE_MACROS:
            return "feature-macros";

        case VITTE_C17_SECTION_INCLUDES:
            return "includes";

        case VITTE_C17_SECTION_FORWARD_DECLARATIONS:
            return "forward-declarations";

        case VITTE_C17_SECTION_TYPE_DECLARATIONS:
            return "type-declarations";

        case VITTE_C17_SECTION_GLOBAL_DECLARATIONS:
            return "global-declarations";

        case VITTE_C17_SECTION_RUNTIME_DECLARATIONS:
            return "runtime-declarations";

        case VITTE_C17_SECTION_FUNCTION_DECLARATIONS:
            return "function-declarations";

        case VITTE_C17_SECTION_RUNTIME_DEFINITIONS:
            return "runtime-definitions";

        case VITTE_C17_SECTION_GLOBAL_DEFINITIONS:
            return "global-definitions";

        case VITTE_C17_SECTION_FUNCTION_DEFINITIONS:
            return "function-definitions";

        case VITTE_C17_SECTION_ENTRY:
            return "entry";

        case VITTE_C17_SECTION_EPILOGUE:
            return "epilogue";

        case VITTE_C17_SECTION_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

vitte_c17_backend_config_t
vitte_c17_backend_config_default(void)
{
    vitte_c17_backend_config_t config;

    memset(
        &config,
        0,
        sizeof(config));

    config.indent_width =
        VITTE_C17_DEFAULT_INDENT_WIDTH;

    config.max_indent_depth =
        VITTE_C17_DEFAULT_MAX_INDENT_DEPTH;

    config.max_output_bytes =
        VITTE_C17_DEFAULT_MAX_OUTPUT_BYTES;

    config.max_identifier_bytes =
        VITTE_C17_DEFAULT_MAX_IDENTIFIER_BYTES;

    config.max_errors =
        VITTE_C17_DEFAULT_MAX_ERRORS;

    config.emit_banner = true;
    config.emit_line_directives = false;
    config.emit_source_map = true;

    config.emit_comments = true;
    config.emit_debug_comments = false;

    config.emit_static_assertions = true;

    config.emit_runtime = true;

    config.pretty = true;

    config.deterministic = true;

    config.strict_c17 = true;

    config.fail_fast = false;

    config.trigraph_safe_strings = true;

    config.escape_non_ascii = false;

    return config;
}

bool
vitte_c17_backend_config_validate(
    const vitte_c17_backend_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (config->indent_width == 0u) {
        return false;
    }

    if (config->max_indent_depth == 0u) {
        return false;
    }

    if (config->max_identifier_bytes == 0u) {
        return false;
    }

    if (config->max_errors == 0u) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Target                                                                    */
/* ========================================================================= */

vitte_c17_target_t
vitte_c17_target_default(void)
{
    vitte_c17_target_t target;

    memset(
        &target,
        0,
        sizeof(target));

    target.pointer_bits =
        (uint32_t)(sizeof(void *) * 8u);

    target.size_bits =
        (uint32_t)(sizeof(size_t) * 8u);

    target.char_bits = 8u;

    target.little_endian = true;

    {
        uint16_t probe;

        probe = UINT16_C(1);

        target.little_endian =
            *((const unsigned char *)
                &probe) == 1u;
    }

#if defined(_WIN32)
    target.environment =
        VITTE_C17_ENVIRONMENT_WINDOWS;
#elif defined(__APPLE__)
    target.environment =
        VITTE_C17_ENVIRONMENT_DARWIN;
#elif defined(__linux__)
    target.environment =
        VITTE_C17_ENVIRONMENT_LINUX;
#elif defined(__FreeBSD__)
    target.environment =
        VITTE_C17_ENVIRONMENT_FREEBSD;
#else
    target.environment =
        VITTE_C17_ENVIRONMENT_UNKNOWN;
#endif

#if defined(__x86_64__) || defined(_M_X64)
    target.architecture =
        VITTE_C17_ARCH_X86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    target.architecture =
        VITTE_C17_ARCH_AARCH64;
#elif defined(__i386__) || defined(_M_IX86)
    target.architecture =
        VITTE_C17_ARCH_X86;
#elif defined(__arm__) || defined(_M_ARM)
    target.architecture =
        VITTE_C17_ARCH_ARM;
#elif defined(__riscv)
    target.architecture =
        VITTE_C17_ARCH_RISCV;
#else
    target.architecture =
        VITTE_C17_ARCH_UNKNOWN;
#endif

    return target;
}

bool
vitte_c17_target_validate(
    const vitte_c17_target_t *target)
{
    if (target == NULL) {
        return false;
    }

    if (target->pointer_bits != 16u &&
        target->pointer_bits != 32u &&
        target->pointer_bits != 64u &&
        target->pointer_bits != 128u) {
        return false;
    }

    if (target->size_bits == 0u ||
        target->char_bits == 0u) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Internal error management                                                 */
/* ========================================================================= */

static void
vitte_c17_backend_set_error(
    vitte_c17_backend_t *backend,
    vitte_c17_backend_error_t error)
{
    if (backend == NULL) {
        return;
    }

    backend->last_error = error;

    if (error !=
        VITTE_C17_BACKEND_ERROR_NONE) {
        backend->state =
            VITTE_C17_BACKEND_STATE_FAILED;
    }
}

static bool
vitte_c17_backend_fail(
    vitte_c17_backend_t *backend,
    vitte_c17_backend_error_t error)
{
    vitte_c17_backend_set_error(
        backend,
        error);

    return false;
}

/* ========================================================================= */
/* Validity                                                                  */
/* ========================================================================= */

bool
vitte_c17_backend_is_valid(
    const vitte_c17_backend_t *backend)
{
    if (backend == NULL) {
        return false;
    }

    if (backend->magic !=
        VITTE_C17_BACKEND_MAGIC) {
        return false;
    }

    if (!vitte_c17_backend_config_validate(
            &backend->config)) {
        return false;
    }

    if (!vitte_c17_target_validate(
            &backend->target)) {
        return false;
    }

    if (backend->writer.write == NULL) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Writer                                                                    */
/* ========================================================================= */

vitte_c17_writer_t
vitte_c17_writer_make(
    vitte_c17_write_fn write,
    void *user_data)
{
    vitte_c17_writer_t writer;

    writer.write = write;
    writer.user_data = user_data;

    return writer;
}

static bool
vitte_c17_backend_raw_write(
    vitte_c17_backend_t *backend,
    const char *data,
    size_t length)
{
    size_t new_total;
    bool ok;

    if (!vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    if (backend->state !=
        VITTE_C17_BACKEND_STATE_GENERATING) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    if (backend->cancelled) {
        backend->state =
            VITTE_C17_BACKEND_STATE_CANCELLED;

        backend->last_error =
            VITTE_C17_BACKEND_ERROR_CANCELLED;

        return false;
    }

    if (length == 0u) {
        return true;
    }

    if (data == NULL) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_ARGUMENT);
    }

    if (vitte_c17_add_overflow(
            backend->stats.output_bytes,
            length,
            &new_total)) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_OVERFLOW);
    }

    if (new_total >
        backend->config.max_output_bytes) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_OUTPUT_LIMIT);
    }

    ok =
        backend->writer.write(
            backend->writer.user_data,
            data,
            length);

    if (!ok) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_OUTPUT);
    }

    backend->stats.output_bytes =
        new_total;

    return true;
}

/* ========================================================================= */
/* Indentation                                                               */
/* ========================================================================= */

static bool
vitte_c17_backend_emit_indent(
    vitte_c17_backend_t *backend)
{
    static const char spaces[] =
        "                                                                ";
    size_t count;

    if (!backend->at_line_start) {
        return true;
    }

    if (!backend->config.pretty) {
        backend->at_line_start = false;
        return true;
    }

    if (backend->indent_depth >
        backend->config.max_indent_depth) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    if (backend->indent_depth != 0u &&
        backend->config.indent_width >
            SIZE_MAX /
            backend->indent_depth) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_OVERFLOW);
    }

    count =
        backend->indent_depth *
        backend->config.indent_width;

    while (count != 0u) {
        size_t chunk;

        chunk = count;

        if (chunk >
            sizeof(spaces) - 1u) {
            chunk =
                sizeof(spaces) - 1u;
        }

        if (!vitte_c17_backend_raw_write(
                backend,
                spaces,
                chunk)) {
            return false;
        }

        count -= chunk;
    }

    backend->at_line_start = false;

    return true;
}

bool
vitte_c17_backend_indent(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    if (backend->indent_depth >=
        backend->config.max_indent_depth) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_OVERFLOW);
    }

    ++backend->indent_depth;

    if (backend->indent_depth >
        backend->stats.maximum_indent_depth) {
        backend->stats.maximum_indent_depth =
            backend->indent_depth;
    }

    return true;
}

bool
vitte_c17_backend_dedent(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    if (backend->indent_depth == 0u) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    --backend->indent_depth;

    return true;
}

/* ========================================================================= */
/* Public output                                                             */
/* ========================================================================= */

bool
vitte_c17_backend_write_n(
    vitte_c17_backend_t *backend,
    const char *text,
    size_t length)
{
    size_t offset;

    if (!vitte_c17_backend_is_valid(
            backend) ||
        (text == NULL && length != 0u)) {
        return false;
    }

    offset = 0u;

    while (offset < length) {
        size_t begin;
        size_t chunk;

        if (backend->at_line_start) {
            if (!vitte_c17_backend_emit_indent(
                    backend)) {
                return false;
            }
        }

        begin = offset;

        while (offset < length &&
               text[offset] != '\n') {
            ++offset;
        }

        chunk = offset - begin;

        if (chunk != 0u) {
            if (!vitte_c17_backend_raw_write(
                    backend,
                    text + begin,
                    chunk)) {
                return false;
            }

            backend->column =
                vitte_c17_saturating_add(
                    backend->column,
                    chunk);
        }

        if (offset < length &&
            text[offset] == '\n') {
            if (!vitte_c17_backend_raw_write(
                    backend,
                    "\n",
                    1u)) {
                return false;
            }

            ++offset;

            backend->line =
                vitte_c17_saturating_add(
                    backend->line,
                    1u);

            backend->column = 0u;
            backend->at_line_start = true;

            backend->stats.output_lines =
                vitte_c17_saturating_add(
                    backend->stats.output_lines,
                    1u);
        }
    }

    return true;
}

bool
vitte_c17_backend_write(
    vitte_c17_backend_t *backend,
    const char *text)
{
    if (text == NULL) {
        return false;
    }

    return
        vitte_c17_backend_write_n(
            backend,
            text,
            strlen(text));
}

bool
vitte_c17_backend_write_char(
    vitte_c17_backend_t *backend,
    char character)
{
    return
        vitte_c17_backend_write_n(
            backend,
            &character,
            1u);
}

bool
vitte_c17_backend_newline(
    vitte_c17_backend_t *backend)
{
    return
        vitte_c17_backend_write_n(
            backend,
            "\n",
            1u);
}

bool
vitte_c17_backend_blank_line(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_newline(
            backend)) {
        return false;
    }

    return
        vitte_c17_backend_newline(
            backend);
}

/* ========================================================================= */
/* C string escaping                                                         */
/* ========================================================================= */

bool
vitte_c17_backend_write_c_string(
    vitte_c17_backend_t *backend,
    const unsigned char *data,
    size_t length)
{
    size_t index;


    if (!vitte_c17_backend_is_valid(
            backend) ||
        (data == NULL && length != 0u)) {
        return false;
    }

    if (!vitte_c17_backend_write_char(
            backend,
            '"')) {
        return false;
    }

    for (index = 0u;
         index < length;
         ++index) {
        unsigned char c;

        c = data[index];

        switch (c) {
            case '\a':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\a")) {
                    return false;
                }
                break;

            case '\b':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\b")) {
                    return false;
                }
                break;

            case '\f':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\f")) {
                    return false;
                }
                break;

            case '\n':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\n")) {
                    return false;
                }
                break;

            case '\r':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\r")) {
                    return false;
                }
                break;

            case '\t':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\t")) {
                    return false;
                }
                break;

            case '\v':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\v")) {
                    return false;
                }
                break;

            case '\\':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\\\")) {
                    return false;
                }
                break;

            case '"':
                if (!vitte_c17_backend_write(
                        backend,
                        "\\\"")) {
                    return false;
                }
                break;

            default:
                if (c >= 0x20u &&
                    c <= 0x7Eu) {
                    /*
                     * Avoid accidental trigraph sequences in generated C
                     * when requested.
                     */
                    if (backend->config.trigraph_safe_strings &&
                        c == '?' &&
                        index + 1u < length &&
                        data[index + 1u] == '?') {
                        if (!vitte_c17_backend_write(
                                backend,
                                "\\?")) {
                            return false;
                        }
                    } else {
                        char character;

                        character = (char)c;

                        if (!vitte_c17_backend_write_char(
                                backend,
                                character)) {
                            return false;
                        }
                    }
                } else {
                    char escape[5];

                    /*
                     * Fixed-width octal is used rather than \xNN because a
                     * following hexadecimal digit would otherwise continue
                     * the hexadecimal escape.
                     */
                    escape[0] = '\\';
                    escape[1] =
                        (char)('0' +
                            ((c >> 6u) & 7u));
                    escape[2] =
                        (char)('0' +
                            ((c >> 3u) & 7u));
                    escape[3] =
                        (char)('0' +
                            (c & 7u));
                    escape[4] = '\0';

                    if (!vitte_c17_backend_write_n(
                            backend,
                            escape,
                            4u)) {
                        return false;
                    }
                }
                break;
        }
    }

    return
        vitte_c17_backend_write_char(
            backend,
            '"');
}

/* ========================================================================= */
/* Identifier escaping                                                       */
/* ========================================================================= */

static bool
vitte_c17_identifier_first(
    unsigned char c)
{
    return
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        c == '_';
}

static bool
vitte_c17_identifier_rest(
    unsigned char c)
{
    return
        vitte_c17_identifier_first(c) ||
        (c >= '0' && c <= '9');
}

static bool
vitte_c17_keyword(
    const char *text,
    size_t length)
{
#define VITTE_C17_KEYWORD(word) \
    (length == sizeof(word) - 1u && \
     memcmp(text, word, sizeof(word) - 1u) == 0)

    return
        VITTE_C17_KEYWORD("auto") ||
        VITTE_C17_KEYWORD("break") ||
        VITTE_C17_KEYWORD("case") ||
        VITTE_C17_KEYWORD("char") ||
        VITTE_C17_KEYWORD("const") ||
        VITTE_C17_KEYWORD("continue") ||
        VITTE_C17_KEYWORD("default") ||
        VITTE_C17_KEYWORD("do") ||
        VITTE_C17_KEYWORD("double") ||
        VITTE_C17_KEYWORD("else") ||
        VITTE_C17_KEYWORD("enum") ||
        VITTE_C17_KEYWORD("extern") ||
        VITTE_C17_KEYWORD("float") ||
        VITTE_C17_KEYWORD("for") ||
        VITTE_C17_KEYWORD("goto") ||
        VITTE_C17_KEYWORD("if") ||
        VITTE_C17_KEYWORD("inline") ||
        VITTE_C17_KEYWORD("int") ||
        VITTE_C17_KEYWORD("long") ||
        VITTE_C17_KEYWORD("register") ||
        VITTE_C17_KEYWORD("restrict") ||
        VITTE_C17_KEYWORD("return") ||
        VITTE_C17_KEYWORD("short") ||
        VITTE_C17_KEYWORD("signed") ||
        VITTE_C17_KEYWORD("sizeof") ||
        VITTE_C17_KEYWORD("static") ||
        VITTE_C17_KEYWORD("struct") ||
        VITTE_C17_KEYWORD("switch") ||
        VITTE_C17_KEYWORD("typedef") ||
        VITTE_C17_KEYWORD("union") ||
        VITTE_C17_KEYWORD("unsigned") ||
        VITTE_C17_KEYWORD("void") ||
        VITTE_C17_KEYWORD("volatile") ||
        VITTE_C17_KEYWORD("while") ||
        VITTE_C17_KEYWORD("_Alignas") ||
        VITTE_C17_KEYWORD("_Alignof") ||
        VITTE_C17_KEYWORD("_Atomic") ||
        VITTE_C17_KEYWORD("_Bool") ||
        VITTE_C17_KEYWORD("_Complex") ||
        VITTE_C17_KEYWORD("_Generic") ||
        VITTE_C17_KEYWORD("_Imaginary") ||
        VITTE_C17_KEYWORD("_Noreturn") ||
        VITTE_C17_KEYWORD("_Static_assert") ||
        VITTE_C17_KEYWORD("_Thread_local");

#undef VITTE_C17_KEYWORD
}

bool
vitte_c17_backend_write_identifier(
    vitte_c17_backend_t *backend,
    const char *identifier,
    size_t length)
{
    static const char hex[] =
        "0123456789ABCDEF";

    size_t index;

    if (!vitte_c17_backend_is_valid(
            backend) ||
        identifier == NULL ||
        length == 0u ||
        length >
            backend->config.max_identifier_bytes) {
        return false;
    }

    /*
     * Prefix every generated Vitte identifier. Besides preventing C keyword
     * collisions this gives generated symbols a stable namespace.
     */
    if (!vitte_c17_backend_write(
            backend,
            "vitte_")) {
        return false;
    }

    if (vitte_c17_keyword(
            identifier,
            length)) {
        if (!vitte_c17_backend_write(
                backend,
                "kw_")) {
            return false;
        }
    }

    for (index = 0u;
         index < length;
         ++index) {
        unsigned char c;
        bool valid;

        c =
            (unsigned char)
                identifier[index];

        valid =
            index == 0u
                ? vitte_c17_identifier_first(c)
                : vitte_c17_identifier_rest(c);

        if (valid) {
            if (!vitte_c17_backend_write_char(
                    backend,
                    (char)c)) {
                return false;
            }
        } else {
            char encoded[5];

            encoded[0] = '_';
            encoded[1] = 'x';
            encoded[2] =
                hex[(c >> 4u) & 0x0Fu];
            encoded[3] =
                hex[c & 0x0Fu];
            encoded[4] = '\0';

            if (!vitte_c17_backend_write_n(
                    backend,
                    encoded,
                    4u)) {
                return false;
            }
        }
    }

    return true;
}

/* ========================================================================= */
/* Source positions                                                          */
/* ========================================================================= */

void
vitte_c17_backend_set_source_location(
    vitte_c17_backend_t *backend,
    uint32_t file_id,
    size_t begin,
    size_t end)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->source.file_id = file_id;
    backend->source.begin = begin;
    backend->source.end = end;
    backend->source.valid =
        begin <= end;
}

void
vitte_c17_backend_clear_source_location(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    memset(
        &backend->source,
        0,
        sizeof(backend->source));
}

/* ========================================================================= */
/* Source-map notification                                                   */
/* ========================================================================= */

static void
vitte_c17_backend_source_map_point(
    vitte_c17_backend_t *backend)
{
    if (backend == NULL ||
        !backend->config.emit_source_map ||
        backend->source_map == NULL ||
        !backend->source.valid) {
        return;
    }

    backend->source_map(
        backend->source_map_user_data,
        backend->source.file_id,
        backend->source.begin,
        backend->source.end,
        backend->line,
        backend->column,
        backend->stats.output_bytes);

    backend->stats.source_map_entries =
        vitte_c17_saturating_add(
            backend->stats.source_map_entries,
            1u);
}

/* ========================================================================= */
/* Diagnostics                                                               */
/* ========================================================================= */

void
vitte_c17_backend_report(
    vitte_c17_backend_t *backend,
    vitte_c17_backend_diagnostic_level_t level,
    const char *code,
    const char *message)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    if (level ==
        VITTE_C17_DIAGNOSTIC_ERROR) {
        backend->stats.error_count =
            vitte_c17_saturating_add(
                backend->stats.error_count,
                1u);
    } else if (
        level ==
        VITTE_C17_DIAGNOSTIC_WARNING) {
        backend->stats.warning_count =
            vitte_c17_saturating_add(
                backend->stats.warning_count,
                1u);
    } else {
        backend->stats.note_count =
            vitte_c17_saturating_add(
                backend->stats.note_count,
                1u);
    }

    if (backend->diagnostic != NULL) {
        backend->diagnostic(
            backend->diagnostic_user_data,
            level,
            code,
            message,
            backend->source.valid
                ? &backend->source
                : NULL);
    }

    if (level ==
            VITTE_C17_DIAGNOSTIC_ERROR &&
        backend->stats.error_count >=
            backend->config.max_errors) {
        vitte_c17_backend_set_error(
            backend,
            VITTE_C17_BACKEND_ERROR_TOO_MANY_ERRORS);
    } else if (
        level ==
            VITTE_C17_DIAGNOSTIC_ERROR &&
        backend->config.fail_fast) {
        vitte_c17_backend_set_error(
            backend,
            VITTE_C17_BACKEND_ERROR_INTERNAL);
    }
}

/* ========================================================================= */
/* Sections                                                                  */
/* ========================================================================= */

bool
vitte_c17_backend_begin_section(
    vitte_c17_backend_t *backend,
    vitte_c17_section_t section)
{
    if (!vitte_c17_backend_is_valid(
            backend) ||
        section <= VITTE_C17_SECTION_NONE ||
        section >= VITTE_C17_SECTION_COUNT) {
        return false;
    }

    if (backend->state !=
        VITTE_C17_BACKEND_STATE_GENERATING) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    if (backend->current_section !=
        VITTE_C17_SECTION_NONE) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    if (backend->config.deterministic &&
        backend->last_section !=
            VITTE_C17_SECTION_NONE &&
        section <= backend->last_section) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    backend->current_section =
        section;

    backend->last_section =
        section;

    backend->stats.section_count =
        vitte_c17_saturating_add(
            backend->stats.section_count,
            1u);

    if (backend->config.emit_debug_comments) {
        if (!vitte_c17_backend_write(
                backend,
                "/* --- Vitte C17 section: ")) {
            return false;
        }

        if (!vitte_c17_backend_write(
                backend,
                vitte_c17_section_name(section))) {
            return false;
        }

        if (!vitte_c17_backend_write(
                backend,
                " --- */\n")) {
            return false;
        }
    }

    return true;
}

bool
vitte_c17_backend_end_section(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    if (backend->current_section ==
        VITTE_C17_SECTION_NONE) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    backend->current_section =
        VITTE_C17_SECTION_NONE;

    return true;
}

/* ========================================================================= */
/* Braces                                                                    */
/* ========================================================================= */

bool
vitte_c17_backend_open_block(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_write(
            backend,
            "{")) {
        return false;
    }

    if (!vitte_c17_backend_newline(
            backend)) {
        return false;
    }

    return
        vitte_c17_backend_indent(
            backend);
}

bool
vitte_c17_backend_close_block(
    vitte_c17_backend_t *backend,
    bool semicolon)
{
    if (!vitte_c17_backend_dedent(
            backend)) {
        return false;
    }

    if (!vitte_c17_backend_write(
            backend,
            "}")) {
        return false;
    }

    if (semicolon) {
        if (!vitte_c17_backend_write_char(
                backend,
                ';')) {
            return false;
        }
    }

    return true;
}

/* ========================================================================= */
/* Comments                                                                  */
/* ========================================================================= */

bool
vitte_c17_backend_comment(
    vitte_c17_backend_t *backend,
    const char *text)
{
    size_t index;

    if (!vitte_c17_backend_is_valid(
            backend) ||
        text == NULL) {
        return false;
    }

    if (!backend->config.emit_comments) {
        return true;
    }

    /*
     * Use // comments. Embedded newlines are converted into independent
     * comment lines, preventing user/source text from escaping a comment.
     */
    if (!vitte_c17_backend_write(
            backend,
            "// ")) {
        return false;
    }

    for (index = 0u;
         text[index] != '\0';
         ++index) {
        char c;

        c = text[index];

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            if (!vitte_c17_backend_newline(
                    backend) ||
                !vitte_c17_backend_write(
                    backend,
                    "// ")) {
                return false;
            }

            continue;
        }

        if (!vitte_c17_backend_write_char(
                backend,
                c)) {
            return false;
        }
    }

    return true;
}

/* ========================================================================= */
/* Cancellation                                                              */
/* ========================================================================= */

void
vitte_c17_backend_cancel(
    vitte_c17_backend_t *backend)
{
    if (backend == NULL ||
        backend->magic !=
            VITTE_C17_BACKEND_MAGIC) {
        return;
    }

    backend->cancelled = true;
}

bool
vitte_c17_backend_is_cancelled(
    const vitte_c17_backend_t *backend)
{
    return
        backend != NULL &&
        backend->magic ==
            VITTE_C17_BACKEND_MAGIC &&
        backend->cancelled;
}

/* ========================================================================= */
/* Initialization                                                            */
/* ========================================================================= */

bool
vitte_c17_backend_init(
    vitte_c17_backend_t *backend,
    const vitte_c17_backend_config_t *config,
    const vitte_c17_target_t *target,
    vitte_c17_writer_t writer)
{
    vitte_c17_backend_config_t effective_config;
    vitte_c17_target_t effective_target;

    if (backend == NULL ||
        writer.write == NULL) {
        return false;
    }

    effective_config =
        config != NULL
            ? *config
            : vitte_c17_backend_config_default();

    effective_target =
        target != NULL
            ? *target
            : vitte_c17_target_default();

    if (!vitte_c17_backend_config_validate(
            &effective_config) ||
        !vitte_c17_target_validate(
            &effective_target)) {
        return false;
    }

    memset(
        backend,
        0,
        sizeof(*backend));

    backend->magic =
        VITTE_C17_BACKEND_MAGIC;

    backend->state =
        VITTE_C17_BACKEND_STATE_READY;

    backend->last_error =
        VITTE_C17_BACKEND_ERROR_NONE;

    backend->config =
        effective_config;

    backend->target =
        effective_target;

    backend->writer =
        writer;

    backend->line = 1u;
    backend->column = 0u;

    backend->indent_depth = 0u;

    backend->at_line_start = true;

    backend->current_section =
        VITTE_C17_SECTION_NONE;

    backend->last_section =
        VITTE_C17_SECTION_NONE;

    backend->cancelled = false;

    return true;
}

/* ========================================================================= */
/* Hooks                                                                     */
/* ========================================================================= */

void
vitte_c17_backend_set_diagnostic_handler(
    vitte_c17_backend_t *backend,
    vitte_c17_diagnostic_fn handler,
    void *user_data)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->diagnostic =
        handler;

    backend->diagnostic_user_data =
        user_data;
}

void
vitte_c17_backend_set_source_map_handler(
    vitte_c17_backend_t *backend,
    vitte_c17_source_map_fn handler,
    void *user_data)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->source_map =
        handler;

    backend->source_map_user_data =
        user_data;
}

/* ========================================================================= */
/* Generation lifecycle                                                      */
/* ========================================================================= */

bool
vitte_c17_backend_begin(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    if (backend->state !=
        VITTE_C17_BACKEND_STATE_READY) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    memset(
        &backend->stats,
        0,
        sizeof(backend->stats));

    backend->state =
        VITTE_C17_BACKEND_STATE_GENERATING;

    backend->last_error =
        VITTE_C17_BACKEND_ERROR_NONE;

    backend->line = 1u;
    backend->column = 0u;

    backend->indent_depth = 0u;

    backend->at_line_start = true;

    backend->current_section =
        VITTE_C17_SECTION_NONE;

    backend->last_section =
        VITTE_C17_SECTION_NONE;

    backend->cancelled = false;

    memset(
        &backend->source,
        0,
        sizeof(backend->source));

    if (backend->config.emit_banner) {
        if (!vitte_c17_backend_begin_section(
                backend,
                VITTE_C17_SECTION_BANNER)) {
            return false;
        }

        if (!vitte_c17_backend_write(
                backend,
                "/*\n"
                " * Generated by the Vitte compiler.\n"
                " * Backend: ISO C17.\n"
                " * This file is generated; do not edit manually.\n"
                " */\n")) {
            return false;
        }

        if (!vitte_c17_backend_end_section(
                backend)) {
            return false;
        }
    }

    return true;
}

bool
vitte_c17_backend_finish(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    if (backend->state !=
        VITTE_C17_BACKEND_STATE_GENERATING) {
        return false;
    }

    if (backend->current_section !=
        VITTE_C17_SECTION_NONE ||
        backend->indent_depth != 0u) {
        return
            vitte_c17_backend_fail(
                backend,
                VITTE_C17_BACKEND_ERROR_INVALID_STATE);
    }

    if (backend->cancelled) {
        backend->state =
            VITTE_C17_BACKEND_STATE_CANCELLED;

        backend->last_error =
            VITTE_C17_BACKEND_ERROR_CANCELLED;

        return false;
    }

    backend->state =
        VITTE_C17_BACKEND_STATE_FINISHED;

    if (!vitte_c17_backend_validate(backend)) {
        backend->state = VITTE_C17_BACKEND_STATE_FAILED;
        backend->last_error = VITTE_C17_BACKEND_ERROR_INTERNAL;
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Reuse                                                                     */
/* ========================================================================= */

bool
vitte_c17_backend_reset(
    vitte_c17_backend_t *backend)
{
    if (backend == NULL ||
        backend->magic !=
            VITTE_C17_BACKEND_MAGIC) {
        return false;
    }

    if (backend->state ==
        VITTE_C17_BACKEND_STATE_GENERATING) {
        return false;
    }

    backend->state =
        VITTE_C17_BACKEND_STATE_READY;

    backend->last_error =
        VITTE_C17_BACKEND_ERROR_NONE;

    backend->line = 1u;
    backend->column = 0u;

    backend->indent_depth = 0u;
    backend->at_line_start = true;

    backend->current_section =
        VITTE_C17_SECTION_NONE;

    backend->last_section =
        VITTE_C17_SECTION_NONE;

    backend->cancelled = false;

    memset(
        &backend->source,
        0,
        sizeof(backend->source));

    memset(
        &backend->stats,
        0,
        sizeof(backend->stats));

    return true;
}

/* ========================================================================= */
/* Destruction                                                               */
/* ========================================================================= */

void
vitte_c17_backend_destroy(
    vitte_c17_backend_t *backend)
{
    if (backend == NULL ||
        backend->magic !=
            VITTE_C17_BACKEND_MAGIC) {
        return;
    }

    backend->writer.write = NULL;
    backend->writer.user_data = NULL;

    backend->diagnostic = NULL;
    backend->diagnostic_user_data = NULL;

    backend->source_map = NULL;
    backend->source_map_user_data = NULL;

    backend->current_section =
        VITTE_C17_SECTION_NONE;

    backend->state =
        VITTE_C17_BACKEND_STATE_DESTROYED;

    backend->magic =
        VITTE_C17_BACKEND_DEAD_MAGIC;
}

/* ========================================================================= */
/* Error access                                                              */
/* ========================================================================= */

vitte_c17_backend_error_t
vitte_c17_backend_last_error(
    const vitte_c17_backend_t *backend)
{
    if (backend == NULL ||
        backend->magic !=
            VITTE_C17_BACKEND_MAGIC) {
        return
            VITTE_C17_BACKEND_ERROR_INVALID_BACKEND;
    }

    return backend->last_error;
}

void
vitte_c17_backend_clear_error(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->last_error =
        VITTE_C17_BACKEND_ERROR_NONE;

    if (backend->state ==
        VITTE_C17_BACKEND_STATE_FAILED) {
        backend->state =
            VITTE_C17_BACKEND_STATE_READY;
    }
}

/* ========================================================================= */
/* State access                                                              */
/* ========================================================================= */

vitte_c17_backend_state_t
vitte_c17_backend_state(
    const vitte_c17_backend_t *backend)
{
    if (backend == NULL ||
        backend->magic !=
            VITTE_C17_BACKEND_MAGIC) {
        return
            VITTE_C17_BACKEND_STATE_UNINITIALIZED;
    }

    return backend->state;
}

vitte_c17_section_t
vitte_c17_backend_current_section(
    const vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return VITTE_C17_SECTION_NONE;
    }

    return backend->current_section;
}

/* ========================================================================= */
/* Position access                                                           */
/* ========================================================================= */

size_t
vitte_c17_backend_line(
    const vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return 0u;
    }

    return backend->line;
}

size_t
vitte_c17_backend_column(
    const vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return 0u;
    }

    return backend->column;
}

size_t
vitte_c17_backend_output_offset(
    const vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return 0u;
    }

    return backend->stats.output_bytes;
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_c17_backend_stats_t
vitte_c17_backend_stats(
    const vitte_c17_backend_t *backend)
{
    vitte_c17_backend_stats_t empty;

    memset(
        &empty,
        0,
        sizeof(empty));

    if (!vitte_c17_backend_is_valid(
            backend)) {
        return empty;
    }

    return backend->stats;
}

void
vitte_c17_backend_record_type(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->stats.type_count =
        vitte_c17_saturating_add(
            backend->stats.type_count,
            1u);
}

void
vitte_c17_backend_record_global(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->stats.global_count =
        vitte_c17_saturating_add(
            backend->stats.global_count,
            1u);
}

void
vitte_c17_backend_record_function(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->stats.function_count =
        vitte_c17_saturating_add(
            backend->stats.function_count,
            1u);
}

void
vitte_c17_backend_record_statement(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->stats.statement_count =
        vitte_c17_saturating_add(
            backend->stats.statement_count,
            1u);
}

void
vitte_c17_backend_record_expression(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    backend->stats.expression_count =
        vitte_c17_saturating_add(
            backend->stats.expression_count,
            1u);
}

/* ========================================================================= */
/* Source-map explicit mark                                                  */
/* ========================================================================= */

void
vitte_c17_backend_mark_source(
    vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return;
    }

    vitte_c17_backend_source_map_point(
        backend);
}

/* ========================================================================= */
/* Structural validation                                                     */
/* ========================================================================= */

bool
vitte_c17_backend_validate(
    const vitte_c17_backend_t *backend)
{
    if (!vitte_c17_backend_is_valid(
            backend)) {
        return false;
    }

    if (backend->line == 0u) {
        return false;
    }

    if (backend->indent_depth >
        backend->config.max_indent_depth) {
        return false;
    }

    if (backend->stats.maximum_indent_depth >
        backend->config.max_indent_depth) {
        return false;
    }

    if (backend->stats.output_bytes >
        backend->config.max_output_bytes) {
        return false;
    }

    if (backend->current_section >=
        VITTE_C17_SECTION_COUNT ||
        backend->last_section >=
            VITTE_C17_SECTION_COUNT) {
        return false;
    }

    if (backend->source.valid &&
        backend->source.begin >
            backend->source.end) {
        return false;
    }

    if (backend->state ==
            VITTE_C17_BACKEND_STATE_FINISHED &&
        backend->current_section !=
            VITTE_C17_SECTION_NONE) {
        return false;
    }

    if (backend->state ==
            VITTE_C17_BACKEND_STATE_FINISHED &&
        backend->indent_depth != 0u) {
        return false;
    }

    if (backend->state ==
            VITTE_C17_BACKEND_STATE_CANCELLED &&
        backend->last_error !=
            VITTE_C17_BACKEND_ERROR_CANCELLED) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* FILE writer adapter                                                       */
/* ========================================================================= */

bool
vitte_c17_file_writer(
    void *user_data,
    const char *data,
    size_t length)
{
    FILE *file;
    size_t written;

    if (user_data == NULL ||
        (data == NULL && length != 0u)) {
        return false;
    }

    if (length == 0u) {
        return true;
    }

    file =
        (FILE *)user_data;

    written =
        fwrite(
            data,
            1u,
            length,
            file);

    return written == length;
}

vitte_c17_writer_t
vitte_c17_writer_from_file(
    FILE *file)
{
    return
        vitte_c17_writer_make(
            vitte_c17_file_writer,
            file);
}

/* ========================================================================= */
/* Memory writer                                                             */
/* ========================================================================= */

void
vitte_c17_memory_writer_init(
    vitte_c17_memory_writer_t *writer)
{
    if (writer == NULL) {
        return;
    }

    memset(
        writer,
        0,
        sizeof(*writer));
}

void
vitte_c17_memory_writer_destroy(
    vitte_c17_memory_writer_t *writer)
{
    if (writer == NULL) {
        return;
    }

    free(writer->data);

    memset(
        writer,
        0,
        sizeof(*writer));
}

bool
vitte_c17_memory_writer_write(
    void *user_data,
    const char *data,
    size_t length)
{
    vitte_c17_memory_writer_t *writer;
    size_t required;
    size_t capacity;
    char *replacement;

    if (user_data == NULL ||
        (data == NULL && length != 0u)) {
        return false;
    }

    writer =
        (vitte_c17_memory_writer_t *)
            user_data;

    if (length == 0u) {
        return true;
    }

    if (vitte_c17_add_overflow(
            writer->length,
            length,
            &required) ||
        vitte_c17_add_overflow(
            required,
            1u,
            &required)) {
        return false;
    }

    if (required >
        writer->capacity) {
        capacity =
            writer->capacity != 0u
                ? writer->capacity
                : 256u;

        while (capacity < required) {
            if (capacity >
                SIZE_MAX / 2u) {
                capacity = required;
                break;
            }

            capacity *= 2u;
        }

        replacement =
            (char *)realloc(
                writer->data,
                capacity);

        if (replacement == NULL) {
            return false;
        }

        writer->data =
            replacement;

        writer->capacity =
            capacity;
    }

    memcpy(
        writer->data +
            writer->length,
        data,
        length);

    writer->length += length;

    writer->data[
        writer->length] = '\0';

    return true;
}

vitte_c17_writer_t
vitte_c17_writer_from_memory(
    vitte_c17_memory_writer_t *writer)
{
    return
        vitte_c17_writer_make(
            vitte_c17_memory_writer_write,
            writer);
}

/* ========================================================================= */
/* Translation-unit driver                                                   */
/* ========================================================================= */

bool
vitte_c17_backend_generate(
    vitte_c17_backend_t *backend,
    const void *translation_unit,
    vitte_c17_emit_translation_unit_fn emitter,
    void *user_data)
{
    bool ok;

    if (!vitte_c17_backend_is_valid(
            backend) ||
        translation_unit == NULL ||
        emitter == NULL) {
        return false;
    }

    if (!vitte_c17_backend_begin(
            backend)) {
        return false;
    }

    ok =
        emitter(
            backend,
            translation_unit,
            user_data);

    if (!ok) {
        if (backend->state ==
            VITTE_C17_BACKEND_STATE_GENERATING) {
            vitte_c17_backend_set_error(
                backend,
                VITTE_C17_BACKEND_ERROR_INTERNAL);
        }

        return false;
    }

    if (backend->state !=
        VITTE_C17_BACKEND_STATE_GENERATING) {
        return false;
    }

    return
        vitte_c17_backend_finish(
            backend);
}

/* ========================================================================= */
/* End                                                                       */
/* ========================================================================= */
