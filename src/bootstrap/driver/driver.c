/*
 * Vitte Compiler
 * src/driver/driver.c
 *
 * Canonical compiler-driver orchestration core.
 *
 * The driver coordinates compiler subsystems. It does not implement their
 * internal algorithms.
 *
 * Intended architecture:
 *
 *   CLI / embedding API
 *          |
 *          v
 *   +-----------------------+
 *   |        driver         |
 *   +-----------------------+
 *          |
 *          +-- configuration
 *          +-- source loading
 *          +-- frontend
 *          |     +-- lexer
 *          |     +-- parser
 *          |     +-- AST
 *          |     +-- imports
 *          |     +-- names
 *          |     +-- types
 *          |     +-- contracts
 *          |     +-- constant folding
 *          |
 *          +-- lowering
 *          |     +-- HIR
 *          |     +-- IR
 *          |
 *          +-- optimization
 *          +-- code generation
 *          +-- backend
 *          +-- assembler/compiler
 *          +-- linker
 *          +-- execution
 *          +-- diagnostics
 *
 * The concrete subsystem boundary is represented through callbacks so the
 * driver can remain independent of AST/HIR/IR/backend implementation details.
 *
 * ISO C17.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================= */
/* Constants                                                                 */
/* ========================================================================= */

#define VITTE_DRIVER_MAGIC \
    UINT64_C(0x5649545445445256)

#define VITTE_DRIVER_DEAD_MAGIC \
    UINT64_C(0x4445414444525621)

#define VITTE_DRIVER_FNV_OFFSET \
    UINT64_C(14695981039346656037)

#define VITTE_DRIVER_FNV_PRIME \
    UINT64_C(1099511628211)

#define VITTE_DRIVER_DEFAULT_MAX_INPUTS \
    ((size_t)65536u)

#define VITTE_DRIVER_DEFAULT_INITIAL_INPUT_CAPACITY \
    ((size_t)16u)

#define VITTE_DRIVER_DEFAULT_MAX_ERRORS \
    ((size_t)100u)

#define VITTE_DRIVER_DEFAULT_MAX_WARNINGS \
    ((size_t)0u)

#define VITTE_DRIVER_DEFAULT_MAX_OUTPUT_BYTES \
    ((size_t)0u)

/* ========================================================================= */
/* Errors                                                                    */
/* ========================================================================= */

typedef enum vitte_driver_error {
    VITTE_DRIVER_ERROR_NONE = 0,

    VITTE_DRIVER_ERROR_INVALID_DRIVER,
    VITTE_DRIVER_ERROR_INVALID_ARGUMENT,
    VITTE_DRIVER_ERROR_INVALID_STATE,
    VITTE_DRIVER_ERROR_INVALID_CONFIG,

    VITTE_DRIVER_ERROR_NO_INPUT,
    VITTE_DRIVER_ERROR_DUPLICATE_INPUT,
    VITTE_DRIVER_ERROR_TOO_MANY_INPUTS,

    VITTE_DRIVER_ERROR_SOURCE,
    VITTE_DRIVER_ERROR_LEXER,
    VITTE_DRIVER_ERROR_PARSER,
    VITTE_DRIVER_ERROR_IMPORT,
    VITTE_DRIVER_ERROR_NAME_RESOLUTION,
    VITTE_DRIVER_ERROR_TYPE_CHECK,
    VITTE_DRIVER_ERROR_CONTRACT,
    VITTE_DRIVER_ERROR_CONSTANT_FOLD,
    VITTE_DRIVER_ERROR_HIR,
    VITTE_DRIVER_ERROR_IR,
    VITTE_DRIVER_ERROR_OPTIMIZATION,
    VITTE_DRIVER_ERROR_CODEGEN,
    VITTE_DRIVER_ERROR_BACKEND,
    VITTE_DRIVER_ERROR_TOOLCHAIN,
    VITTE_DRIVER_ERROR_LINK,
    VITTE_DRIVER_ERROR_EXECUTION,

    VITTE_DRIVER_ERROR_TOO_MANY_ERRORS,
    VITTE_DRIVER_ERROR_OUTPUT_LIMIT,

    VITTE_DRIVER_ERROR_CANCELLED,

    VITTE_DRIVER_ERROR_OVERFLOW,
    VITTE_DRIVER_ERROR_OUT_OF_MEMORY,

    VITTE_DRIVER_ERROR_UNSUPPORTED,
    VITTE_DRIVER_ERROR_INTERNAL,
    VITTE_DRIVER_ERROR_CORRUPTION,

    VITTE_DRIVER_ERROR_COUNT
} vitte_driver_error_t;

/* ========================================================================= */
/* State                                                                     */
/* ========================================================================= */

typedef enum vitte_driver_state {
    VITTE_DRIVER_STATE_INVALID = 0,

    VITTE_DRIVER_STATE_READY,
    VITTE_DRIVER_STATE_PREPARING,
    VITTE_DRIVER_STATE_PREPARED,
    VITTE_DRIVER_STATE_RUNNING,
    VITTE_DRIVER_STATE_FINISHED,
    VITTE_DRIVER_STATE_FAILED,
    VITTE_DRIVER_STATE_CANCELLED,
    VITTE_DRIVER_STATE_DESTROYED,

    VITTE_DRIVER_STATE_COUNT
} vitte_driver_state_t;

/* ========================================================================= */
/* Commands                                                                  */
/* ========================================================================= */

typedef enum vitte_driver_command {
    VITTE_DRIVER_COMMAND_INVALID = 0,

    VITTE_DRIVER_COMMAND_BUILD,
    VITTE_DRIVER_COMMAND_CHECK,
    VITTE_DRIVER_COMMAND_RUN,
    VITTE_DRIVER_COMMAND_TEST,

    VITTE_DRIVER_COMMAND_COUNT
} vitte_driver_command_t;

/* ========================================================================= */
/* Phases                                                                    */
/* ========================================================================= */

typedef enum vitte_driver_phase {
    VITTE_DRIVER_PHASE_NONE = 0,

    VITTE_DRIVER_PHASE_PREPARE,

    VITTE_DRIVER_PHASE_LOAD_SOURCE,

    VITTE_DRIVER_PHASE_LEX,
    VITTE_DRIVER_PHASE_PARSE,

    VITTE_DRIVER_PHASE_IMPORTS,
    VITTE_DRIVER_PHASE_NAMES,
    VITTE_DRIVER_PHASE_TYPES,
    VITTE_DRIVER_PHASE_CONTRACTS,
    VITTE_DRIVER_PHASE_CONSTANT_FOLD,

    VITTE_DRIVER_PHASE_HIR,
    VITTE_DRIVER_PHASE_IR,

    VITTE_DRIVER_PHASE_OPTIMIZE,

    VITTE_DRIVER_PHASE_CODEGEN,
    VITTE_DRIVER_PHASE_BACKEND,

    VITTE_DRIVER_PHASE_TOOLCHAIN,
    VITTE_DRIVER_PHASE_LINK,

    VITTE_DRIVER_PHASE_EXECUTE,

    VITTE_DRIVER_PHASE_FINALIZE,

    VITTE_DRIVER_PHASE_COUNT
} vitte_driver_phase_t;

/* ========================================================================= */
/* Severity                                                                  */
/* ========================================================================= */

typedef enum vitte_driver_severity {
    VITTE_DRIVER_SEVERITY_INVALID = 0,

    VITTE_DRIVER_SEVERITY_NOTE,
    VITTE_DRIVER_SEVERITY_WARNING,
    VITTE_DRIVER_SEVERITY_ERROR,

    VITTE_DRIVER_SEVERITY_COUNT
} vitte_driver_severity_t;

/* ========================================================================= */
/* Optimization                                                              */
/* ========================================================================= */

typedef enum vitte_driver_optimization {
    VITTE_DRIVER_OPTIMIZATION_DEFAULT = 0,

    VITTE_DRIVER_OPTIMIZATION_NONE,
    VITTE_DRIVER_OPTIMIZATION_DEBUG,
    VITTE_DRIVER_OPTIMIZATION_BALANCED,
    VITTE_DRIVER_OPTIMIZATION_SPEED,
    VITTE_DRIVER_OPTIMIZATION_SIZE,
    VITTE_DRIVER_OPTIMIZATION_AGGRESSIVE,

    VITTE_DRIVER_OPTIMIZATION_COUNT
} vitte_driver_optimization_t;

/* ========================================================================= */
/* Backend                                                                   */
/* ========================================================================= */

typedef enum vitte_driver_backend {
    VITTE_DRIVER_BACKEND_DEFAULT = 0,

    VITTE_DRIVER_BACKEND_C17,
    VITTE_DRIVER_BACKEND_NATIVE,
    VITTE_DRIVER_BACKEND_BYTECODE,

    VITTE_DRIVER_BACKEND_COUNT
} vitte_driver_backend_t;

/* ========================================================================= */
/* Source location                                                           */
/* ========================================================================= */

typedef struct vitte_driver_source_location {
    uint32_t file_id;
    size_t begin;
    size_t end;
    bool valid;
} vitte_driver_source_location_t;

/* ========================================================================= */
/* Input                                                                     */
/* ========================================================================= */

typedef struct vitte_driver_input {
    uint64_t id;

    /*
     * Borrowed path/name.
     */
    const char *name;

    /*
     * Optional subsystem-defined input object.
     *
     * For example, an embedding API may already own a source object and pass
     * it here instead of requiring filesystem loading.
     */
    const void *user_data;

    bool primary;
} vitte_driver_input_t;

/* ========================================================================= */
/* Diagnostic                                                                */
/* ========================================================================= */

typedef struct vitte_driver_diagnostic {
    vitte_driver_severity_t severity;
    vitte_driver_phase_t phase;

    const char *code;
    const char *message;

    vitte_driver_source_location_t location;
} vitte_driver_diagnostic_t;

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

typedef struct vitte_driver_config {
    vitte_driver_command_t command;

    vitte_driver_backend_t backend;
    vitte_driver_optimization_t optimization;

    size_t max_inputs;
    size_t max_errors;
    size_t max_warnings;
    size_t max_output_bytes;

    bool deterministic;

    bool warnings_as_errors;
    bool fail_fast;

    bool emit_debug_info;
    bool emit_source_map;
    bool emit_comments;

    bool runtime_checks;
    bool bounds_checks;
    bool null_checks;
    bool overflow_checks;

    bool incremental;

    bool stop_after_parse;
    bool stop_after_semantic;
    bool stop_after_ir;
    bool stop_after_codegen;

    bool execute_after_build;
} vitte_driver_config_t;

/* ========================================================================= */
/* Phase timing                                                              */
/* ========================================================================= */

typedef struct vitte_driver_phase_stats {
    uint64_t invocations;
    uint64_t successes;
    uint64_t failures;

    uint64_t elapsed_nanoseconds;
} vitte_driver_phase_stats_t;

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

typedef struct vitte_driver_stats {
    size_t input_count;

    size_t processed_input_count;

    size_t error_count;
    size_t warning_count;
    size_t note_count;

    size_t output_bytes;

    uint64_t phase_count;

    uint64_t input_fingerprint;
    uint64_t configuration_fingerprint;

    uint64_t elapsed_nanoseconds;

    vitte_driver_phase_stats_t phases[VITTE_DRIVER_PHASE_COUNT];

    bool prepared;
    bool finished;
    bool failed;
    bool cancelled;
} vitte_driver_stats_t;

/* ========================================================================= */
/* Forward declaration                                                       */
/* ========================================================================= */

typedef struct vitte_driver vitte_driver_t;

/* ========================================================================= */
/* Callback types                                                            */
/* ========================================================================= */

typedef bool (*vitte_driver_prepare_fn)(
    vitte_driver_t *driver,
    void *user_data);

typedef bool (*vitte_driver_phase_fn)(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    void *user_data);

typedef bool (*vitte_driver_input_phase_fn)(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    const vitte_driver_input_t *input,
    void *user_data);

typedef bool (*vitte_driver_finalize_fn)(
    vitte_driver_t *driver,
    bool success,
    void *user_data);

typedef void (*vitte_driver_destroy_fn)(
    vitte_driver_t *driver,
    void *user_data);

typedef void (*vitte_driver_diagnostic_fn)(
    vitte_driver_t *driver,
    const vitte_driver_diagnostic_t *diagnostic,
    void *user_data);

typedef bool (*vitte_driver_cancel_fn)(
    const vitte_driver_t *driver,
    void *user_data);

/* ========================================================================= */
/* Pipeline                                                                  */
/* ========================================================================= */

/*
 * Concrete compiler integration interface.
 *
 * Callbacks may be NULL when a phase is intentionally absent.
 *
 * Required callbacks depend on the command and selected stopping point.
 */
typedef struct vitte_driver_pipeline {
    vitte_driver_prepare_fn prepare;

    vitte_driver_input_phase_fn load_source;

    vitte_driver_input_phase_fn lex;
    vitte_driver_input_phase_fn parse;

    vitte_driver_phase_fn resolve_imports;
    vitte_driver_phase_fn resolve_names;
    vitte_driver_phase_fn check_types;
    vitte_driver_phase_fn check_contracts;
    vitte_driver_phase_fn fold_constants;

    vitte_driver_phase_fn lower_hir;
    vitte_driver_phase_fn lower_ir;

    vitte_driver_phase_fn optimize;

    vitte_driver_phase_fn codegen;
    vitte_driver_phase_fn backend;

    vitte_driver_phase_fn toolchain;
    vitte_driver_phase_fn link;

    vitte_driver_phase_fn execute;

    vitte_driver_finalize_fn finalize;
    vitte_driver_destroy_fn destroy;
} vitte_driver_pipeline_t;

/* ========================================================================= */
/* Driver                                                                    */
/* ========================================================================= */

struct vitte_driver {
    uint64_t magic;

    vitte_driver_state_t state;
    vitte_driver_error_t last_error;
    vitte_driver_phase_t phase;

    vitte_driver_config_t config;

    vitte_driver_input_t *inputs;
    size_t input_count;
    size_t input_capacity;

    vitte_driver_pipeline_t pipeline;
    void *pipeline_user_data;

    vitte_driver_diagnostic_fn diagnostic;
    void *diagnostic_user_data;

    vitte_driver_cancel_fn cancel;
    void *cancel_user_data;

    vitte_driver_stats_t stats;

    uint64_t next_input_id;
    uint64_t generation;

    bool cancellation_requested;
    bool pipeline_installed;
};

/* ========================================================================= */
/* Public prototypes                                                         */
/* ========================================================================= */

const char *
vitte_driver_error_name(vitte_driver_error_t error);

const char *
vitte_driver_state_name(vitte_driver_state_t state);

const char *
vitte_driver_command_name(vitte_driver_command_t command);

const char *
vitte_driver_phase_name(vitte_driver_phase_t phase);

const char *
vitte_driver_severity_name(vitte_driver_severity_t severity);

const char *
vitte_driver_backend_name(vitte_driver_backend_t backend);

const char *
vitte_driver_optimization_name(vitte_driver_optimization_t optimization);

vitte_driver_config_t
vitte_driver_config_default(void);

bool
vitte_driver_config_validate(const vitte_driver_config_t *config);

bool
vitte_driver_is_valid(const vitte_driver_t *driver);

bool
vitte_driver_init(
    vitte_driver_t *driver,
    const vitte_driver_config_t *config);

void
vitte_driver_destroy(vitte_driver_t *driver);

bool
vitte_driver_reset(vitte_driver_t *driver);

bool
vitte_driver_set_pipeline(
    vitte_driver_t *driver,
    const vitte_driver_pipeline_t *pipeline,
    void *user_data);

void
vitte_driver_set_diagnostic_callback(
    vitte_driver_t *driver,
    vitte_driver_diagnostic_fn callback,
    void *user_data);

void
vitte_driver_set_cancel_callback(
    vitte_driver_t *driver,
    vitte_driver_cancel_fn callback,
    void *user_data);

bool
vitte_driver_add_input(
    vitte_driver_t *driver,
    const char *name,
    const void *user_data,
    bool primary);

size_t
vitte_driver_input_count(const vitte_driver_t *driver);

const vitte_driver_input_t *
vitte_driver_input_at(
    const vitte_driver_t *driver,
    size_t index);

bool
vitte_driver_prepare(vitte_driver_t *driver);

bool
vitte_driver_run(vitte_driver_t *driver);

bool
vitte_driver_cancel(vitte_driver_t *driver);

bool
vitte_driver_account_output(
    vitte_driver_t *driver,
    size_t bytes);

bool
vitte_driver_emit_diagnostic(
    vitte_driver_t *driver,
    const vitte_driver_diagnostic_t *diagnostic);

uint64_t
vitte_driver_input_fingerprint(const vitte_driver_t *driver);

uint64_t
vitte_driver_configuration_fingerprint(const vitte_driver_t *driver);

vitte_driver_state_t
vitte_driver_state(const vitte_driver_t *driver);

vitte_driver_error_t
vitte_driver_last_error(const vitte_driver_t *driver);

vitte_driver_phase_t
vitte_driver_current_phase(const vitte_driver_t *driver);

vitte_driver_stats_t
vitte_driver_stats(const vitte_driver_t *driver);

uint64_t
vitte_driver_generation(const vitte_driver_t *driver);

/* ========================================================================= */
/* Checked size arithmetic                                                   */
/* ========================================================================= */

static bool
vitte_driver_size_add(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool
vitte_driver_size_mul(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

/* ========================================================================= */
/* Time                                                                      */
/* ========================================================================= */

static uint64_t
vitte_driver_now_nanoseconds(void)
{
#if defined(TIME_UTC)
    struct timespec value;
    uint64_t seconds;
    uint64_t nanoseconds;

    if (timespec_get(&value, TIME_UTC) != TIME_UTC) {
        return UINT64_C(0);
    }

    if (value.tv_sec < 0 || value.tv_nsec < 0) {
        return UINT64_C(0);
    }

    seconds = (uint64_t)value.tv_sec;
    nanoseconds = (uint64_t)value.tv_nsec;

    if (seconds > UINT64_MAX / UINT64_C(1000000000)) {
        return UINT64_MAX;
    }

    seconds *= UINT64_C(1000000000);

    if (seconds > UINT64_MAX - nanoseconds) {
        return UINT64_MAX;
    }

    return seconds + nanoseconds;
#else
    clock_t value = clock();
    uint64_t ticks;
    uint64_t frequency = (uint64_t)CLOCKS_PER_SEC;
    uint64_t seconds;
    uint64_t remainder;
    uint64_t fractional_nanoseconds;

    if (value == (clock_t)-1 || frequency == 0u) {
        return UINT64_C(0);
    }

    ticks = (uint64_t)value;
    seconds = ticks / frequency;
    remainder = ticks % frequency;
    if (seconds > UINT64_MAX / UINT64_C(1000000000)) {
        return UINT64_MAX;
    }

    fractional_nanoseconds = (uint64_t)(
        ((long double)remainder * 1000000000.0L) / (long double)frequency);
    return seconds * UINT64_C(1000000000) + fractional_nanoseconds;
#endif
}

static uint64_t
vitte_driver_elapsed(
    uint64_t begin,
    uint64_t end)
{
    if (end < begin) {
        return UINT64_C(0);
    }

    return end - begin;
}

static void
vitte_driver_u64_saturating_add(
    uint64_t *destination,
    uint64_t value)
{
    if (destination == NULL) {
        return;
    }

    if (*destination > UINT64_MAX - value) {
        *destination = UINT64_MAX;
        return;
    }

    *destination += value;
}

/* ========================================================================= */
/* Hash                                                                      */
/* ========================================================================= */

static uint64_t
vitte_driver_hash_bytes(
    uint64_t hash,
    const void *data,
    size_t length)
{
    const unsigned char *bytes;
    size_t index;

    if (data == NULL && length != 0u) {
        return hash;
    }

    bytes = (const unsigned char *)data;

    for (index = 0u; index < length; ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= VITTE_DRIVER_FNV_PRIME;
    }

    return hash;
}

static uint64_t
vitte_driver_hash_u64(
    uint64_t hash,
    uint64_t value)
{
    unsigned index;

    for (index = 0u; index < 8u; ++index) {
        unsigned char byte;

        byte =
            (unsigned char)(
                (value >> (index * 8u)) &
                UINT64_C(0xff));

        hash ^= (uint64_t)byte;
        hash *= VITTE_DRIVER_FNV_PRIME;
    }

    return hash;
}

static uint64_t
vitte_driver_hash_bool(
    uint64_t hash,
    bool value)
{
    return
        vitte_driver_hash_u64(
            hash,
            value ? UINT64_C(1) : UINT64_C(0));
}

static uint64_t
vitte_driver_hash_string(
    uint64_t hash,
    const char *value)
{
    size_t length;

    if (value == NULL) {
        return
            vitte_driver_hash_u64(
                hash,
                UINT64_C(0));
    }

    length = strlen(value);

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)length);

    return
        vitte_driver_hash_bytes(
            hash,
            value,
            length);
}

/* ========================================================================= */
/* Enum validity                                                             */
/* ========================================================================= */

static bool
vitte_driver_command_is_valid(
    vitte_driver_command_t command)
{
    return
        command > VITTE_DRIVER_COMMAND_INVALID &&
        command < VITTE_DRIVER_COMMAND_COUNT;
}

static bool
vitte_driver_backend_is_valid(
    vitte_driver_backend_t backend)
{
    return
        backend >= VITTE_DRIVER_BACKEND_DEFAULT &&
        backend < VITTE_DRIVER_BACKEND_COUNT;
}

static bool
vitte_driver_optimization_is_valid(
    vitte_driver_optimization_t optimization)
{
    return
        optimization >= VITTE_DRIVER_OPTIMIZATION_DEFAULT &&
        optimization < VITTE_DRIVER_OPTIMIZATION_COUNT;
}

static bool
vitte_driver_phase_is_valid(
    vitte_driver_phase_t phase)
{
    return
        phase >= VITTE_DRIVER_PHASE_NONE &&
        phase < VITTE_DRIVER_PHASE_COUNT;
}

static bool
vitte_driver_severity_is_valid(
    vitte_driver_severity_t severity)
{
    return
        severity > VITTE_DRIVER_SEVERITY_INVALID &&
        severity < VITTE_DRIVER_SEVERITY_COUNT;
}

/* ========================================================================= */
/* Names                                                                     */
/* ========================================================================= */

const char *
vitte_driver_error_name(vitte_driver_error_t error)
{
    switch (error) {
        case VITTE_DRIVER_ERROR_NONE:
            return "none";

        case VITTE_DRIVER_ERROR_INVALID_DRIVER:
            return "invalid-driver";

        case VITTE_DRIVER_ERROR_INVALID_ARGUMENT:
            return "invalid-argument";

        case VITTE_DRIVER_ERROR_INVALID_STATE:
            return "invalid-state";

        case VITTE_DRIVER_ERROR_INVALID_CONFIG:
            return "invalid-config";

        case VITTE_DRIVER_ERROR_NO_INPUT:
            return "no-input";

        case VITTE_DRIVER_ERROR_DUPLICATE_INPUT:
            return "duplicate-input";

        case VITTE_DRIVER_ERROR_TOO_MANY_INPUTS:
            return "too-many-inputs";

        case VITTE_DRIVER_ERROR_SOURCE:
            return "source";

        case VITTE_DRIVER_ERROR_LEXER:
            return "lexer";

        case VITTE_DRIVER_ERROR_PARSER:
            return "parser";

        case VITTE_DRIVER_ERROR_IMPORT:
            return "import";

        case VITTE_DRIVER_ERROR_NAME_RESOLUTION:
            return "name-resolution";

        case VITTE_DRIVER_ERROR_TYPE_CHECK:
            return "type-check";

        case VITTE_DRIVER_ERROR_CONTRACT:
            return "contract";

        case VITTE_DRIVER_ERROR_CONSTANT_FOLD:
            return "constant-fold";

        case VITTE_DRIVER_ERROR_HIR:
            return "hir";

        case VITTE_DRIVER_ERROR_IR:
            return "ir";

        case VITTE_DRIVER_ERROR_OPTIMIZATION:
            return "optimization";

        case VITTE_DRIVER_ERROR_CODEGEN:
            return "codegen";

        case VITTE_DRIVER_ERROR_BACKEND:
            return "backend";

        case VITTE_DRIVER_ERROR_TOOLCHAIN:
            return "toolchain";

        case VITTE_DRIVER_ERROR_LINK:
            return "link";

        case VITTE_DRIVER_ERROR_EXECUTION:
            return "execution";

        case VITTE_DRIVER_ERROR_TOO_MANY_ERRORS:
            return "too-many-errors";

        case VITTE_DRIVER_ERROR_OUTPUT_LIMIT:
            return "output-limit";

        case VITTE_DRIVER_ERROR_CANCELLED:
            return "cancelled";

        case VITTE_DRIVER_ERROR_OVERFLOW:
            return "overflow";

        case VITTE_DRIVER_ERROR_OUT_OF_MEMORY:
            return "out-of-memory";

        case VITTE_DRIVER_ERROR_UNSUPPORTED:
            return "unsupported";

        case VITTE_DRIVER_ERROR_INTERNAL:
            return "internal";

        case VITTE_DRIVER_ERROR_CORRUPTION:
            return "corruption";

        case VITTE_DRIVER_ERROR_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_driver_state_name(vitte_driver_state_t state)
{
    switch (state) {
        case VITTE_DRIVER_STATE_INVALID:
            return "invalid";

        case VITTE_DRIVER_STATE_READY:
            return "ready";

        case VITTE_DRIVER_STATE_PREPARING:
            return "preparing";

        case VITTE_DRIVER_STATE_PREPARED:
            return "prepared";

        case VITTE_DRIVER_STATE_RUNNING:
            return "running";

        case VITTE_DRIVER_STATE_FINISHED:
            return "finished";

        case VITTE_DRIVER_STATE_FAILED:
            return "failed";

        case VITTE_DRIVER_STATE_CANCELLED:
            return "cancelled";

        case VITTE_DRIVER_STATE_DESTROYED:
            return "destroyed";

        case VITTE_DRIVER_STATE_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_driver_command_name(vitte_driver_command_t command)
{
    switch (command) {
        case VITTE_DRIVER_COMMAND_INVALID:
            return "invalid";

        case VITTE_DRIVER_COMMAND_BUILD:
            return "build";

        case VITTE_DRIVER_COMMAND_CHECK:
            return "check";

        case VITTE_DRIVER_COMMAND_RUN:
            return "run";

        case VITTE_DRIVER_COMMAND_TEST:
            return "test";

        case VITTE_DRIVER_COMMAND_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_driver_phase_name(vitte_driver_phase_t phase)
{
    switch (phase) {
        case VITTE_DRIVER_PHASE_NONE:
            return "none";

        case VITTE_DRIVER_PHASE_PREPARE:
            return "prepare";

        case VITTE_DRIVER_PHASE_LOAD_SOURCE:
            return "load-source";

        case VITTE_DRIVER_PHASE_LEX:
            return "lexer";

        case VITTE_DRIVER_PHASE_PARSE:
            return "parser";

        case VITTE_DRIVER_PHASE_IMPORTS:
            return "imports";

        case VITTE_DRIVER_PHASE_NAMES:
            return "names";

        case VITTE_DRIVER_PHASE_TYPES:
            return "types";

        case VITTE_DRIVER_PHASE_CONTRACTS:
            return "contracts";

        case VITTE_DRIVER_PHASE_CONSTANT_FOLD:
            return "constant-fold";

        case VITTE_DRIVER_PHASE_HIR:
            return "hir";

        case VITTE_DRIVER_PHASE_IR:
            return "ir";

        case VITTE_DRIVER_PHASE_OPTIMIZE:
            return "optimize";

        case VITTE_DRIVER_PHASE_CODEGEN:
            return "codegen";

        case VITTE_DRIVER_PHASE_BACKEND:
            return "backend";

        case VITTE_DRIVER_PHASE_TOOLCHAIN:
            return "toolchain";

        case VITTE_DRIVER_PHASE_LINK:
            return "link";

        case VITTE_DRIVER_PHASE_EXECUTE:
            return "execute";

        case VITTE_DRIVER_PHASE_FINALIZE:
            return "finalize";

        case VITTE_DRIVER_PHASE_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_driver_severity_name(vitte_driver_severity_t severity)
{
    switch (severity) {
        case VITTE_DRIVER_SEVERITY_INVALID:
            return "invalid";

        case VITTE_DRIVER_SEVERITY_NOTE:
            return "note";

        case VITTE_DRIVER_SEVERITY_WARNING:
            return "warning";

        case VITTE_DRIVER_SEVERITY_ERROR:
            return "error";

        case VITTE_DRIVER_SEVERITY_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_driver_backend_name(vitte_driver_backend_t backend)
{
    switch (backend) {
        case VITTE_DRIVER_BACKEND_DEFAULT:
            return "default";

        case VITTE_DRIVER_BACKEND_C17:
            return "c17";

        case VITTE_DRIVER_BACKEND_NATIVE:
            return "native";

        case VITTE_DRIVER_BACKEND_BYTECODE:
            return "bytecode";

        case VITTE_DRIVER_BACKEND_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_driver_optimization_name(
    vitte_driver_optimization_t optimization)
{
    switch (optimization) {
        case VITTE_DRIVER_OPTIMIZATION_DEFAULT:
            return "default";

        case VITTE_DRIVER_OPTIMIZATION_NONE:
            return "none";

        case VITTE_DRIVER_OPTIMIZATION_DEBUG:
            return "debug";

        case VITTE_DRIVER_OPTIMIZATION_BALANCED:
            return "balanced";

        case VITTE_DRIVER_OPTIMIZATION_SPEED:
            return "speed";

        case VITTE_DRIVER_OPTIMIZATION_SIZE:
            return "size";

        case VITTE_DRIVER_OPTIMIZATION_AGGRESSIVE:
            return "aggressive";

        case VITTE_DRIVER_OPTIMIZATION_COUNT:
            return "count";
    }

    return "unknown";
}

/* ========================================================================= */
/* Default configuration                                                     */
/* ========================================================================= */

vitte_driver_config_t
vitte_driver_config_default(void)
{
    vitte_driver_config_t config;

    (void)memset(&config, 0, sizeof(config));

    config.command = VITTE_DRIVER_COMMAND_BUILD;

    config.backend = VITTE_DRIVER_BACKEND_DEFAULT;
    config.optimization = VITTE_DRIVER_OPTIMIZATION_DEFAULT;

    config.max_inputs = VITTE_DRIVER_DEFAULT_MAX_INPUTS;
    config.max_errors = VITTE_DRIVER_DEFAULT_MAX_ERRORS;
    config.max_warnings = VITTE_DRIVER_DEFAULT_MAX_WARNINGS;
    config.max_output_bytes = VITTE_DRIVER_DEFAULT_MAX_OUTPUT_BYTES;

    config.deterministic = true;

    config.warnings_as_errors = false;
    config.fail_fast = false;

    config.emit_debug_info = false;
    config.emit_source_map = true;
    config.emit_comments = false;

    config.runtime_checks = true;
    config.bounds_checks = true;
    config.null_checks = true;
    config.overflow_checks = true;

    config.incremental = true;

    config.stop_after_parse = false;
    config.stop_after_semantic = false;
    config.stop_after_ir = false;
    config.stop_after_codegen = false;

    config.execute_after_build = false;

    return config;
}

bool
vitte_driver_config_validate(const vitte_driver_config_t *config)
{
    unsigned stop_count;

    if (config == NULL) {
        return false;
    }

    if (!vitte_driver_command_is_valid(config->command)) {
        return false;
    }

    if (!vitte_driver_backend_is_valid(config->backend)) {
        return false;
    }

    if (!vitte_driver_optimization_is_valid(config->optimization)) {
        return false;
    }

    if (config->max_inputs == 0u) {
        return false;
    }

    if (config->max_errors == 0u) {
        return false;
    }

    stop_count = 0u;

    if (config->stop_after_parse) {
        ++stop_count;
    }

    if (config->stop_after_semantic) {
        ++stop_count;
    }

    if (config->stop_after_ir) {
        ++stop_count;
    }

    if (config->stop_after_codegen) {
        ++stop_count;
    }

    if (stop_count > 1u) {
        return false;
    }

    if (config->command == VITTE_DRIVER_COMMAND_CHECK &&
        config->execute_after_build) {
        return false;
    }

    if ((config->stop_after_parse ||
         config->stop_after_semantic ||
         config->stop_after_ir ||
         config->stop_after_codegen) &&
        config->execute_after_build) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Structural validation                                                     */
/* ========================================================================= */

bool
vitte_driver_is_valid(const vitte_driver_t *driver)
{
    if (driver == NULL) {
        return false;
    }

    if (driver->magic != VITTE_DRIVER_MAGIC) {
        return false;
    }

    if (driver->state <= VITTE_DRIVER_STATE_INVALID ||
        driver->state >= VITTE_DRIVER_STATE_DESTROYED) {
        return false;
    }

    if (driver->last_error >= VITTE_DRIVER_ERROR_COUNT) {
        return false;
    }

    if (!vitte_driver_phase_is_valid(driver->phase)) {
        return false;
    }

    if (!vitte_driver_config_validate(&driver->config)) {
        return false;
    }

    if (driver->input_count > driver->input_capacity) {
        return false;
    }

    if (driver->input_count > driver->config.max_inputs) {
        return false;
    }

    if (driver->input_capacity != 0u &&
        driver->inputs == NULL) {
        return false;
    }

    if (driver->next_input_id == UINT64_C(0)) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Failure                                                                   */
/* ========================================================================= */

static bool
vitte_driver_fail(
    vitte_driver_t *driver,
    vitte_driver_error_t error)
{
    if (driver == NULL) {
        return false;
    }

    driver->last_error = error;

    if (error == VITTE_DRIVER_ERROR_CANCELLED) {
        driver->state = VITTE_DRIVER_STATE_CANCELLED;
        driver->stats.cancelled = true;
    } else {
        driver->state = VITTE_DRIVER_STATE_FAILED;
        driver->stats.failed = true;
    }

    return false;
}

/* ========================================================================= */
/* Input storage                                                             */
/* ========================================================================= */

static bool
vitte_driver_reserve_inputs(
    vitte_driver_t *driver,
    size_t required)
{
    size_t capacity;
    size_t bytes;
    vitte_driver_input_t *new_inputs;

    if (driver == NULL) {
        return false;
    }

    if (required <= driver->input_capacity) {
        return true;
    }

    if (required > driver->config.max_inputs) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_TOO_MANY_INPUTS);
    }

    capacity = driver->input_capacity;

    if (capacity == 0u) {
        capacity = VITTE_DRIVER_DEFAULT_INITIAL_INPUT_CAPACITY;
    }

    while (capacity < required) {
        size_t next;

        if (!vitte_driver_size_add(capacity, capacity, &next)) {
            capacity = required;
            break;
        }

        if (next > driver->config.max_inputs) {
            capacity = driver->config.max_inputs;
            break;
        }

        capacity = next;
    }

    if (capacity < required) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_TOO_MANY_INPUTS);
    }

    if (!vitte_driver_size_mul(
            capacity,
            sizeof(*driver->inputs),
            &bytes)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_OVERFLOW);
    }

    new_inputs =
        (vitte_driver_input_t *)realloc(
            driver->inputs,
            bytes);

    if (new_inputs == NULL) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_OUT_OF_MEMORY);
    }

    if (capacity > driver->input_capacity) {
        size_t old_bytes;
        size_t extra_bytes;

        old_bytes =
            driver->input_capacity *
            sizeof(*driver->inputs);

        extra_bytes =
            bytes - old_bytes;

        (void)memset(
            (unsigned char *)new_inputs + old_bytes,
            0,
            extra_bytes);
    }

    driver->inputs = new_inputs;
    driver->input_capacity = capacity;

    return true;
}

static bool
vitte_driver_input_name_exists(
    const vitte_driver_t *driver,
    const char *name)
{
    size_t index;

    if (driver == NULL || name == NULL) {
        return false;
    }

    for (index = 0u; index < driver->input_count; ++index) {
        const char *existing;

        existing = driver->inputs[index].name;

        if (existing != NULL &&
            strcmp(existing, name) == 0) {
            return true;
        }
    }

    return false;
}

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

bool
vitte_driver_init(
    vitte_driver_t *driver,
    const vitte_driver_config_t *config)
{
    vitte_driver_config_t resolved;

    if (driver == NULL) {
        return false;
    }

    if (config == NULL) {
        resolved = vitte_driver_config_default();
    } else {
        resolved = *config;
    }

    if (!vitte_driver_config_validate(&resolved)) {
        (void)memset(driver, 0, sizeof(*driver));
        return false;
    }

    (void)memset(driver, 0, sizeof(*driver));

    driver->magic = VITTE_DRIVER_MAGIC;

    driver->state = VITTE_DRIVER_STATE_READY;
    driver->last_error = VITTE_DRIVER_ERROR_NONE;
    driver->phase = VITTE_DRIVER_PHASE_NONE;

    driver->config = resolved;

    driver->next_input_id = UINT64_C(1);
    driver->generation = UINT64_C(1);

    return true;
}

void
vitte_driver_destroy(vitte_driver_t *driver)
{
    if (driver == NULL) {
        return;
    }

    if (driver->magic != VITTE_DRIVER_MAGIC) {
        (void)memset(driver, 0, sizeof(*driver));
        driver->magic = VITTE_DRIVER_DEAD_MAGIC;
        driver->state = VITTE_DRIVER_STATE_DESTROYED;
        return;
    }

    if (driver->pipeline_installed &&
        driver->pipeline.destroy != NULL) {
        driver->pipeline.destroy(
            driver,
            driver->pipeline_user_data);
    }

    free(driver->inputs);

    (void)memset(driver, 0, sizeof(*driver));

    driver->magic = VITTE_DRIVER_DEAD_MAGIC;
    driver->state = VITTE_DRIVER_STATE_DESTROYED;
}

bool
vitte_driver_reset(vitte_driver_t *driver)
{
    vitte_driver_pipeline_t pipeline;
    void *pipeline_user_data;

    vitte_driver_diagnostic_fn diagnostic;
    void *diagnostic_user_data;

    vitte_driver_cancel_fn cancel;
    void *cancel_user_data;

    bool pipeline_installed;

    if (!vitte_driver_is_valid(driver)) {
        return false;
    }

    if (driver->state == VITTE_DRIVER_STATE_RUNNING ||
        driver->state == VITTE_DRIVER_STATE_PREPARING) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_STATE);
    }

    pipeline = driver->pipeline;
    pipeline_user_data = driver->pipeline_user_data;

    diagnostic = driver->diagnostic;
    diagnostic_user_data = driver->diagnostic_user_data;

    cancel = driver->cancel;
    cancel_user_data = driver->cancel_user_data;

    pipeline_installed = driver->pipeline_installed;

    driver->state = VITTE_DRIVER_STATE_READY;
    driver->last_error = VITTE_DRIVER_ERROR_NONE;
    driver->phase = VITTE_DRIVER_PHASE_NONE;

    driver->input_count = 0u;

    driver->pipeline = pipeline;
    driver->pipeline_user_data = pipeline_user_data;
    driver->pipeline_installed = pipeline_installed;

    driver->diagnostic = diagnostic;
    driver->diagnostic_user_data = diagnostic_user_data;

    driver->cancel = cancel;
    driver->cancel_user_data = cancel_user_data;

    (void)memset(&driver->stats, 0, sizeof(driver->stats));

    driver->next_input_id = UINT64_C(1);

    if (driver->generation != UINT64_MAX) {
        ++driver->generation;
    }

    driver->cancellation_requested = false;

    return true;
}

/* ========================================================================= */
/* Pipeline configuration                                                    */
/* ========================================================================= */

bool
vitte_driver_set_pipeline(
    vitte_driver_t *driver,
    const vitte_driver_pipeline_t *pipeline,
    void *user_data)
{
    if (!vitte_driver_is_valid(driver)) {
        return false;
    }

    if (driver->state != VITTE_DRIVER_STATE_READY) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_STATE);
    }

    if (pipeline == NULL) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_ARGUMENT);
    }

    driver->pipeline = *pipeline;
    driver->pipeline_user_data = user_data;
    driver->pipeline_installed = true;

    return true;
}

void
vitte_driver_set_diagnostic_callback(
    vitte_driver_t *driver,
    vitte_driver_diagnostic_fn callback,
    void *user_data)
{
    if (!vitte_driver_is_valid(driver)) {
        return;
    }

    if (driver->state == VITTE_DRIVER_STATE_RUNNING ||
        driver->state == VITTE_DRIVER_STATE_PREPARING) {
        return;
    }

    driver->diagnostic = callback;
    driver->diagnostic_user_data = user_data;
}

void
vitte_driver_set_cancel_callback(
    vitte_driver_t *driver,
    vitte_driver_cancel_fn callback,
    void *user_data)
{
    if (!vitte_driver_is_valid(driver)) {
        return;
    }

    if (driver->state == VITTE_DRIVER_STATE_RUNNING ||
        driver->state == VITTE_DRIVER_STATE_PREPARING) {
        return;
    }

    driver->cancel = callback;
    driver->cancel_user_data = user_data;
}

/* ========================================================================= */
/* Input API                                                                 */
/* ========================================================================= */

bool
vitte_driver_add_input(
    vitte_driver_t *driver,
    const char *name,
    const void *user_data,
    bool primary)
{
    size_t required;
    vitte_driver_input_t *input;

    if (!vitte_driver_is_valid(driver)) {
        return false;
    }

    if (driver->state != VITTE_DRIVER_STATE_READY) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_STATE);
    }

    if (name == NULL || name[0] == '\0') {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_ARGUMENT);
    }

    if (vitte_driver_input_name_exists(driver, name)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_DUPLICATE_INPUT);
    }

    if (!vitte_driver_size_add(
            driver->input_count,
            1u,
            &required)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_OVERFLOW);
    }

    if (!vitte_driver_reserve_inputs(driver, required)) {
        return false;
    }

    if (driver->next_input_id == UINT64_MAX) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_OVERFLOW);
    }

    input = &driver->inputs[driver->input_count];

    (void)memset(input, 0, sizeof(*input));

    input->id = driver->next_input_id;
    input->name = name;
    input->user_data = user_data;
    input->primary = primary;

    ++driver->next_input_id;
    ++driver->input_count;

    driver->stats.input_count = driver->input_count;

    return true;
}

size_t
vitte_driver_input_count(const vitte_driver_t *driver)
{
    if (!vitte_driver_is_valid(driver)) {
        return 0u;
    }

    return driver->input_count;
}

const vitte_driver_input_t *
vitte_driver_input_at(
    const vitte_driver_t *driver,
    size_t index)
{
    if (!vitte_driver_is_valid(driver)) {
        return NULL;
    }

    if (index >= driver->input_count) {
        return NULL;
    }

    return &driver->inputs[index];
}

/* ========================================================================= */
/* Fingerprints                                                              */
/* ========================================================================= */

uint64_t
vitte_driver_input_fingerprint(const vitte_driver_t *driver)
{
    uint64_t hash;
    size_t index;

    if (!vitte_driver_is_valid(driver)) {
        return UINT64_C(0);
    }

    hash = VITTE_DRIVER_FNV_OFFSET;

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)driver->input_count);

    for (index = 0u; index < driver->input_count; ++index) {
        const vitte_driver_input_t *input;

        input = &driver->inputs[index];

        hash =
            vitte_driver_hash_u64(
                hash,
                input->id);

        hash =
            vitte_driver_hash_string(
                hash,
                input->name);

        hash =
            vitte_driver_hash_bool(
                hash,
                input->primary);

        /*
         * user_data pointer intentionally excluded.
         *
         * Pointer addresses are process-specific and would make deterministic
         * fingerprints meaningless.
         */
    }

    return hash;
}

uint64_t
vitte_driver_configuration_fingerprint(
    const vitte_driver_t *driver)
{
    uint64_t hash;
    const vitte_driver_config_t *config;

    if (!vitte_driver_is_valid(driver)) {
        return UINT64_C(0);
    }

    config = &driver->config;

    hash = VITTE_DRIVER_FNV_OFFSET;

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)config->command);

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)config->backend);

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)config->optimization);

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)config->max_inputs);

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)config->max_errors);

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)config->max_warnings);

    hash =
        vitte_driver_hash_u64(
            hash,
            (uint64_t)config->max_output_bytes);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->deterministic);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->warnings_as_errors);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->fail_fast);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->emit_debug_info);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->emit_source_map);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->emit_comments);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->runtime_checks);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->bounds_checks);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->null_checks);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->overflow_checks);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->incremental);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->stop_after_parse);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->stop_after_semantic);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->stop_after_ir);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->stop_after_codegen);

    hash =
        vitte_driver_hash_bool(
            hash,
            config->execute_after_build);

    return hash;
}

/* ========================================================================= */
/* Cancellation                                                              */
/* ========================================================================= */

static bool
vitte_driver_is_cancelled(vitte_driver_t *driver)
{
    if (driver == NULL) {
        return true;
    }

    if (driver->cancellation_requested) {
        return true;
    }

    if (driver->cancel != NULL &&
        driver->cancel(
            driver,
            driver->cancel_user_data)) {
        driver->cancellation_requested = true;
        return true;
    }

    return false;
}

bool
vitte_driver_cancel(vitte_driver_t *driver)
{
    if (!vitte_driver_is_valid(driver)) {
        return false;
    }

    driver->cancellation_requested = true;

    if (driver->state == VITTE_DRIVER_STATE_RUNNING ||
        driver->state == VITTE_DRIVER_STATE_PREPARING) {
        return true;
    }

    driver->last_error = VITTE_DRIVER_ERROR_CANCELLED;
    driver->state = VITTE_DRIVER_STATE_CANCELLED;
    driver->stats.cancelled = true;

    return true;
}

/* ========================================================================= */
/* Diagnostics                                                               */
/* ========================================================================= */

bool
vitte_driver_emit_diagnostic(
    vitte_driver_t *driver,
    const vitte_driver_diagnostic_t *diagnostic)
{
    vitte_driver_severity_t effective;

    if (!vitte_driver_is_valid(driver)) {
        return false;
    }

    if (diagnostic == NULL ||
        !vitte_driver_severity_is_valid(diagnostic->severity) ||
        !vitte_driver_phase_is_valid(diagnostic->phase)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_ARGUMENT);
    }

    effective = diagnostic->severity;

    if (effective == VITTE_DRIVER_SEVERITY_WARNING &&
        driver->config.warnings_as_errors) {
        effective = VITTE_DRIVER_SEVERITY_ERROR;
    }

    switch (effective) {
        case VITTE_DRIVER_SEVERITY_NOTE:
            if (driver->stats.note_count != SIZE_MAX) {
                ++driver->stats.note_count;
            }
            break;

        case VITTE_DRIVER_SEVERITY_WARNING:
            if (driver->stats.warning_count != SIZE_MAX) {
                ++driver->stats.warning_count;
            }
            break;

        case VITTE_DRIVER_SEVERITY_ERROR:
            if (driver->stats.error_count != SIZE_MAX) {
                ++driver->stats.error_count;
            }
            break;

        case VITTE_DRIVER_SEVERITY_INVALID:
        case VITTE_DRIVER_SEVERITY_COUNT:
            return
                vitte_driver_fail(
                    driver,
                    VITTE_DRIVER_ERROR_INVALID_ARGUMENT);
    }

    if (driver->diagnostic != NULL) {
        /*
         * Preserve the original diagnostic object.
         *
         * warnings_as_errors is driver policy used for accounting and stopping
         * decisions. The renderer may independently choose how to communicate
         * that policy.
         */
        driver->diagnostic(
            driver,
            diagnostic,
            driver->diagnostic_user_data);
    }

    if (effective == VITTE_DRIVER_SEVERITY_WARNING &&
        driver->config.max_warnings != 0u &&
        driver->stats.warning_count >= driver->config.max_warnings) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_TOO_MANY_ERRORS);
    }

    if (effective == VITTE_DRIVER_SEVERITY_ERROR) {
        if (driver->config.fail_fast) {
            return
                vitte_driver_fail(
                    driver,
                    VITTE_DRIVER_ERROR_TOO_MANY_ERRORS);
        }

        if (driver->stats.error_count >= driver->config.max_errors) {
            return
                vitte_driver_fail(
                    driver,
                    VITTE_DRIVER_ERROR_TOO_MANY_ERRORS);
        }
    }

    return true;
}

/* ========================================================================= */
/* Output accounting                                                         */
/* ========================================================================= */

bool
vitte_driver_account_output(
    vitte_driver_t *driver,
    size_t bytes)
{
    size_t total;

    if (!vitte_driver_is_valid(driver)) {
        return false;
    }

    if (!vitte_driver_size_add(
            driver->stats.output_bytes,
            bytes,
            &total)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_OVERFLOW);
    }

    if (driver->config.max_output_bytes != 0u &&
        total > driver->config.max_output_bytes) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_OUTPUT_LIMIT);
    }

    driver->stats.output_bytes = total;

    return true;
}

/* ========================================================================= */
/* Phase error mapping                                                       */
/* ========================================================================= */

static vitte_driver_error_t
vitte_driver_phase_error(vitte_driver_phase_t phase)
{
    switch (phase) {
        case VITTE_DRIVER_PHASE_LOAD_SOURCE:
            return VITTE_DRIVER_ERROR_SOURCE;

        case VITTE_DRIVER_PHASE_LEX:
            return VITTE_DRIVER_ERROR_LEXER;

        case VITTE_DRIVER_PHASE_PARSE:
            return VITTE_DRIVER_ERROR_PARSER;

        case VITTE_DRIVER_PHASE_IMPORTS:
            return VITTE_DRIVER_ERROR_IMPORT;

        case VITTE_DRIVER_PHASE_NAMES:
            return VITTE_DRIVER_ERROR_NAME_RESOLUTION;

        case VITTE_DRIVER_PHASE_TYPES:
            return VITTE_DRIVER_ERROR_TYPE_CHECK;

        case VITTE_DRIVER_PHASE_CONTRACTS:
            return VITTE_DRIVER_ERROR_CONTRACT;

        case VITTE_DRIVER_PHASE_CONSTANT_FOLD:
            return VITTE_DRIVER_ERROR_CONSTANT_FOLD;

        case VITTE_DRIVER_PHASE_HIR:
            return VITTE_DRIVER_ERROR_HIR;

        case VITTE_DRIVER_PHASE_IR:
            return VITTE_DRIVER_ERROR_IR;

        case VITTE_DRIVER_PHASE_OPTIMIZE:
            return VITTE_DRIVER_ERROR_OPTIMIZATION;

        case VITTE_DRIVER_PHASE_CODEGEN:
            return VITTE_DRIVER_ERROR_CODEGEN;

        case VITTE_DRIVER_PHASE_BACKEND:
            return VITTE_DRIVER_ERROR_BACKEND;

        case VITTE_DRIVER_PHASE_TOOLCHAIN:
            return VITTE_DRIVER_ERROR_TOOLCHAIN;

        case VITTE_DRIVER_PHASE_LINK:
            return VITTE_DRIVER_ERROR_LINK;

        case VITTE_DRIVER_PHASE_EXECUTE:
            return VITTE_DRIVER_ERROR_EXECUTION;

        case VITTE_DRIVER_PHASE_PREPARE:
        case VITTE_DRIVER_PHASE_FINALIZE:
            return VITTE_DRIVER_ERROR_INTERNAL;

        case VITTE_DRIVER_PHASE_NONE:
        case VITTE_DRIVER_PHASE_COUNT:
            return VITTE_DRIVER_ERROR_INTERNAL;
    }

    return VITTE_DRIVER_ERROR_INTERNAL;
}

/* ========================================================================= */
/* Phase accounting                                                          */
/* ========================================================================= */

static void
vitte_driver_phase_begin(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase)
{
    vitte_driver_phase_stats_t *stats;

    if (driver == NULL ||
        phase <= VITTE_DRIVER_PHASE_NONE ||
        phase >= VITTE_DRIVER_PHASE_COUNT) {
        return;
    }

    driver->phase = phase;

    stats = &driver->stats.phases[phase];

    if (stats->invocations != UINT64_MAX) {
        ++stats->invocations;
    }

    if (driver->stats.phase_count != UINT64_MAX) {
        ++driver->stats.phase_count;
    }
}

static void
vitte_driver_phase_end(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    bool success,
    uint64_t begin,
    uint64_t end)
{
    vitte_driver_phase_stats_t *stats;
    uint64_t elapsed;

    if (driver == NULL ||
        phase <= VITTE_DRIVER_PHASE_NONE ||
        phase >= VITTE_DRIVER_PHASE_COUNT) {
        return;
    }

    stats = &driver->stats.phases[phase];

    if (success) {
        if (stats->successes != UINT64_MAX) {
            ++stats->successes;
        }
    } else {
        if (stats->failures != UINT64_MAX) {
            ++stats->failures;
        }
    }

    elapsed = vitte_driver_elapsed(begin, end);

    vitte_driver_u64_saturating_add(
        &stats->elapsed_nanoseconds,
        elapsed);
}

/* ========================================================================= */
/* Phase execution helpers                                                   */
/* ========================================================================= */

static bool
vitte_driver_run_global_phase(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    vitte_driver_phase_fn callback)
{
    uint64_t begin;
    uint64_t end;
    bool success;

    if (callback == NULL) {
        return true;
    }

    if (vitte_driver_is_cancelled(driver)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_CANCELLED);
    }

    vitte_driver_phase_begin(driver, phase);

    begin = vitte_driver_now_nanoseconds();

    success =
        callback(
            driver,
            phase,
            driver->pipeline_user_data);

    end = vitte_driver_now_nanoseconds();

    vitte_driver_phase_end(
        driver,
        phase,
        success,
        begin,
        end);

    if (!success) {
        if (driver->state == VITTE_DRIVER_STATE_FAILED ||
            driver->state == VITTE_DRIVER_STATE_CANCELLED) {
            return false;
        }

        return
            vitte_driver_fail(
                driver,
                vitte_driver_phase_error(phase));
    }

    if (vitte_driver_is_cancelled(driver)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_CANCELLED);
    }

    return true;
}

static bool
vitte_driver_run_input_phase(
    vitte_driver_t *driver,
    vitte_driver_phase_t phase,
    vitte_driver_input_phase_fn callback)
{
    uint64_t begin;
    uint64_t end;
    bool success;
    size_t index;

    if (callback == NULL) {
        return true;
    }

    if (vitte_driver_is_cancelled(driver)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_CANCELLED);
    }

    vitte_driver_phase_begin(driver, phase);

    begin = vitte_driver_now_nanoseconds();
    success = true;

    for (index = 0u; index < driver->input_count; ++index) {
        if (vitte_driver_is_cancelled(driver)) {
            success = false;

            (void)vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_CANCELLED);

            break;
        }

        if (!callback(
                driver,
                phase,
                &driver->inputs[index],
                driver->pipeline_user_data)) {
            success = false;

            if (driver->state != VITTE_DRIVER_STATE_FAILED &&
                driver->state != VITTE_DRIVER_STATE_CANCELLED) {
                (void)vitte_driver_fail(
                    driver,
                    vitte_driver_phase_error(phase));
            }

            break;
        }

        if (phase == VITTE_DRIVER_PHASE_PARSE &&
            driver->stats.processed_input_count != SIZE_MAX) {
            ++driver->stats.processed_input_count;
        }
    }

    end = vitte_driver_now_nanoseconds();

    vitte_driver_phase_end(
        driver,
        phase,
        success,
        begin,
        end);

    return success;
}

/* ========================================================================= */
/* Prepare                                                                   */
/* ========================================================================= */

bool
vitte_driver_prepare(vitte_driver_t *driver)
{
    uint64_t begin;
    uint64_t end;
    bool success;

    if (!vitte_driver_is_valid(driver)) {
        return false;
    }

    if (driver->state == VITTE_DRIVER_STATE_PREPARED) {
        return true;
    }

    if (driver->state != VITTE_DRIVER_STATE_READY) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_STATE);
    }

    if (driver->input_count == 0u) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_NO_INPUT);
    }

    if (!driver->pipeline_installed) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_CONFIG);
    }

    if (vitte_driver_is_cancelled(driver)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_CANCELLED);
    }

    driver->state = VITTE_DRIVER_STATE_PREPARING;

    vitte_driver_phase_begin(
        driver,
        VITTE_DRIVER_PHASE_PREPARE);

    begin = vitte_driver_now_nanoseconds();

    success = true;

    if (driver->pipeline.prepare != NULL) {
        success =
            driver->pipeline.prepare(
                driver,
                driver->pipeline_user_data);
    }

    end = vitte_driver_now_nanoseconds();

    vitte_driver_phase_end(
        driver,
        VITTE_DRIVER_PHASE_PREPARE,
        success,
        begin,
        end);

    if (!success) {
        if (driver->state != VITTE_DRIVER_STATE_FAILED &&
            driver->state != VITTE_DRIVER_STATE_CANCELLED) {
            return
                vitte_driver_fail(
                    driver,
                    VITTE_DRIVER_ERROR_INTERNAL);
        }

        return false;
    }

    if (vitte_driver_is_cancelled(driver)) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_CANCELLED);
    }

    driver->stats.input_fingerprint =
        vitte_driver_input_fingerprint(driver);

    driver->stats.configuration_fingerprint =
        vitte_driver_configuration_fingerprint(driver);

    driver->stats.prepared = true;

    driver->phase = VITTE_DRIVER_PHASE_NONE;
    driver->state = VITTE_DRIVER_STATE_PREPARED;

    return true;
}

/* ========================================================================= */
/* Finalization                                                              */
/* ========================================================================= */

static bool
vitte_driver_finalize_pipeline(
    vitte_driver_t *driver,
    bool success)
{
    uint64_t begin;
    uint64_t end;
    bool callback_success;

    if (driver == NULL ||
        driver->pipeline.finalize == NULL) {
        return success;
    }

    vitte_driver_phase_begin(
        driver,
        VITTE_DRIVER_PHASE_FINALIZE);

    begin = vitte_driver_now_nanoseconds();

    callback_success =
        driver->pipeline.finalize(
            driver,
            success,
            driver->pipeline_user_data);

    end = vitte_driver_now_nanoseconds();

    vitte_driver_phase_end(
        driver,
        VITTE_DRIVER_PHASE_FINALIZE,
        callback_success,
        begin,
        end);

    if (!callback_success) {
        if (driver->state != VITTE_DRIVER_STATE_FAILED &&
            driver->state != VITTE_DRIVER_STATE_CANCELLED) {
            (void)vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INTERNAL);
        }

        return false;
    }

    return success;
}

/* ========================================================================= */
/* Main pipeline                                                             */
/* ========================================================================= */

bool
vitte_driver_run(vitte_driver_t *driver)
{
    uint64_t begin;
    uint64_t end;
    bool success;
    bool should_execute;

    if (!vitte_driver_is_valid(driver)) {
        return false;
    }

    if (driver->state == VITTE_DRIVER_STATE_READY) {
        if (!vitte_driver_prepare(driver)) {
            return false;
        }
    }

    if (driver->state != VITTE_DRIVER_STATE_PREPARED) {
        return
            vitte_driver_fail(
                driver,
                VITTE_DRIVER_ERROR_INVALID_STATE);
    }

    begin = vitte_driver_now_nanoseconds();

    driver->state = VITTE_DRIVER_STATE_RUNNING;
    driver->last_error = VITTE_DRIVER_ERROR_NONE;

    success = true;

    /* --------------------------------------------------------------------- */
    /* Source                                                                */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_input_phase(
            driver,
            VITTE_DRIVER_PHASE_LOAD_SOURCE,
            driver->pipeline.load_source)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Lexer                                                                 */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_input_phase(
            driver,
            VITTE_DRIVER_PHASE_LEX,
            driver->pipeline.lex)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Parser                                                                */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_input_phase(
            driver,
            VITTE_DRIVER_PHASE_PARSE,
            driver->pipeline.parse)) {
        success = false;
        goto finalize;
    }

    if (driver->config.stop_after_parse) {
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Imports                                                               */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_IMPORTS,
            driver->pipeline.resolve_imports)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Name resolution                                                       */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_NAMES,
            driver->pipeline.resolve_names)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Type checking                                                         */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_TYPES,
            driver->pipeline.check_types)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Contracts                                                             */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_CONTRACTS,
            driver->pipeline.check_contracts)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Constant folding                                                      */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_CONSTANT_FOLD,
            driver->pipeline.fold_constants)) {
        success = false;
        goto finalize;
    }

    if (driver->config.stop_after_semantic ||
        driver->config.command == VITTE_DRIVER_COMMAND_CHECK) {
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* HIR                                                                   */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_HIR,
            driver->pipeline.lower_hir)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* IR                                                                    */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_IR,
            driver->pipeline.lower_ir)) {
        success = false;
        goto finalize;
    }

    if (driver->config.stop_after_ir) {
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Optimization                                                          */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_OPTIMIZE,
            driver->pipeline.optimize)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Codegen                                                               */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_CODEGEN,
            driver->pipeline.codegen)) {
        success = false;
        goto finalize;
    }

    if (driver->config.stop_after_codegen) {
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Backend                                                               */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_BACKEND,
            driver->pipeline.backend)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* External/native toolchain                                             */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_TOOLCHAIN,
            driver->pipeline.toolchain)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Link                                                                  */
    /* --------------------------------------------------------------------- */

    if (!vitte_driver_run_global_phase(
            driver,
            VITTE_DRIVER_PHASE_LINK,
            driver->pipeline.link)) {
        success = false;
        goto finalize;
    }

    /* --------------------------------------------------------------------- */
    /* Execute                                                               */
    /* --------------------------------------------------------------------- */

    should_execute =
        driver->config.command == VITTE_DRIVER_COMMAND_RUN ||
        driver->config.command == VITTE_DRIVER_COMMAND_TEST ||
        driver->config.execute_after_build;

    if (should_execute) {
        if (!vitte_driver_run_global_phase(
                driver,
                VITTE_DRIVER_PHASE_EXECUTE,
                driver->pipeline.execute)) {
            success = false;
            goto finalize;
        }
    }

finalize:

    /*
     * If a phase reported compiler diagnostics but did not itself return
     * failure, error diagnostics still make the compilation unsuccessful.
     */
    if (driver->stats.error_count != 0u) {
        success = false;
    }

    if (!vitte_driver_finalize_pipeline(driver, success)) {
        success = false;
    }

    end = vitte_driver_now_nanoseconds();

    driver->stats.elapsed_nanoseconds =
        vitte_driver_elapsed(begin, end);

    driver->phase = VITTE_DRIVER_PHASE_NONE;

    if (driver->cancellation_requested ||
        driver->state == VITTE_DRIVER_STATE_CANCELLED) {
        driver->state = VITTE_DRIVER_STATE_CANCELLED;
        driver->last_error = VITTE_DRIVER_ERROR_CANCELLED;
        driver->stats.cancelled = true;
        driver->stats.failed = false;
        driver->stats.finished = false;

        return false;
    }

    if (!success) {
        if (driver->state != VITTE_DRIVER_STATE_FAILED) {
            driver->state = VITTE_DRIVER_STATE_FAILED;

            if (driver->last_error == VITTE_DRIVER_ERROR_NONE) {
                driver->last_error = VITTE_DRIVER_ERROR_INTERNAL;
            }
        }

        driver->stats.failed = true;
        driver->stats.finished = false;

        return false;
    }

    driver->state = VITTE_DRIVER_STATE_FINISHED;
    driver->last_error = VITTE_DRIVER_ERROR_NONE;

    driver->stats.finished = true;
    driver->stats.failed = false;
    driver->stats.cancelled = false;

    return true;
}

/* ========================================================================= */
/* Queries                                                                   */
/* ========================================================================= */

vitte_driver_state_t
vitte_driver_state(const vitte_driver_t *driver)
{
    if (driver == NULL ||
        driver->magic != VITTE_DRIVER_MAGIC) {
        return VITTE_DRIVER_STATE_INVALID;
    }

    return driver->state;
}

vitte_driver_error_t
vitte_driver_last_error(const vitte_driver_t *driver)
{
    if (driver == NULL ||
        driver->magic != VITTE_DRIVER_MAGIC) {
        return VITTE_DRIVER_ERROR_INVALID_DRIVER;
    }

    return driver->last_error;
}

vitte_driver_phase_t
vitte_driver_current_phase(const vitte_driver_t *driver)
{
    if (driver == NULL ||
        driver->magic != VITTE_DRIVER_MAGIC) {
        return VITTE_DRIVER_PHASE_NONE;
    }

    return driver->phase;
}

vitte_driver_stats_t
vitte_driver_stats(const vitte_driver_t *driver)
{
    vitte_driver_stats_t stats;

    (void)memset(&stats, 0, sizeof(stats));

    if (!vitte_driver_is_valid(driver)) {
        return stats;
    }

    stats = driver->stats;
    stats.input_count = driver->input_count;

    stats.prepared =
        driver->state == VITTE_DRIVER_STATE_PREPARED ||
        driver->state == VITTE_DRIVER_STATE_RUNNING ||
        driver->state == VITTE_DRIVER_STATE_FINISHED;

    stats.finished =
        driver->state == VITTE_DRIVER_STATE_FINISHED;

    stats.failed =
        driver->state == VITTE_DRIVER_STATE_FAILED;

    stats.cancelled =
        driver->state == VITTE_DRIVER_STATE_CANCELLED;

    return stats;
}

uint64_t
vitte_driver_generation(const vitte_driver_t *driver)
{
    if (!vitte_driver_is_valid(driver)) {
        return UINT64_C(0);
    }

    return driver->generation;
}

/* ========================================================================= */
/* Compile-time invariants                                                   */
/* ========================================================================= */

_Static_assert(
    VITTE_DRIVER_PHASE_COUNT > VITTE_DRIVER_PHASE_FINALIZE,
    "driver phase enum invariant");

_Static_assert(
    VITTE_DRIVER_ERROR_COUNT > VITTE_DRIVER_ERROR_INTERNAL,
    "driver error enum invariant");

_Static_assert(
    VITTE_DRIVER_STATE_COUNT > VITTE_DRIVER_STATE_DESTROYED,
    "driver state enum invariant");

_Static_assert(
    VITTE_DRIVER_DEFAULT_MAX_INPUTS > 0u,
    "driver must permit at least one input");

_Static_assert(
    VITTE_DRIVER_DEFAULT_MAX_ERRORS > 0u,
    "driver must permit at least one error");
