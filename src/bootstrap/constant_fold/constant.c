/*
 * Vitte Compiler
 * src/constant_fold/constant.c
 *
 * Canonical constant-value and constant-folding core.
 *
 * This module provides:
 *
 *   - strongly typed constant values
 *   - signed and unsigned integer constants
 *   - floating-point constants
 *   - boolean constants
 *   - character constants
 *   - string constants
 *   - null constants
 *   - poison/error constants
 *   - unary constant folding
 *   - binary constant folding
 *   - comparisons
 *   - logical operations
 *   - integer arithmetic
 *   - floating-point arithmetic
 *   - bitwise operations
 *   - shifts and rotations
 *   - checked overflow detection
 *   - division-by-zero detection
 *   - invalid-shift detection
 *   - deterministic hashing
 *   - structural equality
 *   - value validation
 *   - statistics
 *
 * Design:
 *
 *   AST / HIR / IR
 *        |
 *        v
 *   constant folding
 *        |
 *        +-- constant.c
 *        |
 *        +-- semantic/type information
 *        |
 *        v
 *   folded constant / not constant / diagnostic
 *
 * This file intentionally does not own AST nodes or diagnostics rendering.
 *
 * ISO C17.
 */

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "constant.h"

/* ========================================================================= */
/* Global statistics                                                         */
/* ========================================================================= */

/* Counters belong to the compilation thread, so concurrent compiler
 * contexts do not mutate shared instrumentation state. */
static _Thread_local vitte_constant_stats_t vitte_constant_thread_stats;

/* ========================================================================= */
/* Utility                                                                   */
/* ========================================================================= */

static vitte_constant_t
vitte_constant_invalid(void)
{
    vitte_constant_t value;

    (void)memset(&value, 0, sizeof(value));
    value.kind = VITTE_CONSTANT_KIND_INVALID;

    return value;
}

static vitte_constant_t
vitte_constant_poison(void)
{
    vitte_constant_t value;

    (void)memset(&value, 0, sizeof(value));
    value.kind = VITTE_CONSTANT_KIND_POISON;

    return value;
}

static vitte_constant_result_t
vitte_constant_result_success(vitte_constant_t value)
{
    vitte_constant_result_t result;

    result.status = VITTE_CONSTANT_FOLD_SUCCESS;
    result.error = VITTE_CONSTANT_ERROR_NONE;
    result.value = value;

    return result;
}

static vitte_constant_result_t
vitte_constant_result_error(vitte_constant_error_t error)
{
    vitte_constant_result_t result;

    result.status = VITTE_CONSTANT_FOLD_ERROR;
    result.error = error;
    result.value = vitte_constant_poison();

    return result;
}

static vitte_constant_result_t
vitte_constant_result_not_constant(void)
{
    vitte_constant_result_t result;

    result.status = VITTE_CONSTANT_FOLD_NOT_CONSTANT;
    result.error = VITTE_CONSTANT_ERROR_NONE;
    result.value = vitte_constant_invalid();

    return result;
}

/* ========================================================================= */
/* Constructors                                                              */
/* ========================================================================= */

static vitte_constant_t
vitte_constant_bool(bool value)
{
    vitte_constant_t result;

    (void)memset(&result, 0, sizeof(result));
    result.kind = VITTE_CONSTANT_KIND_BOOL;
    result.as.boolean = value;

    return result;
}

static vitte_constant_t
vitte_constant_i64(vitte_constant_kind_t kind, int64_t value)
{
    vitte_constant_t result;

    (void)memset(&result, 0, sizeof(result));
    result.kind = kind;

    switch (kind) {
        case VITTE_CONSTANT_KIND_I8:
            result.as.i8 = (int8_t)value;
            break;

        case VITTE_CONSTANT_KIND_I16:
            result.as.i16 = (int16_t)value;
            break;

        case VITTE_CONSTANT_KIND_I32:
            result.as.i32 = (int32_t)value;
            break;

        case VITTE_CONSTANT_KIND_I64:
            result.as.i64 = value;
            break;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_U8:
        case VITTE_CONSTANT_KIND_U16:
        case VITTE_CONSTANT_KIND_U32:
        case VITTE_CONSTANT_KIND_U64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            result = vitte_constant_invalid();
            break;
    }

    return result;
}

static vitte_constant_t
vitte_constant_u64(vitte_constant_kind_t kind, uint64_t value)
{
    vitte_constant_t result;

    (void)memset(&result, 0, sizeof(result));
    result.kind = kind;

    switch (kind) {
        case VITTE_CONSTANT_KIND_U8:
            result.as.u8 = (uint8_t)value;
            break;

        case VITTE_CONSTANT_KIND_U16:
            result.as.u16 = (uint16_t)value;
            break;

        case VITTE_CONSTANT_KIND_U32:
            result.as.u32 = (uint32_t)value;
            break;

        case VITTE_CONSTANT_KIND_U64:
            result.as.u64 = value;
            break;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_I8:
        case VITTE_CONSTANT_KIND_I16:
        case VITTE_CONSTANT_KIND_I32:
        case VITTE_CONSTANT_KIND_I64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            result = vitte_constant_invalid();
            break;
    }

    return result;
}

/* ========================================================================= */
/* Names                                                                     */
/* ========================================================================= */

const char *
vitte_constant_error_name(vitte_constant_error_t error)
{
    switch (error) {
        case VITTE_CONSTANT_ERROR_NONE:
            return "none";

        case VITTE_CONSTANT_ERROR_INVALID_ARGUMENT:
            return "invalid argument";

        case VITTE_CONSTANT_ERROR_INVALID_VALUE:
            return "invalid value";

        case VITTE_CONSTANT_ERROR_INVALID_TYPE:
            return "invalid type";

        case VITTE_CONSTANT_ERROR_INVALID_OPERATOR:
            return "invalid operator";

        case VITTE_CONSTANT_ERROR_TYPE_MISMATCH:
            return "type mismatch";

        case VITTE_CONSTANT_ERROR_OVERFLOW:
            return "overflow";

        case VITTE_CONSTANT_ERROR_UNDERFLOW:
            return "underflow";

        case VITTE_CONSTANT_ERROR_DIVISION_BY_ZERO:
            return "division by zero";

        case VITTE_CONSTANT_ERROR_REMAINDER_BY_ZERO:
            return "remainder by zero";

        case VITTE_CONSTANT_ERROR_INVALID_SHIFT:
            return "invalid shift";

        case VITTE_CONSTANT_ERROR_INVALID_CAST:
            return "invalid cast";

        case VITTE_CONSTANT_ERROR_NON_FINITE:
            return "non-finite value";

        case VITTE_CONSTANT_ERROR_STRING_TOO_LONG:
            return "string too long";

        case VITTE_CONSTANT_ERROR_UNSUPPORTED:
            return "unsupported";

        case VITTE_CONSTANT_ERROR_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_constant_kind_name(vitte_constant_kind_t kind)
{
    switch (kind) {
        case VITTE_CONSTANT_KIND_INVALID:
            return "invalid";

        case VITTE_CONSTANT_KIND_BOOL:
            return "bool";

        case VITTE_CONSTANT_KIND_I8:
            return "i8";

        case VITTE_CONSTANT_KIND_I16:
            return "i16";

        case VITTE_CONSTANT_KIND_I32:
            return "i32";

        case VITTE_CONSTANT_KIND_I64:
            return "i64";

        case VITTE_CONSTANT_KIND_U8:
            return "u8";

        case VITTE_CONSTANT_KIND_U16:
            return "u16";

        case VITTE_CONSTANT_KIND_U32:
            return "u32";

        case VITTE_CONSTANT_KIND_U64:
            return "u64";

        case VITTE_CONSTANT_KIND_F32:
            return "f32";

        case VITTE_CONSTANT_KIND_F64:
            return "f64";

        case VITTE_CONSTANT_KIND_CHAR:
            return "char";

        case VITTE_CONSTANT_KIND_STRING:
            return "string";

        case VITTE_CONSTANT_KIND_NULL:
            return "null";

        case VITTE_CONSTANT_KIND_POISON:
            return "poison";

        case VITTE_CONSTANT_KIND_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_constant_unary_operator_name(vitte_constant_unary_operator_t op)
{
    switch (op) {
        case VITTE_CONSTANT_UNARY_INVALID:
            return "invalid";

        case VITTE_CONSTANT_UNARY_PLUS:
            return "plus";

        case VITTE_CONSTANT_UNARY_NEGATE:
            return "negate";

        case VITTE_CONSTANT_UNARY_LOGICAL_NOT:
            return "logical-not";

        case VITTE_CONSTANT_UNARY_BITWISE_NOT:
            return "bitwise-not";

        case VITTE_CONSTANT_UNARY_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_constant_binary_operator_name(vitte_constant_binary_operator_t op)
{
    switch (op) {
        case VITTE_CONSTANT_BINARY_INVALID:
            return "invalid";

        case VITTE_CONSTANT_BINARY_ADD:
            return "add";

        case VITTE_CONSTANT_BINARY_SUBTRACT:
            return "subtract";

        case VITTE_CONSTANT_BINARY_MULTIPLY:
            return "multiply";

        case VITTE_CONSTANT_BINARY_DIVIDE:
            return "divide";

        case VITTE_CONSTANT_BINARY_REMAINDER:
            return "remainder";

        case VITTE_CONSTANT_BINARY_BIT_AND:
            return "bit-and";

        case VITTE_CONSTANT_BINARY_BIT_OR:
            return "bit-or";

        case VITTE_CONSTANT_BINARY_BIT_XOR:
            return "bit-xor";

        case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
            return "shift-left";

        case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
            return "shift-right";

        case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
            return "rotate-left";

        case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
            return "rotate-right";

        case VITTE_CONSTANT_BINARY_LOGICAL_AND:
            return "logical-and";

        case VITTE_CONSTANT_BINARY_LOGICAL_OR:
            return "logical-or";

        case VITTE_CONSTANT_BINARY_EQUAL:
            return "equal";

        case VITTE_CONSTANT_BINARY_NOT_EQUAL:
            return "not-equal";

        case VITTE_CONSTANT_BINARY_LESS:
            return "less";

        case VITTE_CONSTANT_BINARY_LESS_EQUAL:
            return "less-equal";

        case VITTE_CONSTANT_BINARY_GREATER:
            return "greater";

        case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
            return "greater-equal";

        case VITTE_CONSTANT_BINARY_COUNT:
            return "count";
    }

    return "unknown";
}

const char *
vitte_constant_fold_status_name(vitte_constant_fold_status_t status)
{
    switch (status) {
        case VITTE_CONSTANT_FOLD_INVALID:
            return "invalid";

        case VITTE_CONSTANT_FOLD_SUCCESS:
            return "success";

        case VITTE_CONSTANT_FOLD_NOT_CONSTANT:
            return "not-constant";

        case VITTE_CONSTANT_FOLD_ERROR:
            return "error";

        case VITTE_CONSTANT_FOLD_COUNT:
            return "count";
    }

    return "unknown";
}

/* ========================================================================= */
/* Kind classification                                                       */
/* ========================================================================= */

static bool
vitte_constant_kind_is_signed(vitte_constant_kind_t kind)
{
    return
        kind == VITTE_CONSTANT_KIND_I8 ||
        kind == VITTE_CONSTANT_KIND_I16 ||
        kind == VITTE_CONSTANT_KIND_I32 ||
        kind == VITTE_CONSTANT_KIND_I64;
}

static bool
vitte_constant_kind_is_unsigned(vitte_constant_kind_t kind)
{
    return
        kind == VITTE_CONSTANT_KIND_U8 ||
        kind == VITTE_CONSTANT_KIND_U16 ||
        kind == VITTE_CONSTANT_KIND_U32 ||
        kind == VITTE_CONSTANT_KIND_U64;
}

static bool
vitte_constant_kind_is_integer(vitte_constant_kind_t kind)
{
    return
        vitte_constant_kind_is_signed(kind) ||
        vitte_constant_kind_is_unsigned(kind);
}

static bool
vitte_constant_kind_is_float(vitte_constant_kind_t kind)
{
    return
        kind == VITTE_CONSTANT_KIND_F32 ||
        kind == VITTE_CONSTANT_KIND_F64;
}

bool
vitte_constant_is_integer(const vitte_constant_t *value)
{
    return
        value != NULL &&
        vitte_constant_kind_is_integer(value->kind);
}

bool
vitte_constant_is_signed_integer(const vitte_constant_t *value)
{
    return
        value != NULL &&
        vitte_constant_kind_is_signed(value->kind);
}

bool
vitte_constant_is_unsigned_integer(const vitte_constant_t *value)
{
    return
        value != NULL &&
        vitte_constant_kind_is_unsigned(value->kind);
}

bool
vitte_constant_is_float(const vitte_constant_t *value)
{
    return
        value != NULL &&
        vitte_constant_kind_is_float(value->kind);
}

bool
vitte_constant_is_numeric(const vitte_constant_t *value)
{
    return
        value != NULL &&
        (vitte_constant_kind_is_integer(value->kind) ||
         vitte_constant_kind_is_float(value->kind));
}

/* ========================================================================= */
/* Bit width                                                                 */
/* ========================================================================= */

unsigned
vitte_constant_bit_width(vitte_constant_kind_t kind)
{
    switch (kind) {
        case VITTE_CONSTANT_KIND_I8:
        case VITTE_CONSTANT_KIND_U8:
            return 8u;

        case VITTE_CONSTANT_KIND_I16:
        case VITTE_CONSTANT_KIND_U16:
            return 16u;

        case VITTE_CONSTANT_KIND_I32:
        case VITTE_CONSTANT_KIND_U32:
        case VITTE_CONSTANT_KIND_F32:
            return 32u;

        case VITTE_CONSTANT_KIND_I64:
        case VITTE_CONSTANT_KIND_U64:
        case VITTE_CONSTANT_KIND_F64:
            return 64u;

        case VITTE_CONSTANT_KIND_CHAR:
            return 32u;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return 0u;
    }

    return 0u;
}

/* ========================================================================= */
/* Validation                                                                */
/* ========================================================================= */

bool
vitte_constant_is_valid(const vitte_constant_t *value)
{
    if (value == NULL) {
        return false;
    }

    switch (value->kind) {
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_I8:
        case VITTE_CONSTANT_KIND_I16:
        case VITTE_CONSTANT_KIND_I32:
        case VITTE_CONSTANT_KIND_I64:
        case VITTE_CONSTANT_KIND_U8:
        case VITTE_CONSTANT_KIND_U16:
        case VITTE_CONSTANT_KIND_U32:
        case VITTE_CONSTANT_KIND_U64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
            return true;

        case VITTE_CONSTANT_KIND_STRING:
            if (value->as.string.length > VITTE_CONSTANT_MAX_STRING_LENGTH) {
                return false;
            }

            if (value->as.string.length != 0u &&
                value->as.string.data == NULL) {
                return false;
            }

            return true;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_COUNT:
            return false;
    }

    return false;
}

/* ========================================================================= */
/* Integer extraction                                                        */
/* ========================================================================= */

static bool
vitte_constant_get_signed(
    const vitte_constant_t *value,
    int64_t *result)
{
    if (value == NULL || result == NULL) {
        return false;
    }

    switch (value->kind) {
        case VITTE_CONSTANT_KIND_I8:
            *result = (int64_t)value->as.i8;
            return true;

        case VITTE_CONSTANT_KIND_I16:
            *result = (int64_t)value->as.i16;
            return true;

        case VITTE_CONSTANT_KIND_I32:
            *result = (int64_t)value->as.i32;
            return true;

        case VITTE_CONSTANT_KIND_I64:
            *result = value->as.i64;
            return true;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_U8:
        case VITTE_CONSTANT_KIND_U16:
        case VITTE_CONSTANT_KIND_U32:
        case VITTE_CONSTANT_KIND_U64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return false;
    }

    return false;
}

static bool
vitte_constant_get_unsigned(
    const vitte_constant_t *value,
    uint64_t *result)
{
    if (value == NULL || result == NULL) {
        return false;
    }

    switch (value->kind) {
        case VITTE_CONSTANT_KIND_U8:
            *result = (uint64_t)value->as.u8;
            return true;

        case VITTE_CONSTANT_KIND_U16:
            *result = (uint64_t)value->as.u16;
            return true;

        case VITTE_CONSTANT_KIND_U32:
            *result = (uint64_t)value->as.u32;
            return true;

        case VITTE_CONSTANT_KIND_U64:
            *result = value->as.u64;
            return true;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_I8:
        case VITTE_CONSTANT_KIND_I16:
        case VITTE_CONSTANT_KIND_I32:
        case VITTE_CONSTANT_KIND_I64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return false;
    }

    return false;
}

static bool
vitte_constant_get_double(
    const vitte_constant_t *value,
    double *result)
{
    int64_t signed_value;
    uint64_t unsigned_value;

    if (value == NULL || result == NULL) {
        return false;
    }

    switch (value->kind) {
        case VITTE_CONSTANT_KIND_F32:
            *result = (double)value->as.f32;
            return true;

        case VITTE_CONSTANT_KIND_F64:
            *result = value->as.f64;
            return true;

        case VITTE_CONSTANT_KIND_I8:
        case VITTE_CONSTANT_KIND_I16:
        case VITTE_CONSTANT_KIND_I32:
        case VITTE_CONSTANT_KIND_I64:
            if (!vitte_constant_get_signed(value, &signed_value)) {
                return false;
            }

            *result = (double)signed_value;
            return true;

        case VITTE_CONSTANT_KIND_U8:
        case VITTE_CONSTANT_KIND_U16:
        case VITTE_CONSTANT_KIND_U32:
        case VITTE_CONSTANT_KIND_U64:
            if (!vitte_constant_get_unsigned(value, &unsigned_value)) {
                return false;
            }

            *result = (double)unsigned_value;
            return true;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return false;
    }

    return false;
}

/* ========================================================================= */
/* Integer limits                                                            */
/* ========================================================================= */

static int64_t
vitte_constant_signed_min(vitte_constant_kind_t kind)
{
    switch (kind) {
        case VITTE_CONSTANT_KIND_I8:
            return INT8_MIN;

        case VITTE_CONSTANT_KIND_I16:
            return INT16_MIN;

        case VITTE_CONSTANT_KIND_I32:
            return INT32_MIN;

        case VITTE_CONSTANT_KIND_I64:
            return INT64_MIN;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_U8:
        case VITTE_CONSTANT_KIND_U16:
        case VITTE_CONSTANT_KIND_U32:
        case VITTE_CONSTANT_KIND_U64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return 0;
    }

    return 0;
}

static int64_t
vitte_constant_signed_max(vitte_constant_kind_t kind)
{
    switch (kind) {
        case VITTE_CONSTANT_KIND_I8:
            return INT8_MAX;

        case VITTE_CONSTANT_KIND_I16:
            return INT16_MAX;

        case VITTE_CONSTANT_KIND_I32:
            return INT32_MAX;

        case VITTE_CONSTANT_KIND_I64:
            return INT64_MAX;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_U8:
        case VITTE_CONSTANT_KIND_U16:
        case VITTE_CONSTANT_KIND_U32:
        case VITTE_CONSTANT_KIND_U64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return 0;
    }

    return 0;
}

static uint64_t
vitte_constant_unsigned_max(vitte_constant_kind_t kind)
{
    switch (kind) {
        case VITTE_CONSTANT_KIND_U8:
            return UINT8_MAX;

        case VITTE_CONSTANT_KIND_U16:
            return UINT16_MAX;

        case VITTE_CONSTANT_KIND_U32:
            return UINT32_MAX;

        case VITTE_CONSTANT_KIND_U64:
            return UINT64_MAX;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_BOOL:
        case VITTE_CONSTANT_KIND_I8:
        case VITTE_CONSTANT_KIND_I16:
        case VITTE_CONSTANT_KIND_I32:
        case VITTE_CONSTANT_KIND_I64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return UINT64_C(0);
    }

    return UINT64_C(0);
}

/* ========================================================================= */
/* Zero / one                                                               */
/* ========================================================================= */

bool
vitte_constant_is_zero(const vitte_constant_t *value)
{
    if (value == NULL) {
        return false;
    }

    switch (value->kind) {
        case VITTE_CONSTANT_KIND_BOOL:
            return !value->as.boolean;

        case VITTE_CONSTANT_KIND_I8:
            return value->as.i8 == 0;

        case VITTE_CONSTANT_KIND_I16:
            return value->as.i16 == 0;

        case VITTE_CONSTANT_KIND_I32:
            return value->as.i32 == 0;

        case VITTE_CONSTANT_KIND_I64:
            return value->as.i64 == 0;

        case VITTE_CONSTANT_KIND_U8:
            return value->as.u8 == 0u;

        case VITTE_CONSTANT_KIND_U16:
            return value->as.u16 == 0u;

        case VITTE_CONSTANT_KIND_U32:
            return value->as.u32 == 0u;

        case VITTE_CONSTANT_KIND_U64:
            return value->as.u64 == UINT64_C(0);

        case VITTE_CONSTANT_KIND_F32:
            return value->as.f32 == 0.0f;

        case VITTE_CONSTANT_KIND_F64:
            return value->as.f64 == 0.0;

        case VITTE_CONSTANT_KIND_CHAR:
            return value->as.character == UINT32_C(0);

        case VITTE_CONSTANT_KIND_NULL:
            return true;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return false;
    }

    return false;
}

bool
vitte_constant_is_one(const vitte_constant_t *value)
{
    if (value == NULL) {
        return false;
    }

    switch (value->kind) {
        case VITTE_CONSTANT_KIND_BOOL:
            return value->as.boolean;

        case VITTE_CONSTANT_KIND_I8:
            return value->as.i8 == 1;

        case VITTE_CONSTANT_KIND_I16:
            return value->as.i16 == 1;

        case VITTE_CONSTANT_KIND_I32:
            return value->as.i32 == 1;

        case VITTE_CONSTANT_KIND_I64:
            return value->as.i64 == 1;

        case VITTE_CONSTANT_KIND_U8:
            return value->as.u8 == 1u;

        case VITTE_CONSTANT_KIND_U16:
            return value->as.u16 == 1u;

        case VITTE_CONSTANT_KIND_U32:
            return value->as.u32 == 1u;

        case VITTE_CONSTANT_KIND_U64:
            return value->as.u64 == UINT64_C(1);

        case VITTE_CONSTANT_KIND_F32:
            return value->as.f32 == 1.0f;

        case VITTE_CONSTANT_KIND_F64:
            return value->as.f64 == 1.0;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_STRING:
        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return false;
    }

    return false;
}

/* ========================================================================= */
/* Checked signed arithmetic                                                 */
/* ========================================================================= */

static bool
vitte_constant_signed_add_overflow(
    int64_t left,
    int64_t right,
    int64_t minimum,
    int64_t maximum,
    int64_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (right > 0 && left > maximum - right) {
        return true;
    }

    if (right < 0 && left < minimum - right) {
        return true;
    }

    *result = left + right;
    return false;
}

static bool
vitte_constant_signed_sub_overflow(
    int64_t left,
    int64_t right,
    int64_t minimum,
    int64_t maximum,
    int64_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (right < 0 && left > maximum + right) {
        return true;
    }

    if (right > 0 && left < minimum + right) {
        return true;
    }

    *result = left - right;
    return false;
}

static bool
vitte_constant_signed_mul_overflow(
    int64_t left,
    int64_t right,
    int64_t minimum,
    int64_t maximum,
    int64_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (left == 0 || right == 0) {
        *result = 0;
        return false;
    }

    if (left == -1) {
        if (right == minimum) {
            return true;
        }

        *result = -right;
        return *result < minimum || *result > maximum;
    }

    if (right == -1) {
        if (left == minimum) {
            return true;
        }

        *result = -left;
        return *result < minimum || *result > maximum;
    }

    if (left > 0) {
        if (right > 0) {
            if (left > maximum / right) {
                return true;
            }
        } else {
            if (right < minimum / left) {
                return true;
            }
        }
    } else {
        if (right > 0) {
            if (left < minimum / right) {
                return true;
            }
        } else {
            if (left < maximum / right) {
                return true;
            }
        }
    }

    *result = left * right;

    return *result < minimum || *result > maximum;
}

/* ========================================================================= */
/* Checked unsigned arithmetic                                               */
/* ========================================================================= */

static bool
vitte_constant_unsigned_add_overflow(
    uint64_t left,
    uint64_t right,
    uint64_t maximum,
    uint64_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (right > maximum - left) {
        return true;
    }

    *result = left + right;
    return false;
}

static bool
vitte_constant_unsigned_sub_overflow(
    uint64_t left,
    uint64_t right,
    uint64_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (left < right) {
        return true;
    }

    *result = left - right;
    return false;
}

static bool
vitte_constant_unsigned_mul_overflow(
    uint64_t left,
    uint64_t right,
    uint64_t maximum,
    uint64_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (left != UINT64_C(0) && right > maximum / left) {
        return true;
    }

    *result = left * right;
    return false;
}

/* ========================================================================= */
/* Integer masks                                                             */
/* ========================================================================= */

static uint64_t
vitte_constant_mask(unsigned bits)
{
    if (bits == 0u) {
        return UINT64_C(0);
    }

    if (bits >= 64u) {
        return UINT64_MAX;
    }

    return (UINT64_C(1) << bits) - UINT64_C(1);
}

/* ========================================================================= */
/* Rotation                                                                  */
/* ========================================================================= */

static uint64_t
vitte_constant_rotate_left_u64(
    uint64_t value,
    unsigned shift,
    unsigned bits)
{
    uint64_t mask;

    if (bits == 0u || bits > 64u) {
        return value;
    }

    shift %= bits;
    mask = vitte_constant_mask(bits);
    value &= mask;

    if (shift == 0u) {
        return value;
    }

    if (bits == 64u) {
        return
            (value << shift) |
            (value >> (64u - shift));
    }

    return
        ((value << shift) |
         (value >> (bits - shift))) &
        mask;
}

static uint64_t
vitte_constant_rotate_right_u64(
    uint64_t value,
    unsigned shift,
    unsigned bits)
{
    uint64_t mask;

    if (bits == 0u || bits > 64u) {
        return value;
    }

    shift %= bits;
    mask = vitte_constant_mask(bits);
    value &= mask;

    if (shift == 0u) {
        return value;
    }

    if (bits == 64u) {
        return
            (value >> shift) |
            (value << (64u - shift));
    }

    return
        ((value >> shift) |
         (value << (bits - shift))) &
        mask;
}

/* ========================================================================= */
/* Equality                                                                  */
/* ========================================================================= */

bool
vitte_constant_equal(
    const vitte_constant_t *left,
    const vitte_constant_t *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }

    if (left->kind != right->kind) {
        return false;
    }

    switch (left->kind) {
        case VITTE_CONSTANT_KIND_BOOL:
            return left->as.boolean == right->as.boolean;

        case VITTE_CONSTANT_KIND_I8:
            return left->as.i8 == right->as.i8;

        case VITTE_CONSTANT_KIND_I16:
            return left->as.i16 == right->as.i16;

        case VITTE_CONSTANT_KIND_I32:
            return left->as.i32 == right->as.i32;

        case VITTE_CONSTANT_KIND_I64:
            return left->as.i64 == right->as.i64;

        case VITTE_CONSTANT_KIND_U8:
            return left->as.u8 == right->as.u8;

        case VITTE_CONSTANT_KIND_U16:
            return left->as.u16 == right->as.u16;

        case VITTE_CONSTANT_KIND_U32:
            return left->as.u32 == right->as.u32;

        case VITTE_CONSTANT_KIND_U64:
            return left->as.u64 == right->as.u64;

        case VITTE_CONSTANT_KIND_F32:
            return left->as.f32 == right->as.f32;

        case VITTE_CONSTANT_KIND_F64:
            return left->as.f64 == right->as.f64;

        case VITTE_CONSTANT_KIND_CHAR:
            return left->as.character == right->as.character;

        case VITTE_CONSTANT_KIND_STRING:
            if (left->as.string.length != right->as.string.length) {
                return false;
            }

            if (left->as.string.length == 0u) {
                return true;
            }

            if (left->as.string.data == NULL ||
                right->as.string.data == NULL) {
                return false;
            }

            return
                memcmp(
                    left->as.string.data,
                    right->as.string.data,
                    left->as.string.length) == 0;

        case VITTE_CONSTANT_KIND_NULL:
            return true;

        case VITTE_CONSTANT_KIND_POISON:
            return true;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_COUNT:
            return false;
    }

    return false;
}

/* ========================================================================= */
/* Hash                                                                      */
/* ========================================================================= */

static uint64_t
vitte_constant_hash_bytes(
    uint64_t hash,
    const void *data,
    size_t length)
{
    const unsigned char *bytes;
    size_t index;

    bytes = (const unsigned char *)data;

    for (index = 0u; index < length; ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= VITTE_CONSTANT_FNV_PRIME;
    }

    return hash;
}

static uint64_t
vitte_constant_hash_u64(
    uint64_t hash,
    uint64_t value)
{
    unsigned index;

    for (index = 0u; index < 8u; ++index) {
        unsigned char byte;

        byte = (unsigned char)((value >> (index * 8u)) & UINT64_C(0xff));

        hash ^= (uint64_t)byte;
        hash *= VITTE_CONSTANT_FNV_PRIME;
    }

    return hash;
}

uint64_t
vitte_constant_hash(const vitte_constant_t *value)
{
    uint64_t hash;

    if (!vitte_constant_is_valid(value)) {
        return UINT64_C(0);
    }

    hash = VITTE_CONSTANT_FNV_OFFSET;
    hash = vitte_constant_hash_u64(hash, (uint64_t)value->kind);

    switch (value->kind) {
        case VITTE_CONSTANT_KIND_BOOL:
            return
                vitte_constant_hash_u64(
                    hash,
                    value->as.boolean ? UINT64_C(1) : UINT64_C(0));

        case VITTE_CONSTANT_KIND_I8:
            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)(int64_t)value->as.i8);

        case VITTE_CONSTANT_KIND_I16:
            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)(int64_t)value->as.i16);

        case VITTE_CONSTANT_KIND_I32:
            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)(int64_t)value->as.i32);

        case VITTE_CONSTANT_KIND_I64:
            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)value->as.i64);

        case VITTE_CONSTANT_KIND_U8:
            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)value->as.u8);

        case VITTE_CONSTANT_KIND_U16:
            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)value->as.u16);

        case VITTE_CONSTANT_KIND_U32:
            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)value->as.u32);

        case VITTE_CONSTANT_KIND_U64:
            return
                vitte_constant_hash_u64(
                    hash,
                    value->as.u64);

        case VITTE_CONSTANT_KIND_F32: {
            uint32_t bits;

            (void)memcpy(&bits, &value->as.f32, sizeof(bits));

            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)bits);
        }

        case VITTE_CONSTANT_KIND_F64: {
            uint64_t bits;

            (void)memcpy(&bits, &value->as.f64, sizeof(bits));

            return vitte_constant_hash_u64(hash, bits);
        }

        case VITTE_CONSTANT_KIND_CHAR:
            return
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)value->as.character);

        case VITTE_CONSTANT_KIND_STRING:
            hash =
                vitte_constant_hash_u64(
                    hash,
                    (uint64_t)value->as.string.length);

            return
                vitte_constant_hash_bytes(
                    hash,
                    value->as.string.data,
                    value->as.string.length);

        case VITTE_CONSTANT_KIND_NULL:
        case VITTE_CONSTANT_KIND_POISON:
            return hash;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_COUNT:
            return UINT64_C(0);
    }

    return UINT64_C(0);
}

/* ========================================================================= */
/* Boolean conversion                                                        */
/* ========================================================================= */

static bool
vitte_constant_truthy(
    const vitte_constant_t *value,
    bool *result)
{
    if (value == NULL || result == NULL) {
        return false;
    }

    switch (value->kind) {
        case VITTE_CONSTANT_KIND_BOOL:
            *result = value->as.boolean;
            return true;

        case VITTE_CONSTANT_KIND_I8:
        case VITTE_CONSTANT_KIND_I16:
        case VITTE_CONSTANT_KIND_I32:
        case VITTE_CONSTANT_KIND_I64:
        case VITTE_CONSTANT_KIND_U8:
        case VITTE_CONSTANT_KIND_U16:
        case VITTE_CONSTANT_KIND_U32:
        case VITTE_CONSTANT_KIND_U64:
        case VITTE_CONSTANT_KIND_F32:
        case VITTE_CONSTANT_KIND_F64:
        case VITTE_CONSTANT_KIND_CHAR:
        case VITTE_CONSTANT_KIND_NULL:
            *result = !vitte_constant_is_zero(value);
            return true;

        case VITTE_CONSTANT_KIND_STRING:
            *result = value->as.string.length != 0u;
            return true;

        case VITTE_CONSTANT_KIND_INVALID:
        case VITTE_CONSTANT_KIND_POISON:
        case VITTE_CONSTANT_KIND_COUNT:
            return false;
    }

    return false;
}

/* ========================================================================= */
/* Unary signed                                                              */
/* ========================================================================= */

static vitte_constant_result_t
vitte_constant_fold_unary_signed(
    vitte_constant_unary_operator_t op,
    const vitte_constant_t *operand)
{
    int64_t value;
    int64_t minimum;

    if (!vitte_constant_get_signed(operand, &value)) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_TYPE);
    }

    minimum = vitte_constant_signed_min(operand->kind);

    switch (op) {
        case VITTE_CONSTANT_UNARY_PLUS:
            return vitte_constant_result_success(*operand);

        case VITTE_CONSTANT_UNARY_NEGATE:
            if (value == minimum) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        operand->kind,
                        -value));

        case VITTE_CONSTANT_UNARY_BITWISE_NOT: {
            uint64_t raw;
            uint64_t mask;
            unsigned bits;

            bits = vitte_constant_bit_width(operand->kind);
            mask = vitte_constant_mask(bits);
            raw = ((uint64_t)value) & mask;
            raw = (~raw) & mask;

            if (bits < 64u &&
                (raw & (UINT64_C(1) << (bits - 1u))) != UINT64_C(0)) {
                raw |= ~mask;
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        operand->kind,
                        (int64_t)raw));
        }

        case VITTE_CONSTANT_UNARY_INVALID:
        case VITTE_CONSTANT_UNARY_LOGICAL_NOT:
        case VITTE_CONSTANT_UNARY_COUNT:
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
    }

    return
        vitte_constant_result_error(
            VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
}

/* ========================================================================= */
/* Unary unsigned                                                            */
/* ========================================================================= */

static vitte_constant_result_t
vitte_constant_fold_unary_unsigned(
    vitte_constant_unary_operator_t op,
    const vitte_constant_t *operand)
{
    uint64_t value;
    uint64_t mask;
    unsigned bits;

    if (!vitte_constant_get_unsigned(operand, &value)) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_TYPE);
    }

    bits = vitte_constant_bit_width(operand->kind);
    mask = vitte_constant_mask(bits);

    switch (op) {
        case VITTE_CONSTANT_UNARY_PLUS:
            return vitte_constant_result_success(*operand);

        case VITTE_CONSTANT_UNARY_NEGATE:
            if (value != UINT64_C(0)) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_UNDERFLOW);
            }

            return vitte_constant_result_success(*operand);

        case VITTE_CONSTANT_UNARY_BITWISE_NOT:
            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        operand->kind,
                        (~value) & mask));

        case VITTE_CONSTANT_UNARY_INVALID:
        case VITTE_CONSTANT_UNARY_LOGICAL_NOT:
        case VITTE_CONSTANT_UNARY_COUNT:
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
    }

    return
        vitte_constant_result_error(
            VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
}

/* ========================================================================= */
/* Unary folding                                                             */
/* ========================================================================= */

vitte_constant_result_t
vitte_constant_fold_unary(
    vitte_constant_unary_operator_t op,
    const vitte_constant_t *operand)
{
    bool truth;

    ++vitte_constant_thread_stats.fold_attempts;
    ++vitte_constant_thread_stats.unary_folds;

    if (!vitte_constant_is_valid(operand)) {
        ++vitte_constant_thread_stats.fold_failures;

        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_VALUE);
    }

    if (operand->kind == VITTE_CONSTANT_KIND_POISON) {
        ++vitte_constant_thread_stats.fold_failures;

        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_VALUE);
    }

    if (op == VITTE_CONSTANT_UNARY_LOGICAL_NOT) {
        if (!vitte_constant_truthy(operand, &truth)) {
            ++vitte_constant_thread_stats.fold_failures;

            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_TYPE);
        }

        ++vitte_constant_thread_stats.fold_successes;
        ++vitte_constant_thread_stats.boolean_folds;

        return
            vitte_constant_result_success(
                vitte_constant_bool(!truth));
    }

    if (vitte_constant_kind_is_signed(operand->kind)) {
        vitte_constant_result_t result;

        result = vitte_constant_fold_unary_signed(op, operand);

        if (result.status == VITTE_CONSTANT_FOLD_SUCCESS) {
            ++vitte_constant_thread_stats.fold_successes;
            ++vitte_constant_thread_stats.integer_folds;
        } else {
            ++vitte_constant_thread_stats.fold_failures;
        }

        return result;
    }

    if (vitte_constant_kind_is_unsigned(operand->kind)) {
        vitte_constant_result_t result;

        result = vitte_constant_fold_unary_unsigned(op, operand);

        if (result.status == VITTE_CONSTANT_FOLD_SUCCESS) {
            ++vitte_constant_thread_stats.fold_successes;
            ++vitte_constant_thread_stats.integer_folds;
        } else {
            ++vitte_constant_thread_stats.fold_failures;
        }

        return result;
    }

    if (vitte_constant_kind_is_float(operand->kind)) {
        vitte_constant_t result;

        if (op == VITTE_CONSTANT_UNARY_PLUS) {
            ++vitte_constant_thread_stats.fold_successes;
            ++vitte_constant_thread_stats.floating_folds;
            return vitte_constant_result_success(*operand);
        }

        result = *operand;

        if (op == VITTE_CONSTANT_UNARY_NEGATE) {
            if (operand->kind == VITTE_CONSTANT_KIND_F32) {
                result.as.f32 = -operand->as.f32;
            } else {
                result.as.f64 = -operand->as.f64;
            }

            ++vitte_constant_thread_stats.fold_successes;
            ++vitte_constant_thread_stats.floating_folds;

            return vitte_constant_result_success(result);
        }
    }

    ++vitte_constant_thread_stats.fold_failures;

    return
        vitte_constant_result_error(
            VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
}

/* ========================================================================= */
/* Signed binary arithmetic                                                  */
/* ========================================================================= */

static vitte_constant_result_t
vitte_constant_fold_signed_binary(
    vitte_constant_binary_operator_t op,
    const vitte_constant_t *left,
    const vitte_constant_t *right)
{
    int64_t lhs;
    int64_t rhs;
    int64_t result;
    int64_t minimum;
    int64_t maximum;
    bool overflow;
    unsigned bits;

    if (!vitte_constant_get_signed(left, &lhs) ||
        !vitte_constant_get_signed(right, &rhs)) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_TYPE);
    }

    if (left->kind != right->kind) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_TYPE_MISMATCH);
    }

    minimum = vitte_constant_signed_min(left->kind);
    maximum = vitte_constant_signed_max(left->kind);
    bits = vitte_constant_bit_width(left->kind);

    switch (op) {
        case VITTE_CONSTANT_BINARY_ADD:
            overflow =
                vitte_constant_signed_add_overflow(
                    lhs,
                    rhs,
                    minimum,
                    maximum,
                    &result);

            if (overflow) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(left->kind, result));

        case VITTE_CONSTANT_BINARY_SUBTRACT:
            overflow =
                vitte_constant_signed_sub_overflow(
                    lhs,
                    rhs,
                    minimum,
                    maximum,
                    &result);

            if (overflow) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(left->kind, result));

        case VITTE_CONSTANT_BINARY_MULTIPLY:
            overflow =
                vitte_constant_signed_mul_overflow(
                    lhs,
                    rhs,
                    minimum,
                    maximum,
                    &result);

            if (overflow) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(left->kind, result));

        case VITTE_CONSTANT_BINARY_DIVIDE:
            if (rhs == 0) {
                ++vitte_constant_thread_stats.division_by_zero_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_DIVISION_BY_ZERO);
            }

            if (lhs == minimum && rhs == -1) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        lhs / rhs));

        case VITTE_CONSTANT_BINARY_REMAINDER:
            if (rhs == 0) {
                ++vitte_constant_thread_stats.division_by_zero_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_REMAINDER_BY_ZERO);
            }

            if (lhs == minimum && rhs == -1) {
                result = 0;
            } else {
                result = lhs % rhs;
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        result));

        case VITTE_CONSTANT_BINARY_BIT_AND:
            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        (int64_t)(
                            ((uint64_t)lhs) &
                            ((uint64_t)rhs))));

        case VITTE_CONSTANT_BINARY_BIT_OR:
            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        (int64_t)(
                            ((uint64_t)lhs) |
                            ((uint64_t)rhs))));

        case VITTE_CONSTANT_BINARY_BIT_XOR:
            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        (int64_t)(
                            ((uint64_t)lhs) ^
                            ((uint64_t)rhs))));

        case VITTE_CONSTANT_BINARY_SHIFT_LEFT: {
            uint64_t raw;
            uint64_t mask;
            uint64_t shifted;

            if (rhs < 0 || (uint64_t)rhs >= (uint64_t)bits) {
                ++vitte_constant_thread_stats.invalid_shift_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_SHIFT);
            }

            mask = vitte_constant_mask(bits);
            raw = ((uint64_t)lhs) & mask;

            if (rhs != 0 &&
                (raw >> (bits - (unsigned)rhs)) != UINT64_C(0)) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            shifted = (raw << (unsigned)rhs) & mask;

            if (bits < 64u &&
                (shifted & (UINT64_C(1) << (bits - 1u))) != UINT64_C(0)) {
                shifted |= ~mask;
            }

            result = (int64_t)shifted;

            if (result < minimum || result > maximum) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        result));
        }

        case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
            if (rhs < 0 || (uint64_t)rhs >= (uint64_t)bits) {
                ++vitte_constant_thread_stats.invalid_shift_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_SHIFT);
            }

            /*
             * ISO C signed right shift of a negative value is implementation
             * defined. Constant folding must be deterministic, so arithmetic
             * right shift is implemented explicitly.
             */
            if (rhs == 0) {
                result = lhs;
            } else if (lhs >= 0) {
                result =
                    (int64_t)(
                        ((uint64_t)lhs) >>
                        (unsigned)rhs);
            } else {
                uint64_t raw;
                uint64_t mask;
                uint64_t fill;
                uint64_t shifted;

                mask = vitte_constant_mask(bits);
                raw = ((uint64_t)lhs) & mask;
                shifted = raw >> (unsigned)rhs;

                fill =
                    mask ^
                    (mask >> (unsigned)rhs);

                shifted |= fill;

                if (bits < 64u) {
                    shifted |= ~mask;
                }

                result = (int64_t)shifted;
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        result));

        case VITTE_CONSTANT_BINARY_ROTATE_LEFT: {
            uint64_t rotated;

            if (rhs < 0) {
                ++vitte_constant_thread_stats.invalid_shift_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_SHIFT);
            }

            rotated =
                vitte_constant_rotate_left_u64(
                    (uint64_t)lhs,
                    (unsigned)((uint64_t)rhs % (uint64_t)bits),
                    bits);

            if (bits < 64u &&
                (rotated & (UINT64_C(1) << (bits - 1u))) != UINT64_C(0)) {
                rotated |= ~vitte_constant_mask(bits);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        (int64_t)rotated));
        }

        case VITTE_CONSTANT_BINARY_ROTATE_RIGHT: {
            uint64_t rotated;

            if (rhs < 0) {
                ++vitte_constant_thread_stats.invalid_shift_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_SHIFT);
            }

            rotated =
                vitte_constant_rotate_right_u64(
                    (uint64_t)lhs,
                    (unsigned)((uint64_t)rhs % (uint64_t)bits),
                    bits);

            if (bits < 64u &&
                (rotated & (UINT64_C(1) << (bits - 1u))) != UINT64_C(0)) {
                rotated |= ~vitte_constant_mask(bits);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        left->kind,
                        (int64_t)rotated));
        }

        case VITTE_CONSTANT_BINARY_INVALID:
        case VITTE_CONSTANT_BINARY_LOGICAL_AND:
        case VITTE_CONSTANT_BINARY_LOGICAL_OR:
        case VITTE_CONSTANT_BINARY_EQUAL:
        case VITTE_CONSTANT_BINARY_NOT_EQUAL:
        case VITTE_CONSTANT_BINARY_LESS:
        case VITTE_CONSTANT_BINARY_LESS_EQUAL:
        case VITTE_CONSTANT_BINARY_GREATER:
        case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
        case VITTE_CONSTANT_BINARY_COUNT:
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
    }

    return
        vitte_constant_result_error(
            VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
}

/* ========================================================================= */
/* Unsigned binary arithmetic                                                */
/* ========================================================================= */

static vitte_constant_result_t
vitte_constant_fold_unsigned_binary(
    vitte_constant_binary_operator_t op,
    const vitte_constant_t *left,
    const vitte_constant_t *right)
{
    uint64_t lhs;
    uint64_t rhs;
    uint64_t result;
    uint64_t maximum;
    bool overflow;
    unsigned bits;

    if (!vitte_constant_get_unsigned(left, &lhs) ||
        !vitte_constant_get_unsigned(right, &rhs)) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_TYPE);
    }

    if (left->kind != right->kind) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_TYPE_MISMATCH);
    }

    maximum = vitte_constant_unsigned_max(left->kind);
    bits = vitte_constant_bit_width(left->kind);

    switch (op) {
        case VITTE_CONSTANT_BINARY_ADD:
            overflow =
                vitte_constant_unsigned_add_overflow(
                    lhs,
                    rhs,
                    maximum,
                    &result);

            if (overflow) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(left->kind, result));

        case VITTE_CONSTANT_BINARY_SUBTRACT:
            overflow =
                vitte_constant_unsigned_sub_overflow(
                    lhs,
                    rhs,
                    &result);

            if (overflow) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_UNDERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(left->kind, result));

        case VITTE_CONSTANT_BINARY_MULTIPLY:
            overflow =
                vitte_constant_unsigned_mul_overflow(
                    lhs,
                    rhs,
                    maximum,
                    &result);

            if (overflow) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(left->kind, result));

        case VITTE_CONSTANT_BINARY_DIVIDE:
            if (rhs == UINT64_C(0)) {
                ++vitte_constant_thread_stats.division_by_zero_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_DIVISION_BY_ZERO);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        lhs / rhs));

        case VITTE_CONSTANT_BINARY_REMAINDER:
            if (rhs == UINT64_C(0)) {
                ++vitte_constant_thread_stats.division_by_zero_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_REMAINDER_BY_ZERO);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        lhs % rhs));

        case VITTE_CONSTANT_BINARY_BIT_AND:
            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        lhs & rhs));

        case VITTE_CONSTANT_BINARY_BIT_OR:
            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        lhs | rhs));

        case VITTE_CONSTANT_BINARY_BIT_XOR:
            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        lhs ^ rhs));

        case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
            if (rhs >= (uint64_t)bits) {
                ++vitte_constant_thread_stats.invalid_shift_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_SHIFT);
            }

            if (rhs != UINT64_C(0) &&
                lhs > (maximum >> (unsigned)rhs)) {
                ++vitte_constant_thread_stats.overflow_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        lhs << (unsigned)rhs));

        case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
            if (rhs >= (uint64_t)bits) {
                ++vitte_constant_thread_stats.invalid_shift_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_SHIFT);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        lhs >> (unsigned)rhs));

        case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        vitte_constant_rotate_left_u64(
                            lhs,
                            (unsigned)(rhs % (uint64_t)bits),
                            bits)));

        case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        left->kind,
                        vitte_constant_rotate_right_u64(
                            lhs,
                            (unsigned)(rhs % (uint64_t)bits),
                            bits)));

        case VITTE_CONSTANT_BINARY_INVALID:
        case VITTE_CONSTANT_BINARY_LOGICAL_AND:
        case VITTE_CONSTANT_BINARY_LOGICAL_OR:
        case VITTE_CONSTANT_BINARY_EQUAL:
        case VITTE_CONSTANT_BINARY_NOT_EQUAL:
        case VITTE_CONSTANT_BINARY_LESS:
        case VITTE_CONSTANT_BINARY_LESS_EQUAL:
        case VITTE_CONSTANT_BINARY_GREATER:
        case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
        case VITTE_CONSTANT_BINARY_COUNT:
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
    }

    return
        vitte_constant_result_error(
            VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
}

/* ========================================================================= */
/* Floating binary arithmetic                                                */
/* ========================================================================= */

static vitte_constant_result_t
vitte_constant_fold_float_binary(
    vitte_constant_binary_operator_t op,
    const vitte_constant_t *left,
    const vitte_constant_t *right)
{
    double lhs;
    double rhs;
    double result;
    vitte_constant_t value;

    if (left->kind != right->kind) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_TYPE_MISMATCH);
    }

    if (!vitte_constant_get_double(left, &lhs) ||
        !vitte_constant_get_double(right, &rhs)) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_TYPE);
    }

    switch (op) {
        case VITTE_CONSTANT_BINARY_ADD:
            result = lhs + rhs;
            break;

        case VITTE_CONSTANT_BINARY_SUBTRACT:
            result = lhs - rhs;
            break;

        case VITTE_CONSTANT_BINARY_MULTIPLY:
            result = lhs * rhs;
            break;

        case VITTE_CONSTANT_BINARY_DIVIDE:
            if (rhs == 0.0) {
                ++vitte_constant_thread_stats.division_by_zero_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_DIVISION_BY_ZERO);
            }

            result = lhs / rhs;
            break;

        case VITTE_CONSTANT_BINARY_REMAINDER:
            if (rhs == 0.0) {
                ++vitte_constant_thread_stats.division_by_zero_errors;

                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_REMAINDER_BY_ZERO);
            }

            result = fmod(lhs, rhs);
            break;

        case VITTE_CONSTANT_BINARY_INVALID:
        case VITTE_CONSTANT_BINARY_BIT_AND:
        case VITTE_CONSTANT_BINARY_BIT_OR:
        case VITTE_CONSTANT_BINARY_BIT_XOR:
        case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
        case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
        case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
        case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
        case VITTE_CONSTANT_BINARY_LOGICAL_AND:
        case VITTE_CONSTANT_BINARY_LOGICAL_OR:
        case VITTE_CONSTANT_BINARY_EQUAL:
        case VITTE_CONSTANT_BINARY_NOT_EQUAL:
        case VITTE_CONSTANT_BINARY_LESS:
        case VITTE_CONSTANT_BINARY_LESS_EQUAL:
        case VITTE_CONSTANT_BINARY_GREATER:
        case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
        case VITTE_CONSTANT_BINARY_COUNT:
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
    }

    if (!isfinite(result)) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_NON_FINITE);
    }

    value = *left;

    if (left->kind == VITTE_CONSTANT_KIND_F32) {
        if (result > (double)FLT_MAX ||
            result < -(double)FLT_MAX) {
            ++vitte_constant_thread_stats.overflow_errors;

            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_OVERFLOW);
        }

        value.as.f32 = (float)result;

        if (!isfinite(value.as.f32)) {
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_NON_FINITE);
        }
    } else {
        value.as.f64 = result;
    }

    return vitte_constant_result_success(value);
}

/* ========================================================================= */
/* Comparison                                                                */
/* ========================================================================= */

static vitte_constant_result_t
vitte_constant_fold_comparison(
    vitte_constant_binary_operator_t op,
    const vitte_constant_t *left,
    const vitte_constant_t *right)
{
    bool result;

    if (left->kind != right->kind) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_TYPE_MISMATCH);
    }

    if (op == VITTE_CONSTANT_BINARY_EQUAL ||
        op == VITTE_CONSTANT_BINARY_NOT_EQUAL) {
        result = vitte_constant_equal(left, right);

        if (op == VITTE_CONSTANT_BINARY_NOT_EQUAL) {
            result = !result;
        }

        return
            vitte_constant_result_success(
                vitte_constant_bool(result));
    }

    if (vitte_constant_kind_is_signed(left->kind)) {
        int64_t lhs;
        int64_t rhs;

        if (!vitte_constant_get_signed(left, &lhs) ||
            !vitte_constant_get_signed(right, &rhs)) {
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_TYPE);
        }

        switch (op) {
            case VITTE_CONSTANT_BINARY_LESS:
                result = lhs < rhs;
                break;

            case VITTE_CONSTANT_BINARY_LESS_EQUAL:
                result = lhs <= rhs;
                break;

            case VITTE_CONSTANT_BINARY_GREATER:
                result = lhs > rhs;
                break;

            case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
                result = lhs >= rhs;
                break;

            case VITTE_CONSTANT_BINARY_INVALID:
            case VITTE_CONSTANT_BINARY_ADD:
            case VITTE_CONSTANT_BINARY_SUBTRACT:
            case VITTE_CONSTANT_BINARY_MULTIPLY:
            case VITTE_CONSTANT_BINARY_DIVIDE:
            case VITTE_CONSTANT_BINARY_REMAINDER:
            case VITTE_CONSTANT_BINARY_BIT_AND:
            case VITTE_CONSTANT_BINARY_BIT_OR:
            case VITTE_CONSTANT_BINARY_BIT_XOR:
            case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
            case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
            case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
            case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
            case VITTE_CONSTANT_BINARY_LOGICAL_AND:
            case VITTE_CONSTANT_BINARY_LOGICAL_OR:
            case VITTE_CONSTANT_BINARY_EQUAL:
            case VITTE_CONSTANT_BINARY_NOT_EQUAL:
            case VITTE_CONSTANT_BINARY_COUNT:
            default:
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
        }

        return
            vitte_constant_result_success(
                vitte_constant_bool(result));
    }

    if (vitte_constant_kind_is_unsigned(left->kind)) {
        uint64_t lhs;
        uint64_t rhs;

        if (!vitte_constant_get_unsigned(left, &lhs) ||
            !vitte_constant_get_unsigned(right, &rhs)) {
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_TYPE);
        }

        switch (op) {
            case VITTE_CONSTANT_BINARY_LESS:
                result = lhs < rhs;
                break;

            case VITTE_CONSTANT_BINARY_LESS_EQUAL:
                result = lhs <= rhs;
                break;

            case VITTE_CONSTANT_BINARY_GREATER:
                result = lhs > rhs;
                break;

            case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
                result = lhs >= rhs;
                break;

            case VITTE_CONSTANT_BINARY_INVALID:
            case VITTE_CONSTANT_BINARY_ADD:
            case VITTE_CONSTANT_BINARY_SUBTRACT:
            case VITTE_CONSTANT_BINARY_MULTIPLY:
            case VITTE_CONSTANT_BINARY_DIVIDE:
            case VITTE_CONSTANT_BINARY_REMAINDER:
            case VITTE_CONSTANT_BINARY_BIT_AND:
            case VITTE_CONSTANT_BINARY_BIT_OR:
            case VITTE_CONSTANT_BINARY_BIT_XOR:
            case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
            case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
            case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
            case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
            case VITTE_CONSTANT_BINARY_LOGICAL_AND:
            case VITTE_CONSTANT_BINARY_LOGICAL_OR:
            case VITTE_CONSTANT_BINARY_EQUAL:
            case VITTE_CONSTANT_BINARY_NOT_EQUAL:
            case VITTE_CONSTANT_BINARY_COUNT:
            default:
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
        }

        return
            vitte_constant_result_success(
                vitte_constant_bool(result));
    }

    if (vitte_constant_kind_is_float(left->kind)) {
        double lhs;
        double rhs;

        if (!vitte_constant_get_double(left, &lhs) ||
            !vitte_constant_get_double(right, &rhs)) {
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_TYPE);
        }

        switch (op) {
            case VITTE_CONSTANT_BINARY_LESS:
                result = lhs < rhs;
                break;

            case VITTE_CONSTANT_BINARY_LESS_EQUAL:
                result = lhs <= rhs;
                break;

            case VITTE_CONSTANT_BINARY_GREATER:
                result = lhs > rhs;
                break;

            case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
                result = lhs >= rhs;
                break;

            case VITTE_CONSTANT_BINARY_INVALID:
            case VITTE_CONSTANT_BINARY_ADD:
            case VITTE_CONSTANT_BINARY_SUBTRACT:
            case VITTE_CONSTANT_BINARY_MULTIPLY:
            case VITTE_CONSTANT_BINARY_DIVIDE:
            case VITTE_CONSTANT_BINARY_REMAINDER:
            case VITTE_CONSTANT_BINARY_BIT_AND:
            case VITTE_CONSTANT_BINARY_BIT_OR:
            case VITTE_CONSTANT_BINARY_BIT_XOR:
            case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
            case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
            case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
            case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
            case VITTE_CONSTANT_BINARY_LOGICAL_AND:
            case VITTE_CONSTANT_BINARY_LOGICAL_OR:
            case VITTE_CONSTANT_BINARY_EQUAL:
            case VITTE_CONSTANT_BINARY_NOT_EQUAL:
            case VITTE_CONSTANT_BINARY_COUNT:
            default:
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
        }

        return
            vitte_constant_result_success(
                vitte_constant_bool(result));
    }

    if (left->kind == VITTE_CONSTANT_KIND_CHAR) {
        uint32_t lhs;
        uint32_t rhs;

        lhs = left->as.character;
        rhs = right->as.character;

        switch (op) {
            case VITTE_CONSTANT_BINARY_LESS:
                result = lhs < rhs;
                break;

            case VITTE_CONSTANT_BINARY_LESS_EQUAL:
                result = lhs <= rhs;
                break;

            case VITTE_CONSTANT_BINARY_GREATER:
                result = lhs > rhs;
                break;

            case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
                result = lhs >= rhs;
                break;

            case VITTE_CONSTANT_BINARY_INVALID:
            case VITTE_CONSTANT_BINARY_ADD:
            case VITTE_CONSTANT_BINARY_SUBTRACT:
            case VITTE_CONSTANT_BINARY_MULTIPLY:
            case VITTE_CONSTANT_BINARY_DIVIDE:
            case VITTE_CONSTANT_BINARY_REMAINDER:
            case VITTE_CONSTANT_BINARY_BIT_AND:
            case VITTE_CONSTANT_BINARY_BIT_OR:
            case VITTE_CONSTANT_BINARY_BIT_XOR:
            case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
            case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
            case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
            case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
            case VITTE_CONSTANT_BINARY_LOGICAL_AND:
            case VITTE_CONSTANT_BINARY_LOGICAL_OR:
            case VITTE_CONSTANT_BINARY_EQUAL:
            case VITTE_CONSTANT_BINARY_NOT_EQUAL:
            case VITTE_CONSTANT_BINARY_COUNT:
            default:
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
        }

        return
            vitte_constant_result_success(
                vitte_constant_bool(result));
    }

    return
        vitte_constant_result_error(
            VITTE_CONSTANT_ERROR_INVALID_TYPE);
}

/* ========================================================================= */
/* Logical binary                                                            */
/* ========================================================================= */

static vitte_constant_result_t
vitte_constant_fold_logical(
    vitte_constant_binary_operator_t op,
    const vitte_constant_t *left,
    const vitte_constant_t *right)
{
    bool lhs;
    bool rhs;

    if (!vitte_constant_truthy(left, &lhs) ||
        !vitte_constant_truthy(right, &rhs)) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_TYPE);
    }

    switch (op) {
        case VITTE_CONSTANT_BINARY_LOGICAL_AND:
            return
                vitte_constant_result_success(
                    vitte_constant_bool(lhs && rhs));

        case VITTE_CONSTANT_BINARY_LOGICAL_OR:
            return
                vitte_constant_result_success(
                    vitte_constant_bool(lhs || rhs));

        case VITTE_CONSTANT_BINARY_INVALID:
        case VITTE_CONSTANT_BINARY_ADD:
        case VITTE_CONSTANT_BINARY_SUBTRACT:
        case VITTE_CONSTANT_BINARY_MULTIPLY:
        case VITTE_CONSTANT_BINARY_DIVIDE:
        case VITTE_CONSTANT_BINARY_REMAINDER:
        case VITTE_CONSTANT_BINARY_BIT_AND:
        case VITTE_CONSTANT_BINARY_BIT_OR:
        case VITTE_CONSTANT_BINARY_BIT_XOR:
        case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
        case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
        case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
        case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
        case VITTE_CONSTANT_BINARY_EQUAL:
        case VITTE_CONSTANT_BINARY_NOT_EQUAL:
        case VITTE_CONSTANT_BINARY_LESS:
        case VITTE_CONSTANT_BINARY_LESS_EQUAL:
        case VITTE_CONSTANT_BINARY_GREATER:
        case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
        case VITTE_CONSTANT_BINARY_COUNT:
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
    }

    return
        vitte_constant_result_error(
            VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
}

/* ========================================================================= */
/* Binary folding                                                            */
/* ========================================================================= */

vitte_constant_result_t
vitte_constant_fold_binary(
    vitte_constant_binary_operator_t op,
    const vitte_constant_t *left,
    const vitte_constant_t *right)
{
    vitte_constant_result_t result;

    ++vitte_constant_thread_stats.fold_attempts;
    ++vitte_constant_thread_stats.binary_folds;

    if (!vitte_constant_is_valid(left) ||
        !vitte_constant_is_valid(right)) {
        ++vitte_constant_thread_stats.fold_failures;

        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_VALUE);
    }

    if (left->kind == VITTE_CONSTANT_KIND_POISON ||
        right->kind == VITTE_CONSTANT_KIND_POISON) {
        ++vitte_constant_thread_stats.fold_failures;

        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_VALUE);
    }

    switch (op) {
        case VITTE_CONSTANT_BINARY_LOGICAL_AND:
        case VITTE_CONSTANT_BINARY_LOGICAL_OR:
            result =
                vitte_constant_fold_logical(
                    op,
                    left,
                    right);

            if (result.status == VITTE_CONSTANT_FOLD_SUCCESS) {
                ++vitte_constant_thread_stats.boolean_folds;
            }

            break;

        case VITTE_CONSTANT_BINARY_EQUAL:
        case VITTE_CONSTANT_BINARY_NOT_EQUAL:
        case VITTE_CONSTANT_BINARY_LESS:
        case VITTE_CONSTANT_BINARY_LESS_EQUAL:
        case VITTE_CONSTANT_BINARY_GREATER:
        case VITTE_CONSTANT_BINARY_GREATER_EQUAL:
            result =
                vitte_constant_fold_comparison(
                    op,
                    left,
                    right);

            if (result.status == VITTE_CONSTANT_FOLD_SUCCESS) {
                ++vitte_constant_thread_stats.comparison_folds;
            }

            break;

        case VITTE_CONSTANT_BINARY_ADD:
        case VITTE_CONSTANT_BINARY_SUBTRACT:
        case VITTE_CONSTANT_BINARY_MULTIPLY:
        case VITTE_CONSTANT_BINARY_DIVIDE:
        case VITTE_CONSTANT_BINARY_REMAINDER:
        case VITTE_CONSTANT_BINARY_BIT_AND:
        case VITTE_CONSTANT_BINARY_BIT_OR:
        case VITTE_CONSTANT_BINARY_BIT_XOR:
        case VITTE_CONSTANT_BINARY_SHIFT_LEFT:
        case VITTE_CONSTANT_BINARY_SHIFT_RIGHT:
        case VITTE_CONSTANT_BINARY_ROTATE_LEFT:
        case VITTE_CONSTANT_BINARY_ROTATE_RIGHT:
            if (left->kind != right->kind) {
                result =
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_TYPE_MISMATCH);
                break;
            }

            if (vitte_constant_kind_is_signed(left->kind)) {
                result =
                    vitte_constant_fold_signed_binary(
                        op,
                        left,
                        right);

                if (result.status == VITTE_CONSTANT_FOLD_SUCCESS) {
                    ++vitte_constant_thread_stats.integer_folds;

                    if (op == VITTE_CONSTANT_BINARY_BIT_AND ||
                        op == VITTE_CONSTANT_BINARY_BIT_OR ||
                        op == VITTE_CONSTANT_BINARY_BIT_XOR ||
                        op == VITTE_CONSTANT_BINARY_SHIFT_LEFT ||
                        op == VITTE_CONSTANT_BINARY_SHIFT_RIGHT ||
                        op == VITTE_CONSTANT_BINARY_ROTATE_LEFT ||
                        op == VITTE_CONSTANT_BINARY_ROTATE_RIGHT) {
                        ++vitte_constant_thread_stats.bitwise_folds;
                    }
                }

                break;
            }

            if (vitte_constant_kind_is_unsigned(left->kind)) {
                result =
                    vitte_constant_fold_unsigned_binary(
                        op,
                        left,
                        right);

                if (result.status == VITTE_CONSTANT_FOLD_SUCCESS) {
                    ++vitte_constant_thread_stats.integer_folds;

                    if (op == VITTE_CONSTANT_BINARY_BIT_AND ||
                        op == VITTE_CONSTANT_BINARY_BIT_OR ||
                        op == VITTE_CONSTANT_BINARY_BIT_XOR ||
                        op == VITTE_CONSTANT_BINARY_SHIFT_LEFT ||
                        op == VITTE_CONSTANT_BINARY_SHIFT_RIGHT ||
                        op == VITTE_CONSTANT_BINARY_ROTATE_LEFT ||
                        op == VITTE_CONSTANT_BINARY_ROTATE_RIGHT) {
                        ++vitte_constant_thread_stats.bitwise_folds;
                    }
                }

                break;
            }

            if (vitte_constant_kind_is_float(left->kind)) {
                result =
                    vitte_constant_fold_float_binary(
                        op,
                        left,
                        right);

                if (result.status == VITTE_CONSTANT_FOLD_SUCCESS) {
                    ++vitte_constant_thread_stats.floating_folds;
                }

                break;
            }

            result =
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_TYPE);
            break;

        case VITTE_CONSTANT_BINARY_INVALID:
        case VITTE_CONSTANT_BINARY_COUNT:
            result =
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_OPERATOR);
            break;
    }

    if (result.status == VITTE_CONSTANT_FOLD_SUCCESS) {
        ++vitte_constant_thread_stats.fold_successes;
    } else {
        ++vitte_constant_thread_stats.fold_failures;
    }

    return result;
}

/* ========================================================================= */
/* Cast helpers                                                              */
/* ========================================================================= */

static bool
vitte_constant_double_to_signed(
    double value,
    vitte_constant_kind_t destination,
    int64_t *result)
{
    double minimum;
    double maximum;
    int64_t signed_minimum;
    int64_t signed_maximum;

    if (result == NULL || !isfinite(value)) {
        return false;
    }

    signed_minimum =
        vitte_constant_signed_min(destination);
    signed_maximum =
        vitte_constant_signed_max(destination);

    minimum = (double)signed_minimum;
    maximum = (double)signed_maximum;

    if (value < minimum || value > maximum) {
        return false;
    }

    *result = (int64_t)value;
    return true;
}

static bool
vitte_constant_double_to_unsigned(
    double value,
    vitte_constant_kind_t destination,
    uint64_t *result)
{
    double maximum;
    uint64_t unsigned_maximum;

    if (result == NULL || !isfinite(value)) {
        return false;
    }

    unsigned_maximum =
        vitte_constant_unsigned_max(destination);
    maximum = (double)unsigned_maximum;

    if (value < 0.0 || value > maximum) {
        return false;
    }

    *result = (uint64_t)value;
    return true;
}

/* ========================================================================= */
/* Constant cast                                                             */
/* ========================================================================= */

vitte_constant_result_t
vitte_constant_cast(
    const vitte_constant_t *value,
    vitte_constant_kind_t destination)
{
    int64_t signed_value;
    uint64_t unsigned_value;
    double float_value;
    vitte_constant_t result;

    if (!vitte_constant_is_valid(value)) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_VALUE);
    }

    if (destination <= VITTE_CONSTANT_KIND_INVALID ||
        destination >= VITTE_CONSTANT_KIND_COUNT ||
        destination == VITTE_CONSTANT_KIND_POISON) {
        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_CAST);
    }

    if (value->kind == destination) {
        return vitte_constant_result_success(*value);
    }

    if (destination == VITTE_CONSTANT_KIND_BOOL) {
        bool truth;

        if (!vitte_constant_truthy(value, &truth)) {
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_CAST);
        }

        return
            vitte_constant_result_success(
                vitte_constant_bool(truth));
    }

    if (vitte_constant_kind_is_signed(destination)) {
        if (vitte_constant_get_signed(value, &signed_value)) {
            if (signed_value <
                    vitte_constant_signed_min(destination) ||
                signed_value >
                    vitte_constant_signed_max(destination)) {
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        destination,
                        signed_value));
        }

        if (vitte_constant_get_unsigned(value, &unsigned_value)) {
            if (unsigned_value >
                (uint64_t)vitte_constant_signed_max(destination)) {
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        destination,
                        (int64_t)unsigned_value));
        }

        if (vitte_constant_get_double(value, &float_value) &&
            vitte_constant_double_to_signed(
                float_value,
                destination,
                &signed_value)) {
            return
                vitte_constant_result_success(
                    vitte_constant_i64(
                        destination,
                        signed_value));
        }

        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_CAST);
    }

    if (vitte_constant_kind_is_unsigned(destination)) {
        if (vitte_constant_get_unsigned(value, &unsigned_value)) {
            if (unsigned_value >
                vitte_constant_unsigned_max(destination)) {
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        destination,
                        unsigned_value));
        }

        if (vitte_constant_get_signed(value, &signed_value)) {
            if (signed_value < 0 ||
                (uint64_t)signed_value >
                    vitte_constant_unsigned_max(destination)) {
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        destination,
                        (uint64_t)signed_value));
        }

        if (vitte_constant_get_double(value, &float_value) &&
            vitte_constant_double_to_unsigned(
                float_value,
                destination,
                &unsigned_value)) {
            return
                vitte_constant_result_success(
                    vitte_constant_u64(
                        destination,
                        unsigned_value));
        }

        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_CAST);
    }

    if (vitte_constant_kind_is_float(destination)) {
        if (!vitte_constant_get_double(value, &float_value)) {
            return
                vitte_constant_result_error(
                    VITTE_CONSTANT_ERROR_INVALID_CAST);
        }

        (void)memset(&result, 0, sizeof(result));
        result.kind = destination;

        if (destination == VITTE_CONSTANT_KIND_F32) {
            if (!isfinite(float_value) ||
                float_value > (double)FLT_MAX ||
                float_value < -(double)FLT_MAX) {
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_OVERFLOW);
            }

            result.as.f32 = (float)float_value;
        } else {
            result.as.f64 = float_value;
        }

        return vitte_constant_result_success(result);
    }

    if (destination == VITTE_CONSTANT_KIND_CHAR) {
        if (vitte_constant_get_unsigned(value, &unsigned_value)) {
            if (unsigned_value > UINT32_C(0x10ffff) ||
                (unsigned_value >= UINT32_C(0xd800) &&
                 unsigned_value <= UINT32_C(0xdfff))) {
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_CAST);
            }

            (void)memset(&result, 0, sizeof(result));
            result.kind = VITTE_CONSTANT_KIND_CHAR;
            result.as.character = (uint32_t)unsigned_value;

            return vitte_constant_result_success(result);
        }

        if (vitte_constant_get_signed(value, &signed_value)) {
            if (signed_value < 0 ||
                (uint64_t)signed_value > UINT64_C(0x10ffff) ||
                (signed_value >= INT64_C(0xd800) &&
                 signed_value <= INT64_C(0xdfff))) {
                return
                    vitte_constant_result_error(
                        VITTE_CONSTANT_ERROR_INVALID_CAST);
            }

            (void)memset(&result, 0, sizeof(result));
            result.kind = VITTE_CONSTANT_KIND_CHAR;
            result.as.character = (uint32_t)signed_value;

            return vitte_constant_result_success(result);
        }

        return
            vitte_constant_result_error(
                VITTE_CONSTANT_ERROR_INVALID_CAST);
    }

    if (destination == VITTE_CONSTANT_KIND_NULL &&
        value->kind == VITTE_CONSTANT_KIND_NULL) {
        return vitte_constant_result_success(*value);
    }

    return
        vitte_constant_result_error(
            VITTE_CONSTANT_ERROR_INVALID_CAST);
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_constant_stats_t
vitte_constant_stats(void)
{
    return vitte_constant_thread_stats;
}

void
vitte_constant_stats_reset(void)
{
    (void)memset(
        &vitte_constant_thread_stats,
        0,
        sizeof(vitte_constant_thread_stats));
}

/* ========================================================================= */
/* Internal self-validation helpers                                          */
/* ========================================================================= */

static bool
vitte_constant_result_is_valid(
    const vitte_constant_result_t *result)
{
    if (result == NULL) {
        return false;
    }

    switch (result->status) {
        case VITTE_CONSTANT_FOLD_SUCCESS:
            return
                result->error == VITTE_CONSTANT_ERROR_NONE &&
                vitte_constant_is_valid(&result->value) &&
                result->value.kind != VITTE_CONSTANT_KIND_POISON;

        case VITTE_CONSTANT_FOLD_NOT_CONSTANT:
            return result->error == VITTE_CONSTANT_ERROR_NONE;

        case VITTE_CONSTANT_FOLD_ERROR:
            return
                result->error != VITTE_CONSTANT_ERROR_NONE &&
                result->error < VITTE_CONSTANT_ERROR_COUNT;

        case VITTE_CONSTANT_FOLD_INVALID:
        case VITTE_CONSTANT_FOLD_COUNT:
            return false;
    }

    return false;
}

/*
 * Keep otherwise internal helpers referenced so strict builds using aggressive
 * unused-function diagnostics can retain the implementation while the public
 * header/API is split out.
 */
static void
vitte_constant_internal_reference(void)
{
    vitte_constant_result_t result;

    result = vitte_constant_result_not_constant();

    (void)vitte_constant_result_is_valid(&result);
}

/* ========================================================================= */
/* Translation-unit anchor                                                   */
/* ========================================================================= */

/*
 * This symbol gives the subsystem a small explicit anchor for integration
 * tests and ensures the internal self-reference path remains reachable.
 */
void
vitte_constant_translation_unit_anchor(void);

void
vitte_constant_translation_unit_anchor(void)
{
    vitte_constant_internal_reference();
}
