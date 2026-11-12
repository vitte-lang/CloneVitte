#include "render_terminal.h"
#include "cli_bridge.h"

#include "diagnostic.h"
#include "help.h"
#include "../api/version.h"
#include "../unicode/unicode.h"
#include "label.h"
#include "registry.h"
#include "render_json.h"
#include "render_lsp.h"
#include "render_sarif.h"
#include "suggestion.h"
#include "../source/source.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Constants                                                                 */
/* ========================================================================= */

#define VITTE_TERMINAL_DEFAULT_TAB_WIDTH        4u
#define VITTE_TERMINAL_DEFAULT_CONTEXT_LINES    1u
#define VITTE_TERMINAL_MAX_RENDER_WIDTH         160u
#define VITTE_TERMINAL_MAX_SOURCE_LINE          16384u

#define VITTE_TERM_RESET            "\x1b[0m"
#define VITTE_TERM_BOLD             "\x1b[1m"
#define VITTE_TERM_DIM              "\x1b[2m"

#define VITTE_TERM_RED              "\x1b[31m"
#define VITTE_TERM_GREEN            "\x1b[32m"
#define VITTE_TERM_YELLOW           "\x1b[33m"
#define VITTE_TERM_BLUE             "\x1b[34m"
#define VITTE_TERM_MAGENTA          "\x1b[35m"
#define VITTE_TERM_CYAN             "\x1b[36m"

#define VITTE_TERM_BRIGHT_RED       "\x1b[91m"
#define VITTE_TERM_BRIGHT_GREEN     "\x1b[92m"
#define VITTE_TERM_BRIGHT_YELLOW    "\x1b[93m"
#define VITTE_TERM_BRIGHT_BLUE      "\x1b[94m"
#define VITTE_TERM_BRIGHT_MAGENTA   "\x1b[95m"
#define VITTE_TERM_BRIGHT_CYAN      "\x1b[96m"

static bool
vitte_diagnostic_default_lsp_position(
    vitte_source_id_t source_id,
    const char *source_name,
    size_t offset,
    vitte_diagnostic_lsp_position_encoding_t encoding,
    vitte_diagnostic_lsp_position_t *position,
    void *user_data)
{
    return vitte_diagnostic_lsp_source_position_converter(
        source_id, source_name, offset, encoding, position, user_data);
}

typedef struct vitte_cli_lsp_fallback_context {
    const vitte_cli_diagnostic_t *diagnostics;
    size_t diagnostic_count;
    vitte_source_id_t source_id;
    const char *source_name;
} vitte_cli_lsp_fallback_context_t;

/*
 * Process and import diagnostics can carry a complete source span without
 * having a source manager attached.  Preserve those coordinates for LSP
 * instead of dropping the diagnostic when no byte-to-line converter exists.
 * The bridge stores zero-based line and column values, so only a same-line
 * byte delta needs to be reconstructed for an interior offset.
 */
static bool
vitte_diagnostic_cli_lsp_fallback_position(
    vitte_source_id_t source_id,
    const char *source_name,
    size_t offset,
    vitte_diagnostic_lsp_position_encoding_t encoding,
    vitte_diagnostic_lsp_position_t *position,
    void *user_data)
{
    const vitte_cli_lsp_fallback_context_t *context =
        (const vitte_cli_lsp_fallback_context_t *)user_data;
    const vitte_source_id_t effective_source_id =
        source_id != VITTE_SOURCE_INVALID_ID
            ? source_id
            : context != NULL ? context->source_id : source_id;
    const char *effective_source_name =
        source_name != NULL && source_name[0] != '\0'
            ? source_name
            : context != NULL ? context->source_name : source_name;
    size_t index;

    (void)encoding;
    if (context == NULL || position == NULL ||
        context->diagnostics == NULL) {
        return false;
    }

    for (index = 0u; index < context->diagnostic_count; ++index) {
        const vitte_cli_diagnostic_t *diagnostic =
            &context->diagnostics[index];
        bool source_matches;

        if (!diagnostic->has_span ||
            offset < diagnostic->start_offset ||
            offset > diagnostic->end_offset) {
            continue;
        }

        source_matches = true;
        if (effective_source_id != VITTE_SOURCE_INVALID_ID &&
            diagnostic->source_id != VITTE_SOURCE_INVALID_ID) {
            source_matches = effective_source_id == diagnostic->source_id;
        }
        if (source_matches && effective_source_name != NULL &&
            effective_source_name[0] != '\0' &&
            diagnostic->source_name != NULL &&
            strcmp(effective_source_name, diagnostic->source_name) != 0) {
            source_matches = false;
        }
        if (!source_matches) {
            continue;
        }

        if (offset == diagnostic->end_offset) {
            position->line = diagnostic->end_line;
            position->character = diagnostic->end_column;
        } else {
            size_t delta = offset - diagnostic->start_offset;
            if (diagnostic->start_column > SIZE_MAX - delta) {
                return false;
            }
            position->line = diagnostic->start_line;
            position->character = diagnostic->start_column + delta;
        }
        position->valid = true;
        return true;
    }

    return false;
}

/* ========================================================================= */
/* Internal types                                                            */
/* ========================================================================= */

typedef struct vitte_terminal_source_line {
    const char *text;
    size_t length;
    size_t start_offset;
    size_t line_index;
    bool valid;
} vitte_terminal_source_line_t;

typedef struct vitte_terminal_render_span {
    vitte_source_id_t source_id;
    const char *source_name;

    size_t start_offset;
    size_t end_offset;

    size_t start_line;
    size_t start_column;
    size_t end_line;
    size_t end_column;

    bool valid;
} vitte_terminal_render_span_t;

/* ========================================================================= */
/* Basic helpers                                                             */
/* ========================================================================= */

static bool
vitte_terminal_text_present(
    const char *text
)
{
    return text != NULL &&
           text[0] != '\0';
}

static const char *
vitte_terminal_safe(
    const char *text
)
{
    return text != NULL
        ? text
        : "";
}

static size_t
vitte_terminal_digits(
    size_t value
)
{
    size_t digits;

    digits = 1u;

    while (value >= 10u) {
        value /= 10u;
        digits++;
    }

    return digits;
}

static size_t
vitte_terminal_min_size(
    size_t left,
    size_t right
)
{
    return left < right
        ? left
        : right;
}

/* ========================================================================= */
/* Terminal styles                                                           */
/* ========================================================================= */

static void
vitte_terminal_style(
    FILE *stream,
    const vitte_diagnostic_terminal_options_t *options,
    const char *style
)
{
    if (stream == NULL ||
        options == NULL ||
        !options->color ||
        style == NULL) {
        return;
    }

    (void)fputs(
        style,
        stream
    );
}

static void
vitte_terminal_reset(
    FILE *stream,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (stream == NULL ||
        options == NULL ||
        !options->color) {
        return;
    }

    (void)fputs(
        VITTE_TERM_RESET,
        stream
    );
}

static const char *
vitte_terminal_severity_name(
    vitte_diagnostic_severity_t severity
)
{
    switch (severity) {
        case VITTE_DIAGNOSTIC_FATAL:
            return "fatal";

        case VITTE_DIAGNOSTIC_ERROR:
            return "error";

        case VITTE_DIAGNOSTIC_WARNING:
            return "warning";

        case VITTE_DIAGNOSTIC_NOTE:
            return "note";

        case VITTE_DIAGNOSTIC_HELP:
            return "help";

        default:
            return "diagnostic";
    }
}

static const char *
vitte_terminal_severity_color(
    vitte_diagnostic_severity_t severity
)
{
    switch (severity) {
        case VITTE_DIAGNOSTIC_FATAL:
            return VITTE_TERM_BRIGHT_RED;

        case VITTE_DIAGNOSTIC_ERROR:
            return VITTE_TERM_RED;

        case VITTE_DIAGNOSTIC_WARNING:
            return VITTE_TERM_YELLOW;

        case VITTE_DIAGNOSTIC_NOTE:
            return VITTE_TERM_CYAN;

        case VITTE_DIAGNOSTIC_HELP:
            return VITTE_TERM_GREEN;

        default:
            return VITTE_TERM_RESET;
    }
}

/* ========================================================================= */
/* Public defaults                                                           */
/* ========================================================================= */

void
vitte_diagnostic_terminal_options_init(
    vitte_diagnostic_terminal_options_t *options
)
{
    if (options == NULL) {
        return;
    }

    memset(
        options,
        0,
        sizeof(*options)
    );

    options->color = true;

    options->show_code = true;
    options->show_phase = true;
    options->show_category = true;

    options->show_source = true;
    options->show_labels = true;
    options->show_related = true;

    options->show_causes = true;
    options->show_notes = true;
    options->show_help = true;
    options->show_suggestions = true;

    options->show_contract = true;
    options->show_context = true;

    options->show_cascade = false;
    options->show_fingerprint = false;

    options->include_suppressed = false;

    options->compact = false;
    options->trailing_newline = true;

    options->tab_width =
        VITTE_TERMINAL_DEFAULT_TAB_WIDTH;

    options->context_lines =
        VITTE_TERMINAL_DEFAULT_CONTEXT_LINES;

    options->sources = NULL;
}

/* ========================================================================= */
/* Source manager abstraction                                                */
/* ========================================================================= */

/*
 * The source context owns indexed lines and returns borrowed slices. Keep
 * lookup here so the renderer never duplicates source storage logic.
 */
static bool
vitte_terminal_source_line_lookup(
    const vitte_diagnostic_terminal_options_t *options,
    vitte_source_id_t source_id,
    size_t line_index,
    vitte_terminal_source_line_t *line
)
{
    vitte_source_slice_t slice;

    if (line == NULL) {
        return false;
    }

    memset(
        line,
        0,
        sizeof(*line)
    );

    line->line_index =
        line_index;

    if (options == NULL ||
        options->sources == NULL ||
        line_index == SIZE_MAX) {

        return false;
    }

    if (!vitte_source_get_line(
            options->sources,
            source_id,
            line_index + 1u,
            &slice
        )) {
        return false;
    }

    line->text = slice.data;
    line->length = slice.length;
    line->start_offset = slice.begin;
    line->valid = slice.valid;
    return line->valid;
}

/* ========================================================================= */
/* Span conversion                                                           */
/* ========================================================================= */

static bool
vitte_terminal_render_span_from_ast(
    const vitte_ast_span_t *span,
    vitte_terminal_render_span_t *output
)
{
    if (output == NULL) {
        return false;
    }

    memset(
        output,
        0,
        sizeof(*output)
    );

    if (span == NULL ||
        !vitte_ast_span_is_valid(*span)) {

        return false;
    }

    output->source_id =
        span->source_id;

    output->source_name =
        span->source_name;

    output->start_offset =
        span->start_offset;

    output->end_offset =
        span->end_offset;

    output->start_line =
        span->start_line;

    output->start_column =
        span->start_column;

    output->end_line =
        span->end_line;

    output->end_column =
        span->end_column;

    output->valid = true;

    return true;
}

static bool
vitte_terminal_render_span_from_diagnostic(
    const vitte_diagnostic_t *diagnostic,
    vitte_terminal_render_span_t *output
)
{
    if (output == NULL) {
        return false;
    }

    memset(
        output,
        0,
        sizeof(*output)
    );

    if (diagnostic == NULL ||
        !diagnostic->has_span) {

        return false;
    }

    output->source_id =
        diagnostic->source_id;

    output->source_name =
        diagnostic->source_name;

    output->start_offset =
        diagnostic->start_offset;

    output->end_offset =
        diagnostic->end_offset;

    output->start_line =
        diagnostic->start_line;

    output->start_column =
        diagnostic->start_column;

    output->end_line =
        diagnostic->end_line;

    output->end_column =
        diagnostic->end_column;

    output->valid = true;

    return true;
}

/* ========================================================================= */
/* Header                                                                    */
/* ========================================================================= */

static void
vitte_terminal_render_header(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    const char *severity_color;

    severity_color =
        vitte_terminal_severity_color(
            diagnostic->severity
        );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_BOLD
    );

    vitte_terminal_style(
        stream,
        options,
        severity_color
    );

    (void)fputs(
        vitte_terminal_severity_name(
            diagnostic->severity
        ),
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    if (options->show_code &&
        vitte_terminal_text_present(
            diagnostic->short_code
        )) {

        (void)fputc('[', stream);

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_BOLD
        );

        (void)fputs(
            diagnostic->short_code,
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );

        (void)fputc(']', stream);
    }

    (void)fputs(": ", stream);

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_BOLD
    );

    (void)fputs(
        vitte_terminal_safe(
            diagnostic->message
        ),
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputc('\n', stream);

    if (options->show_phase ||
        options->show_category) {

        bool emitted;

        emitted = false;

        (void)fputs(
            "  ",
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_DIM
        );

        if (options->show_phase &&
            diagnostic->origin !=
                VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN) {

            (void)fprintf(
                stream,
                "phase: %s",
                vitte_diagnostic_origin_name(
                    diagnostic->origin
                )
            );

            emitted = true;
        }

        if (options->show_category &&
            vitte_terminal_text_present(
                diagnostic->category
            )) {

            if (emitted) {
                (void)fputs(
                    " | ",
                    stream
                );
            }

            (void)fprintf(
                stream,
                "category: %s",
                diagnostic->category
            );

            emitted = true;
        }

        vitte_terminal_reset(
            stream,
            options
        );

        if (emitted) {
            (void)fputc(
                '\n',
                stream
            );
        }
    }
}

/* ========================================================================= */
/* Main location                                                             */
/* ========================================================================= */

static void
vitte_terminal_render_main_location(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (!diagnostic->has_span &&
        !vitte_terminal_text_present(
            diagnostic->source_name
        )) {

        return;
    }

    (void)fputs(
        "  ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_BLUE
    );

    (void)fputs(
        "--> ",
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputs(
        vitte_terminal_text_present(
            diagnostic->source_name
        )
            ? diagnostic->source_name
            : "<unknown>",
        stream
    );

    if (diagnostic->has_span) {
        (void)fprintf(
            stream,
            ":%zu:%zu",
            diagnostic->start_line + 1u,
            diagnostic->start_column + 1u
        );
    }

    (void)fputc(
        '\n',
        stream
    );
}

/* ========================================================================= */
/* Source gutters                                                            */
/* ========================================================================= */

static void
vitte_terminal_render_empty_gutter(
    FILE *stream,
    size_t width,
    const vitte_diagnostic_terminal_options_t *options
)
{
    (void)fprintf(
        stream,
        "%*s ",
        (int)width,
        ""
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_BLUE
    );

    (void)fputs(
        "|",
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputc(
        '\n',
        stream
    );
}

static void
vitte_terminal_render_source_gutter(
    FILE *stream,
    size_t width,
    size_t line_number,
    const vitte_diagnostic_terminal_options_t *options
)
{
    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_BLUE
    );

    (void)fprintf(
        stream,
        "%*zu |",
        (int)width,
        line_number
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputc(
        ' ',
        stream
    );
}

/* ========================================================================= */
/* Tab-aware source output                                                   */
/* ========================================================================= */

static size_t
vitte_terminal_tab_advance(
    size_t column,
    size_t tab_width
)
{
    size_t width;

    width =
        tab_width != 0u
            ? tab_width
            : VITTE_TERMINAL_DEFAULT_TAB_WIDTH;

    return width -
        (column % width);
}

static size_t
vitte_terminal_visual_column(
    const char *text,
    size_t length,
    size_t byte_column,
    size_t tab_width
)
{
    size_t visual;

    if (text == NULL) {
        return byte_column;
    }

    visual = 0u;
    (void)vitte_unicode_utf8_display_width(text,
        byte_column < length ? byte_column : length,
        tab_width, 0u, &visual, NULL);

    return visual;
}

static void
vitte_terminal_write_source_text(
    FILE *stream,
    const char *text,
    size_t length,
    size_t tab_width
)
{
    size_t index;
    size_t visual;

    if (text == NULL) {
        return;
    }

    index = 0u;
    visual = 0u;

    while (index < length) {
        unsigned char character;

        character =
            (unsigned char)text[index];

        if (character == '\n' ||
            character == '\r') {
            break;
        }

        if (character == '\t') {
            size_t spaces;
            size_t space_index;

            spaces =
                vitte_terminal_tab_advance(
                    visual,
                    tab_width
                );

            for (space_index = 0u;
                 space_index < spaces;
                 space_index++) {

                (void)fputc(
                    ' ',
                    stream
                );
            }

            visual += spaces;
            index++;
            continue;
        }

        {
            vitte_unicode_result_t decoded = vitte_unicode_decode_utf8(
                (const unsigned char *)text + index, length - index);
            int width;
            if (decoded.error != VITTE_UNICODE_ERROR_NONE) {
                (void)fputc('?', stream);
                visual++;
                index++;
                continue;
            }
            width = vitte_unicode_display_width(decoded.codepoint);
            if (width >= 0) {
                (void)fwrite(text + index, 1u, decoded.length, stream);
                visual += (size_t)width;
            }
            index += decoded.length;
        }
    }
}

/* ========================================================================= */
/* Marker line                                                               */
/* ========================================================================= */

static void
vitte_terminal_render_marker_line(
    FILE *stream,
    size_t gutter_width,
    size_t start_column,
    size_t end_column,
    const char *message,
    bool primary,
    const vitte_diagnostic_terminal_options_t *options
)
{
    size_t index;
    size_t marker_length;
    const char *color;

    color =
        primary
            ? VITTE_TERM_BRIGHT_RED
            : VITTE_TERM_BRIGHT_CYAN;

    (void)fprintf(
        stream,
        "%*s ",
        (int)gutter_width,
        ""
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_BLUE
    );

    (void)fputs(
        "|",
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputc(
        ' ',
        stream
    );

    for (index = 0u;
         index < start_column;
         index++) {

        (void)fputc(
            ' ',
            stream
        );
    }

    marker_length =
        end_column > start_column
            ? end_column - start_column
            : 1u;

    marker_length =
        vitte_terminal_min_size(
            marker_length,
            VITTE_TERMINAL_MAX_RENDER_WIDTH
        );

    vitte_terminal_style(
        stream,
        options,
        color
    );

    for (index = 0u;
         index < marker_length;
         index++) {

        if (primary &&
            index == 0u) {

            (void)fputc(
                '^',
                stream
            );
        } else {
            (void)fputc(
                '~',
                stream
            );
        }
    }

    vitte_terminal_reset(
        stream,
        options
    );

    if (vitte_terminal_text_present(
            message
        )) {

        (void)fputc(
            ' ',
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            color
        );

        (void)fputs(
            message,
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );
    }

    (void)fputc(
        '\n',
        stream
    );
}

/* ========================================================================= */
/* Fallback span                                                             */
/* ========================================================================= */

static void
vitte_terminal_render_span_fallback(
    FILE *stream,
    const vitte_terminal_render_span_t *span,
    const char *message,
    bool primary,
    const vitte_diagnostic_terminal_options_t *options
)
{
    const char *color;

    if (span == NULL ||
        !span->valid) {
        return;
    }

    color =
        primary
            ? VITTE_TERM_BRIGHT_RED
            : VITTE_TERM_BRIGHT_CYAN;

    (void)fputs(
        "      ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        color
    );

    (void)fputs(
        primary ? "^ " : "= ",
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    if (vitte_terminal_text_present(
            span->source_name
        )) {

        (void)fprintf(
            stream,
            "%s:",
            span->source_name
        );
    }

    (void)fprintf(
        stream,
        "%zu:%zu",
        span->start_line + 1u,
        span->start_column + 1u
    );

    if (span->end_line !=
            span->start_line ||
        span->end_column !=
            span->start_column) {

        (void)fprintf(
            stream,
            "-%zu:%zu",
            span->end_line + 1u,
            span->end_column + 1u
        );
    }

    if (vitte_terminal_text_present(
            message
        )) {

        (void)fprintf(
            stream,
            ": %s",
            message
        );
    }

    (void)fputc(
        '\n',
        stream
    );
}

/* ========================================================================= */
/* Single-line source span                                                   */
/* ========================================================================= */

static bool
vitte_terminal_render_single_line_span(
    FILE *stream,
    const vitte_terminal_render_span_t *span,
    const char *message,
    bool primary,
    const vitte_diagnostic_terminal_options_t *options
)
{
    vitte_terminal_source_line_t source_line;
    size_t line_number;
    size_t gutter_width;
    size_t visual_start;
    size_t visual_end;

    if (span == NULL ||
        !span->valid ||
        span->start_line !=
            span->end_line) {

        return false;
    }

    if (!vitte_terminal_source_line_lookup(
            options,
            span->source_id,
            span->start_line,
            &source_line
        )) {

        return false;
    }

    if (!source_line.valid ||
        source_line.text == NULL) {

        return false;
    }

    line_number =
        span->start_line + 1u;

    gutter_width =
        vitte_terminal_digits(
            line_number
        );

    vitte_terminal_render_empty_gutter(
        stream,
        gutter_width,
        options
    );

    vitte_terminal_render_source_gutter(
        stream,
        gutter_width,
        line_number,
        options
    );

    vitte_terminal_write_source_text(
        stream,
        source_line.text,
        source_line.length,
        options->tab_width
    );

    (void)fputc(
        '\n',
        stream
    );

    visual_start =
        vitte_terminal_visual_column(
            source_line.text,
            source_line.length,
            span->start_offset >= source_line.start_offset
                ? span->start_offset - source_line.start_offset : 0u,
            options->tab_width
        );

    visual_end =
        vitte_terminal_visual_column(
            source_line.text,
            source_line.length,
            span->end_offset >= source_line.start_offset
                ? span->end_offset - source_line.start_offset : 0u,
            options->tab_width
        );

    if (visual_end < visual_start) {
        visual_end =
            visual_start + 1u;
    }

    vitte_terminal_render_marker_line(
        stream,
        gutter_width,
        visual_start,
        visual_end,
        message,
        primary,
        options
    );

    return true;
}

/* ========================================================================= */
/* Multi-line span                                                           */
/* ========================================================================= */

static bool
vitte_terminal_render_multiline_span(
    FILE *stream,
    const vitte_terminal_render_span_t *span,
    const char *message,
    bool primary,
    const vitte_diagnostic_terminal_options_t *options
)
{
    size_t line_index;
    size_t last_line;
    size_t gutter_width;
    bool rendered = false;

    if (span == NULL || !span->valid || span->end_line <= span->start_line) {
        return false;
    }

    /* The exclusive endpoint at column zero does not include that line. */
    last_line = span->end_line - (span->end_column == 0u ? 1u : 0u);
    gutter_width = vitte_terminal_digits(last_line + 1u);
    vitte_terminal_render_empty_gutter(stream, gutter_width, options);

    for (line_index = span->start_line; line_index <= last_line; ++line_index) {
        vitte_terminal_source_line_t source_line;
        size_t start;
        size_t end;
        if (!vitte_terminal_source_line_lookup(options, span->source_id,
                line_index, &source_line) || !source_line.valid ||
            source_line.text == NULL) continue;

        start = span->start_offset > source_line.start_offset
            ? span->start_offset - source_line.start_offset : 0u;
        end = span->end_offset > source_line.start_offset
            ? span->end_offset - source_line.start_offset : 0u;
        rendered = true;
        vitte_terminal_render_source_gutter(stream, gutter_width,
            line_index + 1u, options);
        vitte_terminal_write_source_text(stream, source_line.text,
            source_line.length, options->tab_width);
        (void)fputc('\n', stream);
        vitte_terminal_render_marker_line(stream, gutter_width,
            vitte_terminal_visual_column(source_line.text, source_line.length,
                start, options->tab_width),
            vitte_terminal_visual_column(source_line.text, source_line.length,
                end, options->tab_width),
            line_index == last_line ? message : NULL, primary, options);
    }
    return rendered;
}

/* ========================================================================= */
/* Generic rendered span                                                     */
/* ========================================================================= */

static void
vitte_terminal_render_span(
    FILE *stream,
    const vitte_terminal_render_span_t *span,
    const char *message,
    bool primary,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (span == NULL ||
        !span->valid) {
        return;
    }

    if (span->start_line ==
        span->end_line) {

        if (vitte_terminal_render_single_line_span(
                stream,
                span,
                message,
                primary,
                options
            )) {

            return;
        }
    } else {
        if (vitte_terminal_render_multiline_span(
                stream,
                span,
                message,
                primary,
                options
            )) {

            return;
        }
    }

    vitte_terminal_render_span_fallback(
        stream,
        span,
        message,
        primary,
        options
    );
}

/* ========================================================================= */
/* Labels                                                                    */
/* ========================================================================= */

static void
vitte_terminal_render_labels(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    size_t index;

    if (!options->show_labels) {
        return;
    }

    if (diagnostic->label_count == 0u) {
        vitte_terminal_render_span_t span;
        const char *message;

        if (vitte_terminal_render_span_from_diagnostic(
                diagnostic,
                &span
            )) {

            message =
                diagnostic->has_contract &&
                vitte_terminal_text_present(
                    diagnostic->contract.reason
                )
                    ? diagnostic->contract.reason
                    : diagnostic->details;

            vitte_terminal_render_span(
                stream,
                &span,
                message,
                true,
                options
            );
        }

        return;
    }

    /*
     * Primary labels first.
     */
    for (index = 0u;
         index < diagnostic->label_count;
         index++) {

        const vitte_diagnostic_label_t *label;
        vitte_terminal_render_span_t span;

        label =
            &diagnostic->labels[index];

        if (label->style !=
            VITTE_DIAGNOSTIC_LABEL_PRIMARY) {

            continue;
        }

        if (!vitte_terminal_render_span_from_ast(
                &label->span,
                &span
            )) {

            continue;
        }

        vitte_terminal_render_span(
            stream,
            &span,
            label->message,
            true,
            options
        );
    }

    /*
     * Secondary labels afterwards.
     */
    for (index = 0u;
         index < diagnostic->label_count;
         index++) {

        const vitte_diagnostic_label_t *label;
        vitte_terminal_render_span_t span;

        label =
            &diagnostic->labels[index];

        if (label->style ==
            VITTE_DIAGNOSTIC_LABEL_PRIMARY) {

            continue;
        }

        if (!vitte_terminal_render_span_from_ast(
                &label->span,
                &span
            )) {

            continue;
        }

        vitte_terminal_render_span(
            stream,
            &span,
            label->message,
            false,
            options
        );
    }
}

/* ========================================================================= */
/* Location kind                                                             */
/* ========================================================================= */

static const char *
vitte_terminal_location_kind_name(
    vitte_diagnostic_location_kind_t kind
)
{
    switch (kind) {
        case VITTE_DIAGNOSTIC_LOCATION_PRIMARY:
            return "primary location";

        case VITTE_DIAGNOSTIC_LOCATION_DECLARATION:
            return "declared here";

        case VITTE_DIAGNOSTIC_LOCATION_DEFINITION:
            return "defined here";

        case VITTE_DIAGNOSTIC_LOCATION_ORIGIN:
            return "originates here";

        case VITTE_DIAGNOSTIC_LOCATION_USE:
            return "used here";

        case VITTE_DIAGNOSTIC_LOCATION_CALL_SITE:
            return "called here";

        case VITTE_DIAGNOSTIC_LOCATION_INSTANTIATION:
            return "instantiated here";

        case VITTE_DIAGNOSTIC_LOCATION_RELATED:
            return "related location";

        default:
            return "related location";
    }
}

/* ========================================================================= */
/* Related locations                                                         */
/* ========================================================================= */

static void
vitte_terminal_render_related(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    size_t index;

    if (!options->show_related) {
        return;
    }

    for (index = 0u;
         index < diagnostic->related_count;
         index++) {

        const vitte_diagnostic_location_t *location;
        vitte_terminal_render_span_t span;

        location =
            &diagnostic->related[index];

        if (!vitte_terminal_render_span_from_ast(
                &location->span,
                &span
            )) {

            continue;
        }

        (void)fputs(
            "  ",
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_CYAN
        );

        (void)fputs(
            "= ",
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );

        (void)fputs(
            vitte_terminal_location_kind_name(
                location->kind
            ),
            stream
        );

        if (vitte_terminal_text_present(
                location->label
            )) {

            (void)fprintf(
                stream,
                ": %s",
                location->label
            );
        }

        if (vitte_terminal_text_present(
                span.source_name
            )) {

            (void)fprintf(
                stream,
                " at %s:%zu:%zu",
                span.source_name,
                span.start_line + 1u,
                span.start_column + 1u
            );
        }

        (void)fputc(
            '\n',
            stream
        );
    }
}

/* ========================================================================= */
/* Details                                                                   */
/* ========================================================================= */

static void
vitte_terminal_render_details(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (!vitte_terminal_text_present(
            diagnostic->details
        )) {

        return;
    }

    (void)fputs(
        "  ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_DIM
    );

    (void)fputs(
        diagnostic->details,
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputc(
        '\n',
        stream
    );
}

static void
vitte_terminal_render_registry_help(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options)
{
    char help[VITTE_DIAGNOSTIC_MESSAGE_CAPACITY];
    if (options == NULL || !options->show_help ||
        vitte_diagnostic_has_help(diagnostic)) return;
    if (vitte_diagnostic_format_help(diagnostic, options->sources, help, sizeof(help))) {
        (void)fprintf(stream, "  help: %s\n", help);
    }
}

/* ========================================================================= */
/* Context                                                                   */
/* ========================================================================= */

static void
vitte_terminal_render_context_item(
    FILE *stream,
    const char *name,
    const char *value,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (!vitte_terminal_text_present(
            value
        )) {

        return;
    }

    (void)fputs(
        "  ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_DIM
    );

    (void)fprintf(
        stream,
        "%s:",
        name
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fprintf(
        stream,
        " %s\n",
        value
    );
}

static void
vitte_terminal_render_context(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    bool generic_subject_differs;

    if (!options->show_context) {
        return;
    }

    generic_subject_differs =
        (vitte_terminal_text_present(diagnostic->expected) &&
         (!vitte_terminal_text_present(diagnostic->expected_type) ||
          strcmp(diagnostic->expected, diagnostic->expected_type) != 0)) ||
        (vitte_terminal_text_present(diagnostic->found) &&
         (!vitte_terminal_text_present(diagnostic->actual_type) ||
          strcmp(diagnostic->found, diagnostic->actual_type) != 0));

    vitte_terminal_render_context_item(
        stream,
        "package",
        diagnostic->package,
        options
    );

    vitte_terminal_render_context_item(
        stream,
        "module",
        diagnostic->module,
        options
    );

    vitte_terminal_render_context_item(
        stream,
        "procedure",
        vitte_terminal_text_present(
            diagnostic->procedure
        )
            ? diagnostic->procedure
            : diagnostic->has_contract
                ? diagnostic->contract.procedure
                : NULL,
        options
    );

    vitte_terminal_render_context_item(
        stream,
        "symbol",
        diagnostic->symbol,
        options
    );

    if (vitte_terminal_text_present(
            diagnostic->expected_type
        ) ||
        vitte_terminal_text_present(
            diagnostic->actual_type
        )) {

        (void)fputs(
            "  ",
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_DIM
        );

        (void)fputs(
            "type:",
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );

        if (vitte_terminal_text_present(
                diagnostic->expected_type
            )) {

            (void)fprintf(
                stream,
                " expected `%s`",
                diagnostic->expected_type
            );
        }

        if (vitte_terminal_text_present(
                diagnostic->actual_type
            )) {

            if (vitte_terminal_text_present(
                    diagnostic->expected_type
                )) {

                (void)fputs(
                    ",",
                    stream
                );
            }

            (void)fprintf(
                stream,
                " found `%s`",
                diagnostic->actual_type
            );
        }

        (void)fputc(
            '\n',
            stream
        );
    }

    if (generic_subject_differs) {
        (void)fputs("  ", stream);
        vitte_terminal_style(stream, options, VITTE_TERM_DIM);
        (void)fputs("expected/found:", stream);
        vitte_terminal_reset(stream, options);
        if (vitte_terminal_text_present(diagnostic->expected)) {
            (void)fprintf(stream, " expected `%s`", diagnostic->expected);
        }
        if (vitte_terminal_text_present(diagnostic->found)) {
            if (vitte_terminal_text_present(diagnostic->expected)) {
                (void)fputs(",", stream);
            }
            (void)fprintf(stream, " found `%s`", diagnostic->found);
        }
        (void)fputc('\n', stream);
    }

    vitte_terminal_render_context_item(
        stream,
        "probable cause",
        diagnostic->probable_cause,
        options
    );

    vitte_terminal_render_context_item(
        stream,
        "context",
        diagnostic->context,
        options
    );
}

/* ========================================================================= */
/* Causes                                                                    */
/* ========================================================================= */

static void
vitte_terminal_render_causes(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    size_t index;

    if (!options->show_causes) {
        return;
    }

    for (index = 0u;
         index < diagnostic->cause_count;
         index++) {

        const vitte_diagnostic_cause_t *cause;

        cause =
            &diagnostic->causes[index];

        (void)fputs(
            "  ",
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_MAGENTA
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_BOLD
        );

        (void)fputs(
            "caused by",
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );

        (void)fprintf(
            stream,
            ": %s",
            vitte_terminal_safe(
                cause->message
            )
        );

        if (cause->has_span &&
            vitte_ast_span_is_valid(cause->span)) {

            if (vitte_terminal_text_present(
                    cause->span.source_name
                )) {

                (void)fprintf(
                    stream,
                    " at %s:%u:%u",
                    cause->span.source_name,
                    cause->span.start_line + 1u,
                    cause->span.start_column + 1u
                );
            }
        }

        (void)fputc(
            '\n',
            stream
        );
    }
}

/* ========================================================================= */
/* Annotations                                                               */
/* ========================================================================= */

static void
vitte_terminal_render_annotations(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    vitte_diagnostic_annotation_kind_t kind,
    const vitte_diagnostic_terminal_options_t *options
)
{
    size_t index;

    for (index = 0u;
         index < diagnostic->annotation_count;
         index++) {

        const vitte_diagnostic_annotation_t *annotation;
        const char *name;
        const char *color;

        annotation =
            &diagnostic->annotations[index];

        if (annotation->kind != kind) {
            continue;
        }

        switch (annotation->kind) {
            case VITTE_DIAGNOSTIC_ANNOTATION_NOTE:
                if (!options->show_notes) {
                    continue;
                }

                name = "note";
                color = VITTE_TERM_CYAN;
                break;

            case VITTE_DIAGNOSTIC_ANNOTATION_HELP:
                if (!options->show_help) {
                    continue;
                }

                name = "help";
                color = VITTE_TERM_GREEN;
                break;

            default:
                name = "info";
                color = VITTE_TERM_CYAN;
                break;
        }

        (void)fputs(
            "  ",
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            color
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_BOLD
        );

        (void)fputs(
            name,
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );

        (void)fprintf(
            stream,
            ": %s",
            vitte_terminal_safe(
                annotation->message
            )
        );

        if (annotation->has_span &&
            vitte_ast_span_is_valid(annotation->span) &&
            vitte_terminal_text_present(
                annotation->span.source_name
            )) {

            (void)fprintf(
                stream,
                " at %s:%u:%u",
                annotation->span.source_name,
                annotation->span.start_line + 1u,
                annotation->span.start_column + 1u
            );
        }

        (void)fputc(
            '\n',
            stream
        );
    }
}

/* ========================================================================= */
/* Suggestion diff                                                           */
/* ========================================================================= */

static void
vitte_terminal_render_suggestion_replacement(
    FILE *stream,
    const vitte_diagnostic_suggestion_t *suggestion,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (!vitte_terminal_text_present(
            suggestion->replacement
        )) {

        return;
    }

    (void)fputs(
        "      ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_BRIGHT_GREEN
    );

    (void)fputs(
        "+ ",
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_GREEN
    );

    (void)fputs(
        suggestion->replacement,
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputc(
        '\n',
        stream
    );
}

/* ========================================================================= */
/* Suggestions                                                               */
/* ========================================================================= */

static void
vitte_terminal_render_suggestions(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    size_t index;

    if (!options->show_suggestions) {
        return;
    }

    for (index = 0u;
         index < diagnostic->suggestion_count;
         index++) {

        const vitte_diagnostic_suggestion_t *suggestion;

        suggestion =
            &diagnostic->suggestions[index];

        (void)fputs(
            "  ",
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_GREEN
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_BOLD
        );

        (void)fputs(
            "help",
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );

        (void)fprintf(
            stream,
            ": %s",
            vitte_terminal_safe(
                suggestion->message
            )
        );

        (void)fputs(
            " [",
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_DIM
        );

        (void)fputs(
            vitte_diagnostic_applicability_name(
                suggestion->applicability
            ),
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );

        (void)fputs(
            "]\n",
            stream
        );

        if (suggestion->has_span &&
            vitte_ast_span_is_valid(suggestion->span)) {

            vitte_terminal_render_span_t span;

            if (vitte_terminal_render_span_from_ast(
                    &suggestion->span,
                    &span
                )) {

                vitte_terminal_render_span(
                    stream,
                    &span,
                    "replace this",
                    false,
                    options
                );
            }
        }

        vitte_terminal_render_suggestion_replacement(
            stream,
            suggestion,
            options
        );
    }
}

/* ========================================================================= */
/* Contract                                                                  */
/* ========================================================================= */

static const char *
vitte_terminal_contract_kind_name(
    vitte_diagnostic_contract_kind_t kind
)
{
    switch (kind) {
        case VITTE_DIAGNOSTIC_CONTRACT_NONE:
            return "contract";

        case VITTE_DIAGNOSTIC_CONTRACT_REQUIRES:
            return "requires";

        case VITTE_DIAGNOSTIC_CONTRACT_ENSURES:
            return "ensures";

        case VITTE_DIAGNOSTIC_CONTRACT_INVARIANT:
            return "invariant";

        case VITTE_DIAGNOSTIC_CONTRACT_ASSERTION:
            return "assertion";

        default:
            return "contract";
    }
}

static void
vitte_terminal_render_contract_span(
    FILE *stream,
    const char *title,
    const vitte_ast_span_t *span,
    const vitte_diagnostic_terminal_options_t *options
)
{
    vitte_terminal_render_span_t rendered;

    if (span == NULL ||
        !vitte_terminal_render_span_from_ast(
            span,
            &rendered
        )) {

        return;
    }

    (void)fputs(
        "    ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_MAGENTA
    );

    (void)fprintf(
        stream,
        "%s:",
        title
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fprintf(
        stream,
        " %s:%zu:%zu\n",
        vitte_terminal_safe(
            rendered.source_name
        ),
        rendered.start_line + 1u,
        rendered.start_column + 1u
    );
}

static void
vitte_terminal_render_contract(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    const vitte_diagnostic_contract_t *contract;

    if (!options->show_contract ||
        !diagnostic->has_contract) {

        return;
    }

    contract =
        &diagnostic->contract;

    (void)fputs(
        "  ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_MAGENTA
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_BOLD
    );

    (void)fputs(
        "contract",
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fprintf(
        stream,
        ": %s",
        vitte_terminal_contract_kind_name(
            contract->kind
        )
    );

    if (vitte_terminal_text_present(
            contract->expression
        )) {

        (void)fputs(
            " `",
            stream
        );

        vitte_terminal_style(
            stream,
            options,
            VITTE_TERM_BRIGHT_MAGENTA
        );

        (void)fputs(
            contract->expression,
            stream
        );

        vitte_terminal_reset(
            stream,
            options
        );

        (void)fputc(
            '`',
            stream
        );
    }

    (void)fputc(
        '\n',
        stream
    );

    if (!options->show_context &&
        vitte_terminal_text_present(
            contract->procedure
        )) {

        (void)fprintf(
            stream,
            "    procedure: %s\n",
            contract->procedure
        );
    }

    if (diagnostic->label_count != 0u &&
        vitte_terminal_text_present(
            contract->reason
        )) {

        (void)fprintf(
            stream,
            "  = %s\n",
            contract->reason
        );
    }

    if (contract->has_contract_span) {
        vitte_terminal_render_contract_span(
            stream,
            "contract declared",
            &contract->contract_span,
            options
        );
    }

    if (contract->has_call_span) {
        vitte_terminal_render_contract_span(
            stream,
            "call site",
            &contract->call_span,
            options
        );
    }

    if (contract->has_declaration_span) {
        vitte_terminal_render_contract_span(
            stream,
            "procedure declared",
            &contract->declaration_span,
            options
        );
    }
}

/* ========================================================================= */
/* Cascade metadata                                                          */
/* ========================================================================= */

static void
vitte_terminal_render_cascade(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (!options->show_cascade) {
        return;
    }

    if (!diagnostic->is_cascade &&
        !diagnostic->is_suppressed &&
        diagnostic->root_diagnostic ==
            VITTE_DIAGNOSTIC_INDEX_NONE &&
        diagnostic->parent_diagnostic ==
            VITTE_DIAGNOSTIC_INDEX_NONE) {

        return;
    }

    (void)fputs(
        "  ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_DIM
    );

    (void)fputs(
        "diagnostic relation:",
        stream
    );

    if (diagnostic->is_primary) {
        (void)fputs(
            " primary",
            stream
        );
    }

    if (diagnostic->is_cascade) {
        (void)fputs(
            " cascade",
            stream
        );
    }

    if (diagnostic->is_suppressed) {
        (void)fputs(
            " suppressed",
            stream
        );
    }

    if (diagnostic->parent_diagnostic !=
        VITTE_DIAGNOSTIC_INDEX_NONE) {

        (void)fprintf(
            stream,
            " parent=%zu",
            diagnostic->parent_diagnostic
        );
    }

    if (diagnostic->root_diagnostic !=
        VITTE_DIAGNOSTIC_INDEX_NONE) {

        (void)fprintf(
            stream,
            " root=%zu",
            diagnostic->root_diagnostic
        );
    }

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputc(
        '\n',
        stream
    );
}

/* ========================================================================= */
/* Fingerprint                                                               */
/* ========================================================================= */

static void
vitte_terminal_render_fingerprint(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (!options->show_fingerprint) {
        return;
    }

    (void)fputs(
        "  ",
        stream
    );

    vitte_terminal_style(
        stream,
        options,
        VITTE_TERM_DIM
    );

    (void)fprintf(
        stream,
        "fingerprint: %016" PRIx64,
        diagnostic->fingerprint
    );

    vitte_terminal_reset(
        stream,
        options
    );

    (void)fputc(
        '\n',
        stream
    );
}

/* ========================================================================= */
/* Compact renderer                                                          */
/* ========================================================================= */

static vitte_status_t
vitte_terminal_render_compact(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    if (stream == NULL ||
        diagnostic == NULL ||
        options == NULL) {

        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    if (vitte_terminal_text_present(
            diagnostic->source_name
        )) {

        (void)fputs(
            diagnostic->source_name,
            stream
        );

        if (diagnostic->has_span) {
            (void)fprintf(
                stream,
                ":%zu:%zu",
                diagnostic->start_line + 1u,
                diagnostic->start_column + 1u
            );
        }

        (void)fputs(
            ": ",
            stream
        );
    }

    vitte_terminal_style(
        stream,
        options,
        vitte_terminal_severity_color(
            diagnostic->severity
        )
    );

    (void)fputs(
        vitte_terminal_severity_name(
            diagnostic->severity
        ),
        stream
    );

    vitte_terminal_reset(
        stream,
        options
    );

    if (options->show_code &&
        vitte_terminal_text_present(
            diagnostic->short_code
        )) {

        (void)fprintf(
            stream,
            "[%s]",
            diagnostic->short_code
        );
    }

    (void)fprintf(
        stream,
        ": %s",
        vitte_terminal_safe(
            diagnostic->message
        )
    );

    if (options->show_phase &&
        diagnostic->origin !=
            VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN) {

        (void)fprintf(
            stream,
            " [%s]",
            vitte_diagnostic_origin_name(
                diagnostic->origin
            )
        );
    }

    (void)fputc(
        '\n',
        stream
    );

    return ferror(stream)
        ? VITTE_STATUS_ERROR_INVALID_STATE
        : VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Public: one diagnostic                                                    */
/* ========================================================================= */

vitte_status_t
vitte_diagnostic_render_terminal_one(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options
)
{
    vitte_diagnostic_terminal_options_t effective;

    if (stream == NULL ||
        diagnostic == NULL) {

        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    vitte_diagnostic_terminal_options_init(
        &effective
    );

    if (options != NULL) {
        effective = *options;
    }

    if (diagnostic->is_suppressed &&
        !effective.include_suppressed) {

        return VITTE_STATUS_OK;
    }

    if (effective.tab_width == 0u) {
        effective.tab_width =
            VITTE_TERMINAL_DEFAULT_TAB_WIDTH;
    }

    if (effective.compact) {
        return vitte_terminal_render_compact(
            stream,
            diagnostic,
            &effective
        );
    }

    vitte_terminal_render_header(
        stream,
        diagnostic,
        &effective
    );

    vitte_terminal_render_context(
        stream,
        diagnostic,
        &effective
    );

    if (effective.show_source) {
        vitte_terminal_render_main_location(
            stream,
            diagnostic,
            &effective
        );

        vitte_terminal_render_labels(
            stream,
            diagnostic,
            &effective
        );
    }

    vitte_terminal_render_related(
        stream,
        diagnostic,
        &effective
    );

    vitte_terminal_render_details(
        stream,
        diagnostic,
        &effective
    );

    vitte_terminal_render_contract(
        stream,
        diagnostic,
        &effective
    );

    vitte_terminal_render_annotations(
        stream,
        diagnostic,
        VITTE_DIAGNOSTIC_ANNOTATION_NOTE,
        &effective
    );

    vitte_terminal_render_causes(
        stream,
        diagnostic,
        &effective
    );

    vitte_terminal_render_annotations(
        stream,
        diagnostic,
        VITTE_DIAGNOSTIC_ANNOTATION_HELP,
        &effective
    );

    vitte_terminal_render_registry_help(
        stream,
        diagnostic,
        &effective
    );

    vitte_terminal_render_suggestions(
        stream,
        diagnostic,
        &effective
    );

    vitte_terminal_render_cascade(
        stream,
        diagnostic,
        &effective
    );

    vitte_terminal_render_fingerprint(
        stream,
        diagnostic,
        &effective
    );

    if (effective.trailing_newline) {
        (void)fputc(
            '\n',
            stream
        );
    }

    return ferror(stream)
        ? VITTE_STATUS_ERROR_INVALID_STATE
        : VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Summary                                                                   */
/* ========================================================================= */

vitte_status_t
vitte_diagnostic_render_terminal_summary(
    FILE *stream,
    const vitte_diagnostic_bag_t *bag,
    const vitte_diagnostic_terminal_options_t *options
)
{
    vitte_diagnostic_terminal_options_t effective;
    size_t errors;

    if (stream == NULL ||
        bag == NULL) {

        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    vitte_diagnostic_terminal_options_init(
        &effective
    );

    if (options != NULL) {
        effective = *options;
    }

    errors =
        bag->counts.error_count +
        bag->counts.fatal_count;

    if (errors != 0u) {
        vitte_terminal_style(
            stream,
            &effective,
            VITTE_TERM_RED
        );
    } else if (bag->counts.warning_count != 0u) {
        vitte_terminal_style(
            stream,
            &effective,
            VITTE_TERM_YELLOW
        );
    } else {
        vitte_terminal_style(
            stream,
            &effective,
            VITTE_TERM_GREEN
        );
    }

    vitte_terminal_style(
        stream,
        &effective,
        VITTE_TERM_BOLD
    );

    if (errors == 0u &&
        bag->counts.warning_count == 0u) {

        (void)fputs(
            "diagnostics: no errors",
            stream
        );
    } else {
        (void)fprintf(
            stream,
            "diagnostics: %zu error%s, %zu warning%s",
            errors,
            errors == 1u ? "" : "s",
            bag->counts.warning_count,
            bag->counts.warning_count == 1u
                ? ""
                : "s"
        );
    }

    vitte_terminal_reset(
        stream,
        &effective
    );

    if (bag->counts.note_count != 0u) {
        (void)fprintf(
            stream,
            ", %zu note%s",
            bag->counts.note_count,
            bag->counts.note_count == 1u
                ? ""
                : "s"
        );
    }

    if (bag->counts.help_count != 0u) {
        (void)fprintf(
            stream,
            ", %zu help",
            bag->counts.help_count
        );
    }

    if (bag->counts.suppressed_count != 0u) {
        (void)fprintf(
            stream,
            ", %zu suppressed",
            bag->counts.suppressed_count
        );
    }

    (void)fputc(
        '\n',
        stream
    );

    return ferror(stream)
        ? VITTE_STATUS_ERROR_INVALID_STATE
        : VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Public: complete bag                                                      */
/* ========================================================================= */

vitte_status_t
vitte_diagnostic_render_terminal(
    FILE *stream,
    const vitte_diagnostic_bag_t *bag,
    const vitte_diagnostic_terminal_options_t *options
)
{
    vitte_diagnostic_terminal_options_t effective;
    size_t index;
    size_t emitted;

    if (stream == NULL ||
        bag == NULL) {

        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    vitte_diagnostic_terminal_options_init(
        &effective
    );

    if (options != NULL) {
        effective = *options;
    }

    emitted = 0u;

    for (index = 0u;
         index < bag->count;
         index++) {

        const vitte_diagnostic_t *diagnostic;
        vitte_status_t status;

        diagnostic =
            &bag->storage[index];

        if (diagnostic->is_suppressed &&
            !effective.include_suppressed) {

            continue;
        }

        if (!effective.compact &&
            emitted != 0u) {

            (void)fputc(
                '\n',
                stream
            );
        }

        status =
            vitte_diagnostic_render_terminal_one(
                stream,
                diagnostic,
                &effective
            );

        if (status !=
            VITTE_STATUS_OK) {

            return status;
        }

        emitted++;
    }

    if (!effective.compact) {
        if (emitted != 0u) {
            (void)fputc(
                '\n',
                stream
            );
        }

        return vitte_diagnostic_render_terminal_summary(
            stream,
            bag,
            &effective
        );
    }

    return ferror(stream)
        ? VITTE_STATUS_ERROR_INVALID_STATE
        : VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Compatibility adapter                                                     */
/* ========================================================================= */

vitte_status_t
vitte_diagnostic_write_terminal(
    FILE *stream,
    const vitte_diagnostic_bag_t *bag
)
{
    vitte_diagnostic_terminal_options_t options;

    vitte_diagnostic_terminal_options_init(
        &options
    );

    return vitte_diagnostic_render_terminal(
        stream,
        bag,
        &options
    );
}

/* ========================================================================= */
/* Metadata                                                                  */
/* ========================================================================= */

const char *
vitte_diagnostic_terminal_renderer_name(void)
{
    return "terminal";
}

/* Build the same concise headline before selecting an output format. */
static const char *
vitte_cli_diagnostic_message(
    const vitte_cli_diagnostic_t *input,
    const vitte_diagnostic_registry_entry_t *entry,
    const vitte_source_context_t *sources,
    vitte_source_id_t source_id,
    char *buffer,
    size_t capacity)
{
    const vitte_source_entry_t *source;
    vitte_source_id_t diagnostic_source_id;
    size_t length;
    size_t index;
    int written;
    if (vitte_terminal_text_present(input->message)) return input->message;
    if (entry == NULL) return NULL;
    if (strcmp(entry->public_code, "E0201") == 0 &&
        vitte_terminal_text_present(input->details)) return input->details;
    if (strcmp(entry->public_code, "E0401") != 0 || !input->has_span) return entry->title;
    diagnostic_source_id = input->source_id != VITTE_SOURCE_INVALID_ID
        ? input->source_id
        : source_id;
    source = vitte_source_get(sources, diagnostic_source_id);
    if (source == NULL || input->start_offset >= input->end_offset ||
        input->end_offset > source->length) return entry->title;
    length = input->end_offset - input->start_offset;
    if (length > 80u) return entry->title;
    for (index = input->start_offset; index < input->end_offset; ++index) {
        unsigned char character = (unsigned char)source->data[index];
        if (character < 0x20u || character == 0x7fu || character == '`') return entry->title;
    }
    written = snprintf(buffer, capacity, "%s `%.*s`", entry->title,
        (int)length, source->data + input->start_offset);
    return written >= 0 && (size_t)written < capacity ? buffer : entry->title;
}

static vitte_diagnostic_origin_t
vitte_cli_origin_to_diagnostic(
    vitte_cli_diagnostic_origin_t origin
)
{
    switch (origin) {
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_UNKNOWN:
            return VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_IO:
            return VITTE_DIAGNOSTIC_ORIGIN_IO;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_LEXER:
            return VITTE_DIAGNOSTIC_ORIGIN_LEXER;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_SCANNER:
            return VITTE_DIAGNOSTIC_ORIGIN_SCANNER;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_PARSER:
            return VITTE_DIAGNOSTIC_ORIGIN_PARSER;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_IMPORT:
            return VITTE_DIAGNOSTIC_ORIGIN_IMPORT;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION:
            return VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_TYPE_CHECK:
            return VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_CONTRACT:
            return VITTE_DIAGNOSTIC_ORIGIN_CONTRACT;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_CONSTANT_EVAL:
            return VITTE_DIAGNOSTIC_ORIGIN_CONSTANT_EVAL;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_HIR:
            return VITTE_DIAGNOSTIC_ORIGIN_HIR;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_IR:
            return VITTE_DIAGNOSTIC_ORIGIN_IR;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_CODEGEN:
            return VITTE_DIAGNOSTIC_ORIGIN_CODEGEN;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_C17_BACKEND:
            return VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_C_COMPILER:
            return VITTE_DIAGNOSTIC_ORIGIN_C_COMPILER;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_LINKER:
            return VITTE_DIAGNOSTIC_ORIGIN_LINKER;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_RUNTIME:
            return VITTE_DIAGNOSTIC_ORIGIN_RUNTIME;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_DRIVER:
            return VITTE_DIAGNOSTIC_ORIGIN_DRIVER;
        case VITTE_CLI_DIAGNOSTIC_ORIGIN_INTERNAL:
            return VITTE_DIAGNOSTIC_ORIGIN_INTERNAL;
    }
    return VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN;
}

static bool
vitte_cli_copy_diagnostic_text(
    char *destination,
    size_t capacity,
    const char *source,
    const char **view
)
{
    int written;

    if (destination == NULL || capacity == 0u || view == NULL) {
        return false;
    }
    if (source == NULL) {
        destination[0] = '\0';
        *view = destination;
        return true;
    }
    written = snprintf(destination, capacity, "%s", source);
    if (written < 0 || (size_t)written >= capacity) {
        return false;
    }
    *view = destination;
    return true;
}

bool
vitte_diagnostic_render_cli_batch_ex(
    FILE *stream,
    const vitte_source_context_t *sources,
    vitte_source_id_t source_id,
    const char *source_name,
    const vitte_cli_diagnostic_t *diagnostics,
    size_t diagnostic_count,
    const vitte_cli_diagnostic_render_options_t *render_options)
{
    vitte_diagnostic_t *storage;
    vitte_diagnostic_bag_t bag;
    vitte_diagnostic_terminal_options_t terminal_options;
    vitte_cli_diagnostic_render_options_t effective_options;
    vitte_cli_lsp_fallback_context_t lsp_fallback_context;
    bool lsp_fallback_available;
    bool color;
    vitte_cli_diagnostic_format_t format;
    vitte_status_t status;
    size_t index;
    size_t previous_count;
    bool rendered;

    if (stream == NULL ||
        diagnostics == NULL ||
        diagnostic_count == 0u ||
        diagnostic_count > SIZE_MAX / sizeof(*storage)) {
        return false;
    }

    vitte_cli_diagnostic_render_options_init(&effective_options);
    if (render_options != NULL) {
        effective_options = *render_options;
    }
    format = effective_options.format;
    color = effective_options.color;
    if (!vitte_cli_diagnostic_format_is_valid(format)) {
        return false;
    }

    storage = (vitte_diagnostic_t *)calloc(
        diagnostic_count,
        sizeof(*storage));
    if (storage == NULL) {
        return false;
    }

    status = vitte_diagnostic_bag_init(
        &bag,
        storage,
        diagnostic_count);
    if (status != VITTE_STATUS_OK) {
        free(storage);
        return false;
    }
    rendered = false;
    bag.options.max_diagnostics = diagnostic_count;
    bag.options.sources = sources;
    lsp_fallback_context.diagnostics = diagnostics;
    lsp_fallback_context.diagnostic_count = diagnostic_count;
    lsp_fallback_context.source_id = source_id;
    lsp_fallback_context.source_name = source_name;
    lsp_fallback_available = false;

    for (index = 0u; index < diagnostic_count; ++index) {
        const vitte_cli_diagnostic_t *input;
        vitte_diagnostic_t *diagnostic;
        vitte_diagnostic_registry_entry_t const *entry;
        vitte_diagnostic_severity_t severity;
        vitte_ast_span_t span;
        const vitte_ast_span_t *span_pointer;
        const char *message;
        const char *details;
        char message_storage[VITTE_DIAGNOSTIC_MESSAGE_CAPACITY];

        input = &diagnostics[index];
        if (input->code == NULL ||
            input->end_offset < input->start_offset) {
            goto cleanup;
        }

        entry = vitte_diagnostic_registry_find(input->code);
        message = vitte_cli_diagnostic_message(input, entry, sources, source_id,
            message_storage, sizeof(message_storage));
        details = input->has_span
            ? input->details
            : vitte_terminal_text_present(input->details)
                ? input->details
                : entry != NULL ? entry->explanation : NULL;
        if (!vitte_terminal_text_present(message)) {
            goto cleanup;
        }

        switch (input->severity) {
            case VITTE_CLI_DIAGNOSTIC_NOTE:
                severity = VITTE_DIAGNOSTIC_NOTE;
                break;
            case VITTE_CLI_DIAGNOSTIC_HELP:
                severity = VITTE_DIAGNOSTIC_HELP;
                break;
            case VITTE_CLI_DIAGNOSTIC_WARNING:
                severity = VITTE_DIAGNOSTIC_WARNING;
                break;
            case VITTE_CLI_DIAGNOSTIC_ERROR:
                severity = VITTE_DIAGNOSTIC_ERROR;
                break;
            case VITTE_CLI_DIAGNOSTIC_FATAL:
                severity = VITTE_DIAGNOSTIC_FATAL;
                break;
            default:
                goto cleanup;
        }

        memset(&span, 0, sizeof(span));
        span_pointer = NULL;
        if (input->has_span) {
            if (input->start_line > UINT_MAX ||
                input->start_column > UINT_MAX ||
                input->end_line > UINT_MAX ||
                input->end_column > UINT_MAX) {
                goto cleanup;
            }

            span.file_id = input->file_id;
            span.source_id = input->source_id != VITTE_SOURCE_INVALID_ID
                ? input->source_id : source_id;
            span.source_name = input->source_name != NULL
                ? input->source_name : source_name;
            span.start_offset = input->start_offset;
            span.end_offset = input->end_offset;
            span.start_line = (unsigned int)input->start_line;
            span.start_column = (unsigned int)input->start_column;
            span.end_line = (unsigned int)input->end_line;
            span.end_column = (unsigned int)input->end_column;
            span.valid = true;
            span_pointer = &span;
            lsp_fallback_available = true;
        }

        previous_count = bag.count;
        status = vitte_diagnostic_emit(
            &bag,
            severity,
            input->origin != VITTE_CLI_DIAGNOSTIC_ORIGIN_UNKNOWN
                ? vitte_cli_origin_to_diagnostic(input->origin)
                : entry != NULL
                    ? entry->origin
                    : VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN,
            input->code,
            message,
            span_pointer);
        if (status != VITTE_STATUS_OK) {
            goto cleanup;
        }
        if (bag.count == previous_count) {
            continue;
        }

        diagnostic = vitte_diagnostic_at_mut(&bag, bag.count - 1u);
        if (diagnostic == NULL) {
            goto cleanup;
        }

        if (span_pointer != NULL) {
            if (diagnostic->start_line > UINT_MAX || diagnostic->start_column > UINT_MAX ||
                diagnostic->end_line > UINT_MAX || diagnostic->end_column > UINT_MAX) {
                goto cleanup;
            }
            span.source_id = diagnostic->source_id;
            span.source_name = diagnostic->source_name;
            span.start_line = (unsigned int)diagnostic->start_line;
            span.start_column = (unsigned int)diagnostic->start_column;
            span.end_line = (unsigned int)diagnostic->end_line;
            span.end_column = (unsigned int)diagnostic->end_column;
        }

        diagnostic->severity = severity;
        if (input->short_code != NULL &&
            !vitte_cli_copy_diagnostic_text(
                diagnostic->short_code_storage,
                sizeof(diagnostic->short_code_storage),
                input->short_code,
                &diagnostic->short_code)) {
            goto cleanup;
        }
        if (input->category != NULL &&
            !vitte_cli_copy_diagnostic_text(
                diagnostic->category_storage,
                sizeof(diagnostic->category_storage),
                input->category,
                &diagnostic->category)) {
            goto cleanup;
        }
        diagnostic->details =
            format == VITTE_CLI_DIAGNOSTIC_TERMINAL &&
                    input->has_span
                ? ""
                : details != NULL ? details : "";
        if (span_pointer != NULL &&
            (vitte_terminal_text_present(input->details) ||
             (entry != NULL &&
              vitte_terminal_text_present(entry->explanation)))) {
            const char *label;

            label = vitte_terminal_text_present(input->details)
                ? input->details
                : entry->explanation;
            status = vitte_diagnostic_add_label(
                diagnostic,
                VITTE_DIAGNOSTIC_LABEL_PRIMARY,
                span_pointer,
                label);
            if (status != VITTE_STATUS_OK) {
                goto cleanup;
            }
        }

        if (input->labels != NULL) {
            size_t label_index;

            if (input->label_count > VITTE_CLI_DIAGNOSTIC_MAX_LABELS) {
                goto cleanup;
            }
            for (label_index = 0u;
                 label_index < input->label_count;
                 ++label_index) {
                const vitte_cli_diagnostic_label_t *input_label =
                    &input->labels[label_index];
                vitte_ast_span_t label_span;

                if ((input_label->style < VITTE_CLI_DIAGNOSTIC_LABEL_PRIMARY ||
                     input_label->style > VITTE_CLI_DIAGNOSTIC_LABEL_SECONDARY) ||
                    !input_label->span.valid ||
                    input_label->span.start_line > UINT_MAX ||
                    input_label->span.start_column > UINT_MAX ||
                    input_label->span.end_line > UINT_MAX ||
                    input_label->span.end_column > UINT_MAX) {
                    goto cleanup;
                }
                memset(&label_span, 0, sizeof(label_span));
                label_span.file_id = input_label->span.file_id;
                label_span.source_id = input_label->span.source_id !=
                    VITTE_SOURCE_INVALID_ID ? input_label->span.source_id : source_id;
                label_span.source_name = input_label->span.source_name != NULL
                    ? input_label->span.source_name : source_name;
                label_span.start_offset = input_label->span.start_offset;
                label_span.end_offset = input_label->span.end_offset;
                label_span.start_line = (unsigned int)input_label->span.start_line;
                label_span.start_column = (unsigned int)input_label->span.start_column;
                label_span.end_line = (unsigned int)input_label->span.end_line;
                label_span.end_column = (unsigned int)input_label->span.end_column;
                label_span.valid = true;
                status = vitte_diagnostic_add_label(
                    diagnostic,
                    (vitte_diagnostic_label_style_t)input_label->style,
                    &label_span,
                    input_label->message);
                if (status != VITTE_STATUS_OK) goto cleanup;
            }
        }

        vitte_diagnostic_set_context(
            diagnostic,
            input->package,
            input->module,
            input->procedure,
            input->context);

        vitte_diagnostic_set_subject(
            diagnostic, input->symbol, input->expected_type, input->actual_type);
        vitte_diagnostic_set_expected_found(
            diagnostic, input->expected, input->found);
        vitte_diagnostic_set_probable_cause(
            diagnostic, input->probable_cause);
        if (input->has_contract) {
            const vitte_cli_diagnostic_contract_t *contract = &input->contract;
            const vitte_cli_diagnostic_span_t *input_spans[3] = {
                &contract->contract_span, &contract->call_span,
                &contract->declaration_span
            };
            bool present[3] = {
                contract->has_contract_span, contract->has_call_span,
                contract->has_declaration_span
            };
            vitte_ast_span_t contract_spans[3];
            size_t span_index;
            if (contract->kind < VITTE_CLI_DIAGNOSTIC_CONTRACT_REQUIRES ||
                contract->kind > VITTE_CLI_DIAGNOSTIC_CONTRACT_ASSERTION) {
                goto cleanup;
            }
            memset(contract_spans, 0, sizeof(contract_spans));
            for (span_index = 0u; span_index < 3u; ++span_index) {
                const vitte_cli_diagnostic_span_t *from = input_spans[span_index];
                vitte_ast_span_t *to = &contract_spans[span_index];
                if (!present[span_index]) continue;
                if (!from->valid || from->start_offset > from->end_offset ||
                    from->start_line > UINT_MAX || from->start_column > UINT_MAX ||
                    from->end_line > UINT_MAX || from->end_column > UINT_MAX) {
                    goto cleanup;
                }
                to->file_id = from->file_id;
                to->source_id = from->source_id;
                to->source_name = from->source_name;
                to->start_offset = from->start_offset;
                to->end_offset = from->end_offset;
                to->start_line = (unsigned int)from->start_line;
                to->start_column = (unsigned int)from->start_column;
                to->end_line = (unsigned int)from->end_line;
                to->end_column = (unsigned int)from->end_column;
                to->valid = true;
            }
            status = vitte_diagnostic_set_contract(
                diagnostic, (vitte_diagnostic_contract_kind_t)contract->kind,
                contract->expression, contract->procedure, contract->reason,
                present[0] ? &contract_spans[0] : NULL,
                present[1] ? &contract_spans[1] : NULL,
                present[2] ? &contract_spans[2] : NULL);
            if (status != VITTE_STATUS_OK) goto cleanup;
        }
        {
            char help[VITTE_DIAGNOSTIC_MESSAGE_CAPACITY];
            if (vitte_terminal_text_present(input->help)) {
                status = vitte_diagnostic_add_help(diagnostic, input->help, span_pointer);
            } else if (vitte_diagnostic_format_help(diagnostic, sources, help, sizeof(help))) {
                status = vitte_diagnostic_add_help(diagnostic, help, span_pointer);
            }
            if (status != VITTE_STATUS_OK) goto cleanup;
        }

        if (input->related_count != 0u) {
            size_t related_index;

            if (input->related == NULL) {
                goto cleanup;
            }

            for (related_index = 0u;
                 related_index < input->related_count;
                 ++related_index) {
                const vitte_cli_diagnostic_location_t *input_location;
                vitte_ast_span_t related_span;

                input_location = &input->related[related_index];
                if (input_location->kind <
                        VITTE_CLI_DIAGNOSTIC_LOCATION_PRIMARY ||
                    input_location->kind >
                        VITTE_CLI_DIAGNOSTIC_LOCATION_RELATED ||
                    !input_location->span.valid ||
                    input_location->span.start_line > UINT_MAX ||
                    input_location->span.start_column > UINT_MAX ||
                    input_location->span.end_line > UINT_MAX ||
                    input_location->span.end_column > UINT_MAX) {
                    goto cleanup;
                }

                memset(&related_span, 0, sizeof(related_span));
                related_span.file_id = input_location->span.file_id;
                related_span.source_id =
                    input_location->span.source_id !=
                            VITTE_SOURCE_INVALID_ID
                        ? input_location->span.source_id
                        : source_id;
                related_span.source_name =
                    input_location->span.source_name != NULL
                        ? input_location->span.source_name
                        : source_name;
                related_span.start_offset =
                    input_location->span.start_offset;
                related_span.end_offset = input_location->span.end_offset;
                related_span.start_line =
                    (unsigned int)input_location->span.start_line;
                related_span.start_column =
                    (unsigned int)input_location->span.start_column;
                related_span.end_line =
                    (unsigned int)input_location->span.end_line;
                related_span.end_column =
                    (unsigned int)input_location->span.end_column;
                related_span.valid = true;
                status = vitte_diagnostic_add_location(
                    diagnostic,
                    (vitte_diagnostic_location_kind_t)input_location->kind,
                    &related_span,
                    input_location->label);
                if (status != VITTE_STATUS_OK) {
                    goto cleanup;
                }
            }
        }

        if (input->annotations != NULL) {
            size_t annotation_index;

            if (input->annotation_count > VITTE_CLI_DIAGNOSTIC_MAX_ANNOTATIONS) {
                goto cleanup;
            }
            for (annotation_index = 0u;
                 annotation_index < input->annotation_count;
                 ++annotation_index) {
                const vitte_cli_diagnostic_annotation_t *annotation =
                    &input->annotations[annotation_index];
                vitte_ast_span_t annotation_span;
                const vitte_ast_span_t *annotation_span_pointer = NULL;

                if (annotation->has_span) {
                    if (!annotation->span.valid ||
                        annotation->span.start_line > UINT_MAX ||
                        annotation->span.start_column > UINT_MAX ||
                        annotation->span.end_line > UINT_MAX ||
                        annotation->span.end_column > UINT_MAX) {
                        goto cleanup;
                    }
                    memset(&annotation_span, 0, sizeof(annotation_span));
                    annotation_span.file_id = annotation->span.file_id;
                    annotation_span.source_id = annotation->span.source_id !=
                        VITTE_SOURCE_INVALID_ID ? annotation->span.source_id : source_id;
                    annotation_span.source_name = annotation->span.source_name != NULL
                        ? annotation->span.source_name : source_name;
                    annotation_span.start_offset = annotation->span.start_offset;
                    annotation_span.end_offset = annotation->span.end_offset;
                    annotation_span.start_line = (unsigned int)annotation->span.start_line;
                    annotation_span.start_column = (unsigned int)annotation->span.start_column;
                    annotation_span.end_line = (unsigned int)annotation->span.end_line;
                    annotation_span.end_column = (unsigned int)annotation->span.end_column;
                    annotation_span.valid = true;
                    annotation_span_pointer = &annotation_span;
                }
                status = vitte_diagnostic_add_annotation(
                    diagnostic,
                    (vitte_diagnostic_annotation_kind_t)annotation->kind,
                    annotation->message,
                    annotation_span_pointer);
                if (status != VITTE_STATUS_OK) goto cleanup;
            }
        }

        if (input->causes != NULL) {
            size_t cause_index;

            if (input->cause_count > VITTE_CLI_DIAGNOSTIC_MAX_CAUSES) {
                goto cleanup;
            }
            for (cause_index = 0u;
                 cause_index < input->cause_count;
                 ++cause_index) {
                const vitte_cli_diagnostic_cause_t *input_cause =
                    &input->causes[cause_index];
                vitte_ast_span_t cause_span;
                const vitte_ast_span_t *cause_span_pointer = NULL;

                if (input_cause->kind > VITTE_CLI_DIAGNOSTIC_CAUSE_INTERNAL ||
                    input_cause->confidence > VITTE_CLI_DIAGNOSTIC_CONFIDENCE_CERTAIN) {
                    goto cleanup;
                }
                if (input_cause->has_span) {
                    if (!input_cause->span.valid ||
                        input_cause->span.start_line > UINT_MAX ||
                        input_cause->span.start_column > UINT_MAX ||
                        input_cause->span.end_line > UINT_MAX ||
                        input_cause->span.end_column > UINT_MAX) {
                        goto cleanup;
                    }
                    memset(&cause_span, 0, sizeof(cause_span));
                    cause_span.file_id = input_cause->span.file_id;
                    cause_span.source_id = input_cause->span.source_id !=
                        VITTE_SOURCE_INVALID_ID ? input_cause->span.source_id : source_id;
                    cause_span.source_name = input_cause->span.source_name != NULL
                        ? input_cause->span.source_name : source_name;
                    cause_span.start_offset = input_cause->span.start_offset;
                    cause_span.end_offset = input_cause->span.end_offset;
                    cause_span.start_line = (unsigned int)input_cause->span.start_line;
                    cause_span.start_column = (unsigned int)input_cause->span.start_column;
                    cause_span.end_line = (unsigned int)input_cause->span.end_line;
                    cause_span.end_column = (unsigned int)input_cause->span.end_column;
                    cause_span.valid = true;
                    cause_span_pointer = &cause_span;
                }
                status = vitte_diagnostic_add_cause_ex(
                    diagnostic,
                    (vitte_diagnostic_cause_kind_t)input_cause->kind,
                    input_cause->message,
                    cause_span_pointer,
                    (vitte_diagnostic_confidence_t)input_cause->confidence);
                if (status != VITTE_STATUS_OK) goto cleanup;
            }
        }

        if (input->trace != NULL) {
            size_t trace_index;

            if (input->trace_count > VITTE_CLI_DIAGNOSTIC_MAX_TRACE) {
                goto cleanup;
            }
            for (trace_index = 0u;
                 trace_index < input->trace_count;
                 ++trace_index) {
                const vitte_cli_diagnostic_trace_t *input_trace =
                    &input->trace[trace_index];
                vitte_ast_span_t trace_span;
                const vitte_ast_span_t *trace_span_pointer = NULL;

                if (input_trace->kind > VITTE_CLI_DIAGNOSTIC_TRACE_RELATED) {
                    goto cleanup;
                }
                if (input_trace->has_span) {
                    if (!input_trace->span.valid ||
                        input_trace->span.start_line > UINT_MAX ||
                        input_trace->span.start_column > UINT_MAX ||
                        input_trace->span.end_line > UINT_MAX ||
                        input_trace->span.end_column > UINT_MAX) {
                        goto cleanup;
                    }
                    memset(&trace_span, 0, sizeof(trace_span));
                    trace_span.file_id = input_trace->span.file_id;
                    trace_span.source_id = input_trace->span.source_id !=
                        VITTE_SOURCE_INVALID_ID ? input_trace->span.source_id : source_id;
                    trace_span.source_name = input_trace->span.source_name != NULL
                        ? input_trace->span.source_name : source_name;
                    trace_span.start_offset = input_trace->span.start_offset;
                    trace_span.end_offset = input_trace->span.end_offset;
                    trace_span.start_line = (unsigned int)input_trace->span.start_line;
                    trace_span.start_column = (unsigned int)input_trace->span.start_column;
                    trace_span.end_line = (unsigned int)input_trace->span.end_line;
                    trace_span.end_column = (unsigned int)input_trace->span.end_column;
                    trace_span.valid = true;
                    trace_span_pointer = &trace_span;
                }
                status = vitte_diagnostic_add_trace(
                    diagnostic,
                    (vitte_diagnostic_trace_kind_t)input_trace->kind,
                    input_trace->message,
                    trace_span_pointer);
                if (status != VITTE_STATUS_OK) goto cleanup;
            }
        }

        if (input->suggestion_count != 0u) {
            size_t suggestion_index;

            if (input->suggestions == NULL) {
                goto cleanup;
            }

            for (suggestion_index = 0u;
                 suggestion_index < input->suggestion_count;
                 ++suggestion_index) {
                const vitte_cli_diagnostic_suggestion_t *input_suggestion;
                vitte_ast_span_t suggestion_span;
                const vitte_ast_span_t *suggestion_span_pointer;
                vitte_diagnostic_suggestion_t *stored_suggestion;
                size_t previous_suggestion_count;

                input_suggestion = &input->suggestions[suggestion_index];
                suggestion_span_pointer = NULL;
                if (input_suggestion->has_span) {
                    if (!input_suggestion->span.valid ||
                        input_suggestion->span.start_line > UINT_MAX ||
                        input_suggestion->span.start_column > UINT_MAX ||
                        input_suggestion->span.end_line > UINT_MAX ||
                        input_suggestion->span.end_column > UINT_MAX) {
                        goto cleanup;
                    }

                    memset(&suggestion_span, 0, sizeof(suggestion_span));
                    suggestion_span.file_id = input_suggestion->span.file_id;
                    suggestion_span.source_id = input_suggestion->span.source_id !=
                        VITTE_SOURCE_INVALID_ID ? input_suggestion->span.source_id : source_id;
                    suggestion_span.source_name = input_suggestion->span.source_name != NULL
                        ? input_suggestion->span.source_name : source_name;
                    suggestion_span.start_offset =
                        input_suggestion->span.start_offset;
                    suggestion_span.end_offset =
                        input_suggestion->span.end_offset;
                    suggestion_span.start_line =
                        (unsigned int)input_suggestion->span.start_line;
                    suggestion_span.start_column =
                        (unsigned int)input_suggestion->span.start_column;
                    suggestion_span.end_line =
                        (unsigned int)input_suggestion->span.end_line;
                    suggestion_span.end_column =
                        (unsigned int)input_suggestion->span.end_column;
                    suggestion_span.valid = true;
                    suggestion_span_pointer = &suggestion_span;
                }

                if (input_suggestion->applicability <
                        VITTE_CLI_DIAGNOSTIC_APPLICABILITY_MACHINE ||
                    input_suggestion->applicability >
                        VITTE_CLI_DIAGNOSTIC_APPLICABILITY_MANUAL ||
                    input_suggestion->confidence <
                        VITTE_CLI_DIAGNOSTIC_CONFIDENCE_NONE ||
                    input_suggestion->confidence >
                        VITTE_CLI_DIAGNOSTIC_CONFIDENCE_CERTAIN) {
                    goto cleanup;
                }

                previous_suggestion_count = diagnostic->suggestion_count;
                status = vitte_diagnostic_add_suggestion(
                    diagnostic,
                    input_suggestion->message,
                    suggestion_span_pointer,
                    input_suggestion->has_replacement
                        ? input_suggestion->replacement
                        : NULL,
                    (vitte_diagnostic_applicability_t)
                        input_suggestion->applicability);
                if (status != VITTE_STATUS_OK) {
                    goto cleanup;
                }

                if (diagnostic->suggestion_count > previous_suggestion_count) {
                    stored_suggestion = &diagnostic->suggestions[
                        diagnostic->suggestion_count - 1u];
                    stored_suggestion->confidence =
                        (vitte_diagnostic_confidence_t)
                            input_suggestion->confidence;
                    stored_suggestion->preferred =
                        input_suggestion->preferred;
                    if (input_suggestion->suggestion_id <= UINT32_MAX) {
                        stored_suggestion->suggestion_id =
                            (uint32_t)input_suggestion->suggestion_id;
                    }
                }
            }
        }
        diagnostic->has_machine_fix = input->has_machine_fix;
        diagnostic->diagnostic_id = input->diagnostic_id;
        diagnostic->fingerprint = input->fingerprint;
        diagnostic->parent_diagnostic = input->parent_diagnostic;
        diagnostic->root_diagnostic = input->root_diagnostic;
        diagnostic->parent_id = input->parent_id;
        diagnostic->root_id = input->root_id;
        diagnostic->provenance_id = input->provenance_id;
        diagnostic->poison_id = input->poison_id;
        diagnostic->is_primary = input->is_primary;
        diagnostic->is_cascade = input->is_cascade;
        diagnostic->is_suppressed = input->is_suppressed;
        diagnostic->poisons_analysis = input->poisons_analysis;
        diagnostic->is_internal = input->is_internal;
        if (input->fingerprint == 0u) {
            vitte_diagnostic_refresh_fingerprint(diagnostic);
        }
    }

    rendered = true;
    if (format == VITTE_CLI_DIAGNOSTIC_JSON) {
        vitte_diagnostic_json_options_t json_options;

        vitte_diagnostic_json_options_init(&json_options);
        json_options.pretty = true;
        status = vitte_diagnostic_render_json_bag(
            stream,
            &bag,
            &json_options);
        rendered = status == VITTE_STATUS_OK;
    } else if (format == VITTE_CLI_DIAGNOSTIC_SARIF) {
        vitte_diagnostic_sarif_options_t sarif_options;

        vitte_diagnostic_sarif_options_init(&sarif_options);
        sarif_options.tool_version = vitte_version_string();
        status = vitte_diagnostic_render_sarif(
            stream,
            &bag,
            &sarif_options);
        rendered = status == VITTE_STATUS_OK;
    } else if (format == VITTE_CLI_DIAGNOSTIC_LSP) {
        vitte_diagnostic_lsp_options_t lsp_options;

        vitte_diagnostic_lsp_options_init(&lsp_options);
        lsp_options.pretty = effective_options.pretty;
        lsp_options.trailing_newline = effective_options.trailing_newline;
        lsp_options.include_suppressed = effective_options.show_suppressed;
        lsp_options.include_related_information = effective_options.show_related;
        lsp_options.include_data = true;
        lsp_options.include_labels_in_data = effective_options.show_labels;
        lsp_options.include_suggestions_in_data = effective_options.show_suggestions;
        lsp_options.include_causes_in_data = effective_options.show_causes;
        lsp_options.include_contract_in_data = true;
        lsp_options.include_cascade_in_data = effective_options.show_cascades;
        lsp_options.position_encoding =
            (vitte_diagnostic_lsp_position_encoding_t)
                effective_options.position_encoding;
        lsp_options.position_converter =
            (vitte_diagnostic_lsp_position_converter_t)
                effective_options.position_converter;
        lsp_options.position_user_data = effective_options.position_user_data;
        if (lsp_options.position_converter == NULL &&
            sources != NULL) {
            lsp_options.position_converter =
                vitte_diagnostic_default_lsp_position;
            lsp_options.position_user_data = (void *)sources;
        }
        if (lsp_options.position_converter == NULL &&
            lsp_fallback_available) {
            lsp_options.position_converter =
                vitte_diagnostic_cli_lsp_fallback_position;
            lsp_options.position_user_data = &lsp_fallback_context;
        }
        lsp_options.uri_converter =
            (vitte_diagnostic_lsp_uri_converter_t)
                effective_options.uri_converter;
        lsp_options.uri_user_data = effective_options.uri_user_data;
        if (lsp_options.position_converter == NULL) {
            rendered = false;
        } else {
            status = vitte_diagnostic_render_lsp_array(
                stream,
                &bag,
                NULL,
                &lsp_options);
            rendered = status == VITTE_STATUS_OK;
        }
    } else {
        vitte_diagnostic_terminal_options_init(&terminal_options);
        terminal_options.color = color;
        terminal_options.sources = sources;

        for (index = 0u; index < bag.count; ++index) {
            const vitte_diagnostic_t *diagnostic;

            diagnostic = vitte_diagnostic_at(&bag, index);
            if (diagnostic == NULL ||
                vitte_diagnostic_render_terminal_one(
                    stream,
                    diagnostic,
                    &terminal_options) != VITTE_STATUS_OK) {
                rendered = false;
                break;
            }
        }
    }

cleanup:
    vitte_diagnostic_bag_reset(&bag);
    free(storage);
    return rendered;
}

bool
vitte_diagnostic_render_cli_batch(
    FILE *stream,
    const vitte_source_context_t *sources,
    vitte_source_id_t source_id,
    const char *source_name,
    const vitte_cli_diagnostic_t *diagnostics,
    size_t diagnostic_count,
    bool color,
    vitte_cli_diagnostic_format_t format)
{
    vitte_cli_diagnostic_render_options_t options;

    vitte_cli_diagnostic_render_options_init(&options);
    options.color = color;
    options.format = format;
    return vitte_diagnostic_render_cli_batch_ex(
        stream,
        sources,
        source_id,
        source_name,
        diagnostics,
        diagnostic_count,
        &options);
}

bool
vitte_diagnostic_render_cli_diagnostic(
    FILE *stream,
    const vitte_source_context_t *sources,
    vitte_source_id_t source_id,
    const char *source_name,
    const vitte_cli_diagnostic_t *diagnostic,
    const vitte_cli_diagnostic_render_options_t *options)
{
    return diagnostic != NULL &&
        vitte_diagnostic_render_cli_batch_ex(
            stream,
            sources,
            source_id,
            source_name,
            diagnostic,
            1u,
            options);
}

bool
vitte_diagnostic_render_cli(
    FILE *stream,
    const vitte_source_context_t *sources,
    vitte_source_id_t source_id,
    const char *source_name,
    vitte_cli_diagnostic_severity_t severity,
    const char *code,
    const char *message,
    const char *details,
    bool has_span,
    uint32_t file_id,
    size_t start_offset,
    size_t end_offset,
    size_t start_line,
    size_t start_column,
    size_t end_line,
    size_t end_column,
    const char *procedure,
    const char *context,
    bool color,
    vitte_cli_diagnostic_format_t format)
{
    vitte_cli_diagnostic_t diagnostic;

    memset(&diagnostic, 0, sizeof(diagnostic));
    diagnostic.severity = severity;
    diagnostic.code = code;
    diagnostic.message = message;
    diagnostic.details = details;
    diagnostic.has_span = has_span;
    diagnostic.file_id = file_id;
    diagnostic.start_offset = start_offset;
    diagnostic.end_offset = end_offset;
    diagnostic.start_line = start_line;
    diagnostic.start_column = start_column;
    diagnostic.end_line = end_line;
    diagnostic.end_column = end_column;
    diagnostic.procedure = procedure;
    diagnostic.context = context;

    return vitte_diagnostic_render_cli_batch(
        stream,
        sources,
        source_id,
        source_name,
        &diagnostic,
        1u,
        color,
        format);
}
