/*
 * Vitte Compiler
 * src/main.c
 *
 * Main executable entry point.
 *
 * This translation unit intentionally remains thin compared with the compiler
 * subsystems themselves. It owns:
 *
 *   - process entry;
 *   - command-line parsing;
 *   - command dispatch;
 *   - top-level compilation lifecycle;
 *   - exit status normalization;
 *   - basic process-level diagnostics.
 *
 * Compiler implementation belongs to the dedicated subsystems:
 *
 *   source
 *   unicode
 *   scanner
 *   lexer
 *   parser
 *   scope
 *   symbol
 *   type
 *   sema
 *   constant_fold
 *   hir
 *   ir
 *   backend
 *   driver
 *   filesystem
 *   module
 *   import
 *   printer
 *   util
 *
 * ISO C17.
 */

#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Keep includes rooted in their subsystem directories.
 *
 * Do not add include/vitte directly to the compiler include search path:
 * that tree can contain names overlapping with libc headers.
 */
#include "driver/driver.h"
#include "backend/c17/ast_emitter.h"
#include "diagnostic/cli_bridge.h"
#include "filesystem/filesystem.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "printer/printer.h"
#include "sema/sema.h"
#include "source/source.h"
#include "unicode/unicode.h"
#include "util/util.h"

/* ========================================================================= */
/* Version                                                                   */
/* ========================================================================= */

#define VITTE_VERSION_MAJOR 0u
#define VITTE_VERSION_MINOR 1u
#define VITTE_VERSION_PATCH 1u

#define VITTE_VERSION_STRING "0.1.1"

#define VITTE_COMPILER_NAME "vitte"

#define VITTE_LANGUAGE_NAME "Vitte"

/* ========================================================================= */
/* Process exit codes                                                        */
/* ========================================================================= */

/*
 * Keep process exit values small and stable.
 *
 * 0:
 *   success
 *
 * 1:
 *   compilation or requested operation failed
 *
 * 2:
 *   invalid command line / usage
 *
 * 3:
 *   infrastructure/internal failure
 */
typedef enum vitte_main_exit_code {
    VITTE_MAIN_EXIT_SUCCESS = 0,
    VITTE_MAIN_EXIT_FAILURE = 1,
    VITTE_MAIN_EXIT_USAGE = 2,
    VITTE_MAIN_EXIT_INTERNAL = 3
} vitte_main_exit_code_t;

/* ========================================================================= */
/* Command                                                                   */
/* ========================================================================= */

typedef enum vitte_main_command {
    VITTE_MAIN_COMMAND_COMPILE = 0,
    VITTE_MAIN_COMMAND_CHECK,
    VITTE_MAIN_COMMAND_RUN,
    VITTE_MAIN_COMMAND_LEX,
    VITTE_MAIN_COMMAND_PARSE,
    VITTE_MAIN_COMMAND_HELP,
    VITTE_MAIN_COMMAND_VERSION
} vitte_main_command_t;

/* ========================================================================= */
/* Diagnostic format                                                         */
/* ========================================================================= */

typedef enum vitte_main_diagnostic_format {
    VITTE_MAIN_DIAGNOSTIC_TERMINAL = 0,
    VITTE_MAIN_DIAGNOSTIC_JSON,
    VITTE_MAIN_DIAGNOSTIC_SARIF
} vitte_main_diagnostic_format_t;

static vitte_main_diagnostic_format_t
    vitte_main_active_diagnostic_format =
        VITTE_MAIN_DIAGNOSTIC_TERMINAL;

/* ========================================================================= */
/* Options                                                                   */
/* ========================================================================= */

typedef struct vitte_main_options {
    vitte_main_command_t command;

    const char *input_path;
    const char *output_path;

    vitte_main_diagnostic_format_t diagnostic_format;

    bool color;
    bool quiet;
    bool verbose;

    bool emit_tokens;
    bool emit_ast;
    bool emit_hir;
    bool emit_ir;
    bool emit_c;
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
} vitte_main_options_t;

/* ========================================================================= */
/* Loaded source                                                             */
/* ========================================================================= */

typedef struct vitte_main_file {
    unsigned char *data;
    size_t length;
} vitte_main_file_t;

typedef struct vitte_main_module_set {
    char **paths;
    size_t count;
    size_t capacity;
    unsigned char *source;
    size_t source_length;
    size_t source_capacity;
} vitte_main_module_set_t;

typedef struct vitte_main_driver_context {
    const vitte_main_options_t *options;

    vitte_main_file_t file;
    vitte_source_context_t sources;
    vitte_source_id_t source_id;
    vitte_lexer_t lexer;
    vitte_parser_t parser;
    vitte_sema_t sema;

    bool source_initialized;
    bool lexer_initialized;
    bool parser_initialized;
    bool sema_initialized;
} vitte_main_driver_context_t;

#define VITTE_MAIN_MODULE_PATH_MAX 4096u
#define VITTE_MAIN_MODULE_DEPTH_MAX 128u

static char vitte_main_executable_path[VITTE_MAIN_MODULE_PATH_MAX];

static bool
vitte_main_read_file(
    const char *path,
    vitte_main_file_t *file);

static void
vitte_main_error_argument(
    const char *message,
    const char *argument);

static void
vitte_main_report_path_error(
    const char *code,
    const char *message,
    const char *path,
    const char *reason);

static void
vitte_main_report_process_warning(
    const char *message,
    const char *details);

static bool
vitte_main_render_parser_diagnostics(
    const vitte_main_driver_context_t *context,
    bool *rendered);

static bool
vitte_main_render_lexer_diagnostic(
    const vitte_main_driver_context_t *context);

static bool
vitte_main_buffer_append(
    unsigned char **buffer,
    size_t *length,
    size_t *capacity,
    const unsigned char *data,
    size_t data_length)
{
    size_t required;
    size_t next_capacity;
    unsigned char *next_buffer;

    if (buffer == NULL || length == NULL || capacity == NULL ||
        (data == NULL && data_length != 0u) ||
        data_length > SIZE_MAX - *length) {
        return false;
    }

    required = *length + data_length;
    if (required > *capacity) {
        next_capacity = *capacity != 0u ? *capacity : 4096u;
        while (next_capacity < required) {
            if (next_capacity > SIZE_MAX / 2u) {
                next_capacity = required;
                break;
            }
            next_capacity *= 2u;
        }

        next_buffer = (unsigned char *)realloc(*buffer, next_capacity);
        if (next_buffer == NULL) {
            return false;
        }

        *buffer = next_buffer;
        *capacity = next_capacity;
    }

    if (data_length != 0u) {
        memcpy(*buffer + *length, data, data_length);
    }
    *length = required;
    return true;
}

static bool
vitte_main_module_path_seen(
    const vitte_main_module_set_t *modules,
    const char *path)
{
    size_t index;

    for (index = 0u; index < modules->count; ++index) {
        if (strcmp(modules->paths[index], path) == 0) {
            return true;
        }
    }
    return false;
}

static bool
vitte_main_module_record_path(
    vitte_main_module_set_t *modules,
    const char *path)
{
    size_t next_capacity;
    char **next_paths;
    char *copy;

    if (modules->count == modules->capacity) {
        next_capacity = modules->capacity != 0u
            ? modules->capacity * 2u
            : 16u;
        if (next_capacity < modules->capacity ||
            next_capacity > SIZE_MAX / sizeof(*next_paths)) {
            return false;
        }

        next_paths = (char **)realloc(
            modules->paths,
            next_capacity * sizeof(*next_paths));
        if (next_paths == NULL) {
            return false;
        }
        modules->paths = next_paths;
        modules->capacity = next_capacity;
    }

    copy = (char *)malloc(strlen(path) + 1u);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, path, strlen(path) + 1u);
    modules->paths[modules->count++] = copy;
    return true;
}

static bool
vitte_main_module_try_candidate(
    const char *base,
    const char *module_path,
    char *resolved,
    size_t resolved_capacity)
{
    char candidate[VITTE_MAIN_MODULE_PATH_MAX];
    FILE *file;
    int written;

    if (base == NULL || module_path == NULL) {
        return false;
    }

    written = snprintf(
        candidate,
        sizeof(candidate),
        "%s/%s.vit",
        base,
        module_path);
    if (written < 0 || (size_t)written >= sizeof(candidate)) {
        return false;
    }
    file = fopen(candidate, "rb");
    if (file != NULL) {
        (void)fclose(file);
        if (strlen(candidate) >= resolved_capacity) {
            return false;
        }
        memcpy(resolved, candidate, strlen(candidate) + 1u);
        return true;
    }

    written = snprintf(
        candidate,
        sizeof(candidate),
        "%s/%s/mod.vit",
        base,
        module_path);
    if (written < 0 || (size_t)written >= sizeof(candidate)) {
        return false;
    }
    file = fopen(candidate, "rb");
    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    if (strlen(candidate) >= resolved_capacity) {
        return false;
    }
    memcpy(resolved, candidate, strlen(candidate) + 1u);
    return true;
}

static bool
vitte_main_module_try_root(
    const char *root,
    const char *module_path,
    char *resolved,
    size_t resolved_capacity)
{
    if (root == NULL || module_path == NULL) {
        return false;
    }

    return vitte_main_module_try_candidate(
        root,
        module_path,
        resolved,
        resolved_capacity);
}

static bool
vitte_main_module_resolve_installed(
    const char *module_path,
    char *resolved,
    size_t resolved_capacity)
{
    const char *configured_paths;
    const char *cursor;
    char root[VITTE_MAIN_MODULE_PATH_MAX];
    char executable_directory[VITTE_MAIN_MODULE_PATH_MAX];
    char *slash;
    size_t length;
    int written;

    configured_paths = getenv("VITTE_MODULE_PATH");
    cursor = configured_paths;
    while (cursor != NULL && *cursor != '\0') {
        const char *separator;

        separator = strchr(cursor, ':');
        length = separator != NULL
            ? (size_t)(separator - cursor)
            : strlen(cursor);
        if (length != 0u && length < sizeof(root)) {
            memcpy(root, cursor, length);
            root[length] = '\0';
            if (vitte_main_module_try_root(
                    root,
                    module_path,
                    resolved,
                    resolved_capacity)) {
                return true;
            }
        }
        if (separator == NULL) {
            break;
        }
        cursor = separator + 1;
    }

    if (vitte_main_executable_path[0] != '\0' &&
        strlen(vitte_main_executable_path) < sizeof(executable_directory)) {
        memcpy(
            executable_directory,
            vitte_main_executable_path,
            strlen(vitte_main_executable_path) + 1u);
        slash = strrchr(executable_directory, '/');
        if (slash != NULL) {
            *slash = '\0';
            slash = strrchr(executable_directory, '/');
            if (slash != NULL &&
                (strcmp(slash + 1, "bin") == 0 ||
                 strcmp(slash + 1, "sbin") == 0)) {
                *slash = '\0';
                written = snprintf(
                    root,
                    sizeof(root),
                    "%s/share/vitte/modules",
                    executable_directory);
                if (written >= 0 &&
                    (size_t)written < sizeof(root) &&
                    vitte_main_module_try_root(
                        root,
                        module_path,
                        resolved,
                        resolved_capacity)) {
                    return true;
                }
            }
        }
    }

    if (vitte_main_module_try_root(
            "/usr/local/share/vitte/modules",
            module_path,
            resolved,
            resolved_capacity) ||
        vitte_main_module_try_root(
            "/usr/share/vitte/modules",
            module_path,
            resolved,
            resolved_capacity)) {
        return true;
    }

    return false;
}

static void
vitte_main_set_executable_path(
    const char *argument_zero)
{
    char candidate[VITTE_MAIN_MODULE_PATH_MAX];
    char working_directory[VITTE_MAIN_MODULE_PATH_MAX];
    const char *path;
    const char *cursor;
    const char *separator;
    size_t directory_length;
    int written;

    vitte_main_executable_path[0] = '\0';
    if (argument_zero == NULL || argument_zero[0] == '\0') {
        return;
    }

    if (strchr(argument_zero, '/') != NULL) {
        if (argument_zero[0] == '/') {
            written = snprintf(
                vitte_main_executable_path,
                sizeof(vitte_main_executable_path),
                "%s",
                argument_zero);
        } else if (getcwd(
                       working_directory,
                       sizeof(working_directory)) != NULL) {
            written = snprintf(
                vitte_main_executable_path,
                sizeof(vitte_main_executable_path),
                "%s/%s",
                working_directory,
                argument_zero);
        } else {
            return;
        }
        if (written < 0 ||
            (size_t)written >= sizeof(vitte_main_executable_path)) {
            vitte_main_executable_path[0] = '\0';
        }
        return;
    }

    path = getenv("PATH");
    cursor = path;
    while (cursor != NULL && *cursor != '\0') {
        separator = strchr(cursor, ':');
        directory_length = separator != NULL
            ? (size_t)(separator - cursor)
            : strlen(cursor);
        if (directory_length != 0u &&
            directory_length < sizeof(candidate)) {
            memcpy(candidate, cursor, directory_length);
            candidate[directory_length] = '\0';
            written = snprintf(
                vitte_main_executable_path,
                sizeof(vitte_main_executable_path),
                "%s/%s",
                candidate,
                argument_zero);
            if (written >= 0 &&
                (size_t)written < sizeof(vitte_main_executable_path) &&
                access(vitte_main_executable_path, X_OK) == 0) {
                return;
            }
        }
        if (separator == NULL) {
            break;
        }
        cursor = separator + 1;
    }
    vitte_main_executable_path[0] = '\0';
}

static bool
vitte_main_module_resolve_path(
    const char *importer_path,
    const char *requested,
    char *resolved,
    size_t resolved_capacity)
{
    char module_path[VITTE_MAIN_MODULE_PATH_MAX];
    char ancestor[VITTE_MAIN_MODULE_PATH_MAX];
    char absolute_importer[VITTE_MAIN_MODULE_PATH_MAX];
    char working_directory[VITTE_MAIN_MODULE_PATH_MAX];
    const char *cursor;
    char *slash;
    size_t path_length;
    size_t depth;
    size_t end;
    int written;

    if (importer_path == NULL || requested == NULL ||
        resolved == NULL || resolved_capacity == 0u) {
        return false;
    }

    path_length = strlen(requested);
    if (path_length == 0u || path_length >= sizeof(module_path)) {
        return false;
    }

    end = 0u;
    cursor = requested;
    while (*cursor != '\0') {
        if (cursor[0] == ':' && cursor[1] == ':') {
            if (end == 0u || module_path[end - 1u] == '/') {
                return false;
            }
            module_path[end++] = '/';
            cursor += 2;
        } else if (*cursor == '.') {
            if (end == 0u || module_path[end - 1u] == '/') {
                return false;
            }
            module_path[end++] = '/';
            ++cursor;
        } else if (isalnum((unsigned char)*cursor) ||
                   *cursor == '_' || *cursor == '/') {
            module_path[end++] = *cursor++;
        } else {
            break;
        }
    }
    while (end != 0u && module_path[end - 1u] == '/') {
        --end;
    }
    if (end == 0u) {
        return false;
    }
    module_path[end] = '\0';

    if (importer_path[0] == '/') {
        written = snprintf(
            absolute_importer,
            sizeof(absolute_importer),
            "%s",
            importer_path);
    } else if (getcwd(
                   working_directory,
                   sizeof(working_directory)) == NULL) {
        return false;
    } else {
        written = snprintf(
            absolute_importer,
            sizeof(absolute_importer),
            "%s/%s",
            working_directory,
            importer_path);
    }
    if (written < 0 ||
        (size_t)written >= sizeof(absolute_importer)) {
        return false;
    }

    memcpy(
        ancestor,
        absolute_importer,
        strlen(absolute_importer) + 1u);
    slash = strrchr(ancestor, '/');
    if (slash == NULL) {
        memcpy(ancestor, ".", 2u);
    } else if (slash == ancestor) {
        ancestor[1] = '\0';
    } else {
        *slash = '\0';
    }

    for (depth = 0u; depth < 12u; ++depth) {
        if (vitte_main_module_try_candidate(
                ancestor,
                module_path,
                resolved,
                resolved_capacity)) {
            return true;
        }
        if (strcmp(ancestor, "/") == 0 ||
            strcmp(ancestor, ".") == 0) {
            break;
        }

        slash = strrchr(ancestor, '/');
        if (slash == NULL) {
            memcpy(ancestor, ".", 2u);
        } else if (slash == ancestor) {
            ancestor[1] = '\0';
        } else {
            *slash = '\0';
        }
    }

    return vitte_main_module_resolve_installed(
        module_path,
        resolved,
        resolved_capacity);
}

static bool
vitte_main_expand_module(
    vitte_main_module_set_t *modules,
    const char *path,
    size_t depth,
    const char *namespace_name);

static bool
vitte_main_append_namespace_open(
    vitte_main_module_set_t *modules,
    const char *namespace_name,
    size_t *namespace_depth)
{
    const char *segment;
    size_t depth;

    if (modules == NULL ||
        namespace_name == NULL ||
        namespace_depth == NULL) {
        return false;
    }

    segment = namespace_name;
    depth = 0u;
    while (*segment != '\0') {
        const char *separator;
        size_t segment_length;

        separator = strstr(segment, "::");
        segment_length = separator != NULL
            ? (size_t)(separator - segment)
            : strlen(segment);
        if (segment_length == 0u ||
            !vitte_main_buffer_append(
                &modules->source,
                &modules->source_length,
                &modules->source_capacity,
                (const unsigned char *)"export space ",
                13u) ||
            !vitte_main_buffer_append(
                &modules->source,
                &modules->source_length,
                &modules->source_capacity,
                (const unsigned char *)segment,
                segment_length) ||
            !vitte_main_buffer_append(
                &modules->source,
                &modules->source_length,
                &modules->source_capacity,
                (const unsigned char *)" {\n",
                3u)) {
            return false;
        }

        ++depth;
        if (separator == NULL) {
            break;
        }
        segment = separator + 2u;
        if (*segment == '\0') {
            return false;
        }
    }

    *namespace_depth = depth;
    return depth != 0u;
}

static bool
vitte_main_append_namespace_close(
    vitte_main_module_set_t *modules,
    size_t namespace_depth)
{
    while (namespace_depth != 0u) {
        if (!vitte_main_buffer_append(
                &modules->source,
                &modules->source_length,
                &modules->source_capacity,
                (const unsigned char *)"}\n",
                2u)) {
            return false;
        }
        --namespace_depth;
    }
    return true;
}

static bool
vitte_main_expand_module(
    vitte_main_module_set_t *modules,
    const char *path,
    size_t depth,
    const char *namespace_name)
{
    vitte_main_file_t file;
    size_t offset;
    size_t namespace_depth;

    if (modules == NULL || path == NULL ||
        depth > VITTE_MAIN_MODULE_DEPTH_MAX) {
        return false;
    }
    if (vitte_main_module_path_seen(modules, path)) {
        return true;
    }
    if (!vitte_main_module_record_path(modules, path)) {
        return false;
    }

    memset(&file, 0, sizeof(file));
    namespace_depth = 0u;
    if (!vitte_main_read_file(path, &file)) {
        vitte_main_error_argument("cannot load imported module", path);
        return false;
    }

    /*
     * Expand dependencies before emitting this module's declarations so
     * imported module scopes remain siblings rather than nested scopes.
     */
    offset = 0u;
    while (offset < file.length) {
        size_t line_end;
        size_t directive_start;
        size_t directive_end;

        line_end = offset;
        while (line_end < file.length && file.data[line_end] != '\n') {
            ++line_end;
        }
        directive_start = offset;
        while (directive_start < line_end &&
               (file.data[directive_start] == ' ' ||
                file.data[directive_start] == '\t')) {
            ++directive_start;
        }

        if (line_end - directive_start < 4u ||
            memcmp(file.data + directive_start, "use ", 4u) != 0) {
            offset = line_end < file.length ? line_end + 1u : line_end;
            continue;
        }

        directive_end = offset;
        while (directive_end < file.length &&
               file.data[directive_end] != ';') {
            ++directive_end;
        }
        if (directive_end == file.length) {
            free(file.data);
            vitte_main_error_argument(
                "unterminated module directive in",
                path);
            return false;
        }

        {
            char requested[VITTE_MAIN_MODULE_PATH_MAX];
            char imported_path[VITTE_MAIN_MODULE_PATH_MAX];
            size_t name_start;
            size_t name_end;
            size_t name_length;

            name_start = directive_start + 4u;
            while (name_start < directive_end &&
                   isspace(file.data[name_start])) {
                ++name_start;
            }
            name_end = name_start;
            while (name_end < directive_end &&
                   file.data[name_end] != '{' &&
                   file.data[name_end] != ';' &&
                   !isspace(file.data[name_end])) {
                ++name_end;
            }
            while (name_end > name_start &&
                   (file.data[name_end - 1u] == ':' ||
                    file.data[name_end - 1u] == '*')) {
                --name_end;
            }
            name_length = name_end - name_start;
            if (name_length == 0u ||
                name_length >= sizeof(requested)) {
                free(file.data);
                vitte_main_error_argument(
                    "invalid module path in",
                    path);
                return false;
            }

            memcpy(requested, file.data + name_start, name_length);
            requested[name_length] = '\0';

            if (!vitte_main_module_resolve_path(
                    path,
                    requested,
                    imported_path,
                    sizeof(imported_path))) {
                free(file.data);
                vitte_main_error_argument(
                    "cannot resolve imported module",
                    requested);
                return false;
            }
            if (!vitte_main_expand_module(
                    modules,
                    imported_path,
                    depth + 1u,
                    requested)) {
                free(file.data);
                return false;
            }
        }

        offset = directive_end + 1u;
        while (offset < file.length && file.data[offset] != '\n') {
            ++offset;
        }
        if (offset < file.length) {
            ++offset;
        }
    }

    if (namespace_name != NULL) {
        if (!vitte_main_append_namespace_open(
                modules,
                namespace_name,
                &namespace_depth)) {
            free(file.data);
            return false;
        }
    }

    offset = 0u;
    while (offset < file.length) {
        size_t line_end;
        size_t directive_start;
        size_t directive_end;
        const char *keyword;

        line_end = offset;
        while (line_end < file.length && file.data[line_end] != '\n') {
            ++line_end;
        }
        directive_start = offset;
        while (directive_start < line_end &&
               (file.data[directive_start] == ' ' ||
                file.data[directive_start] == '\t')) {
            ++directive_start;
        }

        keyword = NULL;
        if (line_end - directive_start >= 4u &&
            memcmp(file.data + directive_start, "use ", 4u) == 0) {
            keyword = "use";
        } else if (line_end - directive_start >= 6u &&
                   memcmp(
                       file.data + directive_start,
                       "space ",
                       6u) == 0) {
            keyword = "space";
        }

        if (keyword == NULL) {
            size_t line_length;
            line_length = line_end - offset;
            if (!vitte_main_buffer_append(
                    &modules->source,
                    &modules->source_length,
                    &modules->source_capacity,
                    file.data + offset,
                    line_length) ||
                (line_end < file.length &&
                 !vitte_main_buffer_append(
                     &modules->source,
                     &modules->source_length,
                     &modules->source_capacity,
                     (const unsigned char *)"\n",
                     1u))) {
                free(file.data);
                return false;
            }
            offset = line_end < file.length ? line_end + 1u : line_end;
            continue;
        }

        if (strcmp(keyword, "use") == 0) {
            size_t directive_length;

            directive_end = offset;
            while (directive_end < file.length &&
                   file.data[directive_end] != ';') {
                ++directive_end;
            }
            if (directive_end == file.length) {
                free(file.data);
                vitte_main_error_argument(
                    "unterminated module directive in",
                    path);
                return false;
            }

            directive_length = directive_end - offset + 1u;
            if (!vitte_main_buffer_append(
                    &modules->source,
                    &modules->source_length,
                    &modules->source_capacity,
                    file.data + offset,
                    directive_length) ||
                !vitte_main_buffer_append(
                    &modules->source,
                    &modules->source_length,
                    &modules->source_capacity,
                    (const unsigned char *)"\n",
                    1u)) {
                free(file.data);
                return false;
            }

            offset = directive_end + 1u;
            while (offset < file.length && file.data[offset] != '\n') {
                ++offset;
            }
            if (offset < file.length) {
                ++offset;
            }
            continue;
        }

        directive_end = offset;
        if (strcmp(keyword, "space") == 0) {
            while (directive_end < line_end &&
                   file.data[directive_end] != ';') {
                ++directive_end;
            }
            if (directive_end == line_end) {
                directive_end = line_end;
            }
        } else {
            while (directive_end < file.length &&
                   file.data[directive_end] != ';') {
                ++directive_end;
            }
        }
        if (directive_end == file.length &&
            strcmp(keyword, "use") == 0) {
            free(file.data);
            vitte_main_error_argument(
                "unterminated module directive in",
                path);
            return false;
        }

        while (offset <= directive_end) {
            if (file.data[offset] == '\n' &&
                !vitte_main_buffer_append(
                    &modules->source,
                    &modules->source_length,
                    &modules->source_capacity,
                    (const unsigned char *)"\n",
                    1u)) {
                free(file.data);
                return false;
            }
            ++offset;
        }
    }

    if (modules->source_length == 0u ||
        modules->source[modules->source_length - 1u] != '\n') {
        if (!vitte_main_buffer_append(
                &modules->source,
                &modules->source_length,
                &modules->source_capacity,
                (const unsigned char *)"\n",
                1u)) {
            free(file.data);
            return false;
        }
    }

    if (namespace_name != NULL &&
        !vitte_main_append_namespace_close(
            modules,
            namespace_depth)) {
        free(file.data);
        return false;
    }

    free(file.data);
    return true;
}

static void
vitte_main_module_set_destroy(
    vitte_main_module_set_t *modules)
{
    size_t index;

    if (modules == NULL) {
        return;
    }
    for (index = 0u; index < modules->count; ++index) {
        free(modules->paths[index]);
    }
    free(modules->paths);
}

/* ========================================================================= */
/* Utility                                                                   */
/* ========================================================================= */

static bool
vitte_main_string_equal(
    const char *left,
    const char *right)
{
    if (left == NULL ||
        right == NULL) {
        return false;
    }

    return strcmp(left, right) == 0;
}

static bool
vitte_main_string_starts_with(
    const char *string,
    const char *prefix)
{
    size_t string_length;
    size_t prefix_length;

    if (string == NULL ||
        prefix == NULL) {
        return false;
    }

    string_length = strlen(string);
    prefix_length = strlen(prefix);

    if (prefix_length > string_length) {
        return false;
    }

    return memcmp(
               string,
               prefix,
               prefix_length) == 0;
}

/* ========================================================================= */
/* Process diagnostics                                                       */
/* ========================================================================= */

static void
vitte_main_report_process_error(
    const char *code,
    const char *message,
    const char *details)
{
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
            vitte_main_active_diagnostic_format ==
                    VITTE_MAIN_DIAGNOSTIC_JSON
                ? VITTE_CLI_DIAGNOSTIC_JSON
                : vitte_main_active_diagnostic_format ==
                        VITTE_MAIN_DIAGNOSTIC_SARIF
                    ? VITTE_CLI_DIAGNOSTIC_SARIF
                    : VITTE_CLI_DIAGNOSTIC_TERMINAL)) {
        (void)fprintf(
            stderr,
            "%s: error: %s\n",
            VITTE_COMPILER_NAME,
            message != NULL ? message : "compiler operation failed");
    }
}

static void
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

static void
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

static void
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

static void
vitte_main_report_process_warning(
    const char *message,
    const char *details)
{
    vitte_cli_diagnostic_format_t format;

    format = vitte_main_active_diagnostic_format ==
            VITTE_MAIN_DIAGNOSTIC_JSON
        ? VITTE_CLI_DIAGNOSTIC_JSON
        : vitte_main_active_diagnostic_format ==
                VITTE_MAIN_DIAGNOSTIC_SARIF
            ? VITTE_CLI_DIAGNOSTIC_SARIF
            : VITTE_CLI_DIAGNOSTIC_TERMINAL;

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

static void
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

static void
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
/* Help                                                                      */
/* ========================================================================= */

static void
vitte_main_print_usage(
    FILE *stream)
{
    if (stream == NULL) {
        return;
    }

    (void)fprintf(
        stream,
        "Usage:\n"
        "  vitte [command] [options] <input.vit>\n"
        "\n"
        "Commands:\n"
        "  compile      Analyze and compile a source file to a native executable\n"
        "  check        Validate UTF-8 and syntax\n"
        "  run          Compile and run a program\n"
        "  lex          Tokenize a source file\n"
        "  parse        Parse a source file\n"
        "  help         Display this help\n"
        "  version      Display compiler version\n"
        "\n"
        "The default command is 'compile'.\n"
        "\n"
        "General options:\n"
        "  -h, --help             Display help\n"
        "  -V, --version          Display compiler version\n"
        "  -q, --quiet            Suppress non-error output\n"
        "  -v, --verbose          Verbose compiler output\n"
        "      --color             Force colored diagnostics\n"
        "      --no-color          Disable colored diagnostics\n"
        "      --diagnostic=FMT    terminal, json, sarif\n"
        "      --timings           Display compiler phase timings\n"
        "\n"
        "Compiler inspection:\n"
        "      --emit-tokens       Print lexer tokens\n"
        "      --emit-ast          Print AST summary\n"
        "\n"
        "Pipeline control:\n"
        "      --stop-after-lexer\n"
        "      --stop-after-parser\n"
        "      --stop-after-sema\n"
        "\n"
        "Source validation:\n"
        "      --validate-utf8\n"
        "      --no-validate-utf8\n"
        "\n"
        "Module lookup:\n"
        "  Installed modules are searched beside the compiler installation.\n"
        "  VITTE_MODULE_PATH adds roots separated by the host path-list separator.\n"
        "\n"
        "Compilation:\n"
        "  'check' validates UTF-8 and syntax.\n"
        "  'compile' emits bootstrap C17 and invokes the system C compiler.\n"
        "  Files containing only test declarations are built as test executables.\n"
        "      --debug-info        Emit DWARF symbols and source line mappings\n"
        "      --debug             Alias for --debug-info\n"
        "      --debug-probes      Enable the legacy compiler probe runtime\n"
        "      --line-directives   Emit #line mappings in generated C\n"
        "\n"
        "Run:\n"
        "  vitte run program.vit -- [program arguments]\n"
        "\n"
        "Examples:\n"
        "  vitte hello.vit\n"
        "  vitte compile hello.vit -o hello\n"
        "  vitte check hello.vit\n"
        "  vitte lex hello.vit\n"
        "  vitte parse hello.vit --emit-ast\n"
        "  vitte run hello.vit\n"
        "  vitte run hello.vit -- arg1 arg2\n"
        "  vitte check hello.vit --diagnostic=json\n");
}

static void
vitte_main_print_version(void)
{
    (void)printf(
        "%s %s\n"
        "%s compiler\n"
        "language: %s\n"
        "frontend: C17 bootstrap\n",
        VITTE_COMPILER_NAME,
        VITTE_VERSION_STRING,
        VITTE_LANGUAGE_NAME,
        VITTE_LANGUAGE_NAME);
}

/* ========================================================================= */
/* Option defaults                                                           */
/* ========================================================================= */

static void
vitte_main_options_init(
    vitte_main_options_t *options)
{
    if (options == NULL) {
        return;
    }

    memset(
        options,
        0,
        sizeof(*options));

    options->command =
        VITTE_MAIN_COMMAND_COMPILE;

    options->diagnostic_format =
        VITTE_MAIN_DIAGNOSTIC_TERMINAL;
    vitte_main_active_diagnostic_format =
        VITTE_MAIN_DIAGNOSTIC_TERMINAL;

    options->color = true;
    options->validate_utf8 = true;
}

/* ========================================================================= */
/* Command parser                                                            */
/* ========================================================================= */

static bool
vitte_main_parse_command(
    const char *argument,
    vitte_main_command_t *command)
{
    if (argument == NULL ||
        command == NULL) {
        return false;
    }

    if (vitte_main_string_equal(
            argument,
            "compile")) {
        *command =
            VITTE_MAIN_COMMAND_COMPILE;

        return true;
    }

    if (vitte_main_string_equal(
            argument,
            "check")) {
        *command =
            VITTE_MAIN_COMMAND_CHECK;

        return true;
    }

    if (vitte_main_string_equal(
            argument,
            "run")) {
        *command =
            VITTE_MAIN_COMMAND_RUN;

        return true;
    }

    if (vitte_main_string_equal(
            argument,
            "lex")) {
        *command =
            VITTE_MAIN_COMMAND_LEX;

        return true;
    }

    if (vitte_main_string_equal(
            argument,
            "parse")) {
        *command =
            VITTE_MAIN_COMMAND_PARSE;

        return true;
    }

    if (vitte_main_string_equal(
            argument,
            "help")) {
        *command =
            VITTE_MAIN_COMMAND_HELP;

        return true;
    }

    if (vitte_main_string_equal(
            argument,
            "version")) {
        *command =
            VITTE_MAIN_COMMAND_VERSION;

        return true;
    }

    return false;
}

/* ========================================================================= */
/* Diagnostic format parser                                                  */
/* ========================================================================= */

static bool
vitte_main_parse_diagnostic_format(
    const char *format,
    vitte_main_diagnostic_format_t *result)
{
    if (format == NULL ||
        result == NULL) {
        return false;
    }

    if (vitte_main_string_equal(
            format,
            "terminal")) {
        *result =
            VITTE_MAIN_DIAGNOSTIC_TERMINAL;

        return true;
    }

    if (vitte_main_string_equal(
            format,
            "json")) {
        *result =
            VITTE_MAIN_DIAGNOSTIC_JSON;

        return true;
    }

    if (vitte_main_string_equal(
            format,
            "sarif")) {
        *result =
            VITTE_MAIN_DIAGNOSTIC_SARIF;

        return true;
    }

    return false;
}

/* ========================================================================= */
/* CLI                                                                       */
/* ========================================================================= */

static bool
vitte_main_parse_options(
    int argc,
    char **argv,
    vitte_main_options_t *options)
{
    int index;
    bool command_seen;
    bool positional_seen;

    if (argc < 0 ||
        argv == NULL ||
        options == NULL) {
        return false;
    }

    vitte_main_options_init(
        options);

    command_seen = false;
    positional_seen = false;

    for (index = 1;
         index < argc;
         ++index) {
        const char *argument =
            argv[index];

        if (argument == NULL) {
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--")) {
            if (options->command ==
                VITTE_MAIN_COMMAND_RUN) {
                options->run_argc =
                    argc - index - 1;

                options->run_argv =
                    argv + index + 1;

                break;
            }

            /*
             * Outside `run`, -- means the remaining argument is positional.
             */
            if (index + 1 < argc) {
                if (positional_seen) {
                    vitte_main_error(
                        "multiple input files are not supported by this entry point");

                    return false;
                }

                options->input_path =
                    argv[index + 1];

                positional_seen = true;

                if (index + 2 < argc) {
                    vitte_main_error(
                        "unexpected arguments after input file");

                    return false;
                }
            }

            break;
        }

        if (!command_seen &&
            !positional_seen &&
            argument[0] != '-') {
            vitte_main_command_t command;

            if (vitte_main_parse_command(
                    argument,
                    &command)) {
                options->command = command;
                command_seen = true;

                continue;
            }
        }

        if (vitte_main_string_equal(
                argument,
                "-h") ||
            vitte_main_string_equal(
                argument,
                "--help")) {
            options->command =
                VITTE_MAIN_COMMAND_HELP;

            return true;
        }

        if (vitte_main_string_equal(
                argument,
                "-V") ||
            vitte_main_string_equal(
                argument,
                "--version")) {
            options->command =
                VITTE_MAIN_COMMAND_VERSION;

            return true;
        }

        if (vitte_main_string_equal(
                argument,
                "-q") ||
            vitte_main_string_equal(
                argument,
                "--quiet")) {
            options->quiet = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "-v") ||
            vitte_main_string_equal(
                argument,
                "--verbose")) {
            options->verbose = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--color")) {
            options->color = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--no-color")) {
            options->color = false;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--validate-utf8")) {
            options->validate_utf8 = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--no-validate-utf8")) {
            options->validate_utf8 = false;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--timings")) {
            options->show_timings = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--emit-tokens")) {
            options->emit_tokens = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--emit-ast")) {
            options->emit_ast = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--emit-hir")) {
            options->emit_hir = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--emit-ir")) {
            options->emit_ir = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--emit-c")) {
            options->emit_c = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--debug-info") ||
            vitte_main_string_equal(
                argument,
                "--debug")) {
            options->debug_info = true;
            options->emit_line_directives = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--debug-probes")) {
            options->debug_info = true;
            options->debug_probes = true;
            options->emit_line_directives = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--line-directives")) {
            options->emit_line_directives = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--stop-after-lexer")) {
            options->stop_after_lexer = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--stop-after-parser")) {
            options->stop_after_parser = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "--stop-after-sema")) {
            options->stop_after_sema = true;
            continue;
        }

        if (vitte_main_string_equal(
                argument,
                "-o")) {
            if (index + 1 >= argc) {
                vitte_main_error(
                    "missing path after -o");

                return false;
            }

            index++;

            options->output_path =
                argv[index];

            continue;
        }

        if (vitte_main_string_starts_with(
                argument,
                "--diagnostic=")) {
            const char *format =
                argument +
                strlen("--diagnostic=");

            if (!vitte_main_parse_diagnostic_format(
                    format,
                    &options->diagnostic_format)) {
                vitte_main_error_argument(
                    "unknown diagnostic format",
                    format);

                return false;
            }

            vitte_main_active_diagnostic_format =
                options->diagnostic_format;
            continue;
        }

        if (argument[0] == '-') {
            vitte_main_error_argument(
                "unknown option",
                argument);

            return false;
        }

        if (positional_seen) {
            vitte_main_error_argument(
                "unexpected positional argument",
                argument);

            return false;
        }

        options->input_path =
            argument;

        positional_seen = true;
    }

    if (options->command ==
            VITTE_MAIN_COMMAND_HELP ||
        options->command ==
            VITTE_MAIN_COMMAND_VERSION) {
        return true;
    }

    if (options->input_path == NULL) {
        vitte_main_error(
            "no input file");

        return false;
    }

    if (options->quiet &&
        options->verbose) {
        /*
         * Quiet wins. This is not a fatal CLI contradiction.
         */
        options->verbose = false;
    }

    if (options->stop_after_lexer &&
        options->stop_after_parser) {
        vitte_main_error(
            "--stop-after-lexer and --stop-after-parser cannot be used together");

        return false;
    }

    if (options->stop_after_lexer &&
        options->stop_after_sema) {
        vitte_main_error(
            "--stop-after-lexer and --stop-after-sema cannot be used together");

        return false;
    }

    if (options->stop_after_parser &&
        options->stop_after_sema) {
        vitte_main_error(
            "--stop-after-parser and --stop-after-sema cannot be used together");

        return false;
    }

    return true;
}

/* ========================================================================= */
/* File loading                                                              */
/* ========================================================================= */

/*
 * main.c deliberately uses the C runtime here rather than guessing the exact
 * filesystem subsystem API. The compiler filesystem subsystem can later own
 * this operation through the driver without changing the source/frontend
 * contracts.
 */
static bool
vitte_main_read_file(
    const char *path,
    vitte_main_file_t *file)
{
    FILE *stream;
    long end;
    size_t length;
    unsigned char *data;
    size_t read_count;

    if (path == NULL ||
        file == NULL) {
        return false;
    }

    file->data = NULL;
    file->length = 0u;

    stream =
        fopen(
            path,
            "rb");

    if (stream == NULL) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_READ",
            "source file could not be opened",
            path,
            strerror(errno));

        return false;
    }

    if (fseek(
            stream,
            0L,
            SEEK_END) != 0) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_READ",
            "source file could not be inspected",
            path,
            strerror(errno));

        (void)fclose(stream);

        return false;
    }

    end = ftell(stream);

    if (end < 0L) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_READ",
            "source file size could not be determined",
            path,
            strerror(errno));

        (void)fclose(stream);

        return false;
    }

    /*
     * long can be wider or narrower than size_t depending on the target.
     */
    if ((uintmax_t)end >
        (uintmax_t)SIZE_MAX - UINTMAX_C(1)) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_LIMIT",
            "source file exceeds the supported size",
            path,
            NULL);

        (void)fclose(stream);

        return false;
    }

    length = (size_t)end;

    if (fseek(
            stream,
            0L,
            SEEK_SET) != 0) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_READ",
            "source file could not be rewound",
            path,
            strerror(errno));

        (void)fclose(stream);

        return false;
    }

    data =
        (unsigned char *)
        malloc(length + 1u);

    if (data == NULL) {
        vitte_main_error(
            "out of memory while loading source file");

        (void)fclose(stream);

        return false;
    }

    read_count = 0u;

    while (read_count < length) {
        const size_t count =
            fread(
                data + read_count,
                1u,
                length - read_count,
                stream);

        if (count == 0u) {
            if (ferror(stream) != 0) {
                vitte_main_report_path_error(
                    "VITTE_INFRA_E_SOURCE_READ",
                    "source file could not be read",
                    path,
                    strerror(errno));

                free(data);
                (void)fclose(stream);

                return false;
            }

            break;
        }

        read_count += count;
    }

    if (read_count != length) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_READ",
            "source file was not read completely",
            path,
            NULL);

        free(data);
        (void)fclose(stream);

        return false;
    }

    data[length] = 0u;

    if (fclose(stream) != 0) {
        char details[1024];
        int close_length;

        close_length = snprintf(
            details,
            sizeof(details),
            "path: '%s'; reason: %s",
            path,
            strerror(errno));
        if (close_length < 0 ||
            (size_t)close_length >= sizeof(details)) {
            vitte_main_report_process_warning(
                "source file could not be closed cleanly",
                NULL);
        } else {
            vitte_main_report_process_warning(
                "source file could not be closed cleanly",
                details);
        }
    }

    file->data = data;
    file->length = length;

    return true;
}

static void
vitte_main_file_destroy(
    vitte_main_file_t *file)
{
    if (file == NULL) {
        return;
    }

    free(file->data);

    file->data = NULL;
    file->length = 0u;
}

/* ========================================================================= */
/* Source name                                                               */
/* ========================================================================= */

static const char *
vitte_main_basename(
    const char *path)
{
    const char *result;
    const char *cursor;

    if (path == NULL) {
        return "";
    }

    result = path;

    for (cursor = path;
         *cursor != '\0';
         ++cursor) {
        if (*cursor == '/' ||
            *cursor == '\\') {
            result = cursor + 1;
        }
    }

    return result;
}

/* ========================================================================= */
/* UTF-8 validation                                                          */
/* ========================================================================= */

static bool
vitte_main_validate_utf8(
    const vitte_main_options_t *options,
    const vitte_main_file_t *file)
{
    size_t error_offset;

    if (options == NULL ||
        file == NULL) {
        return false;
    }

    if (!options->validate_utf8) {
        return true;
    }

    error_offset = 0u;

    if (!vitte_unicode_validate_utf8(
            (const char *)file->data,
            file->length,
            &error_offset)) {
        char details[256];
        int details_length;

        details_length = snprintf(
            details,
            sizeof(details),
            "invalid UTF-8 sequence at byte %zu",
            error_offset);
        if (details_length < 0 ||
            (size_t)details_length >= sizeof(details)) {
            vitte_main_report_process_error(
                "VITTE_INFRA_E_ENCODING",
                "source contains invalid UTF-8",
                NULL);
        } else {
            vitte_main_report_path_error(
                "VITTE_INFRA_E_ENCODING",
                "source contains invalid UTF-8",
                options->input_path,
                details);
        }

        return false;
    }

    return true;
}

/* ========================================================================= */
/* Token output                                                              */
/* ========================================================================= */

static void
vitte_main_print_tokens(
    const vitte_lexer_t *lexer)
{
    size_t index;

    if (lexer == NULL) {
        return;
    }

    for (index = 0u;
         index < lexer->token_count;
         ++index) {
        const vitte_token_t *token =
            &lexer->tokens[index];

        (void)printf(
            "%6zu  %-24s  [%zu, %zu)  %zu:%zu\n",
            index,
            vitte_token_kind_name(
                token->kind),
            token->span.begin,
            token->span.end,
            token->line,
            token->column);
    }
}

/* ========================================================================= */
/* Frontend                                                                  */
/* ========================================================================= */

static vitte_main_exit_code_t
vitte_main_frontend(
    const vitte_main_options_t *options)
{
    vitte_main_file_t file;

    vitte_source_context_t sources;
    vitte_source_id_t source_id;

    vitte_lexer_t lexer;
    vitte_parser_t parser;

    bool source_initialized;
    bool lexer_initialized;
    bool parser_initialized;

    vitte_main_exit_code_t exit_code;

    memset(
        &file,
        0,
        sizeof(file));

    memset(
        &sources,
        0,
        sizeof(sources));

    memset(
        &lexer,
        0,
        sizeof(lexer));

    memset(
        &parser,
        0,
        sizeof(parser));

    source_id =
        VITTE_SOURCE_INVALID_ID;

    source_initialized = false;
    lexer_initialized = false;
    parser_initialized = false;

    exit_code =
        VITTE_MAIN_EXIT_FAILURE;

    /* --------------------------------------------------------------------- */
    /* Load                                                                  */
    /* --------------------------------------------------------------------- */

    vitte_main_verbose(
        options,
        "loading source");

    if (!vitte_main_read_file(
            options->input_path,
            &file)) {
        goto cleanup;
    }

    /* --------------------------------------------------------------------- */
    /* Unicode                                                               */
    /* --------------------------------------------------------------------- */

    vitte_main_verbose(
        options,
        "validating UTF-8");

    if (!vitte_main_validate_utf8(
            options,
            &file)) {
        goto cleanup;
    }

    /* --------------------------------------------------------------------- */
    /* Source database                                                       */
    /* --------------------------------------------------------------------- */

    vitte_main_verbose(
        options,
        "registering source");

    if (!vitte_source_init(
            &sources)) {
        vitte_main_error(
            "failed to initialize source subsystem");

        exit_code =
            VITTE_MAIN_EXIT_INTERNAL;

        goto cleanup;
    }

    source_initialized = true;

    if (!vitte_source_add(
            &sources,
            vitte_main_basename(
                options->input_path),
            options->input_path,
            (const char *)file.data,
            file.length,
            &source_id)) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_MAP",
            "source file could not be registered",
            options->input_path,
            vitte_source_error_name(
                vitte_source_last_error(&sources)));

        goto cleanup;
    }

    /* --------------------------------------------------------------------- */
    /* Lexer                                                                 */
    /* --------------------------------------------------------------------- */

    vitte_main_verbose(
        options,
        "lexing");

    /*
     * The lexer contract generated for the compiler owns the exact source
     * binding API. Keep initialization explicit here.
     *
     * The frontend source bytes are stable for the complete lexer lifetime.
     */
    if (!vitte_lexer_init(
            &lexer,
            (const char *)file.data,
            file.length,
            (uint32_t)source_id)) {
        vitte_main_error(
            "failed to initialize lexer");

        exit_code =
            VITTE_MAIN_EXIT_INTERNAL;

        goto cleanup;
    }

    lexer_initialized = true;

    if (!vitte_lexer_run(
            &lexer)) {
        vitte_main_driver_context_t diagnostic_context;

        memset(
            &diagnostic_context,
            0,
            sizeof(diagnostic_context));
        diagnostic_context.options = options;
        diagnostic_context.sources = sources;
        diagnostic_context.source_id = source_id;
        diagnostic_context.lexer = lexer;
        diagnostic_context.lexer_initialized = true;

        if (!vitte_main_render_lexer_diagnostic(
                &diagnostic_context)) {
            vitte_main_error(
                "failed to render lexical diagnostic");
        }

        goto cleanup;
    }

    if (options->emit_tokens ||
        options->command ==
            VITTE_MAIN_COMMAND_LEX) {
        vitte_main_print_tokens(
            &lexer);
    }

    if (options->command ==
            VITTE_MAIN_COMMAND_LEX ||
        options->stop_after_lexer) {
        exit_code =
            VITTE_MAIN_EXIT_SUCCESS;

        goto cleanup;
    }

    /* --------------------------------------------------------------------- */
    /* Parser                                                                */
    /* --------------------------------------------------------------------- */

    vitte_main_verbose(
        options,
        "parsing");

    if (!vitte_parser_init(
            &parser,
            lexer.tokens,
            lexer.token_count)) {
        vitte_main_error(
            "failed to initialize parser");

        exit_code =
            VITTE_MAIN_EXIT_INTERNAL;

        goto cleanup;
    }

    parser_initialized = true;

    if (!vitte_parser_run(
            &parser)) {
        bool rendered;

        rendered = false;
        {
            vitte_main_driver_context_t diagnostic_context;

            memset(
                &diagnostic_context,
                0,
                sizeof(diagnostic_context));
            diagnostic_context.options = options;
            diagnostic_context.sources = sources;
            diagnostic_context.source_id = source_id;
            diagnostic_context.parser = parser;
            diagnostic_context.parser_initialized = true;

            if (!vitte_main_render_parser_diagnostics(
                    &diagnostic_context,
                    &rendered)) {
                vitte_main_error(
                    "failed to render parser diagnostics");
                goto cleanup;
            }
        }

        if (!rendered) {
            vitte_main_report_process_error(
                "VITTE_INFRA_E_PHASE",
                "parsing failed",
                vitte_parser_error_name(
                    vitte_parser_last_error(&parser)));
        }

        goto cleanup;
    }

    /*
     * AST printing is intentionally delegated to the printer subsystem.
     * Because printer API revisions have been evolving with the parser
     * contract, the main entry point does not duplicate AST traversal.
     */
    if (options->emit_ast ||
        options->command ==
            VITTE_MAIN_COMMAND_PARSE) {
        const vitte_ast_node_t *root = vitte_parser_get_node(
            &parser, vitte_parser_root(&parser));

        if (root == NULL) {
            vitte_main_error(
                "parser produced no translation unit");

            goto cleanup;
        }

        (void)printf(
            "AST root kind: %u\n"
            "nodes: %zu\n",
            (unsigned int)root->kind,
            parser.node_count);
    }

    if (options->command ==
            VITTE_MAIN_COMMAND_PARSE ||
        options->stop_after_parser) {
        exit_code =
            VITTE_MAIN_EXIT_SUCCESS;

        goto cleanup;
    }

    /*
     * Semantic analysis and the lower compiler pipeline are normally owned
     * by driver.c.
     *
     * Do not recreate scope/type/sema/HIR/IR/backend state here: doing so
     * would create two independent compiler orchestration implementations.
     *
     * The explicit lexer/parser path above exists for:
     *
     *   vitte lex
     *   vitte parse
     *   --emit-tokens
     *   --emit-ast
     *   --stop-after-lexer
     *   --stop-after-parser
     *
     * compile/check/run continue through the canonical driver below.
     */

    exit_code =
        VITTE_MAIN_EXIT_SUCCESS;

cleanup:

    if (parser_initialized) {
        vitte_parser_destroy(
            &parser);
    }

    if (lexer_initialized) {
        vitte_lexer_destroy(
            &lexer);
    }

    if (source_initialized) {
        vitte_source_destroy(
            &sources);
    }

    vitte_main_file_destroy(
        &file);

    return exit_code;
}

/* ========================================================================= */
/* Driver bridge                                                             */
/* ========================================================================= */

static bool
vitte_main_driver_load_source(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    const vitte_driver_input_t *input,
    void *user_data)
{
    vitte_main_driver_context_t *context;
    vitte_main_module_set_t modules;
    size_t expanded_length;
    unsigned char terminator;

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

    memset(&modules, 0, sizeof(modules));
    if (!vitte_main_expand_module(&modules, input->name, 0u, NULL) ||
        modules.source == NULL ||
        modules.source_length == SIZE_MAX) {
        vitte_main_module_set_destroy(&modules);
        free(modules.source);
        vitte_main_error_argument(
            "failed to assemble imported modules for",
            input->name);
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
    free(context->file.data);
    context->file.data = modules.source;
    context->file.length = expanded_length;
    modules.source = NULL;
    vitte_main_module_set_destroy(&modules);

    vitte_main_verbose(context->options, "registering source");

    if (!vitte_source_init(&context->sources)) {
        vitte_main_error("failed to initialize source subsystem");
        return false;
    }

    context->source_initialized = true;

    if (!vitte_source_add(
            &context->sources,
            vitte_main_basename(input->name),
            input->name,
            (const char *)context->file.data,
            context->file.length,
            &context->source_id)) {
        vitte_main_report_path_error(
            "VITTE_INFRA_E_SOURCE_MAP",
            "source file could not be registered",
            input->name,
            vitte_source_error_name(
                vitte_source_last_error(&context->sources)));
        return false;
    }

    if (context->source_id > (vitte_source_id_t)UINT32_MAX) {
        vitte_main_error("source identifier exceeds the lexer limit");
        return false;
    }

    return true;
}

static bool
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
        context->file.data == NULL ||
        context->source_id == VITTE_SOURCE_INVALID_ID) {
        return false;
    }

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
    vitte_source_location_t start;
    vitte_source_location_t end;
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

    source = vitte_source_get(
        &context->sources,
        context->source_id);
    source_name = source != NULL
        ? source->name
        : context->options->input_path;

    has_span = source_span != NULL &&
        source_span->valid &&
        source_span->begin <= source_span->end;
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
                context->source_id,
                source_span->begin,
                &start) &&
            start.valid) {
            start_line = start.line - 1u;
            start_column = start.column - 1u;
        }

        if (vitte_source_location_from_offset(
                &context->sources,
                context->source_id,
                source_span->end,
                &end) &&
            end.valid) {
            end_line = end.line - 1u;
            end_column = end.column - 1u;
        } else {
            end_column += source_span->end - source_span->begin;
        }
    }

    return vitte_diagnostic_render_cli(
        stderr,
        &context->sources,
        context->source_id,
        source_name,
        severity,
        code,
        message,
        details,
        has_span,
        source_span != NULL ? source_span->file_id : 0u,
        source_span != NULL ? source_span->begin : 0u,
        source_span != NULL ? source_span->end : 0u,
        start_line,
        start_column,
        end_line,
        end_column,
        procedure,
        diagnostic_context,
        context->options->color,
        context->options->diagnostic_format ==
                VITTE_MAIN_DIAGNOSTIC_JSON
            ? VITTE_CLI_DIAGNOSTIC_JSON
            : context->options->diagnostic_format ==
                    VITTE_MAIN_DIAGNOSTIC_SARIF
                ? VITTE_CLI_DIAGNOSTIC_SARIF
                : VITTE_CLI_DIAGNOSTIC_TERMINAL);
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
    vitte_source_location_t location;

    if (context == NULL ||
        context->options == NULL ||
        diagnostic == NULL ||
        code == NULL) {
        return false;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->severity = severity;
    diagnostic->code = code;
    diagnostic->message = message;
    diagnostic->details = details;
    diagnostic->procedure = procedure;
    diagnostic->context = diagnostic_context;
    diagnostic->end_offset = 0u;
    diagnostic->file_id = 0u;

    if (source_span == NULL ||
        !source_span->valid ||
        source_span->begin > source_span->end ||
        source_span->end > context->file.length) {
        return true;
    }

    diagnostic->has_span = true;
    diagnostic->file_id = source_span->file_id;
    diagnostic->start_offset = source_span->begin;
    diagnostic->end_offset = source_span->end;
    diagnostic->start_line = source_span->line;
    diagnostic->start_column = source_span->column;
    diagnostic->end_line = source_span->line;
    diagnostic->end_column = source_span->column;

    if (vitte_source_location_from_offset(
                &context->sources,
                context->source_id,
                source_span->begin,
                &location) &&
        location.valid) {
        diagnostic->start_line = location.line - 1u;
        diagnostic->start_column = location.column - 1u;
    }

    if (vitte_source_location_from_offset(
                &context->sources,
                context->source_id,
                source_span->end,
                &location) &&
        location.valid) {
        diagnostic->end_line = location.line - 1u;
        diagnostic->end_column = location.column - 1u;
    } else {
        diagnostic->end_column +=
                source_span->end - source_span->begin;
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

static bool
vitte_main_render_lexer_diagnostic(
    const vitte_main_driver_context_t *context)
{
    vitte_lexer_error_t error;
    vitte_lexer_span_t lexer_span;
    vitte_parser_span_t source_span;
    const vitte_parser_span_t *source_span_pointer;
    const char *code;

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

        case VITTE_LEXER_ERROR_COMMENT_DEPTH:
        case VITTE_LEXER_ERROR_TOKEN_LIMIT:
        case VITTE_LEXER_ERROR_SOURCE_TOO_LARGE:
        case VITTE_LEXER_ERROR_OVERFLOW:
            code = "VITTE_INFRA_E_LIMIT";
            break;

        case VITTE_LEXER_ERROR_OUT_OF_MEMORY:
            code = "VITTE_INFRA_E_OUT_OF_MEMORY";
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

    memset(&source_span, 0, sizeof(source_span));
    lexer_span = vitte_lexer_error_span(&context->lexer);
    source_span_pointer = NULL;
    if (lexer_span.valid) {
        source_span.file_id = lexer_span.file_id;
        source_span.begin = lexer_span.begin;
        source_span.end = lexer_span.end;
        source_span.valid = true;
        source_span_pointer = &source_span;
    }

    return vitte_main_render_diagnostic(
        context,
        VITTE_CLI_DIAGNOSTIC_ERROR,
        code,
        NULL,
        NULL,
        source_span_pointer,
        NULL,
        "lexing");
}

static bool
vitte_main_render_parser_diagnostics(
    const vitte_main_driver_context_t *context,
    bool *rendered)
{
    vitte_cli_diagnostic_t *diagnostics;
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
    if (diagnostics == NULL) {
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
        int details_length;

        parser_diagnostic = vitte_parser_diagnostic_at(
            &context->parser,
            index);
        if (parser_diagnostic == NULL) {
            free(diagnostics);
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
            return false;
        }

        details = diagnostic->details_storage[0] != '\0'
            ? diagnostic->details_storage
            : NULL;
        diagnostic->details = details;
    }

    *rendered = vitte_diagnostic_render_cli_batch(
        stderr,
        &context->sources,
        context->source_id,
        source_name,
        diagnostics,
        diagnostic_count,
        context->options->color,
        context->options->diagnostic_format ==
                VITTE_MAIN_DIAGNOSTIC_JSON
            ? VITTE_CLI_DIAGNOSTIC_JSON
            : context->options->diagnostic_format ==
                    VITTE_MAIN_DIAGNOSTIC_SARIF
                ? VITTE_CLI_DIAGNOSTIC_SARIF
                : VITTE_CLI_DIAGNOSTIC_TERMINAL);
    free(diagnostics);
    return *rendered;
}

static bool
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

    if (context == NULL ||
        !context->lexer_initialized) {
        return false;
    }

    vitte_main_verbose(context->options, "parsing");

    if (!vitte_parser_init(
            &context->parser,
            context->lexer.tokens,
            context->lexer.token_count)) {
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

static const char *
vitte_main_sema_type_name(
    const vitte_sema_t *sema,
    vitte_sema_type_id_t type_id)
{
    const vitte_sema_type_t *type;

    if (sema == NULL ||
        type_id == VITTE_SEMA_INVALID_TYPE_ID) {
        return NULL;
    }

    type = vitte_sema_get_type(sema, type_id);
    if (type == NULL) {
        return NULL;
    }

    return vitte_sema_type_kind_name(type->kind);
}

static bool
vitte_main_render_sema_diagnostics(
    const vitte_main_driver_context_t *context,
    bool *rendered)
{
    vitte_cli_diagnostic_t *diagnostics;
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
    if (diagnostics == NULL) {
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
            return false;
        }

        expected_type = vitte_main_sema_type_name(
            &context->sema,
            sema_diagnostic->expected_type);
        found_type = vitte_main_sema_type_name(
            &context->sema,
            sema_diagnostic->found_type);
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
            return false;
        }

        diagnostic->details =
            diagnostic->details_storage[0] != '\0'
                ? diagnostic->details_storage
                : NULL;
    }

    *rendered = vitte_diagnostic_render_cli_batch(
        stderr,
        &context->sources,
        context->source_id,
        source_name,
        diagnostics,
        diagnostic_count,
        context->options->color,
        context->options->diagnostic_format ==
                VITTE_MAIN_DIAGNOSTIC_JSON
            ? VITTE_CLI_DIAGNOSTIC_JSON
            : context->options->diagnostic_format ==
                    VITTE_MAIN_DIAGNOSTIC_SARIF
                ? VITTE_CLI_DIAGNOSTIC_SARIF
                : VITTE_CLI_DIAGNOSTIC_TERMINAL);
    free(diagnostics);
    return *rendered;
}

static bool
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

    if (context == NULL ||
        !context->parser_initialized) {
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

static bool
vitte_main_driver_compile_ast(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    void *user_data)
{
    vitte_main_driver_context_t *context;

    (void)driver;
    (void)phase;

    context = (vitte_main_driver_context_t *)user_data;
    if (context == NULL ||
        context->options == NULL ||
        !context->parser_initialized) {
        vitte_main_error(
            "C17 generation requires a successfully parsed input");
        return false;
    }

    {
        vitte_c17_compile_options_t compile_options;

        memset(&compile_options, 0, sizeof(compile_options));
        compile_options.debug_info = context->options->debug_info;
        compile_options.debug_runtime = context->options->debug_probes;
        compile_options.emit_line_directives =
            context->options->emit_line_directives ||
            context->options->debug_info;
        compile_options.source_path = context->options->input_path;

        if (!vitte_c17_compile_ast_with_options(
            &context->parser,
            context->options->output_path,
            context->options->emit_c,
            context->options->command == VITTE_MAIN_COMMAND_RUN,
            context->options->run_argc,
            context->options->run_argv,
            &compile_options)) {
            vitte_main_error(
                "C17 AST generation or native compilation failed");
            return false;
        }
    }

    return true;
}

static void
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

    if (context->sema_initialized) {
        vitte_sema_destroy(&context->sema);
        context->sema_initialized = false;
    }

    if (context->parser_initialized) {
        vitte_parser_destroy(&context->parser);
        context->parser_initialized = false;
    }

    if (context->lexer_initialized) {
        vitte_lexer_destroy(&context->lexer);
        context->lexer_initialized = false;
    }

    if (context->source_initialized) {
        vitte_source_destroy(&context->sources);
        context->source_initialized = false;
    }

    vitte_main_file_destroy(&context->file);
}

/*
 * The driver subsystem is the canonical owner of the complete compilation
 * pipeline.
 *
 * Keeping this bridge isolated means changes to driver.h affect one small
 * section of main.c rather than CLI parsing and process management.
 */
static vitte_main_exit_code_t
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

    config = vitte_driver_config_default();

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
        case VITTE_MAIN_COMMAND_HELP:
        case VITTE_MAIN_COMMAND_VERSION:
            return VITTE_MAIN_EXIT_INTERNAL;
    }

    config.command = command;
    config.stop_after_parse =
        options->stop_after_parser ||
        options->stop_after_lexer ||
        options->command == VITTE_MAIN_COMMAND_CHECK;
    config.stop_after_semantic =
        options->stop_after_sema;

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
        : vitte_main_driver_compile_ast;
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
/* Dispatch                                                                  */
/* ========================================================================= */

static vitte_main_exit_code_t
vitte_main_dispatch(
    const vitte_main_options_t *options)
{
    if (options == NULL) {
        return VITTE_MAIN_EXIT_INTERNAL;
    }

    switch (options->command) {
        case VITTE_MAIN_COMMAND_HELP:
            vitte_main_print_usage(
                stdout);

            return VITTE_MAIN_EXIT_SUCCESS;

        case VITTE_MAIN_COMMAND_VERSION:
            vitte_main_print_version();

            return VITTE_MAIN_EXIT_SUCCESS;

        case VITTE_MAIN_COMMAND_LEX:
        case VITTE_MAIN_COMMAND_PARSE:
            return vitte_main_frontend(
                options);

        case VITTE_MAIN_COMMAND_COMPILE:
        case VITTE_MAIN_COMMAND_CHECK:
        case VITTE_MAIN_COMMAND_RUN:
            break;
    }

    return vitte_main_driver_compile(
        options);
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

int
main(
    int argc,
    char **argv)
{
    vitte_main_options_t options;
    vitte_main_exit_code_t result;

    vitte_main_set_executable_path(argc > 0 ? argv[0] : NULL);

    if (!vitte_main_parse_options(
            argc,
            argv,
            &options)) {
        if (vitte_main_active_diagnostic_format ==
            VITTE_MAIN_DIAGNOSTIC_TERMINAL) {
            (void)fprintf(stderr, "\n");
            vitte_main_print_usage(stderr);
        }

        return (int)
            VITTE_MAIN_EXIT_USAGE;
    }

    result =
        vitte_main_dispatch(
            &options);

    if (result ==
            VITTE_MAIN_EXIT_INTERNAL &&
        !options.quiet) {
        vitte_main_warning(
            &options,
            "compiler terminated because of an internal or infrastructure failure");
    }

    return (int)result;
}
