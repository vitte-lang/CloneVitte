/*
 * Vitte Compiler
 * src/bootstrap/backend/c17/ir_emitter.c
 *
 * Deterministic C17 emission for the bootstrap IR.
 *
 * The emitter consumes validated IR. Every generated compiler name contains
 * its IR id, so equal source names from different modules remain distinct.
 */

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ir_emitter.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__unix__)
#include <unistd.h>
#elif defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

#include "../../platform/platform.h"

typedef struct vitte_c17_ir_emitter {
    FILE *output;
    const vitte_ir_context_t *ir;
    vitte_c17_ir_emit_options_t options;
    bool failed;
    size_t indent;
} vitte_c17_ir_emitter_t;

#if defined(_WIN32)
static int
vitte_c17_ir_mkstemp(char *path)
{
    char temporary_directory[MAX_PATH];
    char temporary_path[MAX_PATH];
    DWORD directory_length;
    size_t path_length;
    int descriptor;

    if (path == NULL) {
        return -1;
    }
    directory_length = GetTempPathA(
        (DWORD)sizeof(temporary_directory), temporary_directory);
    if (directory_length == 0u ||
        directory_length >= (DWORD)sizeof(temporary_directory) ||
        GetTempFileNameA(
            temporary_directory, "vit", 0u, temporary_path) == 0u) {
        return -1;
    }
    path_length = strlen(temporary_path);
    if (path_length >= 4096u) {
        (void)DeleteFileA(temporary_path);
        return -1;
    }
    memcpy(path, temporary_path, path_length + 1u);
    descriptor = _open(path, _O_BINARY | _O_RDWR);
    if (descriptor < 0) {
        (void)DeleteFileA(path);
    }
    return descriptor;
}

static bool
vitte_c17_ir_set_executable_extension(char *path)
{
    size_t length;

    if (path == NULL) {
        return false;
    }
    length = strlen(path);
    if (length < 4u || strcmp(path + length - 4u, ".tmp") != 0 ||
        length >= 4096u) {
        return false;
    }
    memcpy(path + length - 4u, ".exe", 5u);
    return true;
}

#define mkstemp vitte_c17_ir_mkstemp
#define fdopen _fdopen
#define close _close
#define unlink _unlink
#endif

static bool
vitte_c17_ir_write(
    vitte_c17_ir_emitter_t *emitter,
    const char *text)
{
    if (emitter == NULL || emitter->output == NULL || text == NULL) {
        return false;
    }
    if (fputs(text, emitter->output) == EOF) {
        emitter->failed = true;
        return false;
    }
    return true;
}

static bool
vitte_c17_ir_has_string_slice(const vitte_ir_context_t *ir)
{
    size_t index;

    if (ir == NULL) return false;
    for (index = 0u; index < ir->instruction_count; ++index) {
        if (ir->instructions[index].opcode == VITTE_IR_OP_STRING_SLICE) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ir_has_string_concat(const vitte_ir_context_t *ir)
{
    size_t index;

    if (ir == NULL) return false;
    for (index = 0u; index < ir->instruction_count; ++index) {
        if (ir->instructions[index].opcode == VITTE_IR_OP_STRING_CONCAT) {
            return true;
        }
    }
    return false;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static bool
vitte_c17_ir_printf(
    vitte_c17_ir_emitter_t *emitter,
    const char *format,
    ...)
{
    va_list arguments;
    int result;

    if (emitter == NULL || emitter->output == NULL || format == NULL) {
        return false;
    }
    va_start(arguments, format);
    result = vfprintf(emitter->output, format, arguments);
    va_end(arguments);
    if (result < 0) {
        emitter->failed = true;
        return false;
    }
    return true;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static bool
vitte_c17_ir_indent(
    vitte_c17_ir_emitter_t *emitter)
{
    size_t index;

    if (emitter == NULL) {
        return false;
    }
    for (index = 0u; index < emitter->indent; ++index) {
        if (!vitte_c17_ir_write(emitter, "    ")) {
            return false;
        }
    }
    return true;
}

static const vitte_ir_string_t *
vitte_c17_ir_string(
    const vitte_ir_context_t *ir,
    vitte_ir_string_id_t id)
{
    return vitte_ir_get_string(ir, id);
}

static bool
vitte_c17_ir_name(
    const vitte_ir_context_t *ir,
    vitte_ir_string_id_t id,
    uint64_t suffix,
    const char *prefix,
    char *buffer,
    size_t capacity)
{
    const vitte_ir_string_t *string;
    size_t prefix_length;
    size_t position;
    size_t index;
    int written;

    if (ir == NULL || prefix == NULL || buffer == NULL || capacity < 32u) {
        return false;
    }
    string = vitte_c17_ir_string(ir, id);
    prefix_length = strlen(prefix);
    if (prefix_length + 24u >= capacity) {
        return false;
    }
    memcpy(buffer, prefix, prefix_length);
    position = prefix_length;
    if (string != NULL) {
        for (index = 0u;
             index < string->length && position + 24u < capacity;
             ++index) {
            unsigned char character = (unsigned char)string->data[index];
            if ((character >= (unsigned char)'a' &&
                 character <= (unsigned char)'z') ||
                (character >= (unsigned char)'A' &&
                 character <= (unsigned char)'Z') ||
                (character >= (unsigned char)'0' &&
                 character <= (unsigned char)'9') ||
                character == (unsigned char)'_') {
                buffer[position++] = (char)character;
            } else {
                buffer[position++] = '_';
            }
        }
    }
    if (position == prefix_length) {
        buffer[position++] = 'x';
    }
    written = snprintf(
        buffer + position,
        capacity - position,
        "_%" PRIu64,
        suffix);
    if (written < 0 || (size_t)written >= capacity - position) {
        return false;
    }
    buffer[position + (size_t)written] = '\0';
    return true;
}

static bool
vitte_c17_ir_function_name(
    const vitte_ir_context_t *ir,
    vitte_ir_function_id_t function,
    char *buffer,
    size_t capacity)
{
    const vitte_ir_function_t *entry =
        vitte_ir_get_function(ir, function);
    const vitte_ir_string_t *external_name;
    size_t index;

    if (entry == NULL) {
        return false;
    }
    if (entry->is_declaration &&
        entry->linkage == VITTE_IR_LINKAGE_EXTERNAL) {
        external_name = vitte_ir_get_string(ir, entry->name);
        if (external_name == NULL || external_name->length == 0u ||
            external_name->length >= capacity ||
            !(((external_name->data[0] >= 'a') &&
               (external_name->data[0] <= 'z')) ||
              ((external_name->data[0] >= 'A') &&
               (external_name->data[0] <= 'Z')) ||
              external_name->data[0] == '_')) {
            return false;
        }
        for (index = 1u; index < external_name->length; ++index) {
            unsigned char character =
                (unsigned char)external_name->data[index];
            if (!((character >= (unsigned char)'a' &&
                   character <= (unsigned char)'z') ||
                  (character >= (unsigned char)'A' &&
                   character <= (unsigned char)'Z') ||
                  (character >= (unsigned char)'0' &&
                   character <= (unsigned char)'9') ||
                  character == (unsigned char)'_')) {
                return false;
            }
        }
        memcpy(buffer, external_name->data, external_name->length);
        buffer[external_name->length] = '\0';
        return true;
    }

    return vitte_c17_ir_name(
               ir,
               entry->name,
               function,
               "vitte_fn_",
               buffer,
               capacity);
}

static bool
vitte_c17_ir_is_standard_memory_function(
    const vitte_ir_context_t *ir,
    const vitte_ir_function_t *function)
{
    const vitte_ir_string_t *name;

    if (function == NULL || !function->is_declaration ||
        function->linkage != VITTE_IR_LINKAGE_EXTERNAL) {
        return false;
    }
    name = vitte_ir_get_string(ir, function->name);
    /* The C headers provide these prototypes with void-pointer parameters. */
    return name != NULL &&
        ((name->length == 6u &&
          memcmp(name->data, "malloc", 6u) == 0) ||
         (name->length == 4u &&
          memcmp(name->data, "free", 4u) == 0) ||
         (name->length == 7u &&
          memcmp(name->data, "memmove", 7u) == 0));
}

static bool
vitte_c17_ir_value_name(
    vitte_ir_value_id_t value,
    char *buffer,
    size_t capacity)
{
    int written;

    if (buffer == NULL || capacity < 32u) {
        return false;
    }
    written = snprintf(buffer, capacity, "vitte_v%" PRIu64, value);
    return written >= 0 && (size_t)written < capacity;
}

static bool
vitte_c17_ir_block_name(
    vitte_ir_block_id_t block,
    char *buffer,
    size_t capacity)
{
    int written;

    if (buffer == NULL || capacity < 32u) {
        return false;
    }
    written = snprintf(buffer, capacity, "vitte_bb%" PRIu64, block);
    return written >= 0 && (size_t)written < capacity;
}

static bool
vitte_c17_ir_emit_type(
    vitte_c17_ir_emitter_t *emitter,
    vitte_ir_type_id_t id)
{
    const vitte_ir_type_t *type;
    char name[256];

    type = vitte_ir_get_type(emitter->ir, id);
    if (type == NULL) {
        return false;
    }
    switch (type->kind) {
        case VITTE_IR_TYPE_VOID: return vitte_c17_ir_write(emitter, "void");
        case VITTE_IR_TYPE_BOOL: return vitte_c17_ir_write(emitter, "bool");
        case VITTE_IR_TYPE_I8: return vitte_c17_ir_write(emitter, "int8_t");
        case VITTE_IR_TYPE_I16: return vitte_c17_ir_write(emitter, "int16_t");
        case VITTE_IR_TYPE_I32: return vitte_c17_ir_write(emitter, "int32_t");
        case VITTE_IR_TYPE_I64: return vitte_c17_ir_write(emitter, "int64_t");
        case VITTE_IR_TYPE_U8: return vitte_c17_ir_write(emitter, "uint8_t");
        case VITTE_IR_TYPE_U16: return vitte_c17_ir_write(emitter, "uint16_t");
        case VITTE_IR_TYPE_U32: return vitte_c17_ir_write(emitter, "uint32_t");
        case VITTE_IR_TYPE_U64: return vitte_c17_ir_write(emitter, "uint64_t");
        case VITTE_IR_TYPE_F32: return vitte_c17_ir_write(emitter, "float");
        case VITTE_IR_TYPE_F64: return vitte_c17_ir_write(emitter, "double");
        case VITTE_IR_TYPE_CHAR: return vitte_c17_ir_write(emitter, "uint32_t");
        case VITTE_IR_TYPE_POINTER:
        case VITTE_IR_TYPE_ARRAY:
            return vitte_c17_ir_emit_type(emitter, type->element_type) &&
                   vitte_c17_ir_write(emitter, " *");
        case VITTE_IR_TYPE_SLICE:
            if (!vitte_c17_ir_name(
                    emitter->ir,
                    type->name,
                    id,
                    "vitte_slice_",
                    name,
                    sizeof(name))) {
                return false;
            }
            return vitte_c17_ir_write(emitter, "struct ") &&
                   vitte_c17_ir_write(emitter, name);
        case VITTE_IR_TYPE_FUNCTION:
            return vitte_c17_ir_write(emitter, "void *");
        case VITTE_IR_TYPE_STRUCT:
        case VITTE_IR_TYPE_OPAQUE:
            if (!vitte_c17_ir_name(
                    emitter->ir,
                    type->name,
                    id,
                    "vitte_type_",
                    name,
                    sizeof(name))) {
                return false;
            }
            return vitte_c17_ir_write(emitter, "struct ") &&
                   vitte_c17_ir_write(emitter, name);
        case VITTE_IR_TYPE_INVALID:
        case VITTE_IR_TYPE_COUNT:
            return false;
    }
    return false;
}

static bool
vitte_c17_ir_emit_value(
    vitte_c17_ir_emitter_t *emitter,
    vitte_ir_value_id_t id)
{
    const vitte_ir_value_t *value;
    char name[256];

    value = vitte_ir_get_value(emitter->ir, id);
    if (value == NULL) {
        return false;
    }
    switch (value->kind) {
        case VITTE_IR_VALUE_CONSTANT_BOOL:
            return vitte_c17_ir_write(
                emitter,
                value->bool_value ? "true" : "false");
        case VITTE_IR_VALUE_CONSTANT_INTEGER:
            return vitte_c17_ir_printf(
                emitter,
                "UINT64_C(%" PRIu64 ")",
                value->integer_value);
        case VITTE_IR_VALUE_CONSTANT_FLOAT:
            return vitte_c17_ir_printf(
                emitter,
                "%.17g",
                value->float_value);
        case VITTE_IR_VALUE_CONSTANT_STRING: {
            const vitte_ir_string_t *string = vitte_ir_get_string(
                emitter->ir, value->string_value);
            size_t index;
            if (string == NULL || !vitte_c17_ir_write(emitter, "(uint8_t *)\"") ) {
                return false;
            }
            for (index = 0u; index < string->length; ++index) {
                unsigned char character = (unsigned char)string->data[index];
                switch (character) {
                    case '\\': if (!vitte_c17_ir_write(emitter, "\\\\")) return false; break;
                    case '"': if (!vitte_c17_ir_write(emitter, "\\\"")) return false; break;
                    case '\n': if (!vitte_c17_ir_write(emitter, "\\n")) return false; break;
                    case '\r': if (!vitte_c17_ir_write(emitter, "\\r")) return false; break;
                    case '\t': if (!vitte_c17_ir_write(emitter, "\\t")) return false; break;
                    default:
                        if (character < 0x20u || character >= 0x7fu) {
                            if (!vitte_c17_ir_printf(emitter, "\\%03o", character)) return false;
                        } else if (fputc((int)character, emitter->output) == EOF) {
                            emitter->failed = true;
                            return false;
                        }
                        break;
                }
            }
            return vitte_c17_ir_write(emitter, "\"");
        }
        case VITTE_IR_VALUE_NULL:
            return vitte_c17_ir_write(emitter, "NULL");
        case VITTE_IR_VALUE_UNDEF:
        case VITTE_IR_VALUE_POISON:
            return vitte_c17_ir_write(emitter, "0");
        case VITTE_IR_VALUE_PARAMETER:
        case VITTE_IR_VALUE_INSTRUCTION:
            return vitte_c17_ir_value_name(id, name, sizeof(name)) &&
                   vitte_c17_ir_write(emitter, name);
        case VITTE_IR_VALUE_FUNCTION: {
            const vitte_ir_function_t *function =
                vitte_ir_get_function(emitter->ir, value->function);
            return function != NULL &&
                   vitte_c17_ir_function_name(
                       emitter->ir,
                       function->id,
                       name,
                       sizeof(name)) &&
                   vitte_c17_ir_write(emitter, name);
        }
        case VITTE_IR_VALUE_INVALID:
        case VITTE_IR_VALUE_COUNT:
            return false;
    }
    return false;
}

static bool
vitte_c17_ir_emit_debug_span(
    vitte_c17_ir_emitter_t *emitter,
    const vitte_ir_span_t *span)
{
    if (!emitter->options.emit_debug_comments ||
        span == NULL ||
        !span->valid) {
        return true;
    }
    return vitte_c17_ir_indent(emitter) &&
           vitte_c17_ir_printf(
               emitter,
               "/* vitte:%" PRIu32 ":%zu-%zu */\n",
               span->file_id,
               span->begin,
               span->end);
}

static bool
vitte_c17_ir_emit_line_directive(
    vitte_c17_ir_emitter_t *emitter,
    size_t line)
{
    size_t index;

    if (!emitter->options.emit_line_directives ||
        emitter->options.source_path == NULL ||
        emitter->options.source_path[0] == '\0') {
        return true;
    }
    if (line == 0u) {
        line = 1u;
    }
    if (!vitte_c17_ir_printf(emitter, "#line %zu \"", line)) {
        return false;
    }
    for (index = 0u; emitter->options.source_path[index] != '\0'; ++index) {
        unsigned char character =
            (unsigned char)emitter->options.source_path[index];
        if (character == '\\' || character == '"') {
            if (fputc('\\', emitter->output) == EOF) {
                emitter->failed = true;
                return false;
            }
        }
        if (character == '\n') {
            if (!vitte_c17_ir_write(emitter, "\\n")) {
                return false;
            }
        } else if (character == '\r') {
            if (!vitte_c17_ir_write(emitter, "\\r")) {
                return false;
            }
        } else if (fputc((int)character, emitter->output) == EOF) {
            emitter->failed = true;
            return false;
        }
    }
    return vitte_c17_ir_write(emitter, "\"\n");
}

static bool
vitte_c17_ir_emit_symbol_comment(
    vitte_c17_ir_emitter_t *emitter,
    const vitte_ir_function_t *function,
    const char *generated_name)
{
    const vitte_ir_string_t *source_name;

    if (!emitter->options.emit_debug_comments) {
        return true;
    }
    source_name = vitte_c17_ir_string(emitter->ir, function->name);
    if (source_name == NULL || generated_name == NULL) {
        return false;
    }
    return vitte_c17_ir_printf(
        emitter,
        "/* vitte-symbol: %.*s -> %s */\n",
        (int)source_name->length,
        source_name->data,
        generated_name);
}

static bool
vitte_c17_ir_emit_phi_edges(
    vitte_c17_ir_emitter_t *emitter,
    vitte_ir_block_id_t target,
    vitte_ir_block_id_t source)
{
    const vitte_ir_block_t *block;
    size_t index;
    bool has_phis = false;

    block = vitte_ir_get_block(emitter->ir, target);
    if (block == NULL) {
        return false;
    }

    for (index = 0u;
         index < block->instruction_count;
         ++index) {
        const vitte_ir_instruction_t *instruction =
            vitte_ir_get_instruction(
                emitter->ir,
                block->instructions[index]);
        size_t incoming;
        bool found = false;

        if (instruction == NULL) {
            return false;
        }
        if (instruction->opcode != VITTE_IR_OP_PHI) {
            break;
        }
        has_phis = true;
        for (incoming = 0u;
             incoming < instruction->phi_count;
             ++incoming) {
            if (instruction->phi_incomings[incoming].block == source) {
                found = true;
                break;
            }
        }
        if (!found ||
            instruction->result == VITTE_IR_INVALID_ID ||
            vitte_ir_get_type(emitter->ir, instruction->result_type) == NULL) {
            return false;
        }
    }
    if (!has_phis) {
        return true;
    }

    /* PHI inputs are parallel copies. Snapshot them before writing results. */
    if (!vitte_c17_ir_indent(emitter) ||
        !vitte_c17_ir_write(emitter, "{\n")) {
        return false;
    }
    ++emitter->indent;
    for (index = 0u;
         index < block->instruction_count;
         ++index) {
        const vitte_ir_instruction_t *instruction;
        size_t incoming;
        vitte_ir_value_id_t value;

        instruction = vitte_ir_get_instruction(
            emitter->ir,
            block->instructions[index]);
        if (instruction == NULL) {
            return false;
        }
        if (instruction->opcode != VITTE_IR_OP_PHI) {
            break;
        }
        value = VITTE_IR_INVALID_ID;
        for (incoming = 0u;
             incoming < instruction->phi_count;
             ++incoming) {
            if (instruction->phi_incomings[incoming].block == source) {
                value = instruction->phi_incomings[incoming].value;
                break;
            }
        }
        if (value == VITTE_IR_INVALID_ID ||
            !vitte_c17_ir_indent(emitter) ||
            !vitte_c17_ir_emit_type(emitter, instruction->result_type) ||
            !vitte_c17_ir_write(emitter, " vitte_phi_tmp_") ||
            !vitte_c17_ir_printf(
                emitter,
                "%" PRIu64,
                instruction->result) ||
            !vitte_c17_ir_write(emitter, " = ") ||
            !vitte_c17_ir_emit_value(emitter, value) ||
            !vitte_c17_ir_write(emitter, ";\n")) {
            return false;
        }
    }

    for (index = 0u;
         index < block->instruction_count;
         ++index) {
        const vitte_ir_instruction_t *instruction =
            vitte_ir_get_instruction(
                emitter->ir,
                block->instructions[index]);
        char name[256];

        if (instruction == NULL) {
            return false;
        }
        if (instruction->opcode != VITTE_IR_OP_PHI) {
            break;
        }
        if (!vitte_c17_ir_value_name(
                instruction->result,
                name,
                sizeof(name)) ||
            !vitte_c17_ir_indent(emitter) ||
            !vitte_c17_ir_write(emitter, name) ||
            !vitte_c17_ir_write(emitter, " = vitte_phi_tmp_") ||
            !vitte_c17_ir_printf(
                emitter,
                "%" PRIu64,
                instruction->result) ||
            !vitte_c17_ir_write(emitter, ";\n")) {
            return false;
        }
    }

    --emitter->indent;
    return vitte_c17_ir_indent(emitter) &&
           vitte_c17_ir_write(emitter, "}\n");
}

static const char *
vitte_c17_ir_binary_operator(
    vitte_ir_opcode_t opcode)
{
    switch (opcode) {
        case VITTE_IR_OP_ADD:
        case VITTE_IR_OP_FADD: return "+";
        case VITTE_IR_OP_SUB:
        case VITTE_IR_OP_FSUB: return "-";
        case VITTE_IR_OP_MUL:
        case VITTE_IR_OP_FMUL: return "*";
        case VITTE_IR_OP_SDIV:
        case VITTE_IR_OP_UDIV:
        case VITTE_IR_OP_FDIV: return "/";
        case VITTE_IR_OP_SREM:
        case VITTE_IR_OP_UREM: return "%";
        case VITTE_IR_OP_AND: return "&";
        case VITTE_IR_OP_OR: return "|";
        case VITTE_IR_OP_XOR: return "^";
        case VITTE_IR_OP_SHL: return "<<";
        case VITTE_IR_OP_LSHR:
        case VITTE_IR_OP_ASHR: return ">>";
        case VITTE_IR_OP_ICMP_EQ:
        case VITTE_IR_OP_FCMP_EQ: return "==";
        case VITTE_IR_OP_ICMP_NE:
        case VITTE_IR_OP_FCMP_NE: return "!=";
        case VITTE_IR_OP_ICMP_SLT:
        case VITTE_IR_OP_ICMP_ULT:
        case VITTE_IR_OP_FCMP_LT: return "<";
        case VITTE_IR_OP_ICMP_SLE:
        case VITTE_IR_OP_ICMP_ULE:
        case VITTE_IR_OP_FCMP_LE: return "<=";
        case VITTE_IR_OP_ICMP_SGT:
        case VITTE_IR_OP_ICMP_UGT:
        case VITTE_IR_OP_FCMP_GT: return ">";
        case VITTE_IR_OP_ICMP_SGE:
        case VITTE_IR_OP_ICMP_UGE:
        case VITTE_IR_OP_FCMP_GE: return ">=";
        default: return NULL;
    }
}

static bool
vitte_c17_ir_emit_binary(
    vitte_c17_ir_emitter_t *emitter,
    const vitte_ir_instruction_t *instruction)
{
    const char *operator_text =
        vitte_c17_ir_binary_operator(instruction->opcode);

    return operator_text != NULL &&
           instruction->operand_count == 2u &&
           vitte_c17_ir_write(emitter, "(") &&
           vitte_c17_ir_emit_value(
               emitter,
               instruction->operands[0]) &&
           vitte_c17_ir_write(emitter, " ") &&
           vitte_c17_ir_write(emitter, operator_text) &&
           vitte_c17_ir_write(emitter, " ") &&
           vitte_c17_ir_emit_value(
               emitter,
               instruction->operands[1]) &&
           vitte_c17_ir_write(emitter, ")");
}

static bool
vitte_c17_ir_emit_instruction(
    vitte_c17_ir_emitter_t *emitter,
    const vitte_ir_instruction_t *instruction)
{
    char result[256];
    char target[256];
    bool has_result;

    if (instruction == NULL) {
        return false;
    }
    if (instruction->opcode == VITTE_IR_OP_PHI) {
        return true;
    }
    if (!vitte_c17_ir_emit_debug_span(emitter, &instruction->span)) {
        return false;
    }
    has_result =
        instruction->result != VITTE_IR_INVALID_ID;
    if (has_result &&
        !vitte_c17_ir_value_name(
            instruction->result,
            result,
            sizeof(result))) {
        return false;
    }

    if (instruction->opcode == VITTE_IR_OP_BR) {
        if (!vitte_c17_ir_block_name(
                instruction->target_true,
                target,
                sizeof(target)) ||
            !vitte_c17_ir_emit_phi_edges(
                emitter,
                instruction->target_true,
                instruction->block) ||
            !vitte_c17_ir_indent(emitter) ||
            !vitte_c17_ir_write(emitter, "goto ") ||
            !vitte_c17_ir_write(emitter, target) ||
            !vitte_c17_ir_write(emitter, ";\n")) {
            return false;
        }
        return true;
    }
    if (instruction->opcode == VITTE_IR_OP_COND_BR) {
        char true_target[256];
        char false_target[256];
        if (instruction->operand_count != 1u ||
            !vitte_c17_ir_block_name(
                instruction->target_true,
                true_target,
                sizeof(true_target)) ||
            !vitte_c17_ir_block_name(
                instruction->target_false,
                false_target,
                sizeof(false_target)) ||
            !vitte_c17_ir_indent(emitter) ||
            !vitte_c17_ir_write(emitter, "if (") ||
            !vitte_c17_ir_emit_value(
                emitter,
                instruction->operands[0]) ||
            !vitte_c17_ir_write(emitter, ") {\n")) {
            return false;
        }
        ++emitter->indent;
        if (!vitte_c17_ir_emit_phi_edges(
                emitter,
                instruction->target_true,
                instruction->block) ||
            !vitte_c17_ir_indent(emitter) ||
            !vitte_c17_ir_write(emitter, "goto ") ||
            !vitte_c17_ir_write(emitter, true_target) ||
            !vitte_c17_ir_write(emitter, ";\n")) {
            return false;
        }
        --emitter->indent;
        if (!vitte_c17_ir_indent(emitter) ||
            !vitte_c17_ir_write(emitter, "} else {\n")) {
            return false;
        }
        ++emitter->indent;
        if (!vitte_c17_ir_emit_phi_edges(
                emitter,
                instruction->target_false,
                instruction->block) ||
            !vitte_c17_ir_indent(emitter) ||
            !vitte_c17_ir_write(emitter, "goto ") ||
            !vitte_c17_ir_write(emitter, false_target) ||
            !vitte_c17_ir_write(emitter, ";\n")) {
            return false;
        }
        --emitter->indent;
        return vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, "}\n");
    }
    if (instruction->opcode == VITTE_IR_OP_RETURN) {
        if (!vitte_c17_ir_indent(emitter) ||
            !vitte_c17_ir_write(emitter, "return")) {
            return false;
        }
        if (instruction->operand_count != 0u) {
            if (instruction->operand_count != 1u ||
                !vitte_c17_ir_write(emitter, " ") ||
                !vitte_c17_ir_emit_value(
                    emitter,
                    instruction->operands[0])) {
                return false;
            }
        }
        return vitte_c17_ir_write(emitter, ";\n");
    }
    if (instruction->opcode == VITTE_IR_OP_UNREACHABLE) {
        return vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, "abort();\n");
    }
    if (instruction->opcode == VITTE_IR_OP_SWITCH) {
        /* The v1 IR has no case table in its public instruction structure. */
        return false;
    }
    if (instruction->opcode == VITTE_IR_OP_STORE) {
        return instruction->operand_count == 2u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, "*") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, " = ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[1]) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    if (instruction->opcode == VITTE_IR_OP_ALLOCA) {
        if (!has_result) {
            return false;
        }
        return vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = &vitte_slot_") &&
               vitte_c17_ir_write(
                   emitter,
                   result + strlen("vitte_v")) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    if (instruction->opcode == VITTE_IR_OP_LOAD) {
        return has_result &&
               instruction->operand_count == 1u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = *") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    if (instruction->opcode == VITTE_IR_OP_GEP) {
        return has_result &&
               instruction->operand_count == 2u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, " + ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[1]) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    if (instruction->opcode == VITTE_IR_OP_STRING_LENGTH) {
        return has_result &&
               instruction->operand_count == 1u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = (uint64_t)(") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, " == NULL ? 0u : strlen((const char *)") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, "));\n");
    }
    if (instruction->opcode == VITTE_IR_OP_STRING_CONCAT) {
        return has_result &&
               instruction->operand_count == 2u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = vitte_ir_string_concat(") &&
               vitte_c17_ir_emit_value(emitter, instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ", ") &&
               vitte_c17_ir_emit_value(emitter, instruction->operands[1]) &&
               vitte_c17_ir_write(emitter, ");\n");
    }
    if (instruction->opcode == VITTE_IR_OP_STRING_SLICE) {
        return has_result &&
               instruction->operand_count == 3u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = vitte_ir_string_slice(") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ", (uint64_t)") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[1]) &&
               vitte_c17_ir_write(emitter, ", (uint64_t)") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[2]) &&
               vitte_c17_ir_write(emitter, ");\n");
    }
    if (instruction->opcode == VITTE_IR_OP_SLICE_MAKE) {
        return has_result &&
               instruction->operand_count == 2u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = (") &&
               vitte_c17_ir_emit_type(
                   emitter,
                   instruction->result_type) &&
               vitte_c17_ir_write(emitter, "){ ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ", (uint64_t)") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[1]) &&
               vitte_c17_ir_write(emitter, " };\n");
    }
    if (instruction->opcode == VITTE_IR_OP_SLICE_DATA) {
        return has_result &&
               instruction->operand_count == 1u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ".data;\n");
    }
    if (instruction->opcode == VITTE_IR_OP_SLICE_LENGTH) {
        return has_result &&
               instruction->operand_count == 1u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ".length;\n");
    }
    if (instruction->opcode == VITTE_IR_OP_CALL) {
        size_t index;
        const vitte_ir_type_t *result_type =
            vitte_ir_get_type(emitter->ir, instruction->result_type);
        bool returns_void = result_type != NULL &&
            result_type->kind == VITTE_IR_TYPE_VOID;
        if (instruction->operand_count == 0u ||
            (!returns_void && !has_result) ||
            !vitte_c17_ir_indent(emitter) ||
            (!returns_void && (!vitte_c17_ir_write(emitter, result) ||
                               !vitte_c17_ir_write(emitter, " = "))) ||
            !vitte_c17_ir_emit_value(
                emitter,
                instruction->operands[0]) ||
            !vitte_c17_ir_write(emitter, "(")) {
            return false;
        }
        for (index = 1u;
             index < instruction->operand_count;
             ++index) {
            if ((index > 1u &&
                 !vitte_c17_ir_write(emitter, ", ")) ||
                !vitte_c17_ir_emit_value(
                    emitter,
                    instruction->operands[index])) {
                return false;
            }
        }
        return vitte_c17_ir_write(emitter, ");\n");
    }
    if (instruction->opcode == VITTE_IR_OP_SELECT) {
        return has_result &&
               instruction->operand_count == 3u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, " ? ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[1]) &&
               vitte_c17_ir_write(emitter, " : ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[2]) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    if (vitte_c17_ir_binary_operator(instruction->opcode) != NULL) {
        return has_result &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = ") &&
               vitte_c17_ir_emit_binary(emitter, instruction) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    if (instruction->opcode == VITTE_IR_OP_FREM) {
        return has_result &&
               instruction->operand_count == 2u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = fmod(") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ", ") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[1]) &&
               vitte_c17_ir_write(emitter, ");\n");
    }
    if (instruction->opcode == VITTE_IR_OP_NOP) {
        return vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    if (instruction->opcode == VITTE_IR_OP_NEG ||
        instruction->opcode == VITTE_IR_OP_FNEG ||
        instruction->opcode == VITTE_IR_OP_NOT) {
        const char *operator_text =
            instruction->opcode == VITTE_IR_OP_NOT ? "!" : "-";
        return has_result &&
               instruction->operand_count == 1u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = ") &&
               vitte_c17_ir_write(emitter, operator_text) &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    if (instruction->opcode >= VITTE_IR_OP_TRUNC &&
        instruction->opcode <= VITTE_IR_OP_BITCAST) {
        return has_result &&
               instruction->operand_count == 1u &&
               vitte_c17_ir_indent(emitter) &&
               vitte_c17_ir_write(emitter, result) &&
               vitte_c17_ir_write(emitter, " = (") &&
               vitte_c17_ir_emit_type(
                   emitter,
                   instruction->result_type) &&
               vitte_c17_ir_write(emitter, ")") &&
               vitte_c17_ir_emit_value(
                   emitter,
                   instruction->operands[0]) &&
               vitte_c17_ir_write(emitter, ";\n");
    }
    return false;
}

static bool
vitte_c17_ir_emit_function_declaration(
    vitte_c17_ir_emitter_t *emitter,
    const vitte_ir_function_t *function)
{
    const vitte_ir_type_t *type;
    char name[256];
    size_t index;

    if (vitte_c17_ir_is_standard_memory_function(
            emitter->ir,
            function)) {
        return true;
    }

    type = vitte_ir_get_type(emitter->ir, function->type);
    if (type == NULL ||
        type->kind != VITTE_IR_TYPE_FUNCTION ||
        !vitte_c17_ir_function_name(
            emitter->ir,
            function->id,
            name,
            sizeof(name)) ||
        !vitte_c17_ir_emit_symbol_comment(emitter, function, name) ||
        !vitte_c17_ir_emit_type(
            emitter,
            type->return_type) ||
        !vitte_c17_ir_write(emitter, " ") ||
        !vitte_c17_ir_write(emitter, name) ||
        !vitte_c17_ir_write(emitter, "(")) {
        return false;
    }
    if (type->parameter_count == 0u &&
        !vitte_c17_ir_write(emitter, "void")) {
        return false;
    }
    for (index = 0u;
         index < type->parameter_count;
         ++index) {
        char parameter[256];
        if ((index != 0u &&
             !vitte_c17_ir_write(emitter, ", ")) ||
            index >= function->parameter_count ||
            !vitte_c17_ir_emit_type(
                emitter,
                type->parameter_types[index]) ||
            !vitte_c17_ir_write(emitter, " ") ||
            !vitte_c17_ir_value_name(
                function->parameters[index],
                parameter,
                sizeof(parameter)) ||
            !vitte_c17_ir_write(emitter, parameter)) {
            return false;
        }
    }
    return vitte_c17_ir_write(emitter, ");\n");
}

static bool
vitte_c17_ir_emit_function_definition(
    vitte_c17_ir_emitter_t *emitter,
    const vitte_ir_function_t *function)
{
    const vitte_ir_type_t *type;
    char name[256];
    size_t block_index;
    size_t instruction_index;

    if (function->is_declaration) {
        return true;
    }
    type = vitte_ir_get_type(emitter->ir, function->type);
    if (type == NULL ||
        type->kind != VITTE_IR_TYPE_FUNCTION ||
        !vitte_c17_ir_function_name(
            emitter->ir,
            function->id,
            name,
            sizeof(name)) ||
        !vitte_c17_ir_emit_symbol_comment(emitter, function, name) ||
        !vitte_c17_ir_emit_type(
            emitter,
            type->return_type) ||
        !vitte_c17_ir_write(emitter, " ") ||
        !vitte_c17_ir_write(emitter, name) ||
        !vitte_c17_ir_write(emitter, "(")) {
        return false;
    }
    if (type->parameter_count == 0u &&
        !vitte_c17_ir_write(emitter, "void")) {
        return false;
    }
    for (instruction_index = 0u;
         instruction_index < type->parameter_count;
         ++instruction_index) {
        char parameter[256];
        if ((instruction_index != 0u &&
             !vitte_c17_ir_write(emitter, ", ")) ||
            instruction_index >= function->parameter_count ||
            !vitte_c17_ir_emit_type(
                emitter,
                type->parameter_types[instruction_index]) ||
            !vitte_c17_ir_write(emitter, " ") ||
            !vitte_c17_ir_value_name(
                function->parameters[instruction_index],
                parameter,
                sizeof(parameter)) ||
            !vitte_c17_ir_write(emitter, parameter)) {
            return false;
        }
    }
    if (!vitte_c17_ir_write(emitter, ") {\n")) {
        return false;
    }
    ++emitter->indent;

    /* Declarations precede labels so gotos never cross initialization. */
    for (block_index = 0u;
         block_index < function->block_count;
         ++block_index) {
        const vitte_ir_block_t *block =
            vitte_ir_get_block(
                emitter->ir,
                function->blocks[block_index]);
        if (block == NULL) {
            return false;
        }
        for (instruction_index = 0u;
             instruction_index < block->instruction_count;
             ++instruction_index) {
            const vitte_ir_instruction_t *instruction =
                vitte_ir_get_instruction(
                    emitter->ir,
                    block->instructions[instruction_index]);
            char result[256];
            if (instruction == NULL) {
                return false;
            }
            if (instruction->result != VITTE_IR_INVALID_ID &&
                (vitte_ir_get_type(
                    emitter->ir,
                    instruction->result_type) == NULL ||
                 vitte_ir_get_type(
                    emitter->ir,
                    instruction->result_type)->kind != VITTE_IR_TYPE_VOID)) {
                if (!vitte_c17_ir_emit_type(
                        emitter,
                        instruction->result_type) ||
                    !vitte_c17_ir_write(emitter, " ") ||
                    !vitte_c17_ir_value_name(
                        instruction->result,
                        result,
                        sizeof(result)) ||
                    !vitte_c17_ir_write(emitter, result) ||
                    !vitte_c17_ir_write(emitter, " = 0;\n")) {
                    return false;
                }
            }
            if (instruction->opcode == VITTE_IR_OP_ALLOCA) {
                const vitte_ir_type_t *pointer =
                    vitte_ir_get_type(
                        emitter->ir,
                        instruction->result_type);
                if (pointer == NULL ||
                    pointer->kind != VITTE_IR_TYPE_POINTER ||
                    !vitte_c17_ir_emit_type(
                        emitter,
                        pointer->element_type) ||
                    !vitte_c17_ir_write(emitter, " vitte_slot_") ||
                    !vitte_c17_ir_write(
                        emitter,
                        result + strlen("vitte_v")) ||
                    !vitte_c17_ir_write(emitter, ";\n")) {
                    return false;
                }
            }
        }
    }
    for (block_index = 0u;
         block_index < function->block_count;
         ++block_index) {
        const vitte_ir_block_t *block =
            vitte_ir_get_block(
                emitter->ir,
                function->blocks[block_index]);
        char label[256];
        if (block == NULL ||
            !vitte_c17_ir_block_name(
                block->id,
                label,
                sizeof(label)) ||
            !vitte_c17_ir_write(emitter, label) ||
            !vitte_c17_ir_write(emitter, ":\n")) {
            return false;
        }
        for (instruction_index = 0u;
             instruction_index < block->instruction_count;
             ++instruction_index) {
            const vitte_ir_instruction_t *instruction =
                vitte_ir_get_instruction(
                    emitter->ir,
                    block->instructions[instruction_index]);
            if (!vitte_c17_ir_emit_instruction(
                    emitter,
                    instruction)) {
                return false;
            }
        }
    }
    --emitter->indent;
    return vitte_c17_ir_write(emitter, "}\n\n");
}

static bool
vitte_c17_ir_emit_translation_unit(
    FILE *output,
    const vitte_ir_context_t *ir,
    const vitte_c17_ir_emit_options_t *options)
{
    vitte_c17_ir_emitter_t emitter;
    size_t index;
    vitte_ir_function_id_t entry;

    memset(&emitter, 0, sizeof(emitter));
    emitter.output = output;
    emitter.ir = ir;
    emitter.options = *options;
    if (emitter.options.emit_banner &&
        !vitte_c17_ir_write(
            &emitter,
            "/* Generated by Vitte bootstrap C17 backend. */\n")) {
        return false;
    }
    if (emitter.options.source_path != NULL &&
        emitter.options.emit_debug_comments &&
        !vitte_c17_ir_printf(
            &emitter,
            "/* source: %s */\n",
            emitter.options.source_path)) {
        return false;
    }
    if (!vitte_c17_ir_emit_line_directive(&emitter, 1u)) {
        return false;
    }
    if (!vitte_c17_ir_write(
            &emitter,
            "#include <stdbool.h>\n"
            "#include <stddef.h>\n"
            "#include <stdint.h>\n"
            "#include <math.h>\n"
            "#include <stdio.h>\n"
            "#include <stdlib.h>\n"
            "#include <string.h>\n\n")) {
        return false;
    }
    if (vitte_c17_ir_has_string_slice(ir) &&
        !vitte_c17_ir_write(
            &emitter,
            "static inline uint8_t *vitte_ir_string_slice(\n"
            "    const uint8_t *value, uint64_t begin, uint64_t end) {\n"
            "    size_t length;\n"
            "    size_t count;\n"
            "    char *copy;\n"
            "    if (value == NULL) { value = (const uint8_t *)\"\"; }\n"
            "    length = strlen((const char *)value);\n"
            "    if (begin > (uint64_t)length) { begin = (uint64_t)length; }\n"
            "    if (end > (uint64_t)length) { end = (uint64_t)length; }\n"
            "    if (end < begin) { end = begin; }\n"
            "    count = (size_t)(end - begin);\n"
            "    if (count == SIZE_MAX) { abort(); }\n"
            "    copy = (char *)malloc(count + 1u);\n"
            "    if (copy == NULL) { abort(); }\n"
            "    memcpy(copy, value + (size_t)begin, count);\n"
            "    copy[count] = '\\0';\n"
            "    return (uint8_t *)copy;\n"
            "}\n\n")) {
        return false;
    }
    if (vitte_c17_ir_has_string_concat(ir) &&
        !vitte_c17_ir_write(
            &emitter,
            "static inline uint8_t *vitte_ir_string_concat(\n"
            "    const uint8_t *left, const uint8_t *right) {\n"
            "    size_t left_length;\n"
            "    size_t right_length;\n"
            "    char *result;\n"
            "    if (left == NULL) { left = (const uint8_t *)\"\"; }\n"
            "    if (right == NULL) { right = (const uint8_t *)\"\"; }\n"
            "    left_length = strlen((const char *)left);\n"
            "    right_length = strlen((const char *)right);\n"
            "    if (right_length == SIZE_MAX ||\n"
            "        left_length > SIZE_MAX - right_length - 1u) { abort(); }\n"
            "    result = (char *)malloc(left_length + right_length + 1u);\n"
            "    if (result == NULL) { abort(); }\n"
            "    memcpy(result, left, left_length);\n"
            "    memcpy(result + left_length, right, right_length + 1u);\n"
            "    return (uint8_t *)result;\n"
            "}\n\n")) {
        return false;
    }
    for (index = 0u;
         index < ir->type_count;
         ++index) {
        const vitte_ir_type_t *type = &ir->types[index];
        char name[256];
        if ((type->kind == VITTE_IR_TYPE_STRUCT ||
             type->kind == VITTE_IR_TYPE_OPAQUE) &&
            !vitte_c17_ir_name(
                ir,
                type->name,
                type->id,
                "vitte_type_",
                name,
                sizeof(name))) {
            return false;
        }
        if ((type->kind == VITTE_IR_TYPE_STRUCT ||
             type->kind == VITTE_IR_TYPE_OPAQUE) &&
            !vitte_c17_ir_printf(
                &emitter,
                "struct %s { unsigned char _vitte_placeholder; };\n",
                name)) {
            return false;
        }
        if (type->kind == VITTE_IR_TYPE_SLICE) {
            if (!vitte_c17_ir_name(
                    ir,
                    type->name,
                    type->id,
                    "vitte_slice_",
                    name,
                    sizeof(name)) ||
                !vitte_c17_ir_printf(&emitter, "struct %s { ", name) ||
                !vitte_c17_ir_emit_type(&emitter, type->element_type) ||
                !vitte_c17_ir_write(
                    &emitter,
                    " *data; uint64_t length; };\n")) {
                return false;
            }
        }
    }
    if (!vitte_c17_ir_write(&emitter, "\n")) {
        return false;
    }
    for (index = 0u;
         index < ir->function_count;
         ++index) {
        if (!vitte_c17_ir_emit_function_declaration(
                &emitter,
                &ir->functions[index])) {
            return false;
        }
    }
    if (!vitte_c17_ir_write(&emitter, "\n")) {
        return false;
    }
    for (index = 0u;
         index < ir->function_count;
         ++index) {
        if (!vitte_c17_ir_emit_function_definition(
                &emitter,
                &ir->functions[index])) {
            return false;
        }
    }

    entry = VITTE_IR_INVALID_ID;
    if (emitter.options.emit_entry_wrapper) {
        for (index = 0u;
             index < ir->function_count;
             ++index) {
            const vitte_ir_function_t *function =
                &ir->functions[index];
            const vitte_ir_string_t *name =
                vitte_c17_ir_string(ir, function->name);
            if (name != NULL &&
                name->length == 4u &&
                memcmp(name->data, "main", 4u) == 0 &&
                function->parameter_count == 0u) {
                entry = function->id;
                break;
            }
        }
    }
    if (entry != VITTE_IR_INVALID_ID) {
        char name[256];
        const vitte_ir_type_t *entry_type =
            vitte_ir_get_type(emitter.ir,
                              vitte_ir_get_function(emitter.ir, entry)->type);
        if (!vitte_c17_ir_function_name(
                ir,
                entry,
                name,
                sizeof(name)) || entry_type == NULL ||
            !vitte_c17_ir_write(&emitter, "int main(void) {\n    ")) {
            return false;
        }
        if (entry_type->return_type != VITTE_IR_INVALID_ID &&
            vitte_ir_get_type(ir, entry_type->return_type) != NULL &&
            vitte_ir_get_type(ir, entry_type->return_type)->kind ==
                VITTE_IR_TYPE_VOID) {
            if (!vitte_c17_ir_write(&emitter, name) ||
                !vitte_c17_ir_write(&emitter, "();\n    return 0;\n}\n")) {
                return false;
            }
        } else if (!vitte_c17_ir_write(&emitter, "return (int)") ||
                   !vitte_c17_ir_write(&emitter, name) ||
                   !vitte_c17_ir_write(&emitter, "();\n}\n")) {
            return false;
        }
    }
    return !emitter.failed && fflush(output) == 0;
}

vitte_c17_ir_emit_options_t
vitte_c17_ir_emit_options_default(void)
{
    vitte_c17_ir_emit_options_t options;

    memset(&options, 0, sizeof(options));
    options.emit_banner = true;
    options.emit_entry_wrapper = true;
    options.strict = true;
    return options;
}

bool
vitte_c17_emit_ir(
    FILE *output,
    const vitte_ir_context_t *ir,
    const vitte_c17_ir_emit_options_t *options)
{
    vitte_c17_ir_emit_options_t defaults;

    if (output == NULL ||
        ir == NULL ||
        ir->magic != VITTE_IR_MAGIC ||
        ir->state == VITTE_IR_STATE_DESTROYED ||
        ir->state == VITTE_IR_STATE_INVALID) {
        return false;
    }
    if (!vitte_ir_validate((vitte_ir_context_t *)ir)) {
        return false;
    }
    defaults = vitte_c17_ir_emit_options_default();
    if (options == NULL) {
        options = &defaults;
    }
    return vitte_c17_ir_emit_translation_unit(output, ir, options);
}

#if defined(__APPLE__) || defined(__unix__) || defined(_WIN32)
static vitte_c17_ir_compile_result_t
vitte_c17_ir_compile_temp(
    const vitte_ir_context_t *ir,
    const char *output_path,
    bool object,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_ir_emit_options_t *options)
{
    char source_path[4096] = "/tmp/vitte-ir-XXXXXX";
    char object_path[4096] = "/tmp/vitte-ir-object-XXXXXX";
    char executable_path[4096] = "/tmp/vitte-ir-exe-XXXXXX";
    int source_descriptor;
    int object_descriptor;
    int executable_descriptor;
    FILE *source;
    const char *arguments[16];
    const char *link_arguments[5];
    size_t count;
    size_t link_count;
    bool success;
    vitte_c17_ir_compile_result_t result;

    if (ir == NULL || (output_path == NULL && !run) ||
        (run && (run_argc < 0 || (run_argc != 0 && run_argv == NULL)))) {
        return VITTE_C17_IR_COMPILE_INVALID_ARGUMENT;
    }
    source_descriptor = mkstemp(source_path);
    if (source_descriptor < 0) {
        return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
    }
    source = fdopen(source_descriptor, "w");
    if (source == NULL) {
        (void)close(source_descriptor);
        (void)unlink(source_path);
        return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
    }
    success = vitte_c17_emit_ir(source, ir, options);
    if (fclose(source) != 0) {
        (void)unlink(source_path);
        return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
    }
    if (!success) {
        (void)unlink(source_path);
        return VITTE_C17_IR_COMPILE_EMIT_FAILED;
    }

    if (!object) {
        object_descriptor = mkstemp(object_path);
        if (object_descriptor < 0) {
            (void)unlink(source_path);
            return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
        }
        if (close(object_descriptor) != 0) {
            (void)unlink(source_path);
            (void)unlink(object_path);
            return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
        }
    }
    if (run) {
        executable_descriptor = mkstemp(executable_path);
        if (executable_descriptor < 0) {
            (void)unlink(source_path);
            (void)unlink(object_path);
            return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
        }
        if (close(executable_descriptor) != 0 ||
            unlink(executable_path) != 0) {
            (void)unlink(source_path);
            (void)unlink(object_path);
            (void)unlink(executable_path);
            return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
        }
#if defined(_WIN32)
        if (!vitte_c17_ir_set_executable_extension(executable_path)) {
            (void)unlink(source_path);
            (void)unlink(object_path);
            return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
        }
#endif
    }
    count = 0u;
    arguments[count++] = "cc";
    if (options != NULL && options->emit_debug_info) {
        arguments[count++] = "-g";
        arguments[count++] = "-O0";
        arguments[count++] = "-fno-omit-frame-pointer";
        arguments[count++] = "-fno-inline";
    }
    arguments[count++] = "-std=c17";
    arguments[count++] = "-x";
    arguments[count++] = "c";
    arguments[count++] = "-c";
    arguments[count++] = source_path;
    arguments[count++] = "-o";
    arguments[count++] = object ? output_path : object_path;
    arguments[count] = NULL;
    success = vitte_platform_run(arguments, true);
    (void)unlink(source_path);
    if (!success) {
        (void)unlink(object ? output_path : object_path);
        (void)unlink(executable_path);
        return VITTE_C17_IR_COMPILE_C_COMPILATION_FAILED;
    }
    if (object) {
        return VITTE_C17_IR_COMPILE_SUCCESS;
    }

    link_count = 0u;
    link_arguments[link_count++] = "cc";
    link_arguments[link_count++] = object_path;
    link_arguments[link_count++] = "-o";
    link_arguments[link_count++] = run ? executable_path : output_path;
    link_arguments[link_count] = NULL;
    success = vitte_platform_run(link_arguments, true);
    (void)unlink(object_path);
    if (!success) {
        (void)unlink(run ? executable_path : output_path);
        return VITTE_C17_IR_COMPILE_LINK_FAILED;
    }
    if (run) {
        char **child_argv;
        size_t index;

        if ((size_t)run_argc > SIZE_MAX / sizeof(*child_argv) - 2u) {
            (void)unlink(executable_path);
            return VITTE_C17_IR_COMPILE_INVALID_ARGUMENT;
        }
        child_argv = (char **)calloc(
            (size_t)run_argc + 2u,
            sizeof(*child_argv));
        if (child_argv == NULL) {
            (void)unlink(executable_path);
            return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
        }
        child_argv[0] = executable_path;
        for (index = 0u;
             index < (size_t)run_argc;
             ++index) {
            child_argv[index + 1u] = run_argv[index];
        }
        child_argv[(size_t)run_argc + 1u] = NULL;
        success = vitte_platform_run(
            (const char *const *)child_argv,
            true);
        free(child_argv);
        (void)unlink(executable_path);
        result = success
            ? VITTE_C17_IR_COMPILE_SUCCESS
            : VITTE_C17_IR_COMPILE_RUNTIME_FAILED;
        return result;
    }
    return VITTE_C17_IR_COMPILE_SUCCESS;
}
#endif

vitte_c17_ir_compile_result_t
vitte_c17_compile_ir_result(
    const vitte_ir_context_t *ir,
    const char *output_path,
    bool emit_c,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_ir_emit_options_t *options)
{
    if (ir == NULL || (output_path != NULL && output_path[0] == '\0') ||
        (run && (run_argc < 0 || (run_argc != 0 && run_argv == NULL)))) {
        return VITTE_C17_IR_COMPILE_INVALID_ARGUMENT;
    }
    if (emit_c) {
        FILE *output;
        bool success;

        output = output_path == NULL
            ? stdout
            : fopen(output_path, "wb");
        if (output == NULL) {
            return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
        }
        success = vitte_c17_emit_ir(output, ir, options);
        if (output_path != NULL && fclose(output) != 0) {
            return VITTE_C17_IR_COMPILE_OUTPUT_FAILED;
        }
        return success
            ? VITTE_C17_IR_COMPILE_SUCCESS
            : VITTE_C17_IR_COMPILE_EMIT_FAILED;
    }
#if defined(__APPLE__) || defined(__unix__) || defined(_WIN32)
    return vitte_c17_ir_compile_temp(
        ir,
        output_path,
        false,
        run,
        run_argc,
        run_argv,
        options);
#else
    (void)ir;
    (void)output_path;
    (void)run;
    (void)run_argc;
    (void)run_argv;
    (void)options;
    return VITTE_C17_IR_COMPILE_UNSUPPORTED;
#endif
}

bool
vitte_c17_compile_ir(
    const vitte_ir_context_t *ir,
    const char *output_path,
    bool emit_c,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_ir_emit_options_t *options)
{
    return vitte_c17_compile_ir_result(
               ir,
               output_path,
               emit_c,
               run,
               run_argc,
               run_argv,
               options) == VITTE_C17_IR_COMPILE_SUCCESS;
}

vitte_c17_ir_compile_result_t
vitte_c17_compile_ir_object_result(
    const vitte_ir_context_t *ir,
    const char *output_path,
    const vitte_c17_ir_emit_options_t *options)
{
    if (ir == NULL || output_path == NULL || output_path[0] == '\0') {
        return VITTE_C17_IR_COMPILE_INVALID_ARGUMENT;
    }
#if defined(__APPLE__) || defined(__unix__) || defined(_WIN32)
    return vitte_c17_ir_compile_temp(
        ir,
        output_path,
        true,
        false,
        0,
        NULL,
        options);
#else
    (void)ir;
    (void)output_path;
    (void)options;
    return VITTE_C17_IR_COMPILE_UNSUPPORTED;
#endif
}

bool
vitte_c17_compile_ir_object(
    const vitte_ir_context_t *ir,
    const char *output_path,
    const vitte_c17_ir_emit_options_t *options)
{
    return vitte_c17_compile_ir_object_result(
               ir,
               output_path,
               options) == VITTE_C17_IR_COMPILE_SUCCESS;
}
