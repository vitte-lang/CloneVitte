/*
 * Vitte Compiler
 * src/printer/printer.c
 *
 * Canonical AST / token / diagnostic printer.
 *
 * Public contract: printer.h
 *
 * Goals:
 *   - ISO C17;
 *   - printer.h is the single public contract;
 *   - deterministic output;
 *   - human-readable AST rendering;
 *   - compact / pretty / tree / debug modes;
 *   - token stream rendering;
 *   - parser diagnostic rendering;
 *   - source snippets with underlines;
 *   - configurable indentation;
 *   - escaped strings;
 *   - optional source locations;
 *   - optional node IDs;
 *   - optional token metadata;
 *   - optional spans;
 *   - bounded output;
 *   - explicit error propagation;
 *   - FILE*, memory-buffer and callback sinks;
 *   - statistics;
 *   - validation;
 *   - reusable context.
 */

#include "printer.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Private constants                                                         */
/* ========================================================================= */

#define VITTE_PRINTER_ASCII_SPACE ((unsigned char)' ')
#define VITTE_PRINTER_ASCII_TAB   ((unsigned char)'\t')
#define VITTE_PRINTER_ASCII_LF    ((unsigned char)'\n')
#define VITTE_PRINTER_ASCII_CR    ((unsigned char)'\r')

/* ========================================================================= */
/* Private arithmetic                                                        */
/* ========================================================================= */

static bool
vitte_printer_size_add(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool
vitte_printer_size_mul(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (left != 0u &&
        right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static uint64_t
vitte_printer_u64_add_sat(
    uint64_t left,
    uint64_t right)
{
    if (UINT64_MAX - left < right) {
        return UINT64_MAX;
    }

    return left + right;
}

/* ========================================================================= */
/* Context validation                                                        */
/* ========================================================================= */

static bool
vitte_printer_context_valid(
    const vitte_printer_t *printer)
{
    if (printer == NULL) {
        return false;
    }

    if (printer->magic != VITTE_PRINTER_MAGIC) {
        return false;
    }

    if (printer->state <=
            VITTE_PRINTER_STATE_INVALID ||
        printer->state >=
            VITTE_PRINTER_STATE_DESTROYED) {
        return false;
    }

    if (printer->indent_width == 0u ||
        printer->indent_width >
            VITTE_PRINTER_MAX_INDENT_WIDTH) {
        return false;
    }

    if (printer->max_depth == 0u ||
        printer->max_output_bytes == 0u) {
        return false;
    }

    if (printer->depth > printer->max_depth) {
        return false;
    }

    if (printer->sink_kind <=
            VITTE_PRINTER_SINK_INVALID ||
        printer->sink_kind >=
            VITTE_PRINTER_SINK_COUNT) {
        return false;
    }

    switch (printer->sink_kind) {
        case VITTE_PRINTER_SINK_FILE:
            if (printer->sink.file == NULL) {
                return false;
            }
            break;

        case VITTE_PRINTER_SINK_BUFFER:
            if (printer->sink.buffer.data == NULL &&
                printer->sink.buffer.capacity != 0u) {
                return false;
            }

            if (printer->sink.buffer.length >
                printer->sink.buffer.capacity) {
                return false;
            }
            break;

        case VITTE_PRINTER_SINK_CALLBACK:
            if (printer->sink.callback.write == NULL) {
                return false;
            }
            break;

        case VITTE_PRINTER_SINK_INVALID:
        case VITTE_PRINTER_SINK_COUNT:
            return false;
    }

    return true;
}

static bool
vitte_printer_fail(
    vitte_printer_t *printer,
    vitte_printer_error_t error)
{
    if (printer != NULL &&
        printer->magic == VITTE_PRINTER_MAGIC) {
        printer->last_error = error;

        if (printer->state !=
            VITTE_PRINTER_STATE_DESTROYED) {
            printer->state =
                VITTE_PRINTER_STATE_FAILED;
        }
    }

    return false;
}

/* ========================================================================= */
/* Buffer management                                                         */
/* ========================================================================= */

static bool
vitte_printer_buffer_reserve(
    vitte_printer_t *printer,
    size_t required)
{
    size_t capacity;
    size_t bytes;
    char *replacement;

    if (printer == NULL ||
        printer->sink_kind !=
            VITTE_PRINTER_SINK_BUFFER) {
        return false;
    }

    if (required <=
        printer->sink.buffer.capacity) {
        return true;
    }

    capacity = printer->sink.buffer.capacity;

    if (capacity == 0u) {
        capacity =
            VITTE_PRINTER_DEFAULT_BUFFER_CAPACITY;
    }

    while (capacity < required) {
        size_t doubled;

        if (!vitte_printer_size_mul(
                capacity,
                (size_t)2u,
                &doubled) ||
            doubled <= capacity) {
            capacity = required;
            break;
        }

        capacity = doubled;
    }

    bytes = capacity;

    replacement =
        (char *)realloc(
            printer->sink.buffer.data,
            bytes);

    if (replacement == NULL) {
        printer->stats.allocation_failures =
            vitte_printer_u64_add_sat(
                printer->stats.allocation_failures,
                UINT64_C(1));

        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_OUT_OF_MEMORY);
    }

    if (printer->sink.buffer.data == NULL) {
        printer->stats.allocations =
            vitte_printer_u64_add_sat(
                printer->stats.allocations,
                UINT64_C(1));
    } else {
        printer->stats.reallocations =
            vitte_printer_u64_add_sat(
                printer->stats.reallocations,
                UINT64_C(1));
    }

    printer->sink.buffer.data = replacement;
    printer->sink.buffer.capacity = capacity;

    return true;
}

/* ========================================================================= */
/* Low-level output                                                          */
/* ========================================================================= */

static bool
vitte_printer_check_output_limit(
    vitte_printer_t *printer,
    size_t additional)
{
    size_t total;

    if (!vitte_printer_size_add(
            printer->bytes_written,
            additional,
            &total)) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_OVERFLOW);
    }

    if (total > printer->max_output_bytes) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_OUTPUT_LIMIT);
    }

    return true;
}

static bool
vitte_printer_write_raw(
    vitte_printer_t *printer,
    const char *data,
    size_t length)
{
    size_t written;

    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (data == NULL && length != 0u) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    if (length == 0u) {
        return true;
    }

    if (!vitte_printer_check_output_limit(
            printer,
            length)) {
        return false;
    }

    written = 0u;

    switch (printer->sink_kind) {
        case VITTE_PRINTER_SINK_FILE:
        {
            size_t result;

            result =
                fwrite(
                    data,
                    1u,
                    length,
                    printer->sink.file);

            if (result != length) {
                return vitte_printer_fail(
                    printer,
                    VITTE_PRINTER_ERROR_IO);
            }

            written = result;
            break;
        }

        case VITTE_PRINTER_SINK_BUFFER:
        {
            size_t required;

            if (!vitte_printer_size_add(
                    printer->sink.buffer.length,
                    length,
                    &required) ||
                !vitte_printer_size_add(
                    required,
                    1u,
                    &required)) {
                return vitte_printer_fail(
                    printer,
                    VITTE_PRINTER_ERROR_OVERFLOW);
            }

            if (!vitte_printer_buffer_reserve(
                    printer,
                    required)) {
                return false;
            }

            memcpy(
                printer->sink.buffer.data +
                    printer->sink.buffer.length,
                data,
                length);

            printer->sink.buffer.length += length;

            printer->sink.buffer.data[
                printer->sink.buffer.length] = '\0';

            written = length;
            break;
        }

        case VITTE_PRINTER_SINK_CALLBACK:
        {
            size_t result;

            result =
                printer->sink.callback.write(
                    printer->sink.callback.userdata,
                    data,
                    length);

            if (result != length) {
                return vitte_printer_fail(
                    printer,
                    VITTE_PRINTER_ERROR_CALLBACK);
            }

            written = result;
            break;
        }

        case VITTE_PRINTER_SINK_INVALID:
        case VITTE_PRINTER_SINK_COUNT:
            return vitte_printer_fail(
                printer,
                VITTE_PRINTER_ERROR_INVALID_SINK);
    }

    printer->bytes_written += written;

    printer->stats.bytes_written =
        vitte_printer_u64_add_sat(
            printer->stats.bytes_written,
            (uint64_t)written);

    return true;
}

static bool
vitte_printer_write_cstr(
    vitte_printer_t *printer,
    const char *text)
{
    if (text == NULL) {
        return vitte_printer_write_raw(
            printer,
            "(null)",
            sizeof("(null)") - 1u);
    }

    return vitte_printer_write_raw(
        printer,
        text,
        strlen(text));
}

static bool
vitte_printer_write_char(
    vitte_printer_t *printer,
    char value)
{
    return vitte_printer_write_raw(
        printer,
        &value,
        1u);
}

static bool vitte_printer_vprintf_private(
    vitte_printer_t *printer,
    const char *format,
    va_list arguments)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 0)))
#endif
    ;

static bool
vitte_printer_vprintf_private(
    vitte_printer_t *printer,
    const char *format,
    va_list arguments)
{
    char stack_buffer[256];
    va_list copy;
    int required;

    if (format == NULL) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    va_copy(copy, arguments);

    required =
        vsnprintf(
            stack_buffer,
            sizeof(stack_buffer),
            format,
            copy);

    va_end(copy);

    if (required < 0) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_FORMAT);
    }

    if ((size_t)required <
        sizeof(stack_buffer)) {
        return vitte_printer_write_raw(
            printer,
            stack_buffer,
            (size_t)required);
    }

    {
        size_t allocation_size;
        char *buffer;
        int second_result;

        if (!vitte_printer_size_add(
                (size_t)required,
                1u,
                &allocation_size)) {
            return vitte_printer_fail(
                printer,
                VITTE_PRINTER_ERROR_OVERFLOW);
        }

        buffer =
            (char *)malloc(allocation_size);

        if (buffer == NULL) {
            printer->stats.allocation_failures =
                vitte_printer_u64_add_sat(
                    printer->stats.allocation_failures,
                    UINT64_C(1));

            return vitte_printer_fail(
                printer,
                VITTE_PRINTER_ERROR_OUT_OF_MEMORY);
        }

        printer->stats.allocations =
            vitte_printer_u64_add_sat(
                printer->stats.allocations,
                UINT64_C(1));

        va_copy(copy, arguments);

        second_result =
            vsnprintf(
                buffer,
                allocation_size,
                format,
                copy);

        va_end(copy);

        if (second_result < 0 ||
            second_result != required) {
            free(buffer);

            return vitte_printer_fail(
                printer,
                VITTE_PRINTER_ERROR_FORMAT);
        }

        if (!vitte_printer_write_raw(
                printer,
                buffer,
                (size_t)required)) {
            free(buffer);
            return false;
        }

        free(buffer);
    }

    return true;
}

static bool
vitte_printer_printf_private(
    vitte_printer_t *printer,
    const char *format,
    ...)
{
    va_list arguments;
    bool result;

    va_start(arguments, format);

    result =
        vitte_printer_vprintf_private(
            printer,
            format,
            arguments);

    va_end(arguments);

    return result;
}

/* ========================================================================= */
/* Whitespace                                                                */
/* ========================================================================= */

static bool
vitte_printer_newline(
    vitte_printer_t *printer)
{
    if (!vitte_printer_write_char(
            printer,
            '\n')) {
        return false;
    }

    printer->at_line_start = true;

    printer->stats.lines_written =
        vitte_printer_u64_add_sat(
            printer->stats.lines_written,
            UINT64_C(1));

    return true;
}

static bool
vitte_printer_indent(
    vitte_printer_t *printer)
{
    size_t count;
    size_t index;

    if (!printer->at_line_start) {
        return true;
    }

    if (!vitte_printer_size_mul(
            printer->depth,
            printer->indent_width,
            &count)) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_OVERFLOW);
    }

    for (index = 0u; index < count; ++index) {
        if (!vitte_printer_write_char(
                printer,
                ' ')) {
            return false;
        }
    }

    printer->at_line_start = false;

    return true;
}

static bool
vitte_printer_line(
    vitte_printer_t *printer,
    const char *text)
{
    if (!vitte_printer_indent(printer)) {
        return false;
    }

    if (text != NULL &&
        !vitte_printer_write_cstr(
            printer,
            text)) {
        return false;
    }

    return vitte_printer_newline(printer);
}

static bool
vitte_printer_push_depth(
    vitte_printer_t *printer)
{
    if (printer->depth >=
        printer->max_depth) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_DEPTH_LIMIT);
    }

    ++printer->depth;

    if ((uint64_t)printer->depth >
        printer->stats.max_depth) {
        printer->stats.max_depth =
            (uint64_t)printer->depth;
    }

    return true;
}

static void
vitte_printer_pop_depth(
    vitte_printer_t *printer)
{
    if (printer != NULL &&
        printer->depth != 0u) {
        --printer->depth;
    }
}

/* ========================================================================= */
/* Escaping                                                                  */
/* ========================================================================= */

static bool
vitte_printer_write_hex_byte(
    vitte_printer_t *printer,
    unsigned char byte)
{
    static const char digits[] =
        "0123456789abcdef";

    char output[4];

    output[0] = '\\';
    output[1] = 'x';
    output[2] = digits[(byte >> 4u) & 0x0fu];
    output[3] = digits[byte & 0x0fu];

    return vitte_printer_write_raw(
        printer,
        output,
        sizeof(output));
}

static bool
vitte_printer_write_escaped(
    vitte_printer_t *printer,
    const char *data,
    size_t length,
    char quote)
{
    size_t index;

    if (data == NULL && length != 0u) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    for (index = 0u; index < length; ++index) {
        unsigned char byte;

        byte = (unsigned char)data[index];

        switch (byte) {
            case '\0':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\0")) {
                    return false;
                }
                break;

            case '\a':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\a")) {
                    return false;
                }
                break;

            case '\b':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\b")) {
                    return false;
                }
                break;

            case '\f':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\f")) {
                    return false;
                }
                break;

            case '\n':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\n")) {
                    return false;
                }
                break;

            case '\r':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\r")) {
                    return false;
                }
                break;

            case '\t':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\t")) {
                    return false;
                }
                break;

            case '\v':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\v")) {
                    return false;
                }
                break;

            case '\\':
                if (!vitte_printer_write_cstr(
                        printer,
                        "\\\\")) {
                    return false;
                }
                break;

            case '\'':
                if (quote == '\'' &&
                    !vitte_printer_write_cstr(
                        printer,
                        "\\'")) {
                    return false;
                }

                if (quote != '\'' &&
                    !vitte_printer_write_char(
                        printer,
                        '\'')) {
                    return false;
                }
                break;

            case '"':
                if (quote == '"' &&
                    !vitte_printer_write_cstr(
                        printer,
                        "\\\"")) {
                    return false;
                }

                if (quote != '"' &&
                    !vitte_printer_write_char(
                        printer,
                        '"')) {
                    return false;
                }
                break;

            default:
                if (byte < 0x20u ||
                    byte == 0x7fu) {
                    if (!vitte_printer_write_hex_byte(
                            printer,
                            byte)) {
                        return false;
                    }
                } else {
                    if (!vitte_printer_write_raw(
                            printer,
                            (const char *)&data[index],
                            1u)) {
                        return false;
                    }
                }
                break;
        }
    }

    return true;
}

static bool
vitte_printer_write_quoted(
    vitte_printer_t *printer,
    const char *data,
    size_t length,
    char quote)
{
    if (!vitte_printer_write_char(
            printer,
            quote)) {
        return false;
    }

    if (!vitte_printer_write_escaped(
            printer,
            data,
            length,
            quote)) {
        return false;
    }

    return vitte_printer_write_char(
        printer,
        quote);
}

/* ========================================================================= */
/* Token names                                                               */
/* ========================================================================= */

const char *
vitte_printer_token_kind_name(
    vitte_token_kind_t kind)
{
    switch (kind) {
        case VITTE_TOKEN_INVALID:
            return "invalid";

        case VITTE_TOKEN_EOF:
            return "eof";

        case VITTE_TOKEN_IDENTIFIER:
            return "identifier";

        case VITTE_TOKEN_INTEGER_LITERAL:
            return "integer_literal";

        case VITTE_TOKEN_FLOAT_LITERAL:
            return "float_literal";

        case VITTE_TOKEN_STRING_LITERAL:
            return "string_literal";

        case VITTE_TOKEN_CHARACTER_LITERAL:
            return "character_literal";

        case VITTE_TOKEN_KW_SPACE:
            return "space";

        case VITTE_TOKEN_KW_USE:
            return "use";

        case VITTE_TOKEN_KW_PUB:
            return "pub";

        case VITTE_TOKEN_KW_CONST:
            return "const";

        case VITTE_TOKEN_KW_STATIC:
            return "static";

        case VITTE_TOKEN_KW_TYPE:
            return "type";

        case VITTE_TOKEN_KW_OPAQUE:
            return "opaque";

        case VITTE_TOKEN_KW_FORM:
            return "form";

        case VITTE_TOKEN_KW_PICK:
            return "pick";

        case VITTE_TOKEN_KW_TRAIT:
            return "trait";

        case VITTE_TOKEN_KW_IMPL:
            return "impl";

        case VITTE_TOKEN_KW_DYN:
            return "dyn";

        case VITTE_TOKEN_KW_WHERE:
            return "where";

        case VITTE_TOKEN_KW_PROC:
            return "proc";

        case VITTE_TOKEN_KW_EXTERN:
            return "extern";

        case VITTE_TOKEN_KW_MACRO:
            return "macro";

        case VITTE_TOKEN_KW_COMPTIME:
            return "comptime";

        case VITTE_TOKEN_KW_TEST:
            return "test";

        case VITTE_TOKEN_KW_LET:
            return "let";

        case VITTE_TOKEN_KW_MUT:
            return "mut";

        case VITTE_TOKEN_KW_SET:
            return "set";

        case VITTE_TOKEN_KW_RETURN:
            return "return";

        case VITTE_TOKEN_KW_DEFER:
            return "defer";

        case VITTE_TOKEN_KW_REQUIRES:
            return "requires";

        case VITTE_TOKEN_KW_ENSURES:
            return "ensures";

        case VITTE_TOKEN_KW_EXPORT:
            return "export";

        case VITTE_TOKEN_KW_IF:
            return "if";

        case VITTE_TOKEN_KW_ELSE:
            return "else";

        case VITTE_TOKEN_KW_ELIF:
            return "elif";

        case VITTE_TOKEN_KW_WHILE:
            return "while";

        case VITTE_TOKEN_KW_LOOP:
            return "loop";

        case VITTE_TOKEN_KW_FOR:
            return "for";

        case VITTE_TOKEN_KW_IN:
            return "in";

        case VITTE_TOKEN_KW_BREAK:
            return "break";

        case VITTE_TOKEN_KW_CONTINUE:
            return "continue";

        case VITTE_TOKEN_KW_MATCH:
            return "match";

        case VITTE_TOKEN_KW_AS:
            return "as";

        case VITTE_TOKEN_KW_ASYNC:
            return "async";

        case VITTE_TOKEN_KW_AWAIT:
            return "await";

        case VITTE_TOKEN_KW_UNSAFE:
            return "unsafe";

        case VITTE_TOKEN_KW_ASM:
            return "asm";

        case VITTE_TOKEN_KW_MOVE:
            return "move";

        case VITTE_TOKEN_KW_REF:
            return "ref";

        case VITTE_TOKEN_KW_SELF:
            return "self";

        case VITTE_TOKEN_KW_AND:
            return "and";

        case VITTE_TOKEN_KW_OR:
            return "or";

        case VITTE_TOKEN_KW_NOT:
            return "not";

        case VITTE_TOKEN_KW_TRUE:
            return "true";

        case VITTE_TOKEN_KW_FALSE:
            return "false";

        case VITTE_TOKEN_KW_NULL:
            return "null";

        case VITTE_TOKEN_KW_ASSERT:
            return "assert";

        case VITTE_TOKEN_KW_MAP:
            return "map";

        case VITTE_TOKEN_KW_SIZEOF:
            return "sizeof";

        case VITTE_TOKEN_KW_ALIGNOF:
            return "alignof";

        case VITTE_TOKEN_KW_OFFSETOF:
            return "offsetof";

        case VITTE_TOKEN_KW_TYPEOF:
            return "typeof";

        case VITTE_TOKEN_KW_PRINT:
            return "print";

        case VITTE_TOKEN_KW_RESERVED:
            return "reserved";

        case VITTE_TOKEN_LEFT_PAREN:
            return "left_paren";

        case VITTE_TOKEN_RIGHT_PAREN:
            return "right_paren";

        case VITTE_TOKEN_LEFT_BRACE:
            return "left_brace";

        case VITTE_TOKEN_RIGHT_BRACE:
            return "right_brace";

        case VITTE_TOKEN_LEFT_BRACKET:
            return "left_bracket";

        case VITTE_TOKEN_RIGHT_BRACKET:
            return "right_bracket";

        case VITTE_TOKEN_COMMA:
            return "comma";

        case VITTE_TOKEN_SEMICOLON:
            return "semicolon";

        case VITTE_TOKEN_COLON:
            return "colon";

        case VITTE_TOKEN_COLON_COLON:
            return "colon_colon";

        case VITTE_TOKEN_DOT:
            return "dot";

        case VITTE_TOKEN_DOT_DOT:
            return "dot_dot";

        case VITTE_TOKEN_DOT_DOT_EQUAL:
            return "dot_dot_equal";

        case VITTE_TOKEN_ARROW:
            return "arrow";

        case VITTE_TOKEN_FAT_ARROW:
            return "fat_arrow";

        case VITTE_TOKEN_PLUS:
            return "plus";

        case VITTE_TOKEN_MINUS:
            return "minus";

        case VITTE_TOKEN_STAR:
            return "star";

        case VITTE_TOKEN_SLASH:
            return "slash";

        case VITTE_TOKEN_PERCENT:
            return "percent";

        case VITTE_TOKEN_AMP:
            return "amp";

        case VITTE_TOKEN_PIPE:
            return "pipe";

        case VITTE_TOKEN_CARET:
            return "caret";

        case VITTE_TOKEN_TILDE:
            return "tilde";

        case VITTE_TOKEN_BANG:
            return "bang";

        case VITTE_TOKEN_AMP_AMP:
            return "amp_amp";

        case VITTE_TOKEN_PIPE_PIPE:
            return "pipe_pipe";

        case VITTE_TOKEN_EQUAL:
            return "equal";

        case VITTE_TOKEN_EQUAL_EQUAL:
            return "equal_equal";

        case VITTE_TOKEN_BANG_EQUAL:
            return "bang_equal";

        case VITTE_TOKEN_LESS:
            return "less";

        case VITTE_TOKEN_LESS_EQUAL:
            return "less_equal";

        case VITTE_TOKEN_GREATER:
            return "greater";

        case VITTE_TOKEN_GREATER_EQUAL:
            return "greater_equal";

        case VITTE_TOKEN_SHIFT_LEFT:
            return "shift_left";

        case VITTE_TOKEN_SHIFT_RIGHT:
            return "shift_right";

        case VITTE_TOKEN_PLUS_EQUAL:
            return "plus_equal";

        case VITTE_TOKEN_MINUS_EQUAL:
            return "minus_equal";

        case VITTE_TOKEN_STAR_EQUAL:
            return "star_equal";

        case VITTE_TOKEN_SLASH_EQUAL:
            return "slash_equal";

        case VITTE_TOKEN_PERCENT_EQUAL:
            return "percent_equal";

        case VITTE_TOKEN_AMP_EQUAL:
            return "amp_equal";

        case VITTE_TOKEN_PIPE_EQUAL:
            return "pipe_equal";

        case VITTE_TOKEN_CARET_EQUAL:
            return "caret_equal";

        case VITTE_TOKEN_SHIFT_LEFT_EQUAL:
            return "shift_left_equal";

        case VITTE_TOKEN_SHIFT_RIGHT_EQUAL:
            return "shift_right_equal";

        case VITTE_TOKEN_QUESTION:
            return "question";

        case VITTE_TOKEN_QUESTION_QUESTION:
            return "question_question";

        case VITTE_TOKEN_AT:
            return "at";

        case VITTE_TOKEN_HASH:
            return "hash";

        case VITTE_TOKEN_DOLLAR:
            return "dollar";

        case VITTE_TOKEN_COUNT:
            return "count";
    }

    return "unknown";
}

/* ========================================================================= */
/* Error / state / mode names                                                */
/* ========================================================================= */

const char *
vitte_printer_error_name(
    vitte_printer_error_t error)
{
    switch (error) {
        case VITTE_PRINTER_ERROR_NONE:
            return "none";

        case VITTE_PRINTER_ERROR_INVALID_ARGUMENT:
            return "invalid_argument";

        case VITTE_PRINTER_ERROR_INVALID_CONTEXT:
            return "invalid_context";

        case VITTE_PRINTER_ERROR_INVALID_STATE:
            return "invalid_state";

        case VITTE_PRINTER_ERROR_INVALID_SINK:
            return "invalid_sink";

        case VITTE_PRINTER_ERROR_INVALID_NODE:
            return "invalid_node";

        case VITTE_PRINTER_ERROR_INVALID_TOKEN:
            return "invalid_token";

        case VITTE_PRINTER_ERROR_INVALID_SOURCE:
            return "invalid_source";

        case VITTE_PRINTER_ERROR_DEPTH_LIMIT:
            return "depth_limit";

        case VITTE_PRINTER_ERROR_OUTPUT_LIMIT:
            return "output_limit";

        case VITTE_PRINTER_ERROR_OVERFLOW:
            return "overflow";

        case VITTE_PRINTER_ERROR_OUT_OF_MEMORY:
            return "out_of_memory";

        case VITTE_PRINTER_ERROR_IO:
            return "io";

        case VITTE_PRINTER_ERROR_CALLBACK:
            return "callback";

        case VITTE_PRINTER_ERROR_FORMAT:
            return "format";

        case VITTE_PRINTER_ERROR_VALIDATION:
            return "validation";

        case VITTE_PRINTER_ERROR_INTERNAL:
            return "internal";

        case VITTE_PRINTER_ERROR_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_printer_state_name(
    vitte_printer_state_t state)
{
    switch (state) {
        case VITTE_PRINTER_STATE_INVALID:
            return "invalid";

        case VITTE_PRINTER_STATE_READY:
            return "ready";

        case VITTE_PRINTER_STATE_PRINTING:
            return "printing";

        case VITTE_PRINTER_STATE_FAILED:
            return "failed";

        case VITTE_PRINTER_STATE_DESTROYED:
            return "destroyed";

        case VITTE_PRINTER_STATE_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_printer_mode_name(
    vitte_printer_mode_t mode)
{
    switch (mode) {
        case VITTE_PRINTER_MODE_COMPACT:
            return "compact";

        case VITTE_PRINTER_MODE_PRETTY:
            return "pretty";

        case VITTE_PRINTER_MODE_TREE:
            return "tree";

        case VITTE_PRINTER_MODE_DEBUG:
            return "debug";

        case VITTE_PRINTER_MODE_COUNT:
            return "count";
    }

    return "unknown";
}

/* ========================================================================= */
/* Token rendering                                                           */
/* ========================================================================= */

static bool
vitte_printer_print_token_location(
    vitte_printer_t *printer,
    const vitte_token_t *token)
{
    if (!printer->options.show_locations ||
        token == NULL) {
        return true;
    }

    return vitte_printer_printf_private(
        printer,
        " @%u:%zu:%zu",
        token->span.file_id,
        token->line,
        token->column);
}

static bool
vitte_printer_print_token_span(
    vitte_printer_t *printer,
    const vitte_token_t *token)
{
    if (!printer->options.show_spans ||
        token == NULL) {
        return true;
    }

    return vitte_printer_printf_private(
        printer,
        " [%zu..%zu)",
        token->span.begin,
        token->span.end);
}

bool
vitte_printer_print_token(
    vitte_printer_t *printer,
    const vitte_token_t *token)
{
    const char *kind_name;

    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (token == NULL) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_TOKEN);
    }

    kind_name =
        vitte_printer_token_kind_name(
            token->kind);

    if (!vitte_printer_indent(printer)) {
        return false;
    }

    if (!vitte_printer_write_cstr(
            printer,
            kind_name)) {
        return false;
    }

    if (token->lexeme != NULL &&
        token->length != 0u) {
        if (!vitte_printer_write_cstr(
                printer,
                " ")) {
            return false;
        }

        if (!vitte_printer_write_quoted(
                printer,
                token->lexeme,
                token->length,
                '"')) {
            return false;
        }
    }

    if (!vitte_printer_print_token_location(
            printer,
            token) ||
        !vitte_printer_print_token_span(
            printer,
            token)) {
        return false;
    }

    if (printer->options.show_token_metadata) {
        if (!vitte_printer_printf_private(
                printer,
                " {len=%zu}",
                token->length)) {
            return false;
        }
    }

    if (!vitte_printer_newline(printer)) {
        return false;
    }

    printer->stats.tokens_printed =
        vitte_printer_u64_add_sat(
            printer->stats.tokens_printed,
            UINT64_C(1));

    return true;
}

bool
vitte_printer_print_tokens(
    vitte_printer_t *printer,
    const vitte_token_t *tokens,
    size_t token_count)
{
    size_t index;

    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (tokens == NULL && token_count != 0u) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    printer->state =
        VITTE_PRINTER_STATE_PRINTING;

    for (index = 0u;
         index < token_count;
         ++index) {
        if (!vitte_printer_print_token(
                printer,
                &tokens[index])) {
            return false;
        }
    }

    printer->state =
        VITTE_PRINTER_STATE_READY;

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    return true;
}

/* ========================================================================= */
/* AST metadata                                                              */
/* ========================================================================= */

static bool
vitte_printer_print_ast_flags(
    vitte_printer_t *printer,
    vitte_ast_flags_t flags)
{
    bool first;

    if (flags == VITTE_AST_FLAG_NONE) {
        return true;
    }

    if (!vitte_printer_write_cstr(
            printer,
            " flags=[")) {
        return false;
    }

    first = true;

#define VITTE_PRINTER_FLAG(flag_value, text_value)            \
    do {                                                       \
        if ((flags & (flag_value)) != 0u) {                   \
            if (!first &&                                     \
                !vitte_printer_write_cstr(printer, ",")) {    \
                return false;                                  \
            }                                                  \
            if (!vitte_printer_write_cstr(                    \
                    printer,                                  \
                    (text_value))) {                           \
                return false;                                  \
            }                                                  \
            first = false;                                     \
        }                                                      \
    } while (0)

    VITTE_PRINTER_FLAG(
        VITTE_AST_FLAG_PUBLIC,
        "pub");

    VITTE_PRINTER_FLAG(
        VITTE_AST_FLAG_MUTABLE,
        "mut");

    VITTE_PRINTER_FLAG(
        VITTE_AST_FLAG_EXTERN,
        "extern");

    VITTE_PRINTER_FLAG(
        VITTE_AST_FLAG_ASYNC,
        "async");

    VITTE_PRINTER_FLAG(
        VITTE_AST_FLAG_UNSAFE,
        "unsafe");

    VITTE_PRINTER_FLAG(
        VITTE_AST_FLAG_COMPTIME,
        "comptime");

#undef VITTE_PRINTER_FLAG

    return vitte_printer_write_char(
        printer,
        ']');
}

static bool
vitte_printer_print_ast_span(
    vitte_printer_t *printer,
    const vitte_ast_node_t *node)
{
    if (!printer->options.show_spans ||
        node == NULL ||
        !node->span.valid) {
        return true;
    }

    return vitte_printer_printf_private(
        printer,
        " span=%u:%zu..%zu",
        node->span.file_id,
        node->span.begin,
        node->span.end);
}

static bool
vitte_printer_print_ast_location(
    vitte_printer_t *printer,
    const vitte_ast_node_t *node)
{
    if (!printer->options.show_locations ||
        node == NULL ||
        !node->span.valid) {
        return true;
    }

    return vitte_printer_printf_private(
        printer,
        " loc=%zu:%zu",
        node->span.line,
        node->span.column);
}

static bool
vitte_printer_print_node_token(
    vitte_printer_t *printer,
    const vitte_parser_t *parser,
    const vitte_ast_node_t *node)
{
    const vitte_token_t *token;

    if (parser == NULL ||
        node == NULL ||
        node->token_index ==
            VITTE_PARSER_NO_TOKEN ||
        node->token_index >=
            parser->token_count) {
        return true;
    }

    token =
        &parser->tokens[node->token_index];

    if (token->lexeme == NULL ||
        token->length == 0u) {
        return true;
    }

    if (!vitte_printer_write_cstr(
            printer,
            " value=")) {
        return false;
    }

    return vitte_printer_write_quoted(
        printer,
        token->lexeme,
        token->length,
        '"');
}

/* ========================================================================= */
/* Tree rendering                                                            */
/* ========================================================================= */

static bool
vitte_printer_print_ast_node_recursive(
    vitte_printer_t *printer,
    const vitte_parser_t *parser,
    vitte_ast_node_id_t id)
{
    const vitte_ast_node_t *node;
    const char *name;
    size_t index;

    if (printer->depth >=
        printer->max_depth) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_DEPTH_LIMIT);
    }

    node =
        vitte_parser_get_node(
            parser,
            id);

    if (node == NULL) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_NODE);
    }

    name =
        vitte_ast_node_kind_name(
            node->kind);

    if (!vitte_printer_indent(printer)) {
        return false;
    }

    if (printer->options.show_node_ids) {
        if (!vitte_printer_printf_private(
                printer,
                "#%" PRIu64 " ",
                (uint64_t)node->id)) {
            return false;
        }
    }

    if (!vitte_printer_write_cstr(
            printer,
            name)) {
        return false;
    }

    if (!vitte_printer_print_node_token(
            printer,
            parser,
            node)) {
        return false;
    }

    if (node->operator_kind !=
        VITTE_TOKEN_INVALID) {
        if (!vitte_printer_printf_private(
                printer,
                " op=%s",
                vitte_printer_token_kind_name(
                    node->operator_kind))) {
            return false;
        }
    }

    if (!vitte_printer_print_ast_flags(
            printer,
            node->flags) ||
        !vitte_printer_print_ast_location(
            printer,
            node) ||
        !vitte_printer_print_ast_span(
            printer,
            node)) {
        return false;
    }

    if (printer->options.show_child_counts) {
        if (!vitte_printer_printf_private(
                printer,
                " children=%zu",
                node->child_count)) {
            return false;
        }
    }

    if (!vitte_printer_newline(printer)) {
        return false;
    }

    printer->stats.nodes_printed =
        vitte_printer_u64_add_sat(
            printer->stats.nodes_printed,
            UINT64_C(1));

    if (node->child_count == 0u) {
        return true;
    }

    if (!vitte_printer_push_depth(printer)) {
        return false;
    }

    for (index = 0u;
         index < node->child_count;
         ++index) {
        if (!vitte_printer_print_ast_node_recursive(
                printer,
                parser,
                node->children[index])) {
            vitte_printer_pop_depth(printer);
            return false;
        }
    }

    vitte_printer_pop_depth(printer);

    return true;
}

/* ========================================================================= */
/* S-expression AST rendering                                                */
/* ========================================================================= */

static bool
vitte_printer_print_ast_compact_recursive(
    vitte_printer_t *printer,
    const vitte_parser_t *parser,
    vitte_ast_node_id_t id,
    size_t depth)
{
    const vitte_ast_node_t *node;
    size_t index;

    if (depth >= printer->max_depth) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_DEPTH_LIMIT);
    }

    node =
        vitte_parser_get_node(
            parser,
            id);

    if (node == NULL) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_NODE);
    }

    if (!vitte_printer_write_char(
            printer,
            '(') ||
        !vitte_printer_write_cstr(
            printer,
            vitte_ast_node_kind_name(
                node->kind))) {
        return false;
    }

    if (printer->options.show_node_ids) {
        if (!vitte_printer_printf_private(
                printer,
                " #%" PRIu64,
                (uint64_t)node->id)) {
            return false;
        }
    }

    if (!vitte_printer_print_node_token(
            printer,
            parser,
            node)) {
        return false;
    }

    if (node->operator_kind !=
        VITTE_TOKEN_INVALID) {
        if (!vitte_printer_printf_private(
                printer,
                " op=%s",
                vitte_printer_token_kind_name(
                    node->operator_kind))) {
            return false;
        }
    }

    for (index = 0u;
         index < node->child_count;
         ++index) {
        if (!vitte_printer_write_char(
                printer,
                ' ') ||
            !vitte_printer_print_ast_compact_recursive(
                printer,
                parser,
                node->children[index],
                depth + 1u)) {
            return false;
        }
    }

    if (!vitte_printer_write_char(
            printer,
            ')')) {
        return false;
    }

    printer->stats.nodes_printed =
        vitte_printer_u64_add_sat(
            printer->stats.nodes_printed,
            UINT64_C(1));

    return true;
}

/* ========================================================================= */
/* AST public API                                                            */
/* ========================================================================= */

bool
vitte_printer_print_ast_node(
    vitte_printer_t *printer,
    const vitte_parser_t *parser,
    vitte_ast_node_id_t id)
{
    bool result = false;

    if (!vitte_printer_context_valid(printer) ||
        parser == NULL ||
        !vitte_parser_is_valid(parser)) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    if (vitte_parser_get_node(
            parser,
            id) == NULL) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_NODE);
    }

    printer->state =
        VITTE_PRINTER_STATE_PRINTING;

    switch (printer->mode) {
        case VITTE_PRINTER_MODE_COMPACT:
            result =
                vitte_printer_print_ast_compact_recursive(
                    printer,
                    parser,
                    id,
                    0u);

            if (result) {
                result =
                    vitte_printer_newline(
                        printer);
            }
            break;

        case VITTE_PRINTER_MODE_PRETTY:
        case VITTE_PRINTER_MODE_TREE:
        case VITTE_PRINTER_MODE_DEBUG:
            result =
                vitte_printer_print_ast_node_recursive(
                    printer,
                    parser,
                    id);
            break;

        case VITTE_PRINTER_MODE_COUNT:
            result = false;
            break;
    }

    if (!result) {
        return false;
    }

    printer->state =
        VITTE_PRINTER_STATE_READY;

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    return true;
}

bool
vitte_printer_print_ast(
    vitte_printer_t *printer,
    const vitte_parser_t *parser)
{
    vitte_ast_node_id_t root;

    if (!vitte_printer_context_valid(printer) ||
        parser == NULL ||
        !vitte_parser_is_valid(parser)) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    root = vitte_parser_root(parser);

    if (root == VITTE_AST_INVALID_ID) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_NODE);
    }

    return vitte_printer_print_ast_node(
        printer,
        parser,
        root);
}

/* ========================================================================= */
/* Source line utilities                                                     */
/* ========================================================================= */

static bool
vitte_printer_source_line_bounds(
    const char *source,
    size_t source_length,
    size_t offset,
    size_t *line_begin,
    size_t *line_end)
{
    size_t begin;
    size_t end;

    if (source == NULL ||
        line_begin == NULL ||
        line_end == NULL ||
        offset > source_length) {
        return false;
    }

    begin = offset;

    while (begin != 0u) {
        unsigned char previous;

        previous =
            (unsigned char)source[begin - 1u];

        if (previous == VITTE_PRINTER_ASCII_LF ||
            previous == VITTE_PRINTER_ASCII_CR) {
            break;
        }

        --begin;
    }

    end = offset;

    while (end < source_length) {
        unsigned char current;

        current =
            (unsigned char)source[end];

        if (current == VITTE_PRINTER_ASCII_LF ||
            current == VITTE_PRINTER_ASCII_CR) {
            break;
        }

        ++end;
    }

    *line_begin = begin;
    *line_end = end;

    return true;
}

static size_t
vitte_printer_display_width(
    const char *data,
    size_t length,
    size_t tab_width)
{
    size_t index;
    size_t column;

    column = 0u;

    for (index = 0u; index < length; ++index) {
        unsigned char byte;

        byte = (unsigned char)data[index];

        if (byte == VITTE_PRINTER_ASCII_TAB) {
            size_t remainder;

            remainder = column % tab_width;
            column += tab_width - remainder;
        } else {
            /*
             * Source diagnostics use byte-oriented columns here.
             * Unicode display-cell calculation belongs to a dedicated
             * Unicode width subsystem and is deliberately not guessed.
             */
            ++column;
        }
    }

    return column;
}

static bool
vitte_printer_write_source_expanded(
    vitte_printer_t *printer,
    const char *data,
    size_t length,
    size_t tab_width)
{
    size_t index;
    size_t column;

    column = 0u;

    for (index = 0u; index < length; ++index) {
        unsigned char byte;

        byte = (unsigned char)data[index];

        if (byte == VITTE_PRINTER_ASCII_TAB) {
            size_t spaces;
            size_t remainder;
            size_t space_index;

            remainder = column % tab_width;
            spaces = tab_width - remainder;

            for (space_index = 0u;
                 space_index < spaces;
                 ++space_index) {
                if (!vitte_printer_write_char(
                        printer,
                        ' ')) {
                    return false;
                }
            }

            column += spaces;
        } else {
            if (!vitte_printer_write_raw(
                    printer,
                    &data[index],
                    1u)) {
                return false;
            }

            ++column;
        }
    }

    return true;
}

/* ========================================================================= */
/* Source snippet                                                            */
/* ========================================================================= */

bool
vitte_printer_print_source_span(
    vitte_printer_t *printer,
    const char *source,
    size_t source_length,
    vitte_parser_span_t span,
    const char *label)
{
    size_t line_begin;
    size_t line_end;
    size_t underline_begin;
    size_t underline_end;
    size_t prefix_width;
    size_t underline_width;
    size_t index;

    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (source == NULL ||
        !span.valid ||
        span.begin > span.end ||
        span.end > source_length) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_SOURCE);
    }

    if (!vitte_printer_source_line_bounds(
            source,
            source_length,
            span.begin,
            &line_begin,
            &line_end)) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_SOURCE);
    }

    underline_begin = span.begin;

    underline_end = span.end;

    if (underline_end > line_end) {
        underline_end = line_end;
    }

    if (underline_end <= underline_begin) {
        if (underline_begin < line_end) {
            underline_end =
                underline_begin + 1u;
        } else {
            underline_end =
                underline_begin;
        }
    }

    if (printer->options.show_locations) {
        if (!vitte_printer_printf_private(
                printer,
                "%zu:%zu | ",
                span.line,
                span.column)) {
            return false;
        }
    }

    if (!vitte_printer_write_source_expanded(
            printer,
            source + line_begin,
            line_end - line_begin,
            printer->options.tab_width) ||
        !vitte_printer_newline(printer)) {
        return false;
    }

    if (printer->options.show_locations) {
        char prefix[96];
        int prefix_length;

        prefix_length =
            snprintf(
                prefix,
                sizeof(prefix),
                "%zu:%zu | ",
                span.line,
                span.column);

        if (prefix_length < 0) {
            return vitte_printer_fail(
                printer,
                VITTE_PRINTER_ERROR_FORMAT);
        }

        for (index = 0u;
             index < (size_t)prefix_length;
             ++index) {
            if (!vitte_printer_write_char(
                    printer,
                    ' ')) {
                return false;
            }
        }
    }

    prefix_width =
        vitte_printer_display_width(
            source + line_begin,
            underline_begin - line_begin,
            printer->options.tab_width);

    underline_width =
        vitte_printer_display_width(
            source + underline_begin,
            underline_end - underline_begin,
            printer->options.tab_width);

    for (index = 0u;
         index < prefix_width;
         ++index) {
        if (!vitte_printer_write_char(
                printer,
                ' ')) {
            return false;
        }
    }

    if (underline_width == 0u) {
        underline_width = 1u;
    }

    if (!vitte_printer_write_char(
            printer,
            '^')) {
        return false;
    }

    for (index = 1u;
         index < underline_width;
         ++index) {
        if (!vitte_printer_write_char(
                printer,
                '~')) {
            return false;
        }
    }

    if (label != NULL &&
        label[0] != '\0') {
        if (!vitte_printer_write_cstr(
                printer,
                " ") ||
            !vitte_printer_write_cstr(
                printer,
                label)) {
            return false;
        }
    }

    if (!vitte_printer_newline(printer)) {
        return false;
    }

    printer->stats.source_spans_printed =
        vitte_printer_u64_add_sat(
            printer->stats.source_spans_printed,
            UINT64_C(1));

    return true;
}

/* ========================================================================= */
/* Diagnostics                                                               */
/* ========================================================================= */

static const char *
vitte_printer_diagnostic_kind_name(
    vitte_parser_diagnostic_kind_t kind)
{
    switch (kind) {
        case VITTE_PARSER_DIAGNOSTIC_INVALID:
            return "invalid";

        case VITTE_PARSER_DIAGNOSTIC_NOTE:
            return "note";

        case VITTE_PARSER_DIAGNOSTIC_HELP:
            return "help";

        case VITTE_PARSER_DIAGNOSTIC_WARNING:
            return "warning";

        case VITTE_PARSER_DIAGNOSTIC_ERROR:
            return "error";

        case VITTE_PARSER_DIAGNOSTIC_COUNT:
            return "count";
    }

    return "unknown";
}

bool
vitte_printer_print_diagnostic(
    vitte_printer_t *printer,
    const vitte_parser_diagnostic_t *diagnostic,
    const char *source,
    size_t source_length)
{
    const char *kind_name;
    const char *error_name;

    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (diagnostic == NULL) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    kind_name =
        vitte_printer_diagnostic_kind_name(
            diagnostic->kind);

    error_name =
        vitte_parser_error_name(
            diagnostic->error);

    if (!vitte_printer_indent(printer)) {
        return false;
    }

    if (!vitte_printer_printf_private(
            printer,
            "%s[%s]",
            kind_name,
            error_name)) {
        return false;
    }

    if (diagnostic->span.valid &&
        printer->options.show_locations) {
        if (!vitte_printer_printf_private(
                printer,
                " at %u:%zu:%zu",
                diagnostic->span.file_id,
                diagnostic->span.line,
                diagnostic->span.column)) {
            return false;
        }
    }

    if (diagnostic->expected !=
        VITTE_TOKEN_INVALID) {
        if (!vitte_printer_printf_private(
                printer,
                ": expected %s",
                vitte_printer_token_kind_name(
                    diagnostic->expected))) {
            return false;
        }

        if (diagnostic->found !=
            VITTE_TOKEN_INVALID) {
            if (!vitte_printer_printf_private(
                    printer,
                    ", found %s",
                    vitte_printer_token_kind_name(
                        diagnostic->found))) {
                return false;
            }
        }
    } else if (diagnostic->found !=
               VITTE_TOKEN_INVALID) {
        if (!vitte_printer_printf_private(
                printer,
                ": found %s",
                vitte_printer_token_kind_name(
                    diagnostic->found))) {
            return false;
        }
    }

    if (!vitte_printer_newline(printer)) {
        return false;
    }

    if (source != NULL &&
        diagnostic->span.valid &&
        diagnostic->span.end <=
            source_length) {
        if (!vitte_printer_print_source_span(
                printer,
                source,
                source_length,
                diagnostic->span,
                error_name)) {
            return false;
        }
    }

    printer->stats.diagnostics_printed =
        vitte_printer_u64_add_sat(
            printer->stats.diagnostics_printed,
            UINT64_C(1));

    return true;
}

bool
vitte_printer_print_diagnostics(
    vitte_printer_t *printer,
    const vitte_parser_t *parser,
    const char *source,
    size_t source_length)
{
    size_t index;

    if (!vitte_printer_context_valid(printer) ||
        parser == NULL ||
        !vitte_parser_is_valid(parser)) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    if (source == NULL &&
        source_length != 0u) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_SOURCE);
    }

    printer->state =
        VITTE_PRINTER_STATE_PRINTING;

    for (index = 0u;
         index <
            vitte_parser_diagnostic_count(parser);
         ++index) {
        const vitte_parser_diagnostic_t *diagnostic;

        diagnostic =
            vitte_parser_diagnostic_at(
                parser,
                index);

        if (diagnostic == NULL) {
            return vitte_printer_fail(
                printer,
                VITTE_PRINTER_ERROR_INTERNAL);
        }

        if (!vitte_printer_print_diagnostic(
                printer,
                diagnostic,
                source,
                source_length)) {
            return false;
        }
    }

    printer->state =
        VITTE_PRINTER_STATE_READY;

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    return true;
}

/* ========================================================================= */
/* Summary                                                                   */
/* ========================================================================= */

bool
vitte_printer_print_parser_summary(
    vitte_printer_t *printer,
    const vitte_parser_t *parser)
{
    vitte_parser_stats_t stats;

    if (!vitte_printer_context_valid(printer) ||
        parser == NULL ||
        !vitte_parser_is_valid(parser)) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    stats = vitte_parser_stats(parser);

    if (!vitte_printer_line(
            printer,
            "parser {")) {
        return false;
    }

    if (!vitte_printer_push_depth(printer)) {
        return false;
    }

#define VITTE_PRINTER_SUMMARY_SIZE(label, value)                     \
    do {                                                             \
        if (!vitte_printer_indent(printer) ||                        \
            !vitte_printer_printf_private(                           \
                printer,                                             \
                label ": %zu",                                       \
                (size_t)(value)) ||                                  \
            !vitte_printer_newline(printer)) {                       \
            vitte_printer_pop_depth(printer);                        \
            return false;                                            \
        }                                                            \
    } while (0)

#define VITTE_PRINTER_SUMMARY_U64(label, value)                      \
    do {                                                             \
        if (!vitte_printer_indent(printer) ||                        \
            !vitte_printer_printf_private(                           \
                printer,                                             \
                label ": %" PRIu64,                                  \
                (uint64_t)(value)) ||                                \
            !vitte_printer_newline(printer)) {                       \
            vitte_printer_pop_depth(printer);                        \
            return false;                                            \
        }                                                            \
    } while (0)

    VITTE_PRINTER_SUMMARY_SIZE(
        "tokens",
        parser->token_count);

    VITTE_PRINTER_SUMMARY_SIZE(
        "nodes",
        parser->node_count);

    VITTE_PRINTER_SUMMARY_SIZE(
        "diagnostics",
        parser->diagnostic_count);

    VITTE_PRINTER_SUMMARY_U64(
        "runs",
        stats.runs);

    VITTE_PRINTER_SUMMARY_U64(
        "failed_runs",
        stats.failed_runs);

    VITTE_PRINTER_SUMMARY_U64(
        "tokens_consumed",
        stats.tokens_consumed);

    VITTE_PRINTER_SUMMARY_U64(
        "nodes_created",
        stats.nodes_created);

    VITTE_PRINTER_SUMMARY_U64(
        "declarations",
        stats.declarations);

    VITTE_PRINTER_SUMMARY_U64(
        "errors",
        stats.errors);

    VITTE_PRINTER_SUMMARY_U64(
        "recoveries",
        stats.recoveries);

    VITTE_PRINTER_SUMMARY_U64(
        "max_recursion_depth",
        stats.max_recursion_depth);

#undef VITTE_PRINTER_SUMMARY_SIZE
#undef VITTE_PRINTER_SUMMARY_U64

    vitte_printer_pop_depth(printer);

    return vitte_printer_line(
        printer,
        "}");
}

/* ========================================================================= */
/* Validation                                                                */
/* ========================================================================= */

bool
vitte_printer_validate(
    vitte_printer_t *printer)
{
    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    printer->stats.validation_runs =
        vitte_printer_u64_add_sat(
            printer->stats.validation_runs,
            UINT64_C(1));

    if (printer->mode >=
            VITTE_PRINTER_MODE_COUNT ||
        printer->options.tab_width == 0u ||
        printer->options.tab_width >
            VITTE_PRINTER_MAX_TAB_WIDTH ||
        printer->indent_width == 0u ||
        printer->indent_width >
            VITTE_PRINTER_MAX_INDENT_WIDTH ||
        printer->max_depth == 0u ||
        printer->max_output_bytes == 0u) {
        printer->stats.validation_failures =
            vitte_printer_u64_add_sat(
                printer->stats.validation_failures,
                UINT64_C(1));

        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_VALIDATION);
    }

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    return true;
}

/* ========================================================================= */
/* Flush                                                                     */
/* ========================================================================= */

bool
vitte_printer_flush(
    vitte_printer_t *printer)
{
    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    switch (printer->sink_kind) {
        case VITTE_PRINTER_SINK_FILE:
            if (fflush(printer->sink.file) != 0) {
                return vitte_printer_fail(
                    printer,
                    VITTE_PRINTER_ERROR_IO);
            }
            break;

        case VITTE_PRINTER_SINK_BUFFER:
            /*
             * Memory sink is already committed.
             */
            break;

        case VITTE_PRINTER_SINK_CALLBACK:
            if (printer->sink.callback.flush != NULL &&
                !printer->sink.callback.flush(
                    printer->sink.callback.userdata)) {
                return vitte_printer_fail(
                    printer,
                    VITTE_PRINTER_ERROR_CALLBACK);
            }
            break;

        case VITTE_PRINTER_SINK_INVALID:
        case VITTE_PRINTER_SINK_COUNT:
            return vitte_printer_fail(
                printer,
                VITTE_PRINTER_ERROR_INVALID_SINK);
    }

    printer->stats.flushes =
        vitte_printer_u64_add_sat(
            printer->stats.flushes,
            UINT64_C(1));

    return true;
}

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

vitte_printer_options_t
vitte_printer_default_options(void)
{
    vitte_printer_options_t options;

    memset(&options, 0, sizeof(options));

    options.show_node_ids = true;
    options.show_locations = true;
    options.show_spans = true;
    options.show_token_metadata = false;
    options.show_child_counts = false;
    options.escape_non_printable = true;

    options.tab_width =
        VITTE_PRINTER_DEFAULT_TAB_WIDTH;

    return options;
}

bool
vitte_printer_set_options(
    vitte_printer_t *printer,
    const vitte_printer_options_t *options)
{
    if (!vitte_printer_context_valid(printer) ||
        options == NULL) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    if (printer->state ==
        VITTE_PRINTER_STATE_PRINTING) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_STATE);
    }

    if (options->tab_width == 0u ||
        options->tab_width >
            VITTE_PRINTER_MAX_TAB_WIDTH) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    printer->options = *options;

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    return true;
}

bool
vitte_printer_set_mode(
    vitte_printer_t *printer,
    vitte_printer_mode_t mode)
{
    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (mode >= VITTE_PRINTER_MODE_COUNT) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    if (printer->state ==
        VITTE_PRINTER_STATE_PRINTING) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_STATE);
    }

    printer->mode = mode;

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    return true;
}

bool
vitte_printer_set_limits(
    vitte_printer_t *printer,
    size_t max_output_bytes,
    size_t max_depth)
{
    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (max_output_bytes == 0u ||
        max_depth == 0u ||
        max_output_bytes <
            printer->bytes_written ||
        max_depth < printer->depth) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    printer->max_output_bytes =
        max_output_bytes;

    printer->max_depth =
        max_depth;

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    return true;
}

bool
vitte_printer_set_indent_width(
    vitte_printer_t *printer,
    size_t indent_width)
{
    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (indent_width == 0u ||
        indent_width >
            VITTE_PRINTER_MAX_INDENT_WIDTH) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_ARGUMENT);
    }

    printer->indent_width =
        indent_width;

    return true;
}

/* ========================================================================= */
/* Initialization                                                            */
/* ========================================================================= */

static void
vitte_printer_initialize_common(
    vitte_printer_t *printer)
{
    printer->magic =
        VITTE_PRINTER_MAGIC;

    printer->state =
        VITTE_PRINTER_STATE_READY;

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    printer->mode =
        VITTE_PRINTER_MODE_PRETTY;

    printer->options =
        vitte_printer_default_options();

    printer->indent_width =
        VITTE_PRINTER_DEFAULT_INDENT_WIDTH;

    printer->max_depth =
        VITTE_PRINTER_DEFAULT_MAX_DEPTH;

    printer->max_output_bytes =
        VITTE_PRINTER_DEFAULT_MAX_OUTPUT_BYTES;

    printer->depth = 0u;
    printer->bytes_written = 0u;

    printer->at_line_start = true;

    printer->generation = UINT64_C(1);
}

bool
vitte_printer_init_file(
    vitte_printer_t *printer,
    FILE *file)
{
    if (printer == NULL ||
        file == NULL) {
        return false;
    }

    memset(printer, 0, sizeof(*printer));

    vitte_printer_initialize_common(printer);

    printer->sink_kind =
        VITTE_PRINTER_SINK_FILE;

    printer->sink.file = file;

    return true;
}

bool
vitte_printer_init_buffer(
    vitte_printer_t *printer)
{
    if (printer == NULL) {
        return false;
    }

    memset(printer, 0, sizeof(*printer));

    vitte_printer_initialize_common(printer);

    printer->sink_kind =
        VITTE_PRINTER_SINK_BUFFER;

    printer->sink.buffer.data = NULL;
    printer->sink.buffer.length = 0u;
    printer->sink.buffer.capacity = 0u;

    if (!vitte_printer_buffer_reserve(
            printer,
            VITTE_PRINTER_DEFAULT_BUFFER_CAPACITY)) {
        return false;
    }

    printer->sink.buffer.data[0] = '\0';

    return true;
}

bool
vitte_printer_init_callback(
    vitte_printer_t *printer,
    vitte_printer_write_fn write_callback,
    vitte_printer_flush_fn flush_callback,
    void *userdata)
{
    if (printer == NULL ||
        write_callback == NULL) {
        return false;
    }

    memset(printer, 0, sizeof(*printer));

    vitte_printer_initialize_common(printer);

    printer->sink_kind =
        VITTE_PRINTER_SINK_CALLBACK;

    printer->sink.callback.write =
        write_callback;

    printer->sink.callback.flush =
        flush_callback;

    printer->sink.callback.userdata =
        userdata;

    return true;
}

/* ========================================================================= */
/* Reset                                                                     */
/* ========================================================================= */

bool
vitte_printer_reset(
    vitte_printer_t *printer)
{
    uint64_t generation;

    if (!vitte_printer_context_valid(printer)) {
        return false;
    }

    if (printer->state ==
        VITTE_PRINTER_STATE_PRINTING) {
        return vitte_printer_fail(
            printer,
            VITTE_PRINTER_ERROR_INVALID_STATE);
    }

    generation = printer->generation;

    printer->state =
        VITTE_PRINTER_STATE_READY;

    printer->last_error =
        VITTE_PRINTER_ERROR_NONE;

    printer->depth = 0u;
    printer->bytes_written = 0u;
    printer->at_line_start = true;

    memset(
        &printer->stats,
        0,
        sizeof(printer->stats));

    if (printer->sink_kind ==
        VITTE_PRINTER_SINK_BUFFER) {
        printer->sink.buffer.length = 0u;

        if (printer->sink.buffer.data != NULL &&
            printer->sink.buffer.capacity != 0u) {
            printer->sink.buffer.data[0] = '\0';
        }
    }

    printer->generation =
        generation == UINT64_MAX
            ? UINT64_MAX
            : generation + UINT64_C(1);

    return true;
}

/* ========================================================================= */
/* Buffer access                                                             */
/* ========================================================================= */

const char *
vitte_printer_buffer_data(
    const vitte_printer_t *printer)
{
    if (!vitte_printer_context_valid(printer) ||
        printer->sink_kind !=
            VITTE_PRINTER_SINK_BUFFER) {
        return NULL;
    }

    return printer->sink.buffer.data;
}

size_t
vitte_printer_buffer_length(
    const vitte_printer_t *printer)
{
    if (!vitte_printer_context_valid(printer) ||
        printer->sink_kind !=
            VITTE_PRINTER_SINK_BUFFER) {
        return 0u;
    }

    return printer->sink.buffer.length;
}

char *
vitte_printer_take_buffer(
    vitte_printer_t *printer,
    size_t *length)
{
    char *data;

    if (!vitte_printer_context_valid(printer) ||
        printer->sink_kind !=
            VITTE_PRINTER_SINK_BUFFER) {
        return NULL;
    }

    data = printer->sink.buffer.data;

    if (length != NULL) {
        *length =
            printer->sink.buffer.length;
    }

    printer->sink.buffer.data = NULL;
    printer->sink.buffer.length = 0u;
    printer->sink.buffer.capacity = 0u;

    return data;
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_printer_stats_t
vitte_printer_stats(
    const vitte_printer_t *printer)
{
    vitte_printer_stats_t stats;

    memset(&stats, 0, sizeof(stats));

    if (!vitte_printer_context_valid(printer)) {
        return stats;
    }

    return printer->stats;
}

/* ========================================================================= */
/* Destroy                                                                   */
/* ========================================================================= */

void
vitte_printer_destroy(
    vitte_printer_t *printer)
{
    if (printer == NULL) {
        return;
    }

    if (printer->magic ==
            VITTE_PRINTER_MAGIC &&
        printer->sink_kind ==
            VITTE_PRINTER_SINK_BUFFER) {
        free(printer->sink.buffer.data);
    }

    memset(printer, 0, sizeof(*printer));

    printer->magic =
        VITTE_PRINTER_DEAD_MAGIC;

    printer->state =
        VITTE_PRINTER_STATE_DESTROYED;
}

/* ========================================================================= */
/* Queries                                                                   */
/* ========================================================================= */

bool
vitte_printer_is_valid(
    const vitte_printer_t *printer)
{
    return vitte_printer_context_valid(printer);
}

vitte_printer_error_t
vitte_printer_last_error(
    const vitte_printer_t *printer)
{
    if (printer == NULL ||
        printer->magic !=
            VITTE_PRINTER_MAGIC) {
        return VITTE_PRINTER_ERROR_INVALID_CONTEXT;
    }

    return printer->last_error;
}

uint64_t
vitte_printer_generation(
    const vitte_printer_t *printer)
{
    if (!vitte_printer_context_valid(printer)) {
        return UINT64_C(0);
    }

    return printer->generation;
}

/* ========================================================================= */
/* Translation-unit anchor                                                   */
/* ========================================================================= */

void
vitte_printer_translation_unit_anchor(void)
{
    /*
     * Stable symbol for static-library linkage and build-system probes.
     */
}
