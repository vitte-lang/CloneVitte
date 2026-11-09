#ifndef VITTE_CLI_INTERNAL_H
#define VITTE_CLI_INTERNAL_H

/*
 * Vitte Compiler
 * src/bootstrap/cli/internal.h
 *
 * Private contracts shared by the executable-facing CLI modules.  The public
 * compiler subsystems keep their own APIs; this header only carries the
 * process context between command-line parsing, diagnostics, pipeline and
 * execution.
 *
 * ISO C17.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "config/config.h"
#include "diagnostic/cli_bridge.h"
#include "driver/driver.h"
#include "hir/hir.h"
#include "import/import.h"
#include "ir/ir.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "sema/sema.h"
#include "source/source.h"

#ifdef VITTE_NATIVE_FRONTEND_LINKED
/* Linked from the Vitte-native pipeline archive by the default Make target. */
enum {
    VITTE_NATIVE_FRONTEND_ACTION_COMPILE = 0u,
    VITTE_NATIVE_FRONTEND_ACTION_CHECK = 1u,
    VITTE_NATIVE_FRONTEND_ACTION_EMIT_IR = 2u,
    VITTE_NATIVE_FRONTEND_ACTION_EMIT_AST_FINGERPRINT = 3u
};
uint32_t vitte_native_frontend_run(
    const char *source,
    size_t source_length,
    uint64_t file_id);
uint32_t vitte_native_frontend_run_with_config(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const void *config);
uint32_t vitte_native_frontend_generate_with_config(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const void *config,
    uint8_t **generated_source,
    size_t *generated_source_length);
uint32_t vitte_native_frontend_generate_with_diagnostics(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const char *source_name,
    const void *config,
    uint8_t **generated_source,
    size_t *generated_source_length,
    vitte_cli_diagnostic_t **diagnostics,
    size_t *diagnostic_count);
uint32_t vitte_native_frontend_execute_with_diagnostics(
    uint32_t action,
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const char *source_name,
    const void *config,
    uint8_t **generated_source,
    size_t *generated_source_length,
    vitte_cli_diagnostic_t **diagnostics,
    size_t *diagnostic_count);
uint32_t compiler_driver_pipeline_vitte_native_driver_run_host_toolchain(
    uint8_t *source,
    size_t source_length,
    uint8_t *output_path,
    bool emit_object,
    bool run_program,
    bool debug_info,
    bool debug_runtime,
    int32_t run_argc,
    uint8_t **run_argv,
    uint32_t *failed_stage);
void vitte_native_frontend_release_output(uint8_t *generated_source);
void vitte_native_frontend_release_diagnostics(
    vitte_cli_diagnostic_t *diagnostics);
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VITTE_COMPILER_NAME "vitte"
#define VITTE_LANGUAGE_NAME "Vitte"
#define VITTE_MAIN_MODULE_PATH_MAX 4096u
#define VITTE_MAIN_MODULE_DEPTH_MAX 128u

typedef enum vitte_main_exit_code {
    VITTE_MAIN_EXIT_SUCCESS = 0,
    VITTE_MAIN_EXIT_FAILURE = 1,
    VITTE_MAIN_EXIT_USAGE = 2,
    VITTE_MAIN_EXIT_INTERNAL = 3
} vitte_main_exit_code_t;

typedef enum vitte_main_command {
    VITTE_MAIN_COMMAND_COMPILE = 0,
    VITTE_MAIN_COMMAND_CHECK,
    VITTE_MAIN_COMMAND_RUN,
    VITTE_MAIN_COMMAND_LEX,
    VITTE_MAIN_COMMAND_PARSE,
    VITTE_MAIN_COMMAND_EXPLAIN,
    VITTE_MAIN_COMMAND_COMPLETION,
    VITTE_MAIN_COMMAND_HELP,
    VITTE_MAIN_COMMAND_VERSION,
    VITTE_MAIN_COMMAND_INSTALL
} vitte_main_command_t;

typedef enum vitte_main_diagnostic_format {
    VITTE_MAIN_DIAGNOSTIC_TERMINAL = 0,
    VITTE_MAIN_DIAGNOSTIC_JSON,
    VITTE_MAIN_DIAGNOSTIC_SARIF,
    VITTE_MAIN_DIAGNOSTIC_LSP
} vitte_main_diagnostic_format_t;

static inline vitte_cli_diagnostic_format_t
vitte_main_cli_diagnostic_format(
    vitte_main_diagnostic_format_t format)
{
    switch (format) {
        case VITTE_MAIN_DIAGNOSTIC_JSON:
            return VITTE_CLI_DIAGNOSTIC_JSON;
        case VITTE_MAIN_DIAGNOSTIC_SARIF:
            return VITTE_CLI_DIAGNOSTIC_SARIF;
        case VITTE_MAIN_DIAGNOSTIC_LSP:
            return VITTE_CLI_DIAGNOSTIC_LSP;
        case VITTE_MAIN_DIAGNOSTIC_TERMINAL:
            return VITTE_CLI_DIAGNOSTIC_TERMINAL;
    }

    return VITTE_CLI_DIAGNOSTIC_TERMINAL;
}

typedef struct vitte_main_options {
    vitte_main_command_t command;
    const char *input_path;
    const char *output_path;
    const char *package_version;
    bool package_global;
    vitte_main_diagnostic_format_t diagnostic_format;
    bool color;
    bool quiet;
    bool verbose;
    bool emit_tokens;
    bool emit_ast;
    bool emit_hir;
    bool emit_ir;
    bool emit_c;
    bool emit_object;
    bool debug_info;
    bool debug_probes;
    bool emit_line_directives;
    bool validate_utf8;
    bool stop_after_lexer;
    bool stop_after_parser;
    bool stop_after_sema;
    bool show_timings;
    int run_argc;
    char **run_argv;
    vitte_config_t config;
    bool config_initialized;
} vitte_main_options_t;

typedef struct vitte_main_file {
    unsigned char *data;
    size_t length;
} vitte_main_file_t;

typedef struct vitte_main_source_origin {
    size_t combined_begin;
    size_t combined_end;
    size_t source_begin;
    size_t source_end;
    size_t path_index;
    vitte_source_id_t source_id;
} vitte_main_source_origin_t;

typedef struct vitte_main_module_set {
    char **paths;
    char **identity_paths;
    vitte_import_module_id_t *graph_module_ids;
    bool *active_paths;
    bool *loaded_paths;
    size_t count;
    size_t capacity;
    size_t *active_stack;
    size_t active_count;
    size_t active_capacity;
    vitte_import_context_t import_graph;
    bool import_graph_initialized;
    unsigned char *source;
    size_t source_length;
    size_t source_capacity;
    vitte_main_source_origin_t *origins;
    size_t origin_count;
    size_t origin_capacity;
} vitte_main_module_set_t;

typedef struct vitte_main_driver_context {
    const vitte_main_options_t *options;
    vitte_main_file_t file;
    vitte_source_context_t sources;
    vitte_source_id_t source_id;
    vitte_source_id_t root_source_id;
    vitte_main_source_origin_t *origins;
    size_t origin_count;
    vitte_lexer_t lexer;
    vitte_parser_t parser;
    vitte_token_t *expanded_tokens;
    size_t expanded_token_count;
    vitte_sema_t sema;
    vitte_hir_t hir;
    vitte_ir_context_t ir;
    bool source_initialized;
    bool lexer_initialized;
    bool parser_initialized;
    bool sema_initialized;
    bool hir_initialized;
    bool ir_initialized;
    bool use_ir_pipeline;
    bool native_frontend_completed;
    uint8_t *native_output;
    size_t native_output_length;
} vitte_main_driver_context_t;

extern _Thread_local vitte_main_diagnostic_format_t
    vitte_main_active_diagnostic_format;
extern _Thread_local bool vitte_main_process_diagnostic_emitted;

/* Process diagnostics and options. */
void vitte_main_error(const char *message);
void vitte_main_error_argument(const char *message, const char *argument);
void vitte_main_report_path_error(const char *code, const char *message,
                                  const char *path, const char *reason);
void vitte_main_report_process_warning(const char *message,
                                       const char *details);
void vitte_main_report_process_error(const char *code, const char *message,
                                     const char *details);
void vitte_main_report_process_error_span(
    const char *code,
    const char *message,
    const char *details,
    const char *source_name,
    const vitte_import_span_t *span,
    const char *diagnostic_context,
    const char *module_name,
    const char *symbol_name,
    const char *procedure_name);
void vitte_main_warning(const vitte_main_options_t *options,
                        const char *message);
void vitte_main_verbose(const vitte_main_options_t *options,
                        const char *message);
void vitte_main_print_usage(FILE *stream);
void vitte_main_print_version(void);
bool vitte_main_parse_options(int argc, char **argv,
                              vitte_main_options_t *options);
void vitte_main_options_destroy(vitte_main_options_t *options);
vitte_main_exit_code_t vitte_main_install_package(const char *name,
                                                 const char *version,
                                                 bool global_install);

/* Source/module helpers owned by entry.c and consumed by the pipeline. */
bool vitte_main_buffer_append(unsigned char **buffer, size_t *length,
                              size_t *capacity, const unsigned char *data,
                              size_t data_length);
bool vitte_main_read_file(const char *path, vitte_main_file_t *file);
void vitte_main_file_destroy(vitte_main_file_t *file);
const char *vitte_main_basename(const char *path);
bool vitte_main_validate_utf8(const vitte_main_options_t *options,
                              const vitte_main_file_t *file);
void vitte_main_print_tokens(const vitte_lexer_t *lexer);
bool vitte_main_render_lexer_diagnostic(
    const vitte_main_driver_context_t *context);
bool vitte_main_render_parser_diagnostics(
    const vitte_main_driver_context_t *context, bool *rendered);
bool vitte_main_expand_module(vitte_main_module_set_t *modules,
                              const char *path, size_t depth,
                              const char *importer);
bool vitte_main_import_graph_load(vitte_main_module_set_t *modules,
                                  const char *root_path);
bool vitte_main_import_graph_resolve(vitte_main_module_set_t *modules);
void vitte_main_module_set_destroy(vitte_main_module_set_t *modules);

/* Pipeline callbacks and execution bridge. */
bool vitte_main_driver_load_source(vitte_driver_t *driver,
                                   vitte_driver_phase_t phase,
                                   const vitte_driver_input_t *input,
                                   void *user_data);
bool vitte_main_driver_lex(vitte_driver_t *driver,
                           vitte_driver_phase_t phase,
                           const vitte_driver_input_t *input,
                           void *user_data);
bool vitte_main_driver_parse(vitte_driver_t *driver,
                             vitte_driver_phase_t phase,
                             const vitte_driver_input_t *input,
                             void *user_data);
bool vitte_main_driver_analyze(vitte_driver_t *driver,
                               vitte_driver_phase_t phase,
                               void *user_data);
bool vitte_main_driver_compile_ast(vitte_driver_t *driver,
                                   vitte_driver_phase_t phase,
                                   void *user_data);
bool vitte_main_driver_lower_hir(vitte_driver_t *driver,
                                 vitte_driver_phase_t phase,
                                 void *user_data);
bool vitte_main_driver_lower_ir(vitte_driver_t *driver,
                                vitte_driver_phase_t phase,
                                void *user_data);
bool vitte_main_driver_codegen_ir(vitte_driver_t *driver,
                                  vitte_driver_phase_t phase,
                                  void *user_data);
void vitte_main_driver_context_destroy(vitte_driver_t *driver,
                                       void *user_data);
vitte_main_exit_code_t vitte_main_driver_compile(
    const vitte_main_options_t *options);

#ifdef __cplusplus
}
#endif

#endif
