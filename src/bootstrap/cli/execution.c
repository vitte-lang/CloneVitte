/*
 * Vitte Compiler
 * src/bootstrap/cli/execution.c
 *
 * Private executable-facing implementation module.
 *
 * ISO C17.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "internal.h"

vitte_main_exit_code_t
vitte_main_driver_compile(
    const vitte_main_options_t *options)
{
    vitte_driver_t driver;
    vitte_driver_config_t config;
    vitte_driver_pipeline_t pipeline;
    vitte_main_driver_context_t context;
    vitte_driver_command_t command;
    bool success;

    if (options == NULL ||
        options->input_path == NULL) {
        return VITTE_MAIN_EXIT_INTERNAL;
    }

    (void)memset(&context, 0, sizeof(context));
    context.options = options;
    context.source_id = VITTE_SOURCE_INVALID_ID;
    context.root_source_id = VITTE_SOURCE_INVALID_ID;
    context.use_ir_pipeline = options->emit_ir;

    config = vitte_driver_config_default();

    /* The CLI owns precedence and validation.  Pass the single resolved
     * configuration snapshot to the driver so file, environment and command
     * line values follow the same path into compilation. */
    if (options->config_initialized) {
        const vitte_config_resolved_t *resolved =
            vitte_config_resolved(&options->config);

        if (resolved == NULL) {
            vitte_main_error("failed to resolve compiler configuration");
            return VITTE_MAIN_EXIT_INTERNAL;
        }

        config.backend =
            (vitte_driver_backend_t)resolved->backend;
        config.optimization =
            (vitte_driver_optimization_t)resolved->optimization;
        config.max_errors = resolved->max_errors;
        config.max_output_bytes = resolved->max_output_bytes;
        config.deterministic = resolved->deterministic;
        config.warnings_as_errors = resolved->warnings_as_errors;
        config.emit_debug_info = resolved->emit_debug_info;
        config.emit_source_map = resolved->emit_source_map;
        config.emit_comments = resolved->emit_comments;
        config.runtime_checks = resolved->runtime_checks;
        config.bounds_checks = resolved->bounds_checks;
        config.null_checks = resolved->null_checks;
        config.overflow_checks = resolved->overflow_checks;
        config.incremental = resolved->incremental;
    }

    switch (options->command) {
        case VITTE_MAIN_COMMAND_COMPILE:
            command = VITTE_DRIVER_COMMAND_BUILD;
            break;

        case VITTE_MAIN_COMMAND_CHECK:
            command = VITTE_DRIVER_COMMAND_CHECK;
            break;

        case VITTE_MAIN_COMMAND_RUN:
            command = VITTE_DRIVER_COMMAND_RUN;
            break;

        case VITTE_MAIN_COMMAND_LEX:
        case VITTE_MAIN_COMMAND_PARSE:
        case VITTE_MAIN_COMMAND_EXPLAIN:
        case VITTE_MAIN_COMMAND_COMPLETION:
        case VITTE_MAIN_COMMAND_HELP:
        case VITTE_MAIN_COMMAND_VERSION:
        case VITTE_MAIN_COMMAND_INSTALL:
            return VITTE_MAIN_EXIT_INTERNAL;

        default:
            return VITTE_MAIN_EXIT_INTERNAL;
    }

    config.command = command;
    config.stop_after_parse =
        options->stop_after_parser ||
        options->stop_after_lexer;
    config.stop_after_semantic =
        options->stop_after_sema ||
        options->command == VITTE_MAIN_COMMAND_CHECK;
    config.stop_after_ir = options->emit_ir;

    if (!vitte_driver_init(&driver, &config)) {
        vitte_main_error("failed to initialize compiler driver");
        return VITTE_MAIN_EXIT_INTERNAL;
    }

    (void)memset(&pipeline, 0, sizeof(pipeline));
    pipeline.load_source = vitte_main_driver_load_source;
    pipeline.lex = vitte_main_driver_lex;
    pipeline.parse = options->stop_after_lexer
        ? NULL
        : vitte_main_driver_parse;
    pipeline.resolve_names = vitte_main_driver_analyze;
    pipeline.lower_hir = command == VITTE_DRIVER_COMMAND_CHECK
        ? NULL
        : context.use_ir_pipeline
            ? vitte_main_driver_lower_hir
            : vitte_main_driver_compile_ast;
    pipeline.lower_ir = context.use_ir_pipeline
        ? vitte_main_driver_lower_ir
        : NULL;
    pipeline.codegen = context.use_ir_pipeline
        ? vitte_main_driver_codegen_ir
        : NULL;
    pipeline.destroy = vitte_main_driver_context_destroy;

    if (!vitte_driver_set_pipeline(
            &driver,
            &pipeline,
            &context)) {
        vitte_main_error("failed to install compiler pipeline");
        vitte_driver_destroy(&driver);
        vitte_main_driver_context_destroy(NULL, &context);
        return VITTE_MAIN_EXIT_INTERNAL;
    }

    if (!vitte_driver_add_input(
            &driver,
            options->input_path,
            NULL,
            true)) {
        vitte_main_error_argument(
            "failed to register compilation input",
            options->input_path);
        vitte_driver_destroy(&driver);
        return VITTE_MAIN_EXIT_INTERNAL;
    }

    success = vitte_driver_run(&driver);

    vitte_driver_destroy(&driver);

    return success
        ? VITTE_MAIN_EXIT_SUCCESS
        : VITTE_MAIN_EXIT_FAILURE;
}

/* ========================================================================= */

