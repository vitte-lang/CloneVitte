/*
 * Public C ABI shim for the Vitte-native compiler API.
 *
 * Compiler state, compilation results, diagnostics and pipeline orchestration
 * live in compiler/api/api.vitte. This file only adapts the stable C ABI.
 */

typedef struct Api Api;
typedef struct ApiResult ApiResult;

#include "../diagnostic/cli_bridge.h"

extern vitte_status_t vitte_api_native_create(Api **out_api);
extern void vitte_api_native_destroy(Api *api);
extern vitte_status_t vitte_api_native_compile_source(
    Api *api, const char *source_name, const char *source,
    size_t source_length, ApiResult **out_result);
extern vitte_status_t vitte_api_native_compile_string(
    Api *api, const char *source, ApiResult **out_result);
extern vitte_status_t vitte_api_native_compile_file(
    Api *api, const char *path, ApiResult **out_result);
extern void vitte_api_native_result_destroy(ApiResult *result);
extern vitte_status_t vitte_api_native_result_status(const ApiResult *result);
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
extern vitte_status_t vitte_api_native_error_status(const Api *api);
extern const char *vitte_api_native_error_code(const Api *api);
extern const char *vitte_api_native_error_message(const Api *api);
extern const char *vitte_api_native_error_details(const Api *api);
extern void vitte_api_native_clear_error(Api *api);

/* The generated Vitte Error layout is status + three borrowed C strings. */
typedef struct vitte_native_error {
    vitte_status_t status;
    const char *code;
    const char *message;
    const char *details;
} vitte_native_error_t;

extern vitte_native_error_t *compiler_api_error_error_last(void);
extern void compiler_api_error_error_set_last(vitte_native_error_t *error);
extern void compiler_api_error_error_clear_last(void);

extern const char *api_version(void);
extern uint32_t api_version_major(void);
extern uint32_t api_version_minor(void);
extern uint32_t api_version_patch(void);
extern const char *api_build_mode(void);
extern const char *api_compiler(void);
extern const char *api_platform(void);
extern const char *api_architecture(void);

extern uint32_t compiler_driver_pipeline_vitte_native_frontend_run(
    const char *source, size_t source_length, uint64_t file_id);
extern uint32_t compiler_driver_pipeline_vitte_native_frontend_run_with_config(
    const char *source, size_t source_length, uint64_t file_id,
    uint8_t *config_bytes);
extern uint32_t compiler_driver_pipeline_vitte_native_frontend_generate_with_config(
    const char *source, size_t source_length, uint64_t file_id,
    uint8_t *config_bytes, uint8_t **generated_source,
    size_t *generated_source_length);
extern uint32_t compiler_driver_pipeline_vitte_native_frontend_generate_with_diagnostics(
    const char *source, size_t source_length, uint64_t file_id,
    const char *source_name, uint8_t *config_bytes,
    uint8_t **generated_source, size_t *generated_source_length,
    void **diagnostics, size_t *diagnostic_count);
extern uint32_t compiler_driver_pipeline_vitte_native_frontend_execute_with_diagnostics(
    uint32_t action, const char *source, size_t source_length,
    uint64_t file_id, const char *source_name, uint8_t *config_bytes,
    uint8_t **generated_source, size_t *generated_source_length,
    void **diagnostics, size_t *diagnostic_count);
extern void compiler_driver_pipeline_vitte_native_frontend_release_output(
    uint8_t *generated_source);
extern void vitte_api_native_diagnostic_transport_destroy(
    uint8_t *diagnostics);

static _Thread_local vitte_error_t vitte_api_native_error_snapshot;

static void
vitte_api_native_snapshot_error(
    vitte_status_t status,
    const char *code,
    const char *message,
    const char *details)
{
    size_t length = 0u;

    vitte_api_native_error_snapshot.status = status;
    vitte_api_native_error_snapshot.code =
        code != NULL ? code : "VITTE_ERROR_UNKNOWN";
    vitte_api_native_error_snapshot.message =
        message != NULL ? message : "unknown error";

    if (details == NULL) {
        vitte_api_native_error_snapshot.details = NULL;
        vitte_api_native_error_snapshot.details_storage[0] = '\0';
        return;
    }
    while (length + 1u < sizeof(vitte_api_native_error_snapshot.details_storage) &&
           details[length] != '\0') {
        ++length;
    }
    if (length != 0u) {
        memcpy(vitte_api_native_error_snapshot.details_storage, details, length);
    }
    vitte_api_native_error_snapshot.details_storage[length] = '\0';
    vitte_api_native_error_snapshot.details =
        vitte_api_native_error_snapshot.details_storage;
}

static void
vitte_api_set_last_error(
    vitte_status_t status,
    const char *code,
    const char *message)
{
    vitte_native_error_t error;

    error.status = status;
    error.code = code;
    error.message = message;
    error.details = NULL;
    compiler_api_error_error_set_last(&error);
}

uint32_t
vitte_native_frontend_run(
    const char *source,
    size_t source_length,
    uint64_t file_id)
{
    return compiler_driver_pipeline_vitte_native_frontend_run(
        source, source_length, file_id);
}

uint32_t
vitte_native_frontend_run_with_config(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const void *config)
{
    return compiler_driver_pipeline_vitte_native_frontend_run_with_config(
        source, source_length, file_id, (uint8_t *)config);
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
        source, source_length, file_id, (uint8_t *)config,
        generated_source, generated_source_length);
}

uint32_t
vitte_native_frontend_generate_with_diagnostics(
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const char *source_name,
    const void *config,
    uint8_t **generated_source,
    size_t *generated_source_length,
    vitte_cli_diagnostic_t **diagnostics,
    size_t *diagnostic_count)
{
    void *native_diagnostics = NULL;
    uint32_t status;

    if (diagnostics != NULL) {
        *diagnostics = NULL;
    }
    if (diagnostic_count != NULL) {
        *diagnostic_count = 0u;
    }
    if (diagnostics == NULL || diagnostic_count == NULL) {
        return 0u;
    }

    status = compiler_driver_pipeline_vitte_native_frontend_generate_with_diagnostics(
        source,
        source_length,
        file_id,
        source_name,
        (uint8_t *)config,
        generated_source,
        generated_source_length,
        &native_diagnostics,
        diagnostic_count);
    *diagnostics = (vitte_cli_diagnostic_t *)native_diagnostics;
    return status;
}

uint32_t
vitte_native_frontend_execute_with_diagnostics(
    uint32_t action,
    const char *source,
    size_t source_length,
    uint64_t file_id,
    const char *source_name,
    const void *config,
    uint8_t **generated_source,
    size_t *generated_source_length,
    vitte_cli_diagnostic_t **diagnostics,
    size_t *diagnostic_count)
{
    void *native_diagnostics = NULL;
    uint32_t status;

    if (generated_source != NULL) {
        *generated_source = NULL;
    }
    if (generated_source_length != NULL) {
        *generated_source_length = 0u;
    }
    if (diagnostics != NULL) {
        *diagnostics = NULL;
    }
    if (diagnostic_count != NULL) {
        *diagnostic_count = 0u;
    }
    if (generated_source == NULL || generated_source_length == NULL ||
        diagnostics == NULL || diagnostic_count == NULL) {
        return 0u;
    }

    status = compiler_driver_pipeline_vitte_native_frontend_execute_with_diagnostics(
        action,
        source,
        source_length,
        file_id,
        source_name,
        (uint8_t *)config,
        generated_source,
        generated_source_length,
        &native_diagnostics,
        diagnostic_count);
    *diagnostics = (vitte_cli_diagnostic_t *)native_diagnostics;
    return status;
}

void
vitte_native_frontend_release_output(uint8_t *generated_source)
{
    compiler_driver_pipeline_vitte_native_frontend_release_output(
        generated_source);
}

void
vitte_native_frontend_release_diagnostics(
    vitte_cli_diagnostic_t *diagnostics)
{
    vitte_api_native_diagnostic_transport_destroy((uint8_t *)diagnostics);
}

vitte_status_t
vitte_api_create(vitte_api_t **out_api)
{
    Api *native_api = NULL;
    vitte_status_t status;

    if (out_api == NULL) {
        vitte_api_set_last_error(
            VITTE_STATUS_ERROR_INVALID_ARGUMENT,
            "VITTE_API_INVALID_ARGUMENT", "out_api is NULL");
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }
    *out_api = NULL;
    status = vitte_api_native_create(&native_api);
    if (status != VITTE_STATUS_OK) {
        vitte_api_set_last_error(
            status, "VITTE_API_NATIVE_CREATE",
            "native Vitte API context initialization failed");
        return status;
    }
    *out_api = (vitte_api_t *)native_api;
    return VITTE_STATUS_OK;
}

void
vitte_api_destroy(vitte_api_t *api)
{
    vitte_api_native_destroy((Api *)api);
}

static vitte_status_t
vitte_api_native_compile_result(
    vitte_status_t status,
    ApiResult *native_result,
    vitte_api_result_t **out_result)
{
    if (out_result != NULL) {
        *out_result = (vitte_api_result_t *)native_result;
    } else if (native_result != NULL) {
        vitte_api_native_result_destroy(native_result);
    }
    return status;
}

vitte_status_t
vitte_api_compile_source(
    vitte_api_t *api,
    const char *source_name,
    const char *source,
    size_t source_length,
    vitte_api_result_t **out_result)
{
    ApiResult *native_result = NULL;
    vitte_status_t status;

    if (out_result != NULL) {
        *out_result = NULL;
    }
    status = vitte_api_native_compile_source(
        (Api *)api, source_name, source, source_length, &native_result);
    return vitte_api_native_compile_result(status, native_result, out_result);
}

vitte_status_t
vitte_api_compile_string(
    vitte_api_t *api,
    const char *source,
    vitte_api_result_t **out_result)
{
    ApiResult *native_result = NULL;
    vitte_status_t status;

    if (out_result != NULL) {
        *out_result = NULL;
    }
    status = vitte_api_native_compile_string(
        (Api *)api, source, &native_result);
    return vitte_api_native_compile_result(status, native_result, out_result);
}

vitte_status_t
vitte_api_compile_file(
    vitte_api_t *api,
    const char *path,
    vitte_api_result_t **out_result)
{
    ApiResult *native_result = NULL;
    vitte_status_t status;

    if (out_result != NULL) {
        *out_result = NULL;
    }
    status = vitte_api_native_compile_file(
        (Api *)api, path, &native_result);
    return vitte_api_native_compile_result(status, native_result, out_result);
}

void
vitte_api_result_destroy(vitte_api_result_t *result)
{
    vitte_api_native_result_destroy((ApiResult *)result);
}

vitte_status_t
vitte_api_result_status(const vitte_api_result_t *result)
{
    return result != NULL
        ? vitte_api_native_result_status((const ApiResult *)result)
        : VITTE_STATUS_ERROR_INVALID_ARGUMENT;
}

bool
vitte_api_result_succeeded(const vitte_api_result_t *result)
{
    return result != NULL &&
        vitte_api_native_result_succeeded((const ApiResult *)result);
}

const char *
vitte_api_result_source_name(const vitte_api_result_t *result)
{
    return result != NULL
        ? vitte_api_native_result_source_name((const ApiResult *)result)
        : NULL;
}

const void *
vitte_api_result_output(
    const vitte_api_result_t *result,
    size_t *out_size)
{
    const ApiResult *native_result = (const ApiResult *)result;

    if (out_size != NULL) {
        *out_size = native_result != NULL
            ? vitte_api_native_result_output_size(native_result)
            : 0u;
    }
    return native_result != NULL
        ? vitte_api_native_result_output_data(native_result)
        : NULL;
}

const char *
vitte_api_result_diagnostics(
    const vitte_api_result_t *result,
    size_t *out_size)
{
    const ApiResult *native_result = (const ApiResult *)result;

    if (out_size != NULL) {
        *out_size = native_result != NULL
            ? vitte_api_native_result_diagnostics_size(native_result)
            : 0u;
    }
    return native_result != NULL
        ? (const char *)vitte_api_native_result_diagnostics(native_result)
        : NULL;
}

size_t
vitte_api_result_error_count(const vitte_api_result_t *result)
{
    return result != NULL
        ? vitte_api_native_result_error_count((const ApiResult *)result)
        : 0u;
}

size_t
vitte_api_result_warning_count(const vitte_api_result_t *result)
{
    return result != NULL
        ? vitte_api_native_result_warning_count((const ApiResult *)result)
        : 0u;
}

size_t
vitte_api_result_note_count(const vitte_api_result_t *result)
{
    return result != NULL
        ? vitte_api_native_result_note_count((const ApiResult *)result)
        : 0u;
}

bool
vitte_api_is_valid(const vitte_api_t *api)
{
    return api != NULL && vitte_api_native_is_valid((const Api *)api);
}

bool
vitte_api_is_compiling(const vitte_api_t *api)
{
    return api != NULL && vitte_api_native_is_compiling((const Api *)api);
}

size_t
vitte_api_compilation_count(const vitte_api_t *api)
{
    return api != NULL
        ? vitte_api_native_compilation_count((const Api *)api)
        : 0u;
}

size_t
vitte_api_successful_compilation_count(const vitte_api_t *api)
{
    return api != NULL
        ? vitte_api_native_successful_compilation_count((const Api *)api)
        : 0u;
}

size_t
vitte_api_failed_compilation_count(const vitte_api_t *api)
{
    return api != NULL
        ? vitte_api_native_failed_compilation_count((const Api *)api)
        : 0u;
}

const vitte_error_t *
vitte_api_last_error(const vitte_api_t *api)
{
    const vitte_native_error_t *native_error;

    if (api == NULL) {
        native_error = compiler_api_error_error_last();
        if (native_error == NULL) {
            vitte_api_native_snapshot_error(
                VITTE_STATUS_ERROR_INTERNAL,
                "VITTE_ERROR_INTERNAL",
                "native thread error state is unavailable",
                NULL);
        } else {
            vitte_api_native_snapshot_error(
                native_error->status,
                native_error->code,
                native_error->message,
                native_error->details);
        }
        return &vitte_api_native_error_snapshot;
    }
    vitte_api_native_snapshot_error(
        vitte_api_native_error_status((const Api *)api),
        vitte_api_native_error_code((const Api *)api),
        vitte_api_native_error_message((const Api *)api),
        vitte_api_native_error_details((const Api *)api));
    return &vitte_api_native_error_snapshot;
}

void
vitte_api_clear_error(vitte_api_t *api)
{
    if (api != NULL) {
        vitte_api_native_clear_error((Api *)api);
    }
    compiler_api_error_error_clear_last();
}

bool
vitte_api_validate(const vitte_api_t *api)
{
    size_t total;
    size_t successful;
    size_t failed;

    if (api == NULL || !vitte_api_native_is_valid((const Api *)api)) {
        return false;
    }
    total = vitte_api_native_compilation_count((const Api *)api);
    successful = vitte_api_native_successful_compilation_count((const Api *)api);
    failed = vitte_api_native_failed_compilation_count((const Api *)api);
    return successful <= total && failed <= total &&
        successful <= SIZE_MAX - failed && successful + failed <= total;
}

const char *vitte_api_version(void) { return api_version(); }
uint32_t vitte_api_version_major(void) { return api_version_major(); }
uint32_t vitte_api_version_minor(void) { return api_version_minor(); }
uint32_t vitte_api_version_patch(void) { return api_version_patch(); }
const char *vitte_api_build_mode(void) { return api_build_mode(); }
const char *vitte_api_compiler(void) { return api_compiler(); }
const char *vitte_api_platform(void) { return api_platform(); }
const char *vitte_api_architecture(void) { return api_architecture(); }
