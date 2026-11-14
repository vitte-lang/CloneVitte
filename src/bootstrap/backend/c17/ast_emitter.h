#ifndef VITTE_SRC_BACKEND_C17_AST_EMITTER_H
#define VITTE_SRC_BACKEND_C17_AST_EMITTER_H

#include "../../parser/parser.h"

#include <stdbool.h>
#include <stdio.h>

/*
 * Options used by the bootstrap AST emitter.  The regular emitter remains
 * source-compatible with its historical API; callers that need native
 * debugging metadata use this small explicit contract.
 */
typedef struct vitte_c17_compile_failure {
    const char *message;
    const char *details;
    vitte_parser_span_t span;
} vitte_c17_compile_failure_t;

typedef struct vitte_c17_compile_options {
    bool debug_info;
    bool debug_runtime;
    bool emit_line_directives;
    const char *source_path;
    vitte_c17_compile_failure_t *failure;
} vitte_c17_compile_options_t;

/* Failure phase reported by the AST-to-C17 compile driver. */
typedef enum vitte_c17_compile_result {
    VITTE_C17_COMPILE_SUCCESS = 0,
    VITTE_C17_COMPILE_INVALID_ARGUMENT,
    VITTE_C17_COMPILE_OUTPUT_FAILED,
    VITTE_C17_COMPILE_EMIT_FAILED,
    VITTE_C17_COMPILE_C_COMPILATION_FAILED,
    VITTE_C17_COMPILE_LINK_FAILED,
    VITTE_C17_COMPILE_RUNTIME_FAILED,
    VITTE_C17_COMPILE_UNSUPPORTED,
    VITTE_C17_COMPILE_UNSUPPORTED_FEATURE
} vitte_c17_compile_result_t;

bool
vitte_c17_emit_ast_with_options(
    FILE *output,
    const vitte_parser_t *parser,
    const vitte_c17_compile_options_t *options);

bool
vitte_c17_emit_ast(
    FILE *output,
    const vitte_parser_t *parser);

bool
vitte_c17_compile_ast(
    const vitte_parser_t *parser,
    const char *output_path,
    bool emit_c,
    bool run,
    int run_argc,
    char **run_argv);

bool
vitte_c17_compile_ast_with_options(
    const vitte_parser_t *parser,
    const char *output_path,
    bool emit_c,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_compile_options_t *options);

vitte_c17_compile_result_t
vitte_c17_compile_ast_result_with_options(
    const vitte_parser_t *parser,
    const char *output_path,
    bool emit_c,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_compile_options_t *options);

/* Compile or emit source text already produced by a frontend. */
vitte_c17_compile_result_t
vitte_c17_compile_source_result_with_options(
    const char *source,
    size_t source_length,
    const char *output_path,
    bool emit_c,
    bool emit_object,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_compile_options_t *options);

/* Emit one relocatable object file without linking an executable. */
bool
vitte_c17_compile_ast_object_with_options(
    const vitte_parser_t *parser,
    const char *output_path,
    const vitte_c17_compile_options_t *options);

vitte_c17_compile_result_t
vitte_c17_compile_ast_object_result_with_options(
    const vitte_parser_t *parser,
    const char *output_path,
    const vitte_c17_compile_options_t *options);

#endif
