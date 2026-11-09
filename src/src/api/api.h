#ifndef VITTE_SRC_API_API_H
#define VITTE_SRC_API_API_H

/*
 * Vitte Compiler
 * src/api/api.h
 *
 * Stable public compiler API.
 *
 * This header defines the high-level embedding interface for applications
 * using Vitte as a compiler library.
 *
 * Design goals:
 *
 *   - opaque compiler objects
 *   - explicit ownership
 *   - deterministic lifecycle
 *   - stable ABI surface
 *   - defensive error handling
 *   - source and file compilation
 *   - diagnostic access
 *   - compilation statistics
 *   - build/version introspection
 *
 * Internal compiler structures are deliberately not exposed here.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Symbol visibility                                                         */
/* ========================================================================= */

#ifndef VITTE_API
# if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(VITTE_BUILD_SHARED)
#   define VITTE_API __declspec(dllexport)
#  elif defined(VITTE_USE_SHARED)
#   define VITTE_API __declspec(dllimport)
#  else
#   define VITTE_API
#  endif
# elif defined(__GNUC__) || defined(__clang__)
#  if defined(VITTE_BUILD_SHARED)
#   define VITTE_API __attribute__((visibility("default")))
#  else
#   define VITTE_API
#  endif
# else
#  define VITTE_API
# endif
#endif

/* ========================================================================= */
/* Attributes                                                                */
/* ========================================================================= */

#if defined(__GNUC__) || defined(__clang__)
# define VITTE_API_NODISCARD __attribute__((warn_unused_result))
# define VITTE_API_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
# define VITTE_API_PURE __attribute__((pure))
# define VITTE_API_CONST __attribute__((const))
#else
# define VITTE_API_NODISCARD
# define VITTE_API_NONNULL(...)
# define VITTE_API_PURE
# define VITTE_API_CONST
#endif

/* ========================================================================= */
/* Opaque handles                                                            */
/* ========================================================================= */

/*
 * Compiler API context.
 *
 * Owns reusable compiler infrastructure and per-context error state.
 *
 * Create with:
 *
 *     vitte_api_create()
 *
 * Destroy with:
 *
 *     vitte_api_destroy()
 *
 * A context must not be destroyed while a compilation using that context is
 * active.
 */
typedef struct vitte_api vitte_api_t;

/*
 * Compilation result.
 *
 * Results are owned independently from the API context once returned.
 *
 * Destroy with:
 *
 *     vitte_api_result_destroy()
 *
 * Pointers returned by result accessors remain owned by the result and become
 * invalid when the result is destroyed.
 */
typedef struct vitte_api_result vitte_api_result_t;

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

/*
 * Create a compiler API context.
 *
 * On success:
 *
 *     *out_api != NULL
 *     return VITTE_STATUS_OK
 *
 * On failure:
 *
 *     *out_api == NULL
 *
 * The returned context owns all reusable compiler infrastructure associated
 * with this API instance.
 */
VITTE_API
VITTE_API_NODISCARD
vitte_status_t
vitte_api_create(
    vitte_api_t **out_api);

/*
 * Destroy an API context.
 *
 * Passing NULL is allowed.
 *
 * All API-context-owned compiler state is released.
 *
 * Compilation results previously returned to the caller remain separately
 * owned and must still be destroyed with vitte_api_result_destroy().
 */
VITTE_API
void
vitte_api_destroy(
    vitte_api_t *api);

/* ========================================================================= */
/* Compilation                                                               */
/* ========================================================================= */

/*
 * Compile an explicitly sized source buffer.
 *
 * source_name:
 *     Logical source name used by diagnostics.
 *
 * source:
 *     Source bytes.
 *
 * source_length:
 *     Number of source bytes. The source does not need to be NUL terminated.
 *
 * out_result:
 *     Receives a compilation result when non-NULL.
 *
 * A result may be returned even when compilation fails. This allows callers
 * to inspect diagnostics associated with failed compilations.
 *
 * If out_result is NULL, any internally produced result is destroyed before
 * the function returns.
 */
VITTE_API
VITTE_API_NODISCARD
vitte_status_t
vitte_api_compile_source(
    vitte_api_t *api,
    const char *source_name,
    const char *source,
    size_t source_length,
    vitte_api_result_t **out_result);

/*
 * Compile a NUL-terminated in-memory source string.
 *
 * The logical source name is "<memory>".
 */
VITTE_API
VITTE_API_NODISCARD
vitte_status_t
vitte_api_compile_string(
    vitte_api_t *api,
    const char *source,
    vitte_api_result_t **out_result);

/*
 * Compile a source file.
 *
 * The API loads the complete file into memory and forwards it to the compiler
 * pipeline.
 */
VITTE_API
VITTE_API_NODISCARD
vitte_status_t
vitte_api_compile_file(
    vitte_api_t *api,
    const char *path,
    vitte_api_result_t **out_result);

/* ========================================================================= */
/* Result lifecycle                                                          */
/* ========================================================================= */

/*
 * Destroy a compilation result.
 *
 * Passing NULL is allowed.
 *
 * This invalidates every pointer returned by result accessors.
 */
VITTE_API
void
vitte_api_result_destroy(
    vitte_api_result_t *result);

/* ========================================================================= */
/* Result status                                                             */
/* ========================================================================= */

/*
 * Return the status associated with a compilation result.
 *
 * Invalid results return VITTE_STATUS_INVALID_ARGUMENT.
 */
VITTE_API
VITTE_API_NODISCARD
vitte_status_t
vitte_api_result_status(
    const vitte_api_result_t *result);

/*
 * Return true only when compilation completed successfully and produced no
 * error diagnostics.
 */
VITTE_API
VITTE_API_NODISCARD
bool
vitte_api_result_succeeded(
    const vitte_api_result_t *result);

/* ========================================================================= */
/* Result source                                                             */
/* ========================================================================= */

/*
 * Return the logical source name associated with the result.
 *
 * The returned pointer is owned by the result.
 */
VITTE_API
VITTE_API_NODISCARD
const char *
vitte_api_result_source_name(
    const vitte_api_result_t *result);

/* ========================================================================= */
/* Result output                                                             */
/* ========================================================================= */

/*
 * Return generated compiler output.
 *
 * The exact representation depends on the selected compilation/backend mode.
 *
 * Examples may include:
 *
 *   - generated C17
 *   - object bytes
 *   - executable bytes
 *   - intermediate representation
 *
 * The returned pointer is owned by the result.
 *
 * out_size may be NULL.
 */
VITTE_API
VITTE_API_NODISCARD
const void *
vitte_api_result_output(
    const vitte_api_result_t *result,
    size_t *out_size);

/* ========================================================================= */
/* Diagnostics                                                               */
/* ========================================================================= */

/*
 * Return the rendered diagnostic stream.
 *
 * The returned string is owned by the result and is NUL terminated.
 *
 * out_size receives the diagnostic byte length excluding the terminating NUL.
 * out_size may be NULL.
 */
VITTE_API
VITTE_API_NODISCARD
const char *
vitte_api_result_diagnostics(
    const vitte_api_result_t *result,
    size_t *out_size);

/*
 * Number of error diagnostics emitted during compilation.
 */
VITTE_API
VITTE_API_NODISCARD
size_t
vitte_api_result_error_count(
    const vitte_api_result_t *result);

/*
 * Number of warning diagnostics emitted during compilation.
 */
VITTE_API
VITTE_API_NODISCARD
size_t
vitte_api_result_warning_count(
    const vitte_api_result_t *result);

/*
 * Number of note/help diagnostics emitted during compilation.
 */
VITTE_API
VITTE_API_NODISCARD
size_t
vitte_api_result_note_count(
    const vitte_api_result_t *result);

/* ========================================================================= */
/* API state                                                                 */
/* ========================================================================= */

/*
 * Perform a cheap structural validity check on an API handle.
 */
VITTE_API
VITTE_API_NODISCARD
bool
vitte_api_is_valid(
    const vitte_api_t *api);

/*
 * Return true when the API context is currently inside a compilation.
 *
 * The synchronous API normally exposes this only for diagnostics,
 * assertions and future asynchronous integration.
 */
VITTE_API
VITTE_API_NODISCARD
bool
vitte_api_is_compiling(
    const vitte_api_t *api);

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

/*
 * Total number of compilation attempts started by this context.
 */
VITTE_API
VITTE_API_NODISCARD
size_t
vitte_api_compilation_count(
    const vitte_api_t *api);

/*
 * Number of successful compilations.
 */
VITTE_API
VITTE_API_NODISCARD
size_t
vitte_api_successful_compilation_count(
    const vitte_api_t *api);

/*
 * Number of failed compilations.
 */
VITTE_API
VITTE_API_NODISCARD
size_t
vitte_api_failed_compilation_count(
    const vitte_api_t *api);

/* ========================================================================= */
/* Errors                                                                    */
/* ========================================================================= */

/*
 * Return the most recent API error.
 *
 * For a valid context, this returns the context-local error object.
 *
 * For an invalid/NULL context, the process/thread-level last error maintained
 * by the error subsystem may be returned.
 *
 * The returned pointer is borrowed and must not be freed.
 */
VITTE_API
VITTE_API_NODISCARD
const vitte_error_t *
vitte_api_last_error(
    const vitte_api_t *api);

/*
 * Clear context-local and thread-local API error state.
 */
VITTE_API
void
vitte_api_clear_error(
    vitte_api_t *api);

/* ========================================================================= */
/* Validation                                                                */
/* ========================================================================= */

/*
 * Validate internal API invariants.
 *
 * Intended primarily for:
 *
 *   - tests
 *   - assertions
 *   - fuzzing
 *   - debug builds
 *
 * It does not perform compilation.
 */
VITTE_API
VITTE_API_NODISCARD
bool
vitte_api_validate(
    const vitte_api_t *api);

/* ========================================================================= */
/* Version                                                                   */
/* ========================================================================= */

/*
 * Human-readable Vitte version string.
 *
 * Example:
 *
 *     "0.1.1"
 */
VITTE_API
VITTE_API_NODISCARD
const char *
vitte_api_version(void);

/*
 * Numeric semantic-version components.
 */
VITTE_API
VITTE_API_NODISCARD
uint32_t
vitte_api_version_major(void);

VITTE_API
VITTE_API_NODISCARD
uint32_t
vitte_api_version_minor(void);

VITTE_API
VITTE_API_NODISCARD
uint32_t
vitte_api_version_patch(void);

/* ========================================================================= */
/* Build information                                                         */
/* ========================================================================= */

/*
 * Return the compiler-library build mode.
 *
 * Currently:
 *
 *     "debug"
 *     "release"
 */
VITTE_API
VITTE_API_NODISCARD
const char *
vitte_api_build_mode(void);

/*
 * Return the C/C++ compiler family used to build the Vitte library.
 *
 * Typical values:
 *
 *     "clang"
 *     "gcc"
 *     "msvc"
 *     "unknown"
 */
VITTE_API
VITTE_API_NODISCARD
const char *
vitte_api_compiler(void);

/*
 * Return the target host platform known by this build.
 *
 * Typical values:
 *
 *     "darwin"
 *     "linux"
 *     "windows"
 *     "freebsd"
 *     "openbsd"
 *     "netbsd"
 *     "unknown"
 */
VITTE_API
VITTE_API_NODISCARD
const char *
vitte_api_platform(void);

/*
 * Return the target architecture known by this build.
 *
 * Typical values:
 *
 *     "aarch64"
 *     "x86_64"
 *     "x86"
 *     "arm"
 *     "riscv64"
 *     "riscv32"
 *     "unknown"
 */
VITTE_API
VITTE_API_NODISCARD
const char *
vitte_api_architecture(void);

/* ========================================================================= */
/* Convenience predicates                                                    */
/* ========================================================================= */

static inline bool
vitte_api_result_failed(
    const vitte_api_result_t *result)
{
    return !vitte_api_result_succeeded(result);
}

static inline bool
vitte_api_result_has_errors(
    const vitte_api_result_t *result)
{
    return vitte_api_result_error_count(result) != 0u;
}

static inline bool
vitte_api_result_has_warnings(
    const vitte_api_result_t *result)
{
    return vitte_api_result_warning_count(result) != 0u;
}

static inline bool
vitte_api_result_has_notes(
    const vitte_api_result_t *result)
{
    return vitte_api_result_note_count(result) != 0u;
}

static inline bool
vitte_api_has_compiled(
    const vitte_api_t *api)
{
    return vitte_api_compilation_count(api) != 0u;
}

/* ========================================================================= */
/* Convenience compilation                                                   */
/* ========================================================================= */

/*
 * Compile a string while ignoring the result object.
 *
 * Diagnostics remain available only through normal API error state; callers
 * requiring compiler diagnostics should use vitte_api_compile_string().
 */
static inline vitte_status_t
vitte_api_check_string(
    vitte_api_t *api,
    const char *source)
{
    vitte_api_result_t *result;
    vitte_status_t status;

    result = NULL;

    status = vitte_api_compile_string(
        api,
        source,
        &result);

    vitte_api_result_destroy(result);

    return status;
}

/*
 * Compile a file while ignoring the result object.
 */
static inline vitte_status_t
vitte_api_check_file(
    vitte_api_t *api,
    const char *path)
{
    vitte_api_result_t *result;
    vitte_status_t status;

    result = NULL;

    status = vitte_api_compile_file(
        api,
        path,
        &result);

    vitte_api_result_destroy(result);

    return status;
}

/* ========================================================================= */
/* Status convenience                                                        */
/* ========================================================================= */

static inline bool
vitte_api_status_is_ok(
    vitte_status_t status)
{
    return status == VITTE_STATUS_OK;
}

static inline bool
vitte_api_status_is_error(
    vitte_status_t status)
{
    return status != VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Compile-time ABI checks                                                   */
/* ========================================================================= */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L

_Static_assert(
    sizeof(uint32_t) == 4u,
    "Vitte API requires a 32-bit uint32_t");

_Static_assert(
    sizeof(uint64_t) == 8u,
    "Vitte API requires a 64-bit uint64_t");

_Static_assert(
    sizeof(size_t) >= sizeof(uint32_t),
    "Vitte API requires size_t to be at least 32 bits");

#endif

/* ========================================================================= */
/* C++                                                                      */
/* ========================================================================= */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VITTE_SRC_API_API_H */
