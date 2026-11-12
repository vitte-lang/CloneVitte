/*
 * Vitte Compiler
 * src/api/api.c
 *
 * Public compiler API implementation.
 *
 * This translation unit is the stable boundary between applications using
 * Vitte as a library and the internal compiler pipeline.
 *
 * Responsibilities:
 *
 *   - API lifecycle
 *   - compiler/context lifecycle
 *   - option normalization
 *   - source validation
 *   - source/file compilation
 *   - diagnostic propagation
 *   - result ownership
 *   - deterministic cleanup
 *   - defensive argument checking
 *   - translation of internal failures into public status codes
 *
 * The API layer owns orchestration and result translation only. Parsing,
 * semantic analysis and code generation remain implemented by their stages.
 */

#include "api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "error.h"

#include "../diagnostic/diagnostic.h"
#include "../diagnostic/registry.h"
#include "../diagnostic/render_terminal.h"
#include "../diagnostic/help.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../sema/sema.h"
#include "../backend/c17/ast_emitter.h"
#include "../filesystem/filesystem.h"
#include "../source/source.h"
#include "version.h"

#ifdef VITTE_NATIVE_FRONTEND_LINKED
#include "native_api_bridge.inc"
#else

#ifdef VITTE_NATIVE_FRONTEND_LINKED
/* Implemented by the Vitte-compiled native frontend archive. */
extern uint32_t compiler_driver_pipeline_vitte_native_frontend_run(
    const char *source,
    size_t source_length,
    uint64_t file_id);
extern uint32_t compiler_driver_pipeline_vitte_native_frontend_run_with_config(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    uint8_t *config_bytes);
extern uint32_t compiler_driver_pipeline_vitte_native_frontend_generate_with_config(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    uint8_t *config_bytes,
    uint8_t **generated_source,
    size_t *generated_source_length);
extern void compiler_driver_pipeline_vitte_native_frontend_release_output(
    uint8_t *generated_source);

/* Keep the legacy C-facing driver symbols as ABI shims over the Vitte module. */
uint32_t
vitte_native_frontend_run(
    const char *source,
    size_t source_length,
    uint64_t file_id)
{
    return compiler_driver_pipeline_vitte_native_frontend_run(
        source,
        source_length,
        file_id);
}

uint32_t
vitte_native_frontend_run_with_config(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const void *config)
{
    return compiler_driver_pipeline_vitte_native_frontend_run_with_config(
        source,
        source_length,
        file_id,
        (uint8_t *)config);
}

uint32_t
vitte_native_frontend_generate_with_config(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const void *config,
    uint8_t **generated_source,
    size_t *generated_source_length)
{
    return compiler_driver_pipeline_vitte_native_frontend_generate_with_config(
        source,
        source_length,
        file_id,
        (uint8_t *)config,
        generated_source,
        generated_source_length);
}

void
vitte_native_frontend_release_output(uint8_t *generated_source)
{
    compiler_driver_pipeline_vitte_native_frontend_release_output(
        generated_source);
}

/* Public API state and results implemented in compiler/api/api.vitte. */
typedef struct Api Api;
typedef struct ApiResult ApiResult;
extern int32_t vitte_api_native_create(Api **out_api);
extern void vitte_api_native_destroy(Api *api);
extern int32_t vitte_api_native_compile_source(
    Api *api, const char *source_name, const char *source,
    size_t source_length, ApiResult **out_result);
extern int32_t vitte_api_native_compile_string(
    Api *api, const char *source, ApiResult **out_result);
extern int32_t vitte_api_native_compile_file(
    Api *api, const char *path, ApiResult **out_result);
extern void vitte_api_native_result_destroy(ApiResult *result);
extern int32_t vitte_api_native_result_status(const ApiResult *result);
extern bool vitte_api_native_result_succeeded(const ApiResult *result);
extern const char *vitte_api_native_result_source_name(const ApiResult *result);
extern const uint8_t *vitte_api_native_result_output_data(const ApiResult *result);
extern size_t vitte_api_native_result_output_size(const ApiResult *result);
extern const uint8_t *vitte_api_native_result_diagnostics(const ApiResult *result);
extern size_t vitte_api_native_result_diagnostics_size(const ApiResult *result);
extern size_t vitte_api_native_result_error_count(const ApiResult *result);
extern size_t vitte_api_native_result_warning_count(const ApiResult *result);
extern size_t vitte_api_native_result_note_count(const ApiResult *result);
extern size_t vitte_api_native_compilation_count(const Api *api);
extern size_t vitte_api_native_successful_compilation_count(const Api *api);
extern size_t vitte_api_native_failed_compilation_count(const Api *api);
extern bool vitte_api_native_is_valid(const Api *api);
extern bool vitte_api_native_is_compiling(const Api *api);
extern int32_t vitte_api_native_error_status(const Api *api);
extern const char *vitte_api_native_error_code(const Api *api);
extern const char *vitte_api_native_error_message(const Api *api);
extern const char *vitte_api_native_error_details(const Api *api);
extern void vitte_api_native_clear_error(Api *api);

static _Thread_local vitte_error_t vitte_api_native_error_snapshot;
#endif

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

#ifndef VITTE_API_MAX_SOURCE_SIZE
#define VITTE_API_MAX_SOURCE_SIZE ((size_t)1024u * 1024u * 1024u)
#endif

#ifndef VITTE_API_MAX_PATH_LENGTH
#define VITTE_API_MAX_PATH_LENGTH ((size_t)32768u)
#endif

#ifndef VITTE_API_INITIAL_OUTPUT_CAPACITY
#define VITTE_API_INITIAL_OUTPUT_CAPACITY ((size_t)4096u)
#endif

#ifndef VITTE_API_TAB_WIDTH
#define VITTE_API_TAB_WIDTH 4u
#endif

#ifndef VITTE_API_RESULT_MAGIC
#define VITTE_API_RESULT_MAGIC UINT64_C(0x5649545445524553)
#endif

#ifndef VITTE_API_CONTEXT_MAGIC
#define VITTE_API_CONTEXT_MAGIC UINT64_C(0x5649545445435458)
#endif

/* ========================================================================= */
/* Internal utilities                                                        */
/* ========================================================================= */


static void
vitte_api_set_last_error(
    vitte_status_t status,
    const char *code,
    const char *message)
{
    vitte_error_t error;

    vitte_error_init(&error);

    error.status = status;
    error.code = code;
    error.message = message;
    error.details = NULL;
    error.details_storage[0] = '\0';

    vitte_error_set_last(&error);
}

static bool
vitte_api_size_add_overflow(size_t a, size_t b, size_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (a > SIZE_MAX - b) {
        return true;
    }

    *result = a + b;
    return false;
}

static bool
vitte_api_size_mul_overflow(size_t a, size_t b, size_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (a != 0u && b > SIZE_MAX / a) {
        return true;
    }

    *result = a * b;
    return false;
}

static size_t
vitte_api_bounded_strlen(const char *text, size_t maximum)
{
    size_t length;

    if (text == NULL) {
        return 0u;
    }

    length = 0u;

    while (length < maximum && text[length] != '\0') {
        ++length;
    }

    return length;
}

static bool
vitte_api_path_is_valid(const char *path)
{
    size_t length;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    length = vitte_api_bounded_strlen(path, VITTE_API_MAX_PATH_LENGTH);

    if (length == 0u || length >= VITTE_API_MAX_PATH_LENGTH) {
        return false;
    }

    return true;
}

static void *
vitte_api_allocate(size_t size)
{
    if (size == 0u) {
        size = 1u;
    }

    return malloc(size);
}

static void *
vitte_api_allocate_zeroed(size_t count, size_t size)
{
    size_t total;

    if (vitte_api_size_mul_overflow(count, size, &total)) {
        return NULL;
    }

    if (total == 0u) {
        total = 1u;
    }

    return calloc(1u, total);
}

static void
vitte_api_deallocate(void *pointer)
{
    free(pointer);
}

static char *
vitte_api_duplicate_string_n(const char *text, size_t length)
{
    char *copy;
    size_t allocation_size;

    if (text == NULL && length != 0u) {
        return NULL;
    }

    if (vitte_api_size_add_overflow(length, 1u, &allocation_size)) {
        return NULL;
    }

    copy = (char *)vitte_api_allocate(allocation_size);

    if (copy == NULL) {
        return NULL;
    }

    if (length != 0u) {
        memcpy(copy, text, length);
    }

    copy[length] = '\0';

    return copy;
}

static char *
vitte_api_duplicate_string(const char *text)
{
    size_t length;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text);

    return vitte_api_duplicate_string_n(text, length);
}

/* ========================================================================= */
/* Internal byte buffer                                                      */
/* ========================================================================= */

typedef struct vitte_api_buffer {
    unsigned char *data;
    size_t length;
    size_t capacity;
} vitte_api_buffer_t;

static void
vitte_api_buffer_init(vitte_api_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }

    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

static void
vitte_api_buffer_destroy(vitte_api_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }

    vitte_api_deallocate(buffer->data);

    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

static bool
vitte_api_buffer_reserve(vitte_api_buffer_t *buffer, size_t required)
{
    unsigned char *new_data;
    size_t new_capacity;

    if (buffer == NULL) {
        return false;
    }

    if (required <= buffer->capacity) {
        return true;
    }

    new_capacity = buffer->capacity;

    if (new_capacity == 0u) {
        new_capacity = VITTE_API_INITIAL_OUTPUT_CAPACITY;
    }

    while (new_capacity < required) {
        size_t next;

        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required;
            break;
        }

        next = new_capacity * 2u;

        if (next <= new_capacity) {
            new_capacity = required;
            break;
        }

        new_capacity = next;
    }

    if (new_capacity < required) {
        return false;
    }

    new_data = (unsigned char *)realloc(buffer->data, new_capacity);

    if (new_data == NULL) {
        return false;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;

    return true;
}

static bool
vitte_api_buffer_append(
    vitte_api_buffer_t *buffer,
    const void *data,
    size_t length)
{
    size_t required;

    if (buffer == NULL) {
        return false;
    }

    if (length == 0u) {
        return true;
    }

    if (data == NULL) {
        return false;
    }

    if (vitte_api_size_add_overflow(buffer->length, length, &required)) {
        return false;
    }

    if (!vitte_api_buffer_reserve(buffer, required)) {
        return false;
    }

    memcpy(buffer->data + buffer->length, data, length);
    buffer->length = required;

    return true;
}

static bool
vitte_api_buffer_terminate(vitte_api_buffer_t *buffer)
{
    size_t required;

    if (buffer == NULL) {
        return false;
    }

    if (vitte_api_size_add_overflow(
            buffer->length,
            1u,
            &required) ||
        !vitte_api_buffer_reserve(buffer, required)) {
        return false;
    }

    buffer->data[buffer->length] = 0u;

    return true;
}

/* ========================================================================= */
/* Public result                                                             */
/* ========================================================================= */

/*
 * api.h may expose this structure directly or through an opaque declaration.
 * The implementation intentionally keeps all ownership in one object.
 */

typedef struct vitte_api_result_internal {
    uint64_t magic;

    vitte_status_t status;

    char *source_name;

    unsigned char *output;
    size_t output_size;

    char *diagnostics;
    size_t diagnostics_size;

    size_t error_count;
    size_t warning_count;
    size_t note_count;

    bool succeeded;
} vitte_api_result_internal_t;

/* ========================================================================= */
/* API context                                                               */
/* ========================================================================= */

typedef enum vitte_api_state {
    VITTE_API_STATE_UNINITIALIZED = 0,
    VITTE_API_STATE_READY,
    VITTE_API_STATE_COMPILING,
    VITTE_API_STATE_FAILED,
    VITTE_API_STATE_DESTROYED
} vitte_api_state_t;

typedef struct vitte_api_context_internal {
    uint64_t magic;

    vitte_api_state_t state;

    vitte_context_t context;

    size_t compilation_count;
    size_t successful_compilation_count;
    size_t failed_compilation_count;

    vitte_error_t last_error;
} vitte_api_context_internal_t;

/* ========================================================================= */
/* Internal error handling                                                   */
/* ========================================================================= */

static vitte_status_t
vitte_api_fail(
    vitte_api_context_internal_t *api,
    vitte_status_t status,
    const char *code,
    const char *message)
{
    if (api != NULL) {
        vitte_error_set(
            &api->last_error,
            status,
            code,
            message);
    }

    vitte_api_set_last_error(status, code, message);

    return status;
}

static vitte_status_t
vitte_api_invalid_argument(
    vitte_api_context_internal_t *api,
    const char *message)
{
    return vitte_api_fail(
        api,
        VITTE_STATUS_ERROR_INVALID_ARGUMENT,
        "VITTE_API_INVALID_ARGUMENT",
        message);
}

static vitte_status_t
vitte_api_invalid_state(
    vitte_api_context_internal_t *api,
    const char *message)
{
    return vitte_api_fail(
        api,
        VITTE_STATUS_ERROR_INVALID_STATE,
        "VITTE_API_INVALID_STATE",
        message);
}

static vitte_status_t
vitte_api_out_of_memory(
    vitte_api_context_internal_t *api)
{
    return vitte_api_fail(
        api,
        VITTE_STATUS_ERROR_OUT_OF_MEMORY,
        "VITTE_API_OUT_OF_MEMORY",
        "out of memory");
}

static vitte_status_t
vitte_api_io_error(
    vitte_api_context_internal_t *api,
    const char *message)
{
    return vitte_api_fail(
        api,
        VITTE_STATUS_ERROR_IO,
        "VITTE_API_IO_ERROR",
        message);
}

static vitte_status_t
vitte_api_internal_error(
    vitte_api_context_internal_t *api,
    const char *message)
{
    return vitte_api_fail(
        api,
        VITTE_STATUS_ERROR_INTERNAL,
        "VITTE_API_INTERNAL_ERROR",
        message);
}

/* ========================================================================= */
/* Context validation                                                        */
/* ========================================================================= */

static bool
vitte_api_context_is_valid(
    const vitte_api_context_internal_t *api)
{
    if (api == NULL) {
        return false;
    }

    if (api->magic != VITTE_API_CONTEXT_MAGIC) {
        return false;
    }

    if (api->state == VITTE_API_STATE_DESTROYED) {
        return false;
    }

    return true;
}

static bool
vitte_api_result_is_valid(
    const vitte_api_result_internal_t *result)
{
    return result != NULL &&
           result->magic == VITTE_API_RESULT_MAGIC;
}

/* ========================================================================= */
/* Result lifecycle                                                          */
/* ========================================================================= */

static vitte_api_result_internal_t *
vitte_api_result_create(void)
{
    vitte_api_result_internal_t *result;

    result = (vitte_api_result_internal_t *)
        vitte_api_allocate_zeroed(
            1u,
            sizeof(*result));

    if (result == NULL) {
        return NULL;
    }

    result->magic = VITTE_API_RESULT_MAGIC;
    result->status = VITTE_STATUS_OK;
    result->succeeded = false;

    return result;
}

static void
vitte_api_result_destroy_internal(
    vitte_api_result_internal_t *result)
{
    if (!vitte_api_result_is_valid(result)) {
        return;
    }

    result->magic = 0u;

    vitte_api_deallocate(result->source_name);
    vitte_api_deallocate(result->output);
    vitte_api_deallocate(result->diagnostics);

    result->source_name = NULL;
    result->output = NULL;
    result->diagnostics = NULL;

    result->output_size = 0u;
    result->diagnostics_size = 0u;

    result->error_count = 0u;
    result->warning_count = 0u;
    result->note_count = 0u;

    vitte_api_deallocate(result);
}

/* ========================================================================= */
/* Source validation                                                         */
/* ========================================================================= */

static vitte_status_t
vitte_api_validate_source(
    vitte_api_context_internal_t *api,
    const char *source_name,
    const char *source,
    size_t source_length)
{
    if (source == NULL && source_length != 0u) {
        return vitte_api_invalid_argument(
            api,
            "source is NULL while source length is non-zero");
    }

    if (source_length > VITTE_API_MAX_SOURCE_SIZE) {
        return vitte_api_invalid_argument(
            api,
            "source exceeds the maximum supported API source size");
    }

    if (source_name != NULL &&
        !vitte_api_path_is_valid(source_name)) {
        return vitte_api_invalid_argument(
            api,
            "invalid source name");
    }

    return VITTE_STATUS_OK;
}

/* ========================================================================= */
/* File loading                                                              */
/* ========================================================================= */

static vitte_status_t
vitte_api_read_file(
    vitte_api_context_internal_t *api,
    const char *path,
    char **out_data,
    size_t *out_size)
{
    vitte_filesystem_buffer_t buffer = {0};
    vitte_filesystem_result_t filesystem_result;
    size_t file_size;
    size_t allocation_size;
    char *data;

    if (out_data == NULL || out_size == NULL) {
        return vitte_api_invalid_argument(
            api,
            "output file buffer arguments are NULL");
    }

    *out_data = NULL;
    *out_size = 0u;

    if (!vitte_api_path_is_valid(path)) {
        return vitte_api_invalid_argument(
            api,
            "invalid input file path");
    }

    filesystem_result = vitte_filesystem_read_file(
        path,
        VITTE_API_MAX_SOURCE_SIZE,
        &buffer);
    if (filesystem_result.error != VITTE_FILESYSTEM_ERROR_NONE) {
        if (filesystem_result.error == VITTE_FILESYSTEM_ERROR_INVALID_ARGUMENT ||
            filesystem_result.error == VITTE_FILESYSTEM_ERROR_INVALID_PATH) {
            return vitte_api_invalid_argument(
                api,
                "invalid input file path");
        }
        if (filesystem_result.error == VITTE_FILESYSTEM_ERROR_TOO_LARGE) {
            return vitte_api_invalid_argument(
                api,
                "input file exceeds maximum source size");
        }
        return vitte_api_io_error(
            api,
            "unable to open input file");
    }
    file_size = buffer.length;

    if (vitte_api_size_add_overflow(file_size, 1u, &allocation_size)) {
        vitte_filesystem_buffer_destroy(&buffer);

        return vitte_api_invalid_argument(
            api,
            "input file size overflows the source buffer");
    }

    data = (char *)vitte_api_allocate(allocation_size);

    if (data == NULL) {
        vitte_filesystem_buffer_destroy(&buffer);
        return vitte_api_out_of_memory(api);
    }
    if (file_size != 0u) {
        memcpy(data, buffer.data, file_size);
    }
    data[file_size] = '\0';
    vitte_filesystem_buffer_destroy(&buffer);

    *out_data = data;
    *out_size = file_size;

    return VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Diagnostic rendering                                                      */
/* ========================================================================= */

static vitte_status_t
vitte_api_capture_diagnostics(
    vitte_api_context_internal_t *api,
    vitte_api_result_internal_t *result)
{
    vitte_api_buffer_t buffer;
    const vitte_diagnostic_bag_t *bag;
    size_t index;

    if (api == NULL || result == NULL) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    vitte_api_buffer_init(&buffer);

    bag = vitte_context_diagnostics(&api->context);

    if (bag == NULL) {
        return VITTE_STATUS_OK;
    }

    result->error_count =
        vitte_diagnostic_error_count(bag);

    result->warning_count =
        vitte_diagnostic_warning_count(bag);

    for (index = 0u;
         index < vitte_diagnostic_bag_count(bag);
         ++index) {
        const vitte_diagnostic_t *diagnostic;
        char rendered[8192];
        size_t rendered_length;
        vitte_status_t status;

        diagnostic =
            vitte_diagnostic_at(bag, index);

        if (diagnostic == NULL) {
            continue;
        }

        if (diagnostic->severity == VITTE_DIAGNOSTIC_NOTE ||
            diagnostic->severity == VITTE_DIAGNOSTIC_HELP) {
            ++result->note_count;
        }

        {
            vitte_diagnostic_terminal_options_t options;
            FILE *stream = tmpfile();
            if (stream == NULL) {
                vitte_api_buffer_destroy(&buffer);
                return vitte_api_internal_error(api, "unable to create diagnostic stream");
            }
            vitte_diagnostic_terminal_options_init(&options);
            options.sources = bag->options.sources;
            vitte_diagnostic_terminal_set_color(&options, false);
            status = vitte_diagnostic_render_terminal_one(stream, diagnostic, &options);
            if (status != VITTE_STATUS_OK || fflush(stream) != 0 ||
                fseek(stream, 0L, SEEK_SET) != 0) {
                fclose(stream);
                vitte_api_buffer_destroy(&buffer);
                return vitte_api_internal_error(api, "failed to format compiler diagnostic");
            }
            while ((rendered_length = fread(rendered, 1u, sizeof(rendered), stream)) != 0u) {
                if (!vitte_api_buffer_append(&buffer, rendered, rendered_length)) {
                    fclose(stream);
                    vitte_api_buffer_destroy(&buffer);
                    return vitte_api_out_of_memory(api);
                }
            }
            if (ferror(stream)) {
                fclose(stream);
                vitte_api_buffer_destroy(&buffer);
                return vitte_api_internal_error(api, "failed to read compiler diagnostic");
            }
            fclose(stream);
        }
    }

    if (!vitte_api_buffer_terminate(&buffer)) {
        vitte_api_buffer_destroy(&buffer);
        return vitte_api_out_of_memory(api);
    }

    result->diagnostics = (char *)buffer.data;
    result->diagnostics_size = buffer.length;

    buffer.data = NULL;
    buffer.length = 0u;
    buffer.capacity = 0u;

    return VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Pipeline diagnostics                                                      */
/* ========================================================================= */

static bool
vitte_api_offset_location(
    const char *source,
    size_t source_length,
    size_t offset,
    unsigned *out_line,
    unsigned *out_column)
{
    size_t index;
    unsigned line;
    unsigned column;

    if ((source == NULL && source_length != 0u) || offset > source_length ||
        out_line == NULL || out_column == NULL) {
        return false;
    }

    line = 0u;
    column = 0u;

    for (index = 0u; index < offset; ++index) {
        if (source[index] == '\n') {
            if (line == UINT32_MAX) {
                return false;
            }
            ++line;
            column = 0u;
        } else if (source[index] == '\t') {
            if (column > UINT32_MAX - VITTE_API_TAB_WIDTH) {
                return false;
            }
            column += VITTE_API_TAB_WIDTH;
        } else {
            if (column == UINT32_MAX) {
                return false;
            }
            ++column;
        }
    }

    *out_line = line;
    *out_column = column;
    return true;
}

static bool
vitte_api_make_diagnostic_span(
    const char *source_name,
    const char *source,
    size_t source_length,
    uint64_t source_id,
    uint32_t file_id,
    size_t begin,
    size_t end,
    size_t start_line_hint,
    size_t start_column_hint,
    vitte_ast_span_t *out_span)
{
    size_t index;

    if (source_name == NULL || out_span == NULL || begin > end ||
        end > source_length) {
        return false;
    }

    memset(out_span, 0, sizeof(*out_span));
    out_span->file_id = file_id;
    out_span->begin = begin;
    out_span->end = end;
    out_span->source_id = source_id;
    out_span->source_name = source_name;
    out_span->start_offset = begin;
    out_span->end_offset = end;

    if (start_line_hint != 0u && start_column_hint != 0u &&
        start_line_hint <= UINT32_MAX &&
        start_column_hint <= UINT32_MAX) {
        out_span->start_line = (unsigned)(start_line_hint - 1u);
        out_span->start_column = (unsigned)(start_column_hint - 1u);
    } else if (!vitte_api_offset_location(
                   source,
                   source_length,
                   begin,
                   &out_span->start_line,
                   &out_span->start_column)) {
        return false;
    }

    out_span->end_line = out_span->start_line;
    out_span->end_column = out_span->start_column;
    for (index = begin; index < end; ++index) {
        if (source[index] == '\n') {
            if (out_span->end_line == UINT32_MAX) {
                return false;
            }
            ++out_span->end_line;
            out_span->end_column = 0u;
        } else if (source[index] == '\t') {
            if (out_span->end_column > UINT32_MAX - VITTE_API_TAB_WIDTH) {
                return false;
            }
            out_span->end_column += VITTE_API_TAB_WIDTH;
        } else {
            if (out_span->end_column == UINT32_MAX) {
                return false;
            }
            ++out_span->end_column;
        }
    }

    out_span->valid = true;
    return true;
}

static vitte_status_t
vitte_api_emit_pipeline_diagnostic(
    vitte_api_context_internal_t *api,
    vitte_diagnostic_severity_t severity,
    vitte_diagnostic_origin_t origin,
    const char *code,
    const char *message,
    const char *details,
    const vitte_ast_span_t *span)
{
    vitte_diagnostic_t diagnostic;
    vitte_diagnostic_bag_t *bag;

    if (api == NULL || code == NULL || message == NULL || message[0] == '\0') {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    bag = vitte_context_diagnostics_mut(&api->context);
    if (bag == NULL) {
        return VITTE_STATUS_ERROR_INVALID_STATE;
    }

    vitte_diagnostic_init(&diagnostic);
    diagnostic.severity = severity;
    diagnostic.origin = origin;
    (void)snprintf(diagnostic.code_storage, sizeof(diagnostic.code_storage), "%s", code);
    diagnostic.code = diagnostic.code_storage;
    (void)snprintf(diagnostic.message_storage, sizeof(diagnostic.message_storage), "%s", message);
    diagnostic.message = diagnostic.message_storage;
    if (details != NULL) {
        (void)snprintf(diagnostic.details_storage, sizeof(diagnostic.details_storage), "%s", details);
        diagnostic.details = diagnostic.details_storage;
    }

    if (span != NULL &&
        vitte_diagnostic_set_span(&diagnostic, span) != VITTE_STATUS_OK) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    {
        vitte_status_t status;
        if (span != NULL) {
            status = vitte_diagnostic_add_label(&diagnostic, VITTE_DIAGNOSTIC_LABEL_PRIMARY,
                span, details != NULL ? details : message);
            if (status != VITTE_STATUS_OK) return status;
        }

        status = vitte_diagnostic_add(bag, &diagnostic);
        if (status != VITTE_STATUS_OK) {
            (void)vitte_api_fail(
                api,
                status,
                "VITTE_API_DIAGNOSTIC_FAILURE",
                "unable to record compiler diagnostic");
            (void)vitte_context_set_failure(
                &api->context,
                status,
                "VITTE_API_DIAGNOSTIC_FAILURE",
                "unable to record compiler diagnostic");
        }
        return status;
    }
}

static const char *
vitte_api_lexer_diagnostic_code(vitte_lexer_error_t error)
{
    switch (error) {
        case VITTE_LEXER_ERROR_INVALID_UTF8:
            return "VITTE_LEXER_E_INVALID_UNICODE";
        case VITTE_LEXER_ERROR_INVALID_NUMBER:
            return "VITTE_LEXER_E_INVALID_NUMBER";
        case VITTE_LEXER_ERROR_INVALID_ESCAPE:
            return "VITTE_LEXER_E_INVALID_ESCAPE";
        case VITTE_LEXER_ERROR_UNTERMINATED_STRING:
            return "VITTE_LEXER_E_UNTERMINATED_STRING";
        case VITTE_LEXER_ERROR_UNTERMINATED_COMMENT:
            return "VITTE_LEXER_E_UNTERMINATED_COMMENT";
        case VITTE_LEXER_ERROR_INVALID_CHARACTER:
        case VITTE_LEXER_ERROR_INVALID_TOKEN:
            return "VITTE_LEXER_E_INVALID_CHARACTER";
        case VITTE_LEXER_ERROR_OVERFLOW:
            return "VITTE_LEXER_E_NUMBER_OVERFLOW";
        case VITTE_LEXER_ERROR_COMMENT_DEPTH:
        case VITTE_LEXER_ERROR_TOKEN_LIMIT:
        case VITTE_LEXER_ERROR_SOURCE_TOO_LARGE:
            return "VITTE_INFRA_E_LIMIT";
        case VITTE_LEXER_ERROR_OUT_OF_MEMORY:
            return "VITTE_INFRA_E_RESOURCE";
        case VITTE_LEXER_ERROR_INVALID_ARGUMENT:
        case VITTE_LEXER_ERROR_INVALID_CONTEXT:
        case VITTE_LEXER_ERROR_INVALID_STATE:
        case VITTE_LEXER_ERROR_VALIDATION:
        case VITTE_LEXER_ERROR_INTERNAL:
        case VITTE_LEXER_ERROR_NONE:
        case VITTE_LEXER_ERROR_COUNT:
            return "VITTE_INFRA_E_PHASE";
    }

    return "VITTE_INFRA_E_PHASE";
}

static const char *
vitte_api_parser_diagnostic_code(vitte_parser_error_t error)
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

static const char *
vitte_api_sema_diagnostic_code(vitte_sema_diagnostic_code_t code)
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
        case VITTE_SEMA_DIAGNOSTIC_CODE_COUNT:
            return "VITTE_TYPE_E_MISMATCH";
    }

    return "VITTE_INFRA_E_FALLBACK";
}

static vitte_diagnostic_severity_t
vitte_api_parser_severity(vitte_parser_diagnostic_kind_t kind)
{
    switch (kind) {
        case VITTE_PARSER_DIAGNOSTIC_NOTE:
            return VITTE_DIAGNOSTIC_NOTE;
        case VITTE_PARSER_DIAGNOSTIC_HELP:
            return VITTE_DIAGNOSTIC_HELP;
        case VITTE_PARSER_DIAGNOSTIC_WARNING:
            return VITTE_DIAGNOSTIC_WARNING;
        case VITTE_PARSER_DIAGNOSTIC_ERROR:
        case VITTE_PARSER_DIAGNOSTIC_INVALID:
        case VITTE_PARSER_DIAGNOSTIC_COUNT:
            return VITTE_DIAGNOSTIC_ERROR;
    }

    return VITTE_DIAGNOSTIC_ERROR;
}

static vitte_diagnostic_severity_t
vitte_api_sema_severity(vitte_sema_diagnostic_kind_t kind)
{
    switch (kind) {
        case VITTE_SEMA_DIAGNOSTIC_NOTE:
            return VITTE_DIAGNOSTIC_NOTE;
        case VITTE_SEMA_DIAGNOSTIC_HELP:
            return VITTE_DIAGNOSTIC_HELP;
        case VITTE_SEMA_DIAGNOSTIC_WARNING:
            return VITTE_DIAGNOSTIC_WARNING;
        case VITTE_SEMA_DIAGNOSTIC_ERROR:
        case VITTE_SEMA_DIAGNOSTIC_KIND_COUNT:
            return VITTE_DIAGNOSTIC_ERROR;
    }

    return VITTE_DIAGNOSTIC_ERROR;
}

static const char *
vitte_api_diagnostic_title(const char *code)
{
    const vitte_diagnostic_registry_entry_t *entry;

    entry = vitte_diagnostic_registry_find(code);
    return entry != NULL && entry->title != NULL
        ? entry->title
        : "compiler diagnostic";
}

static vitte_status_t
vitte_api_capture_parser_diagnostics(
    vitte_api_context_internal_t *api,
    const vitte_parser_t *parser,
    const char *source_name,
    const char *source,
    size_t source_length,
    uint64_t source_id)
{
    size_t index;

    for (index = 0u; index < vitte_parser_diagnostic_count(parser); ++index) {
        const vitte_parser_diagnostic_t *item;
        vitte_ast_span_t span;
        const vitte_ast_span_t *span_pointer;
        const char *code;
        char details[512];
        int details_length;

        item = vitte_parser_diagnostic_at(parser, index);
        if (item == NULL) {
            continue;
        }

        code = vitte_api_parser_diagnostic_code(item->error);
        span_pointer = NULL;
        if (item->span.valid && vitte_api_make_diagnostic_span(
                source_name,
                source,
                source_length,
                source_id,
                item->span.file_id,
                item->span.begin,
                item->span.end,
                item->span.line,
                item->span.column,
                &span)) {
            span_pointer = &span;
        }

        details[0] = '\0';
        if (item->expected != VITTE_TOKEN_INVALID &&
            item->found != VITTE_TOKEN_INVALID) {
            details_length = snprintf(
                details,
                sizeof(details),
                "expected %s, found %s",
                vitte_token_kind_name(item->expected),
                vitte_token_kind_name(item->found));
            if (details_length < 0 || (size_t)details_length >= sizeof(details)) {
                details[0] = '\0';
            }
        } else if (item->expected != VITTE_TOKEN_INVALID) {
            details_length = snprintf(
                details,
                sizeof(details),
                "expected %s",
                vitte_token_kind_name(item->expected));
            if (details_length < 0 || (size_t)details_length >= sizeof(details)) {
                details[0] = '\0';
            }
        }

        {
            vitte_status_t status;
            status = vitte_api_emit_pipeline_diagnostic(
                api,
                vitte_api_parser_severity(item->kind),
                VITTE_DIAGNOSTIC_ORIGIN_PARSER,
                code,
                vitte_api_diagnostic_title(code),
                details[0] != '\0' ? details : NULL,
                span_pointer);
            if (status != VITTE_STATUS_OK) {
                return status;
            }
        }
    }

    return VITTE_STATUS_OK;
}

static vitte_status_t
vitte_api_capture_sema_diagnostics(
    vitte_api_context_internal_t *api,
    const vitte_sema_t *sema,
    const char *source_name,
    const char *source,
    size_t source_length,
    uint64_t source_id)
{
    size_t index;

    for (index = 0u; index < vitte_sema_diagnostic_count(sema); ++index) {
        const vitte_sema_diagnostic_t *item;
        vitte_ast_span_t span;
        const vitte_ast_span_t *span_pointer;
        vitte_diagnostic_origin_t origin;
        const char *code;
        const char *details;

        item = vitte_sema_diagnostic_at(sema, index);
        if (item == NULL) {
            continue;
        }

        code = vitte_api_sema_diagnostic_code(item->code);
        origin = item->code == VITTE_SEMA_DIAGNOSTIC_UNRESOLVED_NAME ||
                 item->code == VITTE_SEMA_DIAGNOSTIC_PRIVATE_SYMBOL ||
                 item->code == VITTE_SEMA_DIAGNOSTIC_DUPLICATE_DECLARATION ||
                 item->code == VITTE_SEMA_DIAGNOSTIC_SHADOWED_DECLARATION
            ? VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION
            : VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK;
        {
            const vitte_diagnostic_registry_entry_t *entry =
                vitte_diagnostic_registry_find(code);
            if (entry != NULL) {
                origin = entry->origin;
            }
        }
        span_pointer = NULL;
        if (item->span.valid && vitte_api_make_diagnostic_span(
                source_name,
                source,
                source_length,
                source_id,
                item->span.file_id,
                item->span.begin,
                item->span.end,
                item->span.line,
                item->span.column,
                &span)) {
            span_pointer = &span;
        }
        {
            const vitte_diagnostic_registry_entry_t *entry = vitte_diagnostic_registry_find(code);
            details = item->help[0] != '\0' ? item->help
                : entry != NULL ? entry->explanation : vitte_sema_diagnostic_code_name(item->code);
        }

        {
            vitte_status_t status;
            vitte_diagnostic_bag_t *capture_bag = vitte_context_diagnostics_mut(&api->context);
            size_t count_before = capture_bag != NULL ? capture_bag->count : 0u;
            status = vitte_api_emit_pipeline_diagnostic(
                api,
                vitte_api_sema_severity(item->kind),
                origin,
                code,
                item->code == VITTE_SEMA_DIAGNOSTIC_INDEX_OUT_OF_BOUNDS
                    ? "array index out of bounds" : vitte_api_diagnostic_title(code),
                details,
                span_pointer);
            if (status != VITTE_STATUS_OK) {
                return status;
            }
            {
                vitte_diagnostic_bag_t *bag = vitte_context_diagnostics_mut(&api->context);
                vitte_diagnostic_t *diagnostic = bag != NULL && bag->count > count_before
                    ? &bag->storage[bag->count - 1u] : NULL;
                if (diagnostic != NULL && item->contract_span.valid &&
                    item->contract_span.begin <= item->contract_span.end && item->contract_span.end <= source_length) {
                    char expression[VITTE_DIAGNOSTIC_CONTRACT_EXPRESSION_CAPACITY];
                    vitte_ast_span_t clause_span;
                    const vitte_symbol_t *symbol = vitte_scope_get_symbol(&sema->scopes, item->contract_symbol_id);
                    bool ensures = (item->contract_flags & VITTE_AST_FLAG_CONTRACT_ENSURES) != 0u;
                    size_t length = item->contract_span.end - item->contract_span.begin;
                    if (length >= sizeof(expression)) length = sizeof(expression) - 1u;
                    memcpy(expression, source + item->contract_span.begin, length);
                    expression[length] = '\0';
                    if (!vitte_api_make_diagnostic_span(source_name, source, source_length, source_id,
                            item->contract_span.file_id, item->contract_span.begin, item->contract_span.end,
                            item->contract_span.line, item->contract_span.column, &clause_span))
                        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
                    status = vitte_diagnostic_set_contract(diagnostic,
                        ensures ? VITTE_DIAGNOSTIC_CONTRACT_ENSURES : VITTE_DIAGNOSTIC_CONTRACT_REQUIRES,
                        expression, symbol != NULL ? symbol->name : NULL, details, &clause_span,
                        item->code == VITTE_SEMA_DIAGNOSTIC_CONTRACT_PRECONDITION ? span_pointer : NULL, NULL);
                    if (status != VITTE_STATUS_OK) return status;
                    vitte_diagnostic_set_context(diagnostic, NULL, NULL,
                        symbol != NULL ? symbol->name : NULL, ensures ? "ensures clause" : "requires clause");
                    if (span_pointer != NULL && (span.start_offset != clause_span.start_offset || span.end_offset != clause_span.end_offset)) {
                        status = vitte_diagnostic_add_location(diagnostic, VITTE_DIAGNOSTIC_LOCATION_DECLARATION,
                            &clause_span, "contract declared here");
                        if (status != VITTE_STATUS_OK) return status;
                    }
                }
                if (diagnostic != NULL) {
                    const vitte_sema_type_t *expected_type = vitte_sema_get_type(sema, item->expected_type);
                    const vitte_sema_type_t *found_type = vitte_sema_get_type(sema, item->found_type);
                    const char *expected = expected_type != NULL ? vitte_sema_type_kind_name(expected_type->kind) : NULL;
                    const char *found = found_type != NULL ? vitte_sema_type_kind_name(found_type->kind) : NULL;
                    char help[VITTE_DIAGNOSTIC_MESSAGE_CAPACITY];
                    vitte_diagnostic_set_subject(diagnostic, NULL, expected, found);
                    vitte_diagnostic_set_expected_found(diagnostic, expected, found);
                    if (item->help[0] != '\0') {
                        status = vitte_diagnostic_add_help(diagnostic, item->help, span_pointer);
                    } else if (vitte_diagnostic_format_help(diagnostic, bag->options.sources, help, sizeof(help))) {
                        status = vitte_diagnostic_add_help(diagnostic, help, span_pointer);
                    }
                    if (status != VITTE_STATUS_OK) return status;
                }
            }
        }
    }

    return VITTE_STATUS_OK;
}

static vitte_status_t
vitte_api_emit_stage_failure(
    vitte_api_context_internal_t *api,
    const char *message,
    vitte_status_t status)
{
    vitte_status_t diagnostic_status;

    diagnostic_status = vitte_api_emit_pipeline_diagnostic(
        api,
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "VITTE_INFRA_E_PHASE",
        "compiler phase failure",
        message,
        NULL);

    if (diagnostic_status != VITTE_STATUS_OK) {
        return diagnostic_status;
    }

    (void)vitte_api_fail(
        api,
        status,
        "VITTE_API_PIPELINE_FAILURE",
        message);
    (void)vitte_context_set_failure(
        &api->context,
        status,
        "VITTE_API_PIPELINE_FAILURE",
        message);

    return status;
}

/* ========================================================================= */
/* C17 output collection                                                     */
/* ========================================================================= */

static vitte_status_t
vitte_api_emit_c17_output(
    vitte_api_context_internal_t *api,
    const vitte_parser_t *parser,
    const char *source_name,
    vitte_api_result_internal_t *result)
{
    FILE *stream;
    vitte_c17_compile_options_t options;
    long output_length;
    unsigned char *output;
    size_t bytes_read;
    bool emit_succeeded;
    vitte_status_t status;

    stream = tmpfile();
    if (stream == NULL) {
        (void)vitte_api_emit_pipeline_diagnostic(
            api,
            VITTE_DIAGNOSTIC_ERROR,
            VITTE_DIAGNOSTIC_ORIGIN_IO,
            "VITTE_BACKEND_E_OUTPUT",
            vitte_api_diagnostic_title("VITTE_BACKEND_E_OUTPUT"),
            "unable to create temporary C17 output stream",
            NULL);
        return vitte_api_fail(
            api,
            VITTE_STATUS_ERROR_IO,
            "VITTE_API_IO_ERROR",
            "unable to create temporary C17 output stream");
    }

    memset(&options, 0, sizeof(options));
    options.source_path = source_name;

    emit_succeeded = vitte_c17_emit_ast_with_options(
        stream,
        parser,
        &options);

    if (!emit_succeeded || fflush(stream) != 0 ||
        fseek(stream, 0L, SEEK_END) != 0) {
        (void)fclose(stream);
        (void)vitte_api_emit_pipeline_diagnostic(
            api,
            VITTE_DIAGNOSTIC_ERROR,
            VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND,
            "VITTE_BACKEND_E_CODEGEN",
            vitte_api_diagnostic_title("VITTE_BACKEND_E_CODEGEN"),
            "the AST could not be emitted as C17",
            NULL);
        return vitte_api_fail(
            api,
            VITTE_STATUS_ERROR_BACKEND,
            "VITTE_API_BACKEND_FAILURE",
            "the AST could not be emitted as C17");
    }

    output_length = ftell(stream);
    if (output_length < 0 || (uintmax_t)output_length > (uintmax_t)SIZE_MAX ||
        fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fclose(stream);
        (void)vitte_api_emit_pipeline_diagnostic(
            api,
            VITTE_DIAGNOSTIC_ERROR,
            VITTE_DIAGNOSTIC_ORIGIN_IO,
            "VITTE_BACKEND_E_OUTPUT",
            vitte_api_diagnostic_title("VITTE_BACKEND_E_OUTPUT"),
            "unable to determine the generated C17 output size",
            NULL);
        return vitte_api_fail(
            api,
            VITTE_STATUS_ERROR_IO,
            "VITTE_API_IO_ERROR",
            "unable to read generated C17 output");
    }

    output = (unsigned char *)vitte_api_allocate((size_t)output_length);
    if (output == NULL) {
        (void)fclose(stream);
        status = vitte_api_out_of_memory(api);
        (void)vitte_context_set_failure(
            &api->context,
            status,
            "VITTE_API_OUT_OF_MEMORY",
            "unable to allocate generated C17 output buffer");
        return status;
    }

    bytes_read = (size_t)output_length == 0u
        ? 0u
        : fread(output, 1u, (size_t)output_length, stream);
    if (bytes_read != (size_t)output_length || ferror(stream) != 0) {
        vitte_api_deallocate(output);
        (void)fclose(stream);
        (void)vitte_api_emit_pipeline_diagnostic(
            api,
            VITTE_DIAGNOSTIC_ERROR,
            VITTE_DIAGNOSTIC_ORIGIN_IO,
            "VITTE_BACKEND_E_OUTPUT",
            vitte_api_diagnostic_title("VITTE_BACKEND_E_OUTPUT"),
            "unable to read generated C17 output",
            NULL);
        return vitte_api_fail(
            api,
            VITTE_STATUS_ERROR_IO,
            "VITTE_API_IO_ERROR",
            "unable to read generated C17 output");
    }

    if (fclose(stream) != 0) {
        vitte_api_deallocate(output);
        (void)vitte_api_emit_pipeline_diagnostic(
            api,
            VITTE_DIAGNOSTIC_ERROR,
            VITTE_DIAGNOSTIC_ORIGIN_IO,
            "VITTE_BACKEND_E_OUTPUT",
            vitte_api_diagnostic_title("VITTE_BACKEND_E_OUTPUT"),
            "unable to close generated C17 output stream",
            NULL);
        return vitte_api_fail(
            api,
            VITTE_STATUS_ERROR_IO,
            "VITTE_API_IO_ERROR",
            "unable to close generated C17 output stream");
    }

    result->output = output;
    result->output_size = (size_t)output_length;
    return VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Front-end and backend orchestration                                       */
/* ========================================================================= */

static vitte_status_t
vitte_api_run_pipeline(
    vitte_api_context_internal_t *api,
    const char *source_name,
    const char *source,
    size_t source_length,
    uint64_t source_id,
    vitte_api_result_internal_t *result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    uint8_t *generated_source;
    size_t generated_source_length;
    uint32_t native_status;

    (void)source_name;
    generated_source = NULL;
    generated_source_length = 0u;

    native_status = vitte_native_frontend_generate_with_config(
        source,
        source_length,
        source_id,
        NULL,
        &generated_source,
        &generated_source_length);

    if (native_status == 1u) {
        unsigned char *output;

        output = (unsigned char *)vitte_api_allocate(generated_source_length);
        if (output == NULL) {
            vitte_native_frontend_release_output(generated_source);
            return vitte_api_out_of_memory(api);
        }

        if (generated_source_length != 0u) {
            if (generated_source == NULL) {
                vitte_api_deallocate(output);
                return vitte_api_internal_error(
                    api,
                    "native frontend returned an empty output buffer");
            }
            memcpy(output, generated_source, generated_source_length);
        }

        vitte_native_frontend_release_output(generated_source);
        result->output = output;
        result->output_size = generated_source_length;
        return VITTE_STATUS_OK;
    }

    vitte_native_frontend_release_output(generated_source);

    if (native_status == 2u) {
        const char *code;
        const char *message;
        vitte_status_t diagnostic_status;

        code = "VITTE_FRONTEND_E_SOURCE";
        message = "native frontend rejected the source";
        diagnostic_status = vitte_api_emit_pipeline_diagnostic(
            api,
            VITTE_DIAGNOSTIC_ERROR,
            VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
            code,
            message,
            "native parser or semantic analysis reported an error",
            NULL);
        if (diagnostic_status != VITTE_STATUS_OK) {
            return diagnostic_status;
        }

        (void)vitte_context_set_failure(
            &api->context,
            VITTE_STATUS_ERROR_PARSE,
            code,
            message);
        return vitte_api_fail(
            api,
            VITTE_STATUS_ERROR_PARSE,
            code,
            message);
    }

    return vitte_api_emit_stage_failure(
        api,
        "native frontend failed before producing C17 output",
        VITTE_STATUS_ERROR_INTERNAL);
#else
    vitte_lexer_t lexer;
    vitte_parser_t parser;
    vitte_sema_t sema;
    bool lexer_initialized;
    bool parser_initialized;
    bool sema_initialized;
    bool parser_succeeded;
    bool sema_succeeded;
    vitte_status_t status;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&sema, 0, sizeof(sema));
    lexer_initialized = false;
    parser_initialized = false;
    sema_initialized = false;
    status = VITTE_STATUS_OK;

    if (source_id > UINT32_MAX) {
        status = vitte_api_emit_stage_failure(
            api,
            "source identifier exceeds lexer limits",
            VITTE_STATUS_ERROR_INTERNAL);
        goto cleanup;
    }
    if (!vitte_lexer_init(
            &lexer,
            source,
            source_length,
            (uint32_t)source_id)) {
        lexer_initialized = lexer.magic == VITTE_LEXER_MAGIC;
        status = vitte_api_emit_stage_failure(
            api,
            "unable to initialize lexer",
            VITTE_STATUS_ERROR_INTERNAL);
        goto cleanup;
    }
    lexer_initialized = true;

    if (!vitte_lexer_run(&lexer)) {
        vitte_lexer_error_t error;
        vitte_lexer_span_t lexer_span;
        vitte_ast_span_t span;
        const vitte_ast_span_t *span_pointer;
        const char *code;
        char details[256];
        int details_length;
        vitte_status_t diagnostic_status;

        error = vitte_lexer_last_error(&lexer);
        lexer_span = vitte_lexer_error_span(&lexer);
        code = vitte_api_lexer_diagnostic_code(error);
        span_pointer = NULL;
        if (lexer_span.valid && vitte_api_make_diagnostic_span(
                source_name,
                source,
                source_length,
                source_id,
                lexer_span.file_id,
                lexer_span.begin,
                lexer_span.end,
                0u,
                0u,
                &span)) {
            span_pointer = &span;
        }
        details_length = snprintf(
            details,
            sizeof(details),
            "lexing failed: %s",
            vitte_lexer_error_name(error));
        if (details_length < 0 || (size_t)details_length >= sizeof(details)) {
            details[0] = '\0';
        }
        diagnostic_status = vitte_api_emit_pipeline_diagnostic(
            api,
            VITTE_DIAGNOSTIC_ERROR,
            VITTE_DIAGNOSTIC_ORIGIN_LEXER,
            code,
            vitte_api_diagnostic_title(code),
            details[0] != '\0' ? details : NULL,
            span_pointer);
        if (diagnostic_status != VITTE_STATUS_OK) {
            status = diagnostic_status;
        } else if (error == VITTE_LEXER_ERROR_OUT_OF_MEMORY) {
            status = vitte_api_out_of_memory(api);
        } else if (error == VITTE_LEXER_ERROR_INVALID_ARGUMENT ||
                   error == VITTE_LEXER_ERROR_INVALID_CONTEXT ||
                   error == VITTE_LEXER_ERROR_INVALID_STATE ||
                   error == VITTE_LEXER_ERROR_VALIDATION ||
                   error == VITTE_LEXER_ERROR_INTERNAL ||
                   error == VITTE_LEXER_ERROR_NONE ||
                   error == VITTE_LEXER_ERROR_COUNT) {
            status = vitte_api_fail(
                api,
                VITTE_STATUS_ERROR_INTERNAL,
                "VITTE_API_PIPELINE_FAILURE",
                vitte_lexer_error_name(error));
            (void)vitte_context_set_failure(
                &api->context,
                status,
                "VITTE_API_PIPELINE_FAILURE",
                vitte_lexer_error_name(error));
        } else {
            status = VITTE_STATUS_ERROR_PARSE;
        }
        goto cleanup;
    }
    if (!vitte_parser_init(
            &parser,
            lexer.tokens,
            lexer.token_count)) {
        parser_initialized = parser.magic == VITTE_PARSER_MAGIC;
        status = vitte_api_emit_stage_failure(
            api,
            "unable to initialize parser",
            VITTE_STATUS_ERROR_INTERNAL);
        goto cleanup;
    }
    parser_initialized = true;

    parser_succeeded = vitte_parser_run(&parser);
    status = vitte_api_capture_parser_diagnostics(
        api,
        &parser,
        source_name,
        source,
        source_length,
        source_id);
    if (status != VITTE_STATUS_OK) {
        goto cleanup;
    }
    if (!parser_succeeded) {
        if (vitte_parser_diagnostic_count(&parser) == 0u) {
            status = vitte_api_emit_stage_failure(
                api,
                vitte_parser_error_name(vitte_parser_last_error(&parser)),
                VITTE_STATUS_ERROR_INTERNAL);
        } else {
            status = VITTE_STATUS_ERROR_PARSE;
        }
        goto cleanup;
    }

    if (!vitte_parser_validate(&parser)) {
        status = vitte_api_emit_stage_failure(
            api,
            "parser produced an invalid abstract syntax tree",
            VITTE_STATUS_ERROR_INTERNAL);
        goto cleanup;
    }

    if (!vitte_sema_init(&sema, &parser)) {
        sema_initialized = sema.magic == VITTE_SEMA_MAGIC;
        status = vitte_api_emit_stage_failure(
            api,
            "unable to initialize semantic analysis",
            VITTE_STATUS_ERROR_INTERNAL);
        goto cleanup;
    }
    sema_initialized = true;

    sema_succeeded = vitte_sema_run(&sema);
    status = vitte_api_capture_sema_diagnostics(
        api,
        &sema,
        source_name,
        source,
        source_length,
        source_id);
    if (status != VITTE_STATUS_OK) {
        goto cleanup;
    }

    if (vitte_sema_has_errors(&sema)) {
        status = VITTE_STATUS_ERROR_PARSE;
        goto cleanup;
    }

    if (!sema_succeeded) {
        status = vitte_api_emit_stage_failure(
            api,
            "semantic analysis terminated without a source diagnostic",
            VITTE_STATUS_ERROR_INTERNAL);
        goto cleanup;
    }

    status = vitte_api_emit_c17_output(
        api,
        &parser,
        source_name,
        result);

cleanup:
    if (sema_initialized) {
        vitte_sema_destroy(&sema);
    }
    if (parser_initialized) {
        vitte_parser_destroy(&parser);
    }
    if (lexer_initialized) {
        vitte_lexer_destroy(&lexer);
    }

    return status;
#endif
}

/* ========================================================================= */
/* Compilation core                                                          */
/* ========================================================================= */

static vitte_status_t
vitte_api_compile_internal(
    vitte_api_context_internal_t *api,
    const char *source_name,
    const char *source,
    size_t source_length,
    vitte_api_result_internal_t **out_result)
{
    vitte_api_result_internal_t *result;
    vitte_status_t status;
    vitte_source_id_t source_id;

    if (!vitte_api_context_is_valid(api)) {
        return VITTE_STATUS_ERROR_INVALID_STATE;
    }

    if (out_result == NULL) {
        return vitte_api_invalid_argument(
            api,
            "out_result is NULL");
    }

    *out_result = NULL;

    status = vitte_api_validate_source(
        api,
        source_name,
        source,
        source_length);

    if (status != VITTE_STATUS_OK) {
        return status;
    }

    if (api->state == VITTE_API_STATE_COMPILING) {
        return vitte_api_invalid_state(
            api,
            "compiler context is already compiling");
    }

    result = vitte_api_result_create();

    if (result == NULL) {
        return vitte_api_out_of_memory(api);
    }

    result->source_name =
        vitte_api_duplicate_string(
            source_name != NULL
                ? source_name
                : "<memory>");

    if (result->source_name == NULL) {
        vitte_api_result_destroy_internal(result);
        return vitte_api_out_of_memory(api);
    }

    api->state = VITTE_API_STATE_COMPILING;
    ++api->compilation_count;

    vitte_error_reset(&api->last_error);
    vitte_error_clear_last();

    /*
     * Reset per-compilation state while retaining reusable compiler
     * infrastructure owned by vitte_context_t.
     */
    status = vitte_context_begin_compilation(
        &api->context);

    if (status != VITTE_STATUS_OK) {
        api->state = VITTE_API_STATE_FAILED;
        ++api->failed_compilation_count;

        result->status = status;

        (void)vitte_api_capture_diagnostics(
            api,
            result);

        *out_result = result;

        return status;
    }

    /*
     * Source registration.
     */
    source_id = VITTE_SOURCE_ID_NONE;
    status = vitte_context_add_source(
        &api->context,
        result->source_name,
        source,
        source_length,
        &source_id);

    if (status == VITTE_STATUS_OK) {
        status = vitte_api_run_pipeline(
            api,
            result->source_name,
            source,
            source_length,
            source_id,
            result);
    } else {
        (void)vitte_api_fail(
            api,
            status,
            "VITTE_API_SOURCE_REGISTRATION_FAILED",
            "unable to register source with compiler context");
    }

    /*
     * Always finalize the compilation context.
     *
     * This gives the diagnostic engine an opportunity to flush deferred
     * diagnostics, resolve cascades and stabilize deterministic ordering.
     */
    {
        vitte_status_t end_status;

        end_status =
            vitte_context_end_compilation(
                &api->context);

        if (status == VITTE_STATUS_OK && end_status != VITTE_STATUS_OK) {
            status = end_status;
        }
    }

    if (status == VITTE_STATUS_ERROR_PARSE) {
        (void)vitte_api_fail(
            api,
            status,
            "VITTE_API_SOURCE_ERROR",
            "source compilation failed; see result diagnostics");
    }

    result->status = status;

    {
        vitte_status_t diagnostic_status;

        diagnostic_status =
            vitte_api_capture_diagnostics(
                api,
                result);

        if (diagnostic_status != VITTE_STATUS_OK) {
            status = diagnostic_status;
            result->status = status;
        }
    }

    result->succeeded =
        status == VITTE_STATUS_OK &&
        result->error_count == 0u;

    if (result->succeeded) {
        api->state = VITTE_API_STATE_READY;
        ++api->successful_compilation_count;
    } else {
        api->state = VITTE_API_STATE_FAILED;
        ++api->failed_compilation_count;
    }

    *out_result = result;

    return status;
}

/* ========================================================================= */
/* Public lifecycle                                                          */
/* ========================================================================= */

vitte_status_t
vitte_api_create(
    vitte_api_t **out_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    Api *native_api;
    vitte_status_t native_status;

    if (out_api == NULL) {
        vitte_api_set_last_error(
            VITTE_STATUS_ERROR_INVALID_ARGUMENT,
            "VITTE_API_INVALID_ARGUMENT",
            "out_api is NULL");
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }
    *out_api = NULL;
    native_api = NULL;
    native_status = (vitte_status_t)
        vitte_api_native_create(&native_api);
    if (native_status != VITTE_STATUS_OK) {
        vitte_api_set_last_error(
            native_status,
            "VITTE_API_NATIVE_CREATE",
            "native Vitte API context initialization failed");
        return native_status;
    }
    *out_api = (vitte_api_t *)native_api;
    return VITTE_STATUS_OK;
#endif
    vitte_api_context_internal_t *api;
    vitte_status_t status;

    if (out_api == NULL) {
        vitte_api_set_last_error(
            VITTE_STATUS_ERROR_INVALID_ARGUMENT,
            "VITTE_API_INVALID_ARGUMENT",
            "out_api is NULL");

        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    *out_api = NULL;

    api = (vitte_api_context_internal_t *)
        vitte_api_allocate_zeroed(
            1u,
            sizeof(*api));

    if (api == NULL) {
        vitte_api_set_last_error(
            VITTE_STATUS_ERROR_OUT_OF_MEMORY,
            "VITTE_API_OUT_OF_MEMORY",
            "unable to allocate compiler API context");

        return VITTE_STATUS_ERROR_OUT_OF_MEMORY;
    }

    api->magic = VITTE_API_CONTEXT_MAGIC;
    api->state = VITTE_API_STATE_UNINITIALIZED;

    vitte_error_init(&api->last_error);

    status = vitte_context_init(
        &api->context);

    if (status != VITTE_STATUS_OK) {
        api->magic = 0u;
        vitte_api_deallocate(api);

        return status;
    }

    api->state = VITTE_API_STATE_READY;

    *out_api = (vitte_api_t *)api;

    return VITTE_STATUS_OK;
}

void
vitte_api_destroy(
    vitte_api_t *public_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    vitte_api_native_destroy((Api *)public_api);
    return;
#endif
    vitte_api_context_internal_t *api;

    api = (vitte_api_context_internal_t *)
        public_api;

    if (!vitte_api_context_is_valid(api)) {
        return;
    }

    if (api->state == VITTE_API_STATE_COMPILING) {
        /*
         * The synchronous public API should never normally reach this path.
         * The check nevertheless prevents destruction of live compiler state.
         */
        return;
    }

    api->state = VITTE_API_STATE_DESTROYED;

    vitte_context_destroy(
        &api->context);

    vitte_error_reset(
        &api->last_error);

    api->magic = 0u;

    vitte_api_deallocate(api);
}

/* ========================================================================= */
/* Public compilation                                                        */
/* ========================================================================= */

vitte_status_t
vitte_api_compile_source(
    vitte_api_t *public_api,
    const char *source_name,
    const char *source,
    size_t source_length,
    vitte_api_result_t **out_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    ApiResult *native_result;
    vitte_status_t native_status;

    if (out_result != NULL) {
        *out_result = NULL;
    }
    native_result = NULL;
    native_status = (vitte_status_t)vitte_api_native_compile_source(
        (Api *)public_api,
        source_name,
        source,
        source_length,
        &native_result);
    if (out_result != NULL) {
        *out_result = (vitte_api_result_t *)native_result;
    } else if (native_result != NULL) {
        vitte_api_native_result_destroy(native_result);
    }
    return native_status;
#endif
    vitte_api_context_internal_t *api;
    vitte_api_result_internal_t *result;
    vitte_status_t status;

    api = (vitte_api_context_internal_t *)
        public_api;

    result = NULL;

    status = vitte_api_compile_internal(
        api,
        source_name,
        source,
        source_length,
        &result);

    if (out_result != NULL) {
        *out_result =
            (vitte_api_result_t *)result;
    } else {
        vitte_api_result_destroy_internal(result);
    }

    return status;
}

vitte_status_t
vitte_api_compile_string(
    vitte_api_t *api,
    const char *source,
    vitte_api_result_t **out_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    ApiResult *native_result;
    vitte_status_t native_status;

    if (out_result != NULL) {
        *out_result = NULL;
    }
    native_result = NULL;
    native_status = (vitte_status_t)vitte_api_native_compile_string(
        (Api *)api,
        source,
        &native_result);
    if (out_result != NULL) {
        *out_result = (vitte_api_result_t *)native_result;
    } else if (native_result != NULL) {
        vitte_api_native_result_destroy(native_result);
    }
    return native_status;
#endif
    size_t length;

    if (source == NULL) {
        return vitte_api_compile_source(
            api,
            "<memory>",
            NULL,
            0u,
            out_result);
    }

    length = strlen(source);

    return vitte_api_compile_source(
        api,
        "<memory>",
        source,
        length,
        out_result);
}

vitte_status_t
vitte_api_compile_file(
    vitte_api_t *public_api,
    const char *path,
    vitte_api_result_t **out_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    ApiResult *native_result;
    vitte_status_t native_status;

    if (out_result != NULL) {
        *out_result = NULL;
    }
    native_result = NULL;
    native_status = (vitte_status_t)vitte_api_native_compile_file(
        (Api *)public_api,
        path,
        &native_result);
    if (out_result != NULL) {
        *out_result = (vitte_api_result_t *)native_result;
    } else if (native_result != NULL) {
        vitte_api_native_result_destroy(native_result);
    }
    return native_status;
#endif
    vitte_api_context_internal_t *api;
    char *source;
    size_t source_size;
    vitte_status_t status;

    api = (vitte_api_context_internal_t *)
        public_api;

    if (out_result != NULL) {
        *out_result = NULL;
    }

    if (!vitte_api_context_is_valid(api)) {
        return VITTE_STATUS_ERROR_INVALID_STATE;
    }

    source = NULL;
    source_size = 0u;

    status = vitte_api_read_file(
        api,
        path,
        &source,
        &source_size);

    if (status != VITTE_STATUS_OK) {
        return status;
    }

    status = vitte_api_compile_source(
        public_api,
        path,
        source,
        source_size,
        out_result);

    vitte_api_deallocate(source);

    return status;
}

/* ========================================================================= */
/* Result API                                                                */
/* ========================================================================= */

void
vitte_api_result_destroy(
    vitte_api_result_t *public_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    vitte_api_native_result_destroy((ApiResult *)public_result);
    return;
#endif
    vitte_api_result_destroy_internal(
        (vitte_api_result_internal_t *)
            public_result);
}

vitte_status_t
vitte_api_result_status(
    const vitte_api_result_t *public_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    if (public_result == NULL) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }
    return (vitte_status_t)vitte_api_native_result_status(
        (const ApiResult *)public_result);
#endif
    const vitte_api_result_internal_t *result;

    result =
        (const vitte_api_result_internal_t *)
            public_result;

    if (!vitte_api_result_is_valid(result)) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    return result->status;
}

bool
vitte_api_result_succeeded(
    const vitte_api_result_t *public_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_result != NULL &&
        vitte_api_native_result_succeeded((const ApiResult *)public_result);
#endif
    const vitte_api_result_internal_t *result;

    result =
        (const vitte_api_result_internal_t *)
            public_result;

    return vitte_api_result_is_valid(result) &&
           result->succeeded;
}

const char *
vitte_api_result_source_name(
    const vitte_api_result_t *public_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    if (public_result == NULL) {
        return NULL;
    }
    return vitte_api_native_result_source_name(
        (const ApiResult *)public_result);
#endif
    const vitte_api_result_internal_t *result;

    result =
        (const vitte_api_result_internal_t *)
            public_result;

    if (!vitte_api_result_is_valid(result)) {
        return NULL;
    }

    return result->source_name;
}

const void *
vitte_api_result_output(
    const vitte_api_result_t *public_result,
    size_t *out_size)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    const ApiResult *native_result = (const ApiResult *)public_result;
    const uint8_t *native_data;

    if (out_size != NULL) {
        *out_size = 0u;
    }
    if (native_result == NULL) {
        return NULL;
    }
    native_data = vitte_api_native_result_output_data(native_result);
    if (out_size != NULL) {
        *out_size = vitte_api_native_result_output_size(native_result);
    }
    return native_data;
#endif
    const vitte_api_result_internal_t *result;

    if (out_size != NULL) {
        *out_size = 0u;
    }

    result =
        (const vitte_api_result_internal_t *)
            public_result;

    if (!vitte_api_result_is_valid(result)) {
        return NULL;
    }

    if (out_size != NULL) {
        *out_size = result->output_size;
    }

    return result->output;
}

const char *
vitte_api_result_diagnostics(
    const vitte_api_result_t *public_result,
    size_t *out_size)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    const ApiResult *native_result = (const ApiResult *)public_result;
    const uint8_t *native_text;

    if (out_size != NULL) {
        *out_size = 0u;
    }
    if (native_result == NULL) {
        return NULL;
    }
    native_text = vitte_api_native_result_diagnostics(native_result);
    if (out_size != NULL) {
        *out_size = vitte_api_native_result_diagnostics_size(native_result);
    }
    return (const char *)native_text;
#endif
    const vitte_api_result_internal_t *result;

    if (out_size != NULL) {
        *out_size = 0u;
    }

    result =
        (const vitte_api_result_internal_t *)
            public_result;

    if (!vitte_api_result_is_valid(result)) {
        return NULL;
    }

    if (out_size != NULL) {
        *out_size = result->diagnostics_size;
    }

    return result->diagnostics;
}

size_t
vitte_api_result_error_count(
    const vitte_api_result_t *public_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_result != NULL
        ? vitte_api_native_result_error_count((const ApiResult *)public_result)
        : 0u;
#endif
    const vitte_api_result_internal_t *result;

    result =
        (const vitte_api_result_internal_t *)
            public_result;

    if (!vitte_api_result_is_valid(result)) {
        return 0u;
    }

    return result->error_count;
}

size_t
vitte_api_result_warning_count(
    const vitte_api_result_t *public_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_result != NULL
        ? vitte_api_native_result_warning_count((const ApiResult *)public_result)
        : 0u;
#endif
    const vitte_api_result_internal_t *result;

    result =
        (const vitte_api_result_internal_t *)
            public_result;

    if (!vitte_api_result_is_valid(result)) {
        return 0u;
    }

    return result->warning_count;
}

size_t
vitte_api_result_note_count(
    const vitte_api_result_t *public_result)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_result != NULL
        ? vitte_api_native_result_note_count((const ApiResult *)public_result)
        : 0u;
#endif
    const vitte_api_result_internal_t *result;

    result =
        (const vitte_api_result_internal_t *)
            public_result;

    if (!vitte_api_result_is_valid(result)) {
        return 0u;
    }

    return result->note_count;
}

/* ========================================================================= */
/* API state                                                                 */
/* ========================================================================= */

bool
vitte_api_is_valid(
    const vitte_api_t *public_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_api != NULL &&
        vitte_api_native_is_valid((const Api *)public_api);
#endif
    return vitte_api_context_is_valid(
        (const vitte_api_context_internal_t *)
            public_api);
}

bool
vitte_api_is_compiling(
    const vitte_api_t *public_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_api != NULL &&
        vitte_api_native_is_compiling((const Api *)public_api);
#endif
    const vitte_api_context_internal_t *api;

    api =
        (const vitte_api_context_internal_t *)
            public_api;

    return vitte_api_context_is_valid(api) &&
           api->state == VITTE_API_STATE_COMPILING;
}

size_t
vitte_api_compilation_count(
    const vitte_api_t *public_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_api != NULL
        ? vitte_api_native_compilation_count((const Api *)public_api)
        : 0u;
#endif
    const vitte_api_context_internal_t *api;

    api =
        (const vitte_api_context_internal_t *)
            public_api;

    if (!vitte_api_context_is_valid(api)) {
        return 0u;
    }

    return api->compilation_count;
}

size_t
vitte_api_successful_compilation_count(
    const vitte_api_t *public_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_api != NULL
        ? vitte_api_native_successful_compilation_count((const Api *)public_api)
        : 0u;
#endif
    const vitte_api_context_internal_t *api;

    api =
        (const vitte_api_context_internal_t *)
            public_api;

    if (!vitte_api_context_is_valid(api)) {
        return 0u;
    }

    return api->successful_compilation_count;
}

size_t
vitte_api_failed_compilation_count(
    const vitte_api_t *public_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    return public_api != NULL
        ? vitte_api_native_failed_compilation_count((const Api *)public_api)
        : 0u;
#endif
    const vitte_api_context_internal_t *api;

    api =
        (const vitte_api_context_internal_t *)
            public_api;

    if (!vitte_api_context_is_valid(api)) {
        return 0u;
    }

    return api->failed_compilation_count;
}

/* ========================================================================= */
/* Error access                                                              */
/* ========================================================================= */

const vitte_error_t *
vitte_api_last_error(
    const vitte_api_t *public_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    if (public_api == NULL) {
        return vitte_error_last();
    }
    vitte_error_init(&vitte_api_native_error_snapshot);
    vitte_error_set_details(
        &vitte_api_native_error_snapshot,
        (vitte_status_t)vitte_api_native_error_status((const Api *)public_api),
        vitte_api_native_error_code((const Api *)public_api),
        vitte_api_native_error_message((const Api *)public_api),
        vitte_api_native_error_details((const Api *)public_api));
    return &vitte_api_native_error_snapshot;
#endif
    const vitte_api_context_internal_t *api;

    api =
        (const vitte_api_context_internal_t *)
            public_api;

    if (!vitte_api_context_is_valid(api)) {
        return vitte_error_last();
    }

    return &api->last_error;
}

void
vitte_api_clear_error(
    vitte_api_t *public_api)
{
#ifdef VITTE_NATIVE_FRONTEND_LINKED
    if (public_api != NULL) {
        vitte_api_native_clear_error((Api *)public_api);
    }
    vitte_error_clear_last();
    return;
#endif
    vitte_api_context_internal_t *api;

    api =
        (vitte_api_context_internal_t *)
            public_api;

    if (!vitte_api_context_is_valid(api)) {
        return;
    }

    vitte_error_reset(
        &api->last_error);

    vitte_error_clear_last();
}

/* ========================================================================= */
/* Version                                                                   */
/* ========================================================================= */

const char *
vitte_api_version(void)
{
#ifdef VITTE_VERSION_STRING
    return VITTE_VERSION_STRING;
#else
    return "unknown";
#endif
}

/* ========================================================================= */
/* ABI information                                                           */
/* ========================================================================= */

uint32_t
vitte_api_version_major(void)
{
#ifdef VITTE_VERSION_MAJOR
    return (uint32_t)VITTE_VERSION_MAJOR;
#else
    return 0u;
#endif
}

uint32_t
vitte_api_version_minor(void)
{
#ifdef VITTE_VERSION_MINOR
    return (uint32_t)VITTE_VERSION_MINOR;
#else
    return 0u;
#endif
}

uint32_t
vitte_api_version_patch(void)
{
#ifdef VITTE_VERSION_PATCH
    return (uint32_t)VITTE_VERSION_PATCH;
#else
    return 0u;
#endif
}

/* ========================================================================= */
/* Build information                                                         */
/* ========================================================================= */

const char *
vitte_api_build_mode(void)
{
#if defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

const char *
vitte_api_compiler(void)
{
#if defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#elif defined(_MSC_VER)
    return "msvc";
#else
    return "unknown";
#endif
}

const char *
vitte_api_platform(void)
{
#if defined(__APPLE__) && defined(__MACH__)
    return "darwin";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "windows";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__NetBSD__)
    return "netbsd";
#else
    return "unknown";
#endif
}

const char *
vitte_api_architecture(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__riscv) && (__riscv_xlen == 64)
    return "riscv64";
#elif defined(__riscv) && (__riscv_xlen == 32)
    return "riscv32";
#else
    return "unknown";
#endif
}

/* ========================================================================= */
/* Sanity checking                                                           */
/* ========================================================================= */

bool
vitte_api_validate(
    const vitte_api_t *public_api)
{
    const vitte_api_context_internal_t *api;

    api =
        (const vitte_api_context_internal_t *)
            public_api;

    if (!vitte_api_context_is_valid(api)) {
        return false;
    }

    if (api->successful_compilation_count >
        api->compilation_count) {
        return false;
    }

    if (api->failed_compilation_count >
        api->compilation_count) {
        return false;
    }

    if (api->successful_compilation_count +
            api->failed_compilation_count >
        api->compilation_count) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* End                                                                       */
/* ========================================================================= */

#endif /* VITTE_NATIVE_FRONTEND_LINKED */
