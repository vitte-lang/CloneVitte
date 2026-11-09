#include "render_terminal.h"
#include "cli_bridge.h"

#include "diagnostic.h"
#include "label.h"
#include "registry.h"
#include "render_json.h"
#include "render_sarif.h"
#include "suggestion.h"

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

/* ========================================================================= */
/* Internal types                                                            */
/* ========================================================================= */

typedef struct vitte_terminal_source_line {
    const char *text;
    size_t length;
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
    size_t index;
    size_t visual;

    if (text == NULL) {
        return byte_column;
    }

    index = 0u;
    visual = 0u;

    while (index < length &&
           index < byte_column) {

        unsigned char character;

        character =
            (unsigned char)text[index];

        if (character == '\t') {
            visual +=
                vitte_terminal_tab_advance(
                    visual,
                    tab_width
                );

            index++;
            continue;
        }

        /*
         * UTF-8 continuation bytes do not start another source character.
         *
         * This intentionally avoids treating every byte as a display cell.
         *
         * Full wcwidth/grapheme handling belongs in the source/display layer.
         */
        if ((character & 0xC0u) ==
            0x80u) {

            index++;
            continue;
        }

        visual++;
        index++;
    }

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

        (void)fputc(
            (int)character,
            stream
        );

        /*
         * Approximate one display cell per UTF-8 leading byte.
         *
         * Exact terminal width for combining characters, CJK and emoji
         * should eventually come from a dedicated Unicode display-width API.
         */
        if ((character & 0xC0u) !=
            0x80u) {

            visual++;
        }

        index++;
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
            span->start_column,
            options->tab_width
        );

    visual_end =
        vitte_terminal_visual_column(
            source_line.text,
            source_line.length,
            span->end_column,
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
    bool rendered;
    const char *color;

    if (span == NULL ||
        !span->valid ||
        span->end_line <=
            span->start_line) {

        return false;
    }

    last_line =
        span->end_line;

    gutter_width =
        vitte_terminal_digits(
            last_line + 1u
        );

    color =
        primary
            ? VITTE_TERM_BRIGHT_RED
            : VITTE_TERM_BRIGHT_CYAN;

    rendered = false;

    vitte_terminal_render_empty_gutter(
        stream,
        gutter_width,
        options
    );

    for (line_index = span->start_line;
         line_index <= last_line;
         line_index++) {

        vitte_terminal_source_line_t source_line;

        if (!vitte_terminal_source_line_lookup(
                options,
                span->source_id,
                line_index,
                &source_line
            )) {

            continue;
        }

        if (!source_line.valid ||
            source_line.text == NULL) {

            continue;
        }

        rendered = true;

        vitte_terminal_render_source_gutter(
            stream,
            gutter_width,
            line_index + 1u,
            options
        );

        vitte_terminal_style(
            stream,
            options,
            color
        );

        if (line_index ==
            span->start_line) {

            (void)fputs(
                "/ ",
                stream
            );
        } else if (line_index ==
                   last_line) {

            (void)fputs(
                "\\ ",
                stream
            );
        } else {
            (void)fputs(
                "| ",
                stream
            );
        }

        vitte_terminal_reset(
            stream,
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
    }

    if (rendered &&
        vitte_terminal_text_present(
            message
        )) {

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
            "| ",
            stream
        );

        vitte_terminal_reset(
            stream,
            options
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

        (void)fputc(
            '\n',
            stream
        );
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

static const char *
vitte_terminal_registry_help(
    const vitte_diagnostic_t *diagnostic)
{
    const vitte_diagnostic_registry_entry_t *entry;

    if (diagnostic == NULL ||
        diagnostic->code == NULL) {
        return "Inspect the highlighted source and rerun the compiler.";
    }

    entry = vitte_diagnostic_registry_find_internal(
        diagnostic->code);
    if (entry == NULL) {
        return "Inspect the highlighted source and rerun the compiler.";
    }

    if (strcmp(entry->internal_code, "VITTE_LEXER_E_UNTERMINATED_STRING") == 0) {
        return "Close the string with a matching quote or escape the quote inside it.";
    }
    if (strcmp(entry->internal_code, "VITTE_LEXER_E_INVALID_ESCAPE") == 0) {
        return "Replace the escape with a supported escape sequence or escape the backslash.";
    }
    if (strcmp(entry->internal_code, "VITTE_LEXER_E_INVALID_NUMBER") == 0 ||
        strcmp(entry->internal_code, "VITTE_LEXER_E_NUMBER_OVERFLOW") == 0) {
        return "Check the literal's digits, base prefix and suffix; keep its value within the target type's range.";
    }
    if (strcmp(entry->internal_code, "VITTE_INFRA_E_PHASE") == 0) {
        return "Check the earlier diagnostic or failed phase; the first reported failure usually identifies the root cause.";
    }
    if (strcmp(entry->internal_code, "VITTE_INFRA_E_CONFIG") == 0) {
        return "Check the option name, value and combination against `vitte --help`.";
    }
    if (strcmp(entry->internal_code, "VITTE_INFRA_E_SOURCE_READ") == 0) {
        return "Check that the path exists, is readable, and names a regular source file.";
    }
    if (strcmp(entry->internal_code, "VITTE_INFRA_E_ENCODING") == 0) {
        return "Save the source as valid UTF-8 and remove or replace the invalid byte sequence.";
    }
    if (strcmp(entry->internal_code, "VITTE_INFRA_E_SOURCE_MAP") == 0) {
        return "Check the source registration and its path; if this repeats, report it as a compiler issue.";
    }
    if (strcmp(entry->internal_code, "VITTE_PARSER_E_EXPECTED_EXPRESSION") == 0) {
        return "Add a valid expression at the highlighted position; the token shown after the underline is not an expression.";
    }
    if (strcmp(entry->internal_code, "VITTE_PARSER_E_EXPECTED_TOKEN") == 0) {
        return "Insert the expected delimiter or keyword before the highlighted token.";
    }
    if (strcmp(entry->internal_code, "VITTE_PARSER_E_EXPECTED_TYPE") == 0) {
        return "Add a type name or type expression where the declaration requires one.";
    }
    if (strcmp(entry->internal_code, "VITTE_PARSER_E_UNEXPECTED_TOKEN") == 0) {
        return "Remove the unexpected token or move it to the syntactic position required by the surrounding construct.";
    }
    if (strcmp(entry->internal_code, "VITTE_RESOLVE_E_UNKNOWN_SYMBOL") == 0) {
        return "Check the spelling and scope of this name, and import or declare it before use.";
    }
    if (strcmp(entry->internal_code, "VITTE_RESOLVE_E_REDECLARATION") == 0 ||
        strcmp(entry->internal_code, "VITTE_RESOLVE_E_DUPLICATE_BINDING") == 0) {
        return "Rename one declaration or remove the duplicate binding in this scope.";
    }
    if (strcmp(entry->internal_code, "VITTE_RESOLVE_E_AMBIGUOUS_SYMBOL") == 0) {
        return "Qualify the name with its module or otherwise make the intended declaration unambiguous.";
    }
    if (strcmp(entry->internal_code, "VITTE_RESOLVE_E_PRIVATE") == 0) {
        return "Use a public API or change the declaration's visibility if this access is intended.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_MISMATCH") == 0 ||
        strcmp(entry->internal_code, "VITTE_CALL_E_ARGUMENT_TYPE") == 0 ||
        strcmp(entry->internal_code, "VITTE_CALL_E_RETURN_TYPE") == 0) {
        return "Make the expression's type match the required type; use an explicit conversion only when it preserves the intended value.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_CANNOT_INFER") == 0) {
        return "Add a type annotation or provide a value that determines one unambiguous type.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_UNKNOWN_TYPE") == 0) {
        return "Check the type name's spelling and import or declare that type in the current scope.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_NOT_A_TYPE") == 0) {
        return "Use a declared type name here instead of a value, procedure or module name.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_INVALID_OPERAND") == 0) {
        return "Use an operator supported by the operand types, or convert the operands to compatible types.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_NOT_ASSIGNABLE") == 0) {
        return "Assign to a mutable variable, field or index rather than a temporary or immutable value.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_INVALID_INDEX") == 0) {
        return "Use an integer index within the collection's valid bounds.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_CONDITION_NOT_BOOL") == 0) {
        return "Make the condition evaluate to `bool`; compare values explicitly instead of relying on truthiness.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_INVALID_CAST") == 0) {
        return "Choose a conversion supported by both types and verify that it preserves the value and range.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_NOT_CALLABLE") == 0) {
        return "Call a procedure/function value; if this name should be callable, check its declaration and imports.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_NOT_INDEXABLE") == 0) {
        return "Index an array, slice or other supported collection instead of this value.";
    }
    if (strcmp(entry->internal_code, "VITTE_TYPE_E_NO_MEMBER") == 0) {
        return "Check the member's spelling and the receiver's type, or use a member provided by that type.";
    }
    if (strcmp(entry->internal_code, "VITTE_CALL_E_ARITY") == 0) {
        return "Pass exactly the required arguments, in the order declared by the selected procedure.";
    }
    if (strcmp(entry->internal_code, "VITTE_CALL_E_NO_CANDIDATE") == 0) {
        return "Check the argument count and types, then select or import a procedure whose signature accepts them.";
    }
    if (strcmp(entry->internal_code, "VITTE_CALL_E_AMBIGUOUS") == 0) {
        return "Disambiguate the call by qualifying the procedure or changing the argument types to select one candidate.";
    }
    if (strcmp(entry->internal_code, "VITTE_CONTRACT_E_PRECONDITION") == 0 ||
        strcmp(entry->internal_code, "VITTE_CONTRACT_E_REQUIRES") == 0) {
        return "Satisfy the procedure's requires condition before calling it; check the argument values and caller invariants.";
    }
    if (strcmp(entry->internal_code, "VITTE_CONTRACT_E_POSTCONDITION") == 0 ||
        strcmp(entry->internal_code, "VITTE_CONTRACT_E_ENSURES") == 0) {
        return "Update the procedure so every successful return satisfies its ensures condition.";
    }
    if (strcmp(entry->internal_code, "VITTE_CONTRACT_E_INVARIANT") == 0) {
        return "Restore the invariant before leaving the operation and preserve it across every state transition.";
    }
    if (strcmp(entry->internal_code, "VITTE_CONTRACT_E_UNPROVABLE") == 0) {
        return "Provide enough preconditions or simpler assertions for the checker to establish this contract.";
    }
    if (strcmp(entry->internal_code, "VITTE_CONTRACT_E_CONTRADICTION") == 0) {
        return "Review the contract clauses for mutually incompatible conditions.";
    }
    if (strcmp(entry->internal_code, "VITTE_CONTROL_E_INVALID_BREAK") == 0 ||
        strcmp(entry->internal_code, "VITTE_CONTROL_E_INVALID_CONTINUE") == 0) {
        return "Move this statement inside a loop or replace it with control flow valid at this location.";
    }
    if (strcmp(entry->internal_code, "VITTE_CONTROL_E_MISSING_RETURN") == 0) {
        return "Return a value of the declared type on every reachable path through this procedure.";
    }
    if (strcmp(entry->internal_code, "VITTE_WARNING_UNUSED_VARIABLE") == 0 ||
        strcmp(entry->internal_code, "VITTE_WARNING_UNUSED_PARAMETER") == 0 ||
        strcmp(entry->internal_code, "VITTE_WARNING_UNUSED_IMPORT") == 0) {
        return "Remove the unused declaration/import, or use it if it was omitted accidentally.";
    }
    if (strcmp(entry->internal_code, "VITTE_WARNING_DEPRECATED") == 0) {
        return "Replace this deprecated API with its documented successor before it is removed.";
    }
    if (strcmp(entry->internal_code, "VITTE_WARNING_TRUNCATION") == 0 ||
        strcmp(entry->internal_code, "VITTE_WARNING_SIGN_CHANGE") == 0) {
        return "Verify the value range and signedness before converting; use a wider or matching type if needed.";
    }
    if (strcmp(entry->internal_code, "VITTE_WARNING_REDUNDANT_CAST") == 0) {
        return "Remove the cast unless it documents an intentional type boundary.";
    }
    if (strcmp(entry->internal_code, "VITTE_WARNING_UNREACHABLE_CODE") == 0) {
        return "Remove the unreachable statements or correct the preceding control-flow condition.";
    }
    if (strcmp(entry->category, "syntax") == 0) {
        return "Compare the highlighted token with the surrounding construct and add, remove or reorder the required syntax.";
    }
    if (strcmp(entry->category, "type") == 0 ||
        strcmp(entry->category, "call") == 0 ||
        strcmp(entry->category, "procedure") == 0) {
        return "Check the highlighted expression against the required type or signature; `vitte explain` shows diagnostic-specific guidance.";
    }
    if (strcmp(entry->category, "name") == 0 ||
        strcmp(entry->category, "name-resolution") == 0 ||
        strcmp(entry->category, "scope") == 0 ||
        strcmp(entry->category, "visibility") == 0) {
        return "Check the declaration, spelling, scope and imports of the highlighted name; `vitte explain` shows diagnostic-specific guidance.";
    }
    if (strcmp(entry->category, "contract") == 0) {
        return "Review the contract condition and the state or values at the highlighted location.";
    }
    if (entry->origin == VITTE_DIAGNOSTIC_ORIGIN_IMPORT ||
        strcmp(entry->category, "module") == 0 ||
        strcmp(entry->category, "package") == 0) {
        return "Check the module path, exported name, package version and visibility from this import site.";
    }
    if (strcmp(entry->category, "memory") == 0 ||
        strcmp(entry->category, "reference") == 0 ||
        strcmp(entry->category, "pointer") == 0) {
        return "Check ownership, lifetime, mutability and bounds at the highlighted use.";
    }
    if (strcmp(entry->category, "unsafe") == 0) {
        return "Verify the safety invariants manually and keep the unsafe operation inside the smallest justified scope.";
    }
    if (strcmp(entry->category, "ffi") == 0 ||
        strcmp(entry->category, "abi") == 0) {
        return "Compare the Vitte declaration with the foreign function's exact ABI, layout and calling convention.";
    }
    if (strcmp(entry->category, "target") == 0 ||
        entry->origin == VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND ||
        entry->origin == VITTE_DIAGNOSTIC_ORIGIN_C_COMPILER) {
        return "Check target support and the reported backend/compiler detail; reduce the source to a reproducible case if this looks like a compiler defect.";
    }
    if (entry->category != NULL &&
        (strcmp(entry->category, "constant") == 0 ||
         strcmp(entry->category, "constant-evaluation") == 0)) {
        return "Use only values and operations permitted in compile-time expressions, or move the computation to run time.";
    }
    if (entry->category != NULL &&
        (strcmp(entry->category, "pattern") == 0 ||
         strcmp(entry->category, "match") == 0)) {
        return "Check the pattern's type and ensure the alternatives are valid and cover the intended cases.";
    }
    if (entry->category != NULL &&
        (strcmp(entry->category, "async") == 0 ||
         strcmp(entry->category, "concurrency") == 0)) {
        return "Check the async context, suspension points and ownership of values shared across tasks.";
    }
    if (entry->default_severity == VITTE_DIAGNOSTIC_WARNING) {
        return "Review the highlighted construct and correct the risky behavior or document why it is intentional.";
    }

    return "Review the highlighted source and the compiler phase shown above.";
}

static bool
vitte_terminal_has_help_annotation(
    const vitte_diagnostic_t *diagnostic)
{
    size_t index;

    if (diagnostic == NULL) {
        return false;
    }

    for (index = 0u;
         index < diagnostic->annotation_count;
         ++index) {
        if (diagnostic->annotations[index].kind ==
            VITTE_DIAGNOSTIC_ANNOTATION_HELP) {
            return true;
        }
    }

    return false;
}

static void
vitte_terminal_render_registry_help(
    FILE *stream,
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_terminal_options_t *options)
{
    const char *help;

    if (diagnostic == NULL ||
        options == NULL ||
        diagnostic->severity < VITTE_DIAGNOSTIC_WARNING ||
        vitte_terminal_has_help_annotation(diagnostic)) {
        return;
    }

    help = vitte_terminal_registry_help(diagnostic);
    (void)fprintf(stream, "  help: %s", help);
    if (diagnostic->short_code != NULL &&
        diagnostic->short_code[0] != '\0') {
        (void)fprintf(
            stream,
            " For diagnostic details, run `vitte explain %s`.",
            diagnostic->short_code);
    }
    (void)fputc('\n', stream);
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
    if (!options->show_context) {
        return;
    }

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
    vitte_diagnostic_t *storage;
    vitte_diagnostic_bag_t bag;
    vitte_diagnostic_terminal_options_t options;
    vitte_status_t status;
    size_t index;
    size_t previous_count;
    bool rendered;

    if (stream == NULL ||
        diagnostics == NULL ||
        diagnostic_count == 0u ||
        diagnostic_count > SIZE_MAX / sizeof(*storage) ||
        format < VITTE_CLI_DIAGNOSTIC_TERMINAL ||
        format > VITTE_CLI_DIAGNOSTIC_SARIF) {
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

    for (index = 0u; index < diagnostic_count; ++index) {
        const vitte_cli_diagnostic_t *input;
        vitte_diagnostic_t *diagnostic;
        vitte_diagnostic_registry_entry_t const *entry;
        vitte_diagnostic_severity_t severity;
        vitte_ast_span_t span;
        const vitte_ast_span_t *span_pointer;
        const char *message;
        const char *details;

        input = &diagnostics[index];
        if (input->code == NULL ||
            input->end_offset < input->start_offset) {
            goto cleanup;
        }

        entry = vitte_diagnostic_registry_find_internal(input->code);
        message = vitte_terminal_text_present(input->message)
            ? input->message
            : entry != NULL ? entry->title : NULL;
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
            span.source_id = source_id;
            span.source_name = source_name;
            span.start_offset = input->start_offset;
            span.end_offset = input->end_offset;
            span.start_line = (unsigned int)input->start_line;
            span.start_column = (unsigned int)input->start_column;
            span.end_line = (unsigned int)input->end_line;
            span.end_column = (unsigned int)input->end_column;
            span.valid = true;
            span_pointer = &span;
        }

        previous_count = bag.count;
        status = vitte_diagnostic_emit(
            &bag,
            severity,
            entry != NULL
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

        diagnostic->severity = severity;
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

        vitte_diagnostic_set_context(
            diagnostic,
            NULL,
            NULL,
            input->procedure,
            input->context);

        if (vitte_terminal_text_present(
                vitte_terminal_registry_help(diagnostic)) &&
            vitte_diagnostic_add_help(
                diagnostic,
                vitte_terminal_registry_help(diagnostic),
                span_pointer) != VITTE_STATUS_OK) {
            goto cleanup;
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
        sarif_options.tool_version = "0.1.1";
        status = vitte_diagnostic_render_sarif(
            stream,
            &bag,
            &sarif_options);
        rendered = status == VITTE_STATUS_OK;
    } else {
        vitte_diagnostic_terminal_options_init(&options);
        options.color = color;
        options.sources = sources;

        for (index = 0u; index < bag.count; ++index) {
            const vitte_diagnostic_t *diagnostic;

            diagnostic = vitte_diagnostic_at(&bag, index);
            if (diagnostic == NULL ||
                vitte_diagnostic_render_terminal_one(
                    stream,
                    diagnostic,
                    &options) != VITTE_STATUS_OK) {
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
