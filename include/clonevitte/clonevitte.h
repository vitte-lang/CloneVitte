/*
 * CloneVitte
 * Public C API
 *
 * Copyright (C) 2026 VitteFoundation
 * SPDX-License-Identifier: LicenseRef-VFPL-1.0
 */

#ifndef CLONEVITTE_CLONEVITTE_H
#define CLONEVITTE_CLONEVITTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Version
 * ============================================================ */

#define CLONEVITTE_VERSION_MAJOR 0
#define CLONEVITTE_VERSION_MINOR 1
#define CLONEVITTE_VERSION_PATCH 1
#define CLONEVITTE_VERSION_STRING "0.1.1"

#define CLONEVITTE_VERSION_ENCODE(major, minor, patch) \
    (((uint32_t)(major) << 24u) |                     \
     ((uint32_t)(minor) << 12u) |                     \
     ((uint32_t)(patch)))

#define CLONEVITTE_VERSION \
    CLONEVITTE_VERSION_ENCODE( \
        CLONEVITTE_VERSION_MAJOR, \
        CLONEVITTE_VERSION_MINOR, \
        CLONEVITTE_VERSION_PATCH)

/* ============================================================
 * Visibility
 * ============================================================ */

#if defined(_WIN32) || defined(__CYGWIN__)

#  if defined(CLONEVITTE_BUILD_SHARED)
#    if defined(CLONEVITTE_BUILDING_LIBRARY)
#      define CLONEVITTE_API __declspec(dllexport)
#    else
#      define CLONEVITTE_API __declspec(dllimport)
#    endif
#  else
#    define CLONEVITTE_API
#  endif

#  define CLONEVITTE_LOCAL

#elif defined(__GNUC__) || defined(__clang__)

#  define CLONEVITTE_API __attribute__((visibility("default")))
#  define CLONEVITTE_LOCAL __attribute__((visibility("hidden")))

#else

#  define CLONEVITTE_API
#  define CLONEVITTE_LOCAL

#endif

/* ============================================================
 * Basic types
 * ============================================================ */

typedef uint8_t  CloneVitteU8;
typedef uint16_t CloneVitteU16;
typedef uint32_t CloneVitteU32;
typedef uint64_t CloneVitteU64;

typedef int8_t  CloneVitteI8;
typedef int16_t CloneVitteI16;
typedef int32_t CloneVitteI32;
typedef int64_t CloneVitteI64;

typedef size_t CloneVitteSize;

/* ============================================================
 * Source locations
 * ============================================================ */

typedef struct CloneVitteSourcePosition {
    uint32_t line;
    uint32_t column;
    uint64_t offset;
} CloneVitteSourcePosition;

typedef struct CloneVitteSourceSpan {
    CloneVitteSourcePosition begin;
    CloneVitteSourcePosition end;
} CloneVitteSourceSpan;

/* ============================================================
 * Status
 * ============================================================ */

typedef enum CloneVitteStatus {
    CLONEVITTE_STATUS_OK = 0,

    CLONEVITTE_STATUS_ERROR,
    CLONEVITTE_STATUS_INVALID_ARGUMENT,
    CLONEVITTE_STATUS_OUT_OF_MEMORY,
    CLONEVITTE_STATUS_IO_ERROR,

    CLONEVITTE_STATUS_LEX_ERROR,
    CLONEVITTE_STATUS_PARSE_ERROR,
    CLONEVITTE_STATUS_SEMANTIC_ERROR,
    CLONEVITTE_STATUS_TYPE_ERROR,

    CLONEVITTE_STATUS_HIR_ERROR,
    CLONEVITTE_STATUS_MIR_ERROR,
    CLONEVITTE_STATUS_IR_ERROR,

    CLONEVITTE_STATUS_BACKEND_ERROR,
    CLONEVITTE_STATUS_LINK_ERROR,

    CLONEVITTE_STATUS_INTERNAL_ERROR,
    CLONEVITTE_STATUS_NOT_IMPLEMENTED
} CloneVitteStatus;

/* ============================================================
 * Diagnostic severity
 * ============================================================ */

typedef enum CloneVitteDiagnosticSeverity {
    CLONEVITTE_DIAGNOSTIC_NOTE = 0,
    CLONEVITTE_DIAGNOSTIC_HELP,
    CLONEVITTE_DIAGNOSTIC_WARNING,
    CLONEVITTE_DIAGNOSTIC_ERROR,
    CLONEVITTE_DIAGNOSTIC_FATAL
} CloneVitteDiagnosticSeverity;

/* ============================================================
 * Diagnostic
 * ============================================================ */

typedef struct CloneVitteDiagnostic {
    CloneVitteDiagnosticSeverity severity;

    const char *code;
    const char *message;
    const char *file;

    CloneVitteSourceSpan span;
} CloneVitteDiagnostic;

/* ============================================================
 * Diagnostic callback
 * ============================================================ */

typedef void (*CloneVitteDiagnosticCallback)(
    const CloneVitteDiagnostic *diagnostic,
    void *userdata
);

/* ============================================================
 * Compilation stages
 * ============================================================ */

typedef enum CloneVitteStage {
    CLONEVITTE_STAGE_NONE = 0,

    CLONEVITTE_STAGE_SOURCE,
    CLONEVITTE_STAGE_LEX,
    CLONEVITTE_STAGE_PARSE,
    CLONEVITTE_STAGE_SEMA,
    CLONEVITTE_STAGE_HIR,
    CLONEVITTE_STAGE_MIR,
    CLONEVITTE_STAGE_IR,
    CLONEVITTE_STAGE_CODEGEN,
    CLONEVITTE_STAGE_LINK
} CloneVitteStage;

/* ============================================================
 * Backend
 * ============================================================ */

typedef enum CloneVitteBackend {
    CLONEVITTE_BACKEND_DEFAULT = 0,
    CLONEVITTE_BACKEND_C17,
    CLONEVITTE_BACKEND_NATIVE,
    CLONEVITTE_BACKEND_LLVM,
    CLONEVITTE_BACKEND_VM
} CloneVitteBackend;

/* ============================================================
 * Optimization
 * ============================================================ */

typedef enum CloneVitteOptimizationLevel {
    CLONEVITTE_OPT_NONE = 0,
    CLONEVITTE_OPT_BASIC = 1,
    CLONEVITTE_OPT_DEFAULT = 2,
    CLONEVITTE_OPT_AGGRESSIVE = 3
} CloneVitteOptimizationLevel;

/* ============================================================
 * Emit mode
 * ============================================================ */

typedef enum CloneVitteEmitKind {
    CLONEVITTE_EMIT_EXECUTABLE = 0,
    CLONEVITTE_EMIT_C,
    CLONEVITTE_EMIT_ASSEMBLY,
    CLONEVITTE_EMIT_OBJECT,
    CLONEVITTE_EMIT_HIR,
    CLONEVITTE_EMIT_MIR,
    CLONEVITTE_EMIT_IR
} CloneVitteEmitKind;

/* ============================================================
 * Opaque compiler objects
 * ============================================================ */

typedef struct CloneVitteContext CloneVitteContext;
typedef struct CloneVitteModule CloneVitteModule;
typedef struct CloneVitteSource CloneVitteSource;
typedef struct CloneVitteAst CloneVitteAst;
typedef struct CloneVitteHir CloneVitteHir;
typedef struct CloneVitteMir CloneVitteMir;
typedef struct CloneVitteIr CloneVitteIr;

/* ============================================================
 * Compiler configuration
 * ============================================================ */

typedef struct CloneVitteConfig {
    CloneVitteBackend backend;
    CloneVitteOptimizationLevel optimization;
    CloneVitteEmitKind emit;

    const char *target;
    const char *sysroot;
    const char *output;

    bool debug_info;
    bool warnings_as_errors;
    bool verify_ast;
    bool verify_hir;
    bool verify_mir;
    bool verify_ir;

    CloneVitteDiagnosticCallback diagnostic_callback;
    void *diagnostic_userdata;
} CloneVitteConfig;

/* ============================================================
 * Compilation result
 * ============================================================ */

typedef struct CloneVitteCompileResult {
    CloneVitteStatus status;
    CloneVitteStage failed_stage;

    uint32_t error_count;
    uint32_t warning_count;

    const char *output_path;
} CloneVitteCompileResult;

/* ============================================================
 * Version API
 * ============================================================ */

CLONEVITTE_API
uint32_t clonevitte_version(void);

CLONEVITTE_API
const char *clonevitte_version_string(void);

/* ============================================================
 * Status API
 * ============================================================ */

CLONEVITTE_API
const char *clonevitte_status_string(
    CloneVitteStatus status
);

/* ============================================================
 * Configuration API
 * ============================================================ */

CLONEVITTE_API
CloneVitteConfig clonevitte_config_default(void);

/* ============================================================
 * Context lifecycle
 * ============================================================ */

CLONEVITTE_API
CloneVitteContext *clonevitte_context_create(
    const CloneVitteConfig *config
);

CLONEVITTE_API
void clonevitte_context_destroy(
    CloneVitteContext *context
);

/* ============================================================
 * Source management
 * ============================================================ */

CLONEVITTE_API
CloneVitteStatus clonevitte_source_from_file(
    CloneVitteContext *context,
    const char *path,
    CloneVitteSource **out_source
);

CLONEVITTE_API
CloneVitteStatus clonevitte_source_from_memory(
    CloneVitteContext *context,
    const char *name,
    const char *data,
    size_t size,
    CloneVitteSource **out_source
);

CLONEVITTE_API
void clonevitte_source_destroy(
    CloneVitteSource *source
);

/* ============================================================
 * Frontend API
 * ============================================================ */

CLONEVITTE_API
CloneVitteStatus clonevitte_parse(
    CloneVitteContext *context,
    CloneVitteSource *source,
    CloneVitteAst **out_ast
);

CLONEVITTE_API
CloneVitteStatus clonevitte_analyze(
    CloneVitteContext *context,
    CloneVitteAst *ast
);

/* ============================================================
 * IR pipeline
 * ============================================================ */

CLONEVITTE_API
CloneVitteStatus clonevitte_lower_hir(
    CloneVitteContext *context,
    CloneVitteAst *ast,
    CloneVitteHir **out_hir
);

CLONEVITTE_API
CloneVitteStatus clonevitte_lower_mir(
    CloneVitteContext *context,
    CloneVitteHir *hir,
    CloneVitteMir **out_mir
);

CLONEVITTE_API
CloneVitteStatus clonevitte_lower_ir(
    CloneVitteContext *context,
    CloneVitteMir *mir,
    CloneVitteIr **out_ir
);

/* ============================================================
 * Code generation
 * ============================================================ */

CLONEVITTE_API
CloneVitteStatus clonevitte_codegen(
    CloneVitteContext *context,
    CloneVitteIr *ir,
    const char *output
);

/* ============================================================
 * High-level compilation
 * ============================================================ */

CLONEVITTE_API
CloneVitteCompileResult clonevitte_compile_file(
    CloneVitteContext *context,
    const char *input
);

CLONEVITTE_API
CloneVitteCompileResult clonevitte_compile_source(
    CloneVitteContext *context,
    const char *name,
    const char *source,
    size_t source_size
);

/* ============================================================
 * Destruction of compiler representations
 * ============================================================ */

CLONEVITTE_API
void clonevitte_ast_destroy(
    CloneVitteAst *ast
);

CLONEVITTE_API
void clonevitte_hir_destroy(
    CloneVitteHir *hir
);

CLONEVITTE_API
void clonevitte_mir_destroy(
    CloneVitteMir *mir
);

CLONEVITTE_API
void clonevitte_ir_destroy(
    CloneVitteIr *ir
);

/* ============================================================
 * Capabilities
 * ============================================================ */

CLONEVITTE_API
bool clonevitte_backend_supported(
    CloneVitteBackend backend
);

CLONEVITTE_API
const char *clonevitte_backend_name(
    CloneVitteBackend backend
);

/* ============================================================
 * Build information
 * ============================================================ */

CLONEVITTE_API
const char *clonevitte_build_compiler(void);

CLONEVITTE_API
const char *clonevitte_build_target(void);

CLONEVITTE_API
const char *clonevitte_build_date(void);

#ifdef __cplusplus
}
#endif

#endif /* CLONEVITTE_CLONEVITTE_H */
