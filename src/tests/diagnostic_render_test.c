#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api/api.h"
#include "diagnostic/diagnostic.h"
#include "diagnostic/cli_bridge.h"
#include "diagnostic/help.h"
#include "diagnostic/explain.h"
#include "diagnostic/render_json.h"
#include "diagnostic/render_lsp.h"
#include "diagnostic/registry.h"
#include "diagnostic/render_terminal.h"

static void
test_api_invalid_utf8_source_diagnostic(void)
{
    static const char prefix[] =
        "proc main() -> i64 { return 0; }\n";
    char source[sizeof(prefix)];
    vitte_api_t *api = NULL;
    vitte_api_result_t *result = NULL;
    vitte_status_t status;
    const char *diagnostics;

    memcpy(source, prefix, sizeof(prefix) - 1u);
    source[sizeof(prefix) - 1u] = (char)0xff;
    status = vitte_api_create(&api);
    assert(status == VITTE_STATUS_OK);
    assert(api != NULL);

    status = vitte_api_compile_source(
        api,
        "invalid-utf8.vit",
        source,
        sizeof(source),
        &result);
    assert(status != VITTE_STATUS_OK);
    assert(result != NULL);
    assert(vitte_api_result_error_count(result) == 1u);
    diagnostics = vitte_api_result_diagnostics(result, NULL);
    assert(diagnostics != NULL);
    assert(strstr(diagnostics, "E0108") != NULL);
    assert(strstr(diagnostics, "phase: lexer") != NULL);
    assert(strstr(diagnostics, "invalid-utf8.vit:2:1") != NULL);

    vitte_api_result_destroy(result);
    vitte_api_destroy(api);
}

static void
test_api_contract_diagnostics(void)
{
    static const char *const sources[] = {
        "proc f(x: int) -> int requires x + 1; { return x; }",
        "proc f() -> int ensures false; { return 0; }",
        "proc f() -> int { }",
        "proc f(x: int) -> int requires x > 0; { return x; } proc main() -> int { return f(0); }",
        "proc f() -> int { return 1 / 0; }",
        "proc f() -> int { let a = [1]; return a[1]; }",
        "proc f(x: bool) -> int { match x { true => 1, } }"
    };
    static const char *const codes[] = {"E0704", "E0702", "E1101", "E0701", "E1503", "E1201", "E1601"};
    size_t index;
    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        vitte_api_t *api = NULL;
        vitte_api_result_t *result = NULL;
        const char *diagnostics;
        assert(vitte_api_create(&api) == VITTE_STATUS_OK);
        assert(vitte_api_compile_source(api, "contract.vit", sources[index],
            strlen(sources[index]), &result) != VITTE_STATUS_OK);
        assert(result != NULL);
        assert(vitte_api_result_error_count(result) == 1u);
        diagnostics = vitte_api_result_diagnostics(result, NULL);
        assert(diagnostics != NULL && strstr(diagnostics, codes[index]) != NULL);
        assert(strstr(diagnostics, index < 2u || index == 3u ? "phase: contract" :
            index == 4u ? "phase: constant-eval" : "phase: type-check") != NULL);
        if (index < 2u || index == 3u) {
            assert(strstr(diagnostics, "contract:") != NULL);
            assert(strstr(diagnostics, "procedure: f") != NULL);
        }
        assert(strstr(diagnostics, "help:") != NULL);
        vitte_api_result_destroy(result);
        vitte_api_destroy(api);
    }
}

/* The fixture is an ASCII line: UTF-8 and UTF-16 offsets coincide. */
static bool
ascii_position(vitte_source_id_t id, const char *name, size_t offset,
               vitte_diagnostic_lsp_position_encoding_t encoding,
               vitte_diagnostic_lsp_position_t *position, void *user_data)
{
    (void)id; (void)name; (void)encoding; (void)user_data;
    if (offset > 4u) return false;
    position->line = 0u;
    position->character = offset;
    position->valid = true;
    return true;
}

static void
test_cli_bridge_consistency(void)
{
    static const vitte_cli_diagnostic_format_t formats[] = {
        VITTE_CLI_DIAGNOSTIC_TERMINAL,
        VITTE_CLI_DIAGNOSTIC_JSON,
        VITTE_CLI_DIAGNOSTIC_SARIF,
        VITTE_CLI_DIAGNOSTIC_LSP
    };
    vitte_cli_diagnostic_t input;
    vitte_cli_diagnostic_suggestion_t suggestion;
    char output[524288];
    size_t index;

    vitte_cli_diagnostic_init(&input);
    memset(&suggestion, 0, sizeof(suggestion));
    input.code = "VITTE_PARSER_E_EXPECTED_TOKEN";
    input.message = "unexpected token";
    input.details = NULL;
    input.origin = VITTE_CLI_DIAGNOSTIC_ORIGIN_PARSER;
    input.has_span = true;
    input.source_id = VITTE_SOURCE_INVALID_ID;
    input.source_name = "bridge.vit";
    input.start_offset = 0u;
    input.end_offset = 4u;
    input.start_line = 0u;
    input.start_column = 0u;
    input.end_line = 0u;
    input.end_column = 4u;
    input.expected = "semicolon";
    input.found = "right brace";
    input.probable_cause = "the declaration is incomplete";
    input.help = "insert the missing semicolon";
    input.module = "parser::demo";
    input.symbol = "statement";
    input.context = "parser recovery";
    suggestion.message = "Insert `;`";
    suggestion.applicability = VITTE_CLI_DIAGNOSTIC_APPLICABILITY_MACHINE;
    suggestion.confidence = VITTE_CLI_DIAGNOSTIC_CONFIDENCE_CERTAIN;
    suggestion.preferred = true;
    suggestion.has_span = true;
    suggestion.span.valid = true;
    suggestion.span.source_id = VITTE_SOURCE_INVALID_ID;
    suggestion.span.source_name = "bridge.vit";
    suggestion.span.start_offset = 0u;
    suggestion.span.end_offset = 4u;
    suggestion.span.start_line = 0u;
    suggestion.span.start_column = 0u;
    suggestion.span.end_line = 0u;
    suggestion.span.end_column = 4u;
    suggestion.has_replacement = true;
    suggestion.replacement = ";";
    input.suggestions = &suggestion;
    input.suggestion_count = 1u;
    input.has_machine_fix = true;
    input.has_contract = true;
    input.contract.kind = VITTE_CLI_DIAGNOSTIC_CONTRACT_REQUIRES;
    input.contract.expression = "bridge_condition";
    input.contract.procedure = "bridge_procedure";
    input.contract.reason = "bridge_contract_reason";
    input.contract.has_contract_span = true;
    input.contract.contract_span = suggestion.span;

    for (index = 0u; index < sizeof(formats) / sizeof(formats[0]); ++index) {
        vitte_cli_diagnostic_render_options_t options;
        FILE *stream = tmpfile();
        size_t bytes;

        assert(stream != NULL);
        vitte_cli_diagnostic_render_options_init(&options);
        options.format = formats[index];
        options.position_converter =
            (vitte_cli_lsp_position_converter_t)ascii_position;
        assert(vitte_diagnostic_render_cli_batch_ex(
            stream, NULL, VITTE_SOURCE_INVALID_ID, "bridge.vit",
            &input, 1u, &options));
        rewind(stream);
        bytes = fread(output, 1u, sizeof(output) - 1u, stream);
        output[bytes] = '\0';
        assert(strstr(output, "semicolon") != NULL);
        assert(strstr(output, "right brace") != NULL);
        assert(strstr(output, "declaration is incomplete") != NULL);
        assert(strstr(output, "Insert `;") != NULL);
        assert(strstr(output, "parser::demo") != NULL);
        assert(strstr(output, "statement") != NULL);
        assert(strstr(output, "bridge_condition") != NULL);
        assert(strstr(output, "bridge_procedure") != NULL);
        assert(strstr(output, "bridge_contract_reason") != NULL);
        if (formats[index] == VITTE_CLI_DIAGNOSTIC_TERMINAL) {
            assert(strstr(output, "bridge.vit:1:1") != NULL);
            assert(strstr(output, "expected `semicolon`") != NULL);
        } else if (formats[index] == VITTE_CLI_DIAGNOSTIC_JSON) {
            assert(strstr(output, "\"source\": \"bridge.vit\"") != NULL);
            assert(strstr(output, "\"start_line\": 0") != NULL);
            assert(strstr(output, "\"start_column\": 0") != NULL);
            assert(strstr(output, "\"expected\": \"semicolon\"") != NULL);
        } else if (formats[index] == VITTE_CLI_DIAGNOSTIC_SARIF) {
            assert(strstr(output, "\"uri\": \"bridge.vit\"") != NULL);
            assert(strstr(output, "\"startLine\": 1") != NULL);
            assert(strstr(output, "\"startColumn\": 1") != NULL);
        } else {
            assert(strstr(output, "\"line\": 0") != NULL);
            assert(strstr(output, "\"character\": 0") != NULL);
            assert(strstr(output, "\"expected\": \"semicolon\"") != NULL);
        }
        assert(fclose(stream) == 0);
    }

    /* Stored bridge coordinates are enough for process/import diagnostics
     * when no source manager is available to perform byte conversion. */
    {
        vitte_cli_diagnostic_render_options_t options;
        FILE *stream = tmpfile();
        size_t bytes;

        assert(stream != NULL);
        vitte_cli_diagnostic_render_options_init(&options);
        options.format = VITTE_CLI_DIAGNOSTIC_LSP;
        assert(vitte_diagnostic_render_cli_batch_ex(
            stream, NULL, VITTE_SOURCE_INVALID_ID, "bridge.vit",
            &input, 1u, &options));
        rewind(stream);
        bytes = fread(output, 1u, sizeof(output) - 1u, stream);
        output[bytes] = '\0';
        assert(strstr(output, "\"line\": 0") != NULL);
        assert(strstr(output, "\"character\": 0") != NULL);
        fclose(stream);
    }
}

static void
test_classification_metadata(void)
{
    vitte_diagnostic_t storage[2];
    vitte_diagnostic_t invalid;
    vitte_diagnostic_bag_t bag;
    size_t i;
    size_t form;

    assert(vitte_diagnostic_bag_init(&bag, storage, 2u) == VITTE_STATUS_OK);
    for (i = 0u; i < vitte_diagnostic_registry_count(); ++i) {
        const vitte_diagnostic_registry_entry_t *entry = vitte_diagnostic_registry_at(i);
        for (form = 0u; form < 2u; ++form) {
            const vitte_diagnostic_t *d;
            assert(vitte_diagnostic_emit(&bag, entry->default_severity,
                VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN,
                form == 0u ? entry->internal_code : entry->public_code,
                entry->title, NULL) == VITTE_STATUS_OK);
            d = vitte_diagnostic_at(&bag, 0u);
            assert(strcmp(d->code, entry->internal_code) == 0);
            assert(strcmp(d->short_code, entry->public_code) == 0);
            assert(strcmp(d->category, entry->category) == 0);
            assert(d->severity == entry->default_severity);
            assert(d->origin == entry->origin);
            vitte_diagnostic_bag_reset(&bag);
        }
    }

    /* A generic failure keeps the phase that actually encountered it. */
    assert(vitte_diagnostic_emit(&bag, VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_LINKER, "E0012", "linking failed",
        NULL) == VITTE_STATUS_OK);
    assert(storage[0].origin == VITTE_DIAGNOSTIC_ORIGIN_LINKER);
    assert(storage[0].severity == VITTE_DIAGNOSTIC_FATAL);
    assert(bag.counts.fatal_count == 1u);
    assert(vitte_diagnostic_has_errors(&bag));
    vitte_diagnostic_bag_reset(&bag);

    /* Promoting a warning changes severity and counts, never its identity. */
    bag.options.warnings_as_errors = true;
    assert(vitte_diagnostic_emit(&bag, VITTE_DIAGNOSTIC_WARNING,
        VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN, "W0003", "unused import",
        NULL) == VITTE_STATUS_OK);
    assert(strcmp(storage[0].short_code, "W0003") == 0);
    assert(storage[0].origin == VITTE_DIAGNOSTIC_ORIGIN_IMPORT);
    assert(storage[0].severity == VITTE_DIAGNOSTIC_ERROR);
    assert(bag.counts.warning_count == 0u && bag.counts.error_count == 1u);
    assert(vitte_diagnostic_has_errors(&bag));

    /* Both entry points reject invalid enums without changing stored data. */
    invalid = storage[0];
    for (i = 0u; i < 2u; ++i) {
        invalid.severity = i == 0u ? (vitte_diagnostic_severity_t)-1
            : (vitte_diagnostic_severity_t)(VITTE_DIAGNOSTIC_FATAL + 1);
        assert(vitte_diagnostic_add(&bag, &invalid) == VITTE_STATUS_ERROR_INVALID_ARGUMENT);
        assert(vitte_diagnostic_emit(&bag, invalid.severity,
            VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN, "E0302", "invalid severity",
            NULL) == VITTE_STATUS_ERROR_INVALID_ARGUMENT);
    }
    invalid.severity = VITTE_DIAGNOSTIC_ERROR;
    for (i = 0u; i < 2u; ++i) {
        invalid.origin = i == 0u ? (vitte_diagnostic_origin_t)-1
            : (vitte_diagnostic_origin_t)(VITTE_DIAGNOSTIC_ORIGIN_INTERNAL + 1);
        assert(vitte_diagnostic_add(&bag, &invalid) == VITTE_STATUS_ERROR_INVALID_ARGUMENT);
        assert(vitte_diagnostic_emit(&bag, VITTE_DIAGNOSTIC_ERROR,
            invalid.origin, "E0302", "invalid origin",
            NULL) == VITTE_STATUS_ERROR_INVALID_ARGUMENT);
    }
    assert(bag.count == 1u && bag.counts.error_count == 1u);
    assert(strcmp(storage[0].short_code, "W0003") == 0);
    vitte_diagnostic_bag_reset(&bag);
}

static void
test_classification_rendering(void)
{
    static const char *const severity_names[] = {
        "note", "help", "warning", "error", "fatal"
    };
    static const char *const sarif_levels[] = {
        "note", "note", "warning", "error", "error"
    };
    static const int lsp_severities[] = {3, 4, 2, 1, 1};
    static const vitte_diagnostic_format_t formats[] = {
        VITTE_DIAGNOSTIC_FORMAT_TERMINAL, VITTE_DIAGNOSTIC_FORMAT_JSON,
        VITTE_DIAGNOSTIC_FORMAT_SARIF, VITTE_DIAGNOSTIC_FORMAT_LSP
    };
    static char output[262144];
    vitte_diagnostic_t storage[1];
    vitte_diagnostic_bag_t bag;
    vitte_ast_span_t span;
    char expected[128];
    size_t i;
    size_t format;

    memset(&span, 0, sizeof(span));
    span.source_name = "classification.vit";
    span.end_offset = 4u;
    span.end_column = 4u;
    span.valid = true;
    assert(vitte_diagnostic_bag_init(&bag, storage, 1u) == VITTE_STATUS_OK);
    bag.options.color_enabled = false;
    for (i = 0u; i < sizeof(severity_names) / sizeof(severity_names[0]); ++i) {
        /* A caller's severity policy must not reassign the public code. */
        assert(vitte_diagnostic_emit(&bag, (vitte_diagnostic_severity_t)i,
            VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN, "E0302", "private symbol imported",
            &span) == VITTE_STATUS_OK);
        for (format = 0u; format < sizeof(formats) / sizeof(formats[0]); ++format) {
            FILE *stream = tmpfile();
            size_t bytes;
            assert(stream != NULL);
            if (formats[format] == VITTE_DIAGNOSTIC_FORMAT_LSP) {
                vitte_diagnostic_lsp_options_t options;
                vitte_diagnostic_lsp_options_init(&options);
                options.pretty = true;
                options.position_converter = ascii_position;
                assert(vitte_diagnostic_render_lsp_array(stream, &bag, NULL,
                    &options) == VITTE_STATUS_OK);
            } else if (formats[format] == VITTE_DIAGNOSTIC_FORMAT_JSON) {
                vitte_diagnostic_json_options_t options;
                vitte_diagnostic_json_options_init(&options);
                options.pretty = true;
                assert(vitte_diagnostic_render_json_bag(stream, &bag,
                    &options) == VITTE_STATUS_OK);
            } else {
                assert(vitte_diagnostic_write_all_format(stream, &bag,
                    formats[format]) == VITTE_STATUS_OK);
            }
            rewind(stream);
            bytes = fread(output, 1u, sizeof(output) - 1u, stream);
            assert(feof(stream));
            output[bytes] = '\0';
            if (formats[format] == VITTE_DIAGNOSTIC_FORMAT_TERMINAL) {
                (void)snprintf(expected, sizeof(expected), "%s[E0302]:", severity_names[i]);
                assert(strstr(output, expected) != NULL);
                assert(strstr(output, "phase: import") != NULL);
            } else {
                (void)snprintf(expected, sizeof(expected), "\"severity\": \"%s\"", severity_names[i]);
                assert(strstr(output, expected) != NULL);
                assert(strstr(output, "\"phase\": \"import\"") != NULL);
                assert(strstr(output, formats[format] == VITTE_DIAGNOSTIC_FORMAT_SARIF
                    ? "\"ruleId\": \"E0302\"" : "\"code\": \"E0302\"") != NULL);
                if (formats[format] == VITTE_DIAGNOSTIC_FORMAT_SARIF) {
                    (void)snprintf(expected, sizeof(expected), "\"level\": \"%s\"", sarif_levels[i]);
                    assert(strstr(output, expected) != NULL);
                } else if (formats[format] == VITTE_DIAGNOSTIC_FORMAT_LSP) {
                    (void)snprintf(expected, sizeof(expected), "\"severity\": %d", lsp_severities[i]);
                    assert(strstr(output, expected) != NULL);
                }
            }
            assert(fclose(stream) == 0);
        }
        vitte_diagnostic_bag_reset(&bag);
    }
}

static void
test_contextual_help(void)
{
    vitte_diagnostic_t storage[1];
    vitte_diagnostic_bag_t bag;
    vitte_diagnostic_t *d;
    char help[512];
    static char output[262144];
    size_t i;
    vitte_ast_span_t span;
    assert(vitte_diagnostic_bag_init(&bag, storage, 1u) == VITTE_STATUS_OK);
    for (i = 0u; i < vitte_diagnostic_registry_count(); ++i) {
        const vitte_diagnostic_registry_entry_t *entry = vitte_diagnostic_registry_at(i);
        if (entry->default_severity < VITTE_DIAGNOSTIC_WARNING) continue;
        /* A new registered diagnostic must ship an actionable help policy. */
        assert(vitte_diagnostic_help_for_code(entry->public_code) != NULL);
        assert(vitte_diagnostic_emit(&bag, entry->default_severity, entry->origin,
            entry->internal_code, entry->title, NULL) == VITTE_STATUS_OK);
        d = vitte_diagnostic_at_mut(&bag, 0u);
        assert(vitte_diagnostic_format_help(d, NULL, help, sizeof(help)));
        assert(strstr(help, "No verified correction") == NULL);
        assert(strstr(help, entry->public_code) != NULL);
        vitte_diagnostic_bag_reset(&bag);
    }
    memset(&span, 0, sizeof(span));
    span.source_name = "context.vit";
    span.start_offset = 0u;
    span.end_offset = 4u;
    span.end_column = 4u;
    span.valid = true;
    assert(vitte_diagnostic_emit(&bag, VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK, "VITTE_CALL_E_ARGUMENT_TYPE",
        "argument type mismatch", &span) == VITTE_STATUS_OK);
    d = vitte_diagnostic_at_mut(&bag, 0u);
    vitte_diagnostic_set_subject(d, "count", "i64", "string");
    assert(vitte_diagnostic_format_help(d, NULL, help, sizeof(help)));
    assert(strstr(help, "`i64` instead of `string`") != NULL);
    assert(strstr(help, "parse it as a numeric value and handle invalid input") != NULL);
    /* All public rendering paths carry help even without the CLI bridge. */
    for (i = 0u; i < 4u; ++i) {
        static const vitte_diagnostic_format_t formats[] = {
            VITTE_DIAGNOSTIC_FORMAT_TERMINAL, VITTE_DIAGNOSTIC_FORMAT_JSON,
            VITTE_DIAGNOSTIC_FORMAT_SARIF, VITTE_DIAGNOSTIC_FORMAT_LSP
        };
        FILE *stream = tmpfile();
        size_t bytes;
        assert(stream != NULL);
        if (formats[i] == VITTE_DIAGNOSTIC_FORMAT_LSP) {
            vitte_diagnostic_lsp_options_t options;
            vitte_diagnostic_lsp_options_init(&options);
            options.position_converter = ascii_position;
            assert(vitte_diagnostic_render_lsp_array(stream, &bag, NULL, &options) == VITTE_STATUS_OK);
        } else {
            assert(vitte_diagnostic_write_all_format(stream, &bag, formats[i]) == VITTE_STATUS_OK);
        }
        rewind(stream);
        bytes = fread(output, 1u, sizeof(output) - 1u, stream);
        output[bytes] = '\0';
        assert(strstr(output, help) != NULL);
        assert(fclose(stream) == 0);
    }
    vitte_diagnostic_set_subject(d, NULL, "image", "string");
    assert(vitte_diagnostic_format_help(d, NULL, help, sizeof(help)));
    assert(strstr(help, "numeric") == NULL);
    assert(vitte_diagnostic_add_help(d, "Use the selected overload instead.", NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_format_help(d, NULL, help, sizeof(help)));
    assert(strcmp(help, "Use the selected overload instead.") == 0);
    {
        char guard[4] = {'a', 'b', 'c', 'd'};
        assert(!vitte_diagnostic_format_help(d, NULL, guard + 1, 1u));
        assert(guard[0] == 'a' && guard[1] == '\0' && guard[2] == 'c');
        assert(!vitte_diagnostic_format_help(d, NULL, guard, 0u));
        assert(!vitte_diagnostic_format_help(d, NULL, NULL, 8u));
    }
    vitte_diagnostic_bag_reset(&bag);
    assert(vitte_diagnostic_emit(&bag, VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IR, "VITTE_NEW_E_FAILURE", "new lowering failed",
        NULL) == VITTE_STATUS_OK);
    d = vitte_diagnostic_at_mut(&bag, 0u);
    assert(vitte_diagnostic_add_cause(d, "operand list missing", NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_format_help(d, NULL, help, sizeof(help)));
    assert(strstr(help, "No verified correction") != NULL);
    assert(strstr(help, "operand list missing") != NULL);
    assert(strstr(help, "vitte explain E????") != NULL);
    d->severity = VITTE_DIAGNOSTIC_NOTE;
    assert(!vitte_diagnostic_format_help(d, NULL, help, sizeof(help)));
    assert(help[0] == '\0');
    vitte_diagnostic_bag_reset(&bag);
}

static void
test_structured_fields_all_formats(void)
{
    static const vitte_diagnostic_format_t formats[] = {
        VITTE_DIAGNOSTIC_FORMAT_TERMINAL,
        VITTE_DIAGNOSTIC_FORMAT_JSON,
        VITTE_DIAGNOSTIC_FORMAT_SARIF,
        VITTE_DIAGNOSTIC_FORMAT_LSP
    };
    vitte_diagnostic_t storage[1];
    vitte_diagnostic_bag_t bag;
    vitte_ast_span_t span;
    char output[524288];
    size_t index;

    memset(&span, 0, sizeof(span));
    span.source_name = "structured.vit";
    span.start_offset = 0u;
    span.end_offset = 4u;
    span.start_line = 0u;
    span.start_column = 0u;
    span.end_line = 0u;
    span.end_column = 4u;
    span.valid = true;
    assert(vitte_diagnostic_bag_init(&bag, storage, 1u) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_emit(
        &bag,
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "VITTE_PARSER_E_EXPECTED_TOKEN",
        "unexpected token",
        &span) == VITTE_STATUS_OK);
    vitte_diagnostic_set_expected_found(
        vitte_diagnostic_at_mut(&bag, 0u), "`;`", "`}`");
    vitte_diagnostic_set_probable_cause(
        vitte_diagnostic_at_mut(&bag, 0u), "the previous declaration is incomplete");

    for (index = 0u; index < sizeof(formats) / sizeof(formats[0]); ++index) {
        FILE *stream = tmpfile();
        size_t bytes;

        assert(stream != NULL);
        if (formats[index] == VITTE_DIAGNOSTIC_FORMAT_LSP) {
            vitte_diagnostic_lsp_options_t options;
            vitte_diagnostic_lsp_options_init(&options);
            options.pretty = true;
            options.position_converter = ascii_position;
            assert(vitte_diagnostic_render_lsp_array(
                stream, &bag, NULL, &options) == VITTE_STATUS_OK);
        } else {
            assert(vitte_diagnostic_write_all_format(
                stream, &bag, formats[index]) == VITTE_STATUS_OK);
        }
        rewind(stream);
        bytes = fread(output, 1u, sizeof(output) - 1u, stream);
        output[bytes] = '\0';
        assert(strstr(output, "`;`") != NULL);
        assert(strstr(output, "`}`") != NULL);
        assert(strstr(output, "previous declaration is incomplete") != NULL);
        assert(fclose(stream) == 0);
    }
    vitte_diagnostic_bag_reset(&bag);
}

static void
test_lsp_source_positions_and_explanations(void)
{
    static const char source_text[] = "a😀b\nfin";
    vitte_source_context_t sources;
    vitte_source_id_t source_id;
    vitte_diagnostic_lsp_position_t position;
    size_t index;

    assert(vitte_source_init(&sources));
    assert(vitte_source_add_cstr(&sources, "unicode.vit", "unicode.vit",
        source_text, &source_id));
    assert(vitte_diagnostic_lsp_source_position_converter(source_id,
        "unicode.vit", 5u, VITTE_DIAGNOSTIC_LSP_POSITION_UTF8,
        &position, &sources));
    assert(position.valid && position.line == 0u && position.character == 5u);
    assert(vitte_diagnostic_lsp_source_position_converter(source_id,
        "unicode.vit", 5u, VITTE_DIAGNOSTIC_LSP_POSITION_UTF16,
        &position, &sources));
    assert(position.valid && position.line == 0u && position.character == 3u);
    assert(vitte_diagnostic_lsp_source_position_converter(source_id,
        "unicode.vit", 5u, VITTE_DIAGNOSTIC_LSP_POSITION_UTF32,
        &position, &sources));
    assert(position.valid && position.line == 0u && position.character == 2u);
    /* Newline bytes map to the end of the preceding LSP line. */
    assert(vitte_diagnostic_lsp_source_position_converter(source_id,
        "unicode.vit", 6u, VITTE_DIAGNOSTIC_LSP_POSITION_UTF16,
        &position, &sources));
    assert(position.valid && position.line == 0u && position.character == 4u);
    assert(vitte_diagnostic_lsp_source_position_converter(source_id,
        "unicode.vit", 7u, VITTE_DIAGNOSTIC_LSP_POSITION_UTF16,
        &position, &sources));
    assert(position.valid && position.line == 1u && position.character == 0u);
    {
        static const char crlf_text[] = "x\r\ny";
        vitte_source_id_t crlf_id;
        assert(vitte_source_add_cstr(&sources, "crlf.vit", "crlf.vit",
            crlf_text, &crlf_id));
        assert(vitte_diagnostic_lsp_source_position_converter(crlf_id,
            "crlf.vit", 1u, VITTE_DIAGNOSTIC_LSP_POSITION_UTF16,
            &position, &sources));
        assert(position.valid && position.line == 0u && position.character == 1u);
        assert(vitte_diagnostic_lsp_source_position_converter(crlf_id,
            "crlf.vit", 2u, VITTE_DIAGNOSTIC_LSP_POSITION_UTF16,
            &position, &sources));
        assert(position.valid && position.line == 0u && position.character == 1u);
        assert(vitte_diagnostic_lsp_source_position_converter(crlf_id,
            "crlf.vit", 3u, VITTE_DIAGNOSTIC_LSP_POSITION_UTF16,
            &position, &sources));
        assert(position.valid && position.line == 1u && position.character == 0u);
    }
    for (index = 0u; index < vitte_diagnostic_registry_count(); ++index) {
        const vitte_diagnostic_registry_entry_t *entry =
            vitte_diagnostic_registry_at(index);
        FILE *stream;
        assert(entry != NULL);
        assert(vitte_diagnostic_explain_find(entry->public_code) == entry);
        stream = tmpfile();
        assert(stream != NULL);
        assert(vitte_diagnostic_explain(stream, entry->public_code,
            VITTE_DIAGNOSTIC_EXPLAIN_TEXT, false) == VITTE_STATUS_OK);
        assert(fclose(stream) == 0);
        stream = tmpfile();
        assert(stream != NULL);
        assert(vitte_diagnostic_explain(stream, entry->public_code,
            VITTE_DIAGNOSTIC_EXPLAIN_JSON, false) == VITTE_STATUS_OK);
        assert(fclose(stream) == 0);
    }
    vitte_source_destroy(&sources);
}

static void
test_generic_lsp_dispatch_with_sources(void)
{
    vitte_source_context_t sources;
    vitte_source_id_t source_id;
    vitte_diagnostic_t storage[1];
    vitte_diagnostic_bag_t bag;
    vitte_ast_span_t span;
    FILE *stream;
    char output[8192];
    size_t bytes;

    assert(vitte_source_init(&sources));
    assert(vitte_source_add_cstr(&sources, "generic-lsp.vit",
        "generic-lsp.vit", "word\n", &source_id));
    memset(&span, 0, sizeof(span));
    span.source_id = source_id;
    span.source_name = "generic-lsp.vit";
    span.start_offset = 0u;
    span.end_offset = 4u;
    span.valid = true;
    assert(vitte_diagnostic_bag_init(&bag, storage, 1u) == VITTE_STATUS_OK);
    bag.options.sources = &sources;
    assert(vitte_diagnostic_emit(&bag, VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER, "E0201", "unexpected token",
        &span) == VITTE_STATUS_OK);
    stream = tmpfile();
    assert(stream != NULL);
    assert(vitte_diagnostic_write_all_format(stream, &bag,
        VITTE_DIAGNOSTIC_FORMAT_LSP) == VITTE_STATUS_OK);
    rewind(stream);
    bytes = fread(output, 1u, sizeof(output) - 1u, stream);
    output[bytes] = '\0';
    assert(strstr(output, "generic-lsp.vit") != NULL);
    assert(strstr(output, "\"line\":0") != NULL);
    assert(strstr(output, "\"character\":0") != NULL);
    assert(fclose(stream) == 0);
    vitte_diagnostic_bag_reset(&bag);
    vitte_source_destroy(&sources);
}

static void
test_format_parity_metadata(void)
{
    static const vitte_diagnostic_format_t formats[] = {
        VITTE_DIAGNOSTIC_FORMAT_TERMINAL,
        VITTE_DIAGNOSTIC_FORMAT_JSON,
        VITTE_DIAGNOSTIC_FORMAT_SARIF,
        VITTE_DIAGNOSTIC_FORMAT_LSP
    };
    vitte_diagnostic_t storage[1];
    vitte_diagnostic_bag_t bag;
    vitte_ast_span_t span;
    char output[262144];
    size_t index;

    memset(&span, 0, sizeof(span));
    span.source_name = "parity.vit";
    span.start_offset = 1u;
    span.end_offset = 3u;
    span.start_line = 0u;
    span.start_column = 1u;
    span.end_line = 0u;
    span.end_column = 3u;
    span.valid = true;
    assert(vitte_diagnostic_bag_init(&bag, storage, 1u) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_emit(&bag, VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK, "E0501", "type mismatch",
        &span) == VITTE_STATUS_OK);
    vitte_diagnostic_set_subject(vitte_diagnostic_at_mut(&bag, 0u),
        "value", "i64", "string");
    vitte_diagnostic_set_expected_found(vitte_diagnostic_at_mut(&bag, 0u),
        "integer", "text");
    vitte_diagnostic_set_probable_cause(vitte_diagnostic_at_mut(&bag, 0u),
        "the expression is text");
    assert(vitte_diagnostic_add_cause(vitte_diagnostic_at_mut(&bag, 0u),
        "a string was passed to an integer parameter", &span) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_help(vitte_diagnostic_at_mut(&bag, 0u),
        "convert the value before the call", NULL) == VITTE_STATUS_OK);

    for (index = 0u; index < sizeof(formats) / sizeof(formats[0]); ++index) {
        FILE *stream = tmpfile();
        size_t bytes;
        assert(stream != NULL);
        if (formats[index] == VITTE_DIAGNOSTIC_FORMAT_LSP) {
            vitte_diagnostic_lsp_options_t options;
            vitte_diagnostic_lsp_options_init(&options);
            options.position_converter = ascii_position;
            assert(vitte_diagnostic_render_lsp_array(stream, &bag, NULL,
                &options) == VITTE_STATUS_OK);
        } else {
            assert(vitte_diagnostic_write_all_format(stream, &bag,
                formats[index]) == VITTE_STATUS_OK);
        }
        rewind(stream);
        bytes = fread(output, 1u, sizeof(output) - 1u, stream);
        output[bytes] = '\0';
        assert(strstr(output, "E0501") != NULL);
        assert(strstr(output, "type-check") != NULL);
        assert(strstr(output, "i64") != NULL);
        assert(strstr(output, "string") != NULL);
        assert(strstr(output, "integer") != NULL);
        assert(strstr(output, "text") != NULL);
        assert(strstr(output, "the expression is text") != NULL);
        assert(strstr(output, "a string was passed to an integer parameter") != NULL);
        assert(strstr(output, "convert the value before the call") != NULL);
        assert(strstr(output, "parity.vit") != NULL);
        assert(fclose(stream) == 0);
    }
    vitte_diagnostic_bag_reset(&bag);
}

int
main(void)
{
    test_api_invalid_utf8_source_diagnostic();
    test_api_contract_diagnostics();
    test_classification_metadata();
    test_classification_rendering();
    test_contextual_help();
    test_structured_fields_all_formats();
    test_cli_bridge_consistency();
    test_lsp_source_positions_and_explanations();
    test_generic_lsp_dispatch_with_sources();
    test_format_parity_metadata();
    static const char source_name[] = "sample.vit";
    static const char source_text[] =
        "let value = 123;\n"
        "            value\n";
    vitte_ast_span_t use_span;
    vitte_ast_span_t declaration_span;
    vitte_diagnostic_t *diagnostic;
    vitte_diagnostic_t *contract_diagnostic;
    vitte_diagnostic_t storage[2];
    vitte_diagnostic_t help_storage[2];
    vitte_diagnostic_t limit_storage[1];
    vitte_diagnostic_bag_t bag;
    vitte_diagnostic_bag_t help_bag;
    vitte_diagnostic_bag_t limit_bag;
    vitte_diagnostic_terminal_options_t terminal_options;
    vitte_diagnostic_registry_validation_t registry_validation;
    vitte_source_context_t sources;
    vitte_source_id_t source_id;
    FILE *stream;
    char output[4096];
    size_t bytes;

    assert(vitte_source_init(&sources));
    assert(vitte_diagnostic_registry_validate(
        &registry_validation) == VITTE_STATUS_OK);
    assert(registry_validation.valid);
    assert(vitte_source_add_cstr(
        &sources,
        source_name,
        source_name,
        source_text,
        &source_id));

    memset(&use_span, 0, sizeof(use_span));
    use_span.source_name = source_name;
    use_span.source_id = source_id;
    use_span.start_offset = 29u;
    use_span.end_offset = 34u;
    use_span.start_line = 1u;
    use_span.start_column = 12u;
    use_span.end_line = 1u;
    use_span.end_column = 17u;
    use_span.valid = true;

    declaration_span = use_span;
    declaration_span.start_offset = 4u;
    declaration_span.end_offset = 9u;
    declaration_span.start_line = 0u;
    declaration_span.start_column = 4u;
    declaration_span.end_line = 0u;
    declaration_span.end_column = 9u;

    assert(vitte_diagnostic_bag_init(&bag, storage, 2u) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_emit(
        &bag,
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "VITTE_TYPE_E_MISMATCH",
        "type incompatible",
        &use_span) == VITTE_STATUS_OK);
    diagnostic = vitte_diagnostic_at_mut(&bag, 0u);
    assert(diagnostic != NULL);
    vitte_diagnostic_set_subject(diagnostic, "expected", "int", "string");
    vitte_diagnostic_set_context(
        diagnostic,
        NULL,
        NULL,
        "clamp",
        "argument 1 of procedure call");
    assert(vitte_diagnostic_add_location(
        diagnostic,
        VITTE_DIAGNOSTIC_LOCATION_DECLARATION,
        &declaration_span,
        "parameter declared here") == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_cause(
        diagnostic,
        "string cannot satisfy integer parameter",
        &use_span) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_note(
        diagnostic,
        "the parameter is statically typed",
        NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_help(
        diagnostic,
        "provide an integer literal",
        NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_suggestion(
        diagnostic,
        "remove the quotes",
        &use_span,
        "42",
        VITTE_DIAGNOSTIC_APPLICABILITY_MACHINE) == VITTE_STATUS_OK);

    assert(vitte_diagnostic_emit(
        &bag,
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "VITTE_CONTRACT_E_POSTCONDITION",
        "postcondition failed",
        &use_span) == VITTE_STATUS_OK);
    contract_diagnostic = vitte_diagnostic_at_mut(&bag, 1u);
    assert(contract_diagnostic != NULL);
    assert(vitte_diagnostic_set_contract(
        contract_diagnostic,
        VITTE_DIAGNOSTIC_CONTRACT_ENSURES,
        "result >= minimum",
        "clamp",
        "condition evaluated to false",
        &use_span,
        NULL,
        NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_note(
        contract_diagnostic,
        "`minimum`: 0",
        NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_note(
        contract_diagnostic,
        "`maximum`: 100",
        NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_note(
        contract_diagnostic,
        "`result`: -1",
        NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_cause(
        contract_diagnostic,
        "returned value produced the violation",
        &use_span) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_add_help(
        contract_diagnostic,
        "every successful return from `clamp` must satisfy this condition",
        NULL) == VITTE_STATUS_OK);

    stream = tmpfile();
    assert(stream != NULL);
    vitte_diagnostic_terminal_options_init(&terminal_options);
    terminal_options.color = false;
    terminal_options.sources = &sources;
    assert(vitte_diagnostic_render_terminal(
        stream,
        &bag,
        &terminal_options) == VITTE_STATUS_OK);
    rewind(stream);
    bytes = fread(output, 1u, sizeof(output) - 1u, stream);
    output[bytes] = '\0';
    assert(strstr(output, "sample.vit:2:13") != NULL);
    assert(strstr(output, "error[E0501]: type incompatible") != NULL);
    assert(strstr(output, "error[E0702]: postcondition failed") != NULL);
    assert(strstr(output, "            value") != NULL);
    assert(strstr(output, "^~~~~") != NULL);
    assert(strstr(output, "^~~~~ condition evaluated to false") != NULL);
    assert(strstr(output, "type incompatible") != NULL);
    assert(strstr(output, "procedure: clamp") != NULL);
    assert(strstr(output, "procedure: clamp") < strstr(output, "--> sample.vit"));
    assert(strstr(output, "symbol: expected") != NULL);
    assert(strstr(output, "type: expected `int`, found `string`") != NULL);
    assert(strstr(output, "argument 1 of procedure call") != NULL);
    assert(strstr(output, "parameter declared here") != NULL);
    assert(strstr(output, "caused by: string cannot satisfy integer parameter") != NULL);
    assert(strstr(output, "note: the parameter is statically typed") != NULL);
    assert(strstr(output, "help: provide an integer literal") != NULL);
    assert(strstr(output, "help: remove the quotes [machine-applicable]") != NULL);
    assert(strstr(output, "+ 42") != NULL);
    assert(strstr(output, "postcondition failed") != NULL);
    assert(strstr(output, "phase: contract") != NULL);
    assert(strstr(output, "contract: ensures `result >= minimum`") != NULL);
    assert(strstr(output, "condition evaluated to false") != NULL);
    assert(strstr(output, "`minimum`: 0") != NULL);
    assert(strstr(output, "`maximum`: 100") != NULL);
    assert(strstr(output, "`result`: -1") != NULL);
    assert(strstr(output, "returned value produced the violation") != NULL);
    assert(strstr(output, "every successful return from `clamp` must satisfy this condition") != NULL);
    assert(strstr(output, "`result`: -1") < strstr(output, "returned value produced the violation"));
    assert(strstr(output, "returned value produced the violation") <
        strstr(output, "every successful return from `clamp` must satisfy this condition"));
    assert(fclose(stream) == 0);

    assert(bag.count == 2u);
    assert(bag.counts.error_count == 2u);
    assert(vitte_diagnostic_has_errors(&bag));

    assert(vitte_diagnostic_bag_init(
        &help_bag,
        help_storage,
        2u) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_emit(
        &help_bag,
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "VITTE_PARSER_E_EXPECTED_EXPRESSION",
        "expected expression",
        NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_emit(
        &help_bag,
        VITTE_DIAGNOSTIC_WARNING,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "VITTE_WARNING_TRUNCATION",
        "possible value truncation",
        NULL) == VITTE_STATUS_OK);

    stream = tmpfile();
    assert(stream != NULL);
    assert(vitte_diagnostic_render_terminal(
        stream,
        &help_bag,
        &terminal_options) == VITTE_STATUS_OK);
    rewind(stream);
    bytes = fread(output, 1u, sizeof(output) - 1u, stream);
    output[bytes] = '\0';
    assert(strstr(output, "help: Provide a valid expression at this position") != NULL);
    assert(strstr(output, "vitte explain E0203") != NULL);
    assert(strstr(output, "help: Verify the source value's range before conversion") != NULL);
    assert(fclose(stream) == 0);

    terminal_options.show_help = false;
    stream = tmpfile();
    assert(stream != NULL);
    assert(vitte_diagnostic_render_terminal(stream, &help_bag, &terminal_options) == VITTE_STATUS_OK);
    rewind(stream);
    bytes = fread(output, 1u, sizeof(output) - 1u, stream);
    output[bytes] = '\0';
    assert(strstr(output, "help:") == NULL);
    assert(fclose(stream) == 0);
    terminal_options.show_help = true;

    assert(vitte_diagnostic_bag_init(&limit_bag, limit_storage, 1u) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_emit(
        &limit_bag,
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        VITTE_INFRA_E_INTERNAL,
        "first error",
        NULL) == VITTE_STATUS_OK);
    assert(vitte_diagnostic_emit(
        &limit_bag,
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        VITTE_INFRA_E_LIMIT,
        "second error",
        NULL) == VITTE_STATUS_ERROR_INVALID_STATE);
    assert(limit_bag.count == 1u);
    assert(limit_bag.counts.error_count == 1u);
    assert(strcmp(limit_storage[0].short_code, "E0001") == 0);

    vitte_diagnostic_bag_reset(&bag);
    vitte_diagnostic_bag_reset(&help_bag);
    vitte_diagnostic_bag_reset(&limit_bag);
    vitte_source_destroy(&sources);
    return 0;
}
