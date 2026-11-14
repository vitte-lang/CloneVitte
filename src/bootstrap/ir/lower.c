#include "lower.h"

#include <stdlib.h>
#include <string.h>

typedef struct vitte_ir_lower_symbol_value {
    vitte_hir_symbol_id_t symbol;
    vitte_ir_value_id_t value;
} vitte_ir_lower_symbol_value_t;

typedef struct vitte_ir_lower_function {
    vitte_hir_symbol_id_t symbol;
    vitte_ir_function_id_t function;
    vitte_ir_value_id_t function_value;
    vitte_ir_type_id_t return_type;
} vitte_ir_lower_function_t;

typedef struct vitte_ir_lower_loop {
    vitte_ir_block_id_t break_target;
    vitte_ir_block_id_t continue_target;
} vitte_ir_lower_loop_t;

typedef struct vitte_ir_lower_context {
    vitte_ir_context_t *ir;
    const vitte_hir_t *hir;
    vitte_ir_type_id_t *types;
    size_t type_count;
    vitte_ir_lower_function_t *functions;
    size_t function_count;
    vitte_ir_lower_symbol_value_t *values;
    size_t value_count;
    size_t value_capacity;
    vitte_ir_lower_loop_t *loops;
    size_t loop_count;
    size_t loop_capacity;
} vitte_ir_lower_context_t;

static vitte_ir_span_t
vitte_ir_lower_span(const vitte_hir_span_t *span)
{
    vitte_ir_span_t result;
    memset(&result, 0, sizeof(result));
    if (span != NULL) {
        result.file_id = span->file_id;
        result.begin = span->begin;
        result.end = span->end;
        result.valid = span->valid;
    }
    return result;
}

static vitte_ir_type_id_t
vitte_ir_lower_type(vitte_ir_lower_context_t *context,
                    vitte_hir_type_id_t id)
{
    const vitte_hir_type_t *source;
    vitte_ir_type_desc_t description;
    vitte_ir_type_id_t element;
    size_t index;
    if (id == VITTE_HIR_INVALID_ID || id > context->type_count) return VITTE_IR_INVALID_ID;
    if (context->types[id - 1u] != VITTE_IR_INVALID_ID) return context->types[id - 1u];
    source = vitte_hir_get_type(context->hir, id);
    if (source == NULL) return VITTE_IR_INVALID_ID;
    memset(&description, 0, sizeof(description));
    switch (source->kind) {
        case VITTE_HIR_TYPE_UNIT: description.kind = VITTE_IR_TYPE_VOID; break;
        case VITTE_HIR_TYPE_BOOL: description.kind = VITTE_IR_TYPE_BOOL; break;
        case VITTE_HIR_TYPE_CHAR: description.kind = VITTE_IR_TYPE_CHAR; break;
        case VITTE_HIR_TYPE_I8: description.kind = VITTE_IR_TYPE_I8; break;
        case VITTE_HIR_TYPE_I16: description.kind = VITTE_IR_TYPE_I16; break;
        case VITTE_HIR_TYPE_I32: description.kind = VITTE_IR_TYPE_I32; break;
        case VITTE_HIR_TYPE_I64: description.kind = VITTE_IR_TYPE_I64; break;
        case VITTE_HIR_TYPE_U8: description.kind = VITTE_IR_TYPE_U8; break;
        case VITTE_HIR_TYPE_U16: description.kind = VITTE_IR_TYPE_U16; break;
        case VITTE_HIR_TYPE_U32: description.kind = VITTE_IR_TYPE_U32; break;
        case VITTE_HIR_TYPE_U64: description.kind = VITTE_IR_TYPE_U64; break;
        case VITTE_HIR_TYPE_F32: description.kind = VITTE_IR_TYPE_F32; break;
        case VITTE_HIR_TYPE_F64: description.kind = VITTE_IR_TYPE_F64; break;
        case VITTE_HIR_TYPE_STRING: {
            vitte_ir_type_desc_t byte_desc;
            vitte_ir_type_id_t byte;
            memset(&byte_desc, 0, sizeof(byte_desc)); byte_desc.kind = VITTE_IR_TYPE_U8;
            byte = vitte_ir_intern_type(context->ir, &byte_desc);
            description.kind = VITTE_IR_TYPE_POINTER; description.element_type = byte;
            break;
        }
        case VITTE_HIR_TYPE_POINTER:
        case VITTE_HIR_TYPE_REFERENCE:
            element = vitte_ir_lower_type(context, source->element_type);
            if (element == VITTE_IR_INVALID_ID) return VITTE_IR_INVALID_ID;
            description.kind = VITTE_IR_TYPE_POINTER; description.element_type = element;
            break;
        case VITTE_HIR_TYPE_SLICE:
            element = vitte_ir_lower_type(context, source->element_type);
            if (element == VITTE_IR_INVALID_ID) return VITTE_IR_INVALID_ID;
            description.kind = VITTE_IR_TYPE_SLICE;
            description.element_type = element;
            break;
        case VITTE_HIR_TYPE_ARRAY:
            element = vitte_ir_lower_type(context, source->element_type);
            if (element == VITTE_IR_INVALID_ID) return VITTE_IR_INVALID_ID;
            description.kind = VITTE_IR_TYPE_ARRAY; description.element_type = element;
            description.array_length = source->array_length; break;
        default: description.kind = VITTE_IR_TYPE_OPAQUE; break;
    }
    if (description.kind == VITTE_IR_TYPE_FUNCTION) {
        description.parameter_count = source->parameter_count;
        description.parameter_types = (vitte_ir_type_id_t *)calloc(
            source->parameter_count, sizeof(*description.parameter_types));
        if (source->parameter_count != 0u && description.parameter_types == NULL) return VITTE_IR_INVALID_ID;
        for (index = 0u; index < source->parameter_count; ++index)
            ((vitte_ir_type_id_t *)description.parameter_types)[index] =
                vitte_ir_lower_type(context, source->parameters[index]);
        description.return_type = vitte_ir_lower_type(context, source->return_type);
    }
    context->types[id - 1u] = vitte_ir_intern_type(context->ir, &description);
    if (description.kind == VITTE_IR_TYPE_FUNCTION) free((void *)description.parameter_types);
    return context->types[id - 1u];
}

static vitte_ir_type_id_t
vitte_ir_lower_default_type(vitte_ir_lower_context_t *context)
{
    size_t index;
    if (context == NULL) return VITTE_IR_INVALID_ID;
    for (index = 1u; index <= context->type_count; ++index) {
        const vitte_hir_type_t *type = vitte_hir_get_type(context->hir, index);
        if (type != NULL && type->kind == VITTE_HIR_TYPE_I64) {
            return vitte_ir_lower_type(context, index);
        }
    }
    return VITTE_IR_INVALID_ID;
}

static vitte_ir_value_id_t
vitte_ir_lower_lookup_value(const vitte_ir_lower_context_t *context,
                            vitte_hir_symbol_id_t symbol)
{
    size_t index;
    for (index = context->value_count; index > 0u; --index)
        if (context->values[index - 1u].symbol == symbol) return context->values[index - 1u].value;
    return VITTE_IR_INVALID_ID;
}

static bool
vitte_ir_lower_bind_value(vitte_ir_lower_context_t *context,
                          vitte_hir_symbol_id_t symbol,
                          vitte_ir_value_id_t value)
{
    vitte_ir_lower_symbol_value_t *values;
    size_t capacity;
    if (symbol == VITTE_HIR_INVALID_ID || value == VITTE_IR_INVALID_ID) return false;
    if (context->value_count == context->value_capacity) {
        capacity = context->value_capacity == 0u ? 16u : context->value_capacity * 2u;
        values = (vitte_ir_lower_symbol_value_t *)realloc(
            context->values, capacity * sizeof(*values));
        if (values == NULL) return false;
        context->values = values; context->value_capacity = capacity;
    }
    context->values[context->value_count].symbol = symbol;
    context->values[context->value_count].value = value;
    ++context->value_count;
    return true;
}

static vitte_ir_opcode_t
vitte_ir_lower_binary(vitte_hir_binary_operator_t op, bool floating)
{
    switch (op) {
        case VITTE_HIR_BINARY_ADD: return floating ? VITTE_IR_OP_FADD : VITTE_IR_OP_ADD;
        case VITTE_HIR_BINARY_SUBTRACT: return floating ? VITTE_IR_OP_FSUB : VITTE_IR_OP_SUB;
        case VITTE_HIR_BINARY_MULTIPLY: return floating ? VITTE_IR_OP_FMUL : VITTE_IR_OP_MUL;
        case VITTE_HIR_BINARY_DIVIDE: return floating ? VITTE_IR_OP_FDIV : VITTE_IR_OP_SDIV;
        case VITTE_HIR_BINARY_REMAINDER: return floating ? VITTE_IR_OP_FREM : VITTE_IR_OP_SREM;
        case VITTE_HIR_BINARY_BIT_AND: return VITTE_IR_OP_AND;
        case VITTE_HIR_BINARY_BIT_OR: return VITTE_IR_OP_OR;
        case VITTE_HIR_BINARY_BIT_XOR: return VITTE_IR_OP_XOR;
        case VITTE_HIR_BINARY_SHIFT_LEFT: return VITTE_IR_OP_SHL;
        case VITTE_HIR_BINARY_SHIFT_RIGHT: return VITTE_IR_OP_ASHR;
        case VITTE_HIR_BINARY_EQUAL: return floating ? VITTE_IR_OP_FCMP_EQ : VITTE_IR_OP_ICMP_EQ;
        case VITTE_HIR_BINARY_NOT_EQUAL: return floating ? VITTE_IR_OP_FCMP_NE : VITTE_IR_OP_ICMP_NE;
        case VITTE_HIR_BINARY_LESS: return floating ? VITTE_IR_OP_FCMP_LT : VITTE_IR_OP_ICMP_SLT;
        case VITTE_HIR_BINARY_LESS_EQUAL: return floating ? VITTE_IR_OP_FCMP_LE : VITTE_IR_OP_ICMP_SLE;
        case VITTE_HIR_BINARY_GREATER: return floating ? VITTE_IR_OP_FCMP_GT : VITTE_IR_OP_ICMP_SGT;
        case VITTE_HIR_BINARY_GREATER_EQUAL: return floating ? VITTE_IR_OP_FCMP_GE : VITTE_IR_OP_ICMP_SGE;
        case VITTE_HIR_BINARY_AND: return VITTE_IR_OP_AND;
        case VITTE_HIR_BINARY_OR: return VITTE_IR_OP_OR;
        default: return VITTE_IR_OP_INVALID;
    }
}

static vitte_ir_value_id_t vitte_ir_lower_expr(vitte_ir_lower_context_t *, vitte_ir_function_id_t,
                                                vitte_ir_block_id_t, vitte_hir_node_id_t);

static bool
vitte_ir_lower_is_contextual_literal(const vitte_hir_node_t *node)
{
    return node != NULL && node->kind == VITTE_HIR_NODE_LITERAL &&
        (node->as.literal.value.kind == VITTE_HIR_LITERAL_SIGNED_INTEGER ||
         node->as.literal.value.kind == VITTE_HIR_LITERAL_UNSIGNED_INTEGER ||
         node->as.literal.value.kind == VITTE_HIR_LITERAL_F32 ||
         node->as.literal.value.kind == VITTE_HIR_LITERAL_F64 ||
         node->as.literal.value.kind == VITTE_HIR_LITERAL_NULL);
}

static vitte_ir_value_id_t
vitte_ir_lower_expr_as(vitte_ir_lower_context_t *context,
                       vitte_ir_function_id_t function,
                       vitte_ir_block_id_t block,
                       vitte_hir_node_id_t id,
                       vitte_ir_type_id_t expected_type)
{
    const vitte_hir_node_t *node = vitte_hir_get_node(context->hir, id);
    const vitte_ir_type_t *type = vitte_ir_get_type(context->ir, expected_type);

    if (node == NULL || type == NULL) return VITTE_IR_INVALID_ID;
    if (node->kind == VITTE_HIR_NODE_LITERAL) {
        if (node->as.literal.value.kind == VITTE_HIR_LITERAL_NULL &&
            type->kind == VITTE_IR_TYPE_POINTER) {
            return vitte_ir_null(context->ir, expected_type);
        }
        if (node->as.literal.value.kind == VITTE_HIR_LITERAL_SIGNED_INTEGER &&
            vitte_ir_type_kind_is_integer(type->kind)) {
            return vitte_ir_const_integer(
                context->ir,
                expected_type,
                (uint64_t)node->as.literal.value.as.signed_integer);
        }
        if (node->as.literal.value.kind == VITTE_HIR_LITERAL_UNSIGNED_INTEGER &&
            vitte_ir_type_kind_is_integer(type->kind)) {
            return vitte_ir_const_integer(
                context->ir,
                expected_type,
                node->as.literal.value.as.unsigned_integer);
        }
        if ((node->as.literal.value.kind == VITTE_HIR_LITERAL_F32 ||
             node->as.literal.value.kind == VITTE_HIR_LITERAL_F64) &&
            (type->kind == VITTE_IR_TYPE_F32 ||
             type->kind == VITTE_IR_TYPE_F64)) {
            return vitte_ir_const_float(
                context->ir,
                expected_type,
                node->as.literal.value.as.f64);
        }
    }
    if (node->kind == VITTE_HIR_NODE_UNARY &&
        node->as.unary.op == VITTE_HIR_UNARY_NEGATE &&
        (type->kind == VITTE_IR_TYPE_F32 ||
         type->kind == VITTE_IR_TYPE_F64)) {
        const vitte_hir_node_t *operand = vitte_hir_get_node(
            context->hir, node->as.unary.operand);
        if (operand != NULL && operand->kind == VITTE_HIR_NODE_LITERAL &&
            (operand->as.literal.value.kind == VITTE_HIR_LITERAL_F32 ||
             operand->as.literal.value.kind == VITTE_HIR_LITERAL_F64)) {
            return vitte_ir_const_float(
                context->ir,
                expected_type,
                -operand->as.literal.value.as.f64);
        }
    }
    return vitte_ir_lower_expr(context, function, block, id);
}

static vitte_ir_value_id_t
vitte_ir_lower_constant_value(vitte_ir_lower_context_t *context,
                              vitte_hir_node_id_t id)
{
    const vitte_hir_node_t *node = vitte_hir_get_node(context->hir, id);
    vitte_ir_type_id_t type;
    const vitte_hir_string_t *string;
    vitte_ir_string_id_t ir_string;
    if (node == NULL || node->kind != VITTE_HIR_NODE_LITERAL) return VITTE_IR_INVALID_ID;
    type = vitte_ir_lower_type(context, node->type);
    if (type == VITTE_IR_INVALID_ID) type = vitte_ir_lower_default_type(context);
    if (type == VITTE_IR_INVALID_ID) return VITTE_IR_INVALID_ID;
    switch (node->as.literal.value.kind) {
        case VITTE_HIR_LITERAL_BOOL:
            return vitte_ir_const_bool(context->ir, type, node->as.literal.value.as.boolean);
        case VITTE_HIR_LITERAL_F32:
        case VITTE_HIR_LITERAL_F64:
            return vitte_ir_const_float(context->ir, type, node->as.literal.value.as.f64);
        case VITTE_HIR_LITERAL_SIGNED_INTEGER:
            return vitte_ir_const_integer(context->ir, type,
                (uint64_t)node->as.literal.value.as.signed_integer);
        case VITTE_HIR_LITERAL_UNSIGNED_INTEGER:
            return vitte_ir_const_integer(context->ir, type,
                node->as.literal.value.as.unsigned_integer);
        case VITTE_HIR_LITERAL_CHAR:
            return vitte_ir_const_integer(context->ir, type,
                node->as.literal.value.as.character);
        case VITTE_HIR_LITERAL_STRING:
            string = vitte_hir_get_string(context->hir, node->as.literal.value.as.string);
            if (string == NULL) return VITTE_IR_INVALID_ID;
            ir_string = vitte_ir_intern_string(context->ir, string->data, string->length);
            return ir_string == VITTE_IR_INVALID_ID
                ? VITTE_IR_INVALID_ID : vitte_ir_const_string(context->ir, type, ir_string);
        case VITTE_HIR_LITERAL_NULL:
            return vitte_ir_null(context->ir, type);
        default:
            return VITTE_IR_INVALID_ID;
    }
}

static bool
vitte_ir_lower_add_loop(vitte_ir_lower_context_t *context,
                        vitte_ir_block_id_t break_target,
                        vitte_ir_block_id_t continue_target)
{
    vitte_ir_lower_loop_t *loops;
    size_t capacity;
    if (context->loop_count == context->loop_capacity) {
        capacity = context->loop_capacity == 0u ? 4u : context->loop_capacity * 2u;
        loops = (vitte_ir_lower_loop_t *)realloc(
            context->loops, capacity * sizeof(*loops));
        if (loops == NULL) return false;
        context->loops = loops;
        context->loop_capacity = capacity;
    }
    context->loops[context->loop_count].break_target = break_target;
    context->loops[context->loop_count].continue_target = continue_target;
    ++context->loop_count;
    return true;
}

static vitte_ir_block_id_t
vitte_ir_lower_new_block(vitte_ir_lower_context_t *context,
                         vitte_ir_function_id_t function,
                         const char *name)
{
    vitte_ir_string_id_t string;
    if (name == NULL) return VITTE_IR_INVALID_ID;
    string = vitte_ir_intern_string(context->ir, name, strlen(name));
    if (string == VITTE_IR_INVALID_ID) return VITTE_IR_INVALID_ID;
    return vitte_ir_add_block(context->ir, function, string, (vitte_ir_span_t){0});
}

static vitte_ir_value_id_t
vitte_ir_lower_expr(vitte_ir_lower_context_t *context,
                    vitte_ir_function_id_t function,
                    vitte_ir_block_id_t block,
                    vitte_hir_node_id_t id);

static bool
vitte_ir_lower_member_is(const vitte_ir_lower_context_t *context,
                         vitte_hir_symbol_id_t symbol_id,
                         const char *name,
                         size_t name_length)
{
    const vitte_hir_symbol_t *symbol;
    const vitte_hir_string_t *text;

    if (context == NULL || name == NULL) return false;
    symbol = vitte_hir_get_symbol(context->hir, symbol_id);
    text = symbol == NULL
        ? NULL : vitte_hir_get_string(context->hir, symbol->name);
    return text != NULL && text->length == name_length &&
        memcmp(text->data, name, name_length) == 0;
}

static bool
vitte_ir_lower_is_string(vitte_ir_lower_context_t *context,
                         vitte_hir_node_id_t id)
{
    const vitte_hir_node_t *node = vitte_hir_get_node(context->hir, id);
    const vitte_hir_type_t *type = node == NULL
        ? NULL : vitte_hir_get_type(context->hir, node->type);
    return type != NULL && type->kind == VITTE_HIR_TYPE_STRING;
}

static bool
vitte_ir_lower_is_slice(vitte_ir_lower_context_t *context,
                        vitte_hir_node_id_t id)
{
    const vitte_hir_node_t *node = vitte_hir_get_node(context->hir, id);
    const vitte_hir_type_t *type = node == NULL
        ? NULL : vitte_hir_get_type(context->hir, node->type);
    return type != NULL && type->kind == VITTE_HIR_TYPE_SLICE;
}

static vitte_ir_value_id_t
vitte_ir_lower_string_operation(vitte_ir_lower_context_t *context,
                                vitte_ir_block_id_t block,
                                vitte_ir_opcode_t opcode,
                                vitte_ir_type_id_t result_type,
                                const vitte_ir_value_id_t *operands,
                                size_t operand_count,
                                const vitte_hir_span_t *span)
{
    vitte_ir_instruction_desc_t description;
    vitte_ir_instruction_id_t instruction;

    memset(&description, 0, sizeof(description));
    description.opcode = opcode;
    description.block = block;
    description.result_type = result_type;
    description.operands = operands;
    description.operand_count = operand_count;
    description.span = vitte_ir_lower_span(span);
    instruction = vitte_ir_add_instruction(context->ir, &description);
    return vitte_ir_instruction_result(context->ir, instruction);
}

static vitte_ir_value_id_t
vitte_ir_lower_index_address(vitte_ir_lower_context_t *context,
                             vitte_ir_function_id_t function,
                             vitte_ir_block_id_t block,
                             vitte_hir_node_id_t id);

static vitte_ir_value_id_t
vitte_ir_lower_expr(vitte_ir_lower_context_t *context,
                    vitte_ir_function_id_t function,
                    vitte_ir_block_id_t block,
                    vitte_hir_node_id_t id)
{
    const vitte_hir_node_t *node = vitte_hir_get_node(context->hir, id);
    vitte_ir_type_id_t type;
    vitte_ir_value_id_t left, right, callee;
    vitte_ir_instruction_id_t instruction;
    vitte_ir_instruction_desc_t description;
    vitte_ir_value_id_t *operands;
    size_t index;
    if (node == NULL) return VITTE_IR_INVALID_ID;
    type = vitte_ir_lower_type(context, node->type);
    if (type == VITTE_IR_INVALID_ID) type = vitte_ir_lower_default_type(context);
    if (type == VITTE_IR_INVALID_ID) return VITTE_IR_INVALID_ID;
    switch (node->kind) {
        case VITTE_HIR_NODE_LITERAL:
            switch (node->as.literal.value.kind) {
                case VITTE_HIR_LITERAL_BOOL: return vitte_ir_const_bool(context->ir, type, node->as.literal.value.as.boolean);
                case VITTE_HIR_LITERAL_F32:
                case VITTE_HIR_LITERAL_F64: return vitte_ir_const_float(context->ir, type, node->as.literal.value.as.f64);
                case VITTE_HIR_LITERAL_STRING: {
                    const vitte_hir_string_t *string = vitte_hir_get_string(
                        context->hir, node->as.literal.value.as.string);
                    vitte_ir_string_id_t ir_string;
                    if (string == NULL) return VITTE_IR_INVALID_ID;
                    ir_string = vitte_ir_intern_string(
                        context->ir, string->data, string->length);
                    return ir_string == VITTE_IR_INVALID_ID
                        ? VITTE_IR_INVALID_ID
                        : vitte_ir_const_string(context->ir, type, ir_string);
                }
                case VITTE_HIR_LITERAL_SIGNED_INTEGER:
                case VITTE_HIR_LITERAL_UNSIGNED_INTEGER: return vitte_ir_const_integer(context->ir, type, (uint64_t)node->as.literal.value.as.signed_integer);
                case VITTE_HIR_LITERAL_CHAR: return vitte_ir_const_integer(context->ir, type, node->as.literal.value.as.character);
                case VITTE_HIR_LITERAL_NULL: return vitte_ir_null(context->ir, type);
                default: return vitte_ir_const_integer(context->ir, type, 0u);
            }
        case VITTE_HIR_NODE_SYMBOL:
            return vitte_ir_lower_lookup_value(context, node->as.symbol_ref.symbol);
        case VITTE_HIR_NODE_UNARY:
            left = vitte_ir_lower_expr(context, function, block, node->as.unary.operand);
            if (left == VITTE_IR_INVALID_ID) return left;
            if (node->as.unary.op == VITTE_HIR_UNARY_PLUS) return left;
            if (node->as.unary.op == VITTE_HIR_UNARY_BIT_NOT) {
                const vitte_ir_type_t *integer_type = vitte_ir_get_type(context->ir, type);
                vitte_ir_value_id_t mask;
                if (integer_type == NULL ||
                    (integer_type->kind < VITTE_IR_TYPE_I8 ||
                     integer_type->kind > VITTE_IR_TYPE_U64)) return VITTE_IR_INVALID_ID;
                mask = vitte_ir_const_integer(context->ir, type, UINT64_MAX);
                if (mask == VITTE_IR_INVALID_ID) return mask;
                instruction = vitte_ir_build_binary(context->ir, VITTE_IR_OP_XOR,
                    block, type, left, mask, vitte_ir_lower_span(&node->span));
                return vitte_ir_instruction_result(context->ir, instruction);
            }
            instruction = vitte_ir_build_unary(context->ir,
                node->as.unary.op == VITTE_HIR_UNARY_NOT ? VITTE_IR_OP_NOT :
                    (node->as.unary.op == VITTE_HIR_UNARY_NEGATE ? VITTE_IR_OP_NEG : VITTE_IR_OP_INVALID),
                block, type, left, vitte_ir_lower_span(&node->span));
            return vitte_ir_instruction_result(context->ir, instruction);
        case VITTE_HIR_NODE_BINARY: {
            const vitte_hir_node_t *left_node = vitte_hir_get_node(
                context->hir, node->as.binary.left);
            const vitte_hir_node_t *right_node = vitte_hir_get_node(
                context->hir, node->as.binary.right);
            const vitte_ir_value_t *left_value;
            vitte_ir_type_id_t expected_type;

            if (node->as.binary.op == VITTE_HIR_BINARY_ADD &&
                vitte_ir_lower_is_string(context, node->as.binary.left) &&
                vitte_ir_lower_is_string(context, node->as.binary.right)) {
                vitte_ir_value_id_t string_operands[2];
                string_operands[0] = vitte_ir_lower_expr(
                    context, function, block, node->as.binary.left);
                string_operands[1] = vitte_ir_lower_expr(
                    context, function, block, node->as.binary.right);
                if (string_operands[0] == VITTE_IR_INVALID_ID ||
                    string_operands[1] == VITTE_IR_INVALID_ID) {
                    return VITTE_IR_INVALID_ID;
                }
                return vitte_ir_lower_string_operation(
                    context, block, VITTE_IR_OP_STRING_CONCAT, type,
                    string_operands, 2u, &node->span);
            }

            if (vitte_ir_lower_is_contextual_literal(left_node) &&
                !vitte_ir_lower_is_contextual_literal(right_node)) {
                expected_type = right_node == NULL
                    ? VITTE_IR_INVALID_ID
                    : vitte_ir_lower_type(context, right_node->type);
                left = vitte_ir_lower_expr_as(
                    context, function, block, node->as.binary.left,
                    expected_type);
                right = vitte_ir_lower_expr(
                    context, function, block, node->as.binary.right);
            } else {
                left = vitte_ir_lower_expr(
                    context, function, block, node->as.binary.left);
                left_value = left == VITTE_IR_INVALID_ID
                    ? NULL : vitte_ir_get_value(context->ir, left);
                expected_type = left_value == NULL
                    ? VITTE_IR_INVALID_ID : left_value->type;
                if (vitte_ir_lower_is_contextual_literal(right_node)) {
                    right = vitte_ir_lower_expr_as(
                        context, function, block, node->as.binary.right,
                        expected_type);
                } else {
                    right = vitte_ir_lower_expr(
                        context, function, block, node->as.binary.right);
                }
            }
            if (left == VITTE_IR_INVALID_ID || right == VITTE_IR_INVALID_ID) return VITTE_IR_INVALID_ID;
            instruction = vitte_ir_build_binary(context->ir,
                vitte_ir_lower_binary(node->as.binary.op,
                    vitte_hir_get_type(context->hir, node->type) != NULL &&
                    (vitte_hir_get_type(context->hir, node->type)->kind == VITTE_HIR_TYPE_F32 ||
                     vitte_hir_get_type(context->hir, node->type)->kind == VITTE_HIR_TYPE_F64)),
                block, type, left, right, vitte_ir_lower_span(&node->span));
            return vitte_ir_instruction_result(context->ir, instruction);
        }
        case VITTE_HIR_NODE_CALL: {
            const vitte_ir_value_t *callee_ir_value;
            const vitte_ir_type_t *callee_signature;
            const vitte_hir_node_t *callee_node = vitte_hir_get_node(
                context->hir, node->as.call.callee);
            if (callee_node != NULL && callee_node->kind == VITTE_HIR_NODE_MEMBER) {
                bool receiver_is_string = vitte_ir_lower_is_string(
                    context, callee_node->as.member.object);
                bool receiver_is_slice = vitte_ir_lower_is_slice(
                    context, callee_node->as.member.object);
                if (vitte_ir_lower_member_is(
                        context, callee_node->as.member.member, "len", 3u) &&
                    node->as.call.arguments.count == 0u &&
                    (receiver_is_string || receiver_is_slice)) {
                    left = vitte_ir_lower_expr(
                        context, function, block, callee_node->as.member.object);
                    if (left == VITTE_IR_INVALID_ID) return left;
                    if (receiver_is_slice) {
                        return vitte_ir_lower_string_operation(
                            context, block, VITTE_IR_OP_SLICE_LENGTH, type,
                            &left, 1u, &node->span);
                    }
                    return vitte_ir_lower_string_operation(
                        context, block, VITTE_IR_OP_STRING_LENGTH, type,
                        &left, 1u, &node->span);
                }
                if (vitte_ir_lower_member_is(
                        context, callee_node->as.member.member, "slice", 5u) &&
                    node->as.call.arguments.count == 2u && receiver_is_string) {
                    vitte_ir_value_id_t arguments[3];
                    arguments[0] = vitte_ir_lower_expr(
                        context, function, block, callee_node->as.member.object);
                    arguments[1] = vitte_ir_lower_expr(
                        context, function, block, node->as.call.arguments.items[0]);
                    arguments[2] = vitte_ir_lower_expr(
                        context, function, block, node->as.call.arguments.items[1]);
                    if (arguments[0] == VITTE_IR_INVALID_ID ||
                        arguments[1] == VITTE_IR_INVALID_ID ||
                        arguments[2] == VITTE_IR_INVALID_ID) return VITTE_IR_INVALID_ID;
                    return vitte_ir_lower_string_operation(
                        context, block, VITTE_IR_OP_STRING_SLICE, type,
                        arguments, 3u, &node->span);
                }
            }
            callee = vitte_ir_lower_expr(context, function, block, node->as.call.callee);
            if (callee == VITTE_IR_INVALID_ID) return callee;
            callee_ir_value = vitte_ir_get_value(context->ir, callee);
            callee_signature = callee_ir_value == NULL
                ? NULL : vitte_ir_get_type(context->ir, callee_ir_value->type);
            operands = (vitte_ir_value_id_t *)calloc(node->as.call.arguments.count + 1u, sizeof(*operands));
            if (operands == NULL) return VITTE_IR_INVALID_ID;
            operands[0] = callee;
            for (index = 0u; index < node->as.call.arguments.count; ++index) {
                if (callee_signature != NULL &&
                    callee_signature->kind == VITTE_IR_TYPE_FUNCTION &&
                    index < callee_signature->parameter_count) {
                    operands[index + 1u] = vitte_ir_lower_expr_as(
                        context, function, block,
                        node->as.call.arguments.items[index],
                        callee_signature->parameter_types[index]);
                } else {
                    operands[index + 1u] = vitte_ir_lower_expr(
                        context, function, block,
                        node->as.call.arguments.items[index]);
                }
                if (operands[index + 1u] == VITTE_IR_INVALID_ID) { free(operands); return VITTE_IR_INVALID_ID; }
            }
            memset(&description, 0, sizeof(description)); description.opcode = VITTE_IR_OP_CALL;
            description.block = block; description.result_type = type; description.operands = operands;
            description.operand_count = node->as.call.arguments.count + 1u;
            description.span = vitte_ir_lower_span(&node->span);
            instruction = vitte_ir_add_instruction(context->ir, &description);
            free(operands);
            return vitte_ir_instruction_result(context->ir, instruction);
        }
        case VITTE_HIR_NODE_MEMBER:
            if (vitte_ir_lower_member_is(
                    context, node->as.member.member, "length", 6u) &&
                (vitte_ir_lower_is_string(context, node->as.member.object) ||
                 vitte_ir_lower_is_slice(context, node->as.member.object))) {
                left = vitte_ir_lower_expr(
                    context, function, block, node->as.member.object);
                if (left == VITTE_IR_INVALID_ID) return left;
                if (vitte_ir_lower_is_slice(context, node->as.member.object)) {
                    return vitte_ir_lower_string_operation(
                        context, block, VITTE_IR_OP_SLICE_LENGTH, type,
                        &left, 1u, &node->span);
                }
                return vitte_ir_lower_string_operation(
                    context, block, VITTE_IR_OP_STRING_LENGTH, type,
                    &left, 1u, &node->span);
            }
            return VITTE_IR_INVALID_ID;
        case VITTE_HIR_NODE_INDEX: {
            vitte_ir_value_id_t address = vitte_ir_lower_index_address(
                context, function, block, id);
            if (address == VITTE_IR_INVALID_ID) return address;
            memset(&description, 0, sizeof(description));
            description.opcode = VITTE_IR_OP_LOAD;
            description.block = block;
            description.result_type = type;
            description.operands = &address;
            description.operand_count = 1u;
            description.span = vitte_ir_lower_span(&node->span);
            instruction = vitte_ir_add_instruction(context->ir, &description);
            return vitte_ir_instruction_result(context->ir, instruction);
        }
        case VITTE_HIR_NODE_DEREF:
            left = vitte_ir_lower_expr(
                context, function, block,
                node->as.unary_expression.expression);
            if (left == VITTE_IR_INVALID_ID) return left;
            memset(&description, 0, sizeof(description));
            description.opcode = VITTE_IR_OP_LOAD;
            description.block = block;
            description.result_type = type;
            description.operands = &left;
            description.operand_count = 1u;
            description.span = vitte_ir_lower_span(&node->span);
            instruction = vitte_ir_add_instruction(context->ir, &description);
            return vitte_ir_instruction_result(context->ir, instruction);
        case VITTE_HIR_NODE_CAST:
            left = vitte_ir_lower_expr(context, function, block, node->as.cast.expression);
            if (left == VITTE_IR_INVALID_ID) return left;
            memset(&description, 0, sizeof(description)); description.opcode = VITTE_IR_OP_BITCAST;
            description.block = block; description.result_type = vitte_ir_lower_type(context, node->as.cast.destination_type);
            description.operands = &left; description.operand_count = 1u; description.span = vitte_ir_lower_span(&node->span);
            instruction = vitte_ir_add_instruction(context->ir, &description);
            return vitte_ir_instruction_result(context->ir, instruction);
        default: return VITTE_IR_INVALID_ID;
    }
}

static vitte_ir_value_id_t
vitte_ir_lower_index_address(vitte_ir_lower_context_t *context,
                             vitte_ir_function_id_t function,
                             vitte_ir_block_id_t block,
                             vitte_hir_node_id_t id)
{
    const vitte_hir_node_t *node = vitte_hir_get_node(context->hir, id);
    const vitte_ir_value_t *object_value;
    const vitte_ir_type_t *object_type;
    vitte_ir_type_id_t address_type;
    vitte_ir_value_id_t operands[2];
    vitte_ir_instruction_desc_t description;
    vitte_ir_instruction_id_t instruction;
    if (node == NULL || node->kind != VITTE_HIR_NODE_INDEX) {
        return VITTE_IR_INVALID_ID;
    }
    operands[0] = vitte_ir_lower_expr(
        context, function, block, node->as.index.object);
    operands[1] = vitte_ir_lower_expr(
        context, function, block, node->as.index.index);
    if (operands[0] == VITTE_IR_INVALID_ID ||
        operands[1] == VITTE_IR_INVALID_ID) {
        return VITTE_IR_INVALID_ID;
    }
    object_value = vitte_ir_get_value(context->ir, operands[0]);
    object_type = object_value == NULL
        ? NULL : vitte_ir_get_type(context->ir, object_value->type);
    if (object_type == NULL) {
        return VITTE_IR_INVALID_ID;
    }
    if (object_type->kind == VITTE_IR_TYPE_POINTER) {
        address_type = object_value->type;
    } else if (object_type->kind == VITTE_IR_TYPE_SLICE) {
        vitte_ir_type_desc_t pointer_description;
        vitte_ir_value_id_t slice_value = operands[0];
        vitte_ir_instruction_desc_t slice_description;
        vitte_ir_instruction_id_t slice_instruction;

        memset(&pointer_description, 0, sizeof(pointer_description));
        pointer_description.kind = VITTE_IR_TYPE_POINTER;
        pointer_description.element_type = object_type->element_type;
        address_type = vitte_ir_intern_type(context->ir, &pointer_description);
        if (address_type == VITTE_IR_INVALID_ID) return address_type;

        memset(&slice_description, 0, sizeof(slice_description));
        slice_description.opcode = VITTE_IR_OP_SLICE_DATA;
        slice_description.block = block;
        slice_description.result_type = address_type;
        slice_description.operands = &slice_value;
        slice_description.operand_count = 1u;
        slice_description.span = vitte_ir_lower_span(&node->span);
        slice_instruction = vitte_ir_add_instruction(
            context->ir, &slice_description);
        operands[0] = vitte_ir_instruction_result(
            context->ir, slice_instruction);
        if (operands[0] == VITTE_IR_INVALID_ID) return operands[0];
    } else if (object_type->kind == VITTE_IR_TYPE_ARRAY) {
        vitte_ir_type_desc_t pointer_description;
        memset(&pointer_description, 0, sizeof(pointer_description));
        pointer_description.kind = VITTE_IR_TYPE_POINTER;
        pointer_description.element_type = object_type->element_type;
        address_type = vitte_ir_intern_type(context->ir, &pointer_description);
        if (address_type == VITTE_IR_INVALID_ID) return address_type;
    } else {
        return VITTE_IR_INVALID_ID;
    }
    memset(&description, 0, sizeof(description));
    description.opcode = VITTE_IR_OP_GEP;
    description.block = block;
    description.result_type = address_type;
    description.operands = operands;
    description.operand_count = 2u;
    description.span = vitte_ir_lower_span(&node->span);
    instruction = vitte_ir_add_instruction(context->ir, &description);
    return vitte_ir_instruction_result(context->ir, instruction);
}

static bool vitte_ir_lower_block(vitte_ir_lower_context_t *, vitte_ir_function_id_t,
                                 vitte_ir_block_id_t, vitte_hir_node_id_t,
                                 vitte_ir_block_id_t *);
static bool vitte_ir_lower_if_statement(vitte_ir_lower_context_t *,
                                        vitte_ir_function_id_t,
                                        vitte_ir_block_id_t,
                                        vitte_hir_node_id_t,
                                        vitte_ir_block_id_t *);

static bool
vitte_ir_lower_branch(vitte_ir_lower_context_t *context,
                      vitte_ir_function_id_t function,
                      vitte_ir_block_id_t block,
                      vitte_hir_node_id_t id,
                      vitte_ir_block_id_t *fallthrough)
{
    const vitte_hir_node_t *node = vitte_hir_get_node(context->hir, id);

    if (node == NULL) return false;
    if (node->kind == VITTE_HIR_NODE_BLOCK) {
        return vitte_ir_lower_block(
            context, function, block, id, fallthrough);
    }
    if (node->kind == VITTE_HIR_NODE_IF) {
        return vitte_ir_lower_if_statement(
            context, function, block, id, fallthrough);
    }
    return false;
}

static bool
vitte_ir_lower_for_statement(vitte_ir_lower_context_t *context,
                             vitte_ir_function_id_t function,
                             vitte_ir_block_id_t block,
                             const vitte_hir_node_t *statement,
                             vitte_ir_block_id_t *fallthrough)
{
    const vitte_hir_node_t *iterator;
    const vitte_hir_node_t *pattern;
    const vitte_hir_type_t *array_type;
    vitte_ir_type_id_t array_ir_type;
    vitte_ir_type_id_t element_ir_type;
    vitte_ir_type_id_t index_ir_type;
    vitte_ir_type_desc_t pointer_description;
    vitte_ir_value_id_t array_value;
    vitte_ir_value_id_t initial_index;
    vitte_ir_value_id_t end_index;
    vitte_ir_value_id_t index_value;
    vitte_ir_value_id_t condition;
    vitte_ir_value_id_t element_address;
    vitte_ir_value_id_t element_value;
    vitte_ir_value_id_t next_index;
    vitte_ir_instruction_desc_t instruction_description;
    vitte_ir_instruction_id_t phi_instruction;
    vitte_ir_instruction_id_t instruction;
    vitte_ir_block_id_t condition_block;
    vitte_ir_block_id_t body_block;
    vitte_ir_block_id_t increment_block;
    vitte_ir_block_id_t exit_block;
    vitte_ir_block_id_t body_end;
    vitte_ir_value_id_t gep_operands[2];
    vitte_ir_span_t span;

    if (statement == NULL || statement->kind != VITTE_HIR_NODE_FOR ||
        fallthrough == NULL) return false;
    iterator = vitte_hir_get_node(context->hir, statement->as.for_stmt.iterator);
    pattern = vitte_hir_get_node(context->hir, statement->as.for_stmt.pattern);
    array_type = iterator == NULL
        ? NULL : vitte_hir_get_type(context->hir, iterator->type);
    if (array_type == NULL || array_type->kind != VITTE_HIR_TYPE_ARRAY ||
        pattern == NULL || pattern->kind != VITTE_HIR_NODE_PATTERN_BINDING) {
        return false;
    }

    array_ir_type = vitte_ir_lower_type(context, iterator->type);
    element_ir_type = vitte_ir_lower_type(context, array_type->element_type);
    index_ir_type = vitte_ir_lower_default_type(context);
    array_value = vitte_ir_lower_expr(
        context, function, block, statement->as.for_stmt.iterator);
    if (array_ir_type == VITTE_IR_INVALID_ID ||
        element_ir_type == VITTE_IR_INVALID_ID ||
        index_ir_type == VITTE_IR_INVALID_ID ||
        array_value == VITTE_IR_INVALID_ID) return false;

    condition_block = vitte_ir_lower_new_block(context, function, "for.cond");
    body_block = vitte_ir_lower_new_block(context, function, "for.body");
    increment_block = vitte_ir_lower_new_block(context, function, "for.increment");
    exit_block = vitte_ir_lower_new_block(context, function, "for.exit");
    if (condition_block == VITTE_IR_INVALID_ID ||
        body_block == VITTE_IR_INVALID_ID ||
        increment_block == VITTE_IR_INVALID_ID ||
        exit_block == VITTE_IR_INVALID_ID) return false;
    span = vitte_ir_lower_span(&statement->span);
    initial_index = vitte_ir_const_integer(context->ir, index_ir_type, 0u);
    end_index = vitte_ir_const_integer(
        context->ir, index_ir_type, (uint64_t)array_type->array_length);
    if (initial_index == VITTE_IR_INVALID_ID ||
        end_index == VITTE_IR_INVALID_ID ||
        vitte_ir_build_br(context->ir, block, condition_block, span) ==
            VITTE_IR_INVALID_ID) return false;

    memset(&instruction_description, 0, sizeof(instruction_description));
    instruction_description.opcode = VITTE_IR_OP_PHI;
    instruction_description.block = condition_block;
    instruction_description.result_type = index_ir_type;
    instruction_description.span = span;
    phi_instruction = vitte_ir_add_instruction(
        context->ir, &instruction_description);
    index_value = vitte_ir_instruction_result(context->ir, phi_instruction);
    if (index_value == VITTE_IR_INVALID_ID ||
        !vitte_ir_phi_add_incoming(
            context->ir, phi_instruction, block, initial_index)) return false;
    instruction = vitte_ir_build_binary(
        context->ir, VITTE_IR_OP_ICMP_ULT, condition_block,
        vitte_ir_intern_type(context->ir, &(vitte_ir_type_desc_t){
            .kind = VITTE_IR_TYPE_BOOL
        }), index_value, end_index, span);
    condition = vitte_ir_instruction_result(context->ir, instruction);
    if (condition == VITTE_IR_INVALID_ID ||
        vitte_ir_build_cond_br(
            context->ir, condition_block, condition,
            body_block, exit_block, span) == VITTE_IR_INVALID_ID) return false;

    memset(&pointer_description, 0, sizeof(pointer_description));
    pointer_description.kind = VITTE_IR_TYPE_POINTER;
    pointer_description.element_type = element_ir_type;
    gep_operands[0] = array_value;
    gep_operands[1] = index_value;
    memset(&instruction_description, 0, sizeof(instruction_description));
    instruction_description.opcode = VITTE_IR_OP_GEP;
    instruction_description.block = body_block;
    instruction_description.result_type =
        vitte_ir_intern_type(context->ir, &pointer_description);
    instruction_description.operands = gep_operands;
    instruction_description.operand_count = 2u;
    instruction_description.span = span;
    instruction = vitte_ir_add_instruction(
        context->ir, &instruction_description);
    element_address = vitte_ir_instruction_result(context->ir, instruction);
    if (element_address == VITTE_IR_INVALID_ID) return false;
    memset(&instruction_description, 0, sizeof(instruction_description));
    instruction_description.opcode = VITTE_IR_OP_LOAD;
    instruction_description.block = body_block;
    instruction_description.result_type = element_ir_type;
    instruction_description.operands = &element_address;
    instruction_description.operand_count = 1u;
    instruction_description.span = span;
    instruction = vitte_ir_add_instruction(
        context->ir, &instruction_description);
    element_value = vitte_ir_instruction_result(context->ir, instruction);
    if (element_value == VITTE_IR_INVALID_ID ||
        !vitte_ir_lower_bind_value(
            context, pattern->as.binding_pattern.symbol, element_value) ||
        !vitte_ir_lower_add_loop(context, exit_block, increment_block)) {
        return false;
    }

    body_end = body_block;
    if (!vitte_ir_lower_block(
            context, function, body_block, statement->as.for_stmt.body,
            &body_end)) return false;
    --context->loop_count;
    if (!vitte_ir_get_block(context->ir, body_end)->terminated &&
        vitte_ir_build_br(context->ir, body_end, increment_block, span) ==
            VITTE_IR_INVALID_ID) return false;

    next_index = vitte_ir_instruction_result(
        context->ir,
        vitte_ir_build_binary(
            context->ir, VITTE_IR_OP_ADD, increment_block, index_ir_type,
            index_value, vitte_ir_const_integer(context->ir, index_ir_type, 1u),
            span));
    if (next_index == VITTE_IR_INVALID_ID ||
        !vitte_ir_phi_add_incoming(
            context->ir, phi_instruction, increment_block, next_index) ||
        vitte_ir_build_br(
            context->ir, increment_block, condition_block, span) ==
            VITTE_IR_INVALID_ID) return false;
    *fallthrough = exit_block;
    return true;
}

static bool
vitte_ir_lower_if_statement(vitte_ir_lower_context_t *context,
                            vitte_ir_function_id_t function,
                            vitte_ir_block_id_t block,
                            vitte_hir_node_id_t id,
                            vitte_ir_block_id_t *fallthrough)
{
    const vitte_hir_node_t *statement = vitte_hir_get_node(context->hir, id);
    vitte_ir_block_id_t then_block;
    vitte_ir_block_id_t else_block;
    vitte_ir_block_id_t merge_block;
    vitte_ir_block_id_t then_end;
    vitte_ir_block_id_t else_end;
    vitte_ir_value_id_t condition;

    if (statement == NULL || statement->kind != VITTE_HIR_NODE_IF ||
        fallthrough == NULL) return false;
    then_block = vitte_ir_lower_new_block(context, function, "if.then");
    else_block = vitte_ir_lower_new_block(context, function, "if.else");
    merge_block = vitte_ir_lower_new_block(context, function, "if.merge");
    if (then_block == VITTE_IR_INVALID_ID ||
        else_block == VITTE_IR_INVALID_ID ||
        merge_block == VITTE_IR_INVALID_ID) return false;
    condition = vitte_ir_lower_expr(
        context, function, block, statement->as.if_expr.condition);
    if (condition == VITTE_IR_INVALID_ID ||
        vitte_ir_build_cond_br(
            context->ir, block, condition, then_block, else_block,
            vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID) {
        return false;
    }

    then_end = then_block;
    if (!vitte_ir_lower_branch(
            context, function, then_block,
            statement->as.if_expr.then_branch, &then_end)) return false;
    if (!vitte_ir_get_block(context->ir, then_end)->terminated &&
        vitte_ir_build_br(
            context->ir, then_end, merge_block,
            vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID) {
        return false;
    }

    else_end = else_block;
    if (statement->as.if_expr.else_branch != VITTE_HIR_INVALID_ID &&
        !vitte_ir_lower_branch(
            context, function, else_block,
            statement->as.if_expr.else_branch, &else_end)) return false;
    if (!vitte_ir_get_block(context->ir, else_end)->terminated &&
        vitte_ir_build_br(
            context->ir, else_end, merge_block,
            vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID) {
        return false;
    }

    *fallthrough = merge_block;
    return true;
}

static bool
vitte_ir_lower_block(vitte_ir_lower_context_t *context,
                     vitte_ir_function_id_t function,
                     vitte_ir_block_id_t block,
                     vitte_hir_node_id_t id,
                     vitte_ir_block_id_t *fallthrough)
{
    const vitte_hir_node_t *node = vitte_hir_get_node(context->hir, id);
    size_t index;
    vitte_ir_block_id_t current = block;
    if (node == NULL || node->kind != VITTE_HIR_NODE_BLOCK || fallthrough == NULL) return false;
    for (index = 0u; index < node->as.block.statements.count; ++index) {
        const vitte_hir_node_t *statement = vitte_hir_get_node(context->hir,
            node->as.block.statements.items[index]);
        vitte_ir_value_id_t value;
        if (statement == NULL) return false;
        switch (statement->kind) {
            case VITTE_HIR_NODE_RETURN:
                value = statement->as.return_stmt.value == VITTE_HIR_INVALID_ID ?
                    VITTE_IR_INVALID_ID : vitte_ir_lower_expr(context, function, current, statement->as.return_stmt.value);
                if (statement->as.return_stmt.value != VITTE_HIR_INVALID_ID && value == VITTE_IR_INVALID_ID) return false;
                if (vitte_ir_build_return(context->ir, current, value, vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID) return false;
                break;
            case VITTE_HIR_NODE_LET:
                value = statement->as.let_stmt.initializer == VITTE_HIR_INVALID_ID ?
                    vitte_ir_const_integer(context->ir, vitte_ir_lower_type(context, statement->as.let_stmt.declared_type), 0u) :
                    vitte_ir_lower_expr(context, function, current, statement->as.let_stmt.initializer);
                if (value == VITTE_IR_INVALID_ID || !vitte_ir_lower_bind_value(context,
                    statement->as.let_stmt.symbol, value)) return false;
                break;
            case VITTE_HIR_NODE_EXPR_STMT:
                if (vitte_ir_lower_expr(context, function, current, statement->as.expression_statement.expression) == VITTE_IR_INVALID_ID) return false;
                break;
            case VITTE_HIR_NODE_ASSIGN:
                {
                    const vitte_hir_node_t *target = vitte_hir_get_node(
                        context->hir, statement->as.assignment.target);
                    if (target == NULL) return false;
                    if (target->kind == VITTE_HIR_NODE_SYMBOL) {
                        value = vitte_ir_lower_expr(
                            context, function, current,
                            statement->as.assignment.value);
                        if (value == VITTE_IR_INVALID_ID ||
                            !vitte_ir_lower_bind_value(
                                context,
                                target->as.symbol_ref.symbol,
                                value)) return false;
                    } else if (target->kind == VITTE_HIR_NODE_DEREF) {
                        vitte_ir_instruction_desc_t description;
                        vitte_ir_value_id_t store_operands[2];

                        store_operands[0] = vitte_ir_lower_expr(
                            context, function, current,
                            target->as.unary_expression.expression);
                        store_operands[1] = vitte_ir_lower_expr(
                            context, function, current,
                            statement->as.assignment.value);
                        if (store_operands[0] == VITTE_IR_INVALID_ID ||
                            store_operands[1] == VITTE_IR_INVALID_ID) {
                            return false;
                        }
                        memset(&description, 0, sizeof(description));
                        description.opcode = VITTE_IR_OP_STORE;
                        description.block = current;
                        description.operands = store_operands;
                        description.operand_count = 2u;
                        description.span = vitte_ir_lower_span(
                            &statement->span);
                        if (vitte_ir_add_instruction(
                                context->ir, &description) ==
                            VITTE_IR_INVALID_ID) {
                            return false;
                        }
                    } else if (target->kind == VITTE_HIR_NODE_INDEX) {
                        vitte_ir_instruction_desc_t description;
                        vitte_ir_instruction_id_t instruction;
                        vitte_ir_value_id_t address = vitte_ir_lower_index_address(
                            context, function, current, target->id);
                        const vitte_hir_node_t *assigned = vitte_hir_get_node(
                            context->hir, statement->as.assignment.value);
                        const vitte_hir_type_t *target_type = vitte_hir_get_type(
                            context->hir, target->type);
                        vitte_ir_type_id_t element_type = vitte_ir_lower_type(
                            context, target->type);
                        if (address == VITTE_IR_INVALID_ID ||
                            element_type == VITTE_IR_INVALID_ID) return false;
                        if (assigned != NULL &&
                            assigned->kind == VITTE_HIR_NODE_BINARY &&
                            assigned->as.binary.left == target->id) {
                            vitte_ir_value_id_t loaded;
                            vitte_ir_value_id_t right;
                            bool floating = target_type != NULL &&
                                (target_type->kind == VITTE_HIR_TYPE_F32 ||
                                 target_type->kind == VITTE_HIR_TYPE_F64);
                            memset(&description, 0, sizeof(description));
                            description.opcode = VITTE_IR_OP_LOAD;
                            description.block = current;
                            description.result_type = element_type;
                            description.operands = &address;
                            description.operand_count = 1u;
                            description.span = vitte_ir_lower_span(&target->span);
                            instruction = vitte_ir_add_instruction(
                                context->ir, &description);
                            loaded = vitte_ir_instruction_result(
                                context->ir, instruction);
                            right = vitte_ir_lower_expr(
                                context, function, current,
                                assigned->as.binary.right);
                            if (loaded == VITTE_IR_INVALID_ID ||
                                right == VITTE_IR_INVALID_ID) return false;
                            instruction = vitte_ir_build_binary(
                                context->ir,
                                vitte_ir_lower_binary(
                                    assigned->as.binary.op, floating),
                                current, element_type, loaded, right,
                                vitte_ir_lower_span(&assigned->span));
                            value = vitte_ir_instruction_result(
                                context->ir, instruction);
                        } else {
                            value = vitte_ir_lower_expr(
                                context, function, current,
                                statement->as.assignment.value);
                        }
                        if (value == VITTE_IR_INVALID_ID) return false;
                        {
                            vitte_ir_value_id_t store_operands[2];
                            store_operands[0] = address;
                            store_operands[1] = value;
                            memset(&description, 0, sizeof(description));
                            description.opcode = VITTE_IR_OP_STORE;
                            description.block = current;
                            description.operands = store_operands;
                            description.operand_count = 2u;
                            description.span = vitte_ir_lower_span(&statement->span);
                            if (vitte_ir_add_instruction(
                                    context->ir, &description) == VITTE_IR_INVALID_ID) {
                                return false;
                            }
                        }
                    } else {
                        return false;
                    }
                }
                break;
            case VITTE_HIR_NODE_BLOCK:
                if (!vitte_ir_lower_block(context, function, current,
                    statement->id, &current)) return false;
                break;
            case VITTE_HIR_NODE_IF:
                if (!vitte_ir_lower_if_statement(
                        context, function, current, statement->id, &current)) {
                    return false;
                }
                break;
            case VITTE_HIR_NODE_WHILE: {
                vitte_ir_block_id_t condition_block = vitte_ir_lower_new_block(context, function, "while.cond");
                vitte_ir_block_id_t body_block = vitte_ir_lower_new_block(context, function, "while.body");
                vitte_ir_block_id_t exit_block = vitte_ir_lower_new_block(context, function, "while.exit");
                vitte_ir_value_id_t condition;
                vitte_ir_block_id_t body_end = body_block;
                if (condition_block == VITTE_IR_INVALID_ID || body_block == VITTE_IR_INVALID_ID ||
                    exit_block == VITTE_IR_INVALID_ID ||
                    vitte_ir_build_br(context->ir, current, condition_block,
                        vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID) return false;
                condition = vitte_ir_lower_expr(context, function, condition_block,
                    statement->as.while_stmt.condition);
                if (condition == VITTE_IR_INVALID_ID ||
                    vitte_ir_build_cond_br(context->ir, condition_block, condition,
                        body_block, exit_block, vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID ||
                    !vitte_ir_lower_add_loop(context, exit_block, condition_block) ||
                    !vitte_ir_lower_block(context, function, body_block,
                        statement->as.while_stmt.body, &body_end)) return false;
                --context->loop_count;
                if (!vitte_ir_get_block(context->ir, body_end)->terminated &&
                    vitte_ir_build_br(context->ir, body_end, condition_block,
                        vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID) return false;
                current = exit_block;
                break;
            }
            case VITTE_HIR_NODE_LOOP: {
                vitte_ir_block_id_t body_block = vitte_ir_lower_new_block(context, function, "loop.body");
                vitte_ir_block_id_t exit_block = vitte_ir_lower_new_block(context, function, "loop.exit");
                vitte_ir_block_id_t body_end = body_block;
                if (body_block == VITTE_IR_INVALID_ID || exit_block == VITTE_IR_INVALID_ID ||
                    vitte_ir_build_br(context->ir, current, body_block,
                        vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID ||
                    !vitte_ir_lower_add_loop(context, exit_block, body_block) ||
                    !vitte_ir_lower_block(context, function, body_block,
                        statement->as.loop_stmt.body, &body_end)) return false;
                --context->loop_count;
                if (!vitte_ir_get_block(context->ir, body_end)->terminated &&
                    vitte_ir_build_br(context->ir, body_end, body_block,
                        vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID) return false;
                current = exit_block;
                break;
            }
            case VITTE_HIR_NODE_FOR:
                if (!vitte_ir_lower_for_statement(
                        context, function, current, statement, &current)) {
                    return false;
                }
                break;
            case VITTE_HIR_NODE_BREAK:
            case VITTE_HIR_NODE_CONTINUE:
                if (context->loop_count == 0u ||
                    vitte_ir_build_br(context->ir, current,
                        statement->kind == VITTE_HIR_NODE_BREAK ?
                            context->loops[context->loop_count - 1u].break_target :
                            context->loops[context->loop_count - 1u].continue_target,
                        vitte_ir_lower_span(&statement->span)) == VITTE_IR_INVALID_ID) return false;
                break;
            default:
                return false;
        }
        if (vitte_ir_get_block(context->ir, current)->terminated) break;
    }
    if (node->as.block.tail != VITTE_HIR_INVALID_ID &&
        !vitte_ir_lower_expr(context, function, current, node->as.block.tail)) return false;
    *fallthrough = current;
    return true;
}

bool
vitte_ir_lower_hir(vitte_ir_context_t *ir, const vitte_hir_t *hir)
{
    vitte_ir_lower_context_t context;
    const vitte_hir_node_t *module;
    size_t index, function_capacity = 0u;
    if (ir == NULL || hir == NULL || !vitte_hir_is_valid(hir) ||
        hir->state != VITTE_HIR_STATE_VALIDATED || !vitte_ir_is_valid(ir) ||
        ir->state != VITTE_IR_STATE_BUILDING || hir->roots.count == 0u) return false;
    memset(&context, 0, sizeof(context)); context.ir = ir; context.hir = hir;
    context.type_count = hir->type_count; context.types = (vitte_ir_type_id_t *)calloc(hir->type_count, sizeof(*context.types));
    if (hir->type_count != 0u && context.types == NULL) goto failure;
    module = vitte_hir_get_node(hir, hir->roots.items[0]);
    if (module == NULL || module->kind != VITTE_HIR_NODE_MODULE) goto failure;
    for (index = 0u; index < module->as.module.items.count; ++index) {
        const vitte_hir_node_t *item = vitte_hir_get_node(
            hir, module->as.module.items.items[index]);
        vitte_hir_symbol_id_t symbol = VITTE_HIR_INVALID_ID;
        vitte_hir_node_id_t value_node = VITTE_HIR_INVALID_ID;
        vitte_ir_value_id_t value;
        if (item == NULL) continue;
        if (item->kind == VITTE_HIR_NODE_CONST_DECL) {
            symbol = item->as.constant.symbol;
            value_node = item->as.constant.value;
        } else if (item->kind == VITTE_HIR_NODE_STATIC_DECL) {
            symbol = item->as.static_decl.symbol;
            value_node = item->as.static_decl.value;
        } else {
            continue;
        }
        value = vitte_ir_lower_constant_value(&context, value_node);
        if (symbol == VITTE_HIR_INVALID_ID || value == VITTE_IR_INVALID_ID ||
            !vitte_ir_lower_bind_value(&context, symbol, value)) goto failure;
    }
    for (index = 0u; index < module->as.module.items.count; ++index) {
        const vitte_hir_node_t *item = vitte_hir_get_node(hir, module->as.module.items.items[index]);
        if (item != NULL && (item->kind == VITTE_HIR_NODE_PROC_DECL || item->kind == VITTE_HIR_NODE_EXTERN_PROC_DECL)) ++function_capacity;
    }
    context.functions = (vitte_ir_lower_function_t *)calloc(function_capacity, sizeof(*context.functions));
    if (function_capacity != 0u && context.functions == NULL) goto failure;
    for (index = 0u; index < module->as.module.items.count; ++index) {
        const vitte_hir_node_t *item = vitte_hir_get_node(hir, module->as.module.items.items[index]);
        vitte_ir_type_id_t *parameters = NULL;
        size_t parameter_count = 0u;
        vitte_ir_type_desc_t type_desc;
        vitte_ir_function_desc_t function_desc;
        vitte_ir_function_id_t function;
        vitte_ir_string_id_t name;
        size_t parameter_index;
        if (item == NULL || (item->kind != VITTE_HIR_NODE_PROC_DECL && item->kind != VITTE_HIR_NODE_EXTERN_PROC_DECL)) continue;
        name = vitte_hir_get_symbol(hir, item->as.procedure.symbol)->name;
        for (parameter_index = 0u; parameter_index < item->as.procedure.parameters.count; ++parameter_index) {
            const vitte_hir_node_t *parameter = vitte_hir_get_node(hir, item->as.procedure.parameters.items[parameter_index]);
            if (parameter != NULL) ++parameter_count;
        }
        parameters = (vitte_ir_type_id_t *)calloc(parameter_count, sizeof(*parameters));
        if (parameter_count != 0u && parameters == NULL) goto failure;
        parameter_count = 0u;
        for (parameter_index = 0u; parameter_index < item->as.procedure.parameters.count; ++parameter_index) {
            const vitte_hir_node_t *parameter = vitte_hir_get_node(hir, item->as.procedure.parameters.items[parameter_index]);
            const vitte_hir_symbol_t *symbol = parameter == NULL ? NULL : vitte_hir_get_symbol(hir, parameter->as.binding_pattern.symbol);
            if (symbol == NULL) { free(parameters); goto failure; }
            parameters[parameter_count++] = vitte_ir_lower_type(&context, symbol->type);
        }
        memset(&type_desc, 0, sizeof(type_desc)); type_desc.kind = VITTE_IR_TYPE_FUNCTION;
        type_desc.parameter_types = parameters; type_desc.parameter_count = parameter_count;
        type_desc.return_type = vitte_ir_lower_type(&context, item->as.procedure.return_type);
        if (type_desc.return_type == VITTE_IR_INVALID_ID) { free(parameters); goto failure; }
        memset(&function_desc, 0, sizeof(function_desc)); function_desc.name = vitte_ir_intern_string(ir,
            vitte_hir_get_string(hir, name)->data, vitte_hir_get_string(hir, name)->length);
        function_desc.type = vitte_ir_intern_type(ir, &type_desc);
        function_desc.is_declaration = item->kind == VITTE_HIR_NODE_EXTERN_PROC_DECL || item->as.procedure.body == VITTE_HIR_INVALID_ID;
        function_desc.linkage = item->kind == VITTE_HIR_NODE_EXTERN_PROC_DECL
            ? VITTE_IR_LINKAGE_EXTERNAL
            : VITTE_IR_LINKAGE_INTERNAL;
        function_desc.calling_convention =
            item->kind == VITTE_HIR_NODE_EXTERN_PROC_DECL
                ? VITTE_IR_CALL_C
                : VITTE_IR_CALL_DEFAULT;
        function_desc.is_variadic = item->as.procedure.is_variadic;
        function_desc.span = vitte_ir_lower_span(&item->span);
        function = vitte_ir_add_function(ir, &function_desc); free(parameters);
        if (function == VITTE_IR_INVALID_ID) goto failure;
        context.functions[context.function_count].symbol = item->as.procedure.symbol;
        context.functions[context.function_count].function = function;
        context.functions[context.function_count].return_type = type_desc.return_type;
        {
            vitte_ir_value_desc_t value_desc; memset(&value_desc, 0, sizeof(value_desc));
            value_desc.kind = VITTE_IR_VALUE_FUNCTION; value_desc.type = function_desc.type; value_desc.function = function;
            context.functions[context.function_count].function_value = vitte_ir_add_value(ir, &value_desc);
        }
        ++context.function_count;
    }
    /* Bind every procedure before lowering any body so forward calls and
     * mutually recursive procedures resolve through the same symbol table. */
    for (index = 0u; index < context.function_count; ++index) {
        if (!vitte_ir_lower_bind_value(
                &context,
                context.functions[index].symbol,
                context.functions[index].function_value)) {
            goto failure;
        }
    }
    for (index = 0u; index < context.function_count; ++index) {
        vitte_ir_lower_function_t *lowered = &context.functions[index];
        const vitte_ir_function_t *function = vitte_ir_get_function(ir, lowered->function);
        const vitte_hir_node_t *procedure = NULL;
        size_t item_index;
        for (item_index = 0u; item_index < module->as.module.items.count; ++item_index) {
            const vitte_hir_node_t *item = vitte_hir_get_node(hir, module->as.module.items.items[item_index]);
            if (item != NULL &&
                (item->kind == VITTE_HIR_NODE_PROC_DECL ||
                 item->kind == VITTE_HIR_NODE_EXTERN_PROC_DECL) &&
                item->as.procedure.symbol == lowered->symbol) {
                procedure = item;
                break;
            }
        }
        if (procedure == NULL) continue;
        for (item_index = 0u; item_index < procedure->as.procedure.parameters.count; ++item_index) {
            const vitte_hir_node_t *parameter = vitte_hir_get_node(hir, procedure->as.procedure.parameters.items[item_index]);
            const vitte_hir_symbol_t *symbol = parameter == NULL ? NULL : vitte_hir_get_symbol(hir, parameter->as.binding_pattern.symbol);
            vitte_ir_value_id_t value;
            if (symbol == NULL) goto failure;
            value = vitte_ir_add_parameter(ir, lowered->function, vitte_ir_lower_type(&context, symbol->type),
                vitte_ir_intern_string(ir, vitte_hir_get_string(hir, symbol->name)->data, vitte_hir_get_string(hir, symbol->name)->length),
                vitte_ir_lower_span(&parameter->span));
            if (value == VITTE_IR_INVALID_ID || !vitte_ir_lower_bind_value(&context, symbol->id, value)) goto failure;
        }
        if (function == NULL || function->is_declaration) continue;
        {
            vitte_ir_block_id_t block = vitte_ir_add_block(ir, lowered->function,
                vitte_ir_intern_string(ir, "entry", 5u), vitte_ir_lower_span(&procedure->span));
            {
                vitte_ir_block_id_t end_block = block;
                if (block == VITTE_IR_INVALID_ID || !vitte_ir_lower_block(
                    &context, lowered->function, block,
                    procedure->as.procedure.body, &end_block)) goto failure;
                if (!vitte_ir_get_block(ir, end_block)->terminated) {
                    vitte_ir_value_id_t value = VITTE_IR_INVALID_ID;
                    const vitte_ir_type_t *return_type = vitte_ir_get_type(
                        ir, lowered->return_type);
                    if (return_type != NULL && return_type->kind != VITTE_IR_TYPE_VOID) {
                        value = vitte_ir_const_integer(ir, lowered->return_type, 0u);
                    }
                    if (vitte_ir_build_return(ir, end_block, value,
                        (vitte_ir_span_t){0}) == VITTE_IR_INVALID_ID) goto failure;
                }
            }
        }
    }
    free(context.types); free(context.functions); free(context.values); free(context.loops);
    return vitte_ir_seal(ir);
failure:
    free(context.types); free(context.functions); free(context.values); free(context.loops);
    return false;
}
