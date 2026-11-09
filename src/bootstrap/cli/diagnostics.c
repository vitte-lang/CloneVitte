/*
 * Vitte Compiler
 * src/bootstrap/cli/diagnostics.c
 *
 * Private executable-facing implementation module.
 *
 * ISO C17.
 */

#include <stdio.h>
#include <string.h>
#include "internal.h"

_Thread_local vitte_main_diagnostic_format_t
    vitte_main_active_diagnostic_format =
        VITTE_MAIN_DIAGNOSTIC_TERMINAL;
_Thread_local bool vitte_main_process_diagnostic_emitted;

/* Process diagnostics                                                       */
/* ========================================================================= */

void
vitte_main_report_process_error(
    const char *code,
    const char *message,
    const char *details)
{
    vitte_main_process_diagnostic_emitted = true;
    if (!vitte_diagnostic_render_cli(
            stderr,
            NULL,
            VITTE_SOURCE_INVALID_ID,
            NULL,
            VITTE_CLI_DIAGNOSTIC_ERROR,
            code,
            message,
            details,
            false,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            NULL,
            "compiler driver",
            false,
            vitte_main_cli_diagnostic_format(
                vitte_main_active_diagnostic_format))) {
        (void)fprintf(
            stderr,
            "%s: error: %s\n",
            VITTE_COMPILER_NAME,
            message != NULL ? message : "compiler operation failed");
    }
}

void
vitte_main_report_process_error_span(
    const char *code,
    const char *message,
    const char *details,
    const char *source_name,
    const vitte_import_span_t *span,
    const char *diagnostic_context,
    const char *module_name,
    const char *symbol_name,
    const char *procedure_name)
{
    vitte_cli_diagnostic_t diagnostic;
    vitte_cli_diagnostic_render_options_t options;
    vitte_cli_diagnostic_format_t format;
    bool has_span;
    size_t start_line;
    size_t start_column;
    size_t end_line;
    size_t end_column;

    vitte_main_process_diagnostic_emitted = true;
    has_span = span != NULL && span->valid;
    start_line = has_span && span->line != 0u
        ? span->line - 1u
        : 0u;
    start_column = has_span && span->column != 0u
        ? span->column - 1u
        : 0u;
    end_line = has_span && span->end_line != 0u
        ? span->end_line - 1u
        : start_line;
    end_column = has_span && span->end_column != 0u
        ? span->end_column - 1u
        : start_column;

    format = vitte_main_cli_diagnostic_format(
        vitte_main_active_diagnostic_format);

    vitte_cli_diagnostic_init(&diagnostic);
    diagnostic.severity = VITTE_CLI_DIAGNOSTIC_ERROR;
    diagnostic.code = code;
    diagnostic.message = message;
    diagnostic.details = details;
    diagnostic.has_span = has_span;
    diagnostic.file_id = has_span ? span->file_id : 0u;
    diagnostic.source_id = VITTE_SOURCE_INVALID_ID;
    diagnostic.source_name = source_name;
    diagnostic.start_offset = has_span ? span->begin : 0u;
    diagnostic.end_offset = has_span ? span->end : 0u;
    diagnostic.start_line = start_line;
    diagnostic.start_column = start_column;
    diagnostic.end_line = end_line;
    diagnostic.end_column = end_column;
    diagnostic.module = module_name;
    diagnostic.symbol = symbol_name;
    diagnostic.procedure = procedure_name != NULL
        ? procedure_name
        : "";
    diagnostic.context = diagnostic_context != NULL
        ? diagnostic_context
        : "use declaration";

    vitte_cli_diagnostic_render_options_init(&options);
    options.format = format;
    options.color = false;

    if (!vitte_diagnostic_render_cli_diagnostic(
            stderr,
            NULL,
            VITTE_SOURCE_INVALID_ID,
            source_name,
            &diagnostic,
            &options)) {
        (void)fprintf(
            stderr,
            "%s: error: %s\n",
            VITTE_COMPILER_NAME,
            message != NULL ? message : "compiler operation failed");
    }
}

void
vitte_main_error(
    const char *message)
{
    if (message == NULL) {
        message = "unknown error";
    }

    vitte_main_report_process_error(
        "VITTE_INFRA_E_PHASE",
        message,
        NULL);
}

void
vitte_main_error_argument(
    const char *message,
    const char *argument)
{
    if (message == NULL) {
        message = "invalid argument";
    }

    if (argument == NULL) {
        vitte_main_error(message);
        return;
    }

    {
        char details[512];
        int length;

        length = snprintf(
            details,
            sizeof(details),
            "argument: '%s'",
            argument);
        if (length < 0 ||
            (size_t)length >= sizeof(details)) {
            vitte_main_report_process_error(
                "VITTE_INFRA_E_CONFIG",
                message,
                "The invalid argument could not be rendered in full.");
            return;
        }

        vitte_main_report_process_error(
            "VITTE_INFRA_E_CONFIG",
            message,
            details);
    }
}

void
vitte_main_report_path_error(
    const char *code,
    const char *message,
    const char *path,
    const char *reason)
{
    char details[1024];
    int length;

    if (path == NULL) {
        vitte_main_report_process_error(code, message, reason);
        return;
    }

    if (reason != NULL) {
        length = snprintf(
            details,
            sizeof(details),
            "path: '%s'; reason: %s",
            path,
            reason);
    } else {
        length = snprintf(
            details,
            sizeof(details),
            "path: '%s'",
            path);
    }

    if (length < 0 ||
        (size_t)length >= sizeof(details)) {
        vitte_main_report_process_error(
            code,
            message,
            "The path details could not be rendered in full.");
        return;
    }

    vitte_main_report_process_error(code, message, details);
}

void
vitte_main_report_process_warning(
    const char *message,
    const char *details)
{
    vitte_cli_diagnostic_format_t format;

    format = vitte_main_cli_diagnostic_format(
        vitte_main_active_diagnostic_format);

    if (!vitte_diagnostic_render_cli(
            stderr,
            NULL,
            VITTE_SOURCE_INVALID_ID,
            NULL,
            VITTE_CLI_DIAGNOSTIC_WARNING,
            "VITTE_WARNING_INFRASTRUCTURE",
            message,
            details,
            false,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            NULL,
            "compiler driver",
            false,
            format)) {
        (void)fprintf(
            stderr,
            "%s: warning: %s\n",
            VITTE_COMPILER_NAME,
            message != NULL ? message : "compiler warning");
    }
}

void
vitte_main_warning(
    const vitte_main_options_t *options,
    const char *message)
{
    if (options != NULL &&
        options->quiet) {
        return;
    }

    if (message == NULL) {
        message = "unknown warning";
    }

    if (vitte_main_active_diagnostic_format ==
        VITTE_MAIN_DIAGNOSTIC_TERMINAL) {
        vitte_main_report_process_warning(message, NULL);
    }
}

void
vitte_main_verbose(
    const vitte_main_options_t *options,
    const char *message)
{
    if (options == NULL ||
        !options->verbose ||
        options->quiet ||
        message == NULL) {
        return;
    }

    (void)fprintf(
        stderr,
        "%s: %s\n",
        VITTE_COMPILER_NAME,
        message);
}

/* ========================================================================= */

