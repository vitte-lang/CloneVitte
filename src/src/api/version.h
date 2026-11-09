#ifndef VITTE_SRC_API_VERSION_H
#define VITTE_SRC_API_VERSION_H

/*
 * Vitte Compiler
 * src/api/version.h
 *
 * Version, ABI, API and build-information interface.
 *
 * This subsystem provides the authoritative runtime representation of:
 *
 *   - Vitte semantic version
 *   - public API version
 *   - binary ABI version
 *   - build configuration
 *   - compiler used to build Vitte
 *   - host platform
 *   - host architecture
 *   - pointer width
 *   - endianness
 *   - C standard
 *   - Git metadata
 *   - compiler capabilities
 *   - deterministic build identity
 *
 * Version domains
 * ---------------
 *
 * Vitte version:
 *     Product/compiler/language release version.
 *
 * API version:
 *     Source-level embedding API compatibility.
 *
 * ABI version:
 *     Binary compatibility between compiled clients and the Vitte library.
 *
 * These versions intentionally evolve independently.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Default semantic version                                                  */
/* ========================================================================= */

/*
 * Build systems may override these macros using compiler definitions.
 *
 * Example:
 *
 *   -DVITTE_VERSION_MAJOR=0
 *   -DVITTE_VERSION_MINOR=2
 *   -DVITTE_VERSION_PATCH=0
 *   -DVITTE_VERSION_PRERELEASE=\"beta.1\"
 */
#ifndef VITTE_VERSION_MAJOR
#define VITTE_VERSION_MAJOR 0
#endif

#ifndef VITTE_VERSION_MINOR
#define VITTE_VERSION_MINOR 1
#endif

#ifndef VITTE_VERSION_PATCH
#define VITTE_VERSION_PATCH 1
#endif

#ifndef VITTE_VERSION_PRERELEASE
#define VITTE_VERSION_PRERELEASE ""
#endif

#ifndef VITTE_VERSION_BUILD_METADATA
#define VITTE_VERSION_BUILD_METADATA ""
#endif

/* ========================================================================= */
/* API version                                                               */
/* ========================================================================= */

#ifndef VITTE_API_VERSION_MAJOR
#define VITTE_API_VERSION_MAJOR 1
#endif

#ifndef VITTE_API_VERSION_MINOR
#define VITTE_API_VERSION_MINOR 0
#endif

#ifndef VITTE_API_VERSION_PATCH
#define VITTE_API_VERSION_PATCH 0
#endif

/* ========================================================================= */
/* ABI version                                                               */
/* ========================================================================= */

#ifndef VITTE_ABI_VERSION_MAJOR
#define VITTE_ABI_VERSION_MAJOR 1
#endif

#ifndef VITTE_ABI_VERSION_MINOR
#define VITTE_ABI_VERSION_MINOR 0
#endif

#ifndef VITTE_ABI_VERSION_PATCH
#define VITTE_ABI_VERSION_PATCH 0
#endif

/* ========================================================================= */
/* Build metadata                                                            */
/* ========================================================================= */

#ifndef VITTE_VERSION_GIT_COMMIT
#define VITTE_VERSION_GIT_COMMIT ""
#endif

#ifndef VITTE_VERSION_GIT_BRANCH
#define VITTE_VERSION_GIT_BRANCH ""
#endif

#ifndef VITTE_VERSION_GIT_DIRTY
#define VITTE_VERSION_GIT_DIRTY 0
#endif

#ifndef VITTE_VERSION_PROJECT_NAME
#define VITTE_VERSION_PROJECT_NAME "Vitte"
#endif

#ifndef VITTE_VERSION_COMPILER_NAME
#define VITTE_VERSION_COMPILER_NAME "vitte"
#endif

/* ========================================================================= */
/* Endianness                                                                */
/* ========================================================================= */

typedef enum vitte_endianness {
    /*
     * Byte order could not be determined.
     */
    VITTE_ENDIANNESS_UNKNOWN = 0,

    /*
     * Least-significant byte stored first.
     */
    VITTE_ENDIANNESS_LITTLE = 1,

    /*
     * Most-significant byte stored first.
     */
    VITTE_ENDIANNESS_BIG = 2
} vitte_endianness_t;

/*
 * Return a stable textual name for an endianness value.
 *
 * Possible values:
 *
 *     "little"
 *     "big"
 *     "unknown"
 */
const char *
vitte_endianness_name(
    vitte_endianness_t endianness);

/* ========================================================================= */
/* Compiler capabilities                                                     */
/* ========================================================================= */

/*
 * Runtime-queryable compiler capabilities.
 *
 * These values describe capabilities compiled into this Vitte build.
 *
 * They are not guarantees that a particular source program is valid.
 */
typedef enum vitte_version_feature {
    VITTE_VERSION_FEATURE_NONE = 0,

    /*
     * C17 code-generation backend.
     */
    VITTE_VERSION_FEATURE_C17_BACKEND,

    /*
     * Structured diagnostic engine.
     */
    VITTE_VERSION_FEATURE_DIAGNOSTICS,

    /*
     * JSON diagnostic renderer.
     */
    VITTE_VERSION_FEATURE_JSON_DIAGNOSTICS,

    /*
     * SARIF diagnostic renderer.
     */
    VITTE_VERSION_FEATURE_SARIF,

    /*
     * Language Server Protocol integration.
     */
    VITTE_VERSION_FEATURE_LSP,

    /*
     * async/await language support.
     */
    VITTE_VERSION_FEATURE_ASYNC,

    /*
     * Foreign-function interface.
     */
    VITTE_VERSION_FEATURE_FFI,

    /*
     * Compile-time evaluation.
     */
    VITTE_VERSION_FEATURE_COMPTIME,

    /*
     * requires/ensures contract system.
     */
    VITTE_VERSION_FEATURE_CONTRACTS,

    /*
     * Macro subsystem.
     */
    VITTE_VERSION_FEATURE_MACROS,

    /*
     * Unsafe/low-level language facilities.
     */
    VITTE_VERSION_FEATURE_UNSAFE,

    /*
     * Integrated test support.
     */
    VITTE_VERSION_FEATURE_TESTS,

    /*
     * Sentinel. Not a feature.
     */
    VITTE_VERSION_FEATURE_COUNT
} vitte_version_feature_t;

/*
 * Query whether this compiler build contains a capability.
 */
bool
vitte_version_has_feature(
    vitte_version_feature_t feature);

/*
 * Return a stable machine-readable feature name.
 *
 * Examples:
 *
 *     "c17-backend"
 *     "diagnostics"
 *     "async"
 *     "ffi"
 */
const char *
vitte_version_feature_name(
    vitte_version_feature_t feature);

/* ========================================================================= */
/* Version information structure                                             */
/* ========================================================================= */

/*
 * Snapshot of runtime version/build information.
 *
 * All string pointers are borrowed static storage and must not be freed.
 */
typedef struct vitte_version_info {
    /*
     * Vitte product/compiler version.
     */
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;

    /*
     * Embedding API version.
     */
    uint32_t api_major;
    uint32_t api_minor;
    uint32_t api_patch;

    /*
     * Binary ABI version.
     */
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t abi_patch;

    /*
     * Complete semantic version string.
     */
    const char *version;

    /*
     * Semantic-version prerelease component.
     *
     * Empty string when absent.
     */
    const char *prerelease;

    /*
     * Semantic-version build metadata.
     *
     * Empty string when absent.
     */
    const char *build_metadata;

    /*
     * Product/project name.
     */
    const char *project_name;

    /*
     * Compiler executable/product name.
     */
    const char *compiler_name;

    /*
     * Build mode.
     *
     * Usually:
     *
     *     "debug"
     *     "release"
     */
    const char *build_mode;

    /*
     * C compiler used to build Vitte.
     */
    const char *host_compiler;

    /*
     * Full host compiler version.
     */
    const char *host_compiler_version;

    /*
     * Host/build platform.
     */
    const char *platform;

    /*
     * Host/build architecture.
     */
    const char *architecture;

    /*
     * Native pointer width.
     */
    uint32_t pointer_bits;

    /*
     * Native byte order.
     */
    vitte_endianness_t endianness;

    /*
     * C standard detected while building Vitte.
     *
     * Examples:
     *
     *     1999
     *     2011
     *     2017
     *     2023
     */
    uint32_t c_standard;

    /*
     * Git commit identifier.
     *
     * Empty string when unavailable.
     */
    const char *git_commit;

    /*
     * Git branch.
     *
     * Empty string when unavailable.
     */
    const char *git_branch;

    /*
     * Whether the source tree was dirty when built.
     */
    bool git_dirty;

    /*
     * Build date/time.
     */
    const char *build_date;
    const char *build_time;
} vitte_version_info_t;

/* ========================================================================= */
/* Vitte semantic version                                                    */
/* ========================================================================= */

/*
 * Return complete semantic version.
 *
 * Examples:
 *
 *     0.1.1
 *     0.2.0-alpha.1
 *     1.0.0-beta.2+git.abcdef
 *
 * Returned storage is static.
 */
const char *
vitte_version_string(void);

uint32_t
vitte_version_major(void);

uint32_t
vitte_version_minor(void);

uint32_t
vitte_version_patch(void);

/*
 * Return prerelease component without the '-' prefix.
 *
 * Returns an empty string when absent.
 */
const char *
vitte_version_prerelease(void);

/*
 * Return build metadata without the '+' prefix.
 *
 * Returns an empty string when absent.
 */
const char *
vitte_version_build_metadata(void);

/* ========================================================================= */
/* API version                                                               */
/* ========================================================================= */

uint32_t
vitte_version_api_major(void);

uint32_t
vitte_version_api_minor(void);

uint32_t
vitte_version_api_patch(void);

const char *
vitte_version_api_string(void);

/* ========================================================================= */
/* ABI version                                                               */
/* ========================================================================= */

uint32_t
vitte_version_abi_major(void);

uint32_t
vitte_version_abi_minor(void);

uint32_t
vitte_version_abi_patch(void);

const char *
vitte_version_abi_string(void);

/* ========================================================================= */
/* Compatibility                                                             */
/* ========================================================================= */

/*
 * Test whether this runtime API is compatible with a requested API version.
 *
 * Current policy:
 *
 *     runtime.major == requested.major
 *     runtime.minor >= requested.minor
 *
 * Patch level does not affect compatibility.
 */
bool
vitte_version_api_compatible(
    uint32_t major,
    uint32_t minor);

/*
 * Test whether this runtime ABI is compatible with a requested ABI version.
 *
 * Current policy:
 *
 *     runtime.major == requested.major
 *     runtime.minor >= requested.minor
 */
bool
vitte_version_abi_compatible(
    uint32_t major,
    uint32_t minor);

/* ========================================================================= */
/* Product information                                                       */
/* ========================================================================= */

const char *
vitte_version_project_name(void);

const char *
vitte_version_compiler_name(void);

/* ========================================================================= */
/* Build information                                                         */
/* ========================================================================= */

const char *
vitte_version_build_mode(void);

/*
 * C compiler family used to build Vitte.
 *
 * Typical values:
 *
 *     clang
 *     gcc
 *     msvc
 *     intel-llvm
 *     intel
 *     unknown
 */
const char *
vitte_version_host_compiler(void);

/*
 * Detailed C compiler version.
 */
const char *
vitte_version_host_compiler_version(void);

/*
 * Host/build platform.
 *
 * Typical values:
 *
 *     darwin
 *     linux
 *     windows
 *     freebsd
 *     openbsd
 *     netbsd
 *     dragonfly
 *     solaris
 *     haiku
 *     android
 *     emscripten
 *     wasi
 *     unknown
 */
const char *
vitte_version_platform(void);

/*
 * Host/build architecture.
 *
 * Typical values:
 *
 *     aarch64
 *     x86_64
 *     x86
 *     arm
 *     riscv64
 *     riscv32
 *     powerpc64
 *     powerpc
 *     s390x
 *     s390
 *     mips64
 *     mips
 *     loongarch64
 *     wasm64
 *     wasm32
 *     unknown
 */
const char *
vitte_version_architecture(void);

/*
 * Native pointer width in bits.
 */
uint32_t
vitte_version_pointer_bits(void);

/*
 * Native byte order.
 */
vitte_endianness_t
vitte_version_endianness(void);

/*
 * Detected C language standard.
 */
uint32_t
vitte_version_c_standard(void);

/* ========================================================================= */
/* Git information                                                           */
/* ========================================================================= */

const char *
vitte_version_git_commit(void);

const char *
vitte_version_git_branch(void);

bool
vitte_version_git_dirty(void);

/* ========================================================================= */
/* Build timestamp                                                           */
/* ========================================================================= */

/*
 * Build timestamp values.
 *
 * By default version.c uses __DATE__ and __TIME__. Reproducible-build
 * configurations should override VITTE_VERSION_BUILD_DATE and
 * VITTE_VERSION_BUILD_TIME through the build system.
 */
const char *
vitte_version_build_date(void);

const char *
vitte_version_build_time(void);

/* ========================================================================= */
/* Complete information                                                      */
/* ========================================================================= */

/*
 * Return a complete information snapshot.
 *
 * No allocation occurs.
 */
vitte_version_info_t
vitte_version_info(void);

/*
 * Compact one-line description.
 *
 * Example:
 *
 *     vitte 0.1.1 (darwin/aarch64, release, clang 18...)
 *
 * Returned storage is static.
 */
const char *
vitte_version_full_string(void);

/*
 * Format detailed multi-line version information.
 *
 * snprintf-like contract:
 *
 *   - buffer may be NULL when capacity == 0
 *   - return value is the number of bytes that would have been written,
 *     excluding the terminating NUL
 *   - a sufficiently large buffer is always NUL terminated
 */
size_t
vitte_version_format(
    char *buffer,
    size_t capacity);

/* ========================================================================= */
/* Build identity                                                            */
/* ========================================================================= */

/*
 * Return deterministic 64-bit identity derived from:
 *
 *   - project name
 *   - semantic version
 *   - API version
 *   - ABI version
 *   - platform
 *   - architecture
 *   - build mode
 *   - Git commit
 *
 * This identifier is useful for:
 *
 *   - compiler caches
 *   - artifact metadata
 *   - diagnostics
 *   - crash reports
 *   - test output
 *
 * It is NOT a cryptographic identifier.
 */
uint64_t
vitte_version_build_id(void);

/* ========================================================================= */
/* Validation                                                                */
/* ========================================================================= */

/*
 * Validate internal version/build invariants.
 *
 * Intended for unit tests, compiler startup self-checks and fuzzing.
 */
bool
vitte_version_validate(void);

/* ========================================================================= */
/* Convenience helpers                                                       */
/* ========================================================================= */

static inline bool
vitte_version_is_prerelease(void)
{
    const char *value;

    value = vitte_version_prerelease();

    return value != NULL &&
           value[0] != '\0';
}

static inline bool
vitte_version_has_build_metadata(void)
{
    const char *value;

    value = vitte_version_build_metadata();

    return value != NULL &&
           value[0] != '\0';
}

static inline bool
vitte_version_has_git_commit(void)
{
    const char *value;

    value = vitte_version_git_commit();

    return value != NULL &&
           value[0] != '\0';
}

static inline bool
vitte_version_has_git_branch(void)
{
    const char *value;

    value = vitte_version_git_branch();

    return value != NULL &&
           value[0] != '\0';
}

/* ========================================================================= */
/* Version packing                                                           */
/* ========================================================================= */

/*
 * Compact numeric representation:
 *
 *   0xMMMMmmmmPPPPPPPP
 *
 * where:
 *
 *   M = major (16 bits)
 *   m = minor (16 bits)
 *   P = patch (32 bits)
 *
 * This representation is for comparisons/storage only. It is not part of
 * semantic-version string formatting.
 */
static inline uint64_t
vitte_version_pack(
    uint32_t major,
    uint32_t minor,
    uint32_t patch)
{
    return
        (((uint64_t)(major & UINT32_C(0xffff))) << 48u) |
        (((uint64_t)(minor & UINT32_C(0xffff))) << 32u) |
        ((uint64_t)patch);
}

static inline uint64_t
vitte_version_current_packed(void)
{
    return vitte_version_pack(
        vitte_version_major(),
        vitte_version_minor(),
        vitte_version_patch());
}

static inline uint64_t
vitte_version_api_packed(void)
{
    return vitte_version_pack(
        vitte_version_api_major(),
        vitte_version_api_minor(),
        vitte_version_api_patch());
}

static inline uint64_t
vitte_version_abi_packed(void)
{
    return vitte_version_pack(
        vitte_version_abi_major(),
        vitte_version_abi_minor(),
        vitte_version_abi_patch());
}

/* ========================================================================= */
/* Version comparison                                                        */
/* ========================================================================= */

static inline int
vitte_version_compare_triplet(
    uint32_t left_major,
    uint32_t left_minor,
    uint32_t left_patch,
    uint32_t right_major,
    uint32_t right_minor,
    uint32_t right_patch)
{
    if (left_major < right_major) {
        return -1;
    }

    if (left_major > right_major) {
        return 1;
    }

    if (left_minor < right_minor) {
        return -1;
    }

    if (left_minor > right_minor) {
        return 1;
    }

    if (left_patch < right_patch) {
        return -1;
    }

    if (left_patch > right_patch) {
        return 1;
    }

    return 0;
}

static inline int
vitte_version_compare(
    uint32_t major,
    uint32_t minor,
    uint32_t patch)
{
    return vitte_version_compare_triplet(
        vitte_version_major(),
        vitte_version_minor(),
        vitte_version_patch(),
        major,
        minor,
        patch);
}

static inline bool
vitte_version_at_least(
    uint32_t major,
    uint32_t minor,
    uint32_t patch)
{
    return vitte_version_compare(
        major,
        minor,
        patch) >= 0;
}

/* ========================================================================= */
/* Feature iteration                                                         */
/* ========================================================================= */

/*
 * Return true when feature is an actual enumerable capability.
 */
static inline bool
vitte_version_feature_is_valid(
    vitte_version_feature_t feature)
{
    return feature > VITTE_VERSION_FEATURE_NONE &&
           feature < VITTE_VERSION_FEATURE_COUNT;
}

/*
 * Number of enumerable feature IDs excluding NONE and COUNT.
 */
static inline size_t
vitte_version_feature_count(void)
{
    return
        (size_t)VITTE_VERSION_FEATURE_COUNT -
        (size_t)1u;
}

/* ========================================================================= */
/* Compile-time version helpers                                              */
/* ========================================================================= */

#define VITTE_VERSION_ENCODE(major, minor, patch) \
    ((((uint64_t)((major) & UINT32_C(0xffff))) << 48u) | \
     (((uint64_t)((minor) & UINT32_C(0xffff))) << 32u) | \
     ((uint64_t)(patch)))

#define VITTE_VERSION_CURRENT \
    VITTE_VERSION_ENCODE( \
        VITTE_VERSION_MAJOR, \
        VITTE_VERSION_MINOR, \
        VITTE_VERSION_PATCH)

#define VITTE_API_VERSION_CURRENT \
    VITTE_VERSION_ENCODE( \
        VITTE_API_VERSION_MAJOR, \
        VITTE_API_VERSION_MINOR, \
        VITTE_API_VERSION_PATCH)

#define VITTE_ABI_VERSION_CURRENT \
    VITTE_VERSION_ENCODE( \
        VITTE_ABI_VERSION_MAJOR, \
        VITTE_ABI_VERSION_MINOR, \
        VITTE_ABI_VERSION_PATCH)

/* ========================================================================= */
/* Compile-time checks                                                       */
/* ========================================================================= */

#if defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 201112L

_Static_assert(
    VITTE_VERSION_MAJOR >= 0,
    "Vitte version major must be non-negative");

_Static_assert(
    VITTE_VERSION_MINOR >= 0,
    "Vitte version minor must be non-negative");

_Static_assert(
    VITTE_VERSION_PATCH >= 0,
    "Vitte version patch must be non-negative");

_Static_assert(
    VITTE_API_VERSION_MAJOR >= 0,
    "Vitte API major version must be non-negative");

_Static_assert(
    VITTE_API_VERSION_MINOR >= 0,
    "Vitte API minor version must be non-negative");

_Static_assert(
    VITTE_API_VERSION_PATCH >= 0,
    "Vitte API patch version must be non-negative");

_Static_assert(
    VITTE_ABI_VERSION_MAJOR >= 0,
    "Vitte ABI major version must be non-negative");

_Static_assert(
    VITTE_ABI_VERSION_MINOR >= 0,
    "Vitte ABI minor version must be non-negative");

_Static_assert(
    VITTE_ABI_VERSION_PATCH >= 0,
    "Vitte ABI patch version must be non-negative");

_Static_assert(
    VITTE_VERSION_MAJOR <= UINT32_C(0xffff),
    "Vitte major version exceeds packed representation");

_Static_assert(
    VITTE_VERSION_MINOR <= UINT32_C(0xffff),
    "Vitte minor version exceeds packed representation");

_Static_assert(
    VITTE_API_VERSION_MAJOR <= UINT32_C(0xffff),
    "Vitte API major version exceeds packed representation");

_Static_assert(
    VITTE_API_VERSION_MINOR <= UINT32_C(0xffff),
    "Vitte API minor version exceeds packed representation");

_Static_assert(
    VITTE_ABI_VERSION_MAJOR <= UINT32_C(0xffff),
    "Vitte ABI major version exceeds packed representation");

_Static_assert(
    VITTE_ABI_VERSION_MINOR <= UINT32_C(0xffff),
    "Vitte ABI minor version exceeds packed representation");

_Static_assert(
    sizeof(uint32_t) == 4u,
    "Vitte version API requires 32-bit uint32_t");

_Static_assert(
    sizeof(uint64_t) == 8u,
    "Vitte version API requires 64-bit uint64_t");

#endif

/* ========================================================================= */
/* C++                                                                       */
/* ========================================================================= */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VITTE_SRC_API_VERSION_H */
