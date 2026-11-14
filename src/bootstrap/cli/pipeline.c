/*
 * Vitte Compiler
 * src/bootstrap/cli/pipeline.c
 *
 * Private executable-facing implementation module.
 *
 * ISO C17.
 */

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "api/version.h"
#include "backend/c17/ast_emitter.h"
#include "backend/c17/ir_emitter.h"
#include "filesystem/filesystem.h"
#include "hir/lower.h"
#include "ir/lower.h"
#include "macro_expansion.h"
#include "platform/platform.h"
#include "printer/printer.h"
#include "unicode/unicode.h"
#include "util/util.h"
#include "internal.h"



static bool vitte_main_origin_offset(
    const vitte_main_driver_context_t *context,
    size_t combined_offset,
    bool end_position,
    const vitte_main_source_origin_t **origin,
    size_t *source_offset);

static bool vitte_main_origin_span(
    const vitte_main_driver_context_t *context,
    size_t combined_begin,
    size_t combined_end,
    vitte_source_id_t *source_id,
    size_t *source_begin,
    size_t *source_end);

static void vitte_main_diagnostic_map_span(
    const vitte_main_driver_context_t *context,
    vitte_cli_diagnostic_span_t *span);
#ifdef VITTE_NATIVE_FRONTEND_LINKED
static void vitte_main_diagnostic_map_native_batch(
    const vitte_main_driver_context_t *context,
    vitte_cli_diagnostic_t *diagnostics,
    size_t diagnostic_count);
#endif
static bool vitte_main_render_diagnostic(
    const vitte_main_driver_context_t *context,
    vitte_cli_diagnostic_severity_t severity,
    const char *code,
    const char *message,
    const char *details,
    const vitte_parser_span_t *source_span,
    const char *procedure,
    const char *diagnostic_context);

/* Driver bridge                                                             */
/* ========================================================================= */

bool
vitte_main_driver_load_source(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    const vitte_driver_input_t *input,
    void *user_data)
{
    vitte_main_driver_context_t *context;
    vitte_main_module_set_t modules;
    char root_path[VITTE_MAIN_MODULE_PATH_MAX];
    char absolute_input[VITTE_MAIN_MODULE_PATH_MAX];
    char expanded_source_path[VITTE_MAIN_MODULE_PATH_MAX];
    char *canonical_input;
    vitte_filesystem_result_t canonical_result;
    size_t expanded_length;
    unsigned char terminator;
    int written;

    (void)driver;
    (void)phase;

    context = (vitte_main_driver_context_t *)user_data;

    if (context == NULL ||
        context->options == NULL ||
        input == NULL ||
        input->name == NULL) {
        return false;
    }

    vitte_main_verbose(context->options, "loading source");

    if (!vitte_main_read_file(input->name, &context->file) ||
        !vitte_main_validate_utf8(context->options, &context->file)) {
        return false;
    }

    canonical_input = NULL;
    if (vitte_filesystem_path_is_absolute(input->name)) {
        if (snprintf(
                absolute_input,
                sizeof(absolute_input),
                "%s",
                input->name) < 0) {
            return false;
        }
    } else {
        char working_directory[VITTE_MAIN_MODULE_PATH_MAX];

        if (!vitte_platform_getcwd(
                working_directory,
                sizeof(working_directory)) ||
            snprintf(
                absolute_input,
                sizeof(absolute_input),
                "%s/%s",
                working_directory,
                input->name) < 0) {
            return false;
        }
    }
    canonical_input = NULL;
    canonical_result = vitte_filesystem_normalize(
        absolute_input,
        &canonical_input);
    if (canonical_result.error != VITTE_FILESYSTEM_ERROR_NONE ||
        canonical_input == NULL ||
        strlen(canonical_input) >= sizeof(root_path)) {
        free(canonical_input);
        vitte_main_error_argument("input path is too long", input->name);
        return false;
    }
    memcpy(root_path, canonical_input, strlen(canonical_input) + 1u);
    free(canonical_input);

    memset(&modules, 0, sizeof(modules));
    if (!vitte_import_init(&modules.import_graph)) {
        vitte_main_error("failed to initialize import graph");
        free(context->file.data);
        context->file.data = NULL;
        context->file.length = 0u;
        return false;
    }
    modules.import_graph_initialized = true;
    if (!vitte_main_import_graph_load(&modules, root_path) ||
        modules.source == NULL ||
        modules.source_length == SIZE_MAX) {
        vitte_main_module_set_destroy(&modules);
        free(modules.source);
        if (!vitte_main_process_diagnostic_emitted) {
            vitte_main_error_argument(
                "failed to assemble imported modules for",
                input->name);
        }
        return false;
    }
    expanded_length = modules.source_length;
    terminator = 0u;
    if (!vitte_main_buffer_append(
            &modules.source,
            &modules.source_length,
            &modules.source_capacity,
            &terminator,
            1u)) {
        vitte_main_module_set_destroy(&modules);
        free(modules.source);
        vitte_main_error("failed to allocate combined module source");
        return false;
    }
    vitte_main_verbose(context->options, "registering source");

    if (!vitte_source_init(&context->sources)) {
        vitte_main_error("failed to initialize source subsystem");
        vitte_main_module_set_destroy(&modules);
        return false;
    }

    context->source_initialized = true;

    if (!vitte_source_add(
            &context->sources,
            vitte_main_basename(input->name),
            input->name,
            (const char *)context->file.data,
            context->file.length,
            &context->root_source_id)) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_MAP",
            "root source file could not be registered",
            input->name,
            vitte_source_error_name(
                vitte_source_last_error(&context->sources)));
        vitte_main_module_set_destroy(&modules);
        free(modules.source);
        return false;
    }

    written = snprintf(
        expanded_source_path,
        sizeof(expanded_source_path),
        "vitte://expanded/%s",
        vitte_main_basename(input->name));
    if (written < 0 ||
        (size_t)written >= sizeof(expanded_source_path)) {
        vitte_main_module_set_destroy(&modules);
        free(modules.source);
        vitte_main_error_argument(
            "expanded source name is too long for",
            input->name);
        return false;
    }

    free(context->file.data);
    context->file.data = modules.source;
    context->file.length = expanded_length;
    modules.source = NULL;

    if (!vitte_source_add(
            &context->sources,
            "expanded module input",
            expanded_source_path,
            (const char *)context->file.data,
            context->file.length,
            &context->source_id)) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_MAP",
            "expanded source could not be registered",
            expanded_source_path,
            vitte_source_error_name(
                vitte_source_last_error(&context->sources)));
        vitte_main_module_set_destroy(&modules);
        return false;
    }

    {
        size_t path_index;
        size_t origin_index;

        for (path_index = 0u;
             path_index < modules.count;
             ++path_index) {
            vitte_source_id_t origin_source_id;

            if (strcmp(modules.paths[path_index], root_path) == 0) {
                origin_source_id = context->root_source_id;
            } else {
                vitte_main_file_t origin_file;

                memset(&origin_file, 0, sizeof(origin_file));
                if (!vitte_main_read_file(
                        modules.paths[path_index],
                        &origin_file) ||
                    !vitte_source_add(
                        &context->sources,
                        vitte_main_basename(modules.paths[path_index]),
                        modules.paths[path_index],
                        (const char *)origin_file.data,
                        origin_file.length,
                        &origin_source_id)) {
                    vitte_main_file_destroy(&origin_file);
                    vitte_main_report_path_error(
                        "VITTE_INFRA_E_SOURCE_MAP",
                        "imported source file could not be registered",
                        modules.paths[path_index],
                        vitte_source_error_name(
                            vitte_source_last_error(&context->sources)));
                    vitte_main_module_set_destroy(&modules);
                    return false;
                }
                vitte_main_file_destroy(&origin_file);
            }

            for (origin_index = 0u;
                 origin_index < modules.origin_count;
                 ++origin_index) {
                if (modules.origins[origin_index].path_index ==
                    path_index) {
                    modules.origins[origin_index].source_id =
                        origin_source_id;
                }
            }
        }

        context->origins = modules.origins;
        context->origin_count = modules.origin_count;
        modules.origins = NULL;
        modules.origin_count = 0u;
        modules.origin_capacity = 0u;
    }
    vitte_main_module_set_destroy(&modules);

    if (context->source_id > (vitte_source_id_t)UINT32_MAX) {
        vitte_main_error("source identifier exceeds the lexer limit");
        return false;
    }

    return true;
}

#ifdef VITTE_NATIVE_FRONTEND_LINKED
static bool
vitte_main_source_has_import_keyword(
    const unsigned char *source,
    size_t length)
{
    size_t index;

    if (source == NULL || length < 4u) {
        return false;
    }

    for (index = 0u; index + 3u < length; ++index) {
        unsigned char previous = index == 0u ? 0u : source[index - 1u];
        unsigned char following = source[index + 3u];
        bool previous_is_identifier =
            (previous >= (unsigned char)'a' && previous <= (unsigned char)'z') ||
            (previous >= (unsigned char)'A' && previous <= (unsigned char)'Z') ||
            (previous >= (unsigned char)'0' && previous <= (unsigned char)'9') ||
            previous == (unsigned char)'_';

        if (previous_is_identifier || source[index] != (unsigned char)'u' ||
            source[index + 1u] != (unsigned char)'s' ||
            source[index + 2u] != (unsigned char)'e') {
            continue;
        }

        if (following == (unsigned char)' ' ||
            following == (unsigned char)'\t' ||
            following == (unsigned char)'\r' ||
            following == (unsigned char)'\n') {
            return true;
        }
    }

    return false;
}
#endif

#ifdef VITTE_NATIVE_FRONTEND_LINKED
static bool
vitte_main_source_identifier_byte(unsigned char byte)
{
    return (byte >= (unsigned char)'a' && byte <= (unsigned char)'z') ||
           (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') ||
           (byte >= (unsigned char)'0' && byte <= (unsigned char)'9') ||
           byte == (unsigned char)'_';
}

static bool
vitte_main_source_has_word(
    const unsigned char *source,
    size_t length,
    const char *word)
{
    size_t word_length;
    size_t index;

    if (source == NULL || word == NULL) {
        return false;
    }
    word_length = strlen(word);
    if (word_length == 0u || word_length > length) {
        return false;
    }

    for (index = 0u; index + word_length <= length; ++index) {
        bool left_boundary = index == 0u ||
            !vitte_main_source_identifier_byte(source[index - 1u]);
        size_t end = index + word_length;
        bool right_boundary = end == length ||
            !vitte_main_source_identifier_byte(source[end]);

        if (left_boundary && right_boundary &&
            memcmp(source + index, word, word_length) == 0) {
            return true;
        }
    }
    return false;
}

static size_t
vitte_main_source_skip_space(const unsigned char *source, size_t length, size_t index)
{
    while (index < length &&
           (source[index] == (unsigned char)' ' ||
            source[index] == (unsigned char)'\t' ||
            source[index] == (unsigned char)'\r' ||
            source[index] == (unsigned char)'\n')) {
        ++index;
    }
    return index;
}

/* Native HIR lowers nongeneric picks with up to sixteen scalar payload fields. */
static bool
vitte_main_pick_payload_type_is_native(
    const unsigned char *source,
    size_t length)
{
#define VITTE_PICK_TYPE(name) \
    (length == sizeof(name) - 1u && \
     memcmp(source, name, sizeof(name) - 1u) == 0)
    return VITTE_PICK_TYPE("i32") || VITTE_PICK_TYPE("i64") ||
           VITTE_PICK_TYPE("int") || VITTE_PICK_TYPE("u8") ||
           VITTE_PICK_TYPE("u16") || VITTE_PICK_TYPE("u32") ||
           VITTE_PICK_TYPE("u64") || VITTE_PICK_TYPE("usize") ||
           VITTE_PICK_TYPE("bool") || VITTE_PICK_TYPE("char") ||
           VITTE_PICK_TYPE("f64") || VITTE_PICK_TYPE("double") ||
           VITTE_PICK_TYPE("string") || VITTE_PICK_TYPE("str");
#undef VITTE_PICK_TYPE
}

static bool
vitte_main_source_has_unsupported_pick(
    const unsigned char *source,
    size_t length)
{
    static const char pick_word[] = "pick";
    size_t index;

    if (source == NULL) {
        return false;
    }
    for (index = 0u; index + sizeof(pick_word) - 1u <= length; ++index) {
        size_t cursor;
        size_t body;
        size_t depth;

        if ((index != 0u &&
             vitte_main_source_identifier_byte(source[index - 1u])) ||
            memcmp(source + index, pick_word, sizeof(pick_word) - 1u) != 0 ||
            (index + sizeof(pick_word) - 1u < length &&
             vitte_main_source_identifier_byte(
                 source[index + sizeof(pick_word) - 1u]))) {
            continue;
        }
        cursor = vitte_main_source_skip_space(
            source,
            length,
            index + sizeof(pick_word) - 1u);
        while (cursor < length && source[cursor] != (unsigned char)'{' &&
               source[cursor] != (unsigned char)';' &&
               source[cursor] != (unsigned char)'}') {
            if (source[cursor] == (unsigned char)'<') {
                return true;
            }
            ++cursor;
        }
        if (cursor == length || source[cursor] != (unsigned char)'{') {
            continue;
        }
        body = cursor + 1u;
        depth = 1u;
        while (body < length && depth != 0u) {
            if (source[body] == (unsigned char)'(') {
#ifndef VITTE_NATIVE_FRONTEND_LINKED
                /* The bootstrap emitter still owns payload-pick source while
                 * the native frontend is being rebuilt. */
                return true;
#else
                size_t payload = body + 1u;
                size_t payload_fields = 0u;
                bool payload_valid = true;
                for (;;) {
                    size_t type_begin = vitte_main_source_skip_space(
                        source,
                        length,
                        payload);
                    size_t type_end = type_begin;
                    size_t after_type;
                    if (type_begin < length &&
                        source[type_begin] == (unsigned char)')') {
                        payload = type_begin + 1u;
                        break;
                    }
                    while (type_end < length &&
                           vitte_main_source_identifier_byte(source[type_end])) {
                        ++type_end;
                    }
                    after_type = vitte_main_source_skip_space(
                        source,
                        length,
                        type_end);
                    if (!vitte_main_pick_payload_type_is_native(
                            source + type_begin,
                            type_end - type_begin) ||
                        after_type >= length ||
                        (source[after_type] != (unsigned char)',' &&
                         source[after_type] != (unsigned char)')') ||
                        payload_fields >= 16u) {
                        payload_valid = false;
                        break;
                    }
                    ++payload_fields;
                    if (source[after_type] == (unsigned char)')') {
                        payload = after_type + 1u;
                        break;
                    }
                    payload = after_type + 1u;
                }
                if (!payload_valid || payload > length) {
                    return true;
                }
                body = payload - 1u;
#endif
            }
            if (source[body] == (unsigned char)'{') {
                ++depth;
            } else if (source[body] == (unsigned char)'}') {
                --depth;
            }
            ++body;
        }
    }
    return false;
}

static bool
vitte_main_source_has_export_alias(
    const unsigned char *source,
    size_t length)
{
    size_t index;
    static const char export_word[] = "export";
    static const char as_word[] = "as";

    if (source == NULL) {
        return false;
    }
    for (index = 0u; index + sizeof(export_word) - 1u <= length; ++index) {
        size_t cursor;
        size_t name_begin;
        size_t name_end;

        if ((index != 0u &&
             vitte_main_source_identifier_byte(source[index - 1u])) ||
            memcmp(source + index,
                   export_word,
                   sizeof(export_word) - 1u) != 0 ||
            (index + sizeof(export_word) - 1u < length &&
             vitte_main_source_identifier_byte(
                 source[index + sizeof(export_word) - 1u]))) {
            continue;
        }
        cursor = vitte_main_source_skip_space(
            source,
            length,
            index + sizeof(export_word) - 1u);
        name_begin = cursor;
        while (cursor < length &&
               vitte_main_source_identifier_byte(source[cursor])) {
            ++cursor;
        }
        name_end = cursor;
        if (name_begin == name_end) {
            continue;
        }
        cursor = vitte_main_source_skip_space(source, length, cursor);
        if (cursor + sizeof(as_word) - 1u <= length &&
            memcmp(source + cursor, as_word, sizeof(as_word) - 1u) == 0 &&
            (cursor + sizeof(as_word) - 1u == length ||
             !vitte_main_source_identifier_byte(
                 source[cursor + sizeof(as_word) - 1u]))) {
            return true;
        }
    }
    return false;
}

static bool
vitte_main_source_has_named_call(
    const unsigned char *source,
    size_t length,
    const char *name)
{
    size_t name_length;
    size_t index;

    if (source == NULL || name == NULL) {
        return false;
    }
    name_length = strlen(name);
    if (name_length == 0u || name_length > length) {
        return false;
    }
    for (index = 0u; index + name_length <= length; ++index) {
        size_t cursor;
        size_t end = index + name_length;

        if ((index != 0u &&
             vitte_main_source_identifier_byte(source[index - 1u])) ||
            (end < length &&
             vitte_main_source_identifier_byte(source[end])) ||
            memcmp(source + index, name, name_length) != 0) {
            continue;
        }
        cursor = vitte_main_source_skip_space(source, length, end);
        if (cursor < length && source[cursor] == (unsigned char)'(') {
            return true;
        }
    }
    return false;
}

static bool
vitte_main_source_has_borrowed_call(
    const unsigned char *source,
    size_t length)
{
    size_t index;

    if (source == NULL) {
        return false;
    }
    for (index = 0u; index < length; ++index) {
        size_t cursor;
        size_t name_begin;

        if (source[index] != (unsigned char)'&') {
            continue;
        }
        cursor = vitte_main_source_skip_space(source, length, index + 1u);
        name_begin = cursor;
        while (cursor < length &&
               vitte_main_source_identifier_byte(source[cursor])) {
            ++cursor;
        }
        if (cursor == name_begin) {
            continue;
        }
        cursor = vitte_main_source_skip_space(source, length, cursor);
        if (cursor < length && source[cursor] == (unsigned char)'(') {
            return true;
        }
    }
    return false;
}

static bool
vitte_main_source_has_if_block_local(
    const unsigned char *source,
    size_t length)
{
    size_t index;

    if (source == NULL) {
        return false;
    }
    for (index = 0u; index + 2u <= length; ++index) {
        size_t cursor;

        if (source[index] != (unsigned char)'i' ||
            source[index + 1u] != (unsigned char)'f' ||
            (index != 0u &&
             vitte_main_source_identifier_byte(source[index - 1u])) ||
            (index + 2u < length &&
             vitte_main_source_identifier_byte(source[index + 2u]))) {
            continue;
        }
        cursor = index + 2u;
        while (cursor < length && source[cursor] != (unsigned char)'{' &&
               source[cursor] != (unsigned char)';' &&
               source[cursor] != (unsigned char)'}') {
            ++cursor;
        }
        if (cursor == length || source[cursor] != (unsigned char)'{') {
            continue;
        }
        cursor = vitte_main_source_skip_space(source, length, cursor + 1u);
        if (cursor + 3u <= length &&
            memcmp(source + cursor, "let", 3u) == 0 &&
            (cursor + 3u == length ||
             !vitte_main_source_identifier_byte(source[cursor + 3u]))) {
            return true;
        }
    }
    return false;
}

/* Keep language features without complete native lowering on the compatibility
 * pipeline. Form fields, including fixed arrays, are selected by the native
 * frontend's AST-aware HIR/IR support check. */
static bool
vitte_main_source_requires_compatibility_frontend(
    const unsigned char *source,
    size_t length)
{
    return vitte_main_source_has_unsupported_pick(source, length) ||
           vitte_main_source_has_word(source, length, "requires") ||
           vitte_main_source_has_word(source, length, "ensures") ||
           vitte_main_source_has_export_alias(source, length) ||
           vitte_main_source_has_named_call(source, length, "trim") ||
           vitte_main_source_has_named_call(source, length, "starts_with") ||
           vitte_main_source_has_named_call(source, length, "ends_with") ||
           vitte_main_source_has_borrowed_call(source, length) ||
           vitte_main_source_has_if_block_local(source, length) ||
           vitte_main_source_has_word(source, length, "comptime") ||
           vitte_main_source_has_word(source, length, "unsafe");
}
#endif

bool
vitte_main_driver_lex(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    const vitte_driver_input_t *input,
    void *user_data)
{
    vitte_main_driver_context_t *context;

    (void)driver;
    (void)phase;
    (void)input;

    context = (vitte_main_driver_context_t *)user_data;

    if (context == NULL ||
        context->options == NULL ||
        context->file.data == NULL ||
        context->source_id == VITTE_SOURCE_INVALID_ID) {
        return false;
    }

#ifdef VITTE_NATIVE_FRONTEND_LINKED
    /*
     * Route compile, check, run, and --emit-ir through one Vitte pipeline.
     * Inspection commands and earlier explicit phase stops retain their
     * dedicated CLI behavior. Known unsupported language features are kept
     * on the bootstrap compatibility pipeline before selecting the native
     * frontend; a failed native invocation stays authoritative.
     */
    if ((context->options->command == VITTE_MAIN_COMMAND_COMPILE ||
         context->options->command == VITTE_MAIN_COMMAND_RUN ||
         context->options->command == VITTE_MAIN_COMMAND_CHECK ||
         context->options->emit_ir ||
         context->options->stop_after_sema) &&
        !vitte_main_source_has_import_keyword(
            context->file.data,
            context->file.length) &&
        !vitte_main_source_requires_compatibility_frontend(
            context->file.data,
            context->file.length) &&
        !context->options->stop_after_lexer &&
        !context->options->stop_after_parser &&
        !context->options->emit_tokens &&
        !context->options->emit_ast &&
        !context->options->emit_hir) {
        uint8_t *native_output = NULL;
        size_t native_output_length = 0u;
        vitte_cli_diagnostic_t *native_diagnostics = NULL;
        size_t native_diagnostic_count = 0u;
        const vitte_source_entry_t *registered_source;
        const char *source_name;
        uint32_t native_action = context->options->emit_ir
            ? VITTE_NATIVE_FRONTEND_ACTION_EMIT_IR
            : ((context->options->command == VITTE_MAIN_COMMAND_CHECK ||
                context->options->stop_after_sema)
                ? VITTE_NATIVE_FRONTEND_ACTION_CHECK
                : VITTE_NATIVE_FRONTEND_ACTION_COMPILE);
        uint32_t native_status =
            vitte_native_frontend_execute_with_diagnostics(
                native_action,
                (const char *)context->file.data,
                context->file.length,
                (uint64_t)context->source_id,
                context->options->input_path,
                &context->options->config,
                &native_output,
                &native_output_length,
                &native_diagnostics,
                &native_diagnostic_count);

        registered_source = vitte_source_get(
            &context->sources,
            context->source_id);
        source_name = registered_source != NULL
            ? (registered_source->path != NULL
                ? registered_source->path
                : registered_source->name)
            : context->options->input_path;

        vitte_main_diagnostic_map_native_batch(
            context,
            native_diagnostics,
            native_diagnostic_count);

        if (native_diagnostic_count != 0u) {
            bool rendered = vitte_diagnostic_render_cli_batch(
                stderr,
                &context->sources,
                context->source_id,
                source_name,
                native_diagnostics,
                native_diagnostic_count,
                context->options->color,
                vitte_main_cli_diagnostic_format(
                    context->options->diagnostic_format));
            vitte_native_frontend_release_diagnostics(native_diagnostics);
            native_diagnostics = NULL;
            if (!rendered) {
                if (native_output != NULL) {
                    vitte_native_frontend_release_output(native_output);
                }
                vitte_main_error(
                    "failed to render native frontend diagnostics");
                return false;
            }
        } else if (native_diagnostics != NULL) {
            vitte_native_frontend_release_diagnostics(native_diagnostics);
            native_diagnostics = NULL;
        }

        if (native_status == 1u || native_status == 2u) {
            context->native_output = native_output;
            context->native_output_length = native_output_length;
            context->native_frontend_completed = true;
            vitte_main_verbose(
                context->options,
                "native Vitte frontend completed before the bootstrap lexer");
            return true;
        }
        if (native_output != NULL) {
            vitte_native_frontend_release_output(native_output);
        }
        context->native_frontend_completed = true;
        if (native_diagnostic_count == 0u) {
            if (!vitte_main_render_diagnostic(
                    context,
                    VITTE_CLI_DIAGNOSTIC_ERROR,
                    "VITTE_INFRA_E_PHASE",
                    "native frontend did not complete",
                    "no structured diagnostics or native pipeline output were returned; bootstrap fallback is disabled for this command",
                    NULL,
                    NULL,
                    "native frontend")) {
                vitte_main_error(
                    "failed to render native frontend failure");
            }
        }
        vitte_main_verbose(
            context->options,
            "native frontend failure is authoritative; bootstrap frontend fallback is disabled");
        return false;
    }
#endif

    vitte_main_verbose(context->options, "lexing");

    if (!vitte_lexer_init(
            &context->lexer,
            (const char *)context->file.data,
            context->file.length,
            (uint32_t)context->source_id)) {
        vitte_main_error("failed to initialize lexer");
        return false;
    }

    context->lexer_initialized = true;

    if (!vitte_lexer_run(&context->lexer)) {
        if (!vitte_main_render_lexer_diagnostic(context)) {
            vitte_main_error(
                "failed to render lexical diagnostic");
        }
        return false;
    }

    if (context->options->emit_tokens) {
        vitte_main_print_tokens(&context->lexer);
    }

    return true;
}

static bool
vitte_main_render_diagnostic(
    const vitte_main_driver_context_t *context,
    vitte_cli_diagnostic_severity_t severity,
    const char *code,
    const char *message,
    const char *details,
    const vitte_parser_span_t *source_span,
    const char *procedure,
    const char *diagnostic_context)
{
    const vitte_source_entry_t *source;
    vitte_source_id_t diagnostic_source_id;
    vitte_source_location_t start;
    vitte_source_location_t end;
    size_t diagnostic_begin;
    size_t diagnostic_end;
    size_t start_line;
    size_t start_column;
    size_t end_line;
    size_t end_column;
    const char *source_name;
    bool has_span;

    if (context == NULL ||
        context->options == NULL) {
        return false;
    }

    has_span = source_span != NULL &&
        source_span->valid &&
        source_span->begin <= source_span->end;
    diagnostic_source_id = context->source_id;
    diagnostic_begin = has_span ? source_span->begin : 0u;
    diagnostic_end = has_span ? source_span->end : 0u;
    if (has_span && source_span->end <= context->file.length) {
        (void)vitte_main_origin_span(
            context,
            source_span->begin,
            source_span->end,
            &diagnostic_source_id,
            &diagnostic_begin,
            &diagnostic_end);
    }

    source = vitte_source_get(
        &context->sources,
        diagnostic_source_id);
    source_name = source != NULL
        ? (source->path != NULL ? source->path : source->name)
        : context->options->input_path;

    start_line = source_span != NULL
        ? source_span->line
        : 0u;
    start_column = source_span != NULL
        ? source_span->column
        : 0u;
    end_line = start_line;
    end_column = start_column;

    if (has_span) {
        if (vitte_source_location_from_offset(
                &context->sources,
                diagnostic_source_id,
                diagnostic_begin,
                &start) &&
            start.valid) {
            start_line = start.line - 1u;
            start_column = start.column - 1u;
        }

        if (vitte_source_location_from_offset(
                &context->sources,
                diagnostic_source_id,
                diagnostic_end,
                &end) &&
            end.valid) {
            end_line = end.line - 1u;
            end_column = end.column - 1u;
        } else {
            end_column += diagnostic_end - diagnostic_begin;
        }
    }

    return vitte_diagnostic_render_cli(
        stderr,
        &context->sources,
        diagnostic_source_id,
        source_name,
        severity,
        code,
        message,
        details,
        has_span,
        has_span ? (uint32_t)diagnostic_source_id : 0u,
        has_span ? diagnostic_begin : 0u,
        has_span ? diagnostic_end : 0u,
        start_line,
        start_column,
        end_line,
        end_column,
        procedure,
        diagnostic_context,
        context->options->color,
        vitte_main_cli_diagnostic_format(
            context->options->diagnostic_format));
}

static bool
vitte_main_make_cli_diagnostic(
    const vitte_main_driver_context_t *context,
    vitte_cli_diagnostic_t *diagnostic,
    vitte_cli_diagnostic_severity_t severity,
    const char *code,
    const char *message,
    const char *details,
    const vitte_parser_span_t *source_span,
    const char *procedure,
    const char *diagnostic_context)
{
    const vitte_source_entry_t *source;
    vitte_source_id_t diagnostic_source_id;
    vitte_source_location_t location;
    size_t diagnostic_begin;
    size_t diagnostic_end;

    if (context == NULL ||
        context->options == NULL ||
        diagnostic == NULL ||
        code == NULL) {
        return false;
    }

    vitte_cli_diagnostic_init(diagnostic);
    diagnostic->severity = severity;
    diagnostic->code = code;
    diagnostic->message = message;
    diagnostic->details = details;
    diagnostic->procedure = procedure;
    diagnostic->context = diagnostic_context;
    diagnostic->end_offset = 0u;
    diagnostic->file_id = 0u;
    diagnostic->source_id = VITTE_SOURCE_INVALID_ID;
    diagnostic->source_name = NULL;

    if (source_span == NULL ||
        !source_span->valid ||
        source_span->begin > source_span->end ||
        source_span->end > context->file.length) {
        return true;
    }

    diagnostic_source_id = context->source_id;
    diagnostic_begin = source_span->begin;
    diagnostic_end = source_span->end;
    (void)vitte_main_origin_span(
        context,
        source_span->begin,
        source_span->end,
        &diagnostic_source_id,
        &diagnostic_begin,
        &diagnostic_end);

    source = vitte_source_get(
        &context->sources,
        diagnostic_source_id);
    diagnostic->has_span = true;
    diagnostic->file_id = (uint32_t)diagnostic_source_id;
    diagnostic->source_id = diagnostic_source_id;
    diagnostic->source_name = source != NULL
        ? source->path
        : context->options->input_path;
    diagnostic->start_offset = diagnostic_begin;
    diagnostic->end_offset = diagnostic_end;
    diagnostic->start_line = source_span->line;
    diagnostic->start_column = source_span->column;
    diagnostic->end_line = source_span->line;
    diagnostic->end_column = source_span->column;

    if (vitte_source_location_from_offset(
                &context->sources,
                diagnostic_source_id,
                diagnostic_begin,
                &location) &&
        location.valid) {
        diagnostic->start_line = location.line - 1u;
        diagnostic->start_column = location.column - 1u;
    }

    if (vitte_source_location_from_offset(
                &context->sources,
                diagnostic_source_id,
                diagnostic_end,
                &location) &&
        location.valid) {
        diagnostic->end_line = location.line - 1u;
        diagnostic->end_column = location.column - 1u;
    } else {
        diagnostic->end_column +=
                diagnostic_end - diagnostic_begin;
    }

    return true;
}

static const char *
vitte_main_parser_diagnostic_code(
    vitte_parser_error_t error)
{
    switch (error) {
        case VITTE_PARSER_ERROR_EXPECTED_TOKEN:
        case VITTE_PARSER_ERROR_EXPECTED_IDENTIFIER:
            return "VITTE_PARSER_E_EXPECTED_TOKEN";

        case VITTE_PARSER_ERROR_EXPECTED_EXPRESSION:
            return "VITTE_PARSER_E_EXPECTED_EXPRESSION";

        case VITTE_PARSER_ERROR_EXPECTED_TYPE:
            return "VITTE_PARSER_E_EXPECTED_TYPE";

        case VITTE_PARSER_ERROR_EXPECTED_DECLARATION:
            return "VITTE_PARSER_E_EXPECTED_DECLARATION";

        case VITTE_PARSER_ERROR_UNEXPECTED_TOKEN:
        case VITTE_PARSER_ERROR_EXPECTED_STATEMENT:
            return "VITTE_PARSER_E_UNEXPECTED_TOKEN";

        case VITTE_PARSER_ERROR_NONE:
        case VITTE_PARSER_ERROR_INVALID_ARGUMENT:
        case VITTE_PARSER_ERROR_INVALID_CONTEXT:
        case VITTE_PARSER_ERROR_INVALID_STATE:
        case VITTE_PARSER_ERROR_INVALID_TOKEN:
        case VITTE_PARSER_ERROR_INVALID_NODE:
        case VITTE_PARSER_ERROR_NODE_LIMIT:
        case VITTE_PARSER_ERROR_DIAGNOSTIC_LIMIT:
        case VITTE_PARSER_ERROR_RECURSION_LIMIT:
        case VITTE_PARSER_ERROR_OVERFLOW:
        case VITTE_PARSER_ERROR_OUT_OF_MEMORY:
        case VITTE_PARSER_ERROR_VALIDATION:
        case VITTE_PARSER_ERROR_CORRUPTION:
        case VITTE_PARSER_ERROR_UNSUPPORTED:
        case VITTE_PARSER_ERROR_INTERNAL:
        case VITTE_PARSER_ERROR_COUNT:
            return "VITTE_INFRA_E_PHASE";
    }

    return "VITTE_INFRA_E_PHASE";
}

/*
 * Lexical diagnostics are reported before the parser has a token that can
 * describe the bad source text.  Keep a small, escaped rendering here so
 * control bytes and malformed UTF-8 never leak into terminal diagnostics or
 * machine-readable output.
 */
static bool
vitte_main_lexer_append_escaped_byte(
    char *buffer,
    size_t capacity,
    size_t *length,
    unsigned char byte)
{
    char escaped[5];
    const char *text;
    size_t text_length;

    if (buffer == NULL || length == NULL || *length >= capacity) {
        return false;
    }

    text = NULL;
    text_length = 0u;
    switch (byte) {
        case '\\': text = "\\"; break;
        case '`':  text = "\\`"; break;
        case '\'': text = "'"; break;
        case '"':  text = "\""; break;
        case '\n': text = "\\n"; break;
        case '\r': text = "\\r"; break;
        case '\t': text = "\\t"; break;
        case '\f': text = "\\f"; break;
        case '\v': text = "\\v"; break;
        case '\b': text = "\\b"; break;
        case '\a': text = "\\a"; break;
        default:
            if (byte >= 0x20u && byte <= 0x7eu) {
                escaped[0] = (char)byte;
                escaped[1] = '\0';
                text = escaped;
            } else {
                int count = snprintf(
                    escaped,
                    sizeof(escaped),
                    "\\x%02X",
                    (unsigned)byte);
                if (count < 0 || (size_t)count >= sizeof(escaped)) {
                    return false;
                }
                text = escaped;
            }
            break;
    }

    text_length = strlen(text);
    if (text_length > capacity - *length - 1u) {
        return false;
    }

    memcpy(buffer + *length, text, text_length);
    *length += text_length;
    buffer[*length] = '\0';
    return true;
}

static size_t
vitte_main_lexer_display_end(
    const vitte_main_driver_context_t *context,
    vitte_lexer_error_t error,
    const vitte_lexer_span_t *span)
{
    size_t end;

    if (context == NULL || span == NULL) {
        return 0u;
    }

    end = span->end;
    if (end > context->file.length) {
        end = context->file.length;
    }

    /* scan_escape stops just before the byte that made the escape invalid. */
    if (error == VITTE_LEXER_ERROR_INVALID_ESCAPE &&
        end < context->file.length) {
        unsigned char next = context->file.data[end];
        if (next != (unsigned char)'"' &&
            next != (unsigned char)'\'' &&
            next != (unsigned char)'\n' &&
            next != (unsigned char)'\r') {
            ++end;
        }
    }

    /* Include the second quote in an empty character literal (`''`). */
    if (error == VITTE_LEXER_ERROR_INVALID_CHARACTER &&
        span->begin < context->file.length &&
        context->file.data[span->begin] == (unsigned char)'\'' &&
        end < context->file.length &&
        context->file.data[end] == (unsigned char)'\'') {
        ++end;
    }

    return end;
}

static bool
vitte_main_lexer_format_details(
    const vitte_main_driver_context_t *context,
    vitte_lexer_error_t error,
    const vitte_lexer_span_t *span,
    size_t display_end,
    char *storage,
    size_t capacity)
{
    const char *prefix;
    size_t length;
    size_t index;

    if (context == NULL || span == NULL || storage == NULL ||
        capacity == 0u || span->begin > display_end ||
        display_end > context->file.length) {
        return false;
    }

    if (error == VITTE_LEXER_ERROR_INVALID_ESCAPE) {
        prefix = "invalid escape sequence `";
    } else if (error == VITTE_LEXER_ERROR_INVALID_UTF8) {
        prefix = "invalid Unicode sequence `";
    } else {
        prefix = "invalid character `";
    }

    length = 0u;
    storage[0] = '\0';
    if (strlen(prefix) >= capacity) {
        return false;
    }
    memcpy(storage, prefix, strlen(prefix));
    length = strlen(prefix);

    if (span->begin == display_end) {
        const char *empty = "<empty>";
        if (strlen(empty) > capacity - length - 2u) {
            return false;
        }
        memcpy(storage + length, empty, strlen(empty));
        length += strlen(empty);
    } else {
        for (index = span->begin; index < display_end; ++index) {
            if (!vitte_main_lexer_append_escaped_byte(
                    storage,
                    capacity,
                    &length,
                    context->file.data[index])) {
                return false;
            }
        }
    }

    if (capacity - length < 2u) {
        return false;
    }
    storage[length++] = '`';
    storage[length] = '\0';
    return true;
}

static bool
vitte_main_lexer_literal_boundary(
    unsigned char byte)
{
    if (byte == (unsigned char)'\n' ||
        byte == (unsigned char)'\r' ||
        byte == (unsigned char)'\0' ||
        (byte >= 0x09u && byte <= 0x0du) ||
        byte == (unsigned char)' ' ||
        byte == (unsigned char)'\t') {
        return true;
    }

    switch (byte) {
        case ';': case ',': case ')': case ']': case '}':
        case ':': case '+': case '-': case '*': case '/':
        case '%': case '=': case '&': case '|': case '^':
        case '!': case '<': case '>': case '?': case '.':
            return true;
        default:
            return false;
    }
}

static void
vitte_main_lexer_add_delimiter_suggestion(
    const vitte_main_driver_context_t *context,
    vitte_lexer_error_t error,
    const vitte_lexer_span_t *span,
    size_t display_end,
    const char *source_name,
    vitte_cli_diagnostic_t *diagnostic,
    vitte_cli_diagnostic_suggestion_t *suggestion)
{
    const char *replacement;
    const char *message;
    size_t offset;
    vitte_source_location_t location;

    if (context == NULL || span == NULL || source_name == NULL ||
        diagnostic == NULL || suggestion == NULL ||
        span->begin >= context->file.length ||
        display_end > context->file.length) {
        return;
    }

    replacement = NULL;
    message = NULL;
    offset = display_end;

    /*
     * A missing closing quote is unambiguous only when the next byte is a
     * literal boundary.  Do not guess for a second identifier character
     * (`'ab'`) or for an already closed empty literal (`''`).
     */
    if (error == VITTE_LEXER_ERROR_UNTERMINATED_STRING &&
        context->file.data[span->begin] == (unsigned char)'"' &&
        (offset == context->file.length ||
         context->file.data[offset] == (unsigned char)'\n' ||
         context->file.data[offset] == (unsigned char)'\r')) {
        replacement = "\"";
        message = "Insert the missing closing `\"` for this string literal.";
    } else if (error == VITTE_LEXER_ERROR_INVALID_CHARACTER &&
               context->file.data[span->begin] == (unsigned char)'\'' &&
               span->end > span->begin + 1u &&
               (offset == context->file.length ||
                vitte_main_lexer_literal_boundary(
                    context->file.data[offset]))) {
        replacement = "'";
        message = "Insert the missing closing `'` for this character literal.";
    }

    if (replacement == NULL) {
        return;
    }

    memset(suggestion, 0, sizeof(*suggestion));
    suggestion->message = message;
    suggestion->applicability = VITTE_CLI_DIAGNOSTIC_APPLICABILITY_MACHINE;
    suggestion->confidence = VITTE_CLI_DIAGNOSTIC_CONFIDENCE_HIGH;
    suggestion->preferred = true;
    suggestion->suggestion_id = (uint64_t)offset + UINT64_C(1);
    suggestion->has_span = true;
    suggestion->span.valid = true;
    suggestion->span.file_id = diagnostic->file_id;
    suggestion->span.source_id = context->source_id;
    suggestion->span.source_name = source_name;
    suggestion->span.start_offset = offset;
    suggestion->span.end_offset = offset;
    suggestion->span.start_line = diagnostic->end_line;
    suggestion->span.start_column = diagnostic->end_column;
    suggestion->span.end_line = diagnostic->end_line;
    suggestion->span.end_column = diagnostic->end_column;
    if (vitte_source_location_from_offset(
            &context->sources,
            context->source_id,
            offset,
            &location) &&
        location.valid) {
        suggestion->span.start_line = location.line - 1u;
        suggestion->span.start_column = location.column - 1u;
        suggestion->span.end_line = suggestion->span.start_line;
        suggestion->span.end_column = suggestion->span.start_column;
    }
    suggestion->has_replacement = true;
    suggestion->replacement = replacement;
    diagnostic->suggestions = suggestion;
    diagnostic->suggestion_count = 1u;
    diagnostic->has_machine_fix = true;
}

static vitte_cli_diagnostic_severity_t
vitte_main_parser_diagnostic_severity(
    vitte_parser_diagnostic_kind_t kind)
{
    switch (kind) {
        case VITTE_PARSER_DIAGNOSTIC_NOTE:
            return VITTE_CLI_DIAGNOSTIC_NOTE;

        case VITTE_PARSER_DIAGNOSTIC_HELP:
            return VITTE_CLI_DIAGNOSTIC_HELP;

        case VITTE_PARSER_DIAGNOSTIC_WARNING:
            return VITTE_CLI_DIAGNOSTIC_WARNING;

        case VITTE_PARSER_DIAGNOSTIC_ERROR:
        case VITTE_PARSER_DIAGNOSTIC_INVALID:
        case VITTE_PARSER_DIAGNOSTIC_COUNT:
            return VITTE_CLI_DIAGNOSTIC_ERROR;
    }

    return VITTE_CLI_DIAGNOSTIC_ERROR;
}

bool
vitte_main_render_lexer_diagnostic(
    const vitte_main_driver_context_t *context)
{
    vitte_lexer_error_t error;
    vitte_lexer_span_t lexer_span;
    vitte_parser_span_t source_span;
    vitte_cli_diagnostic_t diagnostic;
    vitte_cli_diagnostic_suggestion_t suggestion;
    const vitte_source_entry_t *source;
    const char *source_name;
    size_t display_end;
    const char *code;
    bool has_details;

    if (context == NULL ||
        !context->lexer_initialized) {
        return false;
    }

    error = vitte_lexer_last_error(&context->lexer);
    switch (error) {
        case VITTE_LEXER_ERROR_INVALID_UTF8:
            code = "VITTE_LEXER_E_INVALID_UNICODE";
            break;

        case VITTE_LEXER_ERROR_INVALID_NUMBER:
            code = "VITTE_LEXER_E_INVALID_NUMBER";
            break;

        case VITTE_LEXER_ERROR_INVALID_ESCAPE:
            code = "VITTE_LEXER_E_INVALID_ESCAPE";
            break;

        case VITTE_LEXER_ERROR_UNTERMINATED_STRING:
            code = "VITTE_LEXER_E_UNTERMINATED_STRING";
            break;

        case VITTE_LEXER_ERROR_UNTERMINATED_COMMENT:
            code = "VITTE_LEXER_E_UNTERMINATED_COMMENT";
            break;

        case VITTE_LEXER_ERROR_INVALID_CHARACTER:
        case VITTE_LEXER_ERROR_INVALID_TOKEN:
            code = "VITTE_LEXER_E_INVALID_CHARACTER";
            break;

        case VITTE_LEXER_ERROR_OVERFLOW:
            code = "VITTE_LEXER_E_NUMBER_OVERFLOW";
            break;

        case VITTE_LEXER_ERROR_COMMENT_DEPTH:
        case VITTE_LEXER_ERROR_TOKEN_LIMIT:
        case VITTE_LEXER_ERROR_SOURCE_TOO_LARGE:
            code = "VITTE_INFRA_E_LIMIT";
            break;

        case VITTE_LEXER_ERROR_OUT_OF_MEMORY:
            code = "VITTE_INFRA_E_RESOURCE";
            break;

        case VITTE_LEXER_ERROR_NONE:
        case VITTE_LEXER_ERROR_INVALID_ARGUMENT:
        case VITTE_LEXER_ERROR_INVALID_CONTEXT:
        case VITTE_LEXER_ERROR_INVALID_STATE:
        case VITTE_LEXER_ERROR_VALIDATION:
        case VITTE_LEXER_ERROR_INTERNAL:
        case VITTE_LEXER_ERROR_COUNT:
            code = "VITTE_INFRA_E_PHASE";
            break;

        default:
            code = "VITTE_INFRA_E_PHASE";
            break;
    }

    lexer_span = vitte_lexer_error_span(&context->lexer);
    display_end = vitte_main_lexer_display_end(
        context,
        error,
        &lexer_span);
    memset(&source_span, 0, sizeof(source_span));
    if (lexer_span.valid &&
        lexer_span.begin <= display_end &&
        display_end <= context->file.length) {
        source_span.file_id = lexer_span.file_id;
        source_span.begin = lexer_span.begin;
        source_span.end = display_end;
        source_span.valid = true;
    }

    source = vitte_source_get(
        &context->sources,
        context->source_id);
    source_name = source != NULL
        ? source->name
        : context->options->input_path;

    if (!vitte_main_make_cli_diagnostic(
            context,
            &diagnostic,
            VITTE_CLI_DIAGNOSTIC_ERROR,
            code,
            NULL,
            NULL,
            source_span.valid ? &source_span : NULL,
            NULL,
            "lexing")) {
        return false;
    }

    has_details = false;
    if (lexer_span.valid &&
        (error == VITTE_LEXER_ERROR_INVALID_CHARACTER ||
         error == VITTE_LEXER_ERROR_INVALID_TOKEN ||
         error == VITTE_LEXER_ERROR_INVALID_ESCAPE ||
         error == VITTE_LEXER_ERROR_INVALID_UTF8)) {
        has_details = vitte_main_lexer_format_details(
            context,
            error,
            &lexer_span,
            display_end,
            diagnostic.details_storage,
            sizeof(diagnostic.details_storage));
    }
    diagnostic.details = has_details
        ? diagnostic.details_storage
        : NULL;

    memset(&suggestion, 0, sizeof(suggestion));
    if (lexer_span.valid) {
        vitte_main_lexer_add_delimiter_suggestion(
            context,
            error,
            &lexer_span,
            display_end,
            source_name,
            &diagnostic,
            &suggestion);
    }

    return vitte_diagnostic_render_cli_batch(
        stderr,
        &context->sources,
        context->source_id,
        source_name,
        &diagnostic,
        1u,
        context->options->color,
        vitte_main_cli_diagnostic_format(
            context->options->diagnostic_format));
}

static bool
vitte_main_parser_token_can_start_list_item(
    vitte_token_kind_t kind)
{
    return kind == VITTE_TOKEN_IDENTIFIER ||
           vitte_token_kind_is_contextual_keyword(kind) ||
           kind == VITTE_TOKEN_LEFT_PAREN ||
           kind == VITTE_TOKEN_LEFT_BRACKET ||
           kind == VITTE_TOKEN_AMP ||
           kind == VITTE_TOKEN_STAR ||
           kind == VITTE_TOKEN_KW_NOT ||
           kind == VITTE_TOKEN_KW_MOVE ||
           kind == VITTE_TOKEN_KW_REF ||
           kind == VITTE_TOKEN_KW_AWAIT ||
           kind == VITTE_TOKEN_KW_IF ||
           kind == VITTE_TOKEN_KW_SELF ||
           kind == VITTE_TOKEN_KW_PRINT ||
           kind == VITTE_TOKEN_INTEGER_LITERAL ||
           kind == VITTE_TOKEN_FLOAT_LITERAL ||
           kind == VITTE_TOKEN_STRING_LITERAL ||
           kind == VITTE_TOKEN_CHARACTER_LITERAL ||
           kind == VITTE_TOKEN_KW_TRUE ||
           kind == VITTE_TOKEN_KW_FALSE ||
           kind == VITTE_TOKEN_KW_NULL;
}

static const char *
vitte_main_parser_punctuation_replacement(
    vitte_token_kind_t expected,
    vitte_token_kind_t found)
{
    switch (expected) {
        case VITTE_TOKEN_RIGHT_PAREN:
            if (found == VITTE_TOKEN_EOF ||
                found == VITTE_TOKEN_SEMICOLON ||
                found == VITTE_TOKEN_RIGHT_BRACE ||
                found == VITTE_TOKEN_RIGHT_BRACKET) {
                return ")";
            }
            break;

        case VITTE_TOKEN_RIGHT_BRACKET:
            if (found == VITTE_TOKEN_EOF ||
                found == VITTE_TOKEN_SEMICOLON ||
                found == VITTE_TOKEN_RIGHT_BRACE ||
                found == VITTE_TOKEN_RIGHT_PAREN) {
                return "]";
            }
            break;

        case VITTE_TOKEN_RIGHT_BRACE:
            if (found == VITTE_TOKEN_EOF ||
                found == VITTE_TOKEN_SEMICOLON ||
                found == VITTE_TOKEN_KW_ELSE ||
                found == VITTE_TOKEN_KW_ELIF) {
                return "}";
            }
            break;

        case VITTE_TOKEN_SEMICOLON:
            if (found == VITTE_TOKEN_EOF ||
                found == VITTE_TOKEN_RIGHT_BRACE ||
                found == VITTE_TOKEN_IDENTIFIER ||
                found == VITTE_TOKEN_KW_LET ||
                found == VITTE_TOKEN_KW_RETURN ||
                found == VITTE_TOKEN_KW_PRINT ||
                found == VITTE_TOKEN_KW_IF ||
                found == VITTE_TOKEN_KW_WHILE ||
                found == VITTE_TOKEN_KW_FOR ||
                found == VITTE_TOKEN_KW_BREAK ||
                found == VITTE_TOKEN_KW_CONTINUE ||
                found == VITTE_TOKEN_KW_DEFER ||
                found == VITTE_TOKEN_KW_SET ||
                found == VITTE_TOKEN_KW_ASSERT) {
                return ";";
            }
            break;

        case VITTE_TOKEN_COMMA:
            if (vitte_main_parser_token_can_start_list_item(found)) {
                return ",";
            }
            break;

        default:
            break;
    }

    return NULL;
}

static bool
vitte_main_parser_is_import_path_dot(
    const vitte_main_driver_context_t *context,
    const vitte_parser_diagnostic_t *parser_diagnostic)
{
    size_t index;

    if (context == NULL ||
        parser_diagnostic == NULL ||
        !context->lexer_initialized ||
        !parser_diagnostic->span.valid) {
        return false;
    }

    for (index = 0u; index < context->lexer.token_count; ++index) {
        const vitte_token_t *token;
        size_t previous;

        token = &context->lexer.tokens[index];
        if (token->kind != VITTE_TOKEN_DOT ||
            token->span.begin != parser_diagnostic->span.begin ||
            token->span.end != parser_diagnostic->span.end) {
            continue;
        }

        previous = index;
        while (previous != 0u) {
            const vitte_token_t *candidate;

            candidate = &context->lexer.tokens[--previous];
            if (candidate->kind == VITTE_TOKEN_SEMICOLON) {
                return false;
            }
            if (candidate->kind == VITTE_TOKEN_KW_USE) {
                return true;
            }
        }
        return false;
    }

    return false;
}

static void
vitte_main_parser_add_grammar_migration_suggestion(
    const vitte_main_driver_context_t *context,
    const vitte_parser_diagnostic_t *parser_diagnostic,
    const char *source_name,
    vitte_cli_diagnostic_t *diagnostic,
    vitte_cli_diagnostic_suggestion_t *suggestion)
{
    const char *replacement = NULL;
    const char *message = NULL;
    vitte_source_location_t start;
    vitte_source_location_t end;

    if (context == NULL ||
        parser_diagnostic == NULL ||
        source_name == NULL ||
        diagnostic == NULL ||
        suggestion == NULL ||
        parser_diagnostic->kind != VITTE_PARSER_DIAGNOSTIC_ERROR ||
        !parser_diagnostic->span.valid ||
        parser_diagnostic->span.begin >= parser_diagnostic->span.end ||
        parser_diagnostic->span.end > context->file.length) {
        return;
    }

    switch (parser_diagnostic->found) {
        case VITTE_TOKEN_AMP_AMP:
            replacement = "and";
            message = "Use `and` instead of `&&` for logical conjunction.";
            break;
        case VITTE_TOKEN_PIPE_PIPE:
            replacement = "or";
            message = "Use `or` instead of `||` for logical disjunction.";
            break;
        case VITTE_TOKEN_BANG:
            replacement = "not";
            message = "Use `not` instead of `!` for logical negation.";
            break;
        case VITTE_TOKEN_DOT:
            if (vitte_main_parser_is_import_path_dot(
                    context,
                    parser_diagnostic)) {
                replacement = "::";
                message = "Use `::` instead of `.` in an import path.";
            }
            break;
        default:
            break;
    }

    if (replacement == NULL || message == NULL ||
        !vitte_source_location_from_offset(
            &context->sources,
            context->source_id,
            parser_diagnostic->span.begin,
            &start) ||
        !start.valid ||
        !vitte_source_location_from_offset(
            &context->sources,
            context->source_id,
            parser_diagnostic->span.end,
            &end) ||
        !end.valid) {
        return;
    }

    suggestion->message = message;
    suggestion->applicability = VITTE_CLI_DIAGNOSTIC_APPLICABILITY_MAYBE;
    suggestion->confidence = VITTE_CLI_DIAGNOSTIC_CONFIDENCE_HIGH;
    suggestion->preferred = true;
    suggestion->suggestion_id =
        (uint64_t)parser_diagnostic->span.begin + UINT64_C(1);
    suggestion->has_span = true;
    suggestion->span.valid = true;
    suggestion->span.file_id = diagnostic->file_id;
    suggestion->span.source_id = context->source_id;
    suggestion->span.source_name = source_name;
    suggestion->span.start_offset = parser_diagnostic->span.begin;
    suggestion->span.end_offset = parser_diagnostic->span.end;
    suggestion->span.start_line = start.line - 1u;
    suggestion->span.start_column = start.column - 1u;
    suggestion->span.end_line = end.line - 1u;
    suggestion->span.end_column = end.column - 1u;
    suggestion->has_replacement = true;
    suggestion->replacement = replacement;

    diagnostic->suggestions = suggestion;
    diagnostic->suggestion_count = 1u;
}

static void
vitte_main_parser_add_punctuation_suggestion(
    const vitte_main_driver_context_t *context,
    const vitte_parser_diagnostic_t *parser_diagnostic,
    const char *source_name,
    vitte_cli_diagnostic_t *diagnostic,
    vitte_cli_diagnostic_suggestion_t *suggestion)
{
    const char *replacement;
    vitte_source_location_t location;
    size_t offset;

    if (context == NULL ||
        parser_diagnostic == NULL ||
        source_name == NULL ||
        diagnostic == NULL ||
        suggestion == NULL ||
        parser_diagnostic->kind != VITTE_PARSER_DIAGNOSTIC_ERROR ||
        parser_diagnostic->error != VITTE_PARSER_ERROR_EXPECTED_TOKEN ||
        !parser_diagnostic->span.valid) {
        return;
    }

    replacement =
        vitte_main_parser_punctuation_replacement(
            parser_diagnostic->expected,
            parser_diagnostic->found);
    if (replacement == NULL ||
        parser_diagnostic->span.begin > context->file.length) {
        return;
    }

    offset = parser_diagnostic->span.begin;
    suggestion->message =
        strcmp(replacement, ")") == 0
            ? "Insert the missing `)` here."
            : strcmp(replacement, "]") == 0
                ? "Insert the missing `]` here."
                : strcmp(replacement, "}") == 0
                    ? "Insert the missing `}` here."
                    : strcmp(replacement, ";") == 0
                        ? "Insert the missing `;` here."
                        : "Insert the missing `,` here.";
    suggestion->applicability =
        VITTE_CLI_DIAGNOSTIC_APPLICABILITY_MACHINE;
    suggestion->confidence = VITTE_CLI_DIAGNOSTIC_CONFIDENCE_HIGH;
    suggestion->preferred = true;
    suggestion->suggestion_id = (uint64_t)offset + UINT64_C(1);
    suggestion->has_span = true;
    suggestion->span.valid = true;
    suggestion->span.file_id = diagnostic->file_id;
    suggestion->span.source_id = context->source_id;
    suggestion->span.source_name = source_name;
    suggestion->span.start_offset = offset;
    suggestion->span.end_offset = offset;
    suggestion->span.start_line = diagnostic->start_line;
    suggestion->span.start_column = diagnostic->start_column;
    suggestion->span.end_line = diagnostic->start_line;
    suggestion->span.end_column = diagnostic->start_column;
    if (vitte_source_location_from_offset(
            &context->sources,
            context->source_id,
            offset,
            &location) &&
        location.valid) {
        suggestion->span.start_line = location.line - 1u;
        suggestion->span.start_column = location.column - 1u;
        suggestion->span.end_line = suggestion->span.start_line;
        suggestion->span.end_column = suggestion->span.start_column;
    }
    suggestion->has_replacement = true;
    suggestion->replacement = replacement;

    diagnostic->suggestions = suggestion;
    diagnostic->suggestion_count = 1u;
    diagnostic->has_machine_fix = true;
}

static bool
vitte_main_parser_token_index_at_diagnostic(
    const vitte_main_driver_context_t *context,
    const vitte_parser_diagnostic_t *parser_diagnostic,
    size_t *token_index)
{
    size_t index;

    if (context == NULL ||
        parser_diagnostic == NULL ||
        token_index == NULL ||
        !context->lexer_initialized ||
        !parser_diagnostic->span.valid) {
        return false;
    }

    for (index = 0u; index < context->lexer.token_count; ++index) {
        const vitte_token_t *token;

        token = &context->lexer.tokens[index];
        if (token->span.begin == parser_diagnostic->span.begin &&
            token->span.end == parser_diagnostic->span.end &&
            token->kind == parser_diagnostic->found) {
            *token_index = index;
            return true;
        }
    }

    return false;
}

static bool
vitte_main_parser_declaration_expects_block(
    const vitte_main_driver_context_t *context,
    size_t token_index)
{
    size_t index;

    if (context == NULL || token_index > context->lexer.token_count) {
        return false;
    }

    index = token_index;
    while (index != 0u) {
        const vitte_token_t *token;

        token = &context->lexer.tokens[--index];
        switch (token->kind) {
            case VITTE_TOKEN_KW_SPACE:
            case VITTE_TOKEN_KW_FORM:
            case VITTE_TOKEN_KW_PICK:
            case VITTE_TOKEN_KW_TRAIT:
            case VITTE_TOKEN_KW_IMPL:
            case VITTE_TOKEN_KW_PROC:
            case VITTE_TOKEN_KW_MACRO:
            case VITTE_TOKEN_KW_TEST:
                return true;

            case VITTE_TOKEN_KW_IF:
            case VITTE_TOKEN_KW_ELSE:
            case VITTE_TOKEN_KW_ELIF:
            case VITTE_TOKEN_KW_WHILE:
            case VITTE_TOKEN_KW_LOOP:
            case VITTE_TOKEN_KW_MATCH:
            case VITTE_TOKEN_KW_UNSAFE:
            case VITTE_TOKEN_KW_ASM:
            case VITTE_TOKEN_KW_DEFER:
            case VITTE_TOKEN_KW_USE:
            case VITTE_TOKEN_SEMICOLON:
            case VITTE_TOKEN_LEFT_BRACE:
            case VITTE_TOKEN_RIGHT_BRACE:
                return false;

            case VITTE_TOKEN_KW_FOR:
            {
                size_t search;

                search = index;
                while (search != 0u) {
                    const vitte_token_t *preceding;

                    preceding = &context->lexer.tokens[--search];
                    if (preceding->kind == VITTE_TOKEN_KW_IMPL) {
                        return true;
                    }
                    if (preceding->kind == VITTE_TOKEN_SEMICOLON ||
                        preceding->kind == VITTE_TOKEN_LEFT_BRACE ||
                        preceding->kind == VITTE_TOKEN_RIGHT_BRACE) {
                        return false;
                    }
                }
                return false;
            }

            default:
                break;
        }
    }

    return false;
}

static bool
vitte_main_parser_is_import_alias_as(
    const vitte_main_driver_context_t *context,
    size_t token_index)
{
    size_t index;

    if (context == NULL || token_index > context->lexer.token_count) {
        return false;
    }

    index = token_index;
    while (index != 0u) {
        const vitte_token_t *token;

        token = &context->lexer.tokens[--index];
        if (token->kind == VITTE_TOKEN_SEMICOLON) {
            return false;
        }
        if (token->kind == VITTE_TOKEN_KW_USE ||
            token->kind == VITTE_TOKEN_KW_EXPORT) {
            return true;
        }
    }

    return false;
}

static bool
vitte_main_parser_contextual_expectation(
    const vitte_main_driver_context_t *context,
    const vitte_parser_diagnostic_t *parser_diagnostic,
    const char **message,
    const char **help)
{
    size_t token_index;
    vitte_token_kind_t previous;

    if (context == NULL ||
        parser_diagnostic == NULL ||
        message == NULL ||
        help == NULL ||
        parser_diagnostic->kind != VITTE_PARSER_DIAGNOSTIC_ERROR ||
        !vitte_main_parser_token_index_at_diagnostic(
            context,
            parser_diagnostic,
            &token_index)) {
        return false;
    }

    previous = token_index != 0u
        ? context->lexer.tokens[token_index - 1u].kind
        : VITTE_TOKEN_INVALID;

    if (previous == VITTE_TOKEN_KW_AS &&
        vitte_main_parser_is_import_alias_as(context, token_index) &&
        (parser_diagnostic->expected == VITTE_TOKEN_IDENTIFIER ||
         parser_diagnostic->error == VITTE_PARSER_ERROR_EXPECTED_IDENTIFIER)) {
        *message = "expected a name after `as`";
        *help = "Provide the local name that should refer to the imported item.";
        return true;
    }

    if (previous == VITTE_TOKEN_KW_RETURN &&
        parser_diagnostic->error == VITTE_PARSER_ERROR_EXPECTED_EXPRESSION) {
        *message = "expected an expression after `return`";
        *help = "Write the return value after `return`, before the semicolon.";
        return true;
    }

    if (previous == VITTE_TOKEN_COLON &&
        (parser_diagnostic->error == VITTE_PARSER_ERROR_EXPECTED_TYPE ||
         parser_diagnostic->error == VITTE_PARSER_ERROR_EXPECTED_TOKEN ||
         parser_diagnostic->error == VITTE_PARSER_ERROR_EXPECTED_IDENTIFIER)) {
        *message = "expected a type after `:`";
        *help = "Write a type expression after the colon.";
        return true;
    }

    if (parser_diagnostic->expected == VITTE_TOKEN_LEFT_BRACE &&
        vitte_main_parser_declaration_expects_block(context, token_index)) {
        *message = "expected a block after this declaration";
        *help = "Start the declaration body with `{` and close it with `}`.";
        return true;
    }

    return false;
}

static bool
vitte_main_parser_matching_opener_location(
    const vitte_main_driver_context_t *context,
    const vitte_parser_diagnostic_t *parser_diagnostic,
    const char *source_name,
    vitte_cli_diagnostic_location_t *location)
{
    const vitte_token_t *opener;
    const char *label;
    size_t token_index;

    if (context == NULL ||
        parser_diagnostic == NULL ||
        source_name == NULL ||
        location == NULL ||
        parser_diagnostic->kind != VITTE_PARSER_DIAGNOSTIC_ERROR ||
        !vitte_main_parser_token_index_at_diagnostic(
            context,
            parser_diagnostic,
            &token_index)) {
        return false;
    }

    switch (parser_diagnostic->expected) {
        case VITTE_TOKEN_RIGHT_PAREN:
            opener = NULL;
            label = "opening `(` here; closing `)` expected";
            break;
        case VITTE_TOKEN_RIGHT_BRACKET:
            opener = NULL;
            label = "opening `[` here; closing `]` expected";
            break;
        case VITTE_TOKEN_RIGHT_BRACE:
            opener = NULL;
            label = "opening `{` here; closing `}` expected";
            break;
        default:
            opener = NULL;
            label = NULL;
            break;
    }

    if (parser_diagnostic->expected == VITTE_TOKEN_RIGHT_PAREN ||
        parser_diagnostic->expected == VITTE_TOKEN_RIGHT_BRACKET ||
        parser_diagnostic->expected == VITTE_TOKEN_RIGHT_BRACE) {
        vitte_token_kind_t opener_kind;
        size_t nesting;

        opener_kind = parser_diagnostic->expected == VITTE_TOKEN_RIGHT_PAREN
            ? VITTE_TOKEN_LEFT_PAREN
            : parser_diagnostic->expected == VITTE_TOKEN_RIGHT_BRACKET
                ? VITTE_TOKEN_LEFT_BRACKET
                : VITTE_TOKEN_LEFT_BRACE;
        nesting = 0u;
        while (token_index != 0u) {
            const vitte_token_t *token;

            token = &context->lexer.tokens[--token_index];
            if (token->kind == parser_diagnostic->expected) {
                ++nesting;
            } else if (token->kind == opener_kind) {
                if (nesting == 0u) {
                    opener = token;
                    break;
                }
                --nesting;
            }
        }
    } else if (parser_diagnostic->found == VITTE_TOKEN_RIGHT_PAREN ||
               parser_diagnostic->found == VITTE_TOKEN_RIGHT_BRACKET ||
               parser_diagnostic->found == VITTE_TOKEN_RIGHT_BRACE) {
        const vitte_token_t *candidates[3] = { NULL, NULL, NULL };
        size_t nesting[3] = { 0u, 0u, 0u };
        size_t candidate_index;
        size_t found_delimiter_index;
        size_t selected_index;

        found_delimiter_index =
            parser_diagnostic->found == VITTE_TOKEN_RIGHT_PAREN
                ? 0u
                : parser_diagnostic->found == VITTE_TOKEN_RIGHT_BRACKET
                    ? 1u
                    : 2u;

        while (token_index != 0u) {
            const vitte_token_t *token;
            size_t delimiter_index;

            token = &context->lexer.tokens[--token_index];
            switch (token->kind) {
                case VITTE_TOKEN_LEFT_PAREN:
                    delimiter_index = 0u;
                    if (nesting[delimiter_index] != 0u) {
                        --nesting[delimiter_index];
                    } else if (candidates[delimiter_index] == NULL) {
                        candidates[delimiter_index] = token;
                    }
                    break;
                case VITTE_TOKEN_RIGHT_PAREN:
                    ++nesting[0];
                    break;
                case VITTE_TOKEN_LEFT_BRACKET:
                    delimiter_index = 1u;
                    if (nesting[delimiter_index] != 0u) {
                        --nesting[delimiter_index];
                    } else if (candidates[delimiter_index] == NULL) {
                        candidates[delimiter_index] = token;
                    }
                    break;
                case VITTE_TOKEN_RIGHT_BRACKET:
                    ++nesting[1];
                    break;
                case VITTE_TOKEN_LEFT_BRACE:
                    delimiter_index = 2u;
                    if (nesting[delimiter_index] != 0u) {
                        --nesting[delimiter_index];
                    } else if (candidates[delimiter_index] == NULL) {
                        candidates[delimiter_index] = token;
                    }
                    break;
                case VITTE_TOKEN_RIGHT_BRACE:
                    ++nesting[2];
                    break;
                default:
                    break;
            }
        }

        if (candidates[found_delimiter_index] != NULL) {
            return false;
        }

        selected_index = 3u;
        for (candidate_index = 0u; candidate_index < 3u; ++candidate_index) {
            if (candidates[candidate_index] != NULL &&
                (selected_index == 3u ||
                 candidates[candidate_index]->span.begin >
                    candidates[selected_index]->span.begin)) {
                selected_index = candidate_index;
            }
        }
        if (selected_index != 3u) {
            opener = candidates[selected_index];
            label = selected_index == 0u
                ? "unmatched opening `(` here; closing `)` expected"
                : selected_index == 1u
                    ? "unmatched opening `[` here; closing `]` expected"
                    : "unmatched opening `{` here; closing `}` expected";
        }
    }

    if (opener == NULL || label == NULL || !opener->span.valid) {
        return false;
    }

    {
        vitte_source_location_t start;
        vitte_source_location_t end;

        if (!vitte_source_location_from_offset(
                &context->sources,
                context->source_id,
                opener->span.begin,
                &start) ||
            !start.valid ||
            !vitte_source_location_from_offset(
                &context->sources,
                context->source_id,
                opener->span.end,
                &end) ||
            !end.valid) {
            return false;
        }

        memset(location, 0, sizeof(*location));
        location->kind = VITTE_CLI_DIAGNOSTIC_LOCATION_RELATED;
        location->label = label;
        location->span.valid = true;
        location->span.file_id = opener->span.file_id;
        location->span.source_id = context->source_id;
        location->span.source_name = source_name;
        location->span.start_offset = opener->span.begin;
        location->span.end_offset = opener->span.end;
        location->span.start_line = start.line - 1u;
        location->span.start_column = start.column - 1u;
        location->span.end_line = end.line - 1u;
        location->span.end_column = end.column - 1u;
    }
    return true;
}

bool
vitte_main_render_parser_diagnostics(
    const vitte_main_driver_context_t *context,
    bool *rendered)
{
    vitte_cli_diagnostic_t *diagnostics;
    vitte_cli_diagnostic_suggestion_t *suggestions;
    vitte_cli_diagnostic_location_t *related_locations;
    const vitte_source_entry_t *source;
    const char *source_name;
    size_t diagnostic_count;
    size_t index;

    if (context == NULL ||
        rendered == NULL) {
        return false;
    }

    *rendered = false;
    diagnostic_count = vitte_parser_diagnostic_count(&context->parser);
    if (diagnostic_count == 0u) {
        return true;
    }

    diagnostics = (vitte_cli_diagnostic_t *)calloc(
        diagnostic_count,
        sizeof(*diagnostics));
    suggestions = (vitte_cli_diagnostic_suggestion_t *)calloc(
        diagnostic_count,
        sizeof(*suggestions));
    related_locations = (vitte_cli_diagnostic_location_t *)calloc(
        diagnostic_count,
        sizeof(*related_locations));
    if (diagnostics == NULL || suggestions == NULL ||
        related_locations == NULL) {
        free(diagnostics);
        free(suggestions);
        free(related_locations);
        return false;
    }

    source = vitte_source_get(
        &context->sources,
        context->source_id);
    source_name = source != NULL
        ? source->name
        : context->options->input_path;

    for (index = 0u; index < diagnostic_count; ++index) {
        const vitte_parser_diagnostic_t *parser_diagnostic;
        vitte_cli_diagnostic_t *diagnostic;
        const char *details;
        const char *contextual_message;
        const char *contextual_help;
        int details_length;

        parser_diagnostic = vitte_parser_diagnostic_at(
            &context->parser,
            index);
        if (parser_diagnostic == NULL) {
            free(diagnostics);
            free(suggestions);
            free(related_locations);
            return false;
        }

        diagnostic = &diagnostics[index];
        if (!vitte_main_make_cli_diagnostic(
                context,
                diagnostic,
                vitte_main_parser_diagnostic_severity(
                    parser_diagnostic->kind),
                vitte_main_parser_diagnostic_code(
                    parser_diagnostic->error),
                NULL,
                NULL,
                &parser_diagnostic->span,
                NULL,
                NULL)) {
            free(diagnostics);
            free(suggestions);
            free(related_locations);
            return false;
        }

        details_length = 0;
        if (parser_diagnostic->expected != VITTE_TOKEN_INVALID &&
            parser_diagnostic->found != VITTE_TOKEN_INVALID) {
            details_length = snprintf(
                diagnostic->details_storage,
                sizeof(diagnostic->details_storage),
                "expected %s, found %s",
                vitte_token_kind_name(parser_diagnostic->expected),
                vitte_token_kind_name(parser_diagnostic->found));
        } else if (parser_diagnostic->expected != VITTE_TOKEN_INVALID) {
            details_length = snprintf(
                diagnostic->details_storage,
                sizeof(diagnostic->details_storage),
                "expected %s",
                vitte_token_kind_name(parser_diagnostic->expected));
        } else if (parser_diagnostic->found != VITTE_TOKEN_INVALID) {
            details_length = snprintf(
                diagnostic->details_storage,
                sizeof(diagnostic->details_storage),
                "found %s",
                vitte_token_kind_name(parser_diagnostic->found));
        }

        if (details_length < 0 ||
            (size_t)details_length >=
                sizeof(diagnostic->details_storage)) {
            free(diagnostics);
            free(suggestions);
            free(related_locations);
            return false;
        }

        details = diagnostic->details_storage[0] != '\0'
            ? diagnostic->details_storage
            : NULL;
        diagnostic->details = details;
        if (parser_diagnostic->expected != VITTE_TOKEN_INVALID) {
            diagnostic->expected = vitte_token_kind_name(
                parser_diagnostic->expected);
        }
        if (parser_diagnostic->found != VITTE_TOKEN_INVALID) {
            diagnostic->found = vitte_token_kind_name(
                parser_diagnostic->found);
        }
        if (vitte_main_parser_contextual_expectation(
                context,
                parser_diagnostic,
                &contextual_message,
                &contextual_help)) {
            diagnostic->message = contextual_message;
            diagnostic->help = contextual_help;
        }
        vitte_main_parser_add_grammar_migration_suggestion(
            context,
            parser_diagnostic,
            source_name,
            diagnostic,
            &suggestions[index]);
        if (diagnostic->suggestion_count == 0u) {
            vitte_main_parser_add_punctuation_suggestion(
                context,
                parser_diagnostic,
                source_name,
                diagnostic,
                &suggestions[index]);
        }
        if (vitte_main_parser_matching_opener_location(
                context,
                parser_diagnostic,
                source_name,
                &related_locations[index])) {
            diagnostic->related = &related_locations[index];
            diagnostic->related_count = 1u;
        }
        if (diagnostic->suggestion_count != 0u) {
            size_t suggestion_index;

            for (suggestion_index = 0u;
                 suggestion_index < diagnostic->suggestion_count;
                 ++suggestion_index) {
                vitte_main_diagnostic_map_span(
                    context,
                    &suggestions[index].span);
            }
        }
        if (diagnostic->related_count != 0u) {
            size_t related_index;

            for (related_index = 0u;
                 related_index < diagnostic->related_count;
                 ++related_index) {
                vitte_main_diagnostic_map_span(
                    context,
                    &related_locations[index].span);
            }
        }
    }

    *rendered = vitte_diagnostic_render_cli_batch(
        stderr,
        &context->sources,
        context->source_id,
        source_name,
        diagnostics,
        diagnostic_count,
        context->options->color,
        vitte_main_cli_diagnostic_format(
            context->options->diagnostic_format));
    free(diagnostics);
    free(suggestions);
    free(related_locations);
    return *rendered;
}

bool
vitte_main_driver_parse(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    const vitte_driver_input_t *input,
    void *user_data)
{
    vitte_main_driver_context_t *context;

    (void)driver;
    (void)phase;
    (void)input;

    context = (vitte_main_driver_context_t *)user_data;

    if (context == NULL) {
        return false;
    }

    if (context->native_frontend_completed) {
        return true;
    }
    if (!context->lexer_initialized) {
        return false;
    }

    vitte_main_verbose(context->options, "parsing");

    {
        vitte_macro_expansion_result_t expansion;
        if (!vitte_macro_expand_tokens(context->lexer.tokens,
                context->lexer.token_count, &expansion)) {
            const char *code = expansion.error == VITTE_MACRO_EXPANSION_ERROR_RECURSION
                ? "E1802" : "E1801";
            const char *message = expansion.error == VITTE_MACRO_EXPANSION_ERROR_RECURSION
                ? "macro expansion recursion limit reached" : "macro expansion failed";
            const char *details = expansion.error == VITTE_MACRO_EXPANSION_ERROR_RECURSION
                ? "A macro expands itself or exceeds the supported expansion depth."
                : expansion.error == VITTE_MACRO_EXPANSION_ERROR_INVALID_INVOCATION
                    ? "Check that the macro exists and that its invocation supplies the declared number of arguments."
                    : "Macro expansion exceeded an implementation resource limit.";
            vitte_parser_span_t span;
            span.file_id = expansion.error_span.file_id;
            span.begin = expansion.error_span.begin;
            span.end = expansion.error_span.end;
            span.line = 0u;
            span.column = 0u;
            span.valid = expansion.error_span.valid;
            (void)vitte_main_render_diagnostic(context, VITTE_CLI_DIAGNOSTIC_ERROR,
                code, message, details, &span, NULL, "macro expansion");
            vitte_macro_expansion_result_destroy(&expansion);
            return false;
        }
        context->expanded_tokens = expansion.tokens;
        context->expanded_token_count = expansion.token_count;
    }

    if (!vitte_parser_init(
            &context->parser,
            context->expanded_tokens,
            context->expanded_token_count)) {
        vitte_main_error("failed to initialize parser");
        return false;
    }

    context->parser_initialized = true;

    if (!vitte_parser_run(&context->parser)) {
        bool rendered;

        if (!vitte_main_render_parser_diagnostics(
                context,
                &rendered)) {
            vitte_main_error(
                "failed to render parser diagnostics");
            return false;
        }

        if (!rendered) {
            if (!vitte_main_render_diagnostic(
                    context,
                    VITTE_CLI_DIAGNOSTIC_ERROR,
                    "VITTE_INFRA_E_PHASE",
                    "parsing failed",
                    vitte_parser_error_name(
                        vitte_parser_last_error(&context->parser)),
                    NULL,
                    NULL,
                    NULL)) {
                vitte_main_error(
                    "failed to render parser failure");
            }
        }
        return false;
    }

    /* Re-check the completed AST at the driver boundary so later phases
       never consume a tree that changed after parser finalization. */
    if (!vitte_parser_validate(&context->parser)) {
        vitte_main_error("AST validation failed after parsing");
        return false;
    }

    if (context->options->emit_ast) {
        const vitte_ast_node_t *root;

        root = vitte_parser_get_node(
            &context->parser,
            vitte_parser_root(&context->parser));

        if (root == NULL) {
            vitte_main_error("parser produced no translation unit");
            return false;
        }

        (void)printf(
            "AST root kind: %u\n"
            "nodes: %zu\n",
            (unsigned int)root->kind,
            context->parser.node_count);
    }

    return true;
}

static const char *
vitte_main_sema_diagnostic_code(
    vitte_sema_diagnostic_code_t code)
{
    switch (code) {
        case VITTE_SEMA_DIAGNOSTIC_UNRESOLVED_NAME:
            return "VITTE_RESOLVE_E_UNKNOWN_SYMBOL";

        case VITTE_SEMA_DIAGNOSTIC_PRIVATE_SYMBOL:
            return "VITTE_RESOLVE_E_PRIVATE";

        case VITTE_SEMA_DIAGNOSTIC_DUPLICATE_DECLARATION:
            return "VITTE_RESOLVE_E_REDECLARATION";

        case VITTE_SEMA_DIAGNOSTIC_SHADOWED_DECLARATION:
            return "VITTE_WARNING_SHADOWING";

        case VITTE_SEMA_DIAGNOSTIC_NOT_CALLABLE:
            return "VITTE_TYPE_E_NOT_CALLABLE";

        case VITTE_SEMA_DIAGNOSTIC_INVALID_ARGUMENT_COUNT:
            return "VITTE_CALL_E_ARITY";

        case VITTE_SEMA_DIAGNOSTIC_INVALID_ARGUMENT_TYPE:
            return "VITTE_CALL_E_ARGUMENT_TYPE";

        case VITTE_SEMA_DIAGNOSTIC_UNKNOWN_TYPE:
            return "VITTE_TYPE_E_UNKNOWN_TYPE";

        case VITTE_SEMA_DIAGNOSTIC_NOT_A_TYPE:
            return "VITTE_TYPE_E_NOT_A_TYPE";

        case VITTE_SEMA_DIAGNOSTIC_INVALID_UNARY_OPERAND:
        case VITTE_SEMA_DIAGNOSTIC_INVALID_BINARY_OPERANDS:
            return "VITTE_TYPE_E_INVALID_OPERAND";

        case VITTE_SEMA_DIAGNOSTIC_NOT_ASSIGNABLE:
            return "VITTE_TYPE_E_NOT_ASSIGNABLE";

        case VITTE_SEMA_DIAGNOSTIC_NOT_INDEXABLE:
            return "VITTE_TYPE_E_NOT_INDEXABLE";

        case VITTE_SEMA_DIAGNOSTIC_INVALID_INDEX_TYPE:
            return "VITTE_TYPE_E_INVALID_INDEX";

        case VITTE_SEMA_DIAGNOSTIC_UNKNOWN_MEMBER:
            return "VITTE_TYPE_E_NO_MEMBER";

        case VITTE_SEMA_DIAGNOSTIC_CONTRACT_REQUIRES:
            return "VITTE_CONTRACT_E_REQUIRES";
        case VITTE_SEMA_DIAGNOSTIC_CONTRACT_ENSURES:
            return "VITTE_CONTRACT_E_ENSURES";
        case VITTE_SEMA_DIAGNOSTIC_CONTRACT_RESULT:
            return "VITTE_CONTRACT_E_RESULT";
        case VITTE_SEMA_DIAGNOSTIC_CONTRACT_CONTRADICTION:
            return "VITTE_CONTRACT_E_CONTRADICTION";
        case VITTE_SEMA_DIAGNOSTIC_CONTRACT_POSTCONDITION:
            return "VITTE_CONTRACT_E_POSTCONDITION";
        case VITTE_SEMA_DIAGNOSTIC_MISSING_RETURN:
            return "VITTE_CONTROL_E_MISSING_RETURN";
        case VITTE_SEMA_DIAGNOSTIC_CONTRACT_PRECONDITION:
            return "VITTE_CONTRACT_E_PRECONDITION";
        case VITTE_SEMA_DIAGNOSTIC_CONSTANT_OVERFLOW:
            return "VITTE_CONST_E_OVERFLOW";
        case VITTE_SEMA_DIAGNOSTIC_CONSTANT_DIVIDE_BY_ZERO:
            return "VITTE_CONST_E_DIVIDE_BY_ZERO";
        case VITTE_SEMA_DIAGNOSTIC_INDEX_OUT_OF_BOUNDS:
            return "VITTE_MEMORY_E_INVALID_ACCESS";
        case VITTE_SEMA_DIAGNOSTIC_CONTRACT_EFFECT:
            return "VITTE_CONTRACT_E_EFFECT";
        case VITTE_SEMA_DIAGNOSTIC_MATCH_NON_EXHAUSTIVE:
            return "VITTE_PATTERN_E_NON_EXHAUSTIVE";
        case VITTE_SEMA_DIAGNOSTIC_MATCH_UNREACHABLE:
            return "VITTE_PATTERN_E_UNREACHABLE";
        case VITTE_SEMA_DIAGNOSTIC_CONSTANT_CYCLE:
            return "VITTE_CONST_E_CYCLE";
        case VITTE_SEMA_DIAGNOSTIC_MATCH_TYPE:
            return "VITTE_PATTERN_E_TYPE";
        case VITTE_SEMA_DIAGNOSTIC_UNSAFE_REQUIRED:
            return "VITTE_UNSAFE_E_REQUIRED";
        case VITTE_SEMA_DIAGNOSTIC_REFERENCE_ESCAPE:
            return "VITTE_MEMORY_E_REFERENCE_ESCAPE";
        case VITTE_SEMA_DIAGNOSTIC_BORROW_CONFLICT:
            return "VITTE_MEMORY_E_BORROW_CONFLICT";
        case VITTE_SEMA_DIAGNOSTIC_BORROWED_MUTATION:
            return "VITTE_MEMORY_E_BORROWED_MUTATION";
        case VITTE_SEMA_DIAGNOSTIC_MOVE_BORROWED:
            return "VITTE_MEMORY_E_MOVE_BORROWED";
        case VITTE_SEMA_DIAGNOSTIC_USE_AFTER_MOVE:
            return "VITTE_MEMORY_E_USE_AFTER_MOVE";
        case VITTE_SEMA_DIAGNOSTIC_CONDITION_NOT_BOOL:
            return "VITTE_TYPE_E_CONDITION_NOT_BOOL";

        case VITTE_SEMA_DIAGNOSTIC_RETURN_TYPE_MISMATCH:
            return "VITTE_CALL_E_RETURN_TYPE";

        case VITTE_SEMA_DIAGNOSTIC_RETURN_OUTSIDE_PROC:
            return "VITTE_CALL_E_RETURN_OUTSIDE_PROC";

        case VITTE_SEMA_DIAGNOSTIC_BREAK_OUTSIDE_LOOP:
            return "VITTE_CONTROL_E_INVALID_BREAK";

        case VITTE_SEMA_DIAGNOSTIC_CONTINUE_OUTSIDE_LOOP:
            return "VITTE_CONTROL_E_INVALID_CONTINUE";

        case VITTE_SEMA_DIAGNOSTIC_UNREACHABLE_CODE:
            return "VITTE_WARNING_UNREACHABLE_CODE";

        case VITTE_SEMA_DIAGNOSTIC_RECURSION_LIMIT:
            return "VITTE_INFRA_E_LIMIT";

        case VITTE_SEMA_DIAGNOSTIC_NONE:
        case VITTE_SEMA_DIAGNOSTIC_TYPE_MISMATCH:
        case VITTE_SEMA_DIAGNOSTIC_INVALID_TYPE:
            return "VITTE_TYPE_E_MISMATCH";

        case VITTE_SEMA_DIAGNOSTIC_CODE_COUNT:
            return "VITTE_INFRA_E_FALLBACK";
    }

    return "VITTE_INFRA_E_FALLBACK";
}

static vitte_cli_diagnostic_severity_t
vitte_main_sema_diagnostic_severity(
    vitte_sema_diagnostic_kind_t kind)
{
    switch (kind) {
        case VITTE_SEMA_DIAGNOSTIC_NOTE:
            return VITTE_CLI_DIAGNOSTIC_NOTE;

        case VITTE_SEMA_DIAGNOSTIC_HELP:
            return VITTE_CLI_DIAGNOSTIC_HELP;

        case VITTE_SEMA_DIAGNOSTIC_WARNING:
            return VITTE_CLI_DIAGNOSTIC_WARNING;

        case VITTE_SEMA_DIAGNOSTIC_ERROR:
        case VITTE_SEMA_DIAGNOSTIC_KIND_COUNT:
            return VITTE_CLI_DIAGNOSTIC_ERROR;
    }

    return VITTE_CLI_DIAGNOSTIC_ERROR;
}

#define VITTE_MAIN_SEMA_TYPE_NAME_CAPACITY 512u

static bool
vitte_main_sema_format_type_inner(
    const vitte_sema_t *sema,
    vitte_sema_type_id_t type_id,
    char *buffer,
    size_t capacity,
    size_t depth)
{
    const vitte_sema_type_t *type;
    const vitte_symbol_t *symbol;
    char element[VITTE_MAIN_SEMA_TYPE_NAME_CAPACITY];
    const char *name;
    int length;

    if (sema == NULL || buffer == NULL || capacity == 0u ||
        type_id == VITTE_SEMA_INVALID_TYPE_ID) {
        return false;
    }
    buffer[0] = '\0';
    if (depth >= 16u) {
        length = snprintf(buffer, capacity, "…");
        return length >= 0 && (size_t)length < capacity;
    }

    type = vitte_sema_get_type(sema, type_id);
    if (type == NULL) {
        return false;
    }

    switch (type->kind) {
        case VITTE_SEMA_TYPE_POINTER:
        case VITTE_SEMA_TYPE_REFERENCE:
        case VITTE_SEMA_TYPE_ARRAY:
        case VITTE_SEMA_TYPE_SLICE:
        case VITTE_SEMA_TYPE_RANGE:
            if (!vitte_main_sema_format_type_inner(
                    sema,
                    type->element_type,
                    element,
                    sizeof(element),
                    depth + 1u)) {
                return false;
            }
            if (type->kind == VITTE_SEMA_TYPE_POINTER) {
                name = type->is_mutable ? "*mut " : "*";
                length = snprintf(buffer, capacity, "%s%s", name, element);
            } else if (type->kind == VITTE_SEMA_TYPE_REFERENCE) {
                name = type->is_mutable ? "&mut " : "&";
                length = snprintf(buffer, capacity, "%s%s", name, element);
            } else if (type->kind == VITTE_SEMA_TYPE_SLICE) {
                length = snprintf(buffer, capacity, "[%s]", element);
            } else if (type->kind == VITTE_SEMA_TYPE_ARRAY &&
                       type->array_length != 0u) {
                length = snprintf(
                    buffer,
                    capacity,
                    "[%s; %zu]",
                    element,
                    type->array_length);
            } else if (type->kind == VITTE_SEMA_TYPE_ARRAY) {
                length = snprintf(buffer, capacity, "array<%s>", element);
            } else {
                length = snprintf(buffer, capacity, "range<%s>", element);
            }
            return length >= 0 && (size_t)length < capacity;

        case VITTE_SEMA_TYPE_FUNCTION:
            if (type->return_type == VITTE_SEMA_INVALID_TYPE_ID) {
                length = snprintf(buffer, capacity, "proc()");
            } else if (!vitte_main_sema_format_type_inner(
                           sema,
                           type->return_type,
                           element,
                           sizeof(element),
                           depth + 1u)) {
                return false;
            } else {
                length = snprintf(buffer, capacity, "proc() -> %s", element);
            }
            return length >= 0 && (size_t)length < capacity;

        case VITTE_SEMA_TYPE_NAMED:
        case VITTE_SEMA_TYPE_DYN:
            symbol = vitte_scope_get_symbol(&sema->scopes, type->symbol_id);
            if (symbol != NULL && symbol->name != NULL &&
                symbol->name_length != 0u) {
                length = snprintf(
                    buffer,
                    capacity,
                    type->kind == VITTE_SEMA_TYPE_DYN
                        ? "dyn %.*s"
                        : "%.*s",
                    (int)(symbol->name_length > (size_t)INT32_MAX
                        ? (size_t)INT32_MAX
                        : symbol->name_length),
                    symbol->name);
                if (length >= 0 &&
                    (size_t)length < capacity &&
                    type->kind == VITTE_SEMA_TYPE_NAMED &&
                    type->generic_argument_count != 0u) {
                    size_t offset = (size_t)length;
                    size_t argument_index;
                    for (argument_index = 0u;
                         argument_index < type->generic_argument_count;
                         ++argument_index) {
                        char argument[VITTE_MAIN_SEMA_TYPE_NAME_CAPACITY];
                        int written;
                        if (!vitte_main_sema_format_type_inner(
                                sema,
                                type->generic_arguments[argument_index],
                                argument,
                                sizeof(argument),
                                depth + 1u)) {
                            return false;
                        }
                        written = snprintf(
                            buffer + offset,
                            capacity - offset,
                            argument_index == 0u ? "<%s" : ", %s",
                            argument);
                        if (written < 0 ||
                            (size_t)written >= capacity - offset) {
                            return false;
                        }
                        offset += (size_t)written;
                    }
                    if (offset + 1u >= capacity) {
                        return false;
                    }
                    buffer[offset++] = '>';
                    buffer[offset] = '\0';
                    length = (int)offset;
                }
            } else {
                name = vitte_sema_type_kind_name(type->kind);
                length = snprintf(buffer, capacity, "%s", name);
            }
            return length >= 0 && (size_t)length < capacity;

        default:
            name = vitte_sema_type_kind_name(type->kind);
            length = snprintf(buffer, capacity, "%s", name);
            return length >= 0 && (size_t)length < capacity;
    }
}

static bool
vitte_main_sema_format_type(
    const vitte_sema_t *sema,
    vitte_sema_type_id_t type_id,
    char *buffer,
    size_t capacity)
{
    return vitte_main_sema_format_type_inner(
        sema,
        type_id,
        buffer,
        capacity,
        0u);
}

static bool
vitte_main_origin_offset(
    const vitte_main_driver_context_t *context,
    size_t combined_offset,
    bool end_position,
    const vitte_main_source_origin_t **origin,
    size_t *source_offset)
{
    size_t index;

    if (context == NULL || origin == NULL || source_offset == NULL) {
        return false;
    }

    for (index = 0u; index < context->origin_count; ++index) {
        const vitte_main_source_origin_t *candidate;
        bool matches;

        candidate = &context->origins[index];
        matches = end_position
            ? combined_offset > candidate->combined_begin &&
                combined_offset <= candidate->combined_end
            : combined_offset >= candidate->combined_begin &&
                combined_offset < candidate->combined_end;
        if (matches && candidate->source_id != VITTE_SOURCE_INVALID_ID) {
            *origin = candidate;
            *source_offset = candidate->source_begin +
                (combined_offset - candidate->combined_begin);
            return true;
        }
    }

    return false;
}

static bool
vitte_main_origin_span(
    const vitte_main_driver_context_t *context,
    size_t combined_begin,
    size_t combined_end,
    vitte_source_id_t *source_id,
    size_t *source_begin,
    size_t *source_end)
{
    const vitte_main_source_origin_t *begin_origin;
    const vitte_main_source_origin_t *end_origin;
    size_t mapped_begin;
    size_t mapped_end;

    if (context == NULL || source_id == NULL ||
        source_begin == NULL || source_end == NULL ||
        combined_begin > combined_end) {
        return false;
    }

    begin_origin = NULL;
    end_origin = NULL;
    if (combined_begin == combined_end) {
        if (!vitte_main_origin_offset(
                context,
                combined_begin,
                false,
                &begin_origin,
                &mapped_begin) &&
            !vitte_main_origin_offset(
                context,
                combined_begin,
                true,
                &begin_origin,
                &mapped_begin)) {
            return false;
        }
        end_origin = begin_origin;
        mapped_end = mapped_begin;
    } else if (!vitte_main_origin_offset(
                   context,
                   combined_begin,
                   false,
                   &begin_origin,
                   &mapped_begin) ||
               !vitte_main_origin_offset(
                   context,
                   combined_end,
                   true,
                   &end_origin,
                   &mapped_end)) {
        return false;
    }

    if (begin_origin == NULL || end_origin == NULL ||
        begin_origin->source_id == VITTE_SOURCE_INVALID_ID ||
        begin_origin->source_id != end_origin->source_id ||
        mapped_begin > mapped_end) {
        return false;
    }

    *source_id = begin_origin->source_id;
    *source_begin = mapped_begin;
    *source_end = mapped_end;
    return true;
}

static void
vitte_main_diagnostic_map_span(
    const vitte_main_driver_context_t *context,
    vitte_cli_diagnostic_span_t *span)
{
    const vitte_source_entry_t *source;
    vitte_source_id_t source_id;
    vitte_source_location_t start;
    vitte_source_location_t end;
    size_t source_begin;
    size_t source_end;

    if (context == NULL || span == NULL || !span->valid ||
        (span->source_id != VITTE_SOURCE_INVALID_ID &&
         span->source_id != context->source_id) ||
        !vitte_main_origin_span(
            context,
            span->start_offset,
            span->end_offset,
            &source_id,
            &source_begin,
            &source_end)) {
        return;
    }

    source = vitte_source_get(&context->sources, source_id);
    if (source == NULL ||
        !vitte_source_location_from_offset(
            &context->sources,
            source_id,
            source_begin,
            &start) ||
        !start.valid ||
        !vitte_source_location_from_offset(
            &context->sources,
            source_id,
            source_end,
            &end) ||
        !end.valid) {
        return;
    }

    span->file_id = (uint32_t)source_id;
    span->source_id = source_id;
    span->source_name = source->path != NULL
        ? source->path
        : source->name;
    span->start_offset = source_begin;
    span->end_offset = source_end;
    span->start_line = start.line - 1u;
    span->start_column = start.column - 1u;
    span->end_line = end.line - 1u;
    span->end_column = end.column - 1u;
}

#ifdef VITTE_NATIVE_FRONTEND_LINKED
static void
vitte_main_diagnostic_map_native_batch(
    const vitte_main_driver_context_t *context,
    vitte_cli_diagnostic_t *diagnostics,
    size_t diagnostic_count)
{
    size_t index;

    if (context == NULL || diagnostics == NULL) {
        return;
    }
    for (index = 0u; index < diagnostic_count; ++index) {
        vitte_cli_diagnostic_t *diagnostic;
        vitte_cli_diagnostic_span_t span;

        diagnostic = &diagnostics[index];
        if (diagnostic->expected != NULL && diagnostic->found != NULL &&
            (strcmp(diagnostic->code, "VITTE_CALL_E_ARGUMENT_TYPE") == 0 ||
             strcmp(diagnostic->code, "VITTE_TYPE_E_MISMATCH") == 0 ||
             strcmp(diagnostic->code, "VITTE_CALL_E_RETURN_TYPE") == 0 ||
             strcmp(diagnostic->code, "VITTE_TYPE_E_INVALID_OPERAND") == 0)) {
            diagnostic->expected_type = diagnostic->expected;
            diagnostic->actual_type = diagnostic->found;
        }
        if (!diagnostic->has_span) {
            continue;
        }
        memset(&span, 0, sizeof(span));
        span.valid = true;
        span.file_id = diagnostic->file_id;
        span.source_id = diagnostic->source_id;
        span.source_name = diagnostic->source_name;
        span.start_offset = diagnostic->start_offset;
        span.end_offset = diagnostic->end_offset;
        span.start_line = diagnostic->start_line;
        span.start_column = diagnostic->start_column;
        span.end_line = diagnostic->end_line;
        span.end_column = diagnostic->end_column;
        vitte_main_diagnostic_map_span(context, &span);
        diagnostic->file_id = span.file_id;
        diagnostic->source_id = span.source_id;
        diagnostic->source_name = span.source_name;
        diagnostic->start_offset = span.start_offset;
        diagnostic->end_offset = span.end_offset;
        diagnostic->start_line = span.start_line;
        diagnostic->start_column = span.start_column;
        diagnostic->end_line = span.end_line;
        diagnostic->end_column = span.end_column;
    }
}
#endif

static bool
vitte_main_sema_symbol_origin_location(
    const vitte_main_driver_context_t *context,
    const vitte_sema_diagnostic_t *sema_diagnostic,
    const char *source_name,
    vitte_cli_diagnostic_t *diagnostic,
    vitte_cli_diagnostic_location_t *location)
{
    const vitte_symbol_t *symbol;
    const vitte_source_entry_t *origin_source;
    vitte_source_location_t start;
    vitte_source_location_t end;
    vitte_source_id_t origin_source_id;
    vitte_scope_span_t declaration_span;
    const vitte_ast_node_t *declaration;
    size_t origin_begin;
    size_t origin_end;
    const vitte_main_source_origin_t *begin_origin;
    const vitte_main_source_origin_t *end_origin;
    const char *label;
    vitte_symbol_id_t origin_symbol_id;
    bool type_origin;

    if (context == NULL || sema_diagnostic == NULL ||
        diagnostic == NULL || location == NULL) {
        return false;
    }

    type_origin =
        sema_diagnostic->context_symbol_id !=
            VITTE_SCOPE_INVALID_SYMBOL_ID &&
        (sema_diagnostic->code == VITTE_SEMA_DIAGNOSTIC_TYPE_MISMATCH ||
         sema_diagnostic->code == VITTE_SEMA_DIAGNOSTIC_INVALID_ARGUMENT_TYPE);
    if (sema_diagnostic->code != VITTE_SEMA_DIAGNOSTIC_PRIVATE_SYMBOL &&
        sema_diagnostic->code != VITTE_SEMA_DIAGNOSTIC_DUPLICATE_DECLARATION &&
        !type_origin) {
        return false;
    }
    if (!type_origin &&
        sema_diagnostic->symbol_id == VITTE_SCOPE_INVALID_SYMBOL_ID) {
        return false;
    }

    origin_symbol_id = type_origin
        ? sema_diagnostic->context_symbol_id
        : sema_diagnostic->symbol_id;

    symbol = vitte_scope_get_symbol(
        &context->sema.scopes,
        origin_symbol_id);
    if (symbol == NULL || !symbol->span.valid ||
        symbol->span.begin > symbol->span.end) {
        return false;
    }

    declaration_span = symbol->span;
    declaration = (const vitte_ast_node_t *)symbol->payload;
    if (declaration != NULL && declaration->span.valid &&
        declaration->span.begin <= declaration->span.end) {
        declaration_span.file_id = declaration->span.file_id;
        declaration_span.begin = declaration->span.begin;
        declaration_span.end = declaration->span.end;
    }

    begin_origin = NULL;
    end_origin = NULL;
    origin_begin = declaration_span.begin;
    origin_end = declaration_span.end;
    origin_source_id = (vitte_source_id_t)declaration_span.file_id;
    if (vitte_main_origin_offset(
            context,
            declaration_span.begin,
            false,
            &begin_origin,
            &origin_begin) &&
        vitte_main_origin_offset(
            context,
            declaration_span.end,
            true,
            &end_origin,
            &origin_end) &&
        begin_origin->source_id == end_origin->source_id) {
        origin_source_id = begin_origin->source_id;
    } else {
        origin_begin = declaration_span.begin;
        origin_end = declaration_span.end;
    }
    origin_source = origin_source_id != VITTE_SOURCE_INVALID_ID
        ? vitte_source_get(&context->sources, origin_source_id)
        : NULL;
    if (origin_source == NULL &&
        declaration_span.file_id == sema_diagnostic->span.file_id) {
        origin_source_id = context->source_id;
        origin_source = vitte_source_get(
            &context->sources,
            origin_source_id);
    }
    if (origin_source == NULL ||
        !vitte_source_location_from_offset(
            &context->sources,
            origin_source_id,
            origin_begin,
            &start) ||
        !start.valid ||
        !vitte_source_location_from_offset(
            &context->sources,
            origin_source_id,
            origin_end,
            &end) ||
        !end.valid) {
        return false;
    }

    label = sema_diagnostic->code ==
            VITTE_SEMA_DIAGNOSTIC_PRIVATE_SYMBOL
        ? "private declaration is here"
        : sema_diagnostic->code ==
              VITTE_SEMA_DIAGNOSTIC_DUPLICATE_DECLARATION
            ? "conflicting declaration is here"
            : sema_diagnostic->type_context ==
                  VITTE_SEMA_TYPE_CONTEXT_ARGUMENT
                ? "parameter declared here"
                : sema_diagnostic->type_context ==
                      VITTE_SEMA_TYPE_CONTEXT_ASSIGNMENT
                    ? "assignment target declared here"
                    : "field declared here";
    memset(location, 0, sizeof(*location));
    location->kind = VITTE_CLI_DIAGNOSTIC_LOCATION_RELATED;
    location->label = label;
    location->span.valid = true;
    location->span.file_id = (uint32_t)origin_source_id;
    location->span.source_id = origin_source_id;
    location->span.source_name = origin_source->path != NULL
        ? origin_source->path
        : origin_source->name != NULL
            ? origin_source->name
            : source_name;
    location->span.start_offset = origin_begin;
    location->span.end_offset = origin_end;
    location->span.start_line = start.line - 1u;
    location->span.start_column = start.column - 1u;
    location->span.end_line = end.line - 1u;
    location->span.end_column = end.column - 1u;
    diagnostic->symbol = symbol->name;
    return true;
}

static bool
vitte_main_sema_mutable_binding_location(
    const vitte_main_driver_context_t *context,
    const vitte_sema_diagnostic_t *sema_diagnostic,
    const char *source_name,
    vitte_cli_diagnostic_t *diagnostic,
    vitte_cli_diagnostic_location_t *location)
{
    const vitte_symbol_t *symbol;
    vitte_source_location_t start;
    vitte_source_location_t end;

    if (context == NULL || sema_diagnostic == NULL ||
        source_name == NULL || diagnostic == NULL || location == NULL ||
        sema_diagnostic->code != VITTE_SEMA_DIAGNOSTIC_NOT_ASSIGNABLE ||
        sema_diagnostic->symbol_id == VITTE_SCOPE_INVALID_SYMBOL_ID) {
        return false;
    }

    symbol = vitte_scope_get_symbol(
        &context->sema.scopes,
        sema_diagnostic->symbol_id);
    if (symbol == NULL || !symbol->span.valid ||
        symbol->span.file_id != sema_diagnostic->span.file_id ||
        symbol->span.begin > symbol->span.end ||
        !vitte_source_location_from_offset(
            &context->sources,
            context->source_id,
            symbol->span.begin,
            &start) ||
        !start.valid ||
        !vitte_source_location_from_offset(
            &context->sources,
            context->source_id,
            symbol->span.end,
            &end) ||
        !end.valid) {
        return false;
    }

    memset(location, 0, sizeof(*location));
    location->kind = VITTE_CLI_DIAGNOSTIC_LOCATION_RELATED;
    location->label = "binding declared here; make it mutable";
    location->span.valid = true;
    location->span.file_id = symbol->span.file_id;
    location->span.source_id = context->source_id;
    location->span.source_name = source_name;
    location->span.start_offset = symbol->span.begin;
    location->span.end_offset = symbol->span.end;
    location->span.start_line = start.line - 1u;
    location->span.start_column = start.column - 1u;
    location->span.end_line = end.line - 1u;
    location->span.end_column = end.column - 1u;
    diagnostic->symbol = symbol->name;
    return true;
}

static const char *
vitte_main_sema_type_context(
    const vitte_main_driver_context_t *context,
    const vitte_sema_diagnostic_t *sema_diagnostic,
    char *buffer,
    size_t capacity)
{
    const vitte_symbol_t *symbol;
    const char *name;
    size_t name_length;
    int written = 0;

    if (context == NULL || sema_diagnostic == NULL ||
        buffer == NULL || capacity == 0u) {
        return NULL;
    }
    buffer[0] = '\0';
    symbol = sema_diagnostic->context_symbol_id !=
            VITTE_SCOPE_INVALID_SYMBOL_ID
        ? vitte_scope_get_symbol(
              &context->sema.scopes,
              sema_diagnostic->context_symbol_id)
        : NULL;
    name = symbol != NULL ? symbol->name : NULL;
    name_length = symbol != NULL ? symbol->name_length : 0u;

    switch (sema_diagnostic->type_context) {
        case VITTE_SEMA_TYPE_CONTEXT_ARGUMENT:
            if (sema_diagnostic->context_index != SIZE_MAX &&
                name != NULL && name_length != 0u) {
                written = snprintf(
                    buffer,
                    capacity,
                    "argument %zu / parameter `%.*s`",
                    sema_diagnostic->context_index,
                    (int)(name_length > 128u ? 128u : name_length),
                    name);
            } else if (sema_diagnostic->context_index != SIZE_MAX) {
                written = snprintf(
                    buffer,
                    capacity,
                    "argument %zu / parameter",
                    sema_diagnostic->context_index);
            } else {
                written = snprintf(buffer, capacity, "argument / parameter");
            }
            break;

        case VITTE_SEMA_TYPE_CONTEXT_FIELD:
            if (name != NULL && name_length != 0u) {
                written = snprintf(
                    buffer,
                    capacity,
                    "field `%.*s` / value",
                    (int)(name_length > 128u ? 128u : name_length),
                    name);
            } else {
                written = snprintf(buffer, capacity, "field / value");
            }
            break;

        case VITTE_SEMA_TYPE_CONTEXT_RETURN:
            written = snprintf(
                buffer,
                capacity,
                "return value / declared return type");
            break;

        case VITTE_SEMA_TYPE_CONTEXT_ASSIGNMENT:
            written = snprintf(
                buffer,
                capacity,
                "assignment / mutable binding");
            break;

        case VITTE_SEMA_TYPE_CONTEXT_CONDITION:
            written = snprintf(buffer, capacity, "condition");
            break;

        case VITTE_SEMA_TYPE_CONTEXT_EXPRESSION:
            written = snprintf(buffer, capacity, "expression");
            break;

        case VITTE_SEMA_TYPE_CONTEXT_NONE:
        case VITTE_SEMA_TYPE_CONTEXT_COUNT:
            switch (sema_diagnostic->code) {
                case VITTE_SEMA_DIAGNOSTIC_INVALID_INDEX_TYPE:
                    written = snprintf(buffer, capacity, "index expression");
                    break;
                case VITTE_SEMA_DIAGNOSTIC_INVALID_UNARY_OPERAND:
                case VITTE_SEMA_DIAGNOSTIC_INVALID_BINARY_OPERANDS:
                    written = snprintf(buffer, capacity, "operator operands");
                    break;
                case VITTE_SEMA_DIAGNOSTIC_TYPE_MISMATCH:
                    written = snprintf(buffer, capacity, "expression");
                    break;
                case VITTE_SEMA_DIAGNOSTIC_INVALID_ARGUMENT_TYPE:
                    written = snprintf(buffer, capacity, "argument / parameter");
                    break;
                case VITTE_SEMA_DIAGNOSTIC_RETURN_TYPE_MISMATCH:
                    written = snprintf(
                        buffer,
                        capacity,
                        "return value / declared return type");
                    break;
                case VITTE_SEMA_DIAGNOSTIC_NOT_ASSIGNABLE:
                    written = snprintf(buffer, capacity, "assignment / mutable binding");
                    break;
                case VITTE_SEMA_DIAGNOSTIC_CONDITION_NOT_BOOL:
                    written = snprintf(buffer, capacity, "condition");
                    break;
                default:
                    written = 0;
                    break;
            }
            break;
    }

    return written > 0 && (size_t)written < capacity ? buffer : NULL;
}

static const char *
vitte_main_sema_type_cause(
    const vitte_sema_diagnostic_t *sema_diagnostic)
{
    if (sema_diagnostic == NULL) {
        return NULL;
    }
    switch (sema_diagnostic->type_context) {
        case VITTE_SEMA_TYPE_CONTEXT_ARGUMENT:
            return "the argument does not satisfy the parameter type";
        case VITTE_SEMA_TYPE_CONTEXT_FIELD:
            return "the field value does not satisfy the declared field type";
        case VITTE_SEMA_TYPE_CONTEXT_RETURN:
            return "the returned value does not satisfy the declared return type";
        case VITTE_SEMA_TYPE_CONTEXT_ASSIGNMENT:
            return "the assignment target is immutable or not assignable";
        case VITTE_SEMA_TYPE_CONTEXT_CONDITION:
            return "the condition must be boolean";
        case VITTE_SEMA_TYPE_CONTEXT_EXPRESSION:
            return "the expression operands have incompatible types";
        case VITTE_SEMA_TYPE_CONTEXT_NONE:
        case VITTE_SEMA_TYPE_CONTEXT_COUNT:
            switch (sema_diagnostic->code) {
                case VITTE_SEMA_DIAGNOSTIC_INVALID_ARGUMENT_TYPE:
                    return "the argument does not satisfy the parameter type";
                case VITTE_SEMA_DIAGNOSTIC_RETURN_TYPE_MISMATCH:
                    return "the returned value does not satisfy the declared return type";
                case VITTE_SEMA_DIAGNOSTIC_NOT_ASSIGNABLE:
                    return "the assignment target is immutable or not assignable";
                case VITTE_SEMA_DIAGNOSTIC_CONDITION_NOT_BOOL:
                    return "the condition must be boolean";
                case VITTE_SEMA_DIAGNOSTIC_TYPE_MISMATCH:
                    return "the expected and found types are not assignable";
                default:
                    return NULL;
            }
    }
    return NULL;
}

static bool
vitte_main_render_sema_diagnostics(
    const vitte_main_driver_context_t *context,
    bool *rendered)
{
    vitte_cli_diagnostic_t *diagnostics;
    char (*type_names)[2][VITTE_MAIN_SEMA_TYPE_NAME_CAPACITY];
    char (*type_contexts)[VITTE_CLI_DIAGNOSTIC_CONTEXT_CAPACITY];
    vitte_cli_diagnostic_location_t *related_locations;
    const vitte_source_entry_t *source;
    const char *source_name;
    size_t diagnostic_count;
    size_t index;

    if (context == NULL ||
        rendered == NULL) {
        return false;
    }

    *rendered = false;
    diagnostic_count = vitte_sema_diagnostic_count(&context->sema);
    if (diagnostic_count == 0u) {
        return true;
    }

    diagnostics = (vitte_cli_diagnostic_t *)calloc(
        diagnostic_count,
        sizeof(*diagnostics));
    type_names = (char (*)[2][VITTE_MAIN_SEMA_TYPE_NAME_CAPACITY])calloc(
        diagnostic_count,
        sizeof(*type_names));
    type_contexts = (char (*)[VITTE_CLI_DIAGNOSTIC_CONTEXT_CAPACITY])calloc(
        diagnostic_count,
        sizeof(*type_contexts));
    related_locations = (vitte_cli_diagnostic_location_t *)calloc(
        diagnostic_count,
        sizeof(*related_locations));
    if (diagnostics == NULL || type_names == NULL ||
        type_contexts == NULL || related_locations == NULL) {
        free(diagnostics);
        free(type_names);
        free(type_contexts);
        free(related_locations);
        return false;
    }

    source = vitte_source_get(
        &context->sources,
        context->source_id);
    source_name = source != NULL
        ? source->name
        : context->options->input_path;

    for (index = 0u; index < diagnostic_count; ++index) {
        const vitte_sema_diagnostic_t *sema_diagnostic;
        vitte_cli_diagnostic_t *diagnostic;
        const char *expected_type;
        const char *found_type;
        int details_length;

        sema_diagnostic = vitte_sema_diagnostic_at(
            &context->sema,
            index);
        if (sema_diagnostic == NULL) {
            free(diagnostics);
            free(type_names);
            free(type_contexts);
            free(related_locations);
            return false;
        }

        diagnostic = &diagnostics[index];
        if (!vitte_main_make_cli_diagnostic(
                context,
                diagnostic,
                vitte_main_sema_diagnostic_severity(
                    sema_diagnostic->kind),
                vitte_main_sema_diagnostic_code(
                    sema_diagnostic->code),
                NULL,
                NULL,
                &sema_diagnostic->span,
                NULL,
                "semantic analysis")) {
            free(diagnostics);
            free(type_names);
            free(type_contexts);
            free(related_locations);
            return false;
        }

        expected_type = vitte_main_sema_format_type(
                &context->sema,
                sema_diagnostic->expected_type,
                type_names[index][0],
                sizeof(type_names[index][0]))
            ? type_names[index][0]
            : NULL;
        found_type = vitte_main_sema_format_type(
                &context->sema,
                sema_diagnostic->found_type,
                type_names[index][1],
                sizeof(type_names[index][1]))
            ? type_names[index][1]
            : NULL;
        diagnostic->expected_type = expected_type;
        diagnostic->actual_type = found_type;
        diagnostic->expected = expected_type;
        diagnostic->found = found_type;
        diagnostic->context = vitte_main_sema_type_context(
            context,
            sema_diagnostic,
            type_contexts[index],
            sizeof(type_contexts[index]));
        diagnostic->probable_cause = vitte_main_sema_type_cause(
            sema_diagnostic);
        if (sema_diagnostic->context_symbol_id !=
            VITTE_SCOPE_INVALID_SYMBOL_ID) {
            const vitte_symbol_t *context_symbol;

            context_symbol = vitte_scope_get_symbol(
                &context->sema.scopes,
                sema_diagnostic->context_symbol_id);
            if (context_symbol != NULL) {
                diagnostic->symbol = context_symbol->name;
            }
        }
        if (sema_diagnostic->contract_span.valid &&
            sema_diagnostic->contract_span.begin <= sema_diagnostic->contract_span.end &&
            sema_diagnostic->contract_span.end <= context->file.length) {
            const vitte_symbol_t *procedure_symbol = vitte_scope_get_symbol(
                &context->sema.scopes, sema_diagnostic->contract_symbol_id);
            vitte_cli_diagnostic_t clause;
            size_t expression_length = sema_diagnostic->contract_span.end -
                sema_diagnostic->contract_span.begin;
            bool ensures = (sema_diagnostic->contract_flags &
                VITTE_AST_FLAG_CONTRACT_ENSURES) != 0u;
            if (expression_length >= sizeof(type_contexts[index])) {
                expression_length = sizeof(type_contexts[index]) - 1u;
            }
            memcpy(type_contexts[index],
                context->file.data + sema_diagnostic->contract_span.begin,
                expression_length);
            type_contexts[index][expression_length] = '\0';
            diagnostic->context = ensures ? "ensures clause" : "requires clause";
            diagnostic->procedure = procedure_symbol != NULL ? procedure_symbol->name : NULL;
            diagnostic->has_contract = true;
            diagnostic->contract.kind = ensures
                ? VITTE_CLI_DIAGNOSTIC_CONTRACT_ENSURES
                : VITTE_CLI_DIAGNOSTIC_CONTRACT_REQUIRES;
            diagnostic->contract.expression = type_contexts[index];
            diagnostic->contract.procedure = diagnostic->procedure;
            diagnostic->contract.reason =
                sema_diagnostic->code == VITTE_SEMA_DIAGNOSTIC_CONTRACT_PRECONDITION
                    ? "the known call arguments do not satisfy the precondition"
                    : sema_diagnostic->code == VITTE_SEMA_DIAGNOSTIC_CONTRACT_RESULT
                    ? "result is available only in an ensures clause of a procedure returning a value"
                    : sema_diagnostic->code == VITTE_SEMA_DIAGNOSTIC_CONTRACT_CONTRADICTION ||
                      sema_diagnostic->code == VITTE_SEMA_DIAGNOSTIC_CONTRACT_POSTCONDITION
                        ? "the contract condition evaluates to false for the known values"
                        : "the contract expression must be valid and have type bool";
            diagnostic->probable_cause = diagnostic->contract.reason;
            if (vitte_main_make_cli_diagnostic(
                    context, &clause, diagnostic->severity, diagnostic->code,
                    NULL, NULL, &sema_diagnostic->contract_span, NULL, NULL) &&
                clause.has_span) {
                diagnostic->contract.has_contract_span = true;
                diagnostic->contract.contract_span.valid = true;
                diagnostic->contract.contract_span.source_name = clause.source_name;
                diagnostic->contract.contract_span.source_id = clause.source_id;
                diagnostic->contract.contract_span.file_id = clause.file_id;
                diagnostic->contract.contract_span.start_offset = clause.start_offset;
                diagnostic->contract.contract_span.end_offset = clause.end_offset;
                diagnostic->contract.contract_span.start_line = clause.start_line;
                diagnostic->contract.contract_span.start_column = clause.start_column;
                diagnostic->contract.contract_span.end_line = clause.end_line;
                diagnostic->contract.contract_span.end_column = clause.end_column;
            }
        }
        if (sema_diagnostic->code == VITTE_SEMA_DIAGNOSTIC_INDEX_OUT_OF_BOUNDS)
            diagnostic->message = "array index out of bounds";
        diagnostic->help = sema_diagnostic->help;
        details_length = 0;
        if (expected_type != NULL &&
            found_type != NULL) {
            details_length = snprintf(
                diagnostic->details_storage,
                sizeof(diagnostic->details_storage),
                "expected %s, found %s",
                expected_type,
                found_type);
        } else if (expected_type != NULL) {
            details_length = snprintf(
                diagnostic->details_storage,
                sizeof(diagnostic->details_storage),
                "expected %s",
                expected_type);
        } else if (found_type != NULL) {
            details_length = snprintf(
                diagnostic->details_storage,
                sizeof(diagnostic->details_storage),
                "found %s",
                found_type);
        }
        if (details_length < 0 ||
            (size_t)details_length >=
                sizeof(diagnostic->details_storage)) {
            free(diagnostics);
            free(type_names);
            free(type_contexts);
            free(related_locations);
            return false;
        }

        diagnostic->details =
            diagnostic->details_storage[0] != '\0'
                ? diagnostic->details_storage
                : NULL;
        if (vitte_main_sema_mutable_binding_location(
                context,
                sema_diagnostic,
                source_name,
                diagnostic,
                &related_locations[index])) {
            diagnostic->related = &related_locations[index];
            diagnostic->related_count = 1u;
        } else if (vitte_main_sema_symbol_origin_location(
                       context,
                       sema_diagnostic,
                       source_name,
                       diagnostic,
                       &related_locations[index])) {
            diagnostic->related = &related_locations[index];
            diagnostic->related_count = 1u;
        }
        if (diagnostic->has_contract && diagnostic->contract.has_contract_span &&
            (sema_diagnostic->span.begin != sema_diagnostic->contract_span.begin ||
             sema_diagnostic->span.end != sema_diagnostic->contract_span.end)) {
            related_locations[index].kind = VITTE_CLI_DIAGNOSTIC_LOCATION_DECLARATION;
            related_locations[index].span = diagnostic->contract.contract_span;
            related_locations[index].label = "contract declared here";
            diagnostic->related = &related_locations[index];
            diagnostic->related_count = 1u;
        }
        if (diagnostic->related_count != 0u) {
            size_t related_index;

            for (related_index = 0u;
                 related_index < diagnostic->related_count;
                 ++related_index) {
                vitte_main_diagnostic_map_span(
                    context,
                    &related_locations[index].span);
            }
        }
    }

    *rendered = vitte_diagnostic_render_cli_batch(
        stderr,
        &context->sources,
        context->source_id,
        source_name,
        diagnostics,
        diagnostic_count,
        context->options->color,
        vitte_main_cli_diagnostic_format(
            context->options->diagnostic_format));
    free(diagnostics);
    free(type_names);
    free(type_contexts);
    free(related_locations);
    return *rendered;
}

bool
vitte_main_driver_analyze(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    void *user_data)
{
    vitte_main_driver_context_t *context;
    bool rendered;

    (void)driver;
    (void)phase;

    context = (vitte_main_driver_context_t *)user_data;

    if (context == NULL) {
        return false;
    }
    if (context->native_frontend_completed) {
        return true;
    }
    if (!context->parser_initialized) {
        vitte_main_error("semantic analysis requires a parsed source file");
        return false;
    }

    vitte_main_verbose(context->options, "analyzing semantics");

    if (!vitte_sema_init(&context->sema, &context->parser)) {
        vitte_main_error("failed to initialize semantic analysis");
        return false;
    }

    context->sema_initialized = true;

    if (!vitte_sema_run(&context->sema)) {
        if (!vitte_main_render_sema_diagnostics(
                context,
                &rendered)) {
            vitte_main_error(
                "failed to render semantic diagnostics");
            return false;
        }

        if (!rendered &&
            !vitte_main_render_diagnostic(
                context,
                VITTE_CLI_DIAGNOSTIC_ERROR,
                "VITTE_INFRA_E_PHASE",
                "semantic analysis failed",
                vitte_sema_error_name(
                    vitte_sema_last_error(&context->sema)),
                NULL,
                NULL,
                "semantic analysis")) {
            vitte_main_error(
                "failed to render semantic analysis failure");
        }
        return false;
    }

    if (!vitte_main_render_sema_diagnostics(
            context,
            &rendered)) {
        vitte_main_error(
            "failed to render semantic diagnostics");
        return false;
    }

    if (vitte_sema_has_errors(&context->sema)) {
        return false;
    }

    return true;
}

bool
vitte_main_driver_compile_ast(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    void *user_data)
{
    vitte_main_driver_context_t *context;
    vitte_c17_compile_result_t compile_result;

    (void)driver;
    (void)phase;

    context = (vitte_main_driver_context_t *)user_data;
    if (context == NULL ||
        context->options == NULL ||
        (!context->native_frontend_completed &&
         !context->parser_initialized)) {
        vitte_main_error(
            "C17 generation requires a successfully parsed input");
        return false;
    }

    {
        vitte_c17_compile_options_t compile_options;
        vitte_c17_compile_failure_t compile_failure;

        memset(&compile_options, 0, sizeof(compile_options));
        memset(&compile_failure, 0, sizeof(compile_failure));
        compile_options.debug_info = context->options->debug_info;
        compile_options.debug_runtime = context->options->debug_probes;
        compile_options.emit_line_directives =
            context->options->emit_line_directives ||
            context->options->debug_info;
        compile_options.source_path = context->options->input_path;
        compile_options.failure = &compile_failure;

#ifdef VITTE_NATIVE_FRONTEND_LINKED
        if (context->native_frontend_completed) {
            if (!context->options->emit_c) {
                uint32_t failed_stage = 0u;
                uint32_t host_status;
                const char *code;
                const char *message;
                const char *details;
                const char *diagnostic_context;

                host_status =
                    compiler_driver_pipeline_vitte_native_driver_run_host_toolchain(
                    context->native_output,
                    context->native_output_length,
                    (uint8_t *)context->options->output_path,
                    context->options->emit_object,
                    context->options->command == VITTE_MAIN_COMMAND_RUN,
                    context->options->debug_info,
                    context->options->debug_probes,
                    context->options->run_argc,
                    (uint8_t **)(void *)context->options->run_argv,
                    &failed_stage);

                if (context->native_output != NULL) {
                    vitte_native_frontend_release_output(
                        context->native_output);
                }
                context->native_output = NULL;
                context->native_output_length = 0u;
                context->native_frontend_completed = false;

                if (host_status == 0u) {
                    return true;
                }

                if (failed_stage == 2u) {
                    code = "VITTE_BACKEND_E_EXTERNAL_COMPILER";
                    message = "generated C source compilation failed";
                    details = "the external C compiler could not be started or rejected the generated source; see its diagnostics above";
                    diagnostic_context = "C compilation";
                } else if (failed_stage == 3u) {
                    code = "VITTE_BACKEND_E_LINK";
                    message = "generated C object could not be linked";
                    details = "the external linker could not be started or create the requested executable; see its diagnostics above";
                    diagnostic_context = "linking";
                } else if (failed_stage == 4u) {
                    code = "VITTE_BACKEND_E_EXECUTION";
                    message = "compiled program execution failed";
                    details = "C compilation and linking succeeded, but the driver could not start the program or it returned a failure status";
                    diagnostic_context = "program execution";
                } else if (host_status == 4u) {
                    code = "VITTE_BACKEND_E_UNSUPPORTED";
                    message = "C17 host toolchain is unsupported on this platform";
                    details = "the native driver cannot create temporary files or launch host tools on this platform";
                    diagnostic_context = "host toolchain";
                } else if (host_status == 1u) {
                    code = "VITTE_INFRA_E_PHASE";
                    message = "invalid native host toolchain request";
                    details = "the driver received an invalid output path or run configuration";
                    diagnostic_context = "host toolchain";
                } else if (host_status == 5u) {
                    code = "VITTE_INFRA_E_RESOURCE";
                    message = "native host toolchain ran out of memory";
                    details = "the driver could not allocate its temporary compilation state";
                    diagnostic_context = "host toolchain";
                } else {
                    code = "VITTE_BACKEND_E_OUTPUT";
                    message = "native host toolchain could not prepare its files";
                    details = "creating, writing, or closing a temporary C17, object, or executable file failed";
                    diagnostic_context = "host toolchain preparation";
                }

                if (!vitte_main_render_diagnostic(
                        context,
                        VITTE_CLI_DIAGNOSTIC_ERROR,
                        code,
                        message,
                        details,
                        NULL,
                        NULL,
                        diagnostic_context)) {
                    vitte_main_error(
                        "failed to report native host toolchain failure");
                }
                return false;
            }

            /* Source-only output does not invoke host compiler tools. */
            compile_result = vitte_c17_compile_source_result_with_options(
                (const char *)context->native_output,
                context->native_output_length,
                context->options->output_path,
                true,
                false,
                false,
                0,
                NULL,
                &compile_options);
            if (context->native_output != NULL) {
                vitte_native_frontend_release_output(context->native_output);
            }
            context->native_output = NULL;
            context->native_output_length = 0u;
            context->native_frontend_completed = false;
        } else
#endif
        if (context->options->emit_object) {
            compile_result =
                vitte_c17_compile_ast_object_result_with_options(
                    &context->parser,
                    context->options->output_path,
                    &compile_options);
        } else {
            compile_result = vitte_c17_compile_ast_result_with_options(
                &context->parser,
                context->options->output_path,
                context->options->emit_c,
                context->options->command == VITTE_MAIN_COMMAND_RUN,
                context->options->run_argc,
                context->options->run_argv,
                &compile_options);
        }
        if (compile_result != VITTE_C17_COMPILE_SUCCESS) {
            const char *code;
            const char *message;
            const char *details;
            const vitte_parser_span_t *diagnostic_span = NULL;

            switch (compile_result) {
                case VITTE_C17_COMPILE_INVALID_ARGUMENT:
                    code = "VITTE_INFRA_E_PHASE";
                    message = "invalid C17 compilation request";
                    details = "the backend received an invalid compilation configuration";
                    break;
                case VITTE_C17_COMPILE_OUTPUT_FAILED:
                    code = "VITTE_BACKEND_E_OUTPUT";
                    message = "C17 output could not be written";
                    details = "creating, writing, or closing a C17 output file failed";
                    break;
                case VITTE_C17_COMPILE_EMIT_FAILED:
                    code = "VITTE_BACKEND_E_CODEGEN";
                    message = "C17 source emission failed";
                    details = "the AST emitter could not produce a complete C17 translation unit";
                    break;
                case VITTE_C17_COMPILE_C_COMPILATION_FAILED:
                    code = "VITTE_BACKEND_E_EXTERNAL_COMPILER";
                    message = "generated C source compilation failed";
                    details = "the external C compiler rejected the generated source; see its diagnostics above";
                    break;
                case VITTE_C17_COMPILE_LINK_FAILED:
                    code = "VITTE_BACKEND_E_LINK";
                    message = "generated C object could not be linked";
                    details = "the external linker could not create the requested executable; see its diagnostics above";
                    break;
                case VITTE_C17_COMPILE_RUNTIME_FAILED:
                    code = "VITTE_BACKEND_E_EXECUTION";
                    message = "compiled program exited unsuccessfully";
                    details = "C17 compilation and linking succeeded, but program execution returned a failure status";
                    break;
                case VITTE_C17_COMPILE_UNSUPPORTED:
                    code = "VITTE_BACKEND_E_UNSUPPORTED";
                    message = "C17 compilation is unsupported on this host";
                    details = "the bootstrap C17 compile driver does not support this platform";
                    break;
                case VITTE_C17_COMPILE_UNSUPPORTED_FEATURE:
                    code = "VITTE_BACKEND_E_UNSUPPORTED";
                    message = "the requested language construct is not supported by the C17 backend";
                    details = "the selected construct has no C17 lowering; no output was emitted";
                    break;
                case VITTE_C17_COMPILE_SUCCESS:
                default:
                    code = "VITTE_BACKEND_E_CODEGEN";
                    message = "C17 compilation failed";
                    details = "the backend returned an unclassified failure";
                    break;
            }
            if (compile_result == VITTE_C17_COMPILE_EMIT_FAILED &&
                compile_failure.message != NULL) {
                message = compile_failure.message;
                details = compile_failure.details;
                diagnostic_span = &compile_failure.span;
            }

            if (!vitte_main_render_diagnostic(
                    context,
                    VITTE_CLI_DIAGNOSTIC_ERROR,
                    code,
                    message,
                    details,
                    diagnostic_span,
                    NULL,
                    "C17 backend")) {
                vitte_main_error(
                    "failed to report the C17 backend failure");
            }
            return false;
        }
    }

    return true;
}

#ifdef VITTE_NATIVE_FRONTEND_LINKED
static bool
vitte_main_emit_native_ir(
    vitte_main_driver_context_t *context)
{
    FILE *output = stdout;
    bool close_output = false;
    bool success;

    if (context == NULL || context->options == NULL ||
        !context->options->emit_ir || context->native_output == NULL) {
        return false;
    }
    if (context->options->output_path != NULL) {
        output = fopen(context->options->output_path, "wb");
        if (output == NULL) {
            vitte_main_report_path_error(
                "VITTE_BACKEND_E_OUTPUT",
                "native IR output could not be opened",
                context->options->output_path,
                "creating the requested IR output file failed");
            return false;
        }
        close_output = true;
    }

    success = fwrite(
        context->native_output,
        1u,
        context->native_output_length,
        output) == context->native_output_length;
    if (success && !close_output) {
        success = fflush(output) == 0;
    }
    if (close_output && fclose(output) != 0) {
        success = false;
    }
    if (!success) {
        vitte_main_report_path_error(
            "VITTE_BACKEND_E_OUTPUT",
            "native IR output could not be written",
            context->options->output_path,
            "writing or closing the IR output failed");
    }
    return success;
}
#endif

bool
vitte_main_driver_lower_hir(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    void *user_data)
{
    vitte_main_driver_context_t *context =
        (vitte_main_driver_context_t *)user_data;
    (void)driver;
    (void)phase;
    if (context == NULL || context->options == NULL ||
        !context->use_ir_pipeline) {
        return false;
    }
    if (context->native_frontend_completed) {
        return context->options->emit_ir;
    }
    if (!context->parser_initialized) return false;
    if (!vitte_hir_init(&context->hir)) {
        vitte_main_error("failed to initialize HIR");
        return false;
    }
    context->hir_initialized = true;
    if (!vitte_hir_lower_ast(&context->hir, &context->parser)) {
        (void)vitte_main_render_diagnostic(
            context,
            VITTE_CLI_DIAGNOSTIC_ERROR,
            "VITTE_HIR_E_LOWERING",
            "HIR lowering failed",
            vitte_hir_error_name(context->hir.last_error),
            NULL,
            NULL,
            "HIR lowering");
        return false;
    }
    return context->hir.state == VITTE_HIR_STATE_VALIDATED;
}

bool
vitte_main_driver_lower_ir(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    void *user_data)
{
    vitte_main_driver_context_t *context =
        (vitte_main_driver_context_t *)user_data;
    (void)driver;
    (void)phase;
    if (context == NULL || context->options == NULL ||
        !context->use_ir_pipeline) return false;
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    if (context->native_frontend_completed && context->options->emit_ir) {
        bool written = vitte_main_emit_native_ir(context);
        vitte_native_frontend_release_output(context->native_output);
        context->native_output = NULL;
        context->native_output_length = 0u;
        context->native_frontend_completed = false;
        return written;
    }
#endif
    if (!context->hir_initialized) return false;
    if (!vitte_ir_init(&context->ir)) {
        vitte_main_error("failed to initialize IR");
        return false;
    }
    context->ir_initialized = true;
    if (!vitte_ir_lower_hir(&context->ir, &context->hir)) {
        (void)vitte_main_render_diagnostic(
            context,
            VITTE_CLI_DIAGNOSTIC_ERROR,
            "VITTE_IR_E_LOWERING",
            "IR lowering failed",
            vitte_ir_error_name(context->ir.last_error),
            NULL,
            NULL,
            "IR lowering");
        return false;
    }
    return context->ir.state == VITTE_IR_STATE_SEALED;
}

bool
vitte_main_driver_codegen_ir(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    void *user_data)
{
    vitte_main_driver_context_t *context =
        (vitte_main_driver_context_t *)user_data;
    vitte_c17_ir_emit_options_t options;
    vitte_c17_ir_compile_result_t compile_result;
    const char *output_path;
    (void)driver;
    (void)phase;
    if (context == NULL || context->options == NULL ||
        !context->ir_initialized) return false;
    options = vitte_c17_ir_emit_options_default();
    options.emit_debug_comments = context->options->debug_info;
    options.emit_line_directives = context->options->debug_info;
    options.emit_debug_info = context->options->debug_info;
    options.source_path = context->options->input_path;
    output_path = context->options->output_path;
    if (!context->options->emit_c && !context->options->emit_object &&
        output_path == NULL &&
        context->options->command != VITTE_MAIN_COMMAND_RUN) {
        output_path = "vitte.out";
    }
    if (context->options->emit_object) {
        compile_result = vitte_c17_compile_ir_object_result(
            &context->ir,
            output_path,
            &options);
    } else {
        compile_result = vitte_c17_compile_ir_result(
            &context->ir,
            output_path,
            context->options->emit_c,
            context->options->command == VITTE_MAIN_COMMAND_RUN,
            context->options->run_argc,
            context->options->run_argv,
            &options);
    }
    if (compile_result != VITTE_C17_IR_COMPILE_SUCCESS) {
        const char *code;
        const char *message;
        const char *details;

        switch (compile_result) {
            case VITTE_C17_IR_COMPILE_INVALID_ARGUMENT:
                code = "VITTE_INFRA_E_PHASE";
                message = "invalid IR compilation request";
                details = "the backend received an invalid IR compilation configuration";
                break;
            case VITTE_C17_IR_COMPILE_OUTPUT_FAILED:
                code = "VITTE_BACKEND_E_OUTPUT";
                message = "IR C17 output could not be written";
                details = "creating, writing, or closing a temporary or output file failed";
                break;
            case VITTE_C17_IR_COMPILE_EMIT_FAILED:
                code = "VITTE_BACKEND_E_CODEGEN";
                message = "C17 source emission from IR failed";
                details = "the IR emitter could not produce a complete C17 translation unit";
                break;
            case VITTE_C17_IR_COMPILE_C_COMPILATION_FAILED:
                code = "VITTE_BACKEND_E_EXTERNAL_COMPILER";
                message = "generated C source compilation failed";
                details = "the external C compiler rejected the generated source; see its diagnostics above";
                break;
            case VITTE_C17_IR_COMPILE_LINK_FAILED:
                code = "VITTE_BACKEND_E_LINK";
                message = "generated C object could not be linked";
                details = "the external linker could not create the requested executable; see its diagnostics above";
                break;
            case VITTE_C17_IR_COMPILE_RUNTIME_FAILED:
                code = "VITTE_BACKEND_E_EXECUTION";
                message = "compiled program exited unsuccessfully";
                details = "C17 compilation and linking succeeded, but program execution returned a failure status";
                break;
            case VITTE_C17_IR_COMPILE_UNSUPPORTED:
                code = "VITTE_BACKEND_E_UNSUPPORTED";
                message = "IR C17 compilation is unsupported on this host";
                details = "the IR C17 compile driver does not support this platform";
                break;
            case VITTE_C17_IR_COMPILE_SUCCESS:
            default:
                code = "VITTE_BACKEND_E_CODEGEN";
                message = "IR C17 compilation failed";
                details = "the backend returned an unclassified failure";
                break;
        }
        (void)vitte_main_render_diagnostic(
            context,
            VITTE_CLI_DIAGNOSTIC_ERROR,
            code,
            message,
            details,
            NULL,
            NULL,
            "C17 IR backend");
        return false;
    }
    return true;
}

void
vitte_main_driver_context_destroy(
    vitte_driver_t *driver,
    void *user_data)
{
    vitte_main_driver_context_t *context;

    (void)driver;

    context = (vitte_main_driver_context_t *)user_data;

    if (context == NULL) {
        return;
    }

#ifdef VITTE_NATIVE_FRONTEND_LINKED
    if (context->native_output != NULL) {
        vitte_native_frontend_release_output(context->native_output);
        context->native_output = NULL;
        context->native_output_length = 0u;
    }
#endif

    if (context->sema_initialized) {
        vitte_sema_destroy(&context->sema);
        context->sema_initialized = false;
    }

    if (context->ir_initialized) {
        vitte_ir_destroy(&context->ir);
        context->ir_initialized = false;
    }

    if (context->hir_initialized) {
        vitte_hir_destroy(&context->hir);
        context->hir_initialized = false;
    }

    if (context->parser_initialized) {
        vitte_parser_destroy(&context->parser);
        context->parser_initialized = false;
    }

    free(context->expanded_tokens);
    context->expanded_tokens = NULL;
    context->expanded_token_count = 0u;

    if (context->lexer_initialized) {
        vitte_lexer_destroy(&context->lexer);
        context->lexer_initialized = false;
    }

    if (context->source_initialized) {
        vitte_source_destroy(&context->sources);
        context->source_initialized = false;
    }

    free(context->origins);
    context->origins = NULL;
    context->origin_count = 0u;

    vitte_main_file_destroy(&context->file);
}

/*
 * The driver subsystem is the canonical owner of the complete compilation
 * pipeline.
 *
 * Keeping this bridge isolated means changes to driver.h affect one small
 * section of main.c rather than CLI parsing and process management.
 */
