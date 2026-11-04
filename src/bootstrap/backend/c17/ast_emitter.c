#if defined(__APPLE__) || defined(__unix__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "ast_emitter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__unix__)
#include <unistd.h>
#endif

#include "../../platform/platform.h"

#define VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX 512u
#define VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX 512u

typedef struct vitte_c17_ast_generic_form_instance {
    const vitte_ast_node_t *declaration;
    const vitte_ast_node_t *type_expression;
    const vitte_ast_node_t **arguments;
    size_t argument_count;
    char argument_name[VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX];
    unsigned char layout_state;
    bool forward_declared;
} vitte_c17_ast_generic_form_instance_t;

typedef struct vitte_c17_ast_generic_proc_instance {
    const vitte_ast_node_t *declaration;
    const vitte_ast_node_t **argument_type_nodes;
    const char **argument_c_types;
    char **argument_type_keys;
    size_t argument_count;
    char c_name[96];
} vitte_c17_ast_generic_proc_instance_t;

typedef struct vitte_c17_ast_generic_proc_call_binding {
    size_t call_id;
    size_t owner_instance_index;
    size_t target_instance_index;
} vitte_c17_ast_generic_proc_call_binding_t;

typedef struct vitte_c17_ast_emitter {
    FILE *output;
    const vitte_parser_t *parser;
    const vitte_ast_node_t *function;
    const vitte_ast_node_t *deferred_nodes[512];
    size_t deferred_count;
    size_t loop_defer_bases[128];
    char loop_continue_labels[128][128];
    size_t loop_defer_depth;
    const vitte_ast_node_t **generic_parameter_names;
    const vitte_ast_node_t **generic_arguments;
    const char **generic_c_type_names;
    size_t generic_substitution_count;
    size_t generic_substitution_capacity;
    size_t *generic_call_instance_ids;
    const vitte_ast_node_t **procedure_declarations;
    size_t procedure_declaration_count;
    size_t procedure_declaration_capacity;
    vitte_c17_ast_generic_proc_instance_t *generic_procs;
    size_t generic_proc_count;
    size_t generic_proc_capacity;
    vitte_c17_ast_generic_proc_call_binding_t *generic_proc_call_bindings;
    size_t generic_proc_call_binding_count;
    size_t generic_proc_call_binding_capacity;
    size_t generic_proc_context_index;
    bool generic_proc_context_active;
    const char *generic_proc_c_name;
    const char *function_c_name_override;
    unsigned int async_signature_mode;
    const vitte_ast_node_t *space_stack[128];
    size_t space_depth;
    vitte_ast_node_id_t *parent_ids;
    size_t parent_id_count;
    vitte_c17_ast_generic_form_instance_t *generic_forms;
    size_t generic_form_count;
    size_t generic_form_capacity;
    const char *source_path;
    bool debug_info;
    bool debug_runtime;
    bool emit_line_directives;
    struct {
        const char *lexeme;
        size_t length;
    } debug_scalar_names[128];
    size_t debug_scalar_name_count;
    bool failed;
} vitte_c17_ast_emitter_t;

static bool
vitte_c17_ast_is_intrinsic(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node);

static bool
vitte_c17_ast_pick_equality_supported(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pick);

static bool
vitte_c17_ast_pick_has_generic_parameters(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node);

static bool
vitte_c17_ast_emit_pick(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node);

static const vitte_ast_node_t *
vitte_c17_ast_child(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    size_t index);

static bool
vitte_c17_ast_generic_impl_bindings(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *implementation,
    const vitte_ast_node_t *target,
    const vitte_ast_node_t *concrete_type,
    const vitte_ast_node_t **arguments,
    size_t argument_capacity,
    size_t *argument_count);

static const vitte_ast_node_t *
vitte_c17_ast_async_task_origin(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression,
    size_t depth);

static const vitte_ast_node_t *
vitte_c17_ast_find_local_initializer(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name);

static void
vitte_c17_ast_write_async_task_type_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function);

static void
vitte_c17_ast_write_async_task_function_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function,
    const char *suffix);

static const vitte_ast_node_t *
vitte_c17_ast_unwrap_groups(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression);

static bool
vitte_c17_ast_token_equal(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *left,
    const vitte_ast_node_t *right);

static const vitte_ast_node_t *
vitte_c17_ast_find_form(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name);

static void
vitte_c17_ast_emit_generic_form_instance_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_c17_ast_generic_form_instance_t *instance);

static const vitte_c17_ast_generic_form_instance_t *
vitte_c17_ast_generic_form_instance_for_type(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type);

static bool
vitte_c17_ast_generic_argument_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    char *name,
    size_t name_capacity,
    size_t depth);

static const char *
vitte_c17_ast_source_scalar_name_from_c_type(const char *c_type);

static bool
vitte_c17_ast_emit_generic_form_layout_ordered(
    vitte_c17_ast_emitter_t *emitter,
    size_t instance_index,
    size_t depth);

static bool
vitte_c17_ast_emit_generic_pick_layout_ordered(
    vitte_c17_ast_emitter_t *emitter,
    size_t instance_index,
    size_t depth);

static const vitte_ast_node_t *
vitte_c17_ast_find_path_declaration(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *path,
    vitte_ast_node_kind_t first_kind,
    vitte_ast_node_kind_t second_kind);

static void
vitte_c17_ast_emit_declaration_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *declaration,
    const vitte_ast_node_t *name);

static const vitte_ast_node_t *
vitte_c17_ast_node(
    const vitte_c17_ast_emitter_t *emitter,
    vitte_ast_node_id_t id)
{
    return vitte_parser_get_node(emitter->parser, id);
}

static bool
vitte_c17_ast_emitter_reserve_generic_forms(
    vitte_c17_ast_emitter_t *emitter,
    size_t required)
{
    size_t capacity;
    size_t bytes;
    vitte_c17_ast_generic_form_instance_t *forms;

    if (emitter == NULL) {
        return false;
    }
    if (required <= emitter->generic_form_capacity) {
        return true;
    }
    capacity = emitter->generic_form_capacity != 0u
        ? emitter->generic_form_capacity
        : 8u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < required ||
        capacity > SIZE_MAX / sizeof(*forms)) {
        return false;
    }
    bytes = capacity * sizeof(*forms);
    forms = (vitte_c17_ast_generic_form_instance_t *)realloc(
        emitter->generic_forms,
        bytes);
    if (forms == NULL) {
        return false;
    }
    memset(
        forms + emitter->generic_form_capacity,
        0,
        (capacity - emitter->generic_form_capacity) * sizeof(*forms));
    emitter->generic_forms = forms;
    emitter->generic_form_capacity = capacity;
    return true;
}

static bool
vitte_c17_ast_emitter_reserve_generic_procs(
    vitte_c17_ast_emitter_t *emitter,
    size_t required)
{
    size_t capacity;
    vitte_c17_ast_generic_proc_instance_t *procedures;

    if (emitter == NULL) {
        return false;
    }
    if (required <= emitter->generic_proc_capacity) {
        return true;
    }
    capacity = emitter->generic_proc_capacity != 0u
        ? emitter->generic_proc_capacity
        : 8u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < required ||
        capacity > SIZE_MAX / sizeof(*procedures)) {
        return false;
    }
    procedures = (vitte_c17_ast_generic_proc_instance_t *)realloc(
        emitter->generic_procs,
        capacity * sizeof(*procedures));
    if (procedures == NULL) {
        return false;
    }
    memset(
        procedures + emitter->generic_proc_capacity,
        0,
        (capacity - emitter->generic_proc_capacity) * sizeof(*procedures));
    emitter->generic_procs = procedures;
    emitter->generic_proc_capacity = capacity;
    return true;
}

static bool
vitte_c17_ast_emitter_reserve_procedure_declarations(
    vitte_c17_ast_emitter_t *emitter,
    size_t required)
{
    size_t capacity;
    const vitte_ast_node_t **declarations;
    if (emitter == NULL) { return false; }
    if (required <= emitter->procedure_declaration_capacity) {
        return true;
    }
    capacity = emitter->procedure_declaration_capacity != 0u
        ? emitter->procedure_declaration_capacity
        : 8u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < required ||
        capacity > SIZE_MAX / sizeof(*declarations)) {
        return false;
    }
    declarations = (const vitte_ast_node_t **)realloc(
        (void *)emitter->procedure_declarations,
        capacity * sizeof(*declarations));
    if (declarations == NULL) { return false; }
    emitter->procedure_declarations = declarations;
    emitter->procedure_declaration_capacity = capacity;
    return true;
}

static bool
vitte_c17_ast_emitter_reserve_generic_proc_call_bindings(
    vitte_c17_ast_emitter_t *emitter,
    size_t required)
{
    size_t capacity;
    vitte_c17_ast_generic_proc_call_binding_t *bindings;

    if (emitter == NULL) { return false; }
    if (required <= emitter->generic_proc_call_binding_capacity) {
        return true;
    }
    capacity = emitter->generic_proc_call_binding_capacity != 0u
        ? emitter->generic_proc_call_binding_capacity
        : 8u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < required ||
        capacity > SIZE_MAX / sizeof(*bindings)) {
        return false;
    }
    bindings = (vitte_c17_ast_generic_proc_call_binding_t *)realloc(
        emitter->generic_proc_call_bindings,
        capacity * sizeof(*bindings));
    if (bindings == NULL) { return false; }
    emitter->generic_proc_call_bindings = bindings;
    emitter->generic_proc_call_binding_capacity = capacity;
    return true;
}

static void
vitte_c17_ast_emitter_release_generic_state(
    vitte_c17_ast_emitter_t *emitter)
{
    if (emitter == NULL) {
        return;
    }
    free(emitter->parent_ids);
    free(emitter->generic_parameter_names);
    free(emitter->generic_arguments);
    free(emitter->generic_c_type_names);
    free(emitter->generic_call_instance_ids);
    free((void *)emitter->procedure_declarations);
    free(emitter->generic_proc_call_bindings);
    {
        size_t index;
        for (index = 0u; index < emitter->generic_form_count; ++index) {
            free((void *)emitter->generic_forms[index].arguments);
            emitter->generic_forms[index].arguments = NULL;
        }
        for (index = 0u; index < emitter->generic_proc_count; ++index) {
            size_t argument_index;
            for (argument_index = 0u;
                 argument_index <
                     emitter->generic_procs[index].argument_count;
                 ++argument_index) {
                free(emitter->generic_procs[index].argument_type_keys != NULL
                    ? emitter->generic_procs[index].argument_type_keys[
                          argument_index]
                    : NULL);
            }
            free((void *)emitter->generic_procs[index].argument_type_nodes);
            free((void *)emitter->generic_procs[index].argument_c_types);
            free(emitter->generic_procs[index].argument_type_keys);
            emitter->generic_procs[index].argument_type_nodes = NULL;
            emitter->generic_procs[index].argument_c_types = NULL;
            emitter->generic_procs[index].argument_type_keys = NULL;
        }
    }
    free(emitter->generic_forms);
    free(emitter->generic_procs);
    emitter->parent_ids = NULL;
    emitter->parent_id_count = 0u;
    emitter->generic_parameter_names = NULL;
    emitter->generic_arguments = NULL;
    emitter->generic_c_type_names = NULL;
    emitter->generic_substitution_count = 0u;
    emitter->generic_substitution_capacity = 0u;
    emitter->generic_call_instance_ids = NULL;
    emitter->procedure_declarations = NULL;
    emitter->procedure_declaration_count = 0u;
    emitter->procedure_declaration_capacity = 0u;
    emitter->generic_procs = NULL;
    emitter->generic_proc_count = 0u;
    emitter->generic_proc_capacity = 0u;
    emitter->generic_proc_call_bindings = NULL;
    emitter->generic_proc_call_binding_count = 0u;
    emitter->generic_proc_call_binding_capacity = 0u;
    emitter->generic_proc_context_index = 0u;
    emitter->generic_proc_context_active = false;
    emitter->generic_proc_c_name = NULL;
    emitter->generic_forms = NULL;
    emitter->generic_form_count = 0u;
    emitter->generic_form_capacity = 0u;
}

static bool
vitte_c17_ast_build_parent_index(
    vitte_c17_ast_emitter_t *emitter)
{
    size_t index;

    if (emitter->parser == NULL ||
        emitter->parser->node_count == SIZE_MAX) {
        return false;
    }
    emitter->parent_id_count = emitter->parser->node_count + 1u;
    emitter->parent_ids = (vitte_ast_node_id_t *)calloc(
        emitter->parent_id_count,
        sizeof(*emitter->parent_ids));
    emitter->generic_parameter_names =
        (const vitte_ast_node_t **)calloc(
            emitter->parent_id_count,
            sizeof(*emitter->generic_parameter_names));
    emitter->generic_arguments =
        (const vitte_ast_node_t **)calloc(
            emitter->parent_id_count,
            sizeof(*emitter->generic_arguments));
    emitter->generic_c_type_names = (const char **)calloc(
        emitter->parent_id_count,
        sizeof(*emitter->generic_c_type_names));
    emitter->generic_call_instance_ids = (size_t *)calloc(
        emitter->parent_id_count,
        sizeof(*emitter->generic_call_instance_ids));
    if (emitter->parent_ids == NULL ||
        emitter->generic_parameter_names == NULL ||
        emitter->generic_arguments == NULL ||
        emitter->generic_c_type_names == NULL ||
        emitter->generic_call_instance_ids == NULL) {
        vitte_c17_ast_emitter_release_generic_state(emitter);
        return false;
    }
    emitter->generic_substitution_capacity = emitter->parent_id_count;
    for (index = 0u; index < emitter->parser->node_count; ++index) {
        const vitte_ast_node_t *parent = &emitter->parser->nodes[index];
        size_t child_index;

        for (child_index = 0u;
             child_index < parent->child_count;
             ++child_index) {
            vitte_ast_node_id_t child_id = parent->children[child_index];

            if (child_id == VITTE_AST_INVALID_ID ||
                child_id >= emitter->parent_id_count ||
                emitter->parent_ids[(size_t)child_id] !=
                    VITTE_AST_INVALID_ID) {
                vitte_c17_ast_emitter_release_generic_state(emitter);
                return false;
            }
            emitter->parent_ids[(size_t)child_id] = parent->id;
        }
    }
    return true;
}

/* Trait declarations are compile-time contracts; their methods are emitted
 * from concrete implementations. Macros still need expansion before C17. */
static bool
vitte_c17_ast_contains_unlowered_declaration(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    size_t index;

    if (node == NULL) {
        return false;
    }
    if (node->kind == VITTE_AST_NODE_MACRO_DECL ||
        (vitte_ast_node_kind_is_declaration(node->kind) &&
         (node->flags & VITTE_AST_FLAG_COMPTIME) != 0u)) {
        return true;
    }
    for (index = 0u; index < node->child_count; ++index) {
        if (vitte_c17_ast_contains_unlowered_declaration(
                emitter,
                vitte_c17_ast_child(emitter, node, index))) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_attribute_is_cold(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_attribute_t *attribute)
{
    const vitte_token_t *name;

    if (attribute == NULL ||
        attribute->inner ||
        attribute->name_count != 1u ||
        attribute->name_start >= emitter->parser->token_count) {
        return false;
    }

    name = &emitter->parser->tokens[attribute->name_start];
    return name->length == 4u &&
           memcmp(name->lexeme, "cold", 4u) == 0;
}

static bool
vitte_c17_ast_attribute_is_thread_local(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_attribute_t *attribute)
{
    const vitte_token_t *name;

    if (attribute == NULL ||
        attribute->inner ||
        attribute->name_count != 1u ||
        attribute->name_start >= emitter->parser->token_count) {
        return false;
    }

    name = &emitter->parser->tokens[attribute->name_start];
    return name->length == 12u &&
           memcmp(name->lexeme, "thread_local", 12u) == 0;
}

static bool
vitte_c17_ast_contains_unsupported_attributes(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    size_t index;

    if (node == NULL) {
        return false;
    }
    if (node->attribute_count != 0u) {
        bool procedure;
        bool static_declaration;

        procedure =
            node->kind == VITTE_AST_NODE_PROC_DECL ||
            node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL;
        static_declaration =
            node->kind == VITTE_AST_NODE_STATIC_DECL;
        if (!procedure && !static_declaration) {
            return true;
        }
        for (index = 0u; index < node->attribute_count; ++index) {
            const vitte_ast_attribute_t *attribute;

            attribute =
                vitte_parser_get_attribute(
                    emitter->parser,
                    node->id,
                    index);
            if (!(procedure
                      ? vitte_c17_ast_attribute_is_cold(
                            emitter,
                            attribute)
                      : vitte_c17_ast_attribute_is_thread_local(
                            emitter,
                            attribute)) ||
                attribute->argument_count != 0u) {
                return true;
            }
        }
    }
    for (index = 0u; index < node->child_count; ++index) {
        if (vitte_c17_ast_contains_unsupported_attributes(
                emitter,
                vitte_c17_ast_child(emitter, node, index))) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_node_has_thread_local_attribute(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    size_t index;

    if (node == NULL) {
        return false;
    }
    for (index = 0u; index < node->attribute_count; ++index) {
        const vitte_ast_attribute_t *attribute;

        attribute =
            vitte_parser_get_attribute(
                emitter->parser,
                node->id,
                index);
        if (vitte_c17_ast_attribute_is_thread_local(
                emitter,
                attribute) &&
            attribute->argument_count == 0u) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_node_has_cold_attribute(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    size_t index;

    if (node == NULL) {
        return false;
    }
    for (index = 0u; index < node->attribute_count; ++index) {
        const vitte_ast_attribute_t *attribute;

        attribute =
            vitte_parser_get_attribute(
                emitter->parser,
                node->id,
                index);
        if (vitte_c17_ast_attribute_is_cold(emitter, attribute) &&
            attribute->argument_count == 0u) {
            return true;
        }
    }
    return false;
}

static const vitte_token_t *
vitte_c17_ast_token(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    if (node == NULL ||
        node->token_index >= emitter->parser->token_count) {
        return NULL;
    }

    return &emitter->parser->tokens[node->token_index];
}

static void
vitte_c17_ast_write(
    vitte_c17_ast_emitter_t *emitter,
    const char *text)
{
    if (!emitter->failed &&
        fputs(text, emitter->output) == EOF) {
        emitter->failed = true;
    }
}

static void
vitte_c17_ast_emit_source_line(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_token_t *token;
    size_t line;
    size_t index;

    if (emitter == NULL ||
        !emitter->emit_line_directives ||
        emitter->source_path == NULL ||
        emitter->source_path[0] == '\0' ||
        node == NULL) {
        return;
    }

    line = node->span.line;
    token = vitte_c17_ast_token(emitter, node);
    if (line == 0u && token != NULL) {
        line = token->line;
    }
    if (line == 0u) {
        return;
    }

    if (fprintf(
            emitter->output,
            "#line %zu \"",
            line) < 0) {
        emitter->failed = true;
        return;
    }

    for (index = 0u; emitter->source_path[index] != '\0'; ++index) {
        unsigned char character;

        character = (unsigned char)emitter->source_path[index];
        if (character == '\\' || character == '"') {
            if (fputc('\\', emitter->output) == EOF) {
                emitter->failed = true;
                return;
            }
        } else if (character == '\n') {
            if (fputs("\\n", emitter->output) == EOF) {
                emitter->failed = true;
                return;
            }
            continue;
        } else if (character == '\r') {
            if (fputs("\\r", emitter->output) == EOF) {
                emitter->failed = true;
                return;
            }
            continue;
        }
        if (fputc((int)character, emitter->output) == EOF) {
            emitter->failed = true;
            return;
        }
    }

    if (fputs("\"\n", emitter->output) == EOF) {
        emitter->failed = true;
    }
}

static void
vitte_c17_ast_emit_debug_runtime(
    vitte_c17_ast_emitter_t *emitter)
{
    vitte_c17_ast_write(
        emitter,
        "#if defined(VITTE_DEBUG_RUNTIME) && "
        "(defined(__APPLE__) || defined(__unix__))\n"
        "typedef struct vitte_debug_value {\n"
        "    bool active;\n"
        "    char function[64];\n"
        "    char name[64];\n"
        "    int64_t value;\n"
        "} vitte_debug_value_t;\n"
        "static vitte_debug_value_t vitte_debug_values[128];\n"
        "static int vitte_debug_socket = -1;\n"
        "static FILE *vitte_debug_stream = NULL;\n"
        "static void vitte_debug_copy_name(\n"
        "    char *target, size_t capacity, const char *source) {\n"
        "    size_t length;\n"
        "    if (target == NULL || capacity == 0u) { return; }\n"
        "    if (source == NULL) { target[0] = '\\0'; return; }\n"
        "    length = strlen(source);\n"
        "    if (length >= capacity) { length = capacity - 1u; }\n"
        "    memcpy(target, source, length);\n"
        "    target[length] = '\\0';\n"
        "}\n");
    vitte_c17_ast_write(
        emitter,
        "static void vitte_debug_set_i64(\n"
        "    const char *function, const char *name, int64_t value) {\n"
        "    size_t index;\n"
        "    vitte_debug_value_t *slot;\n"
        "    slot = NULL;\n"
        "    for (index = 0u; index < 128u; ++index) {\n"
        "        if (vitte_debug_values[index].active &&\n"
        "            strcmp(vitte_debug_values[index].function,\n"
        "                   function != NULL ? function : \"main\") == 0 &&\n"
        "            strcmp(vitte_debug_values[index].name,\n"
        "                   name != NULL ? name : \"value\") == 0) {\n"
        "            slot = &vitte_debug_values[index];\n"
        "            break;\n"
        "        }\n"
        "    }\n"
        "    if (slot == NULL) {\n"
        "        for (index = 0u; index < 128u; ++index) {\n"
        "            if (!vitte_debug_values[index].active) {\n"
        "                slot = &vitte_debug_values[index];\n"
        "                slot->active = true;\n"
        "                break;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    if (slot == NULL) { return; }\n"
        "    vitte_debug_copy_name(slot->function, sizeof(slot->function),\n"
        "                          function != NULL ? function : \"main\");\n"
        "    vitte_debug_copy_name(slot->name, sizeof(slot->name),\n"
        "                          name != NULL ? name : \"value\");\n"
        "    slot->value = value;\n"
        "}\n"
        "static void vitte_debug_send_values(const char *function) {\n"
        "    size_t index;\n"
        "    const char *current;\n"
        "    current = function != NULL ? function : \"main\";\n"
        "    if (fprintf(vitte_debug_stream, \"vars %s\\n\", current) < 0) {\n"
        "        return;\n"
        "    }\n"
        "    for (index = 0u; index < 128u; ++index) {\n"
        "        if (vitte_debug_values[index].active &&\n"
        "            strcmp(vitte_debug_values[index].function, current) == 0) {\n"
        "            (void)fprintf(vitte_debug_stream, \"var %s %\" PRId64 \"\\n\",\n"
        "                          vitte_debug_values[index].name,\n"
        "                          vitte_debug_values[index].value);\n"
        "        }\n"
        "    }\n"
        "}\n"
        "static bool vitte_debug_connect(void) {\n"
        "    const char *port_text;\n"
        "    char *port_end;\n"
        "    long port;\n"
        "    struct sockaddr_in address;\n"
        "    int socket_fd;\n"
        "    port_text = getenv(\"VITTE_DEBUG_PORT\");\n"
        "    if (port_text == NULL) { return false; }\n"
        "    port = strtol(port_text, &port_end, 10);\n"
        "    if (port_end == port_text || *port_end != '\\0' ||\n"
        "        port < 1L || port > 65535L) { return false; }\n"
        "    socket_fd = socket(AF_INET, SOCK_STREAM, 0);\n"
        "    if (socket_fd < 0) { return false; }\n"
        "    memset(&address, 0, sizeof(address));\n"
        "    address.sin_family = AF_INET;\n"
        "    address.sin_addr.s_addr = htonl(UINT32_C(0x7f000001));\n"
        "    address.sin_port = htons((uint16_t)port);\n"
        "    if (connect(socket_fd, (struct sockaddr *)&address,\n"
        "                sizeof(address)) != 0) {\n"
        "        (void)close(socket_fd);\n"
        "        return false;\n"
        "    }\n"
        "    vitte_debug_stream = fdopen(socket_fd, \"r+\");\n"
        "    if (vitte_debug_stream == NULL) {\n"
        "        (void)close(socket_fd);\n"
        "        return false;\n"
        "    }\n"
        "    vitte_debug_socket = socket_fd;\n"
        "    (void)setvbuf(vitte_debug_stream, NULL, _IOLBF, 0);\n"
        "    return true;\n"
        "}\n"
        "static void vitte_debug_probe(size_t line, const char *function) {\n"
        "    char message[96];\n"
        "    if (vitte_debug_stream == NULL && !vitte_debug_connect()) {\n"
        "        return;\n"
        "    }\n"
        "    vitte_debug_send_values(function);\n"
        "    if (fprintf(vitte_debug_stream, \"line %zu %s\\n\", line,\n"
        "                function != NULL ? function : \"main\") < 0 ||\n"
        "        fflush(vitte_debug_stream) != 0) {\n"
        "        (void)fclose(vitte_debug_stream);\n"
        "        vitte_debug_stream = NULL;\n"
        "        vitte_debug_socket = -1;\n"
        "        return;\n"
        "    }\n"
        "    while (fgets(message, sizeof(message), vitte_debug_stream) != NULL) {\n"
        "        if (strncmp(message, \"continue\", 8u) == 0) {\n"
        "            return;\n"
        "        }\n"
        "        if (strncmp(message, \"stop\", 4u) == 0) {\n"
        "            while (fgets(message, sizeof(message), vitte_debug_stream) != NULL) {\n"
        "                if (strncmp(message, \"continue\", 8u) == 0) {\n"
        "                    return;\n"
        "                }\n"
        "            }\n"
        "            return;\n"
        "        }\n"
        "    }\n"
        "}\n"
        "#else\n"
        "static void vitte_debug_set_i64(\n"
        "    const char *function, const char *name, int64_t value) {\n"
        "    (void)function;\n"
        "    (void)name;\n"
        "    (void)value;\n"
        "}\n"
        "static void vitte_debug_probe(size_t line, const char *function) {\n"
        "    (void)line;\n"
        "    (void)function;\n"
        "}\n"
        "#endif\n\n");
}

static void
vitte_c17_ast_write_token(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_token_t *token;

    token = vitte_c17_ast_token(emitter, node);
    if (token == NULL ||
        token->lexeme == NULL ||
        emitter->failed) {
        emitter->failed = true;
        return;
    }

    if (fwrite(token->lexeme, 1u, token->length, emitter->output) !=
        token->length) {
        emitter->failed = true;
    }
}

static void
vitte_c17_ast_emit_integer_literal(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    static const char signed_max[] = "9223372036854775807";
    const vitte_token_t *token;
    size_t index;
    size_t significant_digits;
    size_t first_significant;
    bool decimal;
    bool started;
    bool exceeds_signed_max;

    token = vitte_c17_ast_token(emitter, node);
    if (token == NULL || token->lexeme == NULL || token->length == 0u) {
        emitter->failed = true;
        return;
    }

    decimal = true;
    started = false;
    significant_digits = 0u;
    first_significant = token->length;
    for (index = 0u; index < token->length; ++index) {
        unsigned char character = (unsigned char)token->lexeme[index];

        if (character == '_') {
            continue;
        }
        if (character < '0' || character > '9') {
            decimal = false;
            break;
        }
        if (!started && character == '0') {
            continue;
        }
        if (!started) {
            started = true;
            first_significant = index;
        }
        ++significant_digits;
    }

    exceeds_signed_max = false;
    if (decimal && significant_digits > sizeof(signed_max) - 1u) {
        exceeds_signed_max = true;
    } else if (decimal &&
               significant_digits == sizeof(signed_max) - 1u) {
        size_t digit_index = 0u;

        for (index = first_significant;
             index < token->length && digit_index < significant_digits;
             ++index) {
            unsigned char character = (unsigned char)token->lexeme[index];

            if (character == '_') {
                continue;
            }
            if (character != (unsigned char)signed_max[digit_index]) {
                exceeds_signed_max =
                    character > (unsigned char)signed_max[digit_index];
                break;
            }
            ++digit_index;
        }
    }

    if (!decimal) {
        vitte_c17_ast_write_token(emitter, node);
        return;
    }

    for (index = 0u; index < token->length; ++index) {
        if (token->lexeme[index] != '_' &&
            fputc((unsigned char)token->lexeme[index], emitter->output) ==
                EOF) {
            emitter->failed = true;
            return;
        }
    }
    if (exceeds_signed_max &&
        fputs("ULL", emitter->output) == EOF) {
        emitter->failed = true;
    }
}

static bool
vitte_c17_ast_token_is(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const char *text)
{
    const vitte_token_t *token;
    size_t length;

    token = vitte_c17_ast_token(emitter, node);
    if (token == NULL || text == NULL) {
        return false;
    }

    length = strlen(text);
    return token->length == length &&
           memcmp(token->lexeme, text, length) == 0;
}

static const vitte_ast_node_t *
vitte_c17_ast_child(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    size_t index)
{
    if (node == NULL || index >= node->child_count) {
        return NULL;
    }

    return vitte_c17_ast_node(emitter, node->children[index]);
}

static bool
vitte_c17_ast_is_visible_local(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression,
    const vitte_ast_node_t *name)
{
    const vitte_ast_node_t *current = expression;

    if (current == NULL || name == NULL || emitter->parent_ids == NULL) {
        return false;
    }
    while (current != NULL && current->id < emitter->parent_id_count) {
        const vitte_ast_node_id_t parent_id =
            emitter->parent_ids[(size_t)current->id];
        const vitte_ast_node_t *parent;
        size_t index;

        if (parent_id == VITTE_AST_INVALID_ID) {
            break;
        }
        parent = vitte_c17_ast_node(emitter, parent_id);
        if (parent == NULL) {
            break;
        }
        if (parent->kind == VITTE_AST_NODE_BLOCK) {
            for (index = 0u; index < parent->child_count; ++index) {
                const vitte_ast_node_t *preceding =
                    vitte_c17_ast_child(emitter, parent, index);
                if (preceding == current) {
                    break;
                }
                if (preceding != NULL &&
                    preceding->kind == VITTE_AST_NODE_LET_STMT &&
                    preceding->child_count != 0u &&
                    vitte_c17_ast_token_equal(
                        emitter,
                        vitte_c17_ast_child(emitter, preceding, 0u),
                        name)) {
                    return true;
                }
            }
        }
        current = parent;
    }
    return false;
}

static void
vitte_c17_ast_emit_expression(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node);

static bool
vitte_c17_ast_expression_is_character(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression);

static bool
vitte_c17_ast_is_string_literal_expression(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    if (node == NULL) {
        return false;
    }

    if (node->kind == VITTE_AST_NODE_STRING_LITERAL) {
        return true;
    }
    if (node->kind == VITTE_AST_NODE_GROUP_EXPR) {
        return vitte_c17_ast_is_string_literal_expression(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u));
    }
    return node->kind == VITTE_AST_NODE_BINARY_EXPR &&
           node->operator_kind == VITTE_TOKEN_PLUS &&
           vitte_c17_ast_is_string_literal_expression(
               emitter,
               vitte_c17_ast_child(emitter, node, 0u)) &&
           vitte_c17_ast_is_string_literal_expression(
               emitter,
               vitte_c17_ast_child(emitter, node, 1u));
}

static void
vitte_c17_ast_emit_string_literal_sequence(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    if (node == NULL) {
        emitter->failed = true;
        return;
    }

    if (node->kind == VITTE_AST_NODE_GROUP_EXPR) {
        vitte_c17_ast_emit_string_literal_sequence(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u));
    } else if (node->kind == VITTE_AST_NODE_BINARY_EXPR &&
               node->operator_kind == VITTE_TOKEN_PLUS) {
        vitte_c17_ast_emit_string_literal_sequence(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u));
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_emit_string_literal_sequence(
            emitter,
            vitte_c17_ast_child(emitter, node, 1u));
    } else if (node->kind == VITTE_AST_NODE_STRING_LITERAL) {
        vitte_c17_ast_write_token(emitter, node);
    } else {
        emitter->failed = true;
    }
}

static void
vitte_c17_ast_emit_condition(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node);

static void
vitte_c17_ast_emit_path(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node);

static bool
vitte_c17_ast_token_equal(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *left,
    const vitte_ast_node_t *right)
{
    const vitte_token_t *left_token;
    const vitte_token_t *right_token;

    left_token = vitte_c17_ast_token(emitter, left);
    right_token = vitte_c17_ast_token(emitter, right);
    return left_token != NULL &&
           right_token != NULL &&
           left_token->length == right_token->length &&
           memcmp(
               left_token->lexeme,
               right_token->lexeme,
               left_token->length) == 0;
}

/*
 * These core pointer procedures are generic syntax for C's built-in pointer
 * operations. The C17 backend lowers their direct calls to macros because C
 * has no generic function type that can preserve the source signatures.
 */
static bool
vitte_c17_ast_is_core_pointer_generic(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *procedure)
{
    const vitte_ast_node_t *name;
    const vitte_token_t *name_token;
    size_t index;
    bool generic;
    static const char *const expected_space_parts[3] = {
        "vitte",
        "core",
        "pointer"
    };
    static const size_t expected_space_lengths[3] = { 5u, 4u, 7u };

    if (emitter == NULL || procedure == NULL ||
        procedure->kind != VITTE_AST_NODE_PROC_DECL ||
        emitter->space_depth == 0u) {
        return false;
    }

    name = vitte_c17_ast_child(emitter, procedure, 0u);
    name_token = vitte_c17_ast_token(emitter, name);
    if (name_token == NULL ||
        !((name_token->length == 7u &&
           memcmp(name_token->lexeme, "is_null", 7u) == 0) ||
          (name_token->length == 4u &&
           memcmp(name_token->lexeme, "read", 4u) == 0) ||
          (name_token->length == 5u &&
           memcmp(name_token->lexeme, "write", 5u) == 0))) {
        return false;
    }

    if (emitter->space_depth < 3u) {
        return false;
    }
    for (index = 0u; index < 3u; ++index) {
        const vitte_ast_node_t *space;
        const vitte_ast_node_t *path;
        const vitte_token_t *part;

        space = emitter->space_stack[
            emitter->space_depth - 3u + index];
        path = vitte_c17_ast_child(emitter, space, 0u);
        if (path == NULL ||
            path->kind != VITTE_AST_NODE_PATH ||
            path->child_count != 1u) {
            return false;
        }
        part = vitte_c17_ast_token(
            emitter,
            vitte_c17_ast_child(emitter, path, 0u));
        if (part == NULL ||
            part->length != expected_space_lengths[index] ||
            memcmp(
                part->lexeme,
                expected_space_parts[index],
                expected_space_lengths[index]) != 0) {
            return false;
        }
    }

    generic = false;
    for (index = 1u; index < procedure->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, procedure, index);
        if (child != NULL &&
            child->kind == VITTE_AST_NODE_GENERIC_PARAM) {
            generic = true;
            break;
        }
    }
    return generic;
}

static bool
vitte_c17_ast_is_function_type_alias(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type_expression)
{
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *name;
    size_t depth;
    size_t node_count;

    if (type_expression == NULL ||
        type_expression->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type_expression->child_count == 0u) {
        return false;
    }
    path = vitte_c17_ast_child(emitter, type_expression, 0u);
    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count == 0u) {
        return false;
    }
    name = vitte_c17_ast_child(emitter, path, path->child_count - 1u);
    node_count = vitte_parser_node_count(emitter->parser);

    /* Follow simple type aliases (such as C17WriteFn = WriteFn) while
     * bounding traversal so malformed cycles cannot loop forever. */
    for (depth = 0u; depth < node_count; ++depth) {
        size_t index;
        bool followed_alias = false;

        for (index = 0u; index < node_count; ++index) {
            const vitte_ast_node_t *declaration = vitte_parser_get_node(
                emitter->parser, (vitte_ast_node_id_t)(index + 1u));
            const vitte_ast_node_t *target;
            const vitte_ast_node_t *target_path;

            if (declaration == NULL ||
                declaration->kind != VITTE_AST_NODE_TYPE_DECL ||
                (declaration->flags & VITTE_AST_FLAG_TYPE_ALIAS) == 0u ||
                declaration->child_count < 2u ||
                !vitte_c17_ast_token_equal(
                    emitter,
                    vitte_c17_ast_child(emitter, declaration, 0u),
                    name)) {
                continue;
            }
            target = vitte_c17_ast_child(
                emitter, declaration, declaration->child_count - 1u);
            if (target != NULL &&
                target->kind == VITTE_AST_NODE_TYPE_EXPR &&
                target->operator_kind == VITTE_TOKEN_KW_PROC) {
                return true;
            }
            if (target == NULL ||
                target->kind != VITTE_AST_NODE_TYPE_EXPR ||
                target->child_count == 0u) {
                return false;
            }
            target_path = vitte_c17_ast_child(emitter, target, 0u);
            if (target_path == NULL ||
                target_path->kind != VITTE_AST_NODE_PATH ||
                target_path->child_count == 0u) {
                return false;
            }
            name = vitte_c17_ast_child(
                emitter, target_path, target_path->child_count - 1u);
            followed_alias = true;
            break;
        }
        if (!followed_alias) {
            return false;
        }
    }
    return false;
}

static void
vitte_c17_ast_emit_function_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name)
{
    size_t space_index;

    for (space_index = emitter->space_depth;
         space_index != 0u;
         --space_index) {
        const vitte_ast_node_t *space;
        const vitte_ast_node_t *body;
        bool matching_declaration;
        size_t declaration_index;

        space = emitter->space_stack[space_index - 1u];
        body = vitte_c17_ast_child(emitter, space, 1u);
        if (body == NULL || body->kind != VITTE_AST_NODE_BLOCK) {
            continue;
        }
        matching_declaration = false;
        for (declaration_index = 0u;
             declaration_index < body->child_count;
             ++declaration_index) {
            const vitte_ast_node_t *declaration;

            declaration = vitte_c17_ast_child(
                emitter,
                body,
                declaration_index);
            if (declaration == NULL ||
                (declaration->kind != VITTE_AST_NODE_PROC_DECL &&
                 declaration->kind != VITTE_AST_NODE_EXTERN_PROC_DECL) ||
                !vitte_c17_ast_token_equal(
                    emitter,
                    vitte_c17_ast_child(emitter, declaration, 0u),
                    name)) {
                continue;
            }
            if (declaration->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) {
                vitte_c17_ast_write_token(emitter, name);
                return;
            }
            matching_declaration = true;
            break;
        }
        if (matching_declaration) {
            break;
        }
    }

    for (space_index = 0u;
         space_index < emitter->space_depth;
         ++space_index) {
        const vitte_ast_node_t *space_path;

        space_path =
            vitte_c17_ast_child(
                emitter,
                emitter->space_stack[space_index],
                0u);
        if (space_path == NULL ||
            space_path->kind != VITTE_AST_NODE_PATH ||
            space_path->child_count == 0u) {
            emitter->failed = true;
            return;
        }
        vitte_c17_ast_emit_path(emitter, space_path);
        vitte_c17_ast_write(emitter, "_");
    }
    vitte_c17_ast_write_token(emitter, name);
}

static size_t
vitte_c17_ast_generic_form_parameter_count(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *declaration,
    bool *has_where)
{
    size_t count = 0u;
    size_t index;

    if (has_where != NULL) {
        *has_where = false;
    }
    if (declaration == NULL ||
        (declaration->kind != VITTE_AST_NODE_FORM_DECL &&
         declaration->kind != VITTE_AST_NODE_PICK_DECL &&
         declaration->kind != VITTE_AST_NODE_PROC_DECL &&
         declaration->kind != VITTE_AST_NODE_EXTERN_PROC_DECL &&
         declaration->kind != VITTE_AST_NODE_TYPE_DECL &&
         declaration->kind != VITTE_AST_NODE_OPAQUE_DECL)) {
        return 0u;
    }
    for (index = 1u; index < declaration->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, declaration, index);
        if (child == NULL) {
            continue;
        }
        if (child->kind == VITTE_AST_NODE_GENERIC_PARAM) {
            ++count;
        } else if (child->kind == VITTE_AST_NODE_WHERE_CLAUSE &&
                   has_where != NULL) {
            *has_where = true;
        }
    }
    return count;
}

static const vitte_ast_node_t *
vitte_c17_ast_generic_form_parameter_name(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *declaration,
    size_t wanted_index)
{
    size_t parameter_index = 0u;
    size_t index;

    if (declaration == NULL) {
        return NULL;
    }
    for (index = 1u; index < declaration->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, declaration, index);
        if (child != NULL &&
            child->kind == VITTE_AST_NODE_GENERIC_PARAM) {
            if (parameter_index == wanted_index) {
                return vitte_c17_ast_child(emitter, child, 0u);
            }
            ++parameter_index;
        }
    }
    return NULL;
}

/* Return the concrete element type of a direct generic alias to a dynamic
 * array, for example `type Values<T> = [T]` used as `Values<i64>`. */
static const vitte_ast_node_t *
vitte_c17_ast_generic_alias_slice_element(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type)
{
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *alias;
    const vitte_ast_node_t *target;
    const vitte_ast_node_t *element;
    const vitte_ast_node_t *element_path;
    const vitte_ast_node_t *element_name;
    size_t parameter_count;
    size_t index;

    if (emitter == NULL || type == NULL ||
        type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count < 2u) {
        return NULL;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    alias = vitte_c17_ast_find_path_declaration(
        emitter,
        path,
        VITTE_AST_NODE_TYPE_DECL,
        VITTE_AST_NODE_TYPE_DECL);
    if (alias == NULL ||
        (alias->flags & VITTE_AST_FLAG_TYPE_ALIAS) == 0u ||
        alias->child_count < 2u) {
        return NULL;
    }
    target = vitte_c17_ast_child(
        emitter, alias, alias->child_count - 1u);
    if (target == NULL || target->kind != VITTE_AST_NODE_ARRAY_TYPE ||
        target->child_count != 1u) {
        return NULL;
    }
    element = vitte_c17_ast_child(emitter, target, 0u);
    element_path = element != NULL &&
            element->kind == VITTE_AST_NODE_TYPE_EXPR
        ? vitte_c17_ast_child(emitter, element, 0u)
        : NULL;
    element_name = element_path != NULL &&
            element_path->kind == VITTE_AST_NODE_PATH &&
            element_path->child_count == 1u
        ? vitte_c17_ast_child(emitter, element_path, 0u)
        : NULL;
    parameter_count = vitte_c17_ast_generic_form_parameter_count(
        emitter, alias, NULL);
    for (index = 0u; index < parameter_count; ++index) {
        const vitte_ast_node_t *parameter =
            vitte_c17_ast_generic_form_parameter_name(
                emitter, alias, index);
        if (element_name != NULL && parameter != NULL &&
            vitte_c17_ast_token_equal(
                emitter, element_name, parameter)) {
            return index + 1u < type->child_count
                ? vitte_c17_ast_child(emitter, type, index + 1u)
                : NULL;
        }
    }
    return element;
}

static bool
vitte_c17_ast_type_expression_is_parameter(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const vitte_ast_node_t *parameter_name);

static size_t
vitte_c17_ast_generic_substitution_index(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type)
{
    size_t index;

    if (emitter == NULL || type == NULL) {
        return SIZE_MAX;
    }
    index = emitter->generic_substitution_count;
    while (index != 0u) {
        --index;
        if (vitte_c17_ast_type_expression_is_parameter(
                emitter,
                type,
                emitter->generic_parameter_names[index])) {
            return index;
        }
    }
    return SIZE_MAX;
}

static const vitte_ast_node_t *
vitte_c17_ast_resolve_generic_argument(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    size_t depth)
{
    size_t substitution_index;

    if (emitter == NULL || type == NULL ||
        depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        return NULL;
    }
    substitution_index =
        vitte_c17_ast_generic_substitution_index(emitter, type);
    if (substitution_index == SIZE_MAX) {
        return type;
    }
    {
        const vitte_ast_node_t *argument =
            emitter->generic_arguments[substitution_index];
        size_t saved_substitution_count =
            emitter->generic_substitution_count;
        const vitte_ast_node_t *resolved;

        if (argument == NULL || argument == type) {
            return NULL;
        }
        emitter->generic_substitution_count = substitution_index;
        resolved = vitte_c17_ast_resolve_generic_argument(
            emitter, argument, depth + 1u);
        emitter->generic_substitution_count = saved_substitution_count;
        return resolved;
    }
}

/* Resolve the concrete element type of a generic alias chain ending in a
 * dynamic array.  Keeping each alias substitution active lets `Values2<T> =
 * Values<T>` resolve the `T` in the final `[T]` target. */
static const vitte_ast_node_t *
vitte_c17_ast_generic_alias_slice_element_recursive(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    size_t depth)
{
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *alias;
    const vitte_ast_node_t *target;
    const vitte_ast_node_t *result = NULL;
    size_t parameter_count;
    size_t saved_count;
    size_t index;

    if (emitter == NULL || type == NULL ||
        type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count < 2u ||
        depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        return NULL;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    alias = vitte_c17_ast_find_path_declaration(
        emitter,
        path,
        VITTE_AST_NODE_TYPE_DECL,
        VITTE_AST_NODE_TYPE_DECL);
    parameter_count = vitte_c17_ast_generic_form_parameter_count(
        emitter, alias, NULL);
    if (alias == NULL || parameter_count == 0u ||
        (alias->flags & VITTE_AST_FLAG_TYPE_ALIAS) == 0u ||
        parameter_count != type->child_count - 1u) {
        return NULL;
    }
    saved_count = emitter->generic_substitution_count;
    if (saved_count > emitter->generic_substitution_capacity ||
        parameter_count >
            emitter->generic_substitution_capacity - saved_count) {
        return NULL;
    }
    for (index = 0u; index < parameter_count; ++index) {
        size_t substitution_index = saved_count + index;
        emitter->generic_parameter_names[substitution_index] =
            vitte_c17_ast_generic_form_parameter_name(
                emitter, alias, index);
        emitter->generic_arguments[substitution_index] =
            vitte_c17_ast_child(emitter, type, index + 1u);
        emitter->generic_c_type_names[substitution_index] = NULL;
        if (emitter->generic_parameter_names[substitution_index] == NULL ||
            emitter->generic_arguments[substitution_index] == NULL) {
            for (index = 0u; index < parameter_count; ++index) {
                size_t cleanup_index = saved_count + index;
                emitter->generic_parameter_names[cleanup_index] = NULL;
                emitter->generic_arguments[cleanup_index] = NULL;
                emitter->generic_c_type_names[cleanup_index] = NULL;
            }
            return NULL;
        }
    }
    emitter->generic_substitution_count = saved_count + parameter_count;
    target = vitte_c17_ast_child(
        emitter, alias, alias->child_count - 1u);
    if (target != NULL && target->kind == VITTE_AST_NODE_ARRAY_TYPE &&
        target->child_count == 1u) {
        result = vitte_c17_ast_resolve_generic_argument(
            emitter,
            vitte_c17_ast_child(emitter, target, 0u),
            depth + 1u);
    } else if (target != NULL &&
               target->kind == VITTE_AST_NODE_TYPE_EXPR &&
               target->child_count > 1u) {
        result = vitte_c17_ast_generic_alias_slice_element_recursive(
            emitter, target, depth + 1u);
    }
    emitter->generic_substitution_count = saved_count;
    for (index = 0u; index < parameter_count; ++index) {
        size_t substitution_index = saved_count + index;
        emitter->generic_parameter_names[substitution_index] = NULL;
        emitter->generic_arguments[substitution_index] = NULL;
        emitter->generic_c_type_names[substitution_index] = NULL;
    }
    return result;
}

static bool
vitte_c17_ast_generic_application_argument_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *application,
    char *name,
    size_t name_capacity,
    size_t depth);

static bool
vitte_c17_ast_simple_type_name(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    char *name,
    size_t name_capacity)
{
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *name_node;
    const vitte_token_t *token;

    if (type == NULL || type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count != 1u || name == NULL || name_capacity == 0u) {
        return false;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count != 1u) {
        return false;
    }
    name_node = vitte_c17_ast_child(emitter, path, 0u);
    token = vitte_c17_ast_token(emitter, name_node);
    if (token == NULL || token->lexeme == NULL ||
        token->length == 0u || token->length >= name_capacity) {
        return false;
    }
    (void)memcpy(name, token->lexeme, token->length);
    name[token->length] = '\0';
    return true;
}

static bool
vitte_c17_ast_generic_scalar_argument(const char *name)
{
    static const char *const supported[] = {
        "bool", "char", "f32", "f64", "i8", "i16", "i32", "i64",
        "int", "isize", "u8", "u16", "u32", "u64", "uintptr", "usize",
        "str", "string"
    };
    size_t index;

    if (name == NULL) {
        return false;
    }
    for (index = 0u; index < sizeof(supported) / sizeof(supported[0]); ++index) {
        if (strcmp(name, supported[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_type_expression_is_parameter(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const vitte_ast_node_t *parameter_name)
{
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *name;

    if (type == NULL || type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count != 1u) {
        return false;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count != 1u) {
        return false;
    }
    name = vitte_c17_ast_child(emitter, path, 0u);
    return name != NULL && parameter_name != NULL &&
           vitte_c17_ast_token_equal(emitter, name, parameter_name);
}

static bool
vitte_c17_ast_type_expression_is_any_generic_parameter(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type)
{
    size_t node_count;
    size_t index;

    node_count = vitte_parser_node_count(emitter->parser);
    for (index = 1u; index <= node_count; ++index) {
        const vitte_ast_node_t *parameter = vitte_parser_get_node(
            emitter->parser, (vitte_ast_node_id_t)index);
        if (parameter != NULL &&
            parameter->kind == VITTE_AST_NODE_GENERIC_PARAM &&
            parameter->child_count != 0u &&
            vitte_c17_ast_type_expression_is_parameter(
                emitter,
                type,
                vitte_c17_ast_child(emitter, parameter, 0u))) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_generic_type_contains_parameter(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    size_t depth)
{
    size_t index;

    if (type == NULL || depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        return false;
    }
    {
        size_t substitution_index =
            vitte_c17_ast_generic_substitution_index(emitter, type);
        if (substitution_index != SIZE_MAX) {
            return vitte_c17_ast_generic_type_contains_parameter(
                emitter,
                emitter->generic_arguments[substitution_index],
                depth + 1u);
        }
    }
    if (vitte_c17_ast_type_expression_is_any_generic_parameter(
            emitter, type)) {
        return true;
    }
    for (index = 0u; index < type->child_count; ++index) {
        if (vitte_c17_ast_generic_type_contains_parameter(
                emitter,
                vitte_c17_ast_child(emitter, type, index),
                depth + 1u)) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_generic_application_argument_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *application,
    char *name,
    size_t name_capacity,
    size_t depth)
{
    size_t used = 0u;
    size_t index;

    if (application == NULL ||
        application->kind != VITTE_AST_NODE_TYPE_EXPR ||
        application->child_count < 2u || name == NULL ||
        name_capacity == 0u ||
        depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        return false;
    }
    name[0] = '\0';
    for (index = 1u; index < application->child_count; ++index) {
        char argument_name[
            VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX];
        size_t argument_length;
        size_t separator_length = index == 1u ? 0u : 2u;

        if (!vitte_c17_ast_generic_argument_name(
                emitter,
                vitte_c17_ast_child(emitter, application, index),
                argument_name,
                sizeof(argument_name),
                depth + 1u)) {
            return false;
        }
        argument_length = strlen(argument_name);
        if (used > name_capacity - 1u ||
            separator_length > name_capacity - 1u - used ||
            argument_length > name_capacity - 1u - used -
                                  separator_length) {
            return false;
        }
        if (separator_length != 0u) {
            name[used++] = '_';
            name[used++] = '_';
        }
        (void)memcpy(name + used, argument_name, argument_length);
        used += argument_length;
        name[used] = '\0';
    }
    return used != 0u;
}

static bool
vitte_c17_ast_generic_argument_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    char *name,
    size_t name_capacity,
    size_t depth)
{
    char simple_name[VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX];

    if (type == NULL || name == NULL || name_capacity == 0u ||
        depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        return false;
    }
    {
        size_t substitution_index =
            vitte_c17_ast_generic_substitution_index(emitter, type);
        if (substitution_index != SIZE_MAX) {
            const vitte_ast_node_t *argument =
                emitter->generic_arguments[substitution_index];
            size_t saved_substitution_count =
                emitter->generic_substitution_count;
            bool resolved;
            if (argument == NULL || argument == type) {
                const char *source_name =
                    vitte_c17_ast_source_scalar_name_from_c_type(
                        emitter->generic_c_type_names[substitution_index]);
                if (source_name == NULL ||
                    strlen(source_name) >= name_capacity) {
                    return false;
                }
                memcpy(name, source_name, strlen(source_name) + 1u);
                return true;
            }
            emitter->generic_substitution_count = substitution_index;
            resolved = vitte_c17_ast_generic_argument_name(
                emitter,
                argument,
                name,
                name_capacity,
                depth + 1u);
            emitter->generic_substitution_count =
                saved_substitution_count;
            return resolved;
        }
    }
    if (vitte_c17_ast_simple_type_name(
            emitter, type, simple_name, sizeof(simple_name))) {
        if (vitte_c17_ast_generic_scalar_argument(simple_name)) {
            if (strlen(simple_name) >= name_capacity) {
                return false;
            }
            (void)memcpy(name, simple_name, strlen(simple_name) + 1u);
            return true;
        }
        {
            const vitte_ast_node_t *root = vitte_c17_ast_node(
                emitter, vitte_parser_root(emitter->parser));
            const vitte_ast_node_t *form =
                vitte_c17_ast_find_form(emitter, root,
                    vitte_c17_ast_child(
                        emitter,
                        vitte_c17_ast_child(emitter, type, 0u),
                        0u));
            if (form != NULL &&
                vitte_c17_ast_generic_form_parameter_count(
                    emitter, form, NULL) == 0u &&
                strlen(simple_name) < name_capacity) {
                (void)memcpy(name, simple_name, strlen(simple_name) + 1u);
                return true;
            }
        }
        return false;
    }
    if (type->kind == VITTE_AST_NODE_TYPE_EXPR &&
        type->child_count > 1u) {
        const vitte_ast_node_t *path =
            vitte_c17_ast_child(emitter, type, 0u);
        const vitte_ast_node_t *form_name = path != NULL &&
                path->kind == VITTE_AST_NODE_PATH &&
                path->child_count == 1u
            ? vitte_c17_ast_child(emitter, path, 0u)
            : NULL;
        const vitte_ast_node_t *root = vitte_c17_ast_node(
            emitter, vitte_parser_root(emitter->parser));
        const vitte_ast_node_t *form;
        const vitte_token_t *token;
        size_t parameter_count;
        char nested_name[VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX];
        size_t token_length;
        size_t nested_length;

        token = vitte_c17_ast_token(emitter, form_name);
        form = vitte_c17_ast_find_form(emitter, root, form_name);
        parameter_count = vitte_c17_ast_generic_form_parameter_count(
            emitter, form, NULL);
        if (token == NULL || token->lexeme == NULL ||
            token->length == 0u || form == NULL ||
            parameter_count == 0u ||
            type->child_count != parameter_count + 1u ||
            !vitte_c17_ast_generic_application_argument_name(
                emitter,
                type,
                nested_name,
                sizeof(nested_name),
                depth + 1u)) {
            return false;
        }
        token_length = token->length;
        nested_length = strlen(nested_name);
        if (token_length == 0u || name_capacity < 3u ||
            token_length > name_capacity - 3u ||
            nested_length > name_capacity - token_length - 3u) {
            return false;
        }
        (void)memcpy(name, token->lexeme, token_length);
        name[token_length] = '_';
        name[token_length + 1u] = '_';
        (void)memcpy(
            name + token_length + 2u,
            nested_name,
            nested_length + 1u);
        return true;
    }
    return false;
}

static const vitte_c17_ast_generic_form_instance_t *
vitte_c17_ast_generic_form_instance_for_type(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type)
{
    const vitte_ast_node_t *root;
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *form_name;
    const vitte_ast_node_t *form;
    char argument_name[VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX];
    size_t parameter_count;
    size_t index;

    if (type == NULL || type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count < 2u) {
        return NULL;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count == 0u ||
        !vitte_c17_ast_generic_application_argument_name(
            emitter,
            type,
            argument_name,
            sizeof(argument_name),
            0u)) {
        return NULL;
    }
    form_name = vitte_c17_ast_child(
        emitter, path, path->child_count - 1u);
    root = vitte_c17_ast_node(
        emitter,
        vitte_parser_root(emitter->parser));
    form = vitte_c17_ast_find_form(emitter, root, form_name);
    if (form == NULL) {
        form = vitte_c17_ast_find_path_declaration(
            emitter,
            path,
            VITTE_AST_NODE_PICK_DECL,
            VITTE_AST_NODE_PICK_DECL);
    }
    parameter_count = vitte_c17_ast_generic_form_parameter_count(
        emitter, form, NULL);
    if (form == NULL || parameter_count == 0u ||
        type->child_count != parameter_count + 1u) {
        return NULL;
    }
    for (index = 0u; index < emitter->generic_form_count; ++index) {
        const vitte_c17_ast_generic_form_instance_t *instance =
            &emitter->generic_forms[index];
        if (instance->declaration == form &&
            strcmp(instance->argument_name, argument_name) == 0) {
            return instance;
        }
    }
    return NULL;
}

static void
vitte_c17_ast_emit_generic_form_instance_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_c17_ast_generic_form_instance_t *instance)
{
    const vitte_ast_node_t *name;

    if (instance == NULL || instance->declaration == NULL) {
        emitter->failed = true;
        return;
    }
    name = vitte_c17_ast_child(emitter, instance->declaration, 0u);
    if (name == NULL) {
        emitter->failed = true;
        return;
    }
    vitte_c17_ast_emit_declaration_name(
        emitter, instance->declaration, name);
    vitte_c17_ast_write(emitter, "__");
    vitte_c17_ast_write(emitter, instance->argument_name);
}

static void
vitte_c17_ast_collect_generic_form_instances(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *root,
    const vitte_ast_node_t *node,
    size_t depth)
{
    size_t index;

    if (node == NULL || emitter->failed) {
        return;
    }
    if (depth > 512u) {
        emitter->failed = true;
        return;
    }
    for (index = 0u; index < node->child_count && !emitter->failed; ++index) {
        vitte_c17_ast_collect_generic_form_instances(
            emitter,
            root,
            vitte_c17_ast_child(emitter, node, index),
            depth + 1u);
    }
    if (emitter->failed || node->kind != VITTE_AST_NODE_TYPE_EXPR ||
        node->operator_kind == VITTE_TOKEN_KW_PROC ||
        node->child_count <= 1u) {
        return;
    }
    {
        const vitte_ast_node_t *path =
            vitte_c17_ast_child(emitter, node, 0u);
        const vitte_ast_node_t *form_name = path != NULL &&
                path->kind == VITTE_AST_NODE_PATH && path->child_count != 0u
            ? vitte_c17_ast_child(emitter, path, path->child_count - 1u)
            : NULL;
        const vitte_ast_node_t *form =
            vitte_c17_ast_find_form(emitter, root, form_name);
        if (form == NULL) {
            form = vitte_c17_ast_find_path_declaration(
                emitter,
                path,
                VITTE_AST_NODE_PICK_DECL,
                VITTE_AST_NODE_PICK_DECL);
        }
        if (form == NULL) {
            const vitte_ast_node_t *alias =
                vitte_c17_ast_find_path_declaration(
                    emitter,
                    path,
                    VITTE_AST_NODE_TYPE_DECL,
                    VITTE_AST_NODE_TYPE_DECL);
            if (alias != NULL &&
                (alias->flags & VITTE_AST_FLAG_TYPE_ALIAS) != 0u &&
                vitte_c17_ast_generic_form_parameter_count(
                    emitter, alias, NULL) != 0u) {
                size_t parameter_count =
                    vitte_c17_ast_generic_form_parameter_count(
                        emitter, alias, NULL);
                size_t saved_substitution_count =
                    emitter->generic_substitution_count;
                const vitte_ast_node_t *target =
                    vitte_c17_ast_child(
                        emitter, alias, alias->child_count - 1u);
                size_t parameter_index;

                if (parameter_count == SIZE_MAX ||
                    node->child_count != parameter_count + 1u ||
                    saved_substitution_count >
                        emitter->generic_substitution_capacity ||
                    parameter_count >
                        emitter->generic_substitution_capacity -
                            saved_substitution_count ||
                    target == NULL) {
                    emitter->failed = true;
                    return;
                }
                for (parameter_index = 0u;
                     parameter_index < parameter_count;
                     ++parameter_index) {
                    size_t substitution_index =
                        saved_substitution_count + parameter_index;
                    emitter->generic_parameter_names[substitution_index] =
                        vitte_c17_ast_generic_form_parameter_name(
                            emitter, alias, parameter_index);
                    emitter->generic_arguments[substitution_index] =
                        vitte_c17_ast_child(
                            emitter, node, parameter_index + 1u);
                    emitter->generic_c_type_names[substitution_index] = NULL;
                    if (emitter->generic_parameter_names[
                            substitution_index] == NULL ||
                        emitter->generic_arguments[substitution_index] == NULL) {
                        emitter->generic_substitution_count =
                            saved_substitution_count;
                        emitter->failed = true;
                        return;
                    }
                }
                emitter->generic_substitution_count =
                    saved_substitution_count + parameter_count;
                /* Discover the concrete form instances introduced by the
                 * alias target while its generic parameters are substituted
                 * by this use site's arguments. */
                vitte_c17_ast_collect_generic_form_instances(
                    emitter, root, target, depth + 1u);
                while (emitter->generic_substitution_count >
                       saved_substitution_count) {
                    --emitter->generic_substitution_count;
                    emitter->generic_parameter_names[
                        emitter->generic_substitution_count] = NULL;
                    emitter->generic_arguments[
                        emitter->generic_substitution_count] = NULL;
                    emitter->generic_c_type_names[
                        emitter->generic_substitution_count] = NULL;
                }
                return;
            }
            {
                const vitte_ast_node_t *opaque =
                    vitte_c17_ast_find_path_declaration(
                        emitter,
                        path,
                        VITTE_AST_NODE_OPAQUE_DECL,
                        VITTE_AST_NODE_OPAQUE_DECL);
                if (opaque != NULL) {
                    size_t expected =
                        vitte_c17_ast_generic_form_parameter_count(
                            emitter, opaque, NULL);
                    if (expected != node->child_count - 1u) {
                        emitter->failed = true;
                    }
                    return;
                }
            }
            emitter->failed = true;
            return;
        }
        {
            size_t parameter_count =
                vitte_c17_ast_generic_form_parameter_count(
                    emitter, form, NULL);
            char argument_name[
                VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX];
            size_t instance_index;
            size_t argument_index;
            const vitte_ast_node_t **resolved_arguments = NULL;

            if (parameter_count == 0u ||
                parameter_count == SIZE_MAX ||
                node->child_count != parameter_count + 1u) {
                emitter->failed = true;
                return;
            }
            for (argument_index = 1u;
                 argument_index < node->child_count;
                 ++argument_index) {
                if (vitte_c17_ast_generic_type_contains_parameter(
                        emitter,
                        vitte_c17_ast_child(emitter, node, argument_index),
                        0u)) {
                    return;
                }
            }
            if (!vitte_c17_ast_generic_application_argument_name(
                    emitter,
                    node,
                    argument_name,
                    sizeof(argument_name),
                    0u)) {
                emitter->failed = true;
                return;
            }
            for (instance_index = 0u;
                 instance_index < emitter->generic_form_count;
                 ++instance_index) {
                const vitte_c17_ast_generic_form_instance_t *existing =
                    &emitter->generic_forms[instance_index];
                const vitte_ast_node_t *existing_name =
                    vitte_c17_ast_child(emitter, existing->declaration, 0u);
                if (existing->declaration == form &&
                    strcmp(existing->argument_name, argument_name) == 0) {
                    break;
                }
                if (existing_name != NULL &&
                    vitte_c17_ast_token_equal(
                        emitter,
                        existing_name,
                        vitte_c17_ast_child(emitter, form, 0u)) &&
                    strcmp(existing->argument_name, argument_name) == 0) {
                    /* Same generated C identifier from two source forms. */
                    emitter->failed = true;
                    return;
                }
            }
            if (instance_index == emitter->generic_form_count) {
                if (parameter_count >
                    SIZE_MAX / sizeof(resolved_arguments[0])) {
                    emitter->failed = true;
                    return;
                }
                resolved_arguments = (const vitte_ast_node_t **)malloc(
                    parameter_count * sizeof(resolved_arguments[0]));
                if (resolved_arguments == NULL) {
                    emitter->failed = true;
                    return;
                }
                for (argument_index = 0u;
                     argument_index < parameter_count;
                     ++argument_index) {
                    resolved_arguments[argument_index] =
                        vitte_c17_ast_resolve_generic_argument(
                            emitter,
                            vitte_c17_ast_child(
                                emitter,
                                node,
                                argument_index + 1u),
                            0u);
                    if (resolved_arguments[argument_index] == NULL) {
                        free((void *)resolved_arguments);
                        emitter->failed = true;
                        return;
                    }
                }
                if (emitter->generic_form_count == SIZE_MAX ||
                    !vitte_c17_ast_emitter_reserve_generic_forms(
                        emitter,
                        emitter->generic_form_count + 1u)) {
                    free((void *)resolved_arguments);
                    emitter->failed = true;
                    return;
                }
                emitter->generic_forms[instance_index].declaration = form;
                emitter->generic_forms[instance_index].type_expression = node;
                emitter->generic_forms[instance_index].arguments =
                    resolved_arguments;
                emitter->generic_forms[instance_index].argument_count =
                    parameter_count;
                (void)memcpy(
                    emitter->generic_forms[instance_index].argument_name,
                    argument_name,
                    strlen(argument_name) + 1u);
                emitter->generic_forms[instance_index].layout_state = 0u;
                emitter->generic_forms[instance_index].forward_declared = false;
                ++emitter->generic_form_count;
            }
        }
    }
}

static bool
vitte_c17_ast_space_declares_function(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *space,
    const vitte_ast_node_t *name)
{
    const vitte_ast_node_t *body;
    size_t index;

    body = vitte_c17_ast_child(emitter, space, 1u);
    if (body == NULL ||
        body->kind != VITTE_AST_NODE_BLOCK) {
        return false;
    }
    for (index = 0u; index < body->child_count; ++index) {
        const vitte_ast_node_t *declaration;

        declaration = vitte_c17_ast_child(emitter, body, index);
        if (declaration == NULL ||
            (declaration->kind != VITTE_AST_NODE_PROC_DECL &&
             declaration->kind != VITTE_AST_NODE_EXTERN_PROC_DECL)) {
            continue;
        }
        if (vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, declaration, 0u),
                name)) {
            return !vitte_c17_ast_is_intrinsic(emitter, declaration);
        }
    }
    return false;
}

static bool
vitte_c17_ast_is_local_space_function(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name)
{
    return emitter->space_depth != 0u &&
           vitte_c17_ast_space_declares_function(
               emitter,
               emitter->space_stack[emitter->space_depth - 1u],
               name);
}

static void
vitte_c17_ast_emit_type(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *name;

    if (node == NULL) {
        emitter->failed = true;
        return;
    }

    switch (node->kind) {
        case VITTE_AST_NODE_REFERENCE_TYPE:
        case VITTE_AST_NODE_POINTER_TYPE:
            vitte_c17_ast_emit_type(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            vitte_c17_ast_write(emitter, " *");
            return;

        case VITTE_AST_NODE_OPTIONAL_TYPE:
            name = vitte_c17_ast_child(emitter, node, 0u);
            if (vitte_c17_ast_is_function_type_alias(emitter, name)) {
                /* Function aliases already denote nullable C function
                 * pointers, so the optional wrapper needs no extra level. */
                vitte_c17_ast_emit_type(emitter, name);
                return;
            }
            if (name != NULL &&
                (name->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                 name->kind == VITTE_AST_NODE_POINTER_TYPE)) {
                /* Pointers already represent an empty optional as NULL. */
                vitte_c17_ast_emit_type(emitter, name);
                return;
            }
            if (name != NULL && name->kind == VITTE_AST_NODE_TYPE_EXPR) {
                const vitte_ast_node_t *path =
                    vitte_c17_ast_child(emitter, name, 0u);
                const vitte_ast_node_t *last = path != NULL &&
                        path->kind == VITTE_AST_NODE_PATH &&
                        path->child_count != 0u
                    ? vitte_c17_ast_child(
                          emitter, path, path->child_count - 1u)
                    : NULL;
                if (path != NULL && path->kind == VITTE_AST_NODE_PATH &&
                    path->child_count == 1u &&
                    (vitte_c17_ast_token_is(emitter, last, "str") ||
                     vitte_c17_ast_token_is(emitter, last, "string"))) {
                    /* C strings already carry their nullable pointer
                     * representation; an extra star would yield char **. */
                    vitte_c17_ast_emit_type(emitter, name);
                    return;
                }
            }
            vitte_c17_ast_emit_type(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            vitte_c17_ast_write(emitter, " *");
            return;

        case VITTE_AST_NODE_TYPE_EXPR:
            name = vitte_c17_ast_child(emitter, node, 0u);
            if (name == NULL ||
                name->kind != VITTE_AST_NODE_PATH ||
                name->child_count == 0u) {
                emitter->failed = true;
                return;
            }
            if (node->operator_kind != VITTE_TOKEN_KW_PROC &&
                node->child_count > 1u) {
                const vitte_ast_node_t *opaque =
                    vitte_c17_ast_find_path_declaration(
                        emitter,
                        name,
                        VITTE_AST_NODE_OPAQUE_DECL,
                        VITTE_AST_NODE_OPAQUE_DECL);
                if (opaque != NULL) {
                    size_t expected =
                        vitte_c17_ast_generic_form_parameter_count(
                            emitter, opaque, NULL);
                    if (expected != node->child_count - 1u) {
                        emitter->failed = true;
                        return;
                    }
                    vitte_c17_ast_emit_declaration_name(
                        emitter,
                        opaque,
                        vitte_c17_ast_child(emitter, opaque, 0u));
                    return;
                }
            }
            if (node->operator_kind != VITTE_TOKEN_KW_PROC &&
                node->child_count > 1u) {
                const vitte_ast_node_t *alias =
                    vitte_c17_ast_find_path_declaration(
                        emitter,
                        name,
                        VITTE_AST_NODE_TYPE_DECL,
                        VITTE_AST_NODE_TYPE_DECL);
                size_t parameter_count =
                    vitte_c17_ast_generic_form_parameter_count(
                        emitter, alias, NULL);
                if (alias != NULL && parameter_count != 0u &&
                    (alias->flags & VITTE_AST_FLAG_TYPE_ALIAS) != 0u) {
                    const vitte_ast_node_t *target;
                    size_t saved_count =
                        emitter->generic_substitution_count;
                    size_t argument_count = node->child_count - 1u;
                    size_t index;
                    if (parameter_count != argument_count ||
                        saved_count > emitter->generic_substitution_capacity ||
                        parameter_count >
                            emitter->generic_substitution_capacity - saved_count) {
                        emitter->failed = true;
                        return;
                    }
                    for (index = 0u; index < parameter_count; ++index) {
                        size_t substitution_index = saved_count + index;
                        emitter->generic_parameter_names[substitution_index] =
                            vitte_c17_ast_generic_form_parameter_name(
                                emitter, alias, index);
                        emitter->generic_arguments[substitution_index] =
                            vitte_c17_ast_child(emitter, node, index + 1u);
                        emitter->generic_c_type_names[substitution_index] = NULL;
                        if (emitter->generic_parameter_names[substitution_index] == NULL ||
                            emitter->generic_arguments[substitution_index] == NULL) {
                            emitter->failed = true;
                            emitter->generic_substitution_count = saved_count;
                            return;
                        }
                    }
                    emitter->generic_substitution_count =
                        saved_count + parameter_count;
                    target = vitte_c17_ast_child(
                        emitter, alias, alias->child_count - 1u);
                    vitte_c17_ast_emit_type(emitter, target);
                    while (emitter->generic_substitution_count > saved_count) {
                        --emitter->generic_substitution_count;
                        emitter->generic_parameter_names[
                            emitter->generic_substitution_count] = NULL;
                        emitter->generic_arguments[
                            emitter->generic_substitution_count] = NULL;
                        emitter->generic_c_type_names[
                            emitter->generic_substitution_count] = NULL;
                    }
                    return;
                }
            }
            if (node->operator_kind != VITTE_TOKEN_KW_PROC &&
                node->child_count > 1u) {
                const vitte_c17_ast_generic_form_instance_t *instance =
                    vitte_c17_ast_generic_form_instance_for_type(
                        emitter,
                        node);
                if (instance == NULL) {
                    emitter->failed = true;
                    return;
                }
                vitte_c17_ast_emit_generic_form_instance_name(
                    emitter,
                    instance);
                return;
            }
            {
                size_t substitution_index =
                    vitte_c17_ast_generic_substitution_index(
                        emitter, node);
                if (substitution_index != SIZE_MAX) {
                    const char *concrete_c_type =
                        emitter->generic_c_type_names[
                            substitution_index];
                    const vitte_ast_node_t *argument =
                        emitter->generic_arguments[substitution_index];
                    size_t saved_substitution_count =
                        emitter->generic_substitution_count;
                    if (concrete_c_type != NULL) {
                        vitte_c17_ast_write(emitter, concrete_c_type);
                        return;
                    }
                    if (argument == NULL) {
                        emitter->failed = true;
                        return;
                    }
                    emitter->generic_substitution_count =
                        substitution_index;
                    vitte_c17_ast_emit_type(emitter, argument);
                    emitter->generic_substitution_count =
                        saved_substitution_count;
                    return;
                }
            }
            {
                const vitte_ast_node_t *path;

                path = name;
                name = vitte_c17_ast_child(
                    emitter,
                    path,
                    path->child_count - 1u);
                if (path->child_count == 1u &&
                    vitte_c17_ast_token_is(emitter, name, "void")) {
                    vitte_c17_ast_write(emitter, "void");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "bool")) {
                    vitte_c17_ast_write(emitter, "bool");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "i8")) {
                    vitte_c17_ast_write(emitter, "int8_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "i16")) {
                    vitte_c17_ast_write(emitter, "int16_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "i32")) {
                    vitte_c17_ast_write(emitter, "int32_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "i64")) {
                    vitte_c17_ast_write(emitter, "int64_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "u64")) {
                    vitte_c17_ast_write(emitter, "uint64_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "u8")) {
                    vitte_c17_ast_write(emitter, "uint8_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "u16")) {
                    vitte_c17_ast_write(emitter, "uint16_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "u32")) {
                    vitte_c17_ast_write(emitter, "uint32_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "usize")) {
                    vitte_c17_ast_write(emitter, "size_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "isize")) {
                    vitte_c17_ast_write(emitter, "ptrdiff_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "uintptr")) {
                    vitte_c17_ast_write(emitter, "uintptr_t");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "f64")) {
                    vitte_c17_ast_write(emitter, "double");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "f32")) {
                    vitte_c17_ast_write(emitter, "float");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "char")) {
                    vitte_c17_ast_write(emitter, "char");
                } else if (path->child_count == 1u &&
                           (vitte_c17_ast_token_is(emitter, name, "string") ||
                            vitte_c17_ast_token_is(emitter, name, "str"))) {
                    vitte_c17_ast_write(emitter, "const char *");
                } else if (path->child_count == 1u &&
                           vitte_c17_ast_token_is(emitter, name, "int")) {
                    vitte_c17_ast_write(emitter, "int32_t");
                } else {
                    const vitte_ast_node_t *declaration =
                        vitte_c17_ast_find_path_declaration(
                            emitter,
                            path,
                            VITTE_AST_NODE_FORM_DECL,
                            VITTE_AST_NODE_PICK_DECL);
                    if (declaration == NULL) {
                        declaration = vitte_c17_ast_find_path_declaration(
                            emitter,
                            path,
                            VITTE_AST_NODE_TYPE_DECL,
                            VITTE_AST_NODE_OPAQUE_DECL);
                    }
                    if (declaration != NULL) {
                        vitte_c17_ast_emit_declaration_name(
                            emitter,
                            declaration,
                            vitte_c17_ast_child(
                                emitter, declaration, 0u));
                    } else {
                        vitte_c17_ast_write_token(emitter, name);
                    }
                }
            }
            return;

        case VITTE_AST_NODE_PATH:
            if (node->child_count == 0u) {
                emitter->failed = true;
                return;
            }
            {
                const vitte_ast_node_t *declaration =
                    vitte_c17_ast_find_path_declaration(
                        emitter,
                        node,
                        VITTE_AST_NODE_FORM_DECL,
                        VITTE_AST_NODE_PICK_DECL);
                if (declaration == NULL) {
                    declaration = vitte_c17_ast_find_path_declaration(
                        emitter,
                        node,
                        VITTE_AST_NODE_TYPE_DECL,
                        VITTE_AST_NODE_OPAQUE_DECL);
                }
                if (declaration != NULL) {
                    vitte_c17_ast_emit_declaration_name(
                        emitter,
                        declaration,
                        vitte_c17_ast_child(emitter, declaration, 0u));
                } else {
                    vitte_c17_ast_write_token(
                        emitter,
                        vitte_c17_ast_child(
                            emitter,
                            node,
                            node->child_count - 1u));
                }
            }
            return;

        case VITTE_AST_NODE_IDENTIFIER:
            vitte_c17_ast_write_token(emitter, node);
            return;

        case VITTE_AST_NODE_ARRAY_TYPE:
            if (node->child_count == 1u) {
                vitte_c17_ast_write(emitter, "VitteArray");
                return;
            }
            emitter->failed = true;
            return;

        default:
            emitter->failed = true;
            return;
    }
}

static void
vitte_c17_ast_emit_declarator(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const vitte_ast_node_t *name)
{
    if (type == NULL || name == NULL) {
        emitter->failed = true;
        return;
    }

    if (type->kind == VITTE_AST_NODE_TYPE_EXPR &&
        type->operator_kind != VITTE_TOKEN_KW_PROC &&
        type->child_count > 1u) {
        const vitte_ast_node_t *path =
            vitte_c17_ast_child(emitter, type, 0u);
        const vitte_ast_node_t *alias =
            vitte_c17_ast_find_path_declaration(
                emitter,
                path,
                VITTE_AST_NODE_TYPE_DECL,
                VITTE_AST_NODE_TYPE_DECL);
        size_t parameter_count =
            vitte_c17_ast_generic_form_parameter_count(
                emitter, alias, NULL);

        if (alias != NULL && parameter_count != 0u &&
            (alias->flags & VITTE_AST_FLAG_TYPE_ALIAS) != 0u) {
            const vitte_ast_node_t *target = vitte_c17_ast_child(
                emitter, alias, alias->child_count - 1u);

            /* C array types need the identifier inside the declarator. Walk
             * generic alias chains until the array target is reached, keeping
             * each alias substitution active while the nested declarator is
             * emitted. */
            bool target_is_fixed_array = target != NULL &&
                target->kind == VITTE_AST_NODE_ARRAY_TYPE &&
                target->child_count == 2u;
            bool target_is_generic_alias = false;
            if (target != NULL &&
                target->kind == VITTE_AST_NODE_TYPE_EXPR &&
                target->operator_kind != VITTE_TOKEN_KW_PROC &&
                target->child_count > 1u) {
                const vitte_ast_node_t *target_path =
                    vitte_c17_ast_child(emitter, target, 0u);
                const vitte_ast_node_t *nested_alias =
                    vitte_c17_ast_find_path_declaration(
                        emitter,
                        target_path,
                        VITTE_AST_NODE_TYPE_DECL,
                        VITTE_AST_NODE_TYPE_DECL);
                target_is_generic_alias = nested_alias != NULL &&
                    (nested_alias->flags & VITTE_AST_FLAG_TYPE_ALIAS) != 0u &&
                    vitte_c17_ast_generic_form_parameter_count(
                        emitter, nested_alias, NULL) != 0u;
            }
            if (target_is_fixed_array || target_is_generic_alias) {
                size_t saved_count =
                    emitter->generic_substitution_count;
                size_t argument_count = type->child_count - 1u;
                size_t index;

                if (parameter_count != argument_count ||
                    saved_count > emitter->generic_substitution_capacity ||
                    parameter_count >
                        emitter->generic_substitution_capacity - saved_count) {
                    emitter->failed = true;
                    return;
                }
                for (index = 0u; index < parameter_count; ++index) {
                    size_t substitution_index = saved_count + index;

                    emitter->generic_parameter_names[substitution_index] =
                        vitte_c17_ast_generic_form_parameter_name(
                            emitter, alias, index);
                    emitter->generic_arguments[substitution_index] =
                        vitte_c17_ast_child(emitter, type, index + 1u);
                    emitter->generic_c_type_names[substitution_index] = NULL;
                    if (emitter->generic_parameter_names[
                            substitution_index] == NULL ||
                        emitter->generic_arguments[substitution_index] == NULL) {
                        emitter->failed = true;
                        while (index + 1u != 0u) {
                            size_t cleanup_index = saved_count + index;
                            emitter->generic_parameter_names[cleanup_index] = NULL;
                            emitter->generic_arguments[cleanup_index] = NULL;
                            emitter->generic_c_type_names[cleanup_index] = NULL;
                            if (index == 0u) {
                                break;
                            }
                            --index;
                        }
                        return;
                    }
                }
                emitter->generic_substitution_count =
                    saved_count + parameter_count;
                vitte_c17_ast_emit_declarator(emitter, target, name);
                emitter->generic_substitution_count = saved_count;
                for (index = 0u; index < parameter_count; ++index) {
                    size_t substitution_index = saved_count + index;
                    emitter->generic_parameter_names[substitution_index] = NULL;
                    emitter->generic_arguments[substitution_index] = NULL;
                    emitter->generic_c_type_names[substitution_index] = NULL;
                }
                return;
            }
        }
    }

    if (type->kind == VITTE_AST_NODE_ARRAY_TYPE) {
        if (type->child_count == 1u) {
            vitte_c17_ast_write(emitter, "VitteArray ");
            vitte_c17_ast_write_token(emitter, name);
            return;
        }
        if (type->child_count != 2u) {
            emitter->failed = true;
            return;
        }
        vitte_c17_ast_emit_type(
            emitter,
            vitte_c17_ast_child(emitter, type, 1u));
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_write_token(emitter, name);
        vitte_c17_ast_write(emitter, "[");
        vitte_c17_ast_emit_expression(
            emitter,
            vitte_c17_ast_child(emitter, type, 0u));
        vitte_c17_ast_write(emitter, "]");
        return;
    }

    vitte_c17_ast_emit_type(emitter, type);
    vitte_c17_ast_write(emitter, " ");
    vitte_c17_ast_write_token(emitter, name);
}

static const char *
vitte_c17_ast_inferred_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    const vitte_ast_node_t *left;

    if (expression == NULL) {
        return NULL;
    }

    switch (expression->kind) {
        case VITTE_AST_NODE_INTEGER_LITERAL:
            return "int64_t";
        case VITTE_AST_NODE_FLOAT_LITERAL:
            return "double";
        case VITTE_AST_NODE_BOOL_LITERAL:
            return "bool";
        case VITTE_AST_NODE_STRING_LITERAL:
            return "const char *";
        case VITTE_AST_NODE_CHARACTER_LITERAL:
            return "char";
        case VITTE_AST_NODE_GROUP_EXPR:
            return vitte_c17_ast_inferred_type(
                emitter,
                vitte_c17_ast_child(
                    emitter,
                    expression,
                    0u));
        case VITTE_AST_NODE_UNARY_EXPR:
            if (expression->operator_kind == VITTE_TOKEN_KW_NOT) {
                return "bool";
            }
            return vitte_c17_ast_inferred_type(
                emitter,
                vitte_c17_ast_child(
                    emitter,
                    expression,
                    0u));
        case VITTE_AST_NODE_BINARY_EXPR:
            switch (expression->operator_kind) {
                case VITTE_TOKEN_EQUAL_EQUAL:
                case VITTE_TOKEN_BANG_EQUAL:
                case VITTE_TOKEN_LESS:
                case VITTE_TOKEN_LESS_EQUAL:
                case VITTE_TOKEN_GREATER:
                case VITTE_TOKEN_GREATER_EQUAL:
                case VITTE_TOKEN_KW_AND:
                case VITTE_TOKEN_AMP_AMP:
                case VITTE_TOKEN_KW_OR:
                case VITTE_TOKEN_PIPE_PIPE:
                    return "bool";
                default:
                    break;
            }
            left =
                vitte_c17_ast_child(
                    emitter,
                    expression,
                    0u);
            return vitte_c17_ast_inferred_type(
                emitter,
                left);
        default:
            return NULL;
    }
}

static bool
vitte_c17_ast_is_reference_parameter(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (emitter->function == NULL || name == NULL) {
        return false;
    }

    for (index = 1u;
         index < emitter->function->child_count;
         ++index) {
        const vitte_ast_node_t *parameter;
        const vitte_ast_node_t *parameter_name;
        const vitte_ast_node_t *parameter_type;
        const vitte_token_t *left;
        const vitte_token_t *right;

        parameter =
            vitte_c17_ast_child(
                emitter,
                emitter->function,
                index);
        if (parameter == NULL ||
            parameter->kind != VITTE_AST_NODE_PARAMETER) {
            continue;
        }

        parameter_name =
            vitte_c17_ast_child(emitter, parameter, 0u);
        parameter_type =
            vitte_c17_ast_child(emitter, parameter, 1u);
        if (parameter_name == NULL ||
            parameter_type == NULL ||
            parameter_type->kind != VITTE_AST_NODE_REFERENCE_TYPE) {
            continue;
        }

        left = vitte_c17_ast_token(emitter, parameter_name);
        right = vitte_c17_ast_token(emitter, name);
        if (left != NULL && right != NULL &&
            left->length == right->length &&
            memcmp(left->lexeme, right->lexeme, left->length) == 0) {
            return true;
        }
    }

    return false;
}

static bool
vitte_c17_ast_is_pointer_type_node(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type)
{
    while (type != NULL && type->kind == VITTE_AST_NODE_OPTIONAL_TYPE) {
        type = type->child_count != 0u
            ? vitte_c17_ast_child(emitter, type, 0u)
            : NULL;
    }
    return type != NULL &&
           (type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
            type->kind == VITTE_AST_NODE_POINTER_TYPE);
}

static const vitte_ast_node_t *
vitte_c17_ast_find_procedure_named(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name);

static bool
vitte_c17_ast_is_address_expression(
    const vitte_ast_node_t *expression)
{
    return expression != NULL &&
           expression->kind == VITTE_AST_NODE_UNARY_EXPR &&
           expression->operator_kind == VITTE_TOKEN_AMP;
}

static bool
vitte_c17_ast_is_string_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type)
{
    const vitte_ast_node_t *name;
    const vitte_token_t *token;

    while (type != NULL &&
           (type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
            type->kind == VITTE_AST_NODE_POINTER_TYPE ||
            type->kind == VITTE_AST_NODE_OPTIONAL_TYPE)) {
        type = vitte_c17_ast_child(emitter, type, 0u);
    }
    if (type == NULL || type->kind != VITTE_AST_NODE_TYPE_EXPR) {
        return false;
    }

    name = type;
    if (type->child_count != 0u) {
        const vitte_ast_node_t *path;

        path = vitte_c17_ast_child(emitter, type, 0u);
        if (path != NULL &&
            path->kind == VITTE_AST_NODE_PATH &&
            path->child_count != 0u) {
            name = vitte_c17_ast_child(
                emitter,
                path,
                path->child_count - 1u);
        }
    }
    token = vitte_c17_ast_token(emitter, name);
    return token != NULL &&
           ((token->length == 6u &&
             memcmp(token->lexeme, "string", 6u) == 0) ||
            (token->length == 3u &&
             memcmp(token->lexeme, "str", 3u) == 0));
}

static bool
vitte_c17_ast_is_string_parameter(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (emitter->function == NULL || name == NULL) {
        return false;
    }
    for (index = 1u; index < emitter->function->child_count; ++index) {
        const vitte_ast_node_t *parameter;

        parameter = vitte_c17_ast_child(emitter, emitter->function, index);
        if (parameter == NULL ||
            parameter->kind != VITTE_AST_NODE_PARAMETER ||
            !vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, parameter, 0u),
                name)) {
            continue;
        }
        return vitte_c17_ast_is_string_type(
            emitter,
            vitte_c17_ast_child(emitter, parameter, 1u));
    }
    return false;
}

static const vitte_ast_node_t *
vitte_c17_ast_call_return_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *call);

static bool
vitte_c17_ast_type_paths_equal(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *left,
    const vitte_ast_node_t *right);

static bool
vitte_c17_ast_type_syntax_equal(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *left,
    const vitte_ast_node_t *right,
    size_t depth)
{
    size_t index;

    if (left == NULL || right == NULL || depth > 512u ||
        left->kind != right->kind ||
        left->child_count != right->child_count ||
        left->operator_kind != right->operator_kind ||
        left->flags != right->flags) {
        return false;
    }
    if (left->kind == VITTE_AST_NODE_PATH) {
        return vitte_c17_ast_type_paths_equal(emitter, left, right);
    }
    if (left->kind == VITTE_AST_NODE_TYPE_EXPR) {
        if (left->child_count == 0u ||
            !vitte_c17_ast_type_paths_equal(
                emitter,
                vitte_c17_ast_child(emitter, left, 0u),
                vitte_c17_ast_child(emitter, right, 0u))) {
            return false;
        }
        for (index = 1u; index < left->child_count; ++index) {
            if (!vitte_c17_ast_type_syntax_equal(
                    emitter,
                    vitte_c17_ast_child(emitter, left, index),
                    vitte_c17_ast_child(emitter, right, index),
                    depth + 1u)) {
                return false;
            }
        }
        return true;
    }
    if (left->child_count == 0u) {
        return vitte_c17_ast_token_equal(emitter, left, right);
    }
    for (index = 0u; index < left->child_count; ++index) {
        if (!vitte_c17_ast_type_syntax_equal(
                emitter,
                vitte_c17_ast_child(emitter, left, index),
                vitte_c17_ast_child(emitter, right, index),
                depth + 1u)) {
            return false;
        }
    }
    return true;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_form(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (node == NULL || name == NULL) {
        return NULL;
    }
    if (node->kind == VITTE_AST_NODE_FORM_DECL &&
        node->child_count != 0u &&
        vitte_c17_ast_token_equal(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u),
            name)) {
        return node;
    }
    for (index = 0u; index < node->child_count; ++index) {
        const vitte_ast_node_t *form;

        form = vitte_c17_ast_find_form(
            emitter,
            vitte_c17_ast_child(emitter, node, index),
            name);
        if (form != NULL) {
            return form;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_async_task_origin(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression,
    size_t depth)
{
    if (emitter == NULL || expression == NULL || depth > 256u) {
        return NULL;
    }
    expression = vitte_c17_ast_unwrap_groups(emitter, expression);
    if (expression == NULL) { return NULL; }
    if (expression->kind == VITTE_AST_NODE_CALL_EXPR &&
        expression->child_count != 0u) {
        const vitte_ast_node_t *callee =
            vitte_c17_ast_child(emitter, expression, 0u);
        const vitte_ast_node_t *procedure =
            vitte_c17_ast_find_path_declaration(
                emitter,
                callee,
                VITTE_AST_NODE_PROC_DECL,
                VITTE_AST_NODE_EXTERN_PROC_DECL);
        return procedure != NULL &&
               (procedure->flags & VITTE_AST_FLAG_ASYNC) != 0u
            ? procedure
            : NULL;
    }
    if (expression->kind == VITTE_AST_NODE_IDENTIFIER ||
        expression->kind == VITTE_AST_NODE_PATH) {
        const vitte_ast_node_t *name = expression;
        const vitte_ast_node_t *initializer;
        if (expression->kind == VITTE_AST_NODE_PATH &&
            expression->child_count == 1u) {
            name = vitte_c17_ast_child(emitter, expression, 0u);
        }
        initializer = vitte_c17_ast_find_local_initializer(
            emitter,
            emitter->function,
            name);
        return vitte_c17_ast_async_task_origin(
            emitter,
            initializer,
            depth + 1u);
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_type_alias_target(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (node == NULL || name == NULL) {
        return NULL;
    }
    if (node->kind == VITTE_AST_NODE_TYPE_DECL &&
        (node->flags & VITTE_AST_FLAG_TYPE_ALIAS) != 0u &&
        node->child_count >= 2u &&
        vitte_c17_ast_token_equal(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u),
            name)) {
        size_t child_index;
        for (child_index = 1u;
             child_index < node->child_count;
             ++child_index) {
            const vitte_ast_node_t *child =
                vitte_c17_ast_child(emitter, node, child_index);
            if (child != NULL &&
                child->kind == VITTE_AST_NODE_GENERIC_PARAM) {
                return NULL;
            }
        }
        return vitte_c17_ast_child(
            emitter,
            node,
            node->child_count - 1u);
    }
    for (index = 0u; index < node->child_count; ++index) {
        const vitte_ast_node_t *target =
            vitte_c17_ast_find_type_alias_target(
                emitter,
                vitte_c17_ast_child(emitter, node, index),
                name);
        if (target != NULL) {
            return target;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_field_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const vitte_ast_node_t *field_name)
{
    const vitte_ast_node_t *root;
    size_t index;
    size_t alias_depth;

    root = vitte_c17_ast_node(
        emitter,
        vitte_parser_root(emitter->parser));
    for (alias_depth = 0u; alias_depth < 32u; ++alias_depth) {
        const vitte_ast_node_t *name;
        const vitte_ast_node_t *path;
        const vitte_ast_node_t *form;

        while (type != NULL &&
               (type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                type->kind == VITTE_AST_NODE_POINTER_TYPE ||
                type->kind == VITTE_AST_NODE_OPTIONAL_TYPE)) {
            type = vitte_c17_ast_child(emitter, type, 0u);
        }
        if (type == NULL) {
            return NULL;
        }
        path = type->kind == VITTE_AST_NODE_TYPE_EXPR
            ? vitte_c17_ast_child(emitter, type, 0u)
            : type;
        if (path == NULL ||
            (path->kind != VITTE_AST_NODE_PATH &&
             path->kind != VITTE_AST_NODE_IDENTIFIER) ||
            (path->kind == VITTE_AST_NODE_PATH &&
             path->child_count == 0u)) {
            return NULL;
        }
        name = path->kind == VITTE_AST_NODE_PATH
            ? vitte_c17_ast_child(emitter, path, path->child_count - 1u)
            : path;
        form = vitte_c17_ast_find_form(emitter, root, name);
        if (form != NULL) {
            for (index = 1u; index < form->child_count; ++index) {
                const vitte_ast_node_t *field =
                    vitte_c17_ast_child(emitter, form, index);
                if (field != NULL &&
                    field->kind == VITTE_AST_NODE_FIELD_DECL &&
                    field->child_count >= 2u &&
                    vitte_c17_ast_token_equal(
                        emitter,
                        vitte_c17_ast_child(emitter, field, 0u),
                        field_name)) {
                    return vitte_c17_ast_child(emitter, field, 1u);
                }
            }
            return NULL;
        }
        type = vitte_c17_ast_find_type_alias_target(
            emitter, root, name);
        if (type == NULL) {
            return NULL;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_expression_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression);

static bool
vitte_c17_ast_expression_is_string(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression);

static const vitte_ast_node_t *
vitte_c17_ast_function_return_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function)
{
    size_t index;

    if (function == NULL) {
        return NULL;
    }
    for (index = 1u; index < function->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, function, index);

        if (child != NULL &&
            (child->kind == VITTE_AST_NODE_TYPE_EXPR ||
             child->kind == VITTE_AST_NODE_ARRAY_TYPE ||
             child->kind == VITTE_AST_NODE_POINTER_TYPE ||
             child->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
             child->kind == VITTE_AST_NODE_OPTIONAL_TYPE)) {
            return child;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_local_initializer(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name);

static const vitte_ast_node_t *
vitte_c17_ast_find_local_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (node == NULL || name == NULL) {
        return NULL;
    }
    if (node->kind == VITTE_AST_NODE_LET_STMT &&
        node->child_count >= 2u &&
        vitte_c17_ast_token_equal(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u),
            name)) {
        const vitte_ast_node_t *declared_type;

        declared_type = vitte_c17_ast_child(emitter, node, 1u);
        if (declared_type != NULL &&
            (declared_type->kind == VITTE_AST_NODE_TYPE_EXPR ||
             declared_type->kind == VITTE_AST_NODE_ARRAY_TYPE ||
             declared_type->kind == VITTE_AST_NODE_POINTER_TYPE ||
             declared_type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
             declared_type->kind == VITTE_AST_NODE_OPTIONAL_TYPE)) {
            return declared_type;
        }
        if (declared_type != NULL &&
            declared_type->kind == VITTE_AST_NODE_CALL_EXPR) {
            return vitte_c17_ast_expression_type(
                emitter,
                declared_type);
        }
        if (declared_type != NULL) {
            return vitte_c17_ast_expression_type(
                emitter,
                declared_type);
        }
    }
    if ((node->kind == VITTE_AST_NODE_CONST_DECL ||
         node->kind == VITTE_AST_NODE_STATIC_DECL) &&
        node->child_count >= 2u &&
        vitte_c17_ast_token_equal(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u),
            name)) {
        const vitte_ast_node_t *declared_type;

        declared_type = vitte_c17_ast_child(emitter, node, 1u);
        if (declared_type != NULL &&
            (declared_type->kind == VITTE_AST_NODE_TYPE_EXPR ||
             declared_type->kind == VITTE_AST_NODE_ARRAY_TYPE ||
             declared_type->kind == VITTE_AST_NODE_POINTER_TYPE ||
             declared_type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
             declared_type->kind == VITTE_AST_NODE_OPTIONAL_TYPE)) {
            return declared_type;
        }
    }
    for (index = 0u; index < node->child_count; ++index) {
        const vitte_ast_node_t *type;

        type = vitte_c17_ast_find_local_type(
            emitter,
            vitte_c17_ast_child(emitter, node, index),
            name);
        if (type != NULL) {
            return type;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_expression_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    const vitte_ast_node_t *name;
    size_t index;

    if (expression == NULL) {
        return NULL;
    }
    if (expression->kind == VITTE_AST_NODE_GROUP_EXPR) {
        return vitte_c17_ast_expression_type(
            emitter,
            vitte_c17_ast_child(emitter, expression, 0u));
    }
    if (expression->kind == VITTE_AST_NODE_AWAIT_EXPR &&
        expression->child_count == 1u) {
        const vitte_ast_node_t *origin =
            vitte_c17_ast_async_task_origin(
                emitter,
                vitte_c17_ast_child(emitter, expression, 0u),
                0u);
        return origin != NULL
            ? vitte_c17_ast_function_return_type(emitter, origin)
            : NULL;
    }
    if (expression->kind == VITTE_AST_NODE_UNARY_EXPR &&
        expression->operator_kind == VITTE_TOKEN_STAR) {
        const vitte_ast_node_t *operand =
            vitte_c17_ast_child(emitter, expression, 0u);
        const vitte_ast_node_t *operand_type =
            vitte_c17_ast_expression_type(
                emitter,
                operand);

        if (operand_type != NULL &&
            (operand_type->kind == VITTE_AST_NODE_POINTER_TYPE ||
             operand_type->kind == VITTE_AST_NODE_REFERENCE_TYPE)) {
            return vitte_c17_ast_child(emitter, operand_type, 0u);
        }
        if (operand_type == NULL && emitter->function != NULL &&
            (operand != NULL &&
             (operand->kind == VITTE_AST_NODE_PATH ||
              operand->kind == VITTE_AST_NODE_IDENTIFIER))) {
            const vitte_ast_node_t *local_name = operand;
            const vitte_ast_node_t *initializer;

            if (operand->kind == VITTE_AST_NODE_PATH &&
                operand->child_count == 1u) {
                local_name = vitte_c17_ast_child(emitter, operand, 0u);
            }
            initializer = vitte_c17_ast_find_local_initializer(
                emitter,
                emitter->function,
                local_name);
            if (initializer != NULL &&
                initializer->kind == VITTE_AST_NODE_UNARY_EXPR &&
                initializer->operator_kind == VITTE_TOKEN_AMP) {
                return vitte_c17_ast_expression_type(
                    emitter,
                    vitte_c17_ast_child(emitter, initializer, 0u));
            }
        }
        return NULL;
    }
    if (expression->kind == VITTE_AST_NODE_TRY_EXPR) {
        const vitte_ast_node_t *operand_type =
            vitte_c17_ast_expression_type(
                emitter,
                vitte_c17_ast_child(emitter, expression, 0u));

        if (operand_type != NULL &&
            operand_type->kind == VITTE_AST_NODE_OPTIONAL_TYPE) {
            return vitte_c17_ast_child(emitter, operand_type, 0u);
        }
        return NULL;
    }
    if (expression->kind == VITTE_AST_NODE_CAST_EXPR) {
        return vitte_c17_ast_child(emitter, expression, 1u);
    }
    if (expression->kind == VITTE_AST_NODE_FORM_EXPR) {
        return vitte_c17_ast_child(emitter, expression, 0u);
    }
    if (expression->kind == VITTE_AST_NODE_IF_EXPR &&
        expression->child_count == 3u) {
        const vitte_ast_node_t *then_branch =
            vitte_c17_ast_child(emitter, expression, 1u);
        const vitte_ast_node_t *else_branch =
            vitte_c17_ast_child(emitter, expression, 2u);
        const vitte_ast_node_t *branch_value;

        if (then_branch != NULL &&
            then_branch->kind == VITTE_AST_NODE_BLOCK_EXPR &&
            then_branch->child_count != 0u) {
            branch_value = vitte_c17_ast_expression_type(
                emitter,
                vitte_c17_ast_child(
                    emitter,
                    then_branch,
                    then_branch->child_count - 1u));
            if (branch_value != NULL) {
                return branch_value;
            }
        }
        if (else_branch != NULL &&
            else_branch->kind == VITTE_AST_NODE_BLOCK_EXPR &&
            else_branch->child_count != 0u) {
            return vitte_c17_ast_expression_type(
                emitter,
                vitte_c17_ast_child(
                    emitter,
                    else_branch,
                    else_branch->child_count - 1u));
        }
        return NULL;
    }
    if (expression->kind == VITTE_AST_NODE_BLOCK_EXPR &&
        expression->child_count != 0u) {
        return vitte_c17_ast_expression_type(
            emitter,
            vitte_c17_ast_child(
                emitter,
                expression,
                expression->child_count - 1u));
    }
    if (expression->kind == VITTE_AST_NODE_CALL_EXPR) {
        const vitte_ast_node_t *callee =
            vitte_c17_ast_child(emitter, expression, 0u);
        const vitte_ast_node_t *method =
            callee != NULL && callee->kind == VITTE_AST_NODE_MEMBER_EXPR
                ? vitte_c17_ast_child(emitter, callee, 1u)
                : NULL;

        /* `str.as_bytes()` is represented by the C backend as a borrowed
         * string pointer, so keep the receiver's type for local inference. */
        if (expression->child_count == 1u &&
            vitte_c17_ast_token_is(emitter, method, "as_bytes") &&
            vitte_c17_ast_expression_is_string(
                emitter,
                vitte_c17_ast_child(emitter, callee, 0u))) {
            return vitte_c17_ast_expression_type(
                emitter,
                vitte_c17_ast_child(emitter, callee, 0u));
        }
        return vitte_c17_ast_call_return_type(
            emitter,
            expression);
    }
    if (expression->kind == VITTE_AST_NODE_INDEX_EXPR) {
        const vitte_ast_node_t *array_type;

        array_type = vitte_c17_ast_expression_type(
            emitter,
            vitte_c17_ast_child(emitter, expression, 0u));
        if (array_type == NULL) {
            return NULL;
        }
        if (array_type->kind == VITTE_AST_NODE_POINTER_TYPE ||
            array_type->kind == VITTE_AST_NODE_REFERENCE_TYPE) {
            return vitte_c17_ast_child(emitter, array_type, 0u);
        }
        if (array_type->kind == VITTE_AST_NODE_TYPE_EXPR) {
            const vitte_ast_node_t *element =
                vitte_c17_ast_generic_alias_slice_element(
                    emitter, array_type);
            if (element != NULL) {
                return element;
            }
        }
        if (array_type->kind != VITTE_AST_NODE_ARRAY_TYPE) {
            return NULL;
        }
        return vitte_c17_ast_child(
            emitter,
            array_type,
            array_type->child_count == 1u ? 0u : 1u);
    }
    if (expression->kind == VITTE_AST_NODE_MEMBER_EXPR) {
        return vitte_c17_ast_find_field_type(
            emitter,
            vitte_c17_ast_expression_type(
                emitter,
                vitte_c17_ast_child(emitter, expression, 0u)),
            vitte_c17_ast_child(emitter, expression, 1u));
    }
    if (expression->kind == VITTE_AST_NODE_BINARY_EXPR ||
        expression->kind == VITTE_AST_NODE_ASSIGN_EXPR) {
        switch (expression->operator_kind) {
            case VITTE_TOKEN_EQUAL_EQUAL:
            case VITTE_TOKEN_BANG_EQUAL:
            case VITTE_TOKEN_LESS:
            case VITTE_TOKEN_LESS_EQUAL:
            case VITTE_TOKEN_GREATER:
            case VITTE_TOKEN_GREATER_EQUAL:
            case VITTE_TOKEN_KW_AND:
            case VITTE_TOKEN_AMP_AMP:
            case VITTE_TOKEN_KW_OR:
            case VITTE_TOKEN_PIPE_PIPE:
                return NULL;
            default:
                return vitte_c17_ast_expression_type(
                    emitter,
                    vitte_c17_ast_child(emitter, expression, 0u));
        }
    }
    if (expression->kind != VITTE_AST_NODE_PATH &&
        expression->kind != VITTE_AST_NODE_IDENTIFIER) {
        return NULL;
    }
    name = expression;
    if (expression->kind == VITTE_AST_NODE_PATH) {
        if (expression->child_count != 1u) {
            return NULL;
        }
        name = vitte_c17_ast_child(emitter, expression, 0u);
    }
    if (emitter->function != NULL) {
        for (index = 1u;
             index < emitter->function->child_count;
             ++index) {
            const vitte_ast_node_t *parameter;

            parameter = vitte_c17_ast_child(
                emitter,
                emitter->function,
                index);
            if (parameter != NULL &&
                parameter->kind == VITTE_AST_NODE_PARAMETER &&
                parameter->child_count >= 2u &&
                vitte_c17_ast_token_equal(
                    emitter,
                    vitte_c17_ast_child(
                        emitter,
                        parameter,
                        0u),
                    name)) {
                return vitte_c17_ast_child(
                    emitter,
                    parameter,
                    1u);
            }
        }
        {
            const vitte_ast_node_t *local_type;

            local_type = vitte_c17_ast_find_local_type(
                emitter,
                emitter->function,
                name);
            if (local_type == NULL) {
                local_type = vitte_c17_ast_find_local_type(
                    emitter,
                    vitte_c17_ast_node(
                        emitter,
                        vitte_parser_root(emitter->parser)),
                    name);
            }
            if (local_type != NULL) {
                return local_type;
            }
        }
    }
    return NULL;
}

static bool
vitte_c17_ast_is_string_builtin_call(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *call)
{
    const vitte_ast_node_t *callee;
    const vitte_ast_node_t *base;
    const vitte_ast_node_t *base_name;
    const vitte_ast_node_t *method;

    if (call == NULL || call->kind != VITTE_AST_NODE_CALL_EXPR) {
        return false;
    }
    callee = vitte_c17_ast_child(emitter, call, 0u);
    if (callee == NULL || callee->kind != VITTE_AST_NODE_MEMBER_EXPR) {
        return false;
    }
    base = vitte_c17_ast_child(emitter, callee, 0u);
    method = vitte_c17_ast_child(emitter, callee, 1u);
    base_name = base;
    if (base != NULL && base->kind == VITTE_AST_NODE_PATH &&
        base->child_count == 1u) {
        base_name = vitte_c17_ast_child(emitter, base, 0u);
    }
    if (!vitte_c17_ast_token_is(emitter, base_name, "string") &&
        !vitte_c17_ast_token_is(emitter, base_name, "str")) {
        return false;
    }
    return vitte_c17_ast_token_is(emitter, method, "from") ||
           vitte_c17_ast_token_is(emitter, method, "from_byte") ||
           vitte_c17_ast_token_is(emitter, method, "from_bytes") ||
           vitte_c17_ast_token_is(emitter, method, "from_slice");
}

static bool
vitte_c17_ast_function_has_string_local(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (node == NULL || name == NULL) {
        return false;
    }
    if (node->kind == VITTE_AST_NODE_LET_STMT &&
        node->child_count >= 2u &&
        vitte_c17_ast_token_equal(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u),
            name)) {
        const vitte_ast_node_t *declared_type;
        const vitte_ast_node_t *initializer;

        declared_type = vitte_c17_ast_child(emitter, node, 1u);
        if (vitte_c17_ast_is_string_type(emitter, declared_type)) {
            return true;
        }
        if (node->child_count > 2u &&
            declared_type != NULL &&
            declared_type->kind == VITTE_AST_NODE_TYPE_EXPR) {
            initializer = vitte_c17_ast_child(emitter, node, 2u);
        } else {
            initializer = declared_type;
        }
        if (initializer != NULL &&
            initializer->kind == VITTE_AST_NODE_STRING_LITERAL) {
            return true;
        }
        if (initializer != NULL &&
            vitte_c17_ast_expression_is_string(
                emitter,
                initializer)) {
            return true;
        }
        if (initializer != NULL &&
            initializer->kind == VITTE_AST_NODE_CALL_EXPR &&
            (vitte_c17_ast_is_string_type(
                 emitter,
                 vitte_c17_ast_call_return_type(
                     emitter,
                     initializer)) ||
             vitte_c17_ast_is_string_builtin_call(
                 emitter,
                 initializer))) {
            return true;
        }
    }
    for (index = 0u; index < node->child_count; ++index) {
        if (vitte_c17_ast_function_has_string_local(
                emitter,
                vitte_c17_ast_child(emitter, node, index),
                name)) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_expression_is_string(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    const vitte_ast_node_t *name;

    if (expression == NULL) {
        return false;
    }
    if (expression->kind == VITTE_AST_NODE_BINARY_EXPR &&
        expression->operator_kind == VITTE_TOKEN_PLUS) {
        return vitte_c17_ast_expression_is_string(
                   emitter,
                   vitte_c17_ast_child(emitter, expression, 0u)) &&
               (vitte_c17_ast_expression_is_string(
                    emitter,
                    vitte_c17_ast_child(emitter, expression, 1u)) ||
                vitte_c17_ast_expression_is_character(
                    emitter,
                    vitte_c17_ast_child(emitter, expression, 1u)));
    }
    if (expression->kind == VITTE_AST_NODE_GROUP_EXPR) {
        return vitte_c17_ast_expression_is_string(
            emitter,
            vitte_c17_ast_child(emitter, expression, 0u));
    }
    if (expression->kind == VITTE_AST_NODE_STRING_LITERAL) {
        return true;
    }
    if (expression->kind == VITTE_AST_NODE_CALL_EXPR) {
        const vitte_ast_node_t *callee =
            vitte_c17_ast_child(emitter, expression, 0u);
        const vitte_ast_node_t *method =
            callee != NULL && callee->kind == VITTE_AST_NODE_MEMBER_EXPR
                ? vitte_c17_ast_child(emitter, callee, 1u)
                : NULL;

        if (vitte_c17_ast_token_is(emitter, method, "to_string") ||
            vitte_c17_ast_is_string_builtin_call(emitter, expression)) {
            return true;
        }
    }
    if (vitte_c17_ast_is_string_type(
            emitter,
            vitte_c17_ast_expression_type(
                emitter,
                expression))) {
        return true;
    }
    if (expression->kind != VITTE_AST_NODE_PATH &&
        expression->kind != VITTE_AST_NODE_IDENTIFIER) {
        return false;
    }
    name = expression->kind == VITTE_AST_NODE_IDENTIFIER
        ? expression
        : (expression->child_count == 1u
            ? vitte_c17_ast_child(emitter, expression, 0u)
            : NULL);
    return vitte_c17_ast_is_string_parameter(emitter, name) ||
           (emitter->function != NULL &&
            vitte_c17_ast_function_has_string_local(
                emitter,
                emitter->function,
                name));
}

static bool
vitte_c17_ast_function_has_pointer_local(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (node == NULL || name == NULL) {
        return false;
    }

    if (node->kind == VITTE_AST_NODE_LET_STMT &&
        node->child_count >= 2u) {
        const vitte_ast_node_t *local_name;
        const vitte_ast_node_t *type_or_initializer;
        const vitte_ast_node_t *initializer;

        local_name = vitte_c17_ast_child(emitter, node, 0u);
        type_or_initializer = vitte_c17_ast_child(emitter, node, 1u);
        if (local_name != NULL &&
            vitte_c17_ast_token_equal(emitter, local_name, name)) {
            if (vitte_c17_ast_is_pointer_type_node(
                    emitter,
                    type_or_initializer)) {
                return true;
            }
            initializer = type_or_initializer;
            if (type_or_initializer->kind == VITTE_AST_NODE_TYPE_EXPR &&
                node->child_count >= 3u) {
                initializer = vitte_c17_ast_child(emitter, node, 2u);
            }
            if (vitte_c17_ast_is_address_expression(initializer)) {
                return true;
            }
        }
    }

    for (index = 0u; index < node->child_count; ++index) {
        if (vitte_c17_ast_function_has_pointer_local(
                emitter,
                vitte_c17_ast_child(emitter, node, index),
                name)) {
            return true;
        }
    }

    return false;
}

static bool
vitte_c17_ast_expression_is_pointer(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    const vitte_ast_node_t *name;
    const vitte_ast_node_t *type;

    if (expression == NULL) {
        return false;
    }

    if (vitte_c17_ast_is_address_expression(expression)) {
        return true;
    }

    if (expression->kind == VITTE_AST_NODE_GROUP_EXPR) {
        return vitte_c17_ast_expression_is_pointer(
            emitter,
            vitte_c17_ast_child(emitter, expression, 0u));
    }

    type = vitte_c17_ast_expression_type(emitter, expression);
    if (vitte_c17_ast_is_pointer_type_node(emitter, type)) {
        return true;
    }

    if (expression->kind != VITTE_AST_NODE_PATH &&
        expression->kind != VITTE_AST_NODE_IDENTIFIER) {
        return false;
    }

    name = expression->kind == VITTE_AST_NODE_PATH
        ? (expression->child_count == 1u
            ? vitte_c17_ast_child(emitter, expression, 0u)
            : NULL)
        : expression;
    if (name == NULL) {
        return false;
    }

    return vitte_c17_ast_is_reference_parameter(emitter, name) ||
           (emitter->function != NULL &&
            vitte_c17_ast_function_has_pointer_local(
                emitter,
                emitter->function,
                name));
}

static bool
vitte_c17_ast_call_parameter_is_reference(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *callee,
    size_t parameter_index)
{
    const vitte_ast_node_t *procedure;
    size_t index;

    if (callee == NULL) {
        return false;
    }
    if (callee->kind == VITTE_AST_NODE_PATH &&
        callee->child_count != 0u) {
        procedure = vitte_c17_ast_find_path_declaration(
            emitter,
            callee,
            VITTE_AST_NODE_PROC_DECL,
            VITTE_AST_NODE_EXTERN_PROC_DECL);
    } else if (callee->kind == VITTE_AST_NODE_IDENTIFIER) {
        const vitte_ast_node_t *root = vitte_c17_ast_node(
            emitter, vitte_parser_root(emitter->parser));
        procedure = vitte_c17_ast_find_procedure_named(
            emitter, root, callee);
    } else {
        return false;
    }
    if (procedure == NULL) {
        return false;
    }

    {
        size_t current_parameter;

        current_parameter = 0u;
        for (index = 1u; index < procedure->child_count; ++index) {
            const vitte_ast_node_t *parameter;

            parameter = vitte_c17_ast_child(emitter, procedure, index);
            if (parameter == NULL ||
                parameter->kind != VITTE_AST_NODE_PARAMETER) {
                continue;
            }
            if (current_parameter++ != parameter_index) {
                continue;
            }
            return vitte_c17_ast_is_pointer_type_node(
                emitter,
                vitte_c17_ast_child(emitter, parameter, 1u));
        }
    }

    return false;
}

static void
vitte_c17_ast_emit_call_argument(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *call,
    size_t argument_index)
{
    const vitte_ast_node_t *callee;
    const vitte_ast_node_t *argument;
    const vitte_ast_node_t *name;

    callee = vitte_c17_ast_child(emitter, call, 0u);
    argument =
        vitte_c17_ast_child(
            emitter,
            call,
            argument_index + 1u);
    name = argument;
    if (argument != NULL &&
        argument->kind == VITTE_AST_NODE_PATH &&
        argument->child_count == 1u) {
        name = vitte_c17_ast_child(emitter, argument, 0u);
    }

    if (name != NULL &&
        vitte_c17_ast_is_reference_parameter(emitter, name) &&
        vitte_c17_ast_call_parameter_is_reference(
            emitter,
            callee,
            argument_index)) {
        vitte_c17_ast_write_token(emitter, name);
        return;
    }

    vitte_c17_ast_emit_expression(emitter, argument);
}

static bool
vitte_c17_ast_emit_string_builtin_callee(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *callee)
{
    const vitte_ast_node_t *name;

    name = callee;
    if (callee != NULL &&
        callee->kind == VITTE_AST_NODE_MEMBER_EXPR &&
        callee->child_count == 2u) {
        const vitte_ast_node_t *base;
        const vitte_ast_node_t *member;

        base = vitte_c17_ast_child(emitter, callee, 0u);
        member = vitte_c17_ast_child(emitter, callee, 1u);
        if (base != NULL && base->kind == VITTE_AST_NODE_PATH &&
            base->child_count == 1u &&
            (vitte_c17_ast_token_is(
                 emitter,
                 vitte_c17_ast_child(emitter, base, 0u),
                 "string") ||
             vitte_c17_ast_token_is(
                 emitter,
                 vitte_c17_ast_child(emitter, base, 0u),
                 "str"))) {
            name = member;
        }
    }
    if (callee != NULL &&
        (callee->kind == VITTE_AST_NODE_PATH ||
         callee->kind == VITTE_AST_NODE_TYPE_EXPR) &&
        callee->child_count != 0u) {
        name = vitte_c17_ast_child(
            emitter,
            callee,
            callee->child_count - 1u);
    }
    if (vitte_c17_ast_token_is(emitter, name, "print") &&
        vitte_c17_ast_find_procedure_named(emitter, NULL, name) == NULL) {
        vitte_c17_ast_write(emitter, "io_print");
        return true;
    }
    if (vitte_c17_ast_token_is(emitter, name, "len")) {
        vitte_c17_ast_write(emitter, "vitte_string_length");
        return true;
    }
    if (vitte_c17_ast_token_is(emitter, name, "trim")) {
        vitte_c17_ast_write(emitter, "vitte_string_trim");
        return true;
    }
    if (vitte_c17_ast_token_is(emitter, name, "starts_with")) {
        vitte_c17_ast_write(emitter, "vitte_string_starts_with");
        return true;
    }
    if (vitte_c17_ast_token_is(emitter, name, "ends_with")) {
        vitte_c17_ast_write(emitter, "vitte_string_ends_with");
        return true;
    }
    if (vitte_c17_ast_token_is(emitter, name, "from")) {
        vitte_c17_ast_write(emitter, "vitte_string_from");
        return true;
    }
    if (vitte_c17_ast_token_is(emitter, name, "from_slice")) {
        vitte_c17_ast_write(emitter, "vitte_string_from_slice");
        return true;
    }
    if (vitte_c17_ast_token_is(emitter, name, "from_byte")) {
        vitte_c17_ast_write(emitter, "vitte_string_from_byte");
        return true;
    }
    if (vitte_c17_ast_token_is(emitter, name, "from_bytes")) {
        vitte_c17_ast_write(emitter, "vitte_string_from_bytes");
        return true;
    }
    return false;
}

static bool
vitte_c17_ast_expression_is_pointer(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression);

static bool
vitte_c17_ast_member_uses_pointer(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *base)
{
    const vitte_ast_node_t *identifier;
    if (base == NULL) {
        return false;
    }

    if (base->kind == VITTE_AST_NODE_PATH ||
        base->kind == VITTE_AST_NODE_IDENTIFIER) {
        identifier = base->kind == VITTE_AST_NODE_PATH
            ? vitte_c17_ast_child(emitter, base, 0u)
            : base;
        if (identifier == NULL) {
            return false;
        }
        return vitte_c17_ast_is_reference_parameter(
                   emitter,
                   identifier) ||
               vitte_c17_ast_expression_is_pointer(
                   emitter,
                   base);
    }

    return vitte_c17_ast_expression_is_pointer(emitter, base);
}

static void
vitte_c17_ast_emit_path(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    size_t index;

    if (node == NULL || node->child_count == 0u) {
        emitter->failed = true;
        return;
    }

    for (index = 0u; index < node->child_count; ++index) {
        const vitte_ast_node_t *segment;

        segment = vitte_c17_ast_child(emitter, node, index);
        if (segment == NULL) {
            emitter->failed = true;
            return;
        }

        if (index != 0u) {
            vitte_c17_ast_write(emitter, "_");
        }

        if (index == 0u &&
            node->child_count == 1u &&
            vitte_c17_ast_token_is(emitter, segment, "main")) {
            vitte_c17_ast_write(emitter, "vitte_entry_main");
        } else {
            vitte_c17_ast_write_token(emitter, segment);
        }
    }
}

static void
vitte_c17_ast_emit_operator(
    vitte_c17_ast_emitter_t *emitter,
    vitte_token_kind_t kind)
{
    switch (kind) {
        case VITTE_TOKEN_PLUS: vitte_c17_ast_write(emitter, "+"); break;
        case VITTE_TOKEN_MINUS: vitte_c17_ast_write(emitter, "-"); break;
        case VITTE_TOKEN_STAR: vitte_c17_ast_write(emitter, "*"); break;
        case VITTE_TOKEN_SLASH: vitte_c17_ast_write(emitter, "/"); break;
        case VITTE_TOKEN_PERCENT: vitte_c17_ast_write(emitter, "%"); break;
        case VITTE_TOKEN_AMP: vitte_c17_ast_write(emitter, "&"); break;
        case VITTE_TOKEN_PIPE: vitte_c17_ast_write(emitter, "|"); break;
        case VITTE_TOKEN_CARET: vitte_c17_ast_write(emitter, "^"); break;
        case VITTE_TOKEN_TILDE: vitte_c17_ast_write(emitter, "~"); break;
        case VITTE_TOKEN_EQUAL: vitte_c17_ast_write(emitter, "="); break;
        case VITTE_TOKEN_PLUS_EQUAL: vitte_c17_ast_write(emitter, "+="); break;
        case VITTE_TOKEN_MINUS_EQUAL: vitte_c17_ast_write(emitter, "-="); break;
        case VITTE_TOKEN_STAR_EQUAL: vitte_c17_ast_write(emitter, "*="); break;
        case VITTE_TOKEN_SLASH_EQUAL: vitte_c17_ast_write(emitter, "/="); break;
        case VITTE_TOKEN_PERCENT_EQUAL: vitte_c17_ast_write(emitter, "%="); break;
        case VITTE_TOKEN_AMP_EQUAL: vitte_c17_ast_write(emitter, "&="); break;
        case VITTE_TOKEN_PIPE_EQUAL: vitte_c17_ast_write(emitter, "|="); break;
        case VITTE_TOKEN_CARET_EQUAL: vitte_c17_ast_write(emitter, "^="); break;
        case VITTE_TOKEN_SHIFT_LEFT_EQUAL: vitte_c17_ast_write(emitter, "<<="); break;
        case VITTE_TOKEN_SHIFT_RIGHT_EQUAL: vitte_c17_ast_write(emitter, ">>="); break;
        case VITTE_TOKEN_EQUAL_EQUAL: vitte_c17_ast_write(emitter, "=="); break;
        case VITTE_TOKEN_BANG_EQUAL: vitte_c17_ast_write(emitter, "!="); break;
        case VITTE_TOKEN_LESS: vitte_c17_ast_write(emitter, "<"); break;
        case VITTE_TOKEN_LESS_EQUAL: vitte_c17_ast_write(emitter, "<="); break;
        case VITTE_TOKEN_GREATER: vitte_c17_ast_write(emitter, ">"); break;
        case VITTE_TOKEN_GREATER_EQUAL: vitte_c17_ast_write(emitter, ">="); break;
        case VITTE_TOKEN_SHIFT_LEFT: vitte_c17_ast_write(emitter, "<<"); break;
        case VITTE_TOKEN_SHIFT_RIGHT: vitte_c17_ast_write(emitter, ">>"); break;
        case VITTE_TOKEN_KW_AND: vitte_c17_ast_write(emitter, "&&"); break;
        case VITTE_TOKEN_AMP_AMP: vitte_c17_ast_write(emitter, "&&"); break;
        case VITTE_TOKEN_KW_OR: vitte_c17_ast_write(emitter, "||"); break;
        case VITTE_TOKEN_PIPE_PIPE: vitte_c17_ast_write(emitter, "||"); break;
        case VITTE_TOKEN_KW_NOT: vitte_c17_ast_write(emitter, "!"); break;
        default: emitter->failed = true; break;
    }
}

static const vitte_ast_node_t *
vitte_c17_ast_find_procedure_named(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name)
{
    size_t index;
    if (node == NULL || name == NULL) { return NULL; }
    (void)node;
    for (index = 0u; index < emitter->procedure_declaration_count; ++index) {
        const vitte_ast_node_t *declaration =
            emitter->procedure_declarations[index];
        if (declaration != NULL && declaration->child_count != 0u &&
            vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, declaration, 0u),
                name)) {
            return declaration;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_generic_procedure_named(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name)
{
    size_t index;
    if (emitter == NULL || name == NULL) { return NULL; }
    for (index = 0u;
         index < emitter->procedure_declaration_count;
         ++index) {
        const vitte_ast_node_t *declaration =
            emitter->procedure_declarations[index];
        if (declaration != NULL && declaration->child_count != 0u &&
            vitte_c17_ast_generic_form_parameter_count(
                emitter, declaration, NULL) != 0u &&
            vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, declaration, 0u),
                name)) {
            return declaration;
        }
    }
    return NULL;
}

static const char *
vitte_c17_ast_scalar_c_type_name(const char *name)
{
    if (name == NULL) { return NULL; }
    if (strcmp(name, "bool") == 0) { return "bool"; }
    if (strcmp(name, "char") == 0) { return "char"; }
    if (strcmp(name, "i8") == 0) { return "int8_t"; }
    if (strcmp(name, "u8") == 0) { return "uint8_t"; }
    if (strcmp(name, "i16") == 0) { return "int16_t"; }
    if (strcmp(name, "u16") == 0) { return "uint16_t"; }
    if (strcmp(name, "i32") == 0 || strcmp(name, "int") == 0) {
        return "int32_t";
    }
    if (strcmp(name, "u32") == 0) { return "uint32_t"; }
    if (strcmp(name, "i64") == 0) { return "int64_t"; }
    if (strcmp(name, "u64") == 0) { return "uint64_t"; }
    if (strcmp(name, "isize") == 0) { return "ptrdiff_t"; }
    if (strcmp(name, "usize") == 0) { return "size_t"; }
    if (strcmp(name, "f32") == 0) { return "float"; }
    if (strcmp(name, "f64") == 0) { return "double"; }
    if (strcmp(name, "str") == 0 || strcmp(name, "string") == 0) {
        return "const char *";
    }
    if (strcmp(name, "void") == 0 || strcmp(name, "unit") == 0) {
        return "void";
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_enclosing_procedure(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *current;

    if (emitter == NULL || node == NULL || emitter->parent_ids == NULL ||
        node->id >= emitter->parent_id_count) {
        return NULL;
    }
    current = node;
    while (current != NULL && current->id < emitter->parent_id_count) {
        vitte_ast_node_id_t parent_id =
            emitter->parent_ids[(size_t)current->id];
        if (parent_id == VITTE_AST_INVALID_ID) { return NULL; }
        current = vitte_c17_ast_node(emitter, parent_id);
        if (current != NULL &&
            (current->kind == VITTE_AST_NODE_PROC_DECL ||
             current->kind == VITTE_AST_NODE_EXTERN_PROC_DECL)) {
            return current;
        }
    }
    return NULL;
}

static bool
vitte_c17_ast_generic_parameter_type_matches(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const vitte_ast_node_t *parameter_name)
{
    const vitte_ast_node_t *path;
    if (type == NULL || parameter_name == NULL ||
        type->kind != VITTE_AST_NODE_TYPE_EXPR || type->child_count != 1u) {
        return false;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    return path != NULL && path->kind == VITTE_AST_NODE_PATH &&
           path->child_count == 1u &&
           vitte_c17_ast_token_equal(
               emitter,
               vitte_c17_ast_child(emitter, path, 0u),
               parameter_name);
}

static const char *
vitte_c17_ast_infer_scalar_c_type(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression,
    const vitte_ast_node_t *call);

static const char *
vitte_c17_ast_infer_generic_call_result_scalar(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression,
    const vitte_ast_node_t *enclosing_call)
{
    const vitte_ast_node_t *callee;
    const vitte_ast_node_t *procedure;
    const vitte_ast_node_t *return_type;
    size_t generic_count;
    size_t generic_index;

    if (emitter == NULL || expression == NULL ||
        expression->kind != VITTE_AST_NODE_CALL_EXPR ||
        expression->child_count == 0u) {
        return NULL;
    }
    callee = vitte_c17_ast_child(emitter, expression, 0u);
    if (callee == NULL ||
        (callee->kind != VITTE_AST_NODE_PATH &&
         callee->kind != VITTE_AST_NODE_IDENTIFIER)) {
        return NULL;
    }
    if (callee->kind == VITTE_AST_NODE_PATH) {
        procedure = vitte_c17_ast_find_path_declaration(
            emitter,
            callee,
            VITTE_AST_NODE_PROC_DECL,
            VITTE_AST_NODE_EXTERN_PROC_DECL);
        if (procedure == NULL && callee->child_count == 1u) {
            procedure = vitte_c17_ast_find_generic_procedure_named(
                emitter,
                vitte_c17_ast_child(emitter, callee, 0u));
        }
    } else {
        procedure = vitte_c17_ast_find_generic_procedure_named(
            emitter, callee);
    }
    generic_count = vitte_c17_ast_generic_form_parameter_count(
        emitter, procedure, NULL);
    if (procedure == NULL || generic_count == 0u) { return NULL; }
    return_type = vitte_c17_ast_function_return_type(emitter, procedure);
    if (return_type == NULL) { return NULL; }

    for (generic_index = 0u;
         generic_index < generic_count;
         ++generic_index) {
        const vitte_ast_node_t *generic_name =
            vitte_c17_ast_generic_form_parameter_name(
                emitter, procedure, generic_index);
        size_t parameter_index;
        size_t argument_index = 1u;
        size_t found = 0u;
        const char *resolved = NULL;

        if (!vitte_c17_ast_generic_parameter_type_matches(
                emitter, return_type, generic_name)) {
            continue;
        }
        for (parameter_index = 1u;
             parameter_index < procedure->child_count;
             ++parameter_index) {
            const vitte_ast_node_t *parameter =
                vitte_c17_ast_child(emitter, procedure, parameter_index);
            const vitte_ast_node_t *parameter_type;
            const vitte_ast_node_t *argument;
            const char *candidate;

            if (parameter == NULL ||
                parameter->kind != VITTE_AST_NODE_PARAMETER) {
                continue;
            }
            parameter_type = vitte_c17_ast_child(emitter, parameter, 1u);
            if (vitte_c17_ast_generic_parameter_type_matches(
                    emitter, parameter_type, generic_name)) {
                ++found;
                if (argument_index >= expression->child_count) {
                    return NULL;
                }
                argument = vitte_c17_ast_child(
                    emitter, expression, argument_index);
                candidate = vitte_c17_ast_infer_scalar_c_type(
                    emitter, argument, enclosing_call);
                if (candidate == NULL ||
                    (resolved != NULL && strcmp(resolved, candidate) != 0)) {
                    return NULL;
                }
                resolved = candidate;
            }
            ++argument_index;
        }
        if (found != 0u) { return resolved; }
    }
    return NULL;
}

static const char *
vitte_c17_ast_infer_scalar_c_type(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression,
    const vitte_ast_node_t *call)
{
    const vitte_ast_node_t *previous_function;
    const vitte_ast_node_t *type;
    const char *scalar_type;
    size_t substitution_index;
    char name[64];

    if (expression == NULL) { return NULL; }
    if (expression->kind == VITTE_AST_NODE_IDENTIFIER ||
        expression->kind == VITTE_AST_NODE_PATH) {
        const vitte_ast_node_t *owner;
        const vitte_ast_node_t *local_name;
        const vitte_ast_node_t *initializer;
        const vitte_ast_node_t *local_previous_function;
        const char *inferred;

        owner = vitte_c17_ast_enclosing_procedure(emitter, call);
        local_name = expression;
        if (expression->kind == VITTE_AST_NODE_PATH &&
            expression->child_count == 1u) {
            local_name = vitte_c17_ast_child(emitter, expression, 0u);
        }
        local_previous_function = emitter->function;
        emitter->function = owner;
        initializer = vitte_c17_ast_find_local_initializer(
            emitter, owner, local_name);
        if (initializer != NULL) {
            inferred = vitte_c17_ast_infer_scalar_c_type(
                emitter, initializer, call);
            emitter->function = local_previous_function;
            if (inferred != NULL) { return inferred; }
        } else {
            emitter->function = local_previous_function;
        }
    }
    switch (expression->kind) {
        case VITTE_AST_NODE_INTEGER_LITERAL: return "int64_t";
        case VITTE_AST_NODE_FLOAT_LITERAL: return "double";
        case VITTE_AST_NODE_STRING_LITERAL: return "const char *";
        case VITTE_AST_NODE_CHARACTER_LITERAL: return "char";
        case VITTE_AST_NODE_BOOL_LITERAL: return "bool";
        case VITTE_AST_NODE_NULL_LITERAL: return "void *";
        case VITTE_AST_NODE_GROUP_EXPR:
            return vitte_c17_ast_infer_scalar_c_type(
                emitter,
                vitte_c17_ast_child(emitter, expression, 0u),
                call);
        case VITTE_AST_NODE_CALL_EXPR: {
            const char *generic_result =
                vitte_c17_ast_infer_generic_call_result_scalar(
                    emitter, expression, call);
            if (generic_result != NULL) { return generic_result; }
            previous_function = emitter->function;
            emitter->function = vitte_c17_ast_enclosing_procedure(
                emitter, call);
            type = vitte_c17_ast_expression_type(emitter, expression);
            emitter->function = previous_function;
            break;
        }

        case VITTE_AST_NODE_CAST_EXPR:
            type = vitte_c17_ast_child(emitter, expression, 1u);
            break;
        default:
            previous_function = emitter->function;
            emitter->function = vitte_c17_ast_enclosing_procedure(
                emitter, call);
            type = vitte_c17_ast_expression_type(emitter, expression);
            emitter->function = previous_function;
            break;
    }
    if (!vitte_c17_ast_simple_type_name(
            emitter,
            type,
            name,
            sizeof(name))) {
        return NULL;
    }
    scalar_type = vitte_c17_ast_scalar_c_type_name(name);
    if (scalar_type != NULL) { return scalar_type; }
    substitution_index = vitte_c17_ast_generic_substitution_index(
        emitter, type);
    if (substitution_index != SIZE_MAX &&
        substitution_index < emitter->generic_substitution_count) {
        return emitter->generic_c_type_names[substitution_index];
    }
    return NULL;
}

static size_t
vitte_c17_ast_generic_proc_instance_for_call(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *call)
{
    size_t index;
    if (emitter == NULL || call == NULL ||
        call->id >= emitter->parent_id_count) {
        return SIZE_MAX;
    }
    if (emitter->generic_proc_context_active) {
        index = emitter->generic_proc_call_binding_count;
        while (index != 0u) {
            const vitte_c17_ast_generic_proc_call_binding_t *binding;
            --index;
            binding = &emitter->generic_proc_call_bindings[index];
            if (binding->call_id == (size_t)call->id &&
                binding->owner_instance_index ==
                    emitter->generic_proc_context_index) {
                return binding->target_instance_index;
            }
        }
        return SIZE_MAX;
    }
    if (emitter->generic_call_instance_ids == NULL ||
        emitter->generic_call_instance_ids[(size_t)call->id] == 0u) {
        return SIZE_MAX;
    }
    return emitter->generic_call_instance_ids[(size_t)call->id] - 1u;
}

static char *
vitte_c17_ast_duplicate_string(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) { return NULL; }
    length = strlen(text);
    if (length == SIZE_MAX) { return NULL; }
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) { return NULL; }
    memcpy(copy, text, length + 1u);
    return copy;
}

static const char *
vitte_c17_ast_source_scalar_name_from_c_type(const char *c_type)
{
    if (c_type == NULL) { return NULL; }
    if (strcmp(c_type, "bool") == 0) { return "bool"; }
    if (strcmp(c_type, "char") == 0) { return "char"; }
    if (strcmp(c_type, "int8_t") == 0) { return "i8"; }
    if (strcmp(c_type, "uint8_t") == 0) { return "u8"; }
    if (strcmp(c_type, "int16_t") == 0) { return "i16"; }
    if (strcmp(c_type, "uint16_t") == 0) { return "u16"; }
    if (strcmp(c_type, "int32_t") == 0) { return "i32"; }
    if (strcmp(c_type, "uint32_t") == 0) { return "u32"; }
    if (strcmp(c_type, "int64_t") == 0) { return "i64"; }
    if (strcmp(c_type, "uint64_t") == 0) { return "u64"; }
    if (strcmp(c_type, "float") == 0) { return "f32"; }
    if (strcmp(c_type, "double") == 0) { return "f64"; }
    if (strcmp(c_type, "const char *") == 0) { return "string"; }
    return NULL;
}

static const char *
vitte_c17_ast_c_type_for_generic_argument(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type)
{
    char name[128];
    size_t substitution_index;
    if (type == NULL) { return NULL; }
    if (vitte_c17_ast_simple_type_name(
            emitter, type, name, sizeof(name))) {
        const char *scalar = vitte_c17_ast_scalar_c_type_name(name);
        if (scalar != NULL) { return scalar; }
    }
    substitution_index =
        vitte_c17_ast_generic_substitution_index(emitter, type);
    if (substitution_index != SIZE_MAX &&
        substitution_index < emitter->generic_substitution_count) {
        return emitter->generic_c_type_names[substitution_index];
    }
    return NULL;
}

static bool
vitte_c17_ast_type_paths_equal(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *left,
    const vitte_ast_node_t *right)
{
    size_t index;
    if (left == NULL || right == NULL ||
        left->kind != VITTE_AST_NODE_PATH ||
        right->kind != VITTE_AST_NODE_PATH ||
        left->child_count != right->child_count) {
        return false;
    }
    for (index = 0u; index < left->child_count; ++index) {
        if (!vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, left, index),
                vitte_c17_ast_child(emitter, right, index))) {
            return false;
        }
    }
    return true;
}

static bool
vitte_c17_ast_type_contains_generic_parameter(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const vitte_ast_node_t *parameter_name,
    size_t depth)
{
    size_t index;
    if (type == NULL || parameter_name == NULL || depth > 512u) {
        return false;
    }
    if (vitte_c17_ast_generic_parameter_type_matches(
            emitter, type, parameter_name)) {
        return true;
    }
    for (index = 0u; index < type->child_count; ++index) {
        if (vitte_c17_ast_type_contains_generic_parameter(
                emitter,
                vitte_c17_ast_child(emitter, type, index),
                parameter_name,
                depth + 1u)) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_infer_generic_parameter_from_type(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pattern,
    const vitte_ast_node_t *actual_type,
    const vitte_ast_node_t *actual_expression,
    const vitte_ast_node_t *call,
    const vitte_ast_node_t *parameter_name,
    const vitte_ast_node_t **resolved_type,
    const char **resolved_c_type,
    char *resolved_key,
    size_t resolved_key_capacity,
    bool *found,
    size_t depth)
{
    size_t index;
    if (emitter == NULL || pattern == NULL || parameter_name == NULL ||
        found == NULL || depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        return false;
    }
    if (!vitte_c17_ast_type_contains_generic_parameter(
            emitter, pattern, parameter_name, depth)) {
        return true;
    }
    if (vitte_c17_ast_generic_parameter_type_matches(
            emitter, pattern, parameter_name)) {
        char candidate_key[VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX];
        const vitte_ast_node_t *candidate_type = actual_type;
        const char *candidate_c_type = NULL;
        const char *source_name;
        bool key_ready = false;
        candidate_key[0] = '\0';
        if (candidate_type != NULL) {
            candidate_c_type =
                vitte_c17_ast_c_type_for_generic_argument(
                    emitter, candidate_type);
            key_ready = vitte_c17_ast_generic_argument_name(
                emitter,
                candidate_type,
                candidate_key,
                sizeof(candidate_key),
                depth + 1u);
            if (!key_ready) {
                candidate_c_type =
                    vitte_c17_ast_infer_scalar_c_type(
                        emitter, actual_expression, call);
                if (candidate_c_type != NULL) {
                    candidate_type = NULL;
                }
            }
        } else {
            candidate_c_type = vitte_c17_ast_infer_scalar_c_type(
                emitter, actual_expression, call);
        }
        if (!key_ready) {
            source_name = vitte_c17_ast_source_scalar_name_from_c_type(
                candidate_c_type);
            if (source_name != NULL &&
                strlen(source_name) < sizeof(candidate_key)) {
                memcpy(candidate_key, source_name, strlen(source_name) + 1u);
                key_ready = true;
            }
        }
        if (!key_ready || candidate_key[0] == '\0') { return false; }
        if (!*found) {
            if (strlen(candidate_key) >= resolved_key_capacity) {
                return false;
            }
            *resolved_type = candidate_type;
            *resolved_c_type = candidate_c_type;
            memcpy(resolved_key, candidate_key, strlen(candidate_key) + 1u);
            *found = true;
            return true;
        }
        if (strcmp(resolved_key, candidate_key) != 0) { return false; }
        if (*resolved_type == NULL && candidate_type != NULL) {
            *resolved_type = candidate_type;
        }
        if (*resolved_c_type == NULL && candidate_c_type != NULL) {
            *resolved_c_type = candidate_c_type;
        }
        return true;
    }

    if (actual_type == NULL || pattern->kind != actual_type->kind) {
        return false;
    }
    if (pattern->kind == VITTE_AST_NODE_TYPE_EXPR) {
        const vitte_ast_node_t *pattern_path =
            vitte_c17_ast_child(emitter, pattern, 0u);
        const vitte_ast_node_t *actual_path =
            vitte_c17_ast_child(emitter, actual_type, 0u);
        if (!vitte_c17_ast_type_paths_equal(
                emitter, pattern_path, actual_path) ||
            pattern->child_count != actual_type->child_count) {
            return false;
        }
        for (index = 1u; index < pattern->child_count; ++index) {
            if (!vitte_c17_ast_infer_generic_parameter_from_type(
                    emitter,
                    vitte_c17_ast_child(emitter, pattern, index),
                    vitte_c17_ast_child(emitter, actual_type, index),
                    actual_expression,
                    call,
                    parameter_name,
                    resolved_type,
                    resolved_c_type,
                    resolved_key,
                    resolved_key_capacity,
                    found,
                    depth + 1u)) {
                return false;
            }
        }
        return true;
    }
    if (pattern->kind == VITTE_AST_NODE_ARRAY_TYPE) {
        if (pattern->child_count != actual_type->child_count) {
            return false;
        }
        for (index = 0u; index < pattern->child_count; ++index) {
            const vitte_ast_node_t *pattern_child =
                vitte_c17_ast_child(emitter, pattern, index);
            const vitte_ast_node_t *actual_child =
                vitte_c17_ast_child(emitter, actual_type, index);
            if (pattern_child == NULL || actual_child == NULL) {
                return false;
            }
            if (pattern_child->kind == VITTE_AST_NODE_INTEGER_LITERAL) {
                if (!vitte_c17_ast_token_equal(
                        emitter, pattern_child, actual_child)) {
                    return false;
                }
            } else if (!vitte_c17_ast_infer_generic_parameter_from_type(
                           emitter,
                           pattern_child,
                           actual_child,
                           actual_expression,
                           call,
                           parameter_name,
                           resolved_type,
                           resolved_c_type,
                           resolved_key,
                           resolved_key_capacity,
                           found,
                           depth + 1u)) {
                return false;
            }
        }
        return true;
    }
    for (index = 0u; index < pattern->child_count; ++index) {
        if (!vitte_c17_ast_infer_generic_parameter_from_type(
                emitter,
                vitte_c17_ast_child(emitter, pattern, index),
                vitte_c17_ast_child(emitter, actual_type, index),
                actual_expression,
                call,
                parameter_name,
                resolved_type,
                resolved_c_type,
                resolved_key,
                resolved_key_capacity,
                found,
                depth + 1u)) {
            return false;
        }
    }
    return true;
}

static bool
vitte_c17_ast_bind_generic_proc_call(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *call,
    size_t target_instance_index)
{
    size_t index;
    if (emitter == NULL || call == NULL ||
        call->id >= emitter->parent_id_count ||
        target_instance_index >= emitter->generic_proc_count) {
        return false;
    }
    if (!emitter->generic_proc_context_active) {
        if (emitter->generic_call_instance_ids == NULL) { return false; }
        emitter->generic_call_instance_ids[(size_t)call->id] =
            target_instance_index + 1u;
        return true;
    }
    for (index = 0u;
         index < emitter->generic_proc_call_binding_count;
         ++index) {
        vitte_c17_ast_generic_proc_call_binding_t *binding =
            &emitter->generic_proc_call_bindings[index];
        if (binding->call_id == (size_t)call->id &&
            binding->owner_instance_index ==
                emitter->generic_proc_context_index) {
            binding->target_instance_index = target_instance_index;
            return true;
        }
    }
    if (emitter->generic_proc_call_binding_count == SIZE_MAX ||
        !vitte_c17_ast_emitter_reserve_generic_proc_call_bindings(
            emitter,
            emitter->generic_proc_call_binding_count + 1u)) {
        return false;
    }
    emitter->generic_proc_call_bindings[
        emitter->generic_proc_call_binding_count++] =
        (vitte_c17_ast_generic_proc_call_binding_t){
            (size_t)call->id,
            emitter->generic_proc_context_index,
            target_instance_index,
        };
    return true;
}

static bool
vitte_c17_ast_generic_proc_arguments_equal(
    const vitte_c17_ast_generic_proc_instance_t *instance,
    const vitte_ast_node_t *procedure,
    char *const *argument_type_keys,
    size_t argument_count)
{
    size_t index;
    if (instance == NULL || instance->declaration != procedure ||
        instance->argument_count != argument_count) {
        return false;
    }
    for (index = 0u; index < argument_count; ++index) {
        if (instance->argument_type_keys[index] == NULL ||
            argument_type_keys[index] == NULL ||
            strcmp(instance->argument_type_keys[index],
                   argument_type_keys[index]) != 0) {
            return false;
        }
    }
    return true;
}

static void
vitte_c17_ast_register_generic_proc_call(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *call)
{
    const vitte_ast_node_t *callee;
    const vitte_ast_node_t *procedure;
    const vitte_ast_node_t *procedure_name;
    const vitte_ast_node_t *argument_type;
    const vitte_ast_node_t *parameter;
    const vitte_ast_node_t *argument;
    const vitte_ast_node_t *owner;
    const vitte_ast_node_t **argument_type_nodes;
    const char **argument_c_types;
    char **argument_type_keys;
    const vitte_ast_node_t *previous_function;
    size_t generic_count;
    size_t parameter_count;
    size_t generic_index;
    size_t parameter_index;
    size_t instance_index;
    size_t index;

    if (emitter == NULL || call == NULL ||
        call->kind != VITTE_AST_NODE_CALL_EXPR || call->child_count == 0u ||
        call->id >= emitter->parent_id_count) {
        return;
    }
    callee = vitte_c17_ast_child(emitter, call, 0u);
    if (callee == NULL ||
        (callee->kind != VITTE_AST_NODE_PATH &&
         callee->kind != VITTE_AST_NODE_IDENTIFIER)) {
        return;
    }
    if (callee->kind == VITTE_AST_NODE_PATH && callee->child_count > 1u) {
        procedure_name = vitte_c17_ast_child(
            emitter,
            callee,
            callee->child_count - 1u);
        if (vitte_c17_ast_find_generic_procedure_named(
                emitter, procedure_name) == NULL) {
            return;
        }
        procedure = vitte_c17_ast_find_path_declaration(
            emitter,
            callee,
            VITTE_AST_NODE_PROC_DECL,
            VITTE_AST_NODE_EXTERN_PROC_DECL);
    } else {
        procedure_name = callee->kind == VITTE_AST_NODE_PATH
            ? vitte_c17_ast_child(emitter, callee, 0u)
            : callee;
        procedure = vitte_c17_ast_find_generic_procedure_named(
            emitter, procedure_name);
    }
    generic_count = vitte_c17_ast_generic_form_parameter_count(
        emitter,
        procedure,
        NULL);
    if (generic_count == 0u) { return; }
    owner = vitte_c17_ast_enclosing_procedure(emitter, call);
    if (owner != NULL &&
        vitte_c17_ast_generic_form_parameter_count(
            emitter,
            owner,
            NULL) != 0u &&
        !emitter->generic_proc_context_active) {
        return;
    }
    if (procedure == NULL || procedure->kind != VITTE_AST_NODE_PROC_DECL) {
        emitter->failed = true;
        return;
    }
    argument_type_nodes = (const vitte_ast_node_t **)calloc(
        generic_count, sizeof(*argument_type_nodes));
    argument_c_types = (const char **)calloc(
        generic_count, sizeof(*argument_c_types));
    argument_type_keys = (char **)calloc(
        generic_count, sizeof(*argument_type_keys));
    if (argument_type_nodes == NULL || argument_c_types == NULL ||
        argument_type_keys == NULL) {
        free((void *)argument_type_nodes);
        free((void *)argument_c_types);
        free(argument_type_keys);
        emitter->failed = true;
        return;
    }
    parameter_count = 0u;
    for (index = 1u; index < procedure->child_count; ++index) {
        parameter = vitte_c17_ast_child(emitter, procedure, index);
        if (parameter != NULL &&
            parameter->kind == VITTE_AST_NODE_PARAMETER) {
            ++parameter_count;
        }
    }
    if (parameter_count + 1u != call->child_count) {
        free((void *)argument_type_nodes);
        free((void *)argument_c_types);
        free(argument_type_keys);
        emitter->failed = true;
        return;
    }
    for (generic_index = 0u;
         generic_index < generic_count;
         ++generic_index) {
        const vitte_ast_node_t *generic_parameter =
            vitte_c17_ast_generic_form_parameter_name(
                emitter, procedure, generic_index);
        bool found = false;
        char resolved_key[VITTE_C17_AST_GENERIC_FORM_ARGUMENT_NAME_MAX];
        const vitte_ast_node_t *resolved_type = NULL;
        const char *resolved_c_type = NULL;
        resolved_key[0] = '\0';
        if (generic_parameter == NULL) {
            emitter->failed = true;
            break;
        }
        for (parameter_index = 0u;
             parameter_index < parameter_count;
             ++parameter_index) {
            parameter = NULL;
            {
                size_t seen = 0u;
                for (index = 1u; index < procedure->child_count; ++index) {
                    const vitte_ast_node_t *candidate =
                        vitte_c17_ast_child(emitter, procedure, index);
                    if (candidate != NULL &&
                        candidate->kind == VITTE_AST_NODE_PARAMETER &&
                        seen++ == parameter_index) {
                        parameter = candidate;
                        break;
                    }
                }
            }
            argument_type = parameter != NULL
                ? vitte_c17_ast_child(emitter, parameter, 1u)
                : NULL;
            if (!vitte_c17_ast_type_contains_generic_parameter(
                    emitter, argument_type, generic_parameter, 0u)) {
                continue;
            }
            argument = vitte_c17_ast_child(
                emitter, call, parameter_index + 1u);
            previous_function = emitter->function;
            emitter->function = owner;
            {
                const vitte_ast_node_t *actual_type =
                    vitte_c17_ast_expression_type(emitter, argument);
                bool inferred =
                    vitte_c17_ast_infer_generic_parameter_from_type(
                        emitter,
                        argument_type,
                        actual_type,
                        argument,
                        call,
                        generic_parameter,
                        &resolved_type,
                        &resolved_c_type,
                        resolved_key,
                        sizeof(resolved_key),
                        &found,
                        0u);
                emitter->function = previous_function;
                if (!inferred) {
                    emitter->failed = true;
                    break;
                }
            }
        }
        if (!found) { emitter->failed = true; }
        if (emitter->failed) { break; }
        argument_type_nodes[generic_index] = resolved_type;
        argument_c_types[generic_index] = resolved_c_type;
        argument_type_keys[generic_index] =
            vitte_c17_ast_duplicate_string(resolved_key);
        if (argument_type_keys[generic_index] == NULL) {
            emitter->failed = true;
            break;
        }
    }
    if (emitter->failed) {
        for (generic_index = 0u;
             generic_index < generic_count;
             ++generic_index) {
            free(argument_type_keys[generic_index]);
        }
        free((void *)argument_type_nodes);
        free((void *)argument_c_types);
        free(argument_type_keys);
        return;
    }
    for (instance_index = 0u;
         instance_index < emitter->generic_proc_count;
         ++instance_index) {
        if (vitte_c17_ast_generic_proc_arguments_equal(
                &emitter->generic_procs[instance_index],
                procedure,
                argument_type_keys,
                generic_count)) {
            if (!vitte_c17_ast_bind_generic_proc_call(
                    emitter, call, instance_index)) {
                emitter->failed = true;
            }
            for (generic_index = 0u;
                 generic_index < generic_count;
                 ++generic_index) {
                free(argument_type_keys[generic_index]);
            }
            free((void *)argument_type_nodes);
            free((void *)argument_c_types);
            free(argument_type_keys);
            return;
        }
    }
    if (emitter->generic_proc_count == SIZE_MAX ||
        !vitte_c17_ast_emitter_reserve_generic_procs(
            emitter,
            emitter->generic_proc_count + 1u)) {
        for (generic_index = 0u;
             generic_index < generic_count;
             ++generic_index) {
            free(argument_type_keys[generic_index]);
        }
        free((void *)argument_type_nodes);
        free((void *)argument_c_types);
        free(argument_type_keys);
        emitter->failed = true;
        return;
    }
    instance_index = emitter->generic_proc_count;
    emitter->generic_procs[instance_index].argument_type_nodes =
        argument_type_nodes;
    emitter->generic_procs[instance_index].argument_c_types =
        argument_c_types;
    emitter->generic_procs[instance_index].argument_type_keys =
        argument_type_keys;
    emitter->generic_procs[instance_index].declaration = procedure;
    emitter->generic_procs[instance_index].argument_count = generic_count;
    (void)snprintf(
        emitter->generic_procs[instance_index].c_name,
        sizeof(emitter->generic_procs[instance_index].c_name),
        "vitte_generic_proc_%zu",
        instance_index);
    ++emitter->generic_proc_count;
    if (!vitte_c17_ast_bind_generic_proc_call(
            emitter, call, instance_index)) {
        emitter->failed = true;
    }
}

static void
vitte_c17_ast_collect_generic_proc_instances(
    vitte_c17_ast_emitter_t *emitter)
{
    size_t index;
    if (emitter == NULL || emitter->parser == NULL) {
        return;
    }
    for (index = 0u;
         index < emitter->parser->node_count && !emitter->failed;
         ++index) {
        const vitte_ast_node_t *node = &emitter->parser->nodes[index];
        if (node->kind == VITTE_AST_NODE_PROC_DECL ||
            node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) {
            if (emitter->procedure_declaration_count == SIZE_MAX ||
                !vitte_c17_ast_emitter_reserve_procedure_declarations(
                    emitter,
                    emitter->procedure_declaration_count + 1u)) {
                emitter->failed = true;
                return;
            }
            emitter->procedure_declarations[
                emitter->procedure_declaration_count++] = node;
        }
    }
    for (index = 0u;
         index < emitter->parser->node_count && !emitter->failed;
         ++index) {
        const vitte_ast_node_t *node = &emitter->parser->nodes[index];
        if (node->kind == VITTE_AST_NODE_CALL_EXPR) {
            vitte_c17_ast_register_generic_proc_call(emitter, node);
        }
    }
}

static bool
vitte_c17_ast_path_prefix_matches(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *path,
    size_t path_count,
    size_t path_index,
    const vitte_ast_node_t *space_path)
{
    size_t index;

    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        space_path == NULL || space_path->kind != VITTE_AST_NODE_PATH ||
        space_path->child_count == 0u ||
        path_index > path_count ||
        space_path->child_count > path_count - path_index) {
        return false;
    }
    for (index = 0u; index < space_path->child_count; ++index) {
        if (!vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, path, path_index + index),
                vitte_c17_ast_child(emitter, space_path, index))) {
            return false;
        }
    }
    return true;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_space_for_path(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    const vitte_ast_node_t *path,
    size_t path_count,
    size_t path_index,
    size_t depth)
{
    size_t index;

    if (scope == NULL || path == NULL || depth > 128u) {
        return NULL;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);
        const vitte_ast_node_t *space_path;
        const vitte_ast_node_t *body;
        size_t next_index;
        const vitte_ast_node_t *found;

        if (declaration == NULL ||
            declaration->kind != VITTE_AST_NODE_SPACE_DECL ||
            declaration->child_count < 2u) {
            continue;
        }
        space_path = vitte_c17_ast_child(emitter, declaration, 0u);
        if (!vitte_c17_ast_path_prefix_matches(
                emitter, path, path_count, path_index, space_path)) {
            continue;
        }
        next_index = path_index + space_path->child_count;
        if (next_index == path_count) {
            return declaration;
        }
        body = vitte_c17_ast_child(emitter, declaration, 1u);
        if (body == NULL || body->kind != VITTE_AST_NODE_BLOCK) {
            continue;
        }
        found = vitte_c17_ast_find_space_for_path(
            emitter,
            body,
            path,
            path_count,
            next_index,
            depth + 1u);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_direct_space(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    const vitte_ast_node_t *path,
    size_t path_index,
    size_t path_count)
{
    size_t index;

    if (scope == NULL || path == NULL || path_index >= path_count) {
        return NULL;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);
        const vitte_ast_node_t *space_path;

        if (declaration == NULL ||
            declaration->kind != VITTE_AST_NODE_SPACE_DECL ||
            declaration->child_count < 2u) {
            continue;
        }
        space_path = vitte_c17_ast_child(emitter, declaration, 0u);
        if (vitte_c17_ast_path_prefix_matches(
                emitter, path, path_count, path_index, space_path) &&
            path_index + space_path->child_count <= path_count) {
            return declaration;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_use_alias_in_scope(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    const vitte_ast_node_t *alias)
{
    size_t index;

    if (scope == NULL || alias == NULL) {
        return NULL;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);
        const vitte_ast_node_t *path;
        const vitte_ast_node_t *declared_alias;

        if (declaration == NULL ||
            declaration->kind != VITTE_AST_NODE_USE_DECL ||
            (declaration->flags & VITTE_AST_FLAG_GLOB_IMPORT) != 0u ||
            declaration->child_count == 0u) {
            continue;
        }
        path = vitte_c17_ast_child(emitter, declaration, 0u);
        if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
            path->child_count == 0u) {
            continue;
        }
        declared_alias = NULL;
        if (declaration->child_count == 2u) {
            const vitte_ast_node_t *candidate =
                vitte_c17_ast_child(emitter, declaration, 1u);
            if (candidate != NULL &&
                candidate->kind == VITTE_AST_NODE_IDENTIFIER) {
                declared_alias = candidate;
            }
        }
        if (declared_alias == NULL) {
            if (declaration->child_count != 1u) {
                continue;
            }
            declared_alias = vitte_c17_ast_child(
                emitter, path, path->child_count - 1u);
        }
        if (vitte_c17_ast_token_equal(emitter, declared_alias, alias)) {
            return declaration;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_current_use_alias(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *alias)
{
    size_t index;
    const vitte_ast_node_t *root;
    const vitte_ast_node_t *found;

    for (index = emitter->space_depth; index != 0u; --index) {
        const vitte_ast_node_t *space = emitter->space_stack[index - 1u];
        const vitte_ast_node_t *body =
            vitte_c17_ast_child(emitter, space, 1u);
        found = vitte_c17_ast_find_use_alias_in_scope(
            emitter, body, alias);
        if (found != NULL) {
            return found;
        }
    }
    root = vitte_c17_ast_node(
        emitter, vitte_parser_root(emitter->parser));
    return vitte_c17_ast_find_use_alias_in_scope(emitter, root, alias);
}

static const vitte_ast_node_t *
vitte_c17_ast_find_module_for_path(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *path,
    size_t module_path_count)
{
    const vitte_ast_node_t *root;
    const vitte_ast_node_t *space;
    const vitte_ast_node_t *use;
    const vitte_ast_node_t *use_path;
    size_t index;

    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        module_path_count == 0u || module_path_count > path->child_count) {
        return NULL;
    }
    root = vitte_c17_ast_node(
        emitter, vitte_parser_root(emitter->parser));
    space = vitte_c17_ast_find_space_for_path(
        emitter, root, path, module_path_count, 0u, 0u);
    if (space != NULL) {
        return space;
    }

    use = vitte_c17_ast_find_current_use_alias(
        emitter,
        vitte_c17_ast_child(emitter, path, 0u));
    if (use == NULL) {
        return NULL;
    }
    use_path = vitte_c17_ast_child(emitter, use, 0u);
    if (use_path == NULL || use_path->kind != VITTE_AST_NODE_PATH) {
        return NULL;
    }
    space = vitte_c17_ast_find_space_for_path(
        emitter,
        root,
        use_path,
        use_path->child_count,
        0u,
        0u);
    if (space == NULL) {
        return NULL;
    }

    for (index = 1u; index < module_path_count; ++index) {
        const vitte_ast_node_t *body =
            vitte_c17_ast_child(emitter, space, 1u);
        const vitte_ast_node_t *nested = vitte_c17_ast_find_direct_space(
            emitter, body, path, index, index + 1u);
        if (nested == NULL) {
            return NULL;
        }
        space = nested;
    }
    return space;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_named_declaration_in_scope(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    const vitte_ast_node_t *name,
    vitte_ast_node_kind_t first_kind,
    vitte_ast_node_kind_t second_kind)
{
    size_t index;

    if (scope == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);
        if (declaration != NULL &&
            (declaration->kind == first_kind ||
             declaration->kind == second_kind) &&
            declaration->child_count != 0u &&
            vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, declaration, 0u),
                name)) {
            return declaration;
        }
    }
    return NULL;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_imported_declaration_in_scope(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    const vitte_ast_node_t *name,
    vitte_ast_node_kind_t first_kind,
    vitte_ast_node_kind_t second_kind,
    bool *ambiguous)
{
    const vitte_ast_node_t *root;
    const vitte_ast_node_t *found = NULL;
    size_t index;

    if (scope == NULL || name == NULL || ambiguous == NULL) {
        return NULL;
    }
    root = vitte_c17_ast_node(
        emitter, vitte_parser_root(emitter->parser));
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *use =
            vitte_c17_ast_child(emitter, scope, index);
        const vitte_ast_node_t *base;
        const vitte_ast_node_t *candidate = NULL;
        const vitte_ast_node_t *module;

        if (use == NULL || use->kind != VITTE_AST_NODE_USE_DECL ||
            use->child_count == 0u) {
            continue;
        }
        base = vitte_c17_ast_child(emitter, use, 0u);
        if (base == NULL || base->kind != VITTE_AST_NODE_PATH ||
            base->child_count == 0u) {
            continue;
        }
        if ((use->flags & VITTE_AST_FLAG_GLOB_IMPORT) != 0u) {
            module = vitte_c17_ast_find_space_for_path(
                emitter, root, base, base->child_count, 0u, 0u);
            if (module != NULL) {
                candidate = vitte_c17_ast_find_named_declaration_in_scope(
                    emitter,
                    vitte_c17_ast_child(emitter, module, 1u),
                    name,
                    first_kind,
                    second_kind);
            }
        } else if (use->child_count == 1u ||
                   (use->child_count == 2u &&
                    vitte_c17_ast_child(emitter, use, 1u)->kind ==
                        VITTE_AST_NODE_IDENTIFIER)) {
            const vitte_ast_node_t *import_name =
                use->child_count == 2u
                    ? vitte_c17_ast_child(emitter, use, 1u)
                    : vitte_c17_ast_child(
                          emitter, base, base->child_count - 1u);
            if (vitte_c17_ast_token_equal(emitter, import_name, name) &&
                vitte_c17_ast_find_space_for_path(
                    emitter, root, base, base->child_count, 0u, 0u) == NULL) {
                module = base->child_count == 1u
                    ? NULL
                    : vitte_c17_ast_find_space_for_path(
                          emitter,
                          root,
                          base,
                          base->child_count - 1u,
                          0u,
                          0u);
                candidate = vitte_c17_ast_find_named_declaration_in_scope(
                    emitter,
                    module == NULL && base->child_count == 1u
                        ? root
                        : vitte_c17_ast_child(emitter, module, 1u),
                    vitte_c17_ast_child(
                        emitter, base, base->child_count - 1u),
                    first_kind,
                    second_kind);
            }
        } else {
            size_t item_index;

            module = vitte_c17_ast_find_space_for_path(
                emitter, root, base, base->child_count, 0u, 0u);
            for (item_index = 1u;
                 module != NULL && item_index < use->child_count;
                 ++item_index) {
                const vitte_ast_node_t *item =
                    vitte_c17_ast_child(emitter, use, item_index);
                const vitte_ast_node_t *next =
                    item_index + 1u < use->child_count
                        ? vitte_c17_ast_child(
                              emitter, use, item_index + 1u)
                        : NULL;
                const vitte_ast_node_t *import_name;
                const vitte_ast_node_t *item_module;
                size_t consumed;

                if (item == NULL || item->kind != VITTE_AST_NODE_PATH ||
                    item->child_count == 0u) {
                    continue;
                }
                import_name = next != NULL &&
                    next->kind == VITTE_AST_NODE_IDENTIFIER
                        ? next
                        : vitte_c17_ast_child(
                              emitter, item, item->child_count - 1u);
                if (next != NULL &&
                    next->kind == VITTE_AST_NODE_IDENTIFIER) {
                    ++item_index;
                }
                if (!vitte_c17_ast_token_equal(
                        emitter, import_name, name)) {
                    continue;
                }
                item_module = module;
                consumed = 0u;
                while (item_module != NULL &&
                       consumed + 1u < item->child_count) {
                    const vitte_ast_node_t *nested =
                        vitte_c17_ast_find_direct_space(
                            emitter,
                            vitte_c17_ast_child(
                                emitter, item_module, 1u),
                            item,
                            consumed,
                            item->child_count - 1u);
                    const vitte_ast_node_t *nested_path =
                        vitte_c17_ast_child(emitter, nested, 0u);
                    if (nested == NULL || nested_path == NULL ||
                        nested_path->kind != VITTE_AST_NODE_PATH ||
                        nested_path->child_count == 0u) {
                        item_module = NULL;
                        break;
                    }
                    consumed += nested_path->child_count;
                    item_module = nested;
                }
                if (item_module != NULL) {
                    candidate =
                        vitte_c17_ast_find_named_declaration_in_scope(
                            emitter,
                            vitte_c17_ast_child(
                                emitter, item_module, 1u),
                            vitte_c17_ast_child(
                                emitter, item, item->child_count - 1u),
                            first_kind,
                            second_kind);
                }
                if (candidate != NULL) {
                    break;
                }
            }
        }
        if (candidate != NULL) {
            if (found != NULL && found != candidate) {
                *ambiguous = true;
                return NULL;
            }
            found = candidate;
        }
    }
    return found;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_current_imported_declaration(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name,
    vitte_ast_node_kind_t first_kind,
    vitte_ast_node_kind_t second_kind,
    bool *ambiguous)
{
    const vitte_ast_node_t *root;
    size_t index;

    for (index = emitter->space_depth; index != 0u; --index) {
        const vitte_ast_node_t *found =
            vitte_c17_ast_find_imported_declaration_in_scope(
                emitter,
                vitte_c17_ast_child(
                    emitter, emitter->space_stack[index - 1u], 1u),
                name,
                first_kind,
                second_kind,
                ambiguous);
        if (found != NULL || *ambiguous) {
            return found;
        }
    }
    root = vitte_c17_ast_node(
        emitter, vitte_parser_root(emitter->parser));
    return vitte_c17_ast_find_imported_declaration_in_scope(
        emitter,
        root,
        name,
        first_kind,
        second_kind,
        ambiguous);
}

static const vitte_ast_node_t *
vitte_c17_ast_find_unique_named_declaration(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    const vitte_ast_node_t *name,
    vitte_ast_node_kind_t first_kind,
    vitte_ast_node_kind_t second_kind,
    size_t *matches,
    size_t depth)
{
    const vitte_ast_node_t *found = NULL;
    size_t index;

    if (scope == NULL || name == NULL || matches == NULL || depth > 128u) {
        return NULL;
    }
    for (index = 0u; index < scope->child_count && *matches < 2u; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);

        if (declaration == NULL) {
            continue;
        }
        if ((declaration->kind == first_kind ||
             declaration->kind == second_kind) &&
            declaration->child_count != 0u &&
            vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, declaration, 0u),
                name)) {
            ++*matches;
            found = declaration;
            continue;
        }
        if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
            declaration->child_count > 1u) {
            const vitte_ast_node_t *nested =
                vitte_c17_ast_find_unique_named_declaration(
                    emitter,
                    vitte_c17_ast_child(emitter, declaration, 1u),
                    name,
                    first_kind,
                    second_kind,
                    matches,
                    depth + 1u);
            if (nested != NULL) {
                found = nested;
            }
        }
    }
    return found;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_path_declaration(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *path,
    vitte_ast_node_kind_t first_kind,
    vitte_ast_node_kind_t second_kind)
{
    const vitte_ast_node_t *name;
    const vitte_ast_node_t *root;
    const vitte_ast_node_t *scope;
    size_t module_path_count;
    size_t index;

    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count == 0u) {
        return NULL;
    }
    name = vitte_c17_ast_child(emitter, path, path->child_count - 1u);
    root = vitte_c17_ast_node(
        emitter, vitte_parser_root(emitter->parser));
    if (path->child_count > 1u) {
        module_path_count = path->child_count - 1u;
        scope = vitte_c17_ast_find_module_for_path(
            emitter, path, module_path_count);
        if (scope == NULL) {
            return NULL;
        }
        return vitte_c17_ast_find_named_declaration_in_scope(
            emitter,
            vitte_c17_ast_child(emitter, scope, 1u),
            name,
            first_kind,
            second_kind);
    }
    for (index = emitter->space_depth; index != 0u; --index) {
        const vitte_ast_node_t *space = emitter->space_stack[index - 1u];
        const vitte_ast_node_t *found =
            vitte_c17_ast_find_named_declaration_in_scope(
                emitter,
                vitte_c17_ast_child(emitter, space, 1u),
                name,
                first_kind,
                second_kind);
        if (found != NULL) {
            return found;
        }
    }
    {
        const vitte_ast_node_t *found =
            vitte_c17_ast_find_named_declaration_in_scope(
                emitter, root, name, first_kind, second_kind);
        bool ambiguous = false;
        size_t matches = 0u;

        if (found != NULL) {
            return found;
        }
        found = vitte_c17_ast_find_current_imported_declaration(
            emitter,
            name,
            first_kind,
            second_kind,
            &ambiguous);
        if (found != NULL || ambiguous) {
            return found;
        }
        found = vitte_c17_ast_find_unique_named_declaration(
            emitter,
            root,
            name,
            first_kind,
            second_kind,
            &matches,
            0u);
        return matches == 1u ? found : NULL;
    }
}

static bool
vitte_c17_ast_find_declaration_scope(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *declaration,
    const vitte_ast_node_t **spaces,
    size_t space_depth,
    const vitte_ast_node_t **declaration_spaces,
    size_t *declaration_space_depth)
{
    const vitte_ast_node_t *current;
    const vitte_ast_node_t *reverse_spaces[128];
    size_t reverse_depth = 0u;
    size_t index;

    (void)spaces;
    (void)space_depth;
    if (node == NULL || declaration == NULL || declaration_spaces == NULL ||
        declaration_space_depth == NULL || emitter->parent_ids == NULL) {
        return false;
    }
    current = declaration;
    while (current != NULL && current != node) {
        if (current->kind == VITTE_AST_NODE_SPACE_DECL) {
            if (reverse_depth >=
                sizeof(reverse_spaces) / sizeof(reverse_spaces[0])) {
                return false;
            }
            reverse_spaces[reverse_depth++] = current;
        }
        if (current->id >= emitter->parent_id_count) {
            return false;
        }
        current = vitte_c17_ast_node(
            emitter,
            emitter->parent_ids[(size_t)current->id]);
    }
    if (current != node) {
        return false;
    }
    for (index = 0u; index < reverse_depth; ++index) {
        declaration_spaces[index] = reverse_spaces[reverse_depth - index - 1u];
    }
    *declaration_space_depth = reverse_depth;
    return true;
}

static void
vitte_c17_ast_emit_declaration_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *declaration,
    const vitte_ast_node_t *name)
{
    const vitte_ast_node_t *root;
    const vitte_ast_node_t *declaration_spaces[128];
    size_t declaration_space_depth = 0u;
    size_t index;

    if (name == NULL) {
        emitter->failed = true;
        return;
    }
    root = vitte_c17_ast_node(
        emitter, vitte_parser_root(emitter->parser));
    if (declaration == NULL) {
        vitte_c17_ast_write_token(emitter, name);
        return;
    }
    if (!vitte_c17_ast_find_declaration_scope(
            emitter,
            root,
            declaration,
            NULL,
            0u,
            declaration_spaces,
            &declaration_space_depth)) {
        vitte_c17_ast_write_token(emitter, name);
        return;
    }
    for (index = 0u; index < declaration_space_depth; ++index) {
        const vitte_ast_node_t *space_path =
            vitte_c17_ast_child(
                emitter, declaration_spaces[index], 0u);
        if (space_path == NULL ||
            space_path->kind != VITTE_AST_NODE_PATH ||
            space_path->child_count == 0u) {
            emitter->failed = true;
            return;
        }
        if (index != 0u) {
            vitte_c17_ast_write(emitter, "_");
        }
        vitte_c17_ast_emit_path(emitter, space_path);
    }
    if (declaration_space_depth != 0u) {
        vitte_c17_ast_write(emitter, "_");
    }
    vitte_c17_ast_write_token(emitter, name);
}

static bool
vitte_c17_ast_emit_resolved_procedure_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *path)
{
    const vitte_ast_node_t *procedure;
    const vitte_ast_node_t *name;

    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count == 0u) {
        return false;
    }
    procedure = vitte_c17_ast_find_path_declaration(
        emitter,
        path,
        VITTE_AST_NODE_PROC_DECL,
        VITTE_AST_NODE_EXTERN_PROC_DECL);
    if (procedure == NULL) {
        return false;
    }
    name = vitte_c17_ast_child(emitter, procedure, 0u);
    if (procedure->kind == VITTE_AST_NODE_EXTERN_PROC_DECL ||
        vitte_c17_ast_is_intrinsic(emitter, procedure)) {
        vitte_c17_ast_write_token(emitter, name);
        return true;
    }
    vitte_c17_ast_emit_declaration_name(emitter, procedure, name);
    return true;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_pick_variant(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pick,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (pick == NULL || name == NULL ||
        pick->kind != VITTE_AST_NODE_PICK_DECL) {
        return NULL;
    }
    for (index = 1u; index < pick->child_count; ++index) {
        const vitte_ast_node_t *variant =
            vitte_c17_ast_child(emitter, pick, index);
        if (variant != NULL &&
            variant->kind == VITTE_AST_NODE_VARIANT_DECL &&
            variant->child_count != 0u &&
            vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, variant, 0u),
                name)) {
            return variant;
        }
    }
    return NULL;
}

static bool
vitte_c17_ast_pick_has_payload(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pick)
{
    size_t index;

    if (pick == NULL || pick->kind != VITTE_AST_NODE_PICK_DECL) {
        return false;
    }
    for (index = 1u; index < pick->child_count; ++index) {
        const vitte_ast_node_t *variant =
            vitte_c17_ast_child(emitter, pick, index);
        if (variant != NULL &&
            variant->kind == VITTE_AST_NODE_VARIANT_DECL &&
            variant->child_count > 1u) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_resolve_pick_variant_path(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *path,
    const vitte_ast_node_t **pick_out,
    const vitte_ast_node_t **variant_out)
{
    const vitte_ast_node_t *pick_name;
    const vitte_ast_node_t *variant_name;
    const vitte_ast_node_t *pick;
    const vitte_ast_node_t *variant;

    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count < 2u) {
        return false;
    }
    pick_name = vitte_c17_ast_child(
        emitter, path, path->child_count - 2u);
    variant_name = vitte_c17_ast_child(
        emitter, path, path->child_count - 1u);
    if (path->child_count == 2u) {
        size_t index;
        const vitte_ast_node_t *root = vitte_c17_ast_node(
            emitter, vitte_parser_root(emitter->parser));
        pick = NULL;
        for (index = emitter->space_depth; index != 0u; --index) {
            pick = vitte_c17_ast_find_named_declaration_in_scope(
                emitter,
                vitte_c17_ast_child(
                    emitter, emitter->space_stack[index - 1u], 1u),
                pick_name,
                VITTE_AST_NODE_PICK_DECL,
                VITTE_AST_NODE_PICK_DECL);
            if (pick != NULL) {
                break;
            }
        }
        if (pick == NULL) {
            pick = vitte_c17_ast_find_named_declaration_in_scope(
                emitter,
                root,
                pick_name,
                VITTE_AST_NODE_PICK_DECL,
                VITTE_AST_NODE_PICK_DECL);
        }
        if (pick == NULL) {
            bool ambiguous = false;
            pick = vitte_c17_ast_find_current_imported_declaration(
                emitter,
                pick_name,
                VITTE_AST_NODE_PICK_DECL,
                VITTE_AST_NODE_PICK_DECL,
                &ambiguous);
            if (ambiguous) {
                return false;
            }
        }
        if (pick == NULL) {
            size_t matches = 0u;
            pick = vitte_c17_ast_find_unique_named_declaration(
                emitter,
                root,
                pick_name,
                VITTE_AST_NODE_PICK_DECL,
                VITTE_AST_NODE_PICK_DECL,
                &matches,
                0u);
            if (matches != 1u) {
                pick = NULL;
            }
        }
    } else {
        const vitte_ast_node_t *scope =
            vitte_c17_ast_find_module_for_path(
                emitter, path, path->child_count - 2u);
        pick = scope == NULL
            ? NULL
            : vitte_c17_ast_find_named_declaration_in_scope(
                  emitter,
                  vitte_c17_ast_child(emitter, scope, 1u),
                  pick_name,
                  VITTE_AST_NODE_PICK_DECL,
                  VITTE_AST_NODE_PICK_DECL);
    }
    variant = vitte_c17_ast_find_pick_variant(
        emitter, pick, variant_name);
    if (pick == NULL || variant == NULL) {
        return false;
    }
    if (pick_out != NULL) {
        *pick_out = pick;
    }
    if (variant_out != NULL) {
        *variant_out = variant;
    }
    return true;
}

static bool
vitte_c17_ast_resolve_generic_pick_variant_member(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *member,
    const vitte_ast_node_t **pick_out,
    const vitte_ast_node_t **variant_out,
    const vitte_c17_ast_generic_form_instance_t **instance_out)
{
    const vitte_ast_node_t *type;
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *pick;
    const vitte_ast_node_t *variant_name;
    const vitte_ast_node_t *variant;
    const vitte_c17_ast_generic_form_instance_t *instance;

    if (member == NULL || member->kind != VITTE_AST_NODE_MEMBER_EXPR ||
        member->child_count != 2u) {
        return false;
    }
    type = vitte_c17_ast_child(emitter, member, 0u);
    variant_name = vitte_c17_ast_child(emitter, member, 1u);
    if (type == NULL || type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count < 2u) {
        return false;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    pick = vitte_c17_ast_find_path_declaration(
        emitter,
        path,
        VITTE_AST_NODE_PICK_DECL,
        VITTE_AST_NODE_PICK_DECL);
    if (pick == NULL ||
        !vitte_c17_ast_pick_has_generic_parameters(emitter, pick)) {
        return false;
    }
    variant = vitte_c17_ast_find_pick_variant(
        emitter, pick, variant_name);
    instance = vitte_c17_ast_generic_form_instance_for_type(
        emitter, type);
    if (variant == NULL || instance == NULL) {
        emitter->failed = true;
    }
    if (pick_out != NULL) {
        *pick_out = pick;
    }
    if (variant_out != NULL) {
        *variant_out = variant;
    }
    if (instance_out != NULL) {
        *instance_out = instance;
    }
    return true;
}

static const vitte_ast_node_t *
vitte_c17_ast_pick_decl_for_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type)
{
    const vitte_ast_node_t *path;

    if (type == NULL) {
        return NULL;
    }
    if (type->kind == VITTE_AST_NODE_PICK_DECL) {
        return type;
    }
    if (type->kind == VITTE_AST_NODE_PATH) {
        path = type;
    } else if (type->kind == VITTE_AST_NODE_TYPE_EXPR &&
               type->child_count != 0u) {
        path = vitte_c17_ast_child(emitter, type, 0u);
    } else {
        return NULL;
    }
    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count == 0u) {
        return NULL;
    }
    return vitte_c17_ast_find_path_declaration(
        emitter,
        path,
        VITTE_AST_NODE_PICK_DECL,
        VITTE_AST_NODE_PICK_DECL);
}

static const vitte_ast_node_t *
vitte_c17_ast_match_pick_decl_from_arms(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *match)
{
    size_t index;

    if (match == NULL || match->kind != VITTE_AST_NODE_MATCH_STMT) {
        return NULL;
    }
    for (index = 1u; index < match->child_count; ++index) {
        const vitte_ast_node_t *arm =
            vitte_c17_ast_child(emitter, match, index);
        const vitte_ast_node_t *pattern;
        const vitte_ast_node_t *path;
        const vitte_ast_node_t *pick;
        const vitte_ast_node_t *variant;

        if (arm == NULL || arm->kind != VITTE_AST_NODE_MATCH_ARM ||
            arm->child_count < 2u) {
            continue;
        }
        pattern = vitte_c17_ast_child(emitter, arm, 0u);
        path = pattern;
        if (pattern != NULL && pattern->kind == VITTE_AST_NODE_PATTERN &&
            (pattern->flags & VITTE_AST_FLAG_PATTERN_HEAD) != 0u &&
            pattern->child_count != 0u) {
            path = vitte_c17_ast_child(emitter, pattern, 0u);
        }
        if (path != NULL && path->kind == VITTE_AST_NODE_PATH &&
            vitte_c17_ast_resolve_pick_variant_path(
                emitter, path, &pick, &variant)) {
            return pick;
        }
    }
    return NULL;
}

static void
vitte_c17_ast_emit_pick_unit_variant(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pick,
    const vitte_ast_node_t *variant)
{
    const vitte_ast_node_t *pick_name;
    const vitte_ast_node_t *variant_name;

    if (pick == NULL || variant == NULL ||
        pick->child_count == 0u || variant->child_count == 0u) {
        emitter->failed = true;
        return;
    }
    pick_name = vitte_c17_ast_child(emitter, pick, 0u);
    variant_name = vitte_c17_ast_child(emitter, variant, 0u);
    if (!vitte_c17_ast_pick_has_payload(emitter, pick)) {
        vitte_c17_ast_emit_declaration_name(
            emitter, pick, pick_name);
        vitte_c17_ast_write(emitter, "_");
        vitte_c17_ast_write_token(emitter, variant_name);
        return;
    }
    if (variant->child_count != 1u) {
        emitter->failed = true;
        return;
    }
    vitte_c17_ast_write(emitter, "((");
    vitte_c17_ast_emit_declaration_name(emitter, pick, pick_name);
    vitte_c17_ast_write(emitter, "){ .tag = ");
    vitte_c17_ast_emit_declaration_name(emitter, pick, pick_name);
    vitte_c17_ast_write(emitter, "_tag_");
    vitte_c17_ast_write_token(emitter, variant_name);
    vitte_c17_ast_write(emitter, " })");
}

static bool
vitte_c17_ast_emit_pick_variant_value(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *path)
{
    const vitte_ast_node_t *pick;
    const vitte_ast_node_t *variant;

    if (!vitte_c17_ast_resolve_pick_variant_path(
            emitter, path, &pick, &variant)) {
        return false;
    }
    if (vitte_c17_ast_pick_has_payload(emitter, pick)) {
        vitte_c17_ast_emit_pick_unit_variant(emitter, pick, variant);
        return true;
    }
    vitte_c17_ast_emit_pick_unit_variant(emitter, pick, variant);
    return true;
}

static void
vitte_c17_ast_write_payload_field_name(
    vitte_c17_ast_emitter_t *emitter,
    size_t index)
{
    char name[32];
    int length = snprintf(name, sizeof(name), "field_%zu", index);

    if (length < 0 || (size_t)length >= sizeof(name)) {
        emitter->failed = true;
        return;
    }
    vitte_c17_ast_write(emitter, name);
}

static bool
vitte_c17_ast_emit_pick_variant_constructor(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *call)
{
    const vitte_ast_node_t *callee;
    const vitte_ast_node_t *pick;
    const vitte_ast_node_t *variant;
    const vitte_ast_node_t *pick_name;
    const vitte_ast_node_t *variant_name;
    const vitte_c17_ast_generic_form_instance_t *generic_instance = NULL;
    size_t payload_count;
    size_t argument_count;
    size_t index;

    if (call == NULL || call->kind != VITTE_AST_NODE_CALL_EXPR ||
        call->child_count == 0u) {
        return false;
    }
    callee = vitte_c17_ast_child(emitter, call, 0u);
    if (vitte_c17_ast_resolve_generic_pick_variant_member(
            emitter,
            callee,
            &pick,
            &variant,
            &generic_instance)) {
        size_t generic_payload_count;
        size_t generic_argument_count = call->child_count - 1u;
        if (emitter->failed || generic_instance == NULL) {
            return true;
        }
        generic_payload_count = variant->child_count - 1u;
        if (generic_payload_count != generic_argument_count) {
            emitter->failed = true;
            return true;
        }
        if (!vitte_c17_ast_pick_has_payload(emitter, pick)) {
            if (generic_payload_count != 0u) {
                emitter->failed = true;
                return true;
            }
            vitte_c17_ast_emit_generic_form_instance_name(
                emitter, generic_instance);
            vitte_c17_ast_write(emitter, "_");
            vitte_c17_ast_write_token(
                emitter, vitte_c17_ast_child(emitter, variant, 0u));
            return true;
        }
        vitte_c17_ast_write(emitter, "((");
        vitte_c17_ast_emit_generic_form_instance_name(
            emitter, generic_instance);
        vitte_c17_ast_write(emitter, "){ .tag = ");
        vitte_c17_ast_emit_generic_form_instance_name(
            emitter, generic_instance);
        vitte_c17_ast_write(emitter, "_tag_");
        vitte_c17_ast_write_token(
            emitter, vitte_c17_ast_child(emitter, variant, 0u));
        if (generic_payload_count != 0u) {
            vitte_c17_ast_write(emitter, ", .payload.");
            vitte_c17_ast_write_token(
                emitter, vitte_c17_ast_child(emitter, variant, 0u));
            vitte_c17_ast_write(emitter, " = { ");
            for (index = 0u; index < generic_payload_count; ++index) {
                if (index != 0u) {
                    vitte_c17_ast_write(emitter, ", ");
                }
                vitte_c17_ast_write(emitter, ".");
                vitte_c17_ast_write_payload_field_name(emitter, index);
                vitte_c17_ast_write(emitter, " = ");
                vitte_c17_ast_emit_expression(
                    emitter,
                    vitte_c17_ast_child(emitter, call, index + 1u));
            }
            vitte_c17_ast_write(emitter, " }");
        }
        vitte_c17_ast_write(emitter, " })");
        return true;
    }
    if (!vitte_c17_ast_resolve_pick_variant_path(
            emitter, callee, &pick, &variant)) {
        return false;
    }
    payload_count = variant->child_count - 1u;
    argument_count = call->child_count - 1u;
    pick_name = vitte_c17_ast_child(emitter, pick, 0u);
    variant_name = vitte_c17_ast_child(emitter, variant, 0u);

    if (!vitte_c17_ast_pick_has_payload(emitter, pick)) {
        if (payload_count != 0u || argument_count != 0u) {
            emitter->failed = true;
            return true;
        }
        vitte_c17_ast_emit_pick_unit_variant(emitter, pick, variant);
        return true;
    }
    if (payload_count != argument_count) {
        emitter->failed = true;
        return true;
    }

    vitte_c17_ast_write(emitter, "((");
    vitte_c17_ast_emit_declaration_name(emitter, pick, pick_name);
    vitte_c17_ast_write(emitter, "){ .tag = ");
    vitte_c17_ast_emit_declaration_name(emitter, pick, pick_name);
    vitte_c17_ast_write(emitter, "_tag_");
    vitte_c17_ast_write_token(emitter, variant_name);
    if (payload_count != 0u) {
        vitte_c17_ast_write(emitter, ", .payload.");
        vitte_c17_ast_write_token(emitter, variant_name);
        vitte_c17_ast_write(emitter, " = { ");
        for (index = 0u; index < payload_count; ++index) {
            if (index != 0u) {
                vitte_c17_ast_write(emitter, ", ");
            }
            vitte_c17_ast_write(emitter, ".");
            vitte_c17_ast_write_payload_field_name(emitter, index);
            vitte_c17_ast_write(emitter, " = ");
            vitte_c17_ast_emit_expression(
                emitter,
                vitte_c17_ast_child(emitter, call, index + 1u));
        }
        vitte_c17_ast_write(emitter, " }");
    }
    vitte_c17_ast_write(emitter, " })");
    return true;
}

static bool
vitte_c17_ast_is_pick_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == VITTE_AST_NODE_IDENTIFIER) {
        const vitte_ast_node_t *root = vitte_c17_ast_node(
            emitter, vitte_parser_root(emitter->parser));
        return vitte_c17_ast_find_named_declaration_in_scope(
            emitter,
            root,
            expression,
            VITTE_AST_NODE_PICK_DECL,
            VITTE_AST_NODE_PICK_DECL) != NULL;
    } else if (expression->kind == VITTE_AST_NODE_PATH &&
               expression->child_count != 0u) {
        return vitte_c17_ast_find_path_declaration(
            emitter,
            expression,
            VITTE_AST_NODE_PICK_DECL,
            VITTE_AST_NODE_PICK_DECL) != NULL;
    } else {
        return false;
    }
}

static const vitte_ast_node_t *
vitte_c17_ast_call_return_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *call)
{
    const vitte_ast_node_t *callee;
    const vitte_ast_node_t *callee_name;
    const vitte_ast_node_t *root;
    const vitte_ast_node_t *procedure;
    const vitte_ast_node_t *return_type;
    size_t generic_parameter_count;
    size_t index;
    size_t argument_index;

    if (call == NULL ||
        call->kind != VITTE_AST_NODE_CALL_EXPR ||
        call->child_count == 0u) {
        return NULL;
    }

    callee = vitte_c17_ast_child(emitter, call, 0u);
    if (callee == NULL) {
        return NULL;
    }
    if (callee->kind == VITTE_AST_NODE_PATH &&
        callee->child_count != 0u) {
        callee_name = vitte_c17_ast_child(
            emitter,
            callee,
            callee->child_count - 1u);
    } else if (callee->kind == VITTE_AST_NODE_IDENTIFIER) {
        callee_name = callee;
    } else {
        return NULL;
    }

    root = vitte_c17_ast_node(
        emitter,
        vitte_parser_root(emitter->parser));
    procedure = vitte_c17_ast_find_procedure_named(
        emitter,
        root,
        callee_name);
    if (procedure == NULL) {
        return NULL;
    }

    return_type = vitte_c17_ast_function_return_type(emitter, procedure);
    if (return_type == NULL) {
        return NULL;
    }
    generic_parameter_count =
        vitte_c17_ast_generic_form_parameter_count(
            emitter, procedure, NULL);
    if (generic_parameter_count == 0u) {
        return return_type;
    }
    argument_index = 1u;
    for (index = 1u; index < procedure->child_count; ++index) {
        const vitte_ast_node_t *child;

        child = vitte_c17_ast_child(emitter, procedure, index);
        if (child != NULL &&
            child->kind == VITTE_AST_NODE_PARAMETER) {
            const vitte_ast_node_t *parameter_type =
                vitte_c17_ast_child(emitter, child, 1u);
            if (vitte_c17_ast_type_syntax_equal(
                    emitter,
                    return_type,
                    parameter_type,
                    0u) &&
                argument_index < call->child_count) {
                const vitte_ast_node_t *actual_type =
                    vitte_c17_ast_expression_type(
                        emitter,
                        vitte_c17_ast_child(
                            emitter,
                            call,
                            argument_index));
                if (actual_type != NULL) {
                    return actual_type;
                }
            }
            ++argument_index;
            continue;
        }
        if (child != NULL &&
            (child->kind == VITTE_AST_NODE_TYPE_EXPR ||
             child->kind == VITTE_AST_NODE_ARRAY_TYPE ||
             child->kind == VITTE_AST_NODE_POINTER_TYPE ||
             child->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
             child->kind == VITTE_AST_NODE_OPTIONAL_TYPE)) {
            return return_type;
        }
    }

    return return_type;
}

static bool
vitte_c17_ast_type_is_named(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const char *name)
{
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *type_name;
    const vitte_token_t *token;

    while (type != NULL &&
           (type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
            type->kind == VITTE_AST_NODE_POINTER_TYPE)) {
        type = vitte_c17_ast_child(emitter, type, 0u);
    }
    if (type == NULL || type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count == 0u) {
        return false;
    }

    path = vitte_c17_ast_child(emitter, type, 0u);
    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count == 0u) {
        return false;
    }
    type_name = vitte_c17_ast_child(
        emitter,
        path,
        path->child_count - 1u);
    token = vitte_c17_ast_token(emitter, type_name);
    return token != NULL &&
           token->length == strlen(name) &&
           memcmp(token->lexeme, name, token->length) == 0;
}

static bool
vitte_c17_ast_is_character_parameter(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (emitter->function == NULL || name == NULL) {
        return false;
    }
    for (index = 1u; index < emitter->function->child_count; ++index) {
        const vitte_ast_node_t *parameter;

        parameter = vitte_c17_ast_child(emitter, emitter->function, index);
        if (parameter != NULL &&
            parameter->kind == VITTE_AST_NODE_PARAMETER &&
            vitte_c17_ast_token_equal(
                emitter,
                vitte_c17_ast_child(emitter, parameter, 0u),
                name)) {
            return vitte_c17_ast_type_is_named(
                emitter,
                vitte_c17_ast_child(emitter, parameter, 1u),
                "char");
        }
    }
    return false;
}

static bool
vitte_c17_ast_function_has_character_local(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (node == NULL || name == NULL) {
        return false;
    }
    if (node->kind == VITTE_AST_NODE_LET_STMT &&
        node->child_count > 1u &&
        vitte_c17_ast_token_equal(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u),
            name) &&
        vitte_c17_ast_type_is_named(
            emitter,
            vitte_c17_ast_child(emitter, node, 1u),
            "char")) {
        return true;
    }
    for (index = 0u; index < node->child_count; ++index) {
        if (vitte_c17_ast_function_has_character_local(
                emitter,
                vitte_c17_ast_child(emitter, node, index),
                name)) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_expression_is_character(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    const vitte_ast_node_t *name;

    if (expression == NULL) {
        return false;
    }
    if (expression->kind == VITTE_AST_NODE_CHARACTER_LITERAL) {
        return true;
    }
    if (expression->kind == VITTE_AST_NODE_GROUP_EXPR) {
        return vitte_c17_ast_expression_is_character(
            emitter,
            vitte_c17_ast_child(emitter, expression, 0u));
    }
    if (expression->kind == VITTE_AST_NODE_CAST_EXPR) {
        return vitte_c17_ast_type_is_named(
            emitter,
            vitte_c17_ast_expression_type(
                emitter,
                expression),
            "char");
    }
    if (expression->kind == VITTE_AST_NODE_INDEX_EXPR) {
        return vitte_c17_ast_expression_is_string(
            emitter,
            vitte_c17_ast_child(emitter, expression, 0u));
    }
    if (expression->kind == VITTE_AST_NODE_CALL_EXPR) {
        return vitte_c17_ast_type_is_named(
            emitter,
            vitte_c17_ast_call_return_type(emitter, expression),
            "char");
    }
    if (expression->kind != VITTE_AST_NODE_PATH &&
        expression->kind != VITTE_AST_NODE_IDENTIFIER) {
        return false;
    }
    name = expression->kind == VITTE_AST_NODE_IDENTIFIER
        ? expression
        : (expression->child_count == 1u
            ? vitte_c17_ast_child(emitter, expression, 0u)
            : NULL);
    return vitte_c17_ast_is_character_parameter(emitter, name) ||
           (emitter->function != NULL &&
            vitte_c17_ast_function_has_character_local(
                emitter,
                emitter->function,
                name));
}

static bool
vitte_c17_ast_emit_address_of_rvalue(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *operand)
{
    const vitte_ast_node_t *return_type;

    if (operand == NULL || operand->kind != VITTE_AST_NODE_CALL_EXPR) {
        return false;
    }

    return_type = vitte_c17_ast_call_return_type(emitter, operand);
    if (return_type == NULL) {
        return false;
    }

    /*
     * C does not permit `&function_returning_struct()`.  A one-element
     * compound array gives the temporary object a real lifetime for the
     * duration of the full expression while preserving the returned value.
     */
    vitte_c17_ast_write(emitter, "&((");
    vitte_c17_ast_emit_type(emitter, return_type);
    vitte_c17_ast_write(emitter, "[]){");
    vitte_c17_ast_emit_expression(emitter, operand);
    vitte_c17_ast_write(emitter, "})[0]");
    return !emitter->failed;
}

static void
vitte_c17_ast_emit_pointer_value(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    const vitte_ast_node_t *name;

    name = expression;
    if (expression != NULL && expression->kind == VITTE_AST_NODE_PATH &&
        expression->child_count == 1u) {
        name = vitte_c17_ast_child(emitter, expression, 0u);
    }
    if (name != NULL &&
        (expression->kind == VITTE_AST_NODE_PATH ||
         expression->kind == VITTE_AST_NODE_IDENTIFIER) &&
        vitte_c17_ast_is_reference_parameter(emitter, name)) {
        vitte_c17_ast_write_token(emitter, name);
        return;
    }
    vitte_c17_ast_emit_expression(emitter, expression);
}

static void
vitte_c17_ast_emit_form_array_initializer(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *array,
    const vitte_ast_node_t *array_type)
{
    const vitte_ast_node_t *element_type = NULL;
    size_t index;

    if (emitter == NULL || array == NULL ||
        array->kind != VITTE_AST_NODE_ARRAY_EXPR || array_type == NULL ||
        array_type->kind != VITTE_AST_NODE_ARRAY_TYPE) {
        if (emitter != NULL) {
            emitter->failed = true;
        }
        return;
    }

    if (array_type->child_count == 1u) {
        if (array->child_count == 0u) {
            vitte_c17_ast_write(emitter, "(VitteArray){0}");
            return;
        }
        element_type = vitte_c17_ast_expression_type(
            emitter,
            vitte_c17_ast_child(emitter, array, 0u));
        if (element_type == NULL) {
            element_type = vitte_c17_ast_child(emitter, array_type, 0u);
        }
        if (element_type == NULL) {
            emitter->failed = true;
            return;
        }
        vitte_c17_ast_write(emitter, "vitte_array_from((");
        vitte_c17_ast_emit_type(emitter, element_type);
        vitte_c17_ast_write(emitter, "[]){");
        for (index = 0u; index < array->child_count; ++index) {
            if (index != 0u) {
                vitte_c17_ast_write(emitter, ", ");
            }
            vitte_c17_ast_emit_expression(
                emitter,
                vitte_c17_ast_child(emitter, array, index));
        }
        (void)fprintf(
            emitter->output,
            "}, %zuu, sizeof(",
            array->child_count);
        vitte_c17_ast_emit_type(emitter, element_type);
        vitte_c17_ast_write(emitter, "))");
        return;
    }

    if (array_type->child_count != 2u) {
        emitter->failed = true;
        return;
    }
    vitte_c17_ast_write(emitter, "{");
    for (index = 0u; index < array->child_count; ++index) {
        if (index != 0u) {
            vitte_c17_ast_write(emitter, ", ");
        }
        vitte_c17_ast_emit_expression(
            emitter,
            vitte_c17_ast_child(emitter, array, index));
    }
    vitte_c17_ast_write(emitter, "}");
}

static bool
vitte_c17_ast_is_type_node(const vitte_ast_node_t *node)
{
    return node != NULL &&
        (node->kind == VITTE_AST_NODE_TYPE_EXPR ||
         node->kind == VITTE_AST_NODE_POINTER_TYPE ||
         node->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
         node->kind == VITTE_AST_NODE_ARRAY_TYPE ||
         node->kind == VITTE_AST_NODE_OPTIONAL_TYPE);
}

static bool
vitte_c17_ast_impl_target_matches_receiver(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *receiver,
    const vitte_ast_node_t *target)
{
    const vitte_ast_node_t *receiver_type;
    size_t substitution_index;

    receiver_type = vitte_c17_ast_expression_type(emitter, receiver);
    if (receiver_type == NULL || target == NULL) {
        return false;
    }
    substitution_index = vitte_c17_ast_generic_substitution_index(
        emitter, receiver_type);
    if (substitution_index != SIZE_MAX &&
        substitution_index < emitter->generic_substitution_count) {
        const vitte_ast_node_t *argument =
            emitter->generic_arguments[substitution_index];
        if (argument != NULL &&
            vitte_c17_ast_type_syntax_equal(
                emitter, target, argument, 0u)) {
            return true;
        }
        if (argument == NULL &&
            emitter->generic_c_type_names[substitution_index] != NULL) {
            char target_name[64];
            const char *source_name =
                vitte_c17_ast_source_scalar_name_from_c_type(
                    emitter->generic_c_type_names[substitution_index]);
            return source_name != NULL &&
                vitte_c17_ast_simple_type_name(
                    emitter, target, target_name, sizeof(target_name)) &&
                strcmp(source_name, target_name) == 0;
        }
        return false;
    }
    return vitte_c17_ast_type_syntax_equal(
        emitter, target, receiver_type, 0u);
}

static const vitte_ast_node_t *
vitte_c17_ast_resolve_impl_method(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *receiver,
    const vitte_ast_node_t *member,
    size_t *generic_instance_index)
{
    const vitte_ast_node_t *found = NULL;
    const vitte_ast_node_t *receiver_type;
    const vitte_ast_node_t *concrete_receiver_type;
    size_t node_index;

    if (emitter == NULL || receiver == NULL || member == NULL ||
        emitter->parser == NULL || generic_instance_index == NULL) {
        return NULL;
    }
    *generic_instance_index = SIZE_MAX;
    receiver_type = vitte_c17_ast_expression_type(emitter, receiver);
    if (receiver_type == NULL) {
        return NULL;
    }
    concrete_receiver_type = receiver_type;
    {
        size_t substitution_index =
            vitte_c17_ast_generic_substitution_index(
                emitter, receiver_type);
        if (substitution_index != SIZE_MAX &&
            emitter->generic_arguments[substitution_index] != NULL) {
            concrete_receiver_type =
                emitter->generic_arguments[substitution_index];
        }
    }
    for (node_index = 0u;
         node_index < emitter->parser->node_count;
         ++node_index) {
        const vitte_ast_node_t *implementation =
            &emitter->parser->nodes[node_index];
        const vitte_ast_node_t *target = NULL;
        size_t type_count = 0u;
        size_t child_index;
        size_t generic_count = 0u;
        size_t generic_index;

        if (implementation->kind != VITTE_AST_NODE_IMPL_DECL) {
            continue;
        }
        for (child_index = 0u;
             child_index < implementation->child_count;
             ++child_index) {
            const vitte_ast_node_t *child = vitte_c17_ast_child(
                emitter, implementation, child_index);
            if (child != NULL &&
                child->kind == VITTE_AST_NODE_GENERIC_PARAM) {
                ++generic_count;
            }
            if (vitte_c17_ast_is_type_node(child)) {
                ++type_count;
                if (type_count == 2u) {
                    target = child;
                    break;
                }
            }
        }
        if (type_count < 2u) {
            continue;
        }
        generic_index = SIZE_MAX;
        if (generic_count != 0u) {
            size_t instance_index;
            for (instance_index = 0u;
                 instance_index < emitter->generic_form_count;
                 ++instance_index) {
                const vitte_c17_ast_generic_form_instance_t *instance =
                    &emitter->generic_forms[instance_index];
                const vitte_ast_node_t *bindings[128];
                size_t binding_count = 0u;

                if (instance->type_expression != NULL &&
                    vitte_c17_ast_type_syntax_equal(
                        emitter,
                        instance->type_expression,
                        concrete_receiver_type,
                        0u) &&
                    vitte_c17_ast_generic_impl_bindings(
                        emitter,
                        implementation,
                        target,
                        concrete_receiver_type,
                        bindings,
                        sizeof(bindings) / sizeof(bindings[0]),
                        &binding_count)) {
                    generic_index = instance_index;
                    break;
                }
            }
            if (generic_index == SIZE_MAX) {
                continue;
            }
        } else if (!vitte_c17_ast_impl_target_matches_receiver(
                       emitter, receiver, target)) {
            continue;
        }
        for (child_index = 0u;
             child_index < implementation->child_count;
             ++child_index) {
            const vitte_ast_node_t *method = vitte_c17_ast_child(
                emitter, implementation, child_index);
            if (method == NULL ||
                method->kind != VITTE_AST_NODE_PROC_DECL ||
                !vitte_c17_ast_token_equal(
                    emitter,
                    vitte_c17_ast_child(emitter, method, 0u),
                    member)) {
                continue;
            }
            if (found != NULL) {
                /* Multiple traits can define the same method for one
                 * concrete type. Without trait-directed selection, refusing
                 * an ambiguous backend dispatch is safer than choosing one. */
                return NULL;
            }
            found = method;
            *generic_instance_index = generic_index;
        }
    }
    return found;
}

static void
vitte_c17_ast_emit_impl_method_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *method,
    size_t generic_instance_index)
{
    if (method == NULL) {
        emitter->failed = true;
        return;
    }
    if (generic_instance_index == SIZE_MAX) {
        if (fprintf(
                emitter->output,
                "vitte_impl_method_%zu",
                (size_t)method->id) < 0) {
            emitter->failed = true;
        }
    } else if (fprintf(
                   emitter->output,
                   "vitte_impl_method_%zu_g%zu",
                   (size_t)method->id,
                   generic_instance_index) < 0) {
        emitter->failed = true;
    }
}

static void
vitte_c17_ast_emit_expression(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *left;
    const vitte_ast_node_t *right;
    size_t index;

    if (node == NULL || emitter->failed) {
        emitter->failed = true;
        return;
    }

    switch (node->kind) {
        case VITTE_AST_NODE_SIZEOF_EXPR:
            if (node->child_count != 1u) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_write(emitter, "sizeof(");
            if ((node->flags &
                 VITTE_AST_FLAG_TYPE_QUERY_TYPE_OPERAND) != 0u) {
                vitte_c17_ast_emit_type(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 0u));
            } else {
                const vitte_ast_node_t *operand =
                    vitte_c17_ast_child(emitter, node, 0u);
                bool visible_value = false;
                const vitte_ast_node_t *type_declaration = NULL;
                size_t parameter_index;

                if (operand != NULL &&
                    operand->kind == VITTE_AST_NODE_PATH &&
                    operand->child_count == 1u) {
                    const vitte_ast_node_t *name =
                        vitte_c17_ast_child(emitter, operand, 0u);
                    visible_value = vitte_c17_ast_is_visible_local(
                        emitter, operand, name);
                    for (parameter_index = 1u;
                         !visible_value && emitter->function != NULL &&
                         parameter_index < emitter->function->child_count;
                         ++parameter_index) {
                        const vitte_ast_node_t *parameter =
                            vitte_c17_ast_child(
                                emitter,
                                emitter->function,
                                parameter_index);
                        if (parameter != NULL &&
                            parameter->kind == VITTE_AST_NODE_PARAMETER &&
                            vitte_c17_ast_token_equal(
                                emitter,
                                vitte_c17_ast_child(
                                    emitter, parameter, 0u),
                                name)) {
                            visible_value = true;
                        }
                    }
                    if (!visible_value) {
                        type_declaration =
                            vitte_c17_ast_find_path_declaration(
                                emitter,
                                operand,
                                VITTE_AST_NODE_FORM_DECL,
                                VITTE_AST_NODE_PICK_DECL);
                        if (type_declaration == NULL) {
                            type_declaration =
                                vitte_c17_ast_find_path_declaration(
                                    emitter,
                                    operand,
                                    VITTE_AST_NODE_TYPE_DECL,
                                    VITTE_AST_NODE_OPAQUE_DECL);
                        }
                    }
                }
                if (type_declaration != NULL) {
                    vitte_c17_ast_emit_type(emitter, operand);
                } else {
                    vitte_c17_ast_emit_expression(emitter, operand);
                }
            }
            vitte_c17_ast_write(emitter, ")");
            return;

        case VITTE_AST_NODE_ALIGNOF_EXPR:
            if (node->child_count != 1u) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_write(emitter, "_Alignof(");
            vitte_c17_ast_emit_type(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            vitte_c17_ast_write(emitter, ")");
            return;

        case VITTE_AST_NODE_TRY_EXPR:
        {
            const vitte_ast_node_t *operand;
            const vitte_ast_node_t *operand_type;
            const vitte_ast_node_t *optional_inner;
            bool already_pointer_representation;

            if (node->child_count != 1u) {
                emitter->failed = true;
                return;
            }

            operand = vitte_c17_ast_child(emitter, node, 0u);
            operand_type = vitte_c17_ast_expression_type(
                emitter,
                operand);
            optional_inner = operand_type != NULL &&
                    operand_type->kind == VITTE_AST_NODE_OPTIONAL_TYPE
                ? vitte_c17_ast_child(emitter, operand_type, 0u)
                : NULL;
            already_pointer_representation = optional_inner != NULL &&
                (optional_inner->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                 optional_inner->kind == VITTE_AST_NODE_POINTER_TYPE ||
                 vitte_c17_ast_is_function_type_alias(
                     emitter,
                     optional_inner) ||
                 vitte_c17_ast_type_is_named(
                     emitter,
                     optional_inner,
                     "str") ||
                 vitte_c17_ast_type_is_named(
                     emitter,
                     optional_inner,
                     "string"));

            if (already_pointer_representation ||
                (operand_type != NULL &&
                 operand_type->kind == VITTE_AST_NODE_REFERENCE_TYPE) ||
                (operand_type == NULL &&
                 vitte_c17_ast_expression_is_pointer(emitter, operand))) {
                vitte_c17_ast_write(emitter, "((__typeof__(");
                vitte_c17_ast_emit_expression(emitter, operand);
                vitte_c17_ast_write(
                    emitter,
                    "))vitte_optional_require((const void *)(");
                vitte_c17_ast_emit_expression(emitter, operand);
                vitte_c17_ast_write(emitter, ")))");
                return;
            }

            vitte_c17_ast_write(emitter, "(*((__typeof__(* (");
            vitte_c17_ast_emit_expression(emitter, operand);
            vitte_c17_ast_write(
                emitter,
                ")) *)vitte_optional_require((const void *)(");
            vitte_c17_ast_emit_expression(emitter, operand);
            vitte_c17_ast_write(emitter, "))))");
            return;
        }

        case VITTE_AST_NODE_AWAIT_EXPR:
            if (node->child_count != 1u) {
                emitter->failed = true;
                return;
            }
            {
                const vitte_ast_node_t *awaited =
                    vitte_c17_ast_child(emitter, node, 0u);
                const vitte_ast_node_t *unwrapped =
                    vitte_c17_ast_unwrap_groups(emitter, awaited);
                const vitte_ast_node_t *origin =
                    vitte_c17_ast_async_task_origin(
                        emitter, awaited, 0u);
                if (origin == NULL) {
                    emitter->failed = true;
                    return;
                }
                if (unwrapped != NULL &&
                    unwrapped->kind == VITTE_AST_NODE_CALL_EXPR) {
                    vitte_c17_ast_write_async_task_function_name(
                        emitter, origin, "await_value");
                    vitte_c17_ast_write(emitter, "(");
                    vitte_c17_ast_emit_expression(emitter, awaited);
                    vitte_c17_ast_write(emitter, ")");
                } else {
                    vitte_c17_ast_write_async_task_function_name(
                        emitter, origin, "await");
                    vitte_c17_ast_write(emitter, "(&(");
                    vitte_c17_ast_emit_expression(emitter, awaited);
                    vitte_c17_ast_write(emitter, "))");
                }
            }
            return;

        case VITTE_AST_NODE_IF_EXPR:
            if (node->child_count != 3u) {
                emitter->failed = true;
                return;
            }
            {
                const vitte_ast_node_t *then_branch;
                const vitte_ast_node_t *else_branch;
                then_branch = vitte_c17_ast_child(emitter, node, 1u);
                else_branch = vitte_c17_ast_child(emitter, node, 2u);
                if (then_branch == NULL ||
                    then_branch->kind != VITTE_AST_NODE_BLOCK_EXPR ||
                    then_branch->child_count != 1u ||
                    else_branch == NULL ||
                    (else_branch->kind ==
                         VITTE_AST_NODE_BLOCK_EXPR &&
                     else_branch->child_count != 1u)) {
                    emitter->failed = true;
                    return;
                }
                vitte_c17_ast_write(emitter, "(");
                vitte_c17_ast_emit_condition(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 0u));
                vitte_c17_ast_write(emitter, " ? ");
                vitte_c17_ast_emit_expression(
                    emitter,
                    vitte_c17_ast_child(emitter, then_branch, 0u));
                vitte_c17_ast_write(emitter, " : ");
                if (else_branch->kind == VITTE_AST_NODE_BLOCK_EXPR) {
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, else_branch, 0u));
                } else {
                    vitte_c17_ast_emit_expression(emitter, else_branch);
                }
                vitte_c17_ast_write(emitter, ")");
            }
            return;

        case VITTE_AST_NODE_BLOCK_EXPR:
            if (node->child_count != 1u) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_emit_expression(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            return;

        case VITTE_AST_NODE_FORM_EXPR:
            if (node->child_count == 0u) {
                emitter->failed = true;
                return;
            }
            {
                const vitte_ast_node_t *form_declaration = NULL;
                const vitte_ast_node_t *form_name;

                left = vitte_c17_ast_child(emitter, node, 0u);
                if (left != NULL &&
                    left->kind == VITTE_AST_NODE_TYPE_EXPR) {
                    const vitte_c17_ast_generic_form_instance_t *instance =
                        vitte_c17_ast_generic_form_instance_for_type(
                            emitter, left);
                    const vitte_ast_node_t *path =
                        vitte_c17_ast_child(emitter, left, 0u);
                    form_name = path != NULL &&
                            path->kind == VITTE_AST_NODE_PATH &&
                            path->child_count != 0u
                        ? vitte_c17_ast_child(
                              emitter, path, path->child_count - 1u)
                        : NULL;
                    if (instance != NULL) {
                        form_declaration = instance->declaration;
                    } else if (form_name != NULL) {
                        const vitte_ast_node_t *root = vitte_c17_ast_node(
                            emitter,
                            vitte_parser_root(emitter->parser));
                        form_declaration = vitte_c17_ast_find_form(
                            emitter, root, form_name);
                    }
                } else {
                    form_name = NULL;
                }

            vitte_c17_ast_write(emitter, "(");
            left = vitte_c17_ast_child(emitter, node, 0u);
            if (left != NULL &&
                left->kind == VITTE_AST_NODE_TYPE_EXPR) {
                vitte_c17_ast_emit_type(emitter, left);
            } else if (left != NULL &&
                       left->kind == VITTE_AST_NODE_PATH &&
                       left->child_count != 0u) {
                const vitte_ast_node_t *form =
                    vitte_c17_ast_find_path_declaration(
                        emitter,
                        left,
                        VITTE_AST_NODE_FORM_DECL,
                        VITTE_AST_NODE_FORM_DECL);
                if (form != NULL) {
                    vitte_c17_ast_emit_declaration_name(
                        emitter,
                        form,
                        vitte_c17_ast_child(emitter, form, 0u));
                } else {
                    vitte_c17_ast_write_token(
                        emitter,
                        vitte_c17_ast_child(
                            emitter,
                            left,
                            left->child_count - 1u));
                }
            } else {
                emitter->failed = true;
            }
            vitte_c17_ast_write(emitter, "){ ");
            if (node->child_count == 1u) {
                vitte_c17_ast_write(emitter, "0 }");
                return;
            }
            for (index = 1u; index < node->child_count; ++index) {
                const vitte_ast_node_t *field;
                field = vitte_c17_ast_child(emitter, node, index);
                if (field == NULL ||
                    field->kind != VITTE_AST_NODE_FIELD_INIT ||
                    field->child_count != 2u) {
                    emitter->failed = true;
                    return;
                }
                if (index != 1u) {
                    vitte_c17_ast_write(emitter, ", ");
                }
                vitte_c17_ast_write(emitter, ".");
                vitte_c17_ast_write_token(
                    emitter,
                    vitte_c17_ast_child(emitter, field, 0u));
                vitte_c17_ast_write(emitter, " = ");
                {
                    const vitte_ast_node_t *field_name =
                        vitte_c17_ast_child(emitter, field, 0u);
                    const vitte_ast_node_t *field_value =
                        vitte_c17_ast_child(emitter, field, 1u);
                    const vitte_ast_node_t *field_type = NULL;
                    size_t declaration_index;

                    if (form_declaration != NULL && field_name != NULL) {
                        for (declaration_index = 1u;
                             declaration_index <
                                 form_declaration->child_count;
                             ++declaration_index) {
                            const vitte_ast_node_t *declared_field =
                                vitte_c17_ast_child(
                                    emitter,
                                    form_declaration,
                                    declaration_index);
                            if (declared_field != NULL &&
                                declared_field->kind ==
                                    VITTE_AST_NODE_FIELD_DECL &&
                                vitte_c17_ast_token_equal(
                                    emitter,
                                    vitte_c17_ast_child(
                                        emitter, declared_field, 0u),
                                    field_name)) {
                                field_type = vitte_c17_ast_child(
                                    emitter, declared_field, 1u);
                                break;
                            }
                        }
                    }
                    if (field_value != NULL &&
                        field_value->kind == VITTE_AST_NODE_ARRAY_EXPR &&
                        field_type != NULL &&
                        field_type->kind == VITTE_AST_NODE_ARRAY_TYPE) {
                        vitte_c17_ast_emit_form_array_initializer(
                            emitter,
                            field_value,
                            field_type);
                    } else {
                        vitte_c17_ast_emit_expression(
                            emitter,
                            field_value);
                    }
                }
            }
            vitte_c17_ast_write(emitter, " }");
            }
            return;

        case VITTE_AST_NODE_PATH:
            if (vitte_c17_ast_emit_pick_variant_value(emitter, node)) {
                return;
            }
            if (node->child_count == 1u &&
                vitte_c17_ast_is_local_space_function(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 0u))) {
                vitte_c17_ast_emit_function_name(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 0u));
            } else if (node->child_count == 1u) {
                left = vitte_c17_ast_child(emitter, node, 0u);
                if (vitte_c17_ast_is_reference_parameter(
                        emitter,
                        left)) {
                    vitte_c17_ast_write(emitter, "(*");
                    vitte_c17_ast_write_token(emitter, left);
                    vitte_c17_ast_write(emitter, ")");
                } else {
                    const vitte_ast_node_t *global;
                    bool local;
                    size_t parameter_index;

                    local = vitte_c17_ast_is_visible_local(
                        emitter, node, left);
                    for (parameter_index = 1u;
                         !local && emitter->function != NULL &&
                         parameter_index < emitter->function->child_count;
                         ++parameter_index) {
                        const vitte_ast_node_t *parameter =
                            vitte_c17_ast_child(
                                emitter,
                                emitter->function,
                                parameter_index);
                        if (parameter != NULL &&
                            parameter->kind == VITTE_AST_NODE_PARAMETER &&
                            vitte_c17_ast_token_equal(
                                emitter,
                                vitte_c17_ast_child(
                                    emitter, parameter, 0u),
                                left)) {
                            local = true;
                        }
                    }
                    global = local ? NULL :
                        vitte_c17_ast_find_path_declaration(
                            emitter,
                            node,
                            VITTE_AST_NODE_CONST_DECL,
                            VITTE_AST_NODE_STATIC_DECL);
                    if (global != NULL) {
                        vitte_c17_ast_emit_declaration_name(
                            emitter,
                            global,
                            vitte_c17_ast_child(emitter, global, 0u));
                    } else {
                        vitte_c17_ast_emit_path(emitter, node);
                    }
                }
            } else if (node->child_count > 1u) {
                const vitte_ast_node_t *declaration =
                    vitte_c17_ast_find_path_declaration(
                        emitter,
                        node,
                        VITTE_AST_NODE_CONST_DECL,
                        VITTE_AST_NODE_STATIC_DECL);
                if (declaration != NULL) {
                    vitte_c17_ast_emit_declaration_name(
                        emitter,
                        declaration,
                        vitte_c17_ast_child(emitter, declaration, 0u));
                } else if (!vitte_c17_ast_emit_resolved_procedure_name(
                               emitter,
                               node)) {
                    vitte_c17_ast_emit_path(emitter, node);
                }
            } else {
                emitter->failed = true;
            }
            return;

        case VITTE_AST_NODE_IDENTIFIER:
            if (vitte_c17_ast_is_reference_parameter(emitter, node)) {
                vitte_c17_ast_write(emitter, "(*");
                vitte_c17_ast_write_token(emitter, node);
                vitte_c17_ast_write(emitter, ")");
            } else {
                vitte_c17_ast_write_token(emitter, node);
            }
            return;

        case VITTE_AST_NODE_INTEGER_LITERAL:
            vitte_c17_ast_emit_integer_literal(emitter, node);
            return;

        case VITTE_AST_NODE_FLOAT_LITERAL:
        case VITTE_AST_NODE_STRING_LITERAL:
        case VITTE_AST_NODE_CHARACTER_LITERAL:
            vitte_c17_ast_write_token(emitter, node);
            return;

        case VITTE_AST_NODE_BOOL_LITERAL:
            vitte_c17_ast_write(
                emitter,
                node->token_index < emitter->parser->token_count &&
                    emitter->parser->tokens[node->token_index].kind ==
                        VITTE_TOKEN_KW_TRUE
                    ? "true"
                    : "false");
            return;

        case VITTE_AST_NODE_NULL_LITERAL:
            vitte_c17_ast_write(emitter, "NULL");
            return;

        case VITTE_AST_NODE_GROUP_EXPR:
            vitte_c17_ast_write(emitter, "(");
            vitte_c17_ast_emit_expression(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            vitte_c17_ast_write(emitter, ")");
            return;

        case VITTE_AST_NODE_ARRAY_EXPR:
            if (node->child_count == 0u) {
                vitte_c17_ast_write(
                    emitter,
                    "(VitteArray){0}");
                return;
            }
            vitte_c17_ast_write(emitter, "{");
            for (index = 0u; index < node->child_count; ++index) {
                if (index != 0u) {
                    vitte_c17_ast_write(emitter, ", ");
                }
                vitte_c17_ast_emit_expression(
                    emitter,
                    vitte_c17_ast_child(emitter, node, index));
            }
            vitte_c17_ast_write(emitter, "}");
            return;

        case VITTE_AST_NODE_UNARY_EXPR:
            if (node->operator_kind == VITTE_TOKEN_AMP &&
                vitte_c17_ast_emit_address_of_rvalue(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 0u))) {
                return;
            }
            if (node->operator_kind == VITTE_TOKEN_STAR) {
                const vitte_ast_node_t *operand;
                const vitte_ast_node_t *name;

                operand = vitte_c17_ast_child(emitter, node, 0u);
                name = operand;
                if (operand != NULL &&
                    operand->kind == VITTE_AST_NODE_PATH &&
                    operand->child_count == 1u) {
                    name = vitte_c17_ast_child(
                        emitter,
                        operand,
                        0u);
                }
                if (vitte_c17_ast_is_reference_parameter(
                        emitter,
                        name)) {
                    vitte_c17_ast_write(emitter, "(*");
                    vitte_c17_ast_write_token(emitter, name);
                    vitte_c17_ast_write(emitter, ")");
                    return;
                }
            }
            vitte_c17_ast_emit_operator(emitter, node->operator_kind);
            vitte_c17_ast_write(emitter, "(");
            vitte_c17_ast_emit_expression(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            vitte_c17_ast_write(emitter, ")");
            return;

        case VITTE_AST_NODE_BINARY_EXPR:
        case VITTE_AST_NODE_ASSIGN_EXPR:
            left = vitte_c17_ast_child(emitter, node, 0u);
            right = vitte_c17_ast_child(emitter, node, 1u);
            if (node->kind == VITTE_AST_NODE_BINARY_EXPR &&
                node->operator_kind == VITTE_TOKEN_QUESTION_QUESTION) {
                const vitte_ast_node_t *optional_type;
                const vitte_ast_node_t *payload_type;
                const vitte_ast_node_t *payload_name;
                const vitte_ast_node_t *payload_path;
                const vitte_ast_node_t *root;
                size_t alias_depth;
                bool pointer_payload = false;

                if (left != NULL &&
                    left->kind == VITTE_AST_NODE_NULL_LITERAL) {
                    vitte_c17_ast_emit_expression(emitter, right);
                    return;
                }
                optional_type =
                    vitte_c17_ast_expression_type(emitter, left);
                root = vitte_c17_ast_node(
                    emitter,
                    vitte_parser_root(emitter->parser));
                for (alias_depth = 0u;
                     optional_type != NULL &&
                     optional_type->kind == VITTE_AST_NODE_TYPE_EXPR &&
                     optional_type->child_count == 1u &&
                     alias_depth < 32u;
                     ++alias_depth) {
                    const vitte_ast_node_t *path =
                        vitte_c17_ast_child(emitter, optional_type, 0u);
                    const vitte_ast_node_t *name =
                        path != NULL && path->kind == VITTE_AST_NODE_PATH &&
                                path->child_count != 0u
                            ? vitte_c17_ast_child(
                                  emitter,
                                  path,
                                  path->child_count - 1u)
                            : path;
                    const vitte_ast_node_t *target =
                        vitte_c17_ast_find_type_alias_target(
                            emitter,
                            root,
                            name);

                    if (target == NULL || target == optional_type) {
                        break;
                    }
                    optional_type = target;
                }
                if (optional_type == NULL ||
                    optional_type->kind != VITTE_AST_NODE_OPTIONAL_TYPE ||
                    optional_type->child_count != 1u) {
                    emitter->failed = true;
                    return;
                }
                payload_type =
                    vitte_c17_ast_child(emitter, optional_type, 0u);
                for (alias_depth = 0u;
                     payload_type != NULL &&
                     payload_type->kind == VITTE_AST_NODE_TYPE_EXPR &&
                     payload_type->child_count == 1u &&
                     alias_depth < 32u;
                     ++alias_depth) {
                    payload_path =
                        vitte_c17_ast_child(emitter, payload_type, 0u);
                    payload_name =
                        payload_path != NULL &&
                                payload_path->kind == VITTE_AST_NODE_PATH &&
                                payload_path->child_count != 0u
                            ? vitte_c17_ast_child(
                                  emitter,
                                  payload_path,
                                  payload_path->child_count - 1u)
                            : payload_path;
                    {
                        const vitte_ast_node_t *target =
                            vitte_c17_ast_find_type_alias_target(
                                emitter,
                                root,
                                payload_name);
                        if (target == NULL || target == payload_type) {
                            break;
                        }
                        payload_type = target;
                    }
                }
                if (payload_type != NULL &&
                    (payload_type->kind == VITTE_AST_NODE_POINTER_TYPE ||
                     payload_type->kind == VITTE_AST_NODE_REFERENCE_TYPE)) {
                    pointer_payload = true;
                } else if (payload_type != NULL &&
                           payload_type->kind == VITTE_AST_NODE_TYPE_EXPR) {
                    payload_path =
                        vitte_c17_ast_child(emitter, payload_type, 0u);
                    payload_name =
                        payload_path != NULL &&
                                payload_path->kind == VITTE_AST_NODE_PATH &&
                                payload_path->child_count == 1u
                            ? vitte_c17_ast_child(emitter, payload_path, 0u)
                            : NULL;
                    pointer_payload =
                        vitte_c17_ast_token_is(
                            emitter,
                            payload_name,
                            "str") ||
                        vitte_c17_ast_token_is(
                            emitter,
                            payload_name,
                            "string");
                }
                vitte_c17_ast_write(
                    emitter,
                    "((vitte_internal_coalesce_");
                if (fprintf(
                        emitter->output,
                        "%llu",
                        (unsigned long long)node->id) < 0) {
                    emitter->failed = true;
                    return;
                }
                vitte_c17_ast_write(emitter, " = ");
                vitte_c17_ast_emit_expression(emitter, left);
                vitte_c17_ast_write(
                    emitter,
                    ") != NULL ? ");
                if (pointer_payload) {
                    vitte_c17_ast_write(emitter, "vitte_internal_coalesce_");
                } else {
                    vitte_c17_ast_write(emitter, "*vitte_internal_coalesce_");
                }
                if (fprintf(
                        emitter->output,
                        "%llu",
                        (unsigned long long)node->id) < 0) {
                    emitter->failed = true;
                    return;
                }
                vitte_c17_ast_write(emitter, " : (");
                vitte_c17_ast_emit_expression(emitter, right);
                vitte_c17_ast_write(emitter, "))");
                return;
            }
            if (node->kind == VITTE_AST_NODE_BINARY_EXPR &&
                (node->operator_kind == VITTE_TOKEN_EQUAL_EQUAL ||
                 node->operator_kind == VITTE_TOKEN_BANG_EQUAL) &&
                (left == NULL ||
                 left->kind != VITTE_AST_NODE_NULL_LITERAL) &&
                (right == NULL ||
                 right->kind != VITTE_AST_NODE_NULL_LITERAL) &&
                (vitte_c17_ast_expression_is_string(emitter, left) ||
                 vitte_c17_ast_expression_is_string(emitter, right))) {
                vitte_c17_ast_write(emitter, "(");
                if (node->operator_kind == VITTE_TOKEN_BANG_EQUAL) {
                    vitte_c17_ast_write(emitter, "(!");
                }
                vitte_c17_ast_write(emitter, "vitte_string_equal(");
                vitte_c17_ast_emit_expression(emitter, left);
                vitte_c17_ast_write(emitter, ", ");
                vitte_c17_ast_emit_expression(emitter, right);
                vitte_c17_ast_write(emitter, ")");
                if (node->operator_kind == VITTE_TOKEN_BANG_EQUAL) {
                    vitte_c17_ast_write(emitter, ")");
                }
                vitte_c17_ast_write(emitter, ")");
                return;
            }
            if (node->kind == VITTE_AST_NODE_BINARY_EXPR &&
                (node->operator_kind == VITTE_TOKEN_EQUAL_EQUAL ||
                 node->operator_kind == VITTE_TOKEN_BANG_EQUAL) &&
                vitte_c17_ast_expression_is_pointer(emitter, left) &&
                vitte_c17_ast_expression_is_pointer(emitter, right)) {
                vitte_c17_ast_write(emitter, "(");
                vitte_c17_ast_emit_pointer_value(emitter, left);
                vitte_c17_ast_write(emitter, " ");
                vitte_c17_ast_emit_operator(
                    emitter,
                    node->operator_kind);
                vitte_c17_ast_write(emitter, " ");
                vitte_c17_ast_emit_pointer_value(emitter, right);
                vitte_c17_ast_write(emitter, ")");
                return;
            }
            if (node->kind == VITTE_AST_NODE_BINARY_EXPR &&
                (node->operator_kind == VITTE_TOKEN_EQUAL_EQUAL ||
                 node->operator_kind == VITTE_TOKEN_BANG_EQUAL)) {
                const vitte_ast_node_t *left_type =
                    vitte_c17_ast_expression_type(emitter, left);
                const vitte_ast_node_t *pick =
                    vitte_c17_ast_pick_decl_for_type(emitter, left_type);
                if (pick != NULL &&
                    vitte_c17_ast_pick_has_payload(emitter, pick)) {
                    const vitte_ast_node_t *right_pick =
                        vitte_c17_ast_pick_decl_for_type(
                            emitter,
                            vitte_c17_ast_expression_type(emitter, right));
                    const vitte_ast_node_t *pick_name =
                        vitte_c17_ast_child(emitter, pick, 0u);
                    if (right_pick != pick || pick_name == NULL ||
                        !vitte_c17_ast_pick_equality_supported(
                            emitter, pick)) {
                        emitter->failed = true;
                        return;
                    }
                    vitte_c17_ast_write(emitter, "(");
                    if (node->operator_kind == VITTE_TOKEN_BANG_EQUAL) {
                        vitte_c17_ast_write(emitter, "!");
                    }
                    vitte_c17_ast_write(emitter, "vitte_pick_equal_");
                    vitte_c17_ast_write_token(emitter, pick_name);
                    vitte_c17_ast_write(emitter, "(");
                    vitte_c17_ast_emit_expression(emitter, left);
                    vitte_c17_ast_write(emitter, ", ");
                    vitte_c17_ast_emit_expression(emitter, right);
                    vitte_c17_ast_write(emitter, "))");
                    return;
                }
            }
            if (node->kind == VITTE_AST_NODE_ASSIGN_EXPR &&
                node->operator_kind == VITTE_TOKEN_PLUS_EQUAL &&
                vitte_c17_ast_expression_is_string(emitter, left)) {
                vitte_c17_ast_write(emitter, "(");
                vitte_c17_ast_emit_expression(emitter, left);
                vitte_c17_ast_write(emitter, " = ");
                vitte_c17_ast_write(
                    emitter,
                    vitte_c17_ast_expression_is_character(
                            emitter,
                            right)
                        ? "vitte_string_concat_char("
                        : "vitte_string_concat(");
                vitte_c17_ast_emit_expression(emitter, left);
                vitte_c17_ast_write(emitter, ", ");
                vitte_c17_ast_emit_expression(emitter, right);
                vitte_c17_ast_write(emitter, "))");
                return;
            }
            if (node->kind == VITTE_AST_NODE_BINARY_EXPR &&
                node->operator_kind == VITTE_TOKEN_PLUS &&
                vitte_c17_ast_expression_is_string(emitter, left)) {
                if (vitte_c17_ast_expression_is_character(
                        emitter,
                        right)) {
                    vitte_c17_ast_write(
                        emitter,
                        "vitte_string_concat_char(");
                } else {
                    vitte_c17_ast_write(
                        emitter,
                        "vitte_string_concat(");
                }
                vitte_c17_ast_emit_expression(emitter, left);
                vitte_c17_ast_write(emitter, ", ");
                vitte_c17_ast_emit_expression(emitter, right);
                vitte_c17_ast_write(emitter, ")");
                return;
            }
            if (node->kind == VITTE_AST_NODE_BINARY_EXPR &&
                node->operator_kind == VITTE_TOKEN_PLUS &&
                vitte_c17_ast_is_string_literal_expression(
                    emitter,
                    left) &&
                vitte_c17_ast_is_string_literal_expression(
                    emitter,
                    right)) {
                vitte_c17_ast_write(emitter, "(");
                vitte_c17_ast_emit_string_literal_sequence(
                    emitter,
                    node);
                vitte_c17_ast_write(emitter, ")");
                return;
            }
            vitte_c17_ast_write(emitter, "(");
            vitte_c17_ast_emit_expression(emitter, left);
            vitte_c17_ast_write(emitter, " ");
            vitte_c17_ast_emit_operator(emitter, node->operator_kind);
            vitte_c17_ast_write(emitter, " ");
            vitte_c17_ast_emit_expression(emitter, right);
            vitte_c17_ast_write(emitter, ")");
            return;

        case VITTE_AST_NODE_CAST_EXPR:
            if (node->child_count != 2u) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_write(emitter, "(");
            vitte_c17_ast_emit_type(
                emitter,
                vitte_c17_ast_child(emitter, node, 1u));
            {
                const vitte_ast_node_t *operand =
                    vitte_c17_ast_child(emitter, node, 0u);
                const vitte_ast_node_t *target_type =
                    vitte_c17_ast_child(emitter, node, 1u);
                const vitte_ast_node_t *operand_type;
                const vitte_ast_node_t *name = operand;
                bool cast_through_void;

                operand_type = vitte_c17_ast_expression_type(
                    emitter,
                    operand);
                cast_through_void =
                    vitte_c17_ast_is_pointer_type_node(
                        emitter,
                        target_type) &&
                    vitte_c17_ast_is_pointer_type_node(
                        emitter,
                        operand_type);
                vitte_c17_ast_write(
                    emitter,
                    cast_through_void ? ")(void *)(" : ")(");

                if (operand != NULL &&
                    operand->kind == VITTE_AST_NODE_PATH &&
                    operand->child_count == 1u) {
                    name = vitte_c17_ast_child(emitter, operand, 0u);
                }
                if (vitte_c17_ast_is_pointer_type_node(
                        emitter,
                        target_type) &&
                    vitte_c17_ast_is_reference_parameter(
                        emitter,
                        name)) {
                    vitte_c17_ast_emit_pointer_value(emitter, operand);
                } else {
                    vitte_c17_ast_emit_expression(emitter, operand);
                }
            }
            vitte_c17_ast_write(emitter, ")");
            return;

        case VITTE_AST_NODE_CALL_EXPR:
            {
                const vitte_ast_node_t *callee;
                const vitte_ast_node_t *method;
                const vitte_token_t *method_token;

                callee = vitte_c17_ast_child(emitter, node, 0u);
                if (callee != NULL &&
                    callee->kind == VITTE_AST_NODE_PATH &&
                    vitte_c17_ast_token_is(emitter, callee, "fopen") &&
                    node->child_count == 3u) {
                    vitte_c17_ast_write(emitter, "((uint8_t *)fopen((const char *)(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 1u));
                    vitte_c17_ast_write(emitter, "), (const char *)(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 2u));
                    vitte_c17_ast_write(emitter, "))) ");
                    return;
                }
                if (callee != NULL &&
                    callee->kind == VITTE_AST_NODE_PATH &&
                    vitte_c17_ast_token_is(emitter, callee, "fread") &&
                    node->child_count == 5u) {
                    vitte_c17_ast_write(emitter, "fread((void *)(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 1u));
                    vitte_c17_ast_write(emitter, "), ");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 2u));
                    vitte_c17_ast_write(emitter, ", ");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 3u));
                    vitte_c17_ast_write(
                        emitter,
                        ", (FILE *)(void *)(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 4u));
                    vitte_c17_ast_write(emitter, "))");
                    return;
                }
                if (callee != NULL &&
                    callee->kind == VITTE_AST_NODE_PATH &&
                    (vitte_c17_ast_token_is(emitter, callee, "ferror") ||
                     vitte_c17_ast_token_is(emitter, callee, "fclose")) &&
                    node->child_count == 2u) {
                    vitte_c17_ast_write_token(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u));
                    vitte_c17_ast_write(
                        emitter,
                        "((FILE *)(void *)(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 1u));
                    vitte_c17_ast_write(emitter, "))");
                    return;
                }
                if (callee != NULL &&
                    callee->kind == VITTE_AST_NODE_MEMBER_EXPR) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);
                    const vitte_ast_node_t *member =
                        vitte_c17_ast_child(emitter, callee, 1u);
                    const char *pointer_helper = NULL;
                    size_t argument_count = node->child_count - 1u;
                    size_t argument_index;

                    if (vitte_c17_ast_token_is(
                            emitter, receiver, "pointer")) {
                        if (vitte_c17_ast_token_is(
                                emitter, member, "is_null") &&
                            argument_count == 1u) {
                            pointer_helper =
                                "vitte_core_pointer_is_null";
                        } else if (vitte_c17_ast_token_is(
                                       emitter, member, "read") &&
                                   argument_count == 1u) {
                            pointer_helper =
                                "vitte_core_pointer_read";
                        } else if (vitte_c17_ast_token_is(
                                       emitter, member, "write") &&
                                   argument_count == 2u) {
                            pointer_helper =
                                "vitte_core_pointer_write";
                        } else if (vitte_c17_ast_token_is(
                                       emitter, member, "address") &&
                                   argument_count == 1u) {
                            pointer_helper =
                                "vitte_core_pointer_address";
                        } else if (vitte_c17_ast_token_is(
                                       emitter, member, "add_bytes") &&
                                   argument_count == 2u) {
                            pointer_helper =
                                "vitte_core_pointer_add_bytes";
                        } else if (vitte_c17_ast_token_is(
                                       emitter, member, "add_bytes_const") &&
                                   argument_count == 2u) {
                            pointer_helper =
                                "vitte_core_pointer_add_bytes_const";
                        } else if (vitte_c17_ast_token_is(
                                       emitter, member, "from_address_mut") &&
                                   argument_count == 1u) {
                            pointer_helper =
                                "vitte_core_pointer_from_address_mut";
                        }
                    }
                    if (pointer_helper != NULL) {
                        vitte_c17_ast_write(emitter, pointer_helper);
                        vitte_c17_ast_write(emitter, "(");
                        for (argument_index = 1u;
                             argument_index < node->child_count;
                             ++argument_index) {
                            if (argument_index != 1u) {
                                vitte_c17_ast_write(emitter, ", ");
                            }
                            vitte_c17_ast_emit_expression(
                                emitter,
                                vitte_c17_ast_child(
                                    emitter,
                                    node,
                                    argument_index));
                        }
                        vitte_c17_ast_write(emitter, ")");
                        return;
                    }
                }
                if (vitte_c17_ast_emit_pick_variant_constructor(
                        emitter, node)) {
                    return;
                }
                method = callee != NULL &&
                         callee->kind == VITTE_AST_NODE_MEMBER_EXPR
                    ? vitte_c17_ast_child(emitter, callee, 1u)
                    : NULL;
                method_token =
                    vitte_c17_ast_token(emitter, method);
                if (method_token != NULL &&
                    method_token->length == 5u &&
                    memcmp(method_token->lexeme, "slice", 5u) == 0 &&
                    node->child_count == 3u &&
                    vitte_c17_ast_expression_is_string(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u))) {
                    vitte_c17_ast_write(emitter, "vitte_string_from_slice(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u));
                    vitte_c17_ast_write(emitter, ", ");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 1u));
                    vitte_c17_ast_write(emitter, ", ");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 2u));
                    vitte_c17_ast_write(emitter, ")");
                    return;
                }
                if (method_token != NULL &&
                    method_token->length == 8u &&
                    memcmp(method_token->lexeme, "as_bytes", 8u) == 0 &&
                    node->child_count == 1u &&
                    vitte_c17_ast_expression_is_string(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u))) {
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u));
                    return;
                }
                if (method_token != NULL &&
                    method_token->length == 5u &&
                    memcmp(method_token->lexeme, "bytes", 5u) == 0 &&
                    node->child_count == 1u &&
                    vitte_c17_ast_expression_is_string(
                            emitter,
                            vitte_c17_ast_child(emitter, callee, 0u))) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);

                    /* A string's byte view borrows its UTF-8 storage. */
                    vitte_c17_ast_write(emitter, "(VitteArray){ (void *)(");
                    vitte_c17_ast_emit_expression(emitter, receiver);
                    vitte_c17_ast_write(emitter, "), vitte_string_length(");
                    vitte_c17_ast_emit_expression(emitter, receiver);
                    vitte_c17_ast_write(emitter, "), vitte_string_length(");
                    vitte_c17_ast_emit_expression(emitter, receiver);
                    vitte_c17_ast_write(emitter, "), sizeof(uint8_t) }");
                    return;
                }
                if (method_token != NULL &&
                    method_token->length == 6u &&
                    memcmp(method_token->lexeme, "as_ptr", 6u) == 0 &&
                    node->child_count == 1u) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);
                    const vitte_ast_node_t *receiver_type;

                    if (vitte_c17_ast_expression_is_string(
                            emitter,
                            receiver)) {
                        vitte_c17_ast_emit_expression(emitter, receiver);
                        return;
                    }
                    receiver_type = vitte_c17_ast_expression_type(
                        emitter,
                        receiver);
                    while (receiver_type != NULL &&
                           (receiver_type->kind ==
                                VITTE_AST_NODE_REFERENCE_TYPE ||
                            receiver_type->kind ==
                                VITTE_AST_NODE_POINTER_TYPE)) {
                        receiver_type = receiver_type->child_count != 0u
                            ? vitte_c17_ast_child(
                                  emitter,
                                  receiver_type,
                                  0u)
                            : NULL;
                    }
                    if (receiver_type != NULL &&
                        receiver_type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
                        receiver_type->child_count == 1u) {
                        vitte_c17_ast_emit_expression(emitter, receiver);
                        vitte_c17_ast_write(
                            emitter,
                            vitte_c17_ast_member_uses_pointer(
                                    emitter,
                                    receiver)
                                ? "->data"
                                : ".data");
                        return;
                    }
                }
                if (method_token != NULL &&
                    (vitte_c17_ast_token_is(emitter, method, "wrapping_mul") ||
                     vitte_c17_ast_token_is(emitter, method, "wrapping_add")) &&
                    node->child_count == 2u) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);
                    const vitte_ast_node_t *receiver_type =
                        vitte_c17_ast_expression_type(emitter, receiver);
                    bool unsigned_type =
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "u8") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "u16") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "u32") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "u64") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "usize") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "uintptr");

                    if (unsigned_type) {
                        vitte_c17_ast_write(emitter, "(");
                        vitte_c17_ast_emit_expression(emitter, receiver);
                        vitte_c17_ast_write(
                            emitter,
                            vitte_c17_ast_token_is(
                                    emitter,
                                    method,
                                    "wrapping_mul")
                                ? " * "
                                : " + ");
                        vitte_c17_ast_emit_expression(
                            emitter,
                            vitte_c17_ast_child(emitter, node, 1u));
                        vitte_c17_ast_write(emitter, ")");
                        return;
                    }
                }
                if (method_token != NULL &&
                    method_token->length == 9u &&
                    memcmp(method_token->lexeme, "to_string", 9u) == 0 &&
                    node->child_count == 1u) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);
                    const vitte_ast_node_t *receiver_type =
                        vitte_c17_ast_expression_type(emitter, receiver);

                    if (vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "i8") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "i16") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "i32") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "i64") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "isize") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "int")) {
                        vitte_c17_ast_write(
                            emitter,
                            "vitte_i64_to_string((int64_t)(");
                        vitte_c17_ast_emit_expression(emitter, receiver);
                        vitte_c17_ast_write(emitter, "))");
                        return;
                    }
                    if (vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "u8") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "u16") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "u32") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "u64") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "usize") ||
                        vitte_c17_ast_type_is_named(
                            emitter, receiver_type, "uintptr")) {
                        vitte_c17_ast_write(
                            emitter,
                            "vitte_u64_to_string((uint64_t)(");
                        vitte_c17_ast_emit_expression(emitter, receiver);
                        vitte_c17_ast_write(emitter, "))");
                        return;
                    }
                }
                if (method_token != NULL &&
                    method_token->length == 6u &&
                    memcmp(method_token->lexeme, "as_ptr", 6u) == 0 &&
                    node->child_count == 1u) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);
                    const vitte_ast_node_t *receiver_type;

                    if (vitte_c17_ast_expression_is_string(
                            emitter,
                            receiver)) {
                        vitte_c17_ast_emit_expression(emitter, receiver);
                        return;
                    }
                    receiver_type = vitte_c17_ast_expression_type(
                        emitter,
                        receiver);
                    while (receiver_type != NULL &&
                           (receiver_type->kind ==
                                VITTE_AST_NODE_REFERENCE_TYPE ||
                            receiver_type->kind ==
                                VITTE_AST_NODE_POINTER_TYPE)) {
                        receiver_type = receiver_type->child_count != 0u
                            ? vitte_c17_ast_child(
                                  emitter,
                                  receiver_type,
                                  0u)
                            : NULL;
                    }
                    if (receiver_type != NULL &&
                        receiver_type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
                        receiver_type->child_count == 1u) {
                        vitte_c17_ast_emit_expression(emitter, receiver);
                        vitte_c17_ast_write(
                            emitter,
                            vitte_c17_ast_member_uses_pointer(
                                    emitter,
                                    receiver)
                                ? "->data"
                                : ".data");
                        return;
                    }
                }
                if (method_token != NULL &&
                    method_token->length == 3u &&
                    memcmp(method_token->lexeme, "len", 3u) == 0 &&
                    node->child_count == 1u) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);
                    const vitte_ast_node_t *receiver_type;

                    if (vitte_c17_ast_expression_is_string(
                            emitter,
                            receiver)) {
                        vitte_c17_ast_write(
                            emitter,
                            "vitte_string_length(");
                        vitte_c17_ast_emit_expression(
                            emitter,
                            receiver);
                        vitte_c17_ast_write(emitter, ")");
                        return;
                    }

                    receiver_type = vitte_c17_ast_expression_type(
                        emitter,
                        receiver);
                    while (receiver_type != NULL &&
                           (receiver_type->kind ==
                                VITTE_AST_NODE_REFERENCE_TYPE ||
                            receiver_type->kind ==
                                VITTE_AST_NODE_POINTER_TYPE)) {
                        receiver_type = receiver_type->child_count != 0u
                            ? vitte_c17_ast_child(
                                  emitter,
                                  receiver_type,
                                  0u)
                            : NULL;
                    }
                    if (receiver_type != NULL &&
                        receiver_type->kind ==
                            VITTE_AST_NODE_ARRAY_TYPE &&
                        receiver_type->child_count == 1u) {
                        if (vitte_c17_ast_member_uses_pointer(
                                emitter,
                                receiver)) {
                            vitte_c17_ast_emit_pointer_value(
                                emitter,
                                receiver);
                            vitte_c17_ast_write(emitter, "->length");
                        } else {
                            vitte_c17_ast_emit_expression(
                                emitter,
                                receiver);
                            vitte_c17_ast_write(emitter, ".length");
                        }
                        return;
                    }
                }
                if (method_token != NULL &&
                    method_token->length == 7u &&
                    memcmp(method_token->lexeme, "reserve", 7u) == 0 &&
                    node->child_count == 2u) {
                    const vitte_ast_node_t *array_type =
                        vitte_c17_ast_expression_type(
                            emitter,
                            vitte_c17_ast_child(emitter, callee, 0u));
                    if (array_type == NULL ||
                        array_type->kind != VITTE_AST_NODE_ARRAY_TYPE ||
                        array_type->child_count != 1u) {
                        emitter->failed = true;
                        return;
                    }
                    vitte_c17_ast_write(emitter, "vitte_array_reserve(&(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u));
                    vitte_c17_ast_write(emitter, "), ");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 1u));
                    vitte_c17_ast_write(emitter, ", sizeof(");
                    vitte_c17_ast_emit_type(
                        emitter,
                        vitte_c17_ast_child(emitter, array_type, 0u));
                    vitte_c17_ast_write(emitter, "))");
                    return;
                }
                if (method_token != NULL &&
                    method_token->length == 5u &&
                    memcmp(method_token->lexeme, "clear", 5u) == 0 &&
                    node->child_count == 1u) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);
                    const vitte_ast_node_t *array_type =
                        vitte_c17_ast_expression_type(emitter, receiver);
                    while (array_type != NULL &&
                           (array_type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                            array_type->kind == VITTE_AST_NODE_POINTER_TYPE)) {
                        array_type = array_type->child_count != 0u
                            ? vitte_c17_ast_child(emitter, array_type, 0u)
                            : NULL;
                    }
                    if (array_type == NULL ||
                        array_type->kind != VITTE_AST_NODE_ARRAY_TYPE ||
                        array_type->child_count != 1u) {
                        emitter->failed = true;
                        return;
                    }
                    vitte_c17_ast_write(emitter, "(");
                    vitte_c17_ast_emit_expression(emitter, receiver);
                    vitte_c17_ast_write(
                        emitter,
                        vitte_c17_ast_member_uses_pointer(emitter, receiver)
                            ? "->length = 0u)"
                            : ".length = 0u)");
                    return;
                }
                if (method_token != NULL &&
                    ((method_token->length == 13u &&
                      memcmp(method_token->lexeme, "shrink_to_fit", 13u) == 0 &&
                      node->child_count == 1u) ||
                     (method_token->length == 13u &&
                      memcmp(method_token->lexeme, "try_shrink_to", 13u) == 0 &&
                      node->child_count == 2u))) {
                    const vitte_ast_node_t *receiver =
                        vitte_c17_ast_child(emitter, callee, 0u);
                    const vitte_ast_node_t *array_type =
                        vitte_c17_ast_expression_type(emitter, receiver);
                    bool shrink_to_fit =
                        memcmp(method_token->lexeme, "shrink_to_fit", 13u) == 0;
                    while (array_type != NULL &&
                           (array_type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                            array_type->kind == VITTE_AST_NODE_POINTER_TYPE)) {
                        array_type = array_type->child_count != 0u
                            ? vitte_c17_ast_child(emitter, array_type, 0u)
                            : NULL;
                    }
                    if (array_type == NULL ||
                        array_type->kind != VITTE_AST_NODE_ARRAY_TYPE ||
                        array_type->child_count != 1u) {
                        emitter->failed = true;
                        return;
                    }
                    vitte_c17_ast_write(
                        emitter,
                        "vitte_array_try_shrink_to(&( ");
                    vitte_c17_ast_emit_expression(emitter, receiver);
                    vitte_c17_ast_write(emitter, "), ");
                    if (shrink_to_fit) {
                        vitte_c17_ast_emit_expression(emitter, receiver);
                        vitte_c17_ast_write(
                            emitter,
                            vitte_c17_ast_member_uses_pointer(emitter, receiver)
                                ? "->length)"
                                : ".length)");
                    } else {
                        vitte_c17_ast_emit_expression(
                            emitter,
                            vitte_c17_ast_child(emitter, node, 1u));
                        vitte_c17_ast_write(emitter, ")");
                    }
                    return;
                }
                if (method_token != NULL &&
                    method_token->length == 4u &&
                    memcmp(method_token->lexeme, "push", 4u) == 0 &&
                    node->child_count == 2u) {
                    const vitte_ast_node_t *argument;
                    const vitte_ast_node_t *argument_type;

                    argument =
                        vitte_c17_ast_child(emitter, node, 1u);
                    argument_type =
                        vitte_c17_ast_expression_type(
                            emitter,
                            argument);
                    vitte_c17_ast_write(
                        emitter,
                        "vitte_array_push(&(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u));
                    vitte_c17_ast_write(emitter, "), ");
                    if (argument_type != NULL) {
                        vitte_c17_ast_write(emitter, "(");
                        vitte_c17_ast_emit_type(
                            emitter,
                            argument_type);
                        vitte_c17_ast_write(emitter, "[]){");
                        vitte_c17_ast_emit_expression(
                            emitter,
                            argument);
                        vitte_c17_ast_write(emitter, "}");
                    } else {
                        vitte_c17_ast_write(emitter, "&(");
                        vitte_c17_ast_emit_expression(
                            emitter,
                            argument);
                        vitte_c17_ast_write(emitter, ")");
                    }
                    vitte_c17_ast_write(emitter, ", sizeof(");
                    if (argument_type != NULL) {
                        vitte_c17_ast_emit_type(
                            emitter,
                            argument_type);
                    } else {
                        vitte_c17_ast_write(emitter, "int");
                    }
                    vitte_c17_ast_write(emitter, "))");
                    return;
                }
            }
            if (!vitte_c17_ast_emit_string_builtin_callee(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 0u))) {
                const vitte_ast_node_t *callee;
                const vitte_ast_node_t *callee_name;
                const vitte_ast_node_t *async_procedure;
                const vitte_ast_node_t *impl_method = NULL;
                size_t generic_instance;
                bool callee_emitted;

                callee = vitte_c17_ast_child(emitter, node, 0u);
                async_procedure = vitte_c17_ast_find_path_declaration(
                    emitter,
                    callee,
                    VITTE_AST_NODE_PROC_DECL,
                    VITTE_AST_NODE_EXTERN_PROC_DECL);
                if (callee != NULL &&
                    callee->kind == VITTE_AST_NODE_MEMBER_EXPR) {
                    impl_method = vitte_c17_ast_resolve_impl_method(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u),
                        vitte_c17_ast_child(emitter, callee, 1u),
                        &generic_instance);
                }
                if (impl_method != NULL) {
                    size_t argument_index;
                    vitte_c17_ast_emit_impl_method_name(
                        emitter, impl_method, generic_instance);
                    vitte_c17_ast_write(emitter, "(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, callee, 0u));
                    for (argument_index = 1u;
                         argument_index < node->child_count;
                         ++argument_index) {
                        vitte_c17_ast_write(emitter, ", ");
                        vitte_c17_ast_emit_call_argument(
                            emitter,
                            node,
                            argument_index - 1u);
                    }
                    vitte_c17_ast_write(emitter, ")");
                    return;
                }
                generic_instance =
                    vitte_c17_ast_generic_proc_instance_for_call(
                        emitter, node);
                callee_name = NULL;
                callee_emitted = false;
                if (callee != NULL &&
                    callee->kind == VITTE_AST_NODE_PATH &&
                    callee->child_count != 0u) {
                    callee_name = vitte_c17_ast_child(
                        emitter,
                        callee,
                        callee->child_count - 1u);
                }
                if (async_procedure != NULL &&
                    (async_procedure->flags & VITTE_AST_FLAG_ASYNC) != 0u) {
                    if (vitte_c17_ast_generic_form_parameter_count(
                            emitter, async_procedure, NULL) != 0u) {
                        emitter->failed = true;
                        return;
                    }
                    vitte_c17_ast_write_async_task_function_name(
                        emitter, async_procedure, "create");
                    callee_emitted = true;
                } else if (generic_instance != SIZE_MAX &&
                    generic_instance < emitter->generic_proc_count) {
                    vitte_c17_ast_write(
                        emitter,
                        emitter->generic_procs[generic_instance].c_name);
                    callee_emitted = true;
                } else if (callee_name != NULL &&
                    callee != NULL &&
                    callee->kind == VITTE_AST_NODE_PATH &&
                    callee->child_count == 1u &&
                    emitter->space_depth != 0u &&
                    vitte_c17_ast_is_local_space_function(
                        emitter,
                        callee_name)) {
                    vitte_c17_ast_emit_function_name(
                        emitter,
                        callee_name);
                    callee_emitted = true;
                } else if (callee_name != NULL) {
                    callee_emitted =
                        vitte_c17_ast_emit_resolved_procedure_name(
                            emitter,
                            callee);
                }
                if (!callee_emitted) {
                    vitte_c17_ast_emit_expression(emitter, callee);
                }
            }
            vitte_c17_ast_write(emitter, "(");
            for (index = 1u; index < node->child_count; ++index) {
                if (index != 1u) {
                    vitte_c17_ast_write(emitter, ", ");
                }
                vitte_c17_ast_emit_call_argument(
                    emitter,
                    node,
                    index - 1u);
            }
            vitte_c17_ast_write(emitter, ")");
            return;

        case VITTE_AST_NODE_MEMBER_EXPR:
            left = vitte_c17_ast_child(emitter, node, 0u);
            right = vitte_c17_ast_child(emitter, node, 1u);
            {
                const vitte_ast_node_t *generic_pick;
                const vitte_ast_node_t *generic_variant;
                const vitte_c17_ast_generic_form_instance_t *generic_instance;
                if (vitte_c17_ast_resolve_generic_pick_variant_member(
                        emitter,
                        node,
                        &generic_pick,
                        &generic_variant,
                        &generic_instance)) {
                    if (emitter->failed || generic_instance == NULL) {
                        return;
                    }
                    if (vitte_c17_ast_pick_has_payload(
                            emitter, generic_pick)) {
                        if (generic_variant->child_count != 1u) {
                            emitter->failed = true;
                            return;
                        }
                        vitte_c17_ast_write(emitter, "((");
                        vitte_c17_ast_emit_generic_form_instance_name(
                            emitter, generic_instance);
                        vitte_c17_ast_write(emitter, "){ .tag = ");
                        vitte_c17_ast_emit_generic_form_instance_name(
                            emitter, generic_instance);
                        vitte_c17_ast_write(emitter, "_tag_");
                        vitte_c17_ast_write_token(
                            emitter,
                            vitte_c17_ast_child(emitter, generic_variant, 0u));
                        vitte_c17_ast_write(emitter, " })");
                    } else {
                        vitte_c17_ast_emit_generic_form_instance_name(
                            emitter, generic_instance);
                        vitte_c17_ast_write(emitter, "_");
                        vitte_c17_ast_write_token(
                            emitter,
                            vitte_c17_ast_child(emitter, generic_variant, 0u));
                    }
                    return;
                }
            }
            if ((vitte_c17_ast_token_is(emitter, left, "usize") ||
                 vitte_c17_ast_token_is(emitter, left, "size_t")) &&
                vitte_c17_ast_token_is(emitter, right, "MAX")) {
                vitte_c17_ast_write(emitter, "SIZE_MAX");
                return;
            }
            if (vitte_c17_ast_token_is(emitter, left, "uintptr") &&
                vitte_c17_ast_token_is(emitter, right, "MAX")) {
                vitte_c17_ast_write(emitter, "UINTPTR_MAX");
                return;
            }
            if (vitte_c17_ast_is_pick_type(emitter, left)) {
                const vitte_ast_node_t *pick;
                const vitte_ast_node_t *variant;

                pick = vitte_c17_ast_pick_decl_for_type(
                    emitter, left);
                variant = vitte_c17_ast_find_pick_variant(
                    emitter, pick, right);
                if (variant == NULL) {
                    emitter->failed = true;
                } else {
                    vitte_c17_ast_emit_pick_unit_variant(
                        emitter, pick, variant);
                }
                return;
            }
            {
                const vitte_token_t *member;

                member = vitte_c17_ast_token(emitter, right);
                if (member != NULL &&
                    member->length == 6u &&
                    memcmp(member->lexeme, "length", 6u) == 0 &&
                    vitte_c17_ast_expression_is_string(emitter, left)) {
                    vitte_c17_ast_write(emitter, "strlen(");
                    vitte_c17_ast_emit_expression(emitter, left);
                    vitte_c17_ast_write(emitter, ")");
                    return;
                }
            }
            if (vitte_c17_ast_member_uses_pointer(emitter, left)) {
                vitte_c17_ast_emit_pointer_value(emitter, left);
                vitte_c17_ast_write(emitter, "->");
            } else {
                vitte_c17_ast_emit_expression(emitter, left);
                vitte_c17_ast_write(emitter, ".");
            }
            vitte_c17_ast_write_token(emitter, right);
            return;

        case VITTE_AST_NODE_INDEX_EXPR:
            {
                const vitte_ast_node_t *base_type;
                const vitte_ast_node_t *array_type;
                const vitte_ast_node_t *base;
                const vitte_ast_node_t *slice_element;
                bool array_pointer;

                base = vitte_c17_ast_child(emitter, node, 0u);
                base_type = vitte_c17_ast_expression_type(emitter, base);
                array_pointer = false;
                while (base_type != NULL &&
                       (base_type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                        base_type->kind == VITTE_AST_NODE_POINTER_TYPE)) {
                    array_pointer = true;
                    base_type = base_type->child_count != 0u
                        ? vitte_c17_ast_child(emitter, base_type, 0u)
                        : NULL;
                }
                array_type = base_type;
                slice_element = array_type != NULL &&
                        array_type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
                        array_type->child_count == 1u
                    ? vitte_c17_ast_child(emitter, array_type, 0u)
                    : vitte_c17_ast_generic_alias_slice_element_recursive(
                          emitter, array_type, 0u);
                if (vitte_c17_ast_expression_is_string(emitter, base)) {
                    vitte_c17_ast_write(emitter, "((uint8_t)(");
                    vitte_c17_ast_emit_expression(emitter, base);
                    vitte_c17_ast_write(emitter, "[");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 1u));
                    vitte_c17_ast_write(emitter, "]))");
                } else if (slice_element != NULL) {
                    vitte_c17_ast_write(emitter, "((");
                    vitte_c17_ast_emit_type(
                        emitter,
                        slice_element);
                    vitte_c17_ast_write(emitter, " *)(");
                    if (array_pointer) {
                        vitte_c17_ast_emit_pointer_value(emitter, base);
                        vitte_c17_ast_write(emitter, "->data");
                    } else {
                        vitte_c17_ast_emit_expression(emitter, base);
                        vitte_c17_ast_write(emitter, ".data");
                    }
                    vitte_c17_ast_write(emitter, "))[");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 1u));
                    vitte_c17_ast_write(emitter, "]");
                } else {
                    vitte_c17_ast_emit_expression(
                        emitter,
                        base);
                    vitte_c17_ast_write(emitter, "[");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 1u));
                    vitte_c17_ast_write(emitter, "]");
                }
            }
            return;

        default:
            emitter->failed = true;
            return;
    }
}

static void
vitte_c17_ast_emit_statement(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    unsigned int indent);

static void
vitte_c17_ast_indent(
    vitte_c17_ast_emitter_t *emitter,
    unsigned int indent);

static const vitte_ast_node_t *
vitte_c17_ast_match_binding(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pattern);

static void
vitte_c17_ast_emit_match_pick_bindings(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pattern,
    const char *subject_name,
    unsigned int indent)
{
    const vitte_ast_node_t *variant_path;
    const vitte_ast_node_t *pick;
    const vitte_ast_node_t *variant;
    const vitte_c17_ast_generic_form_instance_t *generic_instance = NULL;
    size_t saved_substitution_count;
    size_t parameter_index;
    bool generic_variant;
    size_t index;

    if (pattern == NULL || pattern->kind != VITTE_AST_NODE_PATTERN ||
        (pattern->flags & VITTE_AST_FLAG_PATTERN_HEAD) == 0u ||
        pattern->child_count == 0u) {
        return;
    }
    variant_path = vitte_c17_ast_child(emitter, pattern, 0u);
    generic_variant = vitte_c17_ast_resolve_generic_pick_variant_member(
        emitter,
        variant_path,
        &pick,
        &variant,
        &generic_instance);
    saved_substitution_count = emitter->generic_substitution_count;
    if (generic_variant) {
        if (emitter->failed || generic_instance == NULL ||
            saved_substitution_count >
                emitter->generic_substitution_capacity ||
            generic_instance->argument_count >
                emitter->generic_substitution_capacity -
                    saved_substitution_count) {
            emitter->failed = true;
            return;
        }
        for (parameter_index = 0u;
             parameter_index < generic_instance->argument_count;
             ++parameter_index) {
            emitter->generic_parameter_names[
                saved_substitution_count + parameter_index] =
                vitte_c17_ast_generic_form_parameter_name(
                    emitter,
                    generic_instance->declaration,
                    parameter_index);
            emitter->generic_arguments[
                saved_substitution_count + parameter_index] =
                generic_instance->arguments[parameter_index];
            if (emitter->generic_parameter_names[
                    saved_substitution_count + parameter_index] == NULL ||
                emitter->generic_arguments[
                    saved_substitution_count + parameter_index] == NULL) {
                emitter->failed = true;
                emitter->generic_substitution_count = saved_substitution_count;
                return;
            }
        }
        emitter->generic_substitution_count =
            saved_substitution_count + generic_instance->argument_count;
    } else if (!vitte_c17_ast_resolve_pick_variant_path(
                   emitter, variant_path, &pick, &variant)) {
        emitter->failed = true;
        return;
    }
    for (index = 1u; index < pattern->child_count; ++index) {
        const vitte_ast_node_t *payload_pattern =
            vitte_c17_ast_child(emitter, pattern, index);
        const vitte_ast_node_t *binding =
            vitte_c17_ast_match_binding(emitter, payload_pattern);
        const vitte_ast_node_t *binding_name;

        if (binding == NULL) {
            continue;
        }
        binding_name = vitte_c17_ast_child(emitter, binding, 0u);
        vitte_c17_ast_indent(emitter, indent);
        vitte_c17_ast_emit_type(
            emitter,
            vitte_c17_ast_child(emitter, variant, index));
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_write_token(emitter, binding_name);
        vitte_c17_ast_write(emitter, " = ");
        vitte_c17_ast_write(emitter, subject_name);
        vitte_c17_ast_write(emitter, ".payload.");
        vitte_c17_ast_write_token(
            emitter,
            vitte_c17_ast_child(emitter, variant, 0u));
        vitte_c17_ast_write(emitter, ".");
        vitte_c17_ast_write_payload_field_name(emitter, index - 1u);
        vitte_c17_ast_write(emitter, ";\n");
    }
    while (emitter->generic_substitution_count >
           saved_substitution_count) {
        --emitter->generic_substitution_count;
        emitter->generic_parameter_names[
            emitter->generic_substitution_count] = NULL;
        emitter->generic_arguments[
            emitter->generic_substitution_count] = NULL;
        emitter->generic_c_type_names[
            emitter->generic_substitution_count] = NULL;
    }
    (void)pick;
}

static void
vitte_c17_ast_emit_if_expression_into(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *target,
    unsigned int indent);

static void
vitte_c17_ast_emit_block_expression_into(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *block,
    const vitte_ast_node_t *target,
    unsigned int indent)
{
    size_t index;
    const vitte_ast_node_t *tail;

    if (block == NULL ||
        block->kind != VITTE_AST_NODE_BLOCK_EXPR ||
        block->child_count == 0u ||
        target == NULL) {
        emitter->failed = true;
        return;
    }
    for (index = 0u; index + 1u < block->child_count; ++index) {
        vitte_c17_ast_emit_statement(
            emitter,
            vitte_c17_ast_child(emitter, block, index),
            indent);
    }
    tail = vitte_c17_ast_child(
        emitter,
        block,
        block->child_count - 1u);
    if (tail == NULL) {
        emitter->failed = true;
        return;
    }
    if (tail->kind == VITTE_AST_NODE_IF_EXPR) {
        vitte_c17_ast_emit_if_expression_into(
            emitter,
            tail,
            target,
            indent);
        return;
    }
    vitte_c17_ast_indent(emitter, indent);
    vitte_c17_ast_write_token(emitter, target);
    vitte_c17_ast_write(emitter, " = ");
    vitte_c17_ast_emit_expression(emitter, tail);
    vitte_c17_ast_write(emitter, ";\n");
}

static void
vitte_c17_ast_emit_if_expression_into(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *target,
    unsigned int indent)
{
    const vitte_ast_node_t *then_branch;
    const vitte_ast_node_t *else_branch;

    if (node == NULL ||
        node->kind != VITTE_AST_NODE_IF_EXPR ||
        node->child_count != 3u ||
        target == NULL) {
        emitter->failed = true;
        return;
    }
    then_branch = vitte_c17_ast_child(emitter, node, 1u);
    else_branch = vitte_c17_ast_child(emitter, node, 2u);
    vitte_c17_ast_indent(emitter, indent);
    vitte_c17_ast_write(emitter, "if ");
    vitte_c17_ast_emit_condition(
        emitter,
        vitte_c17_ast_child(emitter, node, 0u));
    vitte_c17_ast_write(emitter, " {\n");
    vitte_c17_ast_emit_block_expression_into(
        emitter,
        then_branch,
        target,
        indent + 1u);
    vitte_c17_ast_indent(emitter, indent);
    vitte_c17_ast_write(emitter, "}");
    if (else_branch != NULL &&
        else_branch->kind == VITTE_AST_NODE_IF_EXPR) {
        vitte_c17_ast_write(emitter, " else {\n");
        vitte_c17_ast_emit_if_expression_into(
            emitter,
            else_branch,
            target,
            indent + 1u);
        vitte_c17_ast_indent(emitter, indent);
        vitte_c17_ast_write(emitter, "}\n");
    } else {
        vitte_c17_ast_write(emitter, " else {\n");
        vitte_c17_ast_emit_block_expression_into(
            emitter,
            else_branch,
            target,
            indent + 1u);
        vitte_c17_ast_indent(emitter, indent);
        vitte_c17_ast_write(emitter, "}\n");
    }
}

static void
vitte_c17_ast_emit_condition(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    if (node == NULL) {
        emitter->failed = true;
        return;
    }

    if (node->kind == VITTE_AST_NODE_BINARY_EXPR ||
        node->kind == VITTE_AST_NODE_ASSIGN_EXPR) {
        vitte_c17_ast_emit_expression(emitter, node);
        return;
    }

    vitte_c17_ast_write(emitter, "(");
    vitte_c17_ast_emit_expression(emitter, node);
    vitte_c17_ast_write(emitter, ")");
}

static void
vitte_c17_ast_indent(
    vitte_c17_ast_emitter_t *emitter,
    unsigned int indent)
{
    unsigned int index;

    for (index = 0u; index < indent; ++index) {
        vitte_c17_ast_write(emitter, "    ");
    }
}

static void
vitte_c17_ast_emit_coalesce_temporaries(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    unsigned int depth)
{
    size_t index;

    if (node == NULL || emitter->failed) {
        return;
    }
    if (depth > 512u) {
        emitter->failed = true;
        return;
    }
    if (node->kind == VITTE_AST_NODE_BINARY_EXPR &&
        node->operator_kind == VITTE_TOKEN_QUESTION_QUESTION &&
        node->child_count >= 2u &&
        vitte_c17_ast_child(emitter, node, 0u)->kind !=
            VITTE_AST_NODE_NULL_LITERAL) {
        const vitte_ast_node_t *operand_type =
            vitte_c17_ast_expression_type(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));

        if (operand_type == NULL) {
            emitter->failed = true;
            return;
        }
        vitte_c17_ast_indent(emitter, 1u);
        vitte_c17_ast_emit_type(emitter, operand_type);
        if (fprintf(
                emitter->output,
                " vitte_internal_coalesce_%llu = NULL;\n",
                (unsigned long long)node->id) < 0) {
            emitter->failed = true;
            return;
        }
    }
    for (index = 0u; index < node->child_count && !emitter->failed; ++index) {
        vitte_c17_ast_emit_coalesce_temporaries(
            emitter,
            vitte_c17_ast_child(emitter, node, index),
            depth + 1u);
    }
}

static void
vitte_c17_ast_emit_block_contents(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *block,
    unsigned int indent)
{
    size_t saved_deferred_count;
    size_t index;

    if (block == NULL || block->kind != VITTE_AST_NODE_BLOCK) {
        emitter->failed = true;
        return;
    }

    saved_deferred_count = emitter->deferred_count;
    for (index = 0u; index < block->child_count; ++index) {
        const vitte_ast_node_t *statement =
            vitte_c17_ast_child(emitter, block, index);
        vitte_c17_ast_emit_statement(emitter, statement, indent);
    }
    while (emitter->deferred_count > saved_deferred_count &&
           !emitter->failed) {
        const vitte_ast_node_t *action;

        --emitter->deferred_count;
        action = emitter->deferred_nodes[emitter->deferred_count];
        if (action == NULL) {
            emitter->failed = true;
            break;
        }
        vitte_c17_ast_indent(emitter, indent);
        if (action->kind == VITTE_AST_NODE_BLOCK) {
            vitte_c17_ast_emit_statement(emitter, action, indent);
        } else {
            vitte_c17_ast_emit_expression(emitter, action);
            vitte_c17_ast_write(emitter, ";\n");
        }
    }
}

static void
vitte_c17_ast_emit_deferred_actions(
    vitte_c17_ast_emitter_t *emitter,
    size_t target_count,
    unsigned int indent)
{
    size_t index = emitter->deferred_count;

    while (index > target_count && !emitter->failed) {
        const vitte_ast_node_t *action;

        --index;
        action = emitter->deferred_nodes[index];
        if (action == NULL) {
            emitter->failed = true;
            return;
        }
        vitte_c17_ast_indent(emitter, indent);
        if (action->kind == VITTE_AST_NODE_BLOCK) {
            vitte_c17_ast_emit_statement(emitter, action, indent);
        } else {
            vitte_c17_ast_emit_expression(emitter, action);
            vitte_c17_ast_write(emitter, ";\n");
        }
    }
}

static const vitte_ast_node_t *
vitte_c17_ast_unwrap_groups(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    while (expression != NULL &&
           expression->kind == VITTE_AST_NODE_GROUP_EXPR) {
        expression = vitte_c17_ast_child(emitter, expression, 0u);
    }
    return expression;
}

static const vitte_ast_node_t *
vitte_c17_ast_find_local_initializer(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const vitte_ast_node_t *name)
{
    size_t index;

    if (node == NULL || name == NULL) {
        return NULL;
    }

    if (node->kind == VITTE_AST_NODE_LET_STMT &&
        node->child_count >= 2u &&
        vitte_c17_ast_token_equal(
            emitter,
            vitte_c17_ast_child(emitter, node, 0u),
            name)) {
        const vitte_ast_node_t *value;

        value = vitte_c17_ast_child(emitter, node, 1u);
        if (value != NULL &&
            (value->kind == VITTE_AST_NODE_TYPE_EXPR ||
             value->kind == VITTE_AST_NODE_ARRAY_TYPE ||
             value->kind == VITTE_AST_NODE_POINTER_TYPE ||
             value->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
             value->kind == VITTE_AST_NODE_OPTIONAL_TYPE)) {
            return node->child_count > 2u
                ? vitte_c17_ast_child(emitter, node, 2u)
                : NULL;
        }
        return value;
    }

    for (index = 0u; index < node->child_count; ++index) {
        const vitte_ast_node_t *initializer;

        initializer = vitte_c17_ast_find_local_initializer(
            emitter,
            vitte_c17_ast_child(emitter, node, index),
            name);
        if (initializer != NULL) {
            return initializer;
        }
    }

    return NULL;
}

static bool
vitte_c17_ast_expression_is_lvalue(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    if (expression == NULL) {
        return false;
    }

    if (expression->kind == VITTE_AST_NODE_GROUP_EXPR) {
        return vitte_c17_ast_expression_is_lvalue(
            emitter,
            vitte_c17_ast_child(emitter, expression, 0u));
    }

    if (expression->kind == VITTE_AST_NODE_PATH ||
        expression->kind == VITTE_AST_NODE_IDENTIFIER ||
        expression->kind == VITTE_AST_NODE_MEMBER_EXPR ||
        expression->kind == VITTE_AST_NODE_INDEX_EXPR) {
        return true;
    }

    return expression->kind == VITTE_AST_NODE_UNARY_EXPR &&
           expression->operator_kind == VITTE_TOKEN_STAR;
}

static bool
vitte_c17_ast_generated_name_is_used(
    const vitte_c17_ast_emitter_t *emitter,
    const char *name,
    size_t name_length)
{
    size_t index;

    for (index = 0u; index < emitter->parser->token_count; ++index) {
        const vitte_token_t *token = &emitter->parser->tokens[index];

        if (token->lexeme != NULL &&
            token->length == name_length &&
            memcmp(token->lexeme, name, name_length) == 0) {
            return true;
        }
    }

    return false;
}

static bool
vitte_c17_ast_make_for_temporary_name(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const char *role,
    char *buffer,
    size_t capacity)
{
    size_t attempt;

    if (emitter == NULL || node == NULL || role == NULL ||
        buffer == NULL || capacity == 0u) {
        return false;
    }

    for (attempt = 0u; attempt <= emitter->parser->token_count; ++attempt) {
        int length;

        length = snprintf(
            buffer,
            capacity,
            "vitte_internal_for_%llu_%s_%zu",
            (unsigned long long)node->id,
            role,
            attempt);
        if (length < 0 || (size_t)length >= capacity) {
            return false;
        }
        if (!vitte_c17_ast_generated_name_is_used(
                emitter,
                buffer,
                (size_t)length)) {
            return true;
        }
    }

    return false;
}

static const vitte_ast_node_t *
vitte_c17_ast_for_binding_name(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pattern)
{
    if (pattern == NULL) {
        return NULL;
    }

    if (pattern->kind == VITTE_AST_NODE_PATTERN &&
        pattern->child_count == 1u &&
        (pattern->flags & VITTE_AST_FLAG_PATTERN_MUTABLE) != 0u) {
        pattern = vitte_c17_ast_child(emitter, pattern, 0u);
    }

    if (pattern == NULL ||
        pattern->kind != VITTE_AST_NODE_PATH ||
        pattern->child_count != 1u) {
        return NULL;
    }

    return pattern;
}

static void
vitte_c17_ast_emit_for_element_type(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *element_type,
    const vitte_ast_node_t *sample_expression)
{
    const char *inferred_type;

    if (element_type != NULL) {
        vitte_c17_ast_emit_type(emitter, element_type);
        return;
    }

    inferred_type = vitte_c17_ast_inferred_type(
        emitter,
        sample_expression);
    if (inferred_type == NULL) {
        emitter->failed = true;
        return;
    }

    vitte_c17_ast_write(emitter, inferred_type);
}

static void
vitte_c17_ast_emit_for_statement(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    unsigned int indent)
{
    const vitte_ast_node_t *pattern;
    const vitte_ast_node_t *binding;
    const vitte_ast_node_t *iterable;
    const vitte_ast_node_t *iterable_value;
    const vitte_ast_node_t *array_type;
    const vitte_ast_node_t *element_type;
    const vitte_ast_node_t *element_sample;
    const vitte_ast_node_t *array_literal;
    const vitte_ast_node_t *body;
    bool fixed_array;
    bool literal_array;
    bool inferred_array_local;
    bool typed_array;
    bool dynamic_lvalue;
    char array_name[128];
    char index_name[128];
    char range_start_name[128];
    char range_end_name[128];
    char continue_label[128];
    size_t index;

    if (node == NULL || node->child_count != 3u) {
        emitter->failed = true;
        return;
    }

    pattern = vitte_c17_ast_child(emitter, node, 0u);
    binding = vitte_c17_ast_for_binding_name(emitter, pattern);
    iterable = vitte_c17_ast_child(emitter, node, 1u);
    iterable_value = vitte_c17_ast_unwrap_groups(emitter, iterable);
    body = vitte_c17_ast_child(emitter, node, 2u);
    if (binding == NULL || iterable_value == NULL ||
        body == NULL || body->kind != VITTE_AST_NODE_BLOCK ||
        !vitte_c17_ast_make_for_temporary_name(
            emitter, node, "array", array_name, sizeof(array_name)) ||
        !vitte_c17_ast_make_for_temporary_name(
            emitter, node, "index", index_name, sizeof(index_name))) {
        emitter->failed = true;
        return;
    }

    array_type = vitte_c17_ast_expression_type(emitter, iterable);
    fixed_array = false;
    literal_array = iterable_value->kind == VITTE_AST_NODE_ARRAY_EXPR;
    inferred_array_local = false;
    typed_array = false;
    array_literal = literal_array
        ? iterable_value
        : NULL;
    element_type = NULL;
    element_sample = NULL;

    if (iterable_value->kind == VITTE_AST_NODE_BINARY_EXPR &&
        (iterable_value->operator_kind == VITTE_TOKEN_DOT_DOT ||
         iterable_value->operator_kind == VITTE_TOKEN_DOT_DOT_EQUAL)) {
        const vitte_ast_node_t *range_start;
        const vitte_ast_node_t *range_end;
        const vitte_ast_node_t *range_type;
        const char *inferred_range_type;
        bool inclusive;

        range_start = vitte_c17_ast_child(emitter, iterable_value, 0u);
        range_end = vitte_c17_ast_child(emitter, iterable_value, 1u);
        range_type = vitte_c17_ast_expression_type(emitter, range_start);
        inferred_range_type = range_type == NULL
            ? vitte_c17_ast_inferred_type(emitter, range_start)
            : NULL;
        inclusive =
            iterable_value->operator_kind == VITTE_TOKEN_DOT_DOT_EQUAL;
        if (range_start == NULL || range_end == NULL ||
            (range_type == NULL && inferred_range_type == NULL) ||
            !vitte_c17_ast_make_for_temporary_name(
                emitter,
                node,
                "range_start",
                range_start_name,
                sizeof(range_start_name)) ||
            !vitte_c17_ast_make_for_temporary_name(
                emitter,
                node,
                "range_end",
                range_end_name,
                sizeof(range_end_name)) ||
            !vitte_c17_ast_make_for_temporary_name(
                emitter,
                node,
                "continue",
                continue_label,
                sizeof(continue_label))) {
            emitter->failed = true;
            return;
        }

        vitte_c17_ast_write(emitter, "{\n");
        vitte_c17_ast_indent(emitter, indent + 1u);
        if (range_type != NULL) {
            vitte_c17_ast_emit_type(emitter, range_type);
        } else {
            vitte_c17_ast_write(emitter, inferred_range_type);
        }
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_write(emitter, range_start_name);
        vitte_c17_ast_write(emitter, " = ");
        vitte_c17_ast_emit_expression(emitter, range_start);
        vitte_c17_ast_write(emitter, ";\n");
        vitte_c17_ast_indent(emitter, indent + 1u);
        if (range_type != NULL) {
            vitte_c17_ast_emit_type(emitter, range_type);
        } else {
            vitte_c17_ast_write(emitter, inferred_range_type);
        }
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_write(emitter, range_end_name);
        vitte_c17_ast_write(emitter, " = ");
        vitte_c17_ast_emit_expression(emitter, range_end);
        vitte_c17_ast_write(emitter, ";\n");

        vitte_c17_ast_indent(emitter, indent + 1u);
        vitte_c17_ast_write(emitter, "for (;;) {\n");
        vitte_c17_ast_indent(emitter, indent + 2u);
        vitte_c17_ast_write(emitter, "if (");
        vitte_c17_ast_write(emitter, range_start_name);
        vitte_c17_ast_write(emitter, inclusive ? " > " : " >= ");
        vitte_c17_ast_write(emitter, range_end_name);
        vitte_c17_ast_write(emitter, ") break;\n");
        vitte_c17_ast_indent(emitter, indent + 2u);
        if (range_type != NULL) {
            vitte_c17_ast_emit_type(emitter, range_type);
        } else {
            vitte_c17_ast_write(emitter, inferred_range_type);
        }
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_write_token(emitter, binding);
        vitte_c17_ast_write(emitter, " = ");
        vitte_c17_ast_write(emitter, range_start_name);
        vitte_c17_ast_write(emitter, ";\n");
        vitte_c17_ast_indent(emitter, indent + 2u);
        vitte_c17_ast_write(emitter, "{\n");
        if (emitter->loop_defer_depth >=
            sizeof(emitter->loop_defer_bases) /
                sizeof(emitter->loop_defer_bases[0])) {
            emitter->failed = true;
            return;
        }
        emitter->loop_defer_bases[emitter->loop_defer_depth++] =
            emitter->deferred_count;
        (void)snprintf(
            emitter->loop_continue_labels[
                emitter->loop_defer_depth - 1u],
            sizeof(emitter->loop_continue_labels[0]),
            "%s",
            continue_label);
        vitte_c17_ast_emit_block_contents(
            emitter,
            body,
            indent + 3u);
        --emitter->loop_defer_depth;
        vitte_c17_ast_indent(emitter, indent + 2u);
        vitte_c17_ast_write(emitter, "}\n");
        vitte_c17_ast_write(emitter, continue_label);
        vitte_c17_ast_write(emitter, ": ;\n");
        if (inclusive) {
            vitte_c17_ast_indent(emitter, indent + 2u);
            vitte_c17_ast_write(emitter, "if (");
            vitte_c17_ast_write(emitter, range_start_name);
            vitte_c17_ast_write(emitter, " == ");
            vitte_c17_ast_write(emitter, range_end_name);
            vitte_c17_ast_write(emitter, ") break;\n");
        }
        vitte_c17_ast_indent(emitter, indent + 2u);
        vitte_c17_ast_write(emitter, "++");
        vitte_c17_ast_write(emitter, range_start_name);
        vitte_c17_ast_write(emitter, ";\n");
        vitte_c17_ast_indent(emitter, indent + 1u);
        vitte_c17_ast_write(emitter, "}\n");
        vitte_c17_ast_indent(emitter, indent);
        vitte_c17_ast_write(emitter, "}\n");
        return;
    }

    if (array_type != NULL &&
        array_type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
        (array_type->child_count == 1u ||
         array_type->child_count == 2u)) {
        typed_array = true;
        fixed_array = array_type->child_count == 2u;
        element_type = vitte_c17_ast_child(
            emitter,
            array_type,
            array_type->child_count - 1u);
    } else if (literal_array && array_literal->child_count != 0u) {
        element_sample =
            vitte_c17_ast_child(emitter, array_literal, 0u);
        element_type = vitte_c17_ast_expression_type(
            emitter,
            element_sample);
        if (element_type == NULL &&
            vitte_c17_ast_inferred_type(
                emitter,
                element_sample) == NULL) {
            emitter->failed = true;
            return;
        }
    } else if (array_type == NULL &&
               (iterable_value->kind == VITTE_AST_NODE_PATH ||
                iterable_value->kind == VITTE_AST_NODE_IDENTIFIER) &&
               iterable_value->child_count == 1u) {
        const vitte_ast_node_t *name;
        const vitte_ast_node_t *initializer;

        name = vitte_c17_ast_child(emitter, iterable_value, 0u);
        initializer = vitte_c17_ast_find_local_initializer(
            emitter,
            emitter->function,
            name);
        if (initializer == NULL) {
            initializer = vitte_c17_ast_find_local_initializer(
                emitter,
                vitte_c17_ast_node(
                    emitter,
                    vitte_parser_root(emitter->parser)),
                name);
        }
        initializer = vitte_c17_ast_unwrap_groups(emitter, initializer);
        if (initializer != NULL &&
            initializer->kind == VITTE_AST_NODE_ARRAY_EXPR &&
            initializer->child_count != 0u) {
            inferred_array_local = true;
            array_literal = initializer;
            element_sample =
                vitte_c17_ast_child(emitter, initializer, 0u);
            element_type = vitte_c17_ast_expression_type(
                emitter,
                element_sample);
            if (element_type == NULL &&
                vitte_c17_ast_inferred_type(
                    emitter,
                    element_sample) == NULL) {
                emitter->failed = true;
                return;
            }
        }
    }

    if (!literal_array && !inferred_array_local && !typed_array) {
        emitter->failed = true;
        return;
    }
    if (element_type == NULL && element_sample == NULL) {
        emitter->failed = true;
        return;
    }
    if (literal_array && array_literal->child_count == 0u) {
        emitter->failed = true;
        return;
    }

    dynamic_lvalue = !literal_array && !fixed_array &&
        (typed_array || inferred_array_local) &&
        vitte_c17_ast_expression_is_lvalue(emitter, iterable);

    vitte_c17_ast_write(emitter, "{\n");

    if (literal_array) {
        vitte_c17_ast_indent(emitter, indent + 1u);
        vitte_c17_ast_emit_for_element_type(
            emitter,
            element_type,
            element_sample);
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, "[] = {");
        for (index = 0u; index < array_literal->child_count; ++index) {
            if (index != 0u) {
                vitte_c17_ast_write(emitter, ", ");
            }
            vitte_c17_ast_emit_expression(
                emitter,
                vitte_c17_ast_child(emitter, array_literal, index));
        }
        vitte_c17_ast_write(emitter, "};\n");
    } else if (fixed_array) {
        vitte_c17_ast_indent(emitter, indent + 1u);
        vitte_c17_ast_emit_type(emitter, element_type);
        vitte_c17_ast_write(emitter, " (*");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, ")[");
        vitte_c17_ast_emit_expression(
            emitter,
            vitte_c17_ast_child(emitter, array_type, 0u));
        vitte_c17_ast_write(emitter, "] = &(");
        vitte_c17_ast_emit_expression(emitter, iterable);
        vitte_c17_ast_write(emitter, ");\n");
    } else if (dynamic_lvalue) {
        vitte_c17_ast_indent(emitter, indent + 1u);
        vitte_c17_ast_write(emitter, "VitteArray *");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, " = &(");
        vitte_c17_ast_emit_expression(emitter, iterable);
        vitte_c17_ast_write(emitter, ");\n");
    } else {
        vitte_c17_ast_indent(emitter, indent + 1u);
        vitte_c17_ast_write(emitter, "VitteArray ");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, " = ");
        vitte_c17_ast_emit_expression(emitter, iterable);
        vitte_c17_ast_write(emitter, ";\n");
    }

    vitte_c17_ast_indent(emitter, indent + 1u);
    vitte_c17_ast_write(emitter, "for (size_t ");
    vitte_c17_ast_write(emitter, index_name);
    vitte_c17_ast_write(emitter, " = 0u; ");
    vitte_c17_ast_write(emitter, index_name);
    vitte_c17_ast_write(emitter, " < ");
    if (literal_array) {
        vitte_c17_ast_write(emitter, "sizeof(");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, ") / sizeof(");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, "[0])");
    } else if (fixed_array) {
        vitte_c17_ast_write(emitter, "sizeof(*");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, ") / sizeof((*");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, ")[0])");
    } else {
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(
            emitter,
            dynamic_lvalue ? "->length" : ".length");
    }
    vitte_c17_ast_write(emitter, "; ++");
    vitte_c17_ast_write(emitter, index_name);
    vitte_c17_ast_write(emitter, ") {\n");

    vitte_c17_ast_indent(emitter, indent + 2u);
    vitte_c17_ast_emit_for_element_type(
        emitter,
        element_type,
        element_sample);
    vitte_c17_ast_write(emitter, " ");
    vitte_c17_ast_write_token(emitter, binding);
    vitte_c17_ast_write(emitter, " = ");
    if (literal_array) {
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, "[");
    } else if (fixed_array) {
        vitte_c17_ast_write(emitter, "(*");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(emitter, ")[");
    } else {
        vitte_c17_ast_write(emitter, "((");
        vitte_c17_ast_emit_for_element_type(
            emitter,
            element_type,
            element_sample);
        vitte_c17_ast_write(emitter, " *)");
        vitte_c17_ast_write(emitter, array_name);
        vitte_c17_ast_write(
            emitter,
            dynamic_lvalue ? "->data)[" : ".data)[");
    }
    vitte_c17_ast_write(emitter, index_name);
    vitte_c17_ast_write(emitter, "];\n");

    vitte_c17_ast_indent(emitter, indent + 2u);
    vitte_c17_ast_write(emitter, "{\n");
    if (emitter->loop_defer_depth >=
        sizeof(emitter->loop_defer_bases) /
            sizeof(emitter->loop_defer_bases[0])) {
        emitter->failed = true;
        return;
    }
    emitter->loop_defer_bases[emitter->loop_defer_depth++] =
        emitter->deferred_count;
    emitter->loop_continue_labels[
        emitter->loop_defer_depth - 1u][0] = '\0';
    vitte_c17_ast_emit_block_contents(
        emitter,
        body,
        indent + 3u);
    --emitter->loop_defer_depth;
    vitte_c17_ast_indent(emitter, indent + 2u);
    vitte_c17_ast_write(emitter, "}\n");
    vitte_c17_ast_indent(emitter, indent + 1u);
    vitte_c17_ast_write(emitter, "}\n");
    vitte_c17_ast_indent(emitter, indent);
    vitte_c17_ast_write(emitter, "}\n");
}

static bool
vitte_c17_ast_debug_scalar_type_name(
    const char *name)
{
    return name != NULL &&
           (strcmp(name, "i64") == 0 ||
            strcmp(name, "u64") == 0 ||
            strcmp(name, "u8") == 0 ||
            strcmp(name, "usize") == 0 ||
            strcmp(name, "int") == 0 ||
            strcmp(name, "char") == 0 ||
            strcmp(name, "bool") == 0 ||
            strcmp(name, "int64_t") == 0 ||
            strcmp(name, "uint64_t") == 0 ||
            strcmp(name, "uint8_t") == 0 ||
            strcmp(name, "size_t") == 0 ||
            strcmp(name, "int32_t") == 0);
}

static bool
vitte_c17_ast_debug_scalar_let(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *type;
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *name;
    const vitte_ast_node_t *initializer;
    const char *inferred_type;

    if (node == NULL ||
        node->kind != VITTE_AST_NODE_LET_STMT ||
        node->child_count < 2u) {
        return false;
    }

    type = vitte_c17_ast_child(emitter, node, 1u);
    if (type != NULL && type->kind == VITTE_AST_NODE_TYPE_EXPR) {
        if (node->child_count < 3u) {
            return false;
        }
        path = vitte_c17_ast_child(emitter, type, 0u);
        if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
            path->child_count == 0u) {
            return false;
        }
        name = vitte_c17_ast_child(
            emitter,
            path,
            path->child_count - 1u);
        return vitte_c17_ast_token_is(emitter, name, "i64") ||
               vitte_c17_ast_token_is(emitter, name, "u64") ||
               vitte_c17_ast_token_is(emitter, name, "u8") ||
               vitte_c17_ast_token_is(emitter, name, "usize") ||
               vitte_c17_ast_token_is(emitter, name, "int") ||
               vitte_c17_ast_token_is(emitter, name, "char") ||
               vitte_c17_ast_token_is(emitter, name, "bool");
    }

    if (type == NULL || type->kind == VITTE_AST_NODE_ARRAY_TYPE ||
        type->kind == VITTE_AST_NODE_POINTER_TYPE ||
        type->kind == VITTE_AST_NODE_REFERENCE_TYPE) {
        return false;
    }

    initializer = type;
    inferred_type = vitte_c17_ast_inferred_type(emitter, initializer);
    return vitte_c17_ast_debug_scalar_type_name(inferred_type);
}

static bool
vitte_c17_ast_debug_name_is_known(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name)
{
    const vitte_token_t *token;
    size_t index;

    token = vitte_c17_ast_token(emitter, name);
    if (token == NULL) {
        return false;
    }
    for (index = 0u; index < emitter->debug_scalar_name_count; ++index) {
        if (emitter->debug_scalar_names[index].length == token->length &&
            memcmp(
                emitter->debug_scalar_names[index].lexeme,
                token->lexeme,
                token->length) == 0) {
            return true;
        }
    }
    return false;
}

static void
vitte_c17_ast_debug_remember_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name)
{
    const vitte_token_t *token;

    if (emitter == NULL ||
        emitter->debug_scalar_name_count >=
            sizeof(emitter->debug_scalar_names) /
                sizeof(emitter->debug_scalar_names[0])) {
        return;
    }
    token = vitte_c17_ast_token(emitter, name);
    if (token == NULL || vitte_c17_ast_debug_name_is_known(emitter, name)) {
        return;
    }
    emitter->debug_scalar_names[emitter->debug_scalar_name_count].lexeme =
        token->lexeme;
    emitter->debug_scalar_names[emitter->debug_scalar_name_count].length =
        token->length;
    emitter->debug_scalar_name_count++;
}

static const vitte_ast_node_t *
vitte_c17_ast_debug_assignment_name(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *expression)
{
    const vitte_ast_node_t *left;

    if (expression == NULL ||
        expression->kind != VITTE_AST_NODE_ASSIGN_EXPR) {
        return NULL;
    }
    left = vitte_c17_ast_child(emitter, expression, 0u);
    if (left == NULL) {
        return NULL;
    }
    if (left->kind == VITTE_AST_NODE_IDENTIFIER) {
        return left;
    }
    if (left->kind == VITTE_AST_NODE_PATH && left->child_count == 1u) {
        return vitte_c17_ast_child(emitter, left, 0u);
    }
    return NULL;
}

static void
vitte_c17_ast_emit_debug_scalar_update(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *name,
    unsigned int indent,
    bool remember)
{
    const vitte_ast_node_t *function_name;

    if (emitter == NULL || !emitter->debug_runtime || name == NULL) {
        return;
    }
    if (remember) {
        vitte_c17_ast_debug_remember_name(emitter, name);
    }
    if (!vitte_c17_ast_debug_name_is_known(emitter, name)) {
        return;
    }

    function_name = emitter->function != NULL
        ? vitte_c17_ast_child(emitter, emitter->function, 0u)
        : NULL;
    vitte_c17_ast_indent(emitter, indent);
    vitte_c17_ast_write(emitter, "vitte_debug_set_i64(\"");
    if (function_name != NULL) {
        vitte_c17_ast_write_token(emitter, function_name);
    } else {
        vitte_c17_ast_write(emitter, "main");
    }
    vitte_c17_ast_write(emitter, "\", \"");
    vitte_c17_ast_write_token(emitter, name);
    vitte_c17_ast_write(emitter, "\", (int64_t)(");
    vitte_c17_ast_write_token(emitter, name);
    vitte_c17_ast_write(emitter, "));\n");
}

static bool
vitte_c17_ast_match_pattern_is_wildcard(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pattern)
{
    if (pattern == NULL) {
        return false;
    }
    if (pattern->kind == VITTE_AST_NODE_PATTERN &&
        pattern->child_count == 0u &&
        (pattern->flags & VITTE_AST_FLAG_PATTERN_HEAD) != 0u) {
        return true;
    }
    return pattern->kind == VITTE_AST_NODE_PATH &&
           pattern->child_count == 1u &&
           vitte_c17_ast_token_is(
               emitter,
               vitte_c17_ast_child(emitter, pattern, 0u),
               "_");
}

static const vitte_ast_node_t *
vitte_c17_ast_match_binding(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pattern)
{
    if (pattern != NULL && pattern->kind == VITTE_AST_NODE_PATTERN &&
        pattern->child_count == 1u &&
        (pattern->flags & VITTE_AST_FLAG_PATTERN_MUTABLE) != 0u) {
        pattern = vitte_c17_ast_child(emitter, pattern, 0u);
    }
    if (pattern == NULL || pattern->kind != VITTE_AST_NODE_PATH ||
        pattern->child_count != 1u ||
        vitte_c17_ast_match_pattern_is_wildcard(emitter, pattern)) {
        return NULL;
    }
    return pattern;
}

static bool
vitte_c17_ast_match_or_has_binding(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pattern)
{
    size_t index;

    if (pattern == NULL || pattern->kind != VITTE_AST_NODE_PATTERN ||
        (pattern->flags & VITTE_AST_FLAG_PATTERN_OR) == 0u) {
        return false;
    }
    for (index = 0u; index < pattern->child_count; ++index) {
        const vitte_ast_node_t *alternative =
            vitte_c17_ast_child(emitter, pattern, index);
        if (vitte_c17_ast_match_binding(emitter, alternative) != NULL ||
            vitte_c17_ast_match_or_has_binding(emitter, alternative)) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_match_supported_scalar_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const char *inferred_type)
{
    static const char *const names[] = {
        "bool", "char", "i8", "i16", "i32", "i64", "int",
        "u8", "u16", "u32", "u64", "usize", "f32", "f64",
        "str", "string"
    };
    size_t index;

    if (type == NULL) {
        return inferred_type != NULL &&
               (strcmp(inferred_type, "int64_t") == 0 ||
                strcmp(inferred_type, "double") == 0 ||
                strcmp(inferred_type, "bool") == 0 ||
                strcmp(inferred_type, "char") == 0 ||
                strcmp(inferred_type, "const char *") == 0);
    }
    if (vitte_c17_ast_pick_decl_for_type(emitter, type) != NULL) {
        return true;
    }
    if (type->kind == VITTE_AST_NODE_POINTER_TYPE ||
        type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
        type->kind == VITTE_AST_NODE_OPTIONAL_TYPE) {
        return true;
    }
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (vitte_c17_ast_type_is_named(emitter, type, names[index])) {
            return true;
        }
    }
    return false;
}

static void
vitte_c17_ast_emit_match_pattern_test(
    vitte_c17_ast_emitter_t *emitter,
    const char *subject_name,
    const vitte_ast_node_t *subject_type,
    const char *inferred_type,
    const vitte_ast_node_t *pattern)
{
    size_t index;

    if (pattern == NULL) {
        emitter->failed = true;
        return;
    }
    if (vitte_c17_ast_match_pattern_is_wildcard(emitter, pattern) ||
        vitte_c17_ast_match_binding(emitter, pattern) != NULL) {
        vitte_c17_ast_write(emitter, "1");
        return;
    }
    if (pattern->kind == VITTE_AST_NODE_PATTERN &&
        (pattern->flags & VITTE_AST_FLAG_PATTERN_OR) != 0u &&
        pattern->child_count != 0u) {
        vitte_c17_ast_write(emitter, "(");
        for (index = 0u; index < pattern->child_count; ++index) {
            if (index != 0u) {
                vitte_c17_ast_write(emitter, " || ");
            }
            vitte_c17_ast_emit_match_pattern_test(
                emitter,
                subject_name,
                subject_type,
                inferred_type,
                vitte_c17_ast_child(emitter, pattern, index));
        }
        vitte_c17_ast_write(emitter, ")");
        return;
    }
    if (pattern->kind == VITTE_AST_NODE_PATTERN &&
        (pattern->flags & VITTE_AST_FLAG_PATTERN_RANGE) != 0u &&
        pattern->child_count == 2u) {
        if (vitte_c17_ast_is_string_type(emitter, subject_type) ||
            (subject_type != NULL &&
             (subject_type->kind == VITTE_AST_NODE_POINTER_TYPE ||
              subject_type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
              subject_type->kind == VITTE_AST_NODE_OPTIONAL_TYPE))) {
            emitter->failed = true;
            return;
        }
        vitte_c17_ast_write(emitter, "(");
        vitte_c17_ast_write(emitter, subject_name);
        vitte_c17_ast_write(emitter, " >= ");
        vitte_c17_ast_emit_expression(
            emitter,
            vitte_c17_ast_child(emitter, pattern, 0u));
        vitte_c17_ast_write(emitter, " && ");
        vitte_c17_ast_write(emitter, subject_name);
        vitte_c17_ast_write(
            emitter,
            pattern->operator_kind == VITTE_TOKEN_DOT_DOT_EQUAL
                ? " <= "
                : " < ");
        vitte_c17_ast_emit_expression(
            emitter,
            vitte_c17_ast_child(emitter, pattern, 1u));
        vitte_c17_ast_write(emitter, ")");
        return;
    }
    {
        const vitte_ast_node_t *pick_type =
            vitte_c17_ast_pick_decl_for_type(emitter, subject_type);
        const vitte_ast_node_t *variant_path = NULL;
        const vitte_ast_node_t *pick = NULL;
        const vitte_ast_node_t *variant = NULL;
        bool constructor_pattern = pattern->kind == VITTE_AST_NODE_PATTERN &&
            (pattern->flags & VITTE_AST_FLAG_PATTERN_HEAD) != 0u &&
            pattern->child_count != 0u;

        if (constructor_pattern) {
            variant_path = vitte_c17_ast_child(emitter, pattern, 0u);
        } else if (pattern->kind == VITTE_AST_NODE_PATH) {
            variant_path = pattern;
        } else if (pattern->kind == VITTE_AST_NODE_MEMBER_EXPR) {
            variant_path = pattern;
        }
        if (variant_path != NULL && pick_type != NULL &&
            variant_path->kind == VITTE_AST_NODE_MEMBER_EXPR) {
            const vitte_ast_node_t *generic_pick = NULL;
            const vitte_ast_node_t *generic_variant = NULL;
            const vitte_c17_ast_generic_form_instance_t *generic_instance = NULL;
            const vitte_c17_ast_generic_form_instance_t *subject_instance =
                vitte_c17_ast_generic_form_instance_for_type(
                    emitter, subject_type);
            if (vitte_c17_ast_resolve_generic_pick_variant_member(
                    emitter,
                    variant_path,
                    &generic_pick,
                    &generic_variant,
                    &generic_instance)) {
                size_t payload_index;
                size_t payload_pattern_count = constructor_pattern
                    ? pattern->child_count - 1u
                    : 0u;
                if (emitter->failed || generic_instance == NULL ||
                    generic_pick != pick_type ||
                    subject_instance != generic_instance ||
                    payload_pattern_count !=
                        generic_variant->child_count - 1u) {
                    emitter->failed = true;
                    return;
                }
                for (payload_index = 0u;
                     payload_index < payload_pattern_count;
                     ++payload_index) {
                    const vitte_ast_node_t *payload_pattern =
                        vitte_c17_ast_child(
                            emitter, pattern, payload_index + 1u);
                    if (!vitte_c17_ast_match_pattern_is_wildcard(
                            emitter, payload_pattern) &&
                        vitte_c17_ast_match_binding(
                            emitter, payload_pattern) == NULL) {
                        emitter->failed = true;
                        return;
                    }
                }
                vitte_c17_ast_write(emitter, "(");
                vitte_c17_ast_write(emitter, subject_name);
                vitte_c17_ast_write(emitter, ".tag == ");
                vitte_c17_ast_emit_generic_form_instance_name(
                    emitter, generic_instance);
                vitte_c17_ast_write(emitter, "_tag_");
                vitte_c17_ast_write_token(
                    emitter,
                    vitte_c17_ast_child(emitter, generic_variant, 0u));
                vitte_c17_ast_write(emitter, ")");
                return;
            }
        }
        if (variant_path != NULL && pick_type != NULL &&
            vitte_c17_ast_resolve_pick_variant_path(
                emitter, variant_path, &pick, &variant)) {
            size_t payload_index;
            size_t payload_pattern_count = constructor_pattern
                ? pattern->child_count - 1u
                : 0u;
            if (pick != pick_type ||
                payload_pattern_count != variant->child_count - 1u) {
                emitter->failed = true;
                return;
            }
            for (payload_index = 0u;
                 payload_index < payload_pattern_count;
                 ++payload_index) {
                const vitte_ast_node_t *payload_pattern =
                    vitte_c17_ast_child(
                        emitter, pattern, payload_index + 1u);
                if (!vitte_c17_ast_match_pattern_is_wildcard(
                        emitter, payload_pattern) &&
                    vitte_c17_ast_match_binding(
                        emitter, payload_pattern) == NULL) {
                    emitter->failed = true;
                    return;
                }
            }
            vitte_c17_ast_write(emitter, "(");
            vitte_c17_ast_write(emitter, subject_name);
            if (vitte_c17_ast_pick_has_payload(emitter, pick)) {
                vitte_c17_ast_write(emitter, ".tag == ");
                vitte_c17_ast_emit_declaration_name(
                    emitter,
                    pick,
                    vitte_c17_ast_child(emitter, pick, 0u));
                vitte_c17_ast_write(emitter, "_tag_");
            } else {
                vitte_c17_ast_write(emitter, " == ");
                vitte_c17_ast_emit_declaration_name(
                    emitter,
                    pick,
                    vitte_c17_ast_child(emitter, pick, 0u));
                vitte_c17_ast_write(emitter, "_");
            }
            vitte_c17_ast_write_token(
                emitter,
                vitte_c17_ast_child(emitter, variant, 0u));
            vitte_c17_ast_write(emitter, ")");
            return;
        }
    }
    if (pattern->kind == VITTE_AST_NODE_STRING_LITERAL &&
        (vitte_c17_ast_is_string_type(emitter, subject_type) ||
         (subject_type == NULL && inferred_type != NULL &&
          strcmp(inferred_type, "const char *") == 0))) {
        vitte_c17_ast_write(emitter, "(");
        vitte_c17_ast_write(emitter, subject_name);
        vitte_c17_ast_write(emitter, " != NULL && strcmp(");
        vitte_c17_ast_write(emitter, subject_name);
        vitte_c17_ast_write(emitter, ", ");
        vitte_c17_ast_emit_expression(emitter, pattern);
        vitte_c17_ast_write(emitter, ") == 0)");
        return;
    }
    if (subject_type != NULL &&
        (subject_type->kind == VITTE_AST_NODE_POINTER_TYPE ||
         subject_type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
         subject_type->kind == VITTE_AST_NODE_OPTIONAL_TYPE) &&
        pattern->kind != VITTE_AST_NODE_NULL_LITERAL) {
        emitter->failed = true;
        return;
    }
    if (pattern->kind == VITTE_AST_NODE_INTEGER_LITERAL ||
        pattern->kind == VITTE_AST_NODE_FLOAT_LITERAL ||
        pattern->kind == VITTE_AST_NODE_BOOL_LITERAL ||
        pattern->kind == VITTE_AST_NODE_CHARACTER_LITERAL ||
        pattern->kind == VITTE_AST_NODE_NULL_LITERAL ||
        pattern->kind == VITTE_AST_NODE_PATH) {
        vitte_c17_ast_write(emitter, "(");
        vitte_c17_ast_write(emitter, subject_name);
        vitte_c17_ast_write(emitter, " == ");
        vitte_c17_ast_emit_expression(emitter, pattern);
        vitte_c17_ast_write(emitter, ")");
        return;
    }
    emitter->failed = true;
}

static bool
vitte_c17_ast_make_match_temporary_name(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    const char *role,
    char *buffer,
    size_t capacity)
{
    size_t attempt;

    for (attempt = 0u; attempt <= emitter->parser->token_count; ++attempt) {
        int length = snprintf(
            buffer,
            capacity,
            "vitte_internal_match_%llu_%s_%zu",
            (unsigned long long)node->id,
            role,
            attempt);
        if (length < 0 || (size_t)length >= capacity) {
            return false;
        }
        if (!vitte_c17_ast_generated_name_is_used(
                emitter, buffer, (size_t)length)) {
            return true;
        }
    }
    return false;
}

static void
vitte_c17_ast_emit_statement(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    unsigned int indent)
{
    const vitte_ast_node_t *child;
    size_t index;

    if (node == NULL || emitter->failed) {
        emitter->failed = true;
        return;
    }

    vitte_c17_ast_emit_source_line(emitter, node);
    if (emitter->debug_runtime && node->span.line != 0u) {
        const vitte_ast_node_t *function_name;

        function_name = emitter->function != NULL
            ? vitte_c17_ast_child(emitter, emitter->function, 0u)
            : NULL;
        vitte_c17_ast_indent(emitter, indent);
        vitte_c17_ast_write(emitter, "vitte_debug_probe(");
        if (fprintf(
                emitter->output,
                "%zuu, \"",
                node->span.line) < 0) {
            emitter->failed = true;
            return;
        }
        if (function_name != NULL) {
            vitte_c17_ast_write_token(emitter, function_name);
        } else {
            vitte_c17_ast_write(emitter, "main");
        }
        vitte_c17_ast_write(emitter, "\");\n");
        if (emitter->failed) {
            emitter->failed = true;
            return;
        }
    }
    vitte_c17_ast_indent(emitter, indent);
    switch (node->kind) {
        case VITTE_AST_NODE_BLOCK:
            vitte_c17_ast_write(emitter, "{\n");
            vitte_c17_ast_emit_block_contents(
                emitter,
                node,
                indent + 1u);
            vitte_c17_ast_indent(emitter, indent);
            vitte_c17_ast_write(emitter, "}\n");
            return;

        case VITTE_AST_NODE_LET_STMT:
            child = vitte_c17_ast_child(emitter, node, 0u);
            if (child == NULL) {
                emitter->failed = true;
                return;
            }
            if (vitte_c17_ast_token_is(emitter, child, "_")) {
                const vitte_ast_node_t *initializer = NULL;

                for (index = 1u; index < node->child_count; ++index) {
                    const vitte_ast_node_t *candidate =
                        vitte_c17_ast_child(emitter, node, index);

                    if (candidate != NULL &&
                        candidate->kind != VITTE_AST_NODE_TYPE_EXPR &&
                        candidate->kind != VITTE_AST_NODE_POINTER_TYPE &&
                        candidate->kind != VITTE_AST_NODE_REFERENCE_TYPE &&
                        candidate->kind != VITTE_AST_NODE_ARRAY_TYPE &&
                        candidate->kind != VITTE_AST_NODE_OPTIONAL_TYPE) {
                        initializer = candidate;
                    }
                }
                if (initializer != NULL) {
                    vitte_c17_ast_write(emitter, "(void)(");
                    vitte_c17_ast_emit_expression(emitter, initializer);
                    vitte_c17_ast_write(emitter, ");\n");
                }
                return;
            }
            if (node->child_count > 1u &&
                vitte_c17_ast_child(emitter, node, 1u) != NULL &&
                (vitte_c17_ast_child(emitter, node, 1u)->kind ==
                     VITTE_AST_NODE_TYPE_EXPR ||
                 vitte_c17_ast_child(emitter, node, 1u)->kind ==
                     VITTE_AST_NODE_POINTER_TYPE ||
                 vitte_c17_ast_child(emitter, node, 1u)->kind ==
                     VITTE_AST_NODE_REFERENCE_TYPE ||
                 vitte_c17_ast_child(emitter, node, 1u)->kind ==
                     VITTE_AST_NODE_ARRAY_TYPE ||
                 vitte_c17_ast_child(emitter, node, 1u)->kind ==
                     VITTE_AST_NODE_OPTIONAL_TYPE)) {
                vitte_c17_ast_emit_declarator(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 1u),
                    child);
                index = 2u;
            } else if (node->child_count > 1u) {
                const vitte_ast_node_t *initializer;

                initializer =
                    vitte_c17_ast_child(
                        emitter,
                        node,
                        1u);
                if (initializer != NULL &&
                    initializer->kind == VITTE_AST_NODE_CALL_EXPR) {
                    const vitte_ast_node_t *return_type;
                    const vitte_ast_node_t *async_origin;
                    const char *inferred_call_type;

                    async_origin = vitte_c17_ast_async_task_origin(
                        emitter, initializer, 0u);
                    if (async_origin != NULL) {
                        vitte_c17_ast_write_async_task_type_name(
                            emitter, async_origin);
                    } else {
                    inferred_call_type =
                        vitte_c17_ast_infer_scalar_c_type(
                            emitter,
                            initializer,
                            initializer);
                    if (inferred_call_type == NULL) {
                        return_type =
                            vitte_c17_ast_expression_type(
                                emitter,
                                initializer);
                        if (return_type == NULL) {
                            if (vitte_c17_ast_is_string_builtin_call(
                                    emitter,
                                    initializer)) {
                                /* Built-in string constructors lower to the
                                 * backend's `const char *` representation. */
                                vitte_c17_ast_write(
                                    emitter, "const char *");
                            } else {
                                emitter->failed = true;
                                return;
                            }
                        } else {
                            vitte_c17_ast_emit_type(emitter, return_type);
                        }
                    } else {
                        vitte_c17_ast_write(emitter, inferred_call_type);
                    }
                    }
                } else {
                    const vitte_ast_node_t *inferred_ast_type;
                    const char *inferred_type;

                    inferred_ast_type =
                        vitte_c17_ast_expression_type(
                            emitter,
                            initializer);
                    if (inferred_ast_type != NULL) {
                        vitte_c17_ast_emit_type(
                            emitter,
                            inferred_ast_type);
                    } else if (initializer != NULL &&
                        initializer->kind == VITTE_AST_NODE_INDEX_EXPR &&
                        vitte_c17_ast_expression_is_string(
                            emitter,
                            vitte_c17_ast_child(
                                emitter,
                                initializer,
                                0u))) {
                        /* String indexing yields one UTF-8 byte. */
                        vitte_c17_ast_write(emitter, "uint8_t");
                    } else if (initializer != NULL &&
                        initializer->kind == VITTE_AST_NODE_UNARY_EXPR &&
                        initializer->operator_kind == VITTE_TOKEN_AMP) {
                        const vitte_ast_node_t *operand_type;

                        operand_type = vitte_c17_ast_expression_type(
                            emitter,
                            vitte_c17_ast_child(
                                emitter,
                                initializer,
                                0u));
                        if (operand_type == NULL) {
                            emitter->failed = true;
                            return;
                        }
                        vitte_c17_ast_emit_type(emitter, operand_type);
                        vitte_c17_ast_write(emitter, " *");
                    } else if (initializer != NULL &&
                        initializer->kind == VITTE_AST_NODE_UNARY_EXPR &&
                        initializer->operator_kind == VITTE_TOKEN_STAR) {
                        const vitte_ast_node_t *operand_type;

                        operand_type = vitte_c17_ast_expression_type(
                            emitter,
                            vitte_c17_ast_child(
                                emitter,
                                initializer,
                                0u));
                        if (operand_type == NULL ||
                            (operand_type->kind !=
                                 VITTE_AST_NODE_POINTER_TYPE &&
                             operand_type->kind !=
                                 VITTE_AST_NODE_REFERENCE_TYPE)) {
                            emitter->failed = true;
                            return;
                        }
                        vitte_c17_ast_emit_type(
                            emitter,
                            vitte_c17_ast_child(
                                emitter,
                                operand_type,
                                0u));
                    } else {
                        inferred_type =
                            vitte_c17_ast_inferred_type(
                                emitter,
                                initializer);
                        if (inferred_type == NULL) {
                            emitter->failed = true;
                            return;
                        }
                        vitte_c17_ast_write(emitter, inferred_type);
                    }
                }
                vitte_c17_ast_write(emitter, " ");
                vitte_c17_ast_write_token(emitter, child);
                index = 1u;
            } else {
                emitter->failed = true;
                return;
            }
            if (index < node->child_count) {
                const vitte_ast_node_t *initializer;
                initializer = vitte_c17_ast_child(emitter, node, index);
                if (initializer != NULL &&
                    initializer->kind == VITTE_AST_NODE_IF_EXPR) {
                    vitte_c17_ast_write(emitter, ";\n");
                    vitte_c17_ast_emit_if_expression_into(
                        emitter,
                        initializer,
                        child,
                        indent);
                    if (vitte_c17_ast_debug_scalar_let(emitter, node)) {
                        vitte_c17_ast_emit_debug_scalar_update(
                            emitter,
                            child,
                            indent,
                            true);
                    }
                    return;
                }
                vitte_c17_ast_write(emitter, " = ");
                {
                    const vitte_ast_node_t *declared_type =
                        node->child_count > 1u
                            ? vitte_c17_ast_child(emitter, node, 1u)
                            : NULL;
                    const vitte_ast_node_t *slice_element =
                        declared_type != NULL &&
                                declared_type->kind ==
                                    VITTE_AST_NODE_ARRAY_TYPE &&
                                declared_type->child_count == 1u
                            ? vitte_c17_ast_child(
                                  emitter, declared_type, 0u)
                            : vitte_c17_ast_generic_alias_slice_element_recursive(
                                  emitter, declared_type, 0u);
                if (initializer != NULL &&
                    initializer->kind == VITTE_AST_NODE_ARRAY_EXPR &&
                    slice_element != NULL) {
                    const vitte_ast_node_t *element_type;
                    size_t item_index;

                    element_type = slice_element;
                    if (initializer->child_count == 0u) {
                        vitte_c17_ast_write(
                            emitter,
                            "(VitteArray){0}");
                    } else {
                        vitte_c17_ast_write(
                            emitter,
                            "vitte_array_from((");
                        vitte_c17_ast_emit_type(
                            emitter,
                            element_type);
                        vitte_c17_ast_write(
                            emitter,
                            "[]){");
                        for (item_index = 0u;
                             item_index < initializer->child_count;
                             ++item_index) {
                            if (item_index != 0u) {
                                vitte_c17_ast_write(
                                    emitter,
                                    ", ");
                            }
                            vitte_c17_ast_emit_expression(
                                emitter,
                                vitte_c17_ast_child(
                                    emitter,
                                    initializer,
                                    item_index));
                        }
                        vitte_c17_ast_write(
                            emitter,
                            "}, ");
                        if (fprintf(
                                emitter->output,
                                "%zuu, sizeof(",
                                initializer->child_count) < 0) {
                            emitter->failed = true;
                        }
                        vitte_c17_ast_emit_type(
                            emitter,
                            element_type);
                        vitte_c17_ast_write(
                            emitter,
                            "))");
                    }
                } else {
                    vitte_c17_ast_emit_expression(
                        emitter,
                        initializer);
                }
                }
            }
            vitte_c17_ast_write(emitter, ";\n");
            if (vitte_c17_ast_debug_scalar_let(emitter, node)) {
                vitte_c17_ast_emit_debug_scalar_update(
                    emitter,
                    child,
                    indent,
                    true);
            }
            return;

        case VITTE_AST_NODE_EXPR_STMT:
        case VITTE_AST_NODE_RETURN_STMT:
            if (node->kind == VITTE_AST_NODE_RETURN_STMT) {
                const vitte_ast_node_t *return_type;

                return_type = emitter->function != NULL
                    ? vitte_c17_ast_function_return_type(
                          emitter,
                          emitter->function)
                    : NULL;
                if (emitter->deferred_count != 0u &&
                    node->child_count != 0u &&
                    return_type != NULL &&
                    !vitte_c17_ast_type_is_named(
                        emitter, return_type, "void") &&
                    !vitte_c17_ast_type_is_named(
                        emitter, return_type, "unit") &&
                    !(return_type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
                      return_type->child_count == 1u) &&
                    vitte_c17_ast_generic_alias_slice_element_recursive(
                        emitter, return_type, 0u) == NULL) {
                    vitte_c17_ast_emit_type(emitter, return_type);
                    if (fprintf(
                            emitter->output,
                            " vitte_internal_return_%llu = ",
                            (unsigned long long)node->id) < 0) {
                        emitter->failed = true;
                        return;
                    }
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 0u));
                    vitte_c17_ast_write(emitter, ";\n");
                    vitte_c17_ast_emit_deferred_actions(
                        emitter, 0u, indent);
                    vitte_c17_ast_indent(emitter, indent);
                    if (fprintf(
                            emitter->output,
                            "return vitte_internal_return_%llu;\n",
                            (unsigned long long)node->id) < 0) {
                        emitter->failed = true;
                    }
                    return;
                }
                if (node->child_count != 0u &&
                    (return_type == NULL ||
                     vitte_c17_ast_type_is_named(
                         emitter, return_type, "void") ||
                     vitte_c17_ast_type_is_named(
                         emitter, return_type, "unit"))) {
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, node, 0u));
                    vitte_c17_ast_write(emitter, ";\n");
                    vitte_c17_ast_emit_deferred_actions(
                        emitter, 0u, indent);
                    vitte_c17_ast_indent(emitter, indent);
                    vitte_c17_ast_write(emitter, "return;\n");
                    return;
                }
                if (node->child_count != 0u) {
                    const vitte_ast_node_t *return_value;
                    const vitte_ast_node_t *slice_element;

                    return_value = vitte_c17_ast_child(emitter, node, 0u);
                    slice_element = return_type != NULL &&
                            return_type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
                            return_type->child_count == 1u
                        ? vitte_c17_ast_child(emitter, return_type, 0u)
                        : vitte_c17_ast_generic_alias_slice_element_recursive(
                              emitter, return_type, 0u);
                    if (return_value != NULL &&
                        return_value->kind == VITTE_AST_NODE_ARRAY_EXPR &&
                        slice_element != NULL) {
                        size_t item_index;

                        if (emitter->deferred_count != 0u) {
                            vitte_c17_ast_write(
                                emitter, "VitteArray vitte_internal_return_");
                            if (fprintf(
                                    emitter->output,
                                    "%llu = ",
                                    (unsigned long long)node->id) < 0) {
                                emitter->failed = true;
                                return;
                            }
                        } else {
                            vitte_c17_ast_write(emitter, "return ");
                        }
                        if (return_value->child_count == 0u) {
                            vitte_c17_ast_write(
                                emitter, "(VitteArray){0}");
                        } else {
                            vitte_c17_ast_write(
                                emitter, "vitte_array_from((");
                            vitte_c17_ast_emit_type(
                                emitter, slice_element);
                            vitte_c17_ast_write(emitter, "[]){");
                            for (item_index = 0u;
                                 item_index < return_value->child_count;
                                 ++item_index) {
                                if (item_index != 0u) {
                                    vitte_c17_ast_write(emitter, ", ");
                                }
                                vitte_c17_ast_emit_expression(
                                    emitter,
                                    vitte_c17_ast_child(
                                        emitter, return_value, item_index));
                            }
                            if (fprintf(
                                    emitter->output,
                                    "}, %zuu, sizeof(",
                                    return_value->child_count) < 0) {
                                emitter->failed = true;
                            }
                            vitte_c17_ast_emit_type(
                                emitter, slice_element);
                            vitte_c17_ast_write(emitter, "))");
                        }
                        vitte_c17_ast_write(emitter, ";\n");
                        if (emitter->deferred_count != 0u) {
                            vitte_c17_ast_emit_deferred_actions(
                                emitter, 0u, indent);
                            vitte_c17_ast_indent(emitter, indent);
                            if (fprintf(
                                    emitter->output,
                                    "return vitte_internal_return_%llu;\n",
                                    (unsigned long long)node->id) < 0) {
                                emitter->failed = true;
                            }
                        }
                        return;
                    }
                }
                if (node->child_count == 0u) {
                    vitte_c17_ast_emit_deferred_actions(
                        emitter, 0u, indent);
                    vitte_c17_ast_indent(emitter, indent);
                    vitte_c17_ast_write(emitter, "return;\n");
                    return;
                }
                vitte_c17_ast_write(emitter, "return");
                if (node->child_count != 0u) {
                    vitte_c17_ast_write(emitter, " ");
                }
            }
            if (node->child_count != 0u) {
                vitte_c17_ast_emit_expression(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 0u));
            }
            vitte_c17_ast_write(emitter, ";\n");
            if (node->kind == VITTE_AST_NODE_EXPR_STMT &&
                node->child_count != 0u) {
                const vitte_ast_node_t *assigned_name;

                assigned_name = vitte_c17_ast_debug_assignment_name(
                    emitter,
                    vitte_c17_ast_child(emitter, node, 0u));
                vitte_c17_ast_emit_debug_scalar_update(
                    emitter,
                    assigned_name,
                    indent,
                    false);
            }
            return;

        case VITTE_AST_NODE_DEFER_STMT:
            if (node->child_count != 1u ||
                emitter->deferred_count >=
                    sizeof(emitter->deferred_nodes) /
                        sizeof(emitter->deferred_nodes[0])) {
                emitter->failed = true;
                return;
            }
            emitter->deferred_nodes[emitter->deferred_count++] =
                vitte_c17_ast_child(emitter, node, 0u);
            return;

        case VITTE_AST_NODE_IF_STMT:
            vitte_c17_ast_write(emitter, "if ");
            vitte_c17_ast_emit_condition(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            vitte_c17_ast_write(emitter, " ");
            child = vitte_c17_ast_child(emitter, node, 1u);
            if (child == NULL || child->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_write(emitter, "{\n");
            vitte_c17_ast_emit_block_contents(
                emitter,
                child,
                indent + 1u);
            vitte_c17_ast_indent(emitter, indent);
            vitte_c17_ast_write(emitter, "}");
            if (node->child_count > 2u) {
                child = vitte_c17_ast_child(emitter, node, 2u);
                vitte_c17_ast_write(emitter, " else ");
                if (child != NULL &&
                    child->kind == VITTE_AST_NODE_IF_STMT) {
                    vitte_c17_ast_write(emitter, "\n");
                    vitte_c17_ast_emit_statement(
                        emitter,
                        child,
                        indent);
                    return;
                }
                if (child == NULL ||
                    child->kind != VITTE_AST_NODE_BLOCK) {
                    emitter->failed = true;
                    return;
                }
                vitte_c17_ast_write(emitter, "{\n");
                vitte_c17_ast_emit_block_contents(
                    emitter,
                    child,
                    indent + 1u);
                vitte_c17_ast_indent(emitter, indent);
                vitte_c17_ast_write(emitter, "}");
            }
            vitte_c17_ast_write(emitter, "\n");
            return;

        case VITTE_AST_NODE_WHILE_STMT:
            vitte_c17_ast_write(emitter, "while ");
            vitte_c17_ast_emit_condition(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            vitte_c17_ast_write(emitter, " {\n");
            child = vitte_c17_ast_child(emitter, node, 1u);
            if (emitter->loop_defer_depth >=
                sizeof(emitter->loop_defer_bases) /
                    sizeof(emitter->loop_defer_bases[0])) {
                emitter->failed = true;
                return;
            }
            emitter->loop_defer_bases[emitter->loop_defer_depth++] =
                emitter->deferred_count;
            emitter->loop_continue_labels[
                emitter->loop_defer_depth - 1u][0] = '\0';
            vitte_c17_ast_emit_block_contents(
                emitter,
                child,
                indent + 1u);
            --emitter->loop_defer_depth;
            vitte_c17_ast_indent(emitter, indent);
            vitte_c17_ast_write(emitter, "}\n");
            return;

        case VITTE_AST_NODE_LOOP_STMT:
            if (node->child_count != 1u) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_write(emitter, "for (;;) {\n");
            child = vitte_c17_ast_child(emitter, node, 0u);
            if (emitter->loop_defer_depth >=
                sizeof(emitter->loop_defer_bases) /
                    sizeof(emitter->loop_defer_bases[0])) {
                emitter->failed = true;
                return;
            }
            emitter->loop_defer_bases[emitter->loop_defer_depth++] =
                emitter->deferred_count;
            emitter->loop_continue_labels[
                emitter->loop_defer_depth - 1u][0] = '\0';
            vitte_c17_ast_emit_block_contents(
                emitter,
                child,
                indent + 1u);
            --emitter->loop_defer_depth;
            vitte_c17_ast_indent(emitter, indent);
            vitte_c17_ast_write(emitter, "}\n");
            return;

        case VITTE_AST_NODE_FOR_STMT:
            vitte_c17_ast_emit_for_statement(
                emitter,
                node,
                indent);
            return;

        case VITTE_AST_NODE_MATCH_STMT:
        {
            const vitte_ast_node_t *subject;
            const vitte_ast_node_t *subject_type;
            const char *inferred_type;
            char subject_name[128];
            char matched_name[128];

            subject = vitte_c17_ast_child(emitter, node, 0u);
            subject_type = vitte_c17_ast_expression_type(emitter, subject);
            inferred_type = vitte_c17_ast_inferred_type(emitter, subject);
            if (subject_type == NULL && inferred_type == NULL &&
                subject != NULL &&
                (subject->kind == VITTE_AST_NODE_PATH ||
                 subject->kind == VITTE_AST_NODE_IDENTIFIER)) {
                const vitte_ast_node_t *name = subject;
                const vitte_ast_node_t *initializer;

                if (subject->kind == VITTE_AST_NODE_PATH &&
                    subject->child_count == 1u) {
                    name = vitte_c17_ast_child(emitter, subject, 0u);
                }
                initializer = vitte_c17_ast_find_local_initializer(
                    emitter, emitter->function, name);
                if (initializer == NULL) {
                    initializer = vitte_c17_ast_find_local_initializer(
                        emitter,
                        vitte_c17_ast_node(
                            emitter,
                            vitte_parser_root(emitter->parser)),
                        name);
                }
                initializer = vitte_c17_ast_unwrap_groups(
                    emitter, initializer);
                if (initializer != NULL) {
                    subject_type = vitte_c17_ast_expression_type(
                        emitter, initializer);
                    inferred_type = vitte_c17_ast_inferred_type(
                        emitter, initializer);
                }
            }
            if (subject_type == NULL) {
                /* Imported aggregate layouts are not always present in the
                 * current AST symbol scan. A qualified pick variant in the
                 * arms still gives an exact enum type for the subject. */
                subject_type =
                    vitte_c17_ast_match_pick_decl_from_arms(
                        emitter,
                        node);
            }
            if (node->child_count < 2u ||
                (!vitte_c17_ast_match_supported_scalar_type(
                     emitter, subject_type, inferred_type)) ||
                !vitte_c17_ast_make_match_temporary_name(
                    emitter, node, "subject",
                    subject_name, sizeof(subject_name)) ||
                !vitte_c17_ast_make_match_temporary_name(
                    emitter, node, "matched",
                    matched_name, sizeof(matched_name))) {
                emitter->failed = true;
                return;
            }

            vitte_c17_ast_write(emitter, "{\n");
            vitte_c17_ast_indent(emitter, indent + 1u);
            if (subject_type != NULL &&
                subject_type->kind == VITTE_AST_NODE_PICK_DECL) {
                vitte_c17_ast_emit_declaration_name(
                    emitter,
                    subject_type,
                    vitte_c17_ast_child(emitter, subject_type, 0u));
            } else if (subject_type != NULL) {
                vitte_c17_ast_emit_type(emitter, subject_type);
            } else {
                vitte_c17_ast_write(emitter, inferred_type);
            }
            vitte_c17_ast_write(emitter, " ");
            vitte_c17_ast_write(emitter, subject_name);
            vitte_c17_ast_write(emitter, " = ");
            vitte_c17_ast_emit_expression(emitter, subject);
            vitte_c17_ast_write(emitter, ";\n");
            vitte_c17_ast_indent(emitter, indent + 1u);
            vitte_c17_ast_write(emitter, "bool ");
            vitte_c17_ast_write(emitter, matched_name);
            vitte_c17_ast_write(emitter, " = false;\n");

            for (index = 1u; index < node->child_count; ++index) {
                const vitte_ast_node_t *arm;
                const vitte_ast_node_t *pattern;
                const vitte_ast_node_t *guard;
                const vitte_ast_node_t *body;
                const vitte_ast_node_t *binding;
                size_t body_index;

                arm = vitte_c17_ast_child(emitter, node, index);
                if (arm == NULL ||
                    arm->kind != VITTE_AST_NODE_MATCH_ARM ||
                    (arm->child_count != 2u && arm->child_count != 3u)) {
                    emitter->failed = true;
                    return;
                }
                pattern = vitte_c17_ast_child(emitter, arm, 0u);
                guard = arm->child_count == 3u
                    ? vitte_c17_ast_child(emitter, arm, 1u)
                    : NULL;
                body_index = arm->child_count - 1u;
                body = vitte_c17_ast_child(emitter, arm, body_index);
                binding = vitte_c17_ast_match_binding(emitter, pattern);
                if (pattern == NULL || body == NULL ||
                    vitte_c17_ast_match_or_has_binding(
                        emitter, pattern)) {
                    emitter->failed = true;
                    return;
                }

                vitte_c17_ast_indent(emitter, indent + 1u);
                vitte_c17_ast_write(emitter, "if (!");
                vitte_c17_ast_write(emitter, matched_name);
                vitte_c17_ast_write(emitter, " && (");
                vitte_c17_ast_emit_match_pattern_test(
                    emitter,
                    subject_name,
                    subject_type,
                    inferred_type,
                    pattern);
                vitte_c17_ast_write(emitter, ")) {\n");

                if (binding != NULL) {
                    vitte_c17_ast_indent(emitter, indent + 2u);
                    if (subject_type != NULL &&
                        subject_type->kind == VITTE_AST_NODE_PICK_DECL) {
                        vitte_c17_ast_emit_declaration_name(
                            emitter,
                            subject_type,
                            vitte_c17_ast_child(emitter, subject_type, 0u));
                    } else if (subject_type != NULL) {
                        vitte_c17_ast_emit_type(emitter, subject_type);
                    } else {
                        vitte_c17_ast_write(emitter, inferred_type);
                    }
                    vitte_c17_ast_write(emitter, " ");
                    vitte_c17_ast_write_token(
                        emitter,
                        vitte_c17_ast_child(emitter, binding, 0u));
                    vitte_c17_ast_write(emitter, " = ");
                    vitte_c17_ast_write(emitter, subject_name);
                    vitte_c17_ast_write(emitter, ";\n");
                } else if (pattern->kind == VITTE_AST_NODE_PATTERN &&
                           (pattern->flags &
                            VITTE_AST_FLAG_PATTERN_HEAD) != 0u) {
                    vitte_c17_ast_emit_match_pick_bindings(
                        emitter,
                        pattern,
                        subject_name,
                        indent + 2u);
                }

                if (guard != NULL) {
                    vitte_c17_ast_indent(emitter, indent + 2u);
                    vitte_c17_ast_write(emitter, "if (");
                    vitte_c17_ast_emit_condition(emitter, guard);
                    vitte_c17_ast_write(emitter, ") {\n");
                }
                vitte_c17_ast_indent(
                    emitter,
                    guard != NULL ? indent + 3u : indent + 2u);
                vitte_c17_ast_write(emitter, matched_name);
                vitte_c17_ast_write(emitter, " = true;\n");

                if (body->kind == VITTE_AST_NODE_BLOCK) {
                    vitte_c17_ast_emit_block_contents(
                        emitter,
                        body,
                        guard != NULL ? indent + 3u : indent + 2u);
                } else if (body->kind == VITTE_AST_NODE_LET_STMT ||
                           body->kind == VITTE_AST_NODE_EXPR_STMT ||
                           body->kind == VITTE_AST_NODE_RETURN_STMT ||
                           body->kind == VITTE_AST_NODE_IF_STMT ||
                           body->kind == VITTE_AST_NODE_WHILE_STMT ||
                           body->kind == VITTE_AST_NODE_LOOP_STMT ||
                           body->kind == VITTE_AST_NODE_MATCH_STMT ||
                           body->kind == VITTE_AST_NODE_BREAK_STMT ||
                           body->kind == VITTE_AST_NODE_CONTINUE_STMT ||
                           body->kind == VITTE_AST_NODE_ASSERT_STMT) {
                    vitte_c17_ast_indent(
                        emitter,
                        guard != NULL ? indent + 3u : indent + 2u);
                    vitte_c17_ast_emit_statement(
                        emitter,
                        body,
                        guard != NULL ? indent + 3u : indent + 2u);
                } else {
                    vitte_c17_ast_indent(
                        emitter,
                        guard != NULL ? indent + 3u : indent + 2u);
                    {
                        const vitte_ast_node_t *return_type =
                            emitter->function != NULL
                                ? vitte_c17_ast_function_return_type(
                                      emitter,
                                      emitter->function)
                                : NULL;
                        bool returns_void =
                            return_type == NULL ||
                            vitte_c17_ast_type_is_named(
                                emitter, return_type, "void") ||
                            vitte_c17_ast_type_is_named(
                                emitter, return_type, "unit");

                        if (returns_void) {
                            vitte_c17_ast_emit_expression(emitter, body);
                            vitte_c17_ast_write(emitter, ";\n");
                            vitte_c17_ast_indent(
                                emitter,
                                guard != NULL
                                    ? indent + 3u
                                    : indent + 2u);
                            vitte_c17_ast_write(emitter, "return;\n");
                        } else {
                            vitte_c17_ast_write(emitter, "return ");
                            vitte_c17_ast_emit_expression(emitter, body);
                            vitte_c17_ast_write(emitter, ";\n");
                        }
                    }
                }
                if (guard != NULL) {
                    vitte_c17_ast_indent(emitter, indent + 2u);
                    vitte_c17_ast_write(emitter, "}\n");
                }
                vitte_c17_ast_indent(emitter, indent + 1u);
                vitte_c17_ast_write(emitter, "}\n");
            }
            vitte_c17_ast_indent(emitter, indent);
            vitte_c17_ast_write(emitter, "}\n");
            return;
        }

        case VITTE_AST_NODE_BREAK_STMT:
            if (emitter->loop_defer_depth == 0u) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_emit_deferred_actions(
                emitter,
                emitter->loop_defer_bases[
                    emitter->loop_defer_depth - 1u],
                indent);
            vitte_c17_ast_write(emitter, "break;\n");
            return;

        case VITTE_AST_NODE_CONTINUE_STMT:
            if (emitter->loop_defer_depth == 0u) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_emit_deferred_actions(
                emitter,
                emitter->loop_defer_bases[
                    emitter->loop_defer_depth - 1u],
                indent);
            if (emitter->loop_continue_labels[
                    emitter->loop_defer_depth - 1u][0] != '\0') {
                vitte_c17_ast_write(emitter, "goto ");
                vitte_c17_ast_write(
                    emitter,
                    emitter->loop_continue_labels[
                        emitter->loop_defer_depth - 1u]);
                vitte_c17_ast_write(emitter, ";\n");
            } else {
                vitte_c17_ast_write(emitter, "continue;\n");
            }
            return;

        case VITTE_AST_NODE_ASSERT_STMT:
            vitte_c17_ast_write(emitter, "assert(");
            vitte_c17_ast_emit_expression(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u));
            vitte_c17_ast_write(emitter, ");\n");
            return;

        case VITTE_AST_NODE_ASM_STMT:
        {
            const vitte_token_t *syntax_token;
            size_t operand_index;

            if (node->child_count > 512u) {
                emitter->failed = true;
                return;
            }
            syntax_token = vitte_parser_token_at(
                emitter->parser,
                node->token_index + 1u);
            if (syntax_token == NULL) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_write(
                emitter,
                "#if defined(__GNUC__) || defined(__clang__)\n");
            vitte_c17_ast_indent(emitter, indent);
            if (syntax_token->kind == VITTE_TOKEN_STRING_LITERAL) {
                const vitte_ast_node_t *template_node =
                    vitte_c17_ast_child(emitter, node, 0u);

                if (node->child_count != 1u ||
                    template_node == NULL ||
                    template_node->kind != VITTE_AST_NODE_STRING_LITERAL) {
                    emitter->failed = true;
                    return;
                }
                vitte_c17_ast_write(
                    emitter, "__asm__ __volatile__(");
                vitte_c17_ast_emit_expression(
                    emitter, template_node);
                vitte_c17_ast_write(emitter, ");\n");
            } else {
                vitte_c17_ast_write(
                    emitter,
                    "__asm__ __volatile__(\"\" : : ");
                for (operand_index = 0u;
                     operand_index < node->child_count;
                     ++operand_index) {
                    if (operand_index != 0u) {
                        vitte_c17_ast_write(emitter, ", ");
                    }
                    vitte_c17_ast_write(emitter, "\"g\"(");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(
                            emitter, node, operand_index));
                    vitte_c17_ast_write(emitter, ")");
                }
                vitte_c17_ast_write(emitter, " : \"memory\");\n");
            }
            vitte_c17_ast_write(
                emitter,
                "#else\n#error \"Vitte asm requires GNU-compatible inline assembly\"\n#endif\n");
            return;
        }

        case VITTE_AST_NODE_UNSAFE_BLOCK:
            if (node->child_count != 1u) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_emit_block_contents(
                emitter,
                vitte_c17_ast_child(emitter, node, 0u),
                indent);
            return;

        default:
            emitter->failed = true;
            return;
    }
}

static const vitte_ast_node_t *
vitte_c17_ast_find_child_kind(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node,
    vitte_ast_node_kind_t kind)
{
    size_t index;

    if (node == NULL) {
        return NULL;
    }

    for (index = 0u; index < node->child_count; ++index) {
        const vitte_ast_node_t *child;

        child = vitte_c17_ast_child(emitter, node, index);
        if (child != NULL && child->kind == kind) {
            return child;
        }
    }

    return NULL;
}

static void
vitte_c17_ast_emit_generic_function_alias_parameter(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    const vitte_ast_node_t *parameter_name,
    bool *emitted,
    size_t depth)
{
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *alias;
    const vitte_ast_node_t *target;
    size_t generic_count;
    size_t saved_count;
    size_t parameter_count;
    size_t index;
    bool has_return;

    if (emitted != NULL) {
        *emitted = false;
    }
    if (emitter == NULL || type == NULL || parameter_name == NULL ||
        emitted == NULL || type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count < 2u) {
        return;
    }
    if (depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        *emitted = true;
        emitter->failed = true;
        return;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    alias = vitte_c17_ast_find_path_declaration(
        emitter,
        path,
        VITTE_AST_NODE_TYPE_DECL,
        VITTE_AST_NODE_TYPE_DECL);
    generic_count = vitte_c17_ast_generic_form_parameter_count(
        emitter, alias, NULL);
    target = alias != NULL && alias->child_count > 1u
        ? vitte_c17_ast_child(emitter, alias, alias->child_count - 1u)
        : NULL;
    if (alias == NULL || generic_count == 0u || target == NULL) {
        return;
    }
    saved_count = emitter->generic_substitution_count;
    if (target->kind == VITTE_AST_NODE_TYPE_EXPR &&
        target->operator_kind != VITTE_TOKEN_KW_PROC &&
        target->child_count > 1u) {
        const vitte_ast_node_t *target_path =
            vitte_c17_ast_child(emitter, target, 0u);
        const vitte_ast_node_t *nested_alias =
            vitte_c17_ast_find_path_declaration(
                emitter,
                target_path,
                VITTE_AST_NODE_TYPE_DECL,
                VITTE_AST_NODE_TYPE_DECL);
        size_t nested_generic_count =
            vitte_c17_ast_generic_form_parameter_count(
                emitter, nested_alias, NULL);

        if (nested_alias != NULL &&
            (nested_alias->flags & VITTE_AST_FLAG_TYPE_ALIAS) != 0u &&
            nested_generic_count != 0u) {
            if (generic_count != type->child_count - 1u ||
                saved_count > emitter->generic_substitution_capacity ||
                generic_count >
                    emitter->generic_substitution_capacity - saved_count) {
                *emitted = true;
                emitter->failed = true;
                return;
            }
            for (index = 0u; index < generic_count; ++index) {
                size_t substitution_index = saved_count + index;
                emitter->generic_parameter_names[substitution_index] =
                    vitte_c17_ast_generic_form_parameter_name(
                        emitter, alias, index);
                emitter->generic_arguments[substitution_index] =
                    vitte_c17_ast_child(emitter, type, index + 1u);
                emitter->generic_c_type_names[substitution_index] = NULL;
                if (emitter->generic_parameter_names[substitution_index] == NULL ||
                    emitter->generic_arguments[substitution_index] == NULL) {
                    emitter->failed = true;
                    while (index != 0u ||
                           emitter->generic_parameter_names[saved_count] != NULL) {
                        size_t cleanup_index = saved_count + index;
                        emitter->generic_parameter_names[cleanup_index] = NULL;
                        emitter->generic_arguments[cleanup_index] = NULL;
                        emitter->generic_c_type_names[cleanup_index] = NULL;
                        if (index == 0u) {
                            break;
                        }
                        --index;
                    }
                    return;
                }
            }
            emitter->generic_substitution_count =
                saved_count + generic_count;
            vitte_c17_ast_emit_generic_function_alias_parameter(
                emitter,
                target,
                parameter_name,
                emitted,
                depth + 1u);
            emitter->generic_substitution_count = saved_count;
            for (index = 0u; index < generic_count; ++index) {
                size_t substitution_index = saved_count + index;
                emitter->generic_parameter_names[substitution_index] = NULL;
                emitter->generic_arguments[substitution_index] = NULL;
                emitter->generic_c_type_names[substitution_index] = NULL;
            }
            return;
        }
    }
    if (target->kind != VITTE_AST_NODE_TYPE_EXPR ||
        target->operator_kind != VITTE_TOKEN_KW_PROC) {
        return;
    }
    *emitted = true;
    if (generic_count != type->child_count - 1u) {
        emitter->failed = true;
        return;
    }
    if (saved_count > emitter->generic_substitution_capacity ||
        generic_count > emitter->generic_substitution_capacity - saved_count) {
        emitter->failed = true;
        return;
    }
    for (index = 0u; index < generic_count; ++index) {
        size_t substitution_index = saved_count + index;
        emitter->generic_parameter_names[substitution_index] =
            vitte_c17_ast_generic_form_parameter_name(
                emitter, alias, index);
        emitter->generic_arguments[substitution_index] =
            vitte_c17_ast_child(emitter, type, index + 1u);
        emitter->generic_c_type_names[substitution_index] = NULL;
        if (emitter->generic_parameter_names[substitution_index] == NULL ||
            emitter->generic_arguments[substitution_index] == NULL) {
            emitter->failed = true;
            goto cleanup;
        }
    }
    emitter->generic_substitution_count = saved_count + generic_count;
    has_return = (target->flags &
        VITTE_AST_FLAG_FUNCTION_TYPE_HAS_RETURN) != 0u;
    if (has_return && target->child_count == 0u) {
        emitter->failed = true;
        goto cleanup;
    }
    parameter_count = target->child_count - (has_return ? 1u : 0u);
    if (has_return) {
        vitte_c17_ast_emit_type(
            emitter,
            vitte_c17_ast_child(
                emitter, target, target->child_count - 1u));
    } else {
        vitte_c17_ast_write(emitter, "void");
    }
    vitte_c17_ast_write(emitter, " (*");
    vitte_c17_ast_write_token(emitter, parameter_name);
    vitte_c17_ast_write(emitter, ")( ");
    if (parameter_count == 0u) {
        vitte_c17_ast_write(emitter, "void");
    } else {
        for (index = 0u; index < parameter_count; ++index) {
            if (index != 0u) {
                vitte_c17_ast_write(emitter, ", ");
            }
            vitte_c17_ast_emit_type(
                emitter,
                vitte_c17_ast_child(emitter, target, index));
        }
    }
    vitte_c17_ast_write(emitter, ")");

cleanup:
    emitter->generic_substitution_count = saved_count;
    for (index = 0u; index < generic_count; ++index) {
        size_t substitution_index = saved_count + index;
        emitter->generic_parameter_names[substitution_index] = NULL;
        emitter->generic_arguments[substitution_index] = NULL;
        emitter->generic_c_type_names[substitution_index] = NULL;
    }
}

static void
vitte_c17_ast_emit_parameter_list(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function)
{
    size_t index;
    bool first;

    first = true;
    for (index = 1u; index < function->child_count; ++index) {
        const vitte_ast_node_t *parameter;
        const vitte_ast_node_t *parameter_type;
        const vitte_ast_node_t *parameter_name;
        bool emitted_generic_function_alias;

        parameter = vitte_c17_ast_child(emitter, function, index);
        if (parameter == NULL ||
            parameter->kind != VITTE_AST_NODE_PARAMETER) {
            continue;
        }

        if (!first) {
            vitte_c17_ast_write(emitter, ", ");
        }
        parameter_type = vitte_c17_ast_child(emitter, parameter, 1u);
        parameter_name = vitte_c17_ast_child(emitter, parameter, 0u);
        emitted_generic_function_alias = false;
        vitte_c17_ast_emit_generic_function_alias_parameter(
            emitter,
            parameter_type,
            parameter_name,
            &emitted_generic_function_alias,
            0u);
        if (!emitted_generic_function_alias) {
            vitte_c17_ast_emit_declarator(
                emitter, parameter_type, parameter_name);
        }
        first = false;
    }

    if (first) {
        vitte_c17_ast_write(emitter, "void");
    }
}

static void
vitte_c17_ast_emit_function_signature(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function,
    bool prototype);

static void
vitte_c17_ast_write_async_task_type_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function)
{
    if (function == NULL) {
        emitter->failed = true;
        return;
    }
    (void)fprintf(
        emitter->output,
        "VitteAsyncTask_%zu",
        (size_t)function->id);
}

static void
vitte_c17_ast_write_async_task_function_name(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function,
    const char *suffix)
{
    if (function == NULL || suffix == NULL) {
        emitter->failed = true;
        return;
    }
    (void)fprintf(
        emitter->output,
        "vitte_async_%s_%zu",
        suffix,
        (size_t)function->id);
}

static bool
vitte_c17_ast_async_result_is_void(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function)
{
    const vitte_ast_node_t *return_type =
        vitte_c17_ast_function_return_type(emitter, function);
    return return_type == NULL ||
           vitte_c17_ast_type_is_named(emitter, return_type, "void") ||
           vitte_c17_ast_type_is_named(emitter, return_type, "unit");
}

static void
vitte_c17_ast_emit_async_task_prototypes(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function)
{
    const vitte_ast_node_t *return_type;
    size_t index;
    size_t parameter_index = 0u;
    unsigned int previous_mode;
    bool result_is_void;

    if (function == NULL ||
        (function->flags & VITTE_AST_FLAG_ASYNC) == 0u) {
        return;
    }
    if (vitte_c17_ast_generic_form_parameter_count(
            emitter, function, NULL) != 0u) {
        emitter->failed = true;
        return;
    }
    return_type = vitte_c17_ast_function_return_type(emitter, function);
    result_is_void = vitte_c17_ast_async_result_is_void(
        emitter, function);
    vitte_c17_ast_write(emitter, "typedef struct ");
    vitte_c17_ast_write_async_task_type_name(emitter, function);
    vitte_c17_ast_write(emitter, " {\n    bool completed;\n");
    if (!result_is_void) {
        vitte_c17_ast_write(emitter, "    ");
        vitte_c17_ast_emit_type(emitter, return_type);
        vitte_c17_ast_write(emitter, " result;\n");
    }
    for (index = 1u; index < function->child_count && !emitter->failed;
         ++index) {
        const vitte_ast_node_t *parameter =
            vitte_c17_ast_child(emitter, function, index);
        const vitte_ast_node_t *parameter_type;
        if (parameter == NULL ||
            parameter->kind != VITTE_AST_NODE_PARAMETER) {
            continue;
        }
        parameter_type = vitte_c17_ast_child(emitter, parameter, 1u);
        if (parameter_type == NULL ||
            parameter_type->kind == VITTE_AST_NODE_ARRAY_TYPE) {
            emitter->failed = true;
            return;
        }
        vitte_c17_ast_write(emitter, "    ");
        vitte_c17_ast_emit_type(emitter, parameter_type);
        (void)fprintf(
            emitter->output,
            " argument_%zu;\n",
            parameter_index++);
    }
    vitte_c17_ast_write(emitter, "} ");
    vitte_c17_ast_write_async_task_type_name(emitter, function);
    vitte_c17_ast_write(emitter, ";\n");

    previous_mode = emitter->async_signature_mode;
    emitter->async_signature_mode = 1u;
    vitte_c17_ast_emit_function_signature(emitter, function, true);
    emitter->async_signature_mode = 2u;
    vitte_c17_ast_emit_function_signature(emitter, function, true);
    emitter->async_signature_mode = previous_mode;

    vitte_c17_ast_write(emitter, "static ");
    if (result_is_void) {
        vitte_c17_ast_write(emitter, "void ");
    } else {
        vitte_c17_ast_emit_type(emitter, return_type);
        vitte_c17_ast_write(emitter, " ");
    }
    vitte_c17_ast_write_async_task_function_name(
        emitter, function, "await");
    vitte_c17_ast_write(emitter, "(");
    vitte_c17_ast_write_async_task_type_name(emitter, function);
    vitte_c17_ast_write(emitter, " *task);\nstatic ");
    if (result_is_void) {
        vitte_c17_ast_write(emitter, "void ");
    } else {
        vitte_c17_ast_emit_type(emitter, return_type);
        vitte_c17_ast_write(emitter, " ");
    }
    vitte_c17_ast_write_async_task_function_name(
        emitter, function, "await_value");
    vitte_c17_ast_write(emitter, "(");
    vitte_c17_ast_write_async_task_type_name(emitter, function);
    vitte_c17_ast_write(emitter, " task);\n\n");
}

static void
vitte_c17_ast_emit_function_signature(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function,
    bool prototype)
{
    const vitte_ast_node_t *return_type;
    const vitte_ast_node_t *name;
    size_t index;

    name = vitte_c17_ast_child(emitter, function, 0u);
    if (function->kind == VITTE_AST_NODE_EXTERN_PROC_DECL &&
        (vitte_c17_ast_token_is(emitter, name, "fopen") ||
         vitte_c17_ast_token_is(emitter, name, "fread") ||
         vitte_c17_ast_token_is(emitter, name, "ferror") ||
         vitte_c17_ast_token_is(emitter, name, "fclose"))) {
        /* The system headers already declare these C runtime functions with
         * FILE and void pointer types.  Their Vitte-side byte pointers share
         * the C ABI but must not redeclare incompatible prototypes. */
        return;
    }
    for (index = 1u; index < function->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, function, index);
        if (child != NULL &&
            child->kind == VITTE_AST_NODE_WHERE_CLAUSE &&
            emitter->generic_proc_c_name == NULL) {
            emitter->failed = true;
            return;
        }
        if (child != NULL &&
            child->kind == VITTE_AST_NODE_GENERIC_PARAM &&
            emitter->generic_proc_c_name == NULL) {
            if (vitte_c17_ast_is_core_pointer_generic(
                    emitter,
                    function)) {
                return;
            }
            /* A raw generic signature would emit undeclared C type names,
             * and dropping `where` would silently erase source constraints.
             * Until monomorphization and bound checking are wired in, fail
             * the translation unit instead of producing misleading C. */
            emitter->failed = true;
            return;
        }
    }
    return_type =
        NULL;
    {
        size_t return_index;

        for (return_index = 1u;
             return_index < function->child_count;
             ++return_index) {
            const vitte_ast_node_t *child;

            child = vitte_c17_ast_child(
                emitter,
                function,
                return_index);
            if (child != NULL &&
                (child->kind == VITTE_AST_NODE_TYPE_EXPR ||
                 child->kind == VITTE_AST_NODE_ARRAY_TYPE ||
                 child->kind == VITTE_AST_NODE_POINTER_TYPE ||
                 child->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                 child->kind == VITTE_AST_NODE_OPTIONAL_TYPE)) {
                return_type = child;
                break;
            }
        }
    }

    if (emitter->async_signature_mode != 0u) {
        vitte_c17_ast_write(emitter, "static ");
    }
    if (emitter->async_signature_mode == 1u) {
        vitte_c17_ast_write_async_task_type_name(emitter, function);
        vitte_c17_ast_write(emitter, " ");
    } else if (return_type == NULL) {
        vitte_c17_ast_write(emitter, "void ");
    } else {
        vitte_c17_ast_emit_type(emitter, return_type);
        vitte_c17_ast_write(emitter, " ");
    }

    if (emitter->async_signature_mode == 1u) {
        vitte_c17_ast_write_async_task_function_name(
            emitter, function, "create");
    } else if (emitter->async_signature_mode == 2u) {
        vitte_c17_ast_write_async_task_function_name(
            emitter, function, "run");
    } else if (function->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) {
        vitte_c17_ast_write_token(emitter, name);
    } else if (emitter->function_c_name_override != NULL) {
        vitte_c17_ast_write(
            emitter, emitter->function_c_name_override);
    } else if (emitter->generic_proc_c_name != NULL) {
        vitte_c17_ast_write(emitter, emitter->generic_proc_c_name);
    } else if (emitter->space_depth == 0u &&
        vitte_c17_ast_token_is(emitter, name, "main")) {
        vitte_c17_ast_write(emitter, "vitte_entry_main");
    } else {
        vitte_c17_ast_emit_function_name(emitter, name);
    }
    vitte_c17_ast_write(emitter, "(");
    vitte_c17_ast_emit_parameter_list(emitter, function);
    vitte_c17_ast_write(emitter, ")");
    if (vitte_c17_ast_node_has_cold_attribute(emitter, function)) {
        vitte_c17_ast_write(emitter, " VITTE_C17_AST_COLD");
    }
    vitte_c17_ast_write(
        emitter,
        prototype ? ";\n" : " {\n");
}

static bool
vitte_c17_ast_emit_form(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *name;
    size_t field_count = 0u;
    size_t index;

    name = vitte_c17_ast_child(emitter, node, 0u);
    if (name == NULL) {
        return false;
    }

    for (index = 1u; index < node->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, node, index);
        if (child != NULL &&
            (child->kind == VITTE_AST_NODE_GENERIC_PARAM ||
             child->kind == VITTE_AST_NODE_WHERE_CLAUSE)) {
            emitter->failed = true;
            return false;
        }
        if (child != NULL && child->kind == VITTE_AST_NODE_FIELD_DECL) {
            ++field_count;
        }
    }

    vitte_c17_ast_write(emitter, "struct ");
    vitte_c17_ast_emit_declaration_name(emitter, node, name);
    vitte_c17_ast_write(emitter, " {\n");
    if (field_count == 0u) {
        vitte_c17_ast_write(
            emitter,
            "    unsigned char vitte_empty_storage;\n");
    }
    for (index = 1u; index < node->child_count; ++index) {
        const vitte_ast_node_t *field;

        field = vitte_c17_ast_child(emitter, node, index);
        if (field == NULL ||
            field->kind != VITTE_AST_NODE_FIELD_DECL) {
            continue;
        }
        vitte_c17_ast_write(emitter, "    ");
        vitte_c17_ast_emit_declarator(
            emitter,
            vitte_c17_ast_child(emitter, field, 1u),
            vitte_c17_ast_child(emitter, field, 0u));
        vitte_c17_ast_write(emitter, ";\n");
    }
    vitte_c17_ast_write(emitter, "};\n\n");
    return !emitter->failed;
}

static const vitte_ast_node_t *
vitte_c17_ast_layout_dependency_for_type(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *root,
    const vitte_ast_node_t *type)
{
    const vitte_ast_node_t *path;
    const vitte_ast_node_t *name;

    if (type == NULL ||
        type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
        type->kind == VITTE_AST_NODE_POINTER_TYPE ||
        type->kind == VITTE_AST_NODE_OPTIONAL_TYPE) {
        return NULL;
    }
    if (type->kind == VITTE_AST_NODE_ARRAY_TYPE) {
        return type->child_count == 2u
            ? vitte_c17_ast_layout_dependency_for_type(
                  emitter,
                  root,
                  vitte_c17_ast_child(emitter, type, 1u))
            : NULL;
    }
    if (type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        type->child_count == 0u) {
        return NULL;
    }
    path = vitte_c17_ast_child(emitter, type, 0u);
    if (path == NULL || path->kind != VITTE_AST_NODE_PATH ||
        path->child_count == 0u) {
        return NULL;
    }
    name = vitte_c17_ast_child(emitter, path, path->child_count - 1u);
    {
        const vitte_ast_node_t *form =
            vitte_c17_ast_find_path_declaration(
                emitter,
                path,
                VITTE_AST_NODE_FORM_DECL,
                VITTE_AST_NODE_FORM_DECL);
        const vitte_ast_node_t *pick =
            vitte_c17_ast_find_path_declaration(
                emitter,
                path,
                VITTE_AST_NODE_PICK_DECL,
                VITTE_AST_NODE_PICK_DECL);
        (void)root;
        (void)name;
        if (form != NULL) {
            return form;
        }
        return vitte_c17_ast_pick_has_payload(emitter, pick)
            ? pick
            : NULL;
    }
}

static bool
vitte_c17_ast_emit_layout_type_ordered(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *root,
    const vitte_ast_node_t *declaration,
    unsigned char *states,
    size_t state_count)
{
    size_t index;
    size_t previous_space_depth;
    size_t declaration_space_depth = 0u;
    const vitte_ast_node_t *previous_spaces[128];
    const vitte_ast_node_t *spaces[128];
    const vitte_ast_node_t *declaration_spaces[128];
    bool success = false;

    if (declaration == NULL || states == NULL ||
        declaration->id >= state_count ||
        (declaration->kind != VITTE_AST_NODE_FORM_DECL &&
         (declaration->kind != VITTE_AST_NODE_PICK_DECL ||
          !vitte_c17_ast_pick_has_payload(emitter, declaration)))) {
        emitter->failed = true;
        return false;
    }
    if (declaration->kind == VITTE_AST_NODE_FORM_DECL &&
        vitte_c17_ast_generic_form_parameter_count(
            emitter, declaration, NULL) != 0u) {
        states[declaration->id] = 2u;
        return true;
    }
    if (declaration->kind == VITTE_AST_NODE_PICK_DECL &&
        vitte_c17_ast_pick_has_generic_parameters(emitter, declaration)) {
        states[declaration->id] = 2u;
        return true;
    }
    if (states[declaration->id] == 2u) {
        return true;
    }
    if (states[declaration->id] == 1u) {
        emitter->failed = true;
        return false;
    }
    previous_space_depth = emitter->space_depth;
    if (previous_space_depth != 0u) {
        (void)memcpy(
            previous_spaces,
            emitter->space_stack,
            previous_space_depth * sizeof(previous_spaces[0]));
    }
    if (!vitte_c17_ast_find_declaration_scope(
            emitter,
            root,
            declaration,
            spaces,
            0u,
            declaration_spaces,
            &declaration_space_depth)) {
        emitter->failed = true;
        return false;
    }
    (void)memcpy(
        emitter->space_stack,
        declaration_spaces,
        declaration_space_depth * sizeof(declaration_spaces[0]));
    emitter->space_depth = declaration_space_depth;
    states[declaration->id] = 1u;

    if (declaration->kind == VITTE_AST_NODE_FORM_DECL) {
        for (index = 1u; index < declaration->child_count; ++index) {
            const vitte_ast_node_t *field =
                vitte_c17_ast_child(emitter, declaration, index);
            const vitte_ast_node_t *dependency;

            if (field == NULL || field->kind != VITTE_AST_NODE_FIELD_DECL ||
                field->child_count < 2u) {
                continue;
            }
            dependency = vitte_c17_ast_layout_dependency_for_type(
                emitter,
                root,
                vitte_c17_ast_child(emitter, field, 1u));
            if (dependency != NULL &&
                !vitte_c17_ast_emit_layout_type_ordered(
                    emitter, root, dependency, states, state_count)) {
                goto done;
            }
        }
    } else {
        for (index = 1u; index < declaration->child_count; ++index) {
            const vitte_ast_node_t *variant =
                vitte_c17_ast_child(emitter, declaration, index);
            size_t payload_index;
            if (variant == NULL ||
                variant->kind != VITTE_AST_NODE_VARIANT_DECL) {
                continue;
            }
            for (payload_index = 1u;
                 payload_index < variant->child_count;
                 ++payload_index) {
                const vitte_ast_node_t *dependency =
                    vitte_c17_ast_layout_dependency_for_type(
                        emitter,
                        root,
                        vitte_c17_ast_child(
                            emitter, variant, payload_index));
                if (dependency != NULL &&
                    !vitte_c17_ast_emit_layout_type_ordered(
                        emitter,
                        root,
                        dependency,
                        states,
                        state_count)) {
                    goto done;
                }
            }
        }
    }

    if (declaration->kind == VITTE_AST_NODE_FORM_DECL) {
        if (!vitte_c17_ast_emit_form(emitter, declaration)) {
            goto done;
        }
    } else if (!vitte_c17_ast_emit_pick(emitter, declaration)) {
        goto done;
    }
    states[declaration->id] = 2u;
    success = true;

done:
    if (previous_space_depth != 0u) {
        (void)memcpy(
            emitter->space_stack,
            previous_spaces,
            previous_space_depth * sizeof(previous_spaces[0]));
    }
    emitter->space_depth = previous_space_depth;
    return success;
}

static void
vitte_c17_ast_emit_layout_types_topologically(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    const vitte_ast_node_t *root,
    unsigned char *states,
    size_t state_count)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);
        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_FORM_DECL ||
            (declaration->kind == VITTE_AST_NODE_PICK_DECL &&
             vitte_c17_ast_pick_has_payload(emitter, declaration))) {
            (void)vitte_c17_ast_emit_layout_type_ordered(
                emitter, root, declaration, states, state_count);
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count > 1u) {
            const vitte_ast_node_t *body =
                vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL || body->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_emit_layout_types_topologically(
                emitter, body, root, states, state_count);
        }
        if (emitter->failed) {
            return;
        }
    }
}

static bool
vitte_c17_ast_pick_equality_supported(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pick)
{
    static const char *const scalar_names[] = {
        "bool", "char", "i8", "i16", "i32", "i64", "int",
        "u8", "u16", "u32", "u64", "usize", "f32", "f64"
    };
    size_t index;

    if (pick == NULL || pick->kind != VITTE_AST_NODE_PICK_DECL) {
        return false;
    }
    for (index = 1u; index < pick->child_count; ++index) {
        const vitte_ast_node_t *variant =
            vitte_c17_ast_child(emitter, pick, index);
        size_t payload_index;

        if (variant == NULL ||
            variant->kind != VITTE_AST_NODE_VARIANT_DECL) {
            return false;
        }
        for (payload_index = 1u;
             payload_index < variant->child_count;
             ++payload_index) {
            const vitte_ast_node_t *type =
                vitte_c17_ast_child(emitter, variant, payload_index);
            size_t scalar_index;
            bool supported =
                type != NULL &&
                (type->kind == VITTE_AST_NODE_POINTER_TYPE ||
                 type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                 type->kind == VITTE_AST_NODE_OPTIONAL_TYPE ||
                 vitte_c17_ast_is_string_type(emitter, type));

            for (scalar_index = 0u;
                 !supported &&
                 scalar_index < sizeof(scalar_names) /
                                    sizeof(scalar_names[0]);
                 ++scalar_index) {
                supported = vitte_c17_ast_type_is_named(
                    emitter, type, scalar_names[scalar_index]);
            }
            if (!supported) {
                return false;
            }
        }
    }
    return true;
}

static void
vitte_c17_ast_emit_pick_payload_field_ref(
    vitte_c17_ast_emitter_t *emitter,
    const char *value_name,
    const vitte_ast_node_t *variant_name,
    size_t payload_index)
{
    vitte_c17_ast_write(emitter, value_name);
    vitte_c17_ast_write(emitter, ".payload.");
    vitte_c17_ast_write_token(emitter, variant_name);
    vitte_c17_ast_write(emitter, ".");
    vitte_c17_ast_write_payload_field_name(emitter, payload_index);
}

static void
vitte_c17_ast_emit_pick_equality_helper(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *pick)
{
    const vitte_ast_node_t *name =
        vitte_c17_ast_child(emitter, pick, 0u);
    size_t variant_index;

    if (name == NULL ||
        !vitte_c17_ast_pick_has_payload(emitter, pick) ||
        !vitte_c17_ast_pick_equality_supported(emitter, pick)) {
        return;
    }
    vitte_c17_ast_write(
        emitter,
        "static inline VITTE_C17_AST_UNUSED bool vitte_pick_equal_");
    vitte_c17_ast_write_token(emitter, name);
    vitte_c17_ast_write(emitter, "(");
    vitte_c17_ast_write_token(emitter, name);
    vitte_c17_ast_write(emitter, " left, ");
    vitte_c17_ast_write_token(emitter, name);
    vitte_c17_ast_write(emitter, " right) {\n");
    vitte_c17_ast_write(emitter,
        "    if (left.tag != right.tag) { return false; }\n"
        "    switch (left.tag) {\n");
    for (variant_index = 1u;
         variant_index < pick->child_count;
         ++variant_index) {
        const vitte_ast_node_t *variant =
            vitte_c17_ast_child(emitter, pick, variant_index);
        const vitte_ast_node_t *variant_name;
        size_t payload_index;

        if (variant == NULL ||
            variant->kind != VITTE_AST_NODE_VARIANT_DECL ||
            variant->child_count == 0u) {
            continue;
        }
        variant_name = vitte_c17_ast_child(emitter, variant, 0u);
        vitte_c17_ast_write(emitter, "    case ");
        vitte_c17_ast_write_token(emitter, name);
        vitte_c17_ast_write(emitter, "_tag_");
        vitte_c17_ast_write_token(emitter, variant_name);
        vitte_c17_ast_write(emitter, ":\n");
        if (variant->child_count == 1u) {
            vitte_c17_ast_write(emitter,
                "        return true;\n");
            continue;
        }
        vitte_c17_ast_write(emitter, "        return ");
        for (payload_index = 1u;
             payload_index < variant->child_count;
             ++payload_index) {
            const vitte_ast_node_t *payload_type =
                vitte_c17_ast_child(emitter, variant, payload_index);
            if (payload_index != 1u) {
                vitte_c17_ast_write(emitter, " && ");
            }
            if (payload_type != NULL &&
                payload_type->kind == VITTE_AST_NODE_TYPE_EXPR &&
                vitte_c17_ast_is_string_type(emitter, payload_type)) {
                vitte_c17_ast_write(emitter, "((");
                vitte_c17_ast_emit_pick_payload_field_ref(
                    emitter, "left", variant_name, payload_index - 1u);
                vitte_c17_ast_write(emitter, " == NULL && ");
                vitte_c17_ast_emit_pick_payload_field_ref(
                    emitter, "right", variant_name, payload_index - 1u);
                vitte_c17_ast_write(emitter, " == NULL) || (");
                vitte_c17_ast_emit_pick_payload_field_ref(
                    emitter, "left", variant_name, payload_index - 1u);
                vitte_c17_ast_write(emitter, " != NULL && ");
                vitte_c17_ast_emit_pick_payload_field_ref(
                    emitter, "right", variant_name, payload_index - 1u);
                vitte_c17_ast_write(emitter, " != NULL && strcmp(");
                vitte_c17_ast_emit_pick_payload_field_ref(
                    emitter, "left", variant_name, payload_index - 1u);
                vitte_c17_ast_write(emitter, ", ");
                vitte_c17_ast_emit_pick_payload_field_ref(
                    emitter, "right", variant_name, payload_index - 1u);
                vitte_c17_ast_write(emitter, ") == 0))");
            } else {
                vitte_c17_ast_emit_pick_payload_field_ref(
                    emitter, "left", variant_name, payload_index - 1u);
                vitte_c17_ast_write(emitter, " == ");
                vitte_c17_ast_emit_pick_payload_field_ref(
                    emitter, "right", variant_name, payload_index - 1u);
            }
        }
        vitte_c17_ast_write(emitter, ";\n");
    }
    vitte_c17_ast_write(emitter,
        "    default: return false;\n"
        "    }\n"
        "}\n\n");
}

static void
vitte_c17_ast_emit_pick_equality_helpers_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);
        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_PICK_DECL) {
            vitte_c17_ast_emit_pick_equality_helper(
                emitter, declaration);
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count > 1u) {
            const vitte_ast_node_t *body =
                vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL || body->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_emit_pick_equality_helpers_in_scope(
                emitter, body);
        }
        if (emitter->failed) {
            return;
        }
    }
}

static bool
vitte_c17_ast_emit_type_alias(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *declaration,
    const vitte_ast_node_t *target)
{
    const vitte_ast_node_t *name;
    bool has_return_type;
    size_t parameter_count;
    size_t index;

    name = vitte_c17_ast_child(emitter, declaration, 0u);
    has_return_type = (target->flags &
        VITTE_AST_FLAG_FUNCTION_TYPE_HAS_RETURN) != 0u;
    if (name == NULL ||
        (has_return_type && target->child_count == 0u)) {
        return false;
    }

    if (target->kind != VITTE_AST_NODE_TYPE_EXPR ||
        target->operator_kind != VITTE_TOKEN_KW_PROC) {
        if (target->kind == VITTE_AST_NODE_ARRAY_TYPE) {
            vitte_c17_ast_write(emitter, "typedef ");
            if (target->child_count == 1u) {
                vitte_c17_ast_write(emitter, "VitteArray ");
                vitte_c17_ast_emit_declaration_name(
                    emitter, declaration, name);
            } else if (target->child_count == 2u) {
                vitte_c17_ast_emit_type(
                    emitter,
                    vitte_c17_ast_child(emitter, target, 1u));
                vitte_c17_ast_write(emitter, " ");
                vitte_c17_ast_emit_declaration_name(
                    emitter, declaration, name);
                vitte_c17_ast_write(emitter, "[");
                vitte_c17_ast_emit_expression(
                    emitter,
                    vitte_c17_ast_child(emitter, target, 0u));
                vitte_c17_ast_write(emitter, "]");
            } else {
                return false;
            }
        } else {
            vitte_c17_ast_write(emitter, "typedef ");
            vitte_c17_ast_emit_type(emitter, target);
            vitte_c17_ast_write(emitter, " ");
            vitte_c17_ast_emit_declaration_name(
                emitter, declaration, name);
        }
        vitte_c17_ast_write(emitter, ";\n");
        return !emitter->failed;
    }

    parameter_count = target->child_count -
        (has_return_type ? 1u : 0u);

    vitte_c17_ast_write(emitter, "typedef ");
    if (has_return_type) {
        vitte_c17_ast_emit_type(
            emitter,
            vitte_c17_ast_child(
                emitter, target, target->child_count - 1u));
    } else {
        vitte_c17_ast_write(emitter, "void");
    }
    vitte_c17_ast_write(emitter, " (*");
    vitte_c17_ast_emit_declaration_name(emitter, declaration, name);
    vitte_c17_ast_write(emitter, ")(");
    if (parameter_count == 0u) {
        vitte_c17_ast_write(emitter, "void");
    } else {
        for (index = 0u; index < parameter_count; ++index) {
            if (index != 0u) {
                vitte_c17_ast_write(emitter, ", ");
            }
            vitte_c17_ast_emit_type(
                emitter,
                vitte_c17_ast_child(emitter, target, index));
        }
    }
    vitte_c17_ast_write(emitter, ");\n");
    return !emitter->failed;
}

static void
vitte_c17_ast_emit_function_type_aliases_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);

        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_TYPE_DECL &&
            (declaration->flags & VITTE_AST_FLAG_TYPE_ALIAS) != 0u &&
            declaration->child_count > 1u) {
            bool has_generics = false;
            size_t child_index;
            const vitte_ast_node_t *target = vitte_c17_ast_child(
                emitter,
                declaration,
                declaration->child_count - 1u);

            for (child_index = 1u;
                 child_index < declaration->child_count;
                 ++child_index) {
                const vitte_ast_node_t *child = vitte_c17_ast_child(
                    emitter, declaration, child_index);
                if (child != NULL &&
                    child->kind == VITTE_AST_NODE_GENERIC_PARAM) {
                    has_generics = true;
                    break;
                }
            }
            if (has_generics) {
                /* Generic aliases are expanded at each concrete type use. */
                continue;
            }
            if (!has_generics && target != NULL &&
                (target->kind == VITTE_AST_NODE_TYPE_EXPR ||
                 target->kind == VITTE_AST_NODE_POINTER_TYPE ||
                 target->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
                 target->kind == VITTE_AST_NODE_OPTIONAL_TYPE ||
                 target->kind == VITTE_AST_NODE_ARRAY_TYPE) &&
                !vitte_c17_ast_emit_type_alias(
                    emitter, declaration, target)) {
                emitter->failed = true;
            }
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count > 1u) {
            const vitte_ast_node_t *body =
                vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL || body->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
                return;
            }
            if (emitter->space_depth >=
                sizeof(emitter->space_stack) /
                    sizeof(emitter->space_stack[0])) {
                emitter->failed = true;
                return;
            }
            emitter->space_stack[emitter->space_depth++] = declaration;
            vitte_c17_ast_emit_function_type_aliases_in_scope(
                emitter, body);
            --emitter->space_depth;
        }
        if (emitter->failed) {
            return;
        }
    }
}

static void
vitte_c17_ast_emit_forms_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    bool forward_declarations)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration;

        declaration = vitte_c17_ast_child(emitter, scope, index);
        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }

        if (declaration->kind == VITTE_AST_NODE_FORM_DECL) {
            const vitte_ast_node_t *name;
            size_t generic_index;

            name = vitte_c17_ast_child(emitter, declaration, 0u);
            if (name == NULL) {
                emitter->failed = true;
                return;
            }
            if (forward_declarations) {
                if (vitte_c17_ast_generic_form_parameter_count(
                        emitter, declaration, NULL) == 0u) {
                    vitte_c17_ast_write(emitter, "typedef struct ");
                    vitte_c17_ast_emit_declaration_name(
                        emitter, declaration, name);
                    vitte_c17_ast_write(emitter, " ");
                    vitte_c17_ast_emit_declaration_name(
                        emitter, declaration, name);
                    vitte_c17_ast_write(emitter, ";\n");
                } else {
                    for (generic_index = 0u;
                         generic_index < emitter->generic_form_count;
                         ++generic_index) {
                        const vitte_c17_ast_generic_form_instance_t *instance =
                            &emitter->generic_forms[generic_index];
                        if (instance->declaration == declaration &&
                            !instance->forward_declared) {
                            vitte_c17_ast_write(emitter, "typedef struct ");
                            vitte_c17_ast_emit_generic_form_instance_name(
                                emitter,
                                instance);
                            vitte_c17_ast_write(emitter, " ");
                            vitte_c17_ast_emit_generic_form_instance_name(
                                emitter,
                                instance);
                            vitte_c17_ast_write(emitter, ";\n");
                            emitter->generic_forms[generic_index].forward_declared = true;
                        }
                    }
                }
            } else {
                (void)vitte_c17_ast_emit_form(emitter, declaration);
            }
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count > 1u) {
            const vitte_ast_node_t *body;

            body = vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL ||
                body->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
                return;
            }
            if (emitter->space_depth >=
                sizeof(emitter->space_stack) /
                    sizeof(emitter->space_stack[0])) {
                emitter->failed = true;
                return;
            }
            emitter->space_stack[emitter->space_depth++] = declaration;
            vitte_c17_ast_emit_forms_in_scope(
                emitter,
                body,
                forward_declarations);
            --emitter->space_depth;
        }
    }
}

static void
vitte_c17_ast_emit_opaque_forward_declarations_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);

        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_OPAQUE_DECL) {
            const vitte_ast_node_t *name =
                vitte_c17_ast_child(emitter, declaration, 0u);
            if (name == NULL) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_write(emitter, "typedef struct ");
            vitte_c17_ast_emit_declaration_name(
                emitter, declaration, name);
            vitte_c17_ast_write(emitter, " ");
            vitte_c17_ast_emit_declaration_name(
                emitter, declaration, name);
            vitte_c17_ast_write(emitter, ";\n");
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count > 1u) {
            const vitte_ast_node_t *body =
                vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL || body->kind != VITTE_AST_NODE_BLOCK ||
                emitter->space_depth >=
                    sizeof(emitter->space_stack) /
                        sizeof(emitter->space_stack[0])) {
                emitter->failed = true;
                return;
            }
            emitter->space_stack[emitter->space_depth++] = declaration;
            vitte_c17_ast_emit_opaque_forward_declarations_in_scope(
                emitter, body);
            --emitter->space_depth;
        }
        if (emitter->failed) {
            return;
        }
    }
}

static bool
vitte_c17_ast_emit_generic_form_type_dependencies(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *type,
    bool requires_layout,
    size_t depth)
{
    size_t index;

    if (type == NULL || depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        emitter->failed = true;
        return false;
    }
    if (type->kind == VITTE_AST_NODE_POINTER_TYPE ||
        type->kind == VITTE_AST_NODE_REFERENCE_TYPE ||
        type->kind == VITTE_AST_NODE_OPTIONAL_TYPE) {
        for (index = 0u; index < type->child_count; ++index) {
            if (!vitte_c17_ast_emit_generic_form_type_dependencies(
                    emitter,
                    vitte_c17_ast_child(emitter, type, index),
                    false,
                    depth + 1u)) {
                return false;
            }
        }
        return true;
    }
    if (type->kind == VITTE_AST_NODE_ARRAY_TYPE) {
        /* A fixed-size array stores its elements inline, so its element
         * type must have a complete layout before the containing form is
         * emitted. Dynamic arrays contain a pointer-backed VitteArray header
         * and do not impose that dependency. */
        for (index = 0u; index < type->child_count; ++index) {
            bool element_requires_layout =
                requires_layout && type->child_count > 1u && index == 1u;
            if (!vitte_c17_ast_emit_generic_form_type_dependencies(
                    emitter,
                    vitte_c17_ast_child(emitter, type, index),
                    element_requires_layout,
                    depth + 1u)) {
                return false;
            }
        }
        return true;
    }
    {
        size_t substitution_index =
            vitte_c17_ast_generic_substitution_index(emitter, type);
        if (substitution_index != SIZE_MAX) {
            const vitte_ast_node_t *argument =
                emitter->generic_arguments[substitution_index];
            size_t saved_substitution_count =
                emitter->generic_substitution_count;
            bool resolved;
            if (argument == NULL) {
                return false;
            }
            emitter->generic_substitution_count = substitution_index;
            resolved = vitte_c17_ast_emit_generic_form_type_dependencies(
                emitter,
                argument,
                requires_layout,
                depth + 1u);
            emitter->generic_substitution_count =
                saved_substitution_count;
            return resolved;
        }
    }
    for (index = 0u; index < type->child_count; ++index) {
        if (!vitte_c17_ast_emit_generic_form_type_dependencies(
                emitter,
                vitte_c17_ast_child(emitter, type, index),
                requires_layout,
                depth + 1u)) {
            return false;
        }
    }
    if (type->kind == VITTE_AST_NODE_TYPE_EXPR &&
        type->operator_kind != VITTE_TOKEN_KW_PROC &&
        type->child_count > 1u) {
        const vitte_ast_node_t *root = vitte_c17_ast_node(
            emitter,
            vitte_parser_root(emitter->parser));
        const vitte_c17_ast_generic_form_instance_t *dependency =
            vitte_c17_ast_generic_form_instance_for_type(emitter, type);
        size_t dependency_index;

        if (dependency == NULL && root != NULL) {
            vitte_c17_ast_collect_generic_form_instances(
                emitter, root, type, depth + 1u);
            dependency = vitte_c17_ast_generic_form_instance_for_type(
                emitter, type);
        }
        if (dependency == NULL) {
            emitter->failed = true;
            return false;
        }
        dependency_index = (size_t)(dependency - emitter->generic_forms);
        if (requires_layout) {
            if (dependency->declaration != NULL &&
                dependency->declaration->kind == VITTE_AST_NODE_PICK_DECL) {
                return vitte_c17_ast_emit_generic_pick_layout_ordered(
                    emitter, dependency_index, depth + 1u);
            }
            return vitte_c17_ast_emit_generic_form_layout_ordered(
                emitter, dependency_index, depth + 1u);
        }
        if (!emitter->generic_forms[dependency_index].forward_declared) {
            vitte_c17_ast_write(emitter, "typedef struct ");
            vitte_c17_ast_emit_generic_form_instance_name(
                emitter,
                &emitter->generic_forms[dependency_index]);
            vitte_c17_ast_write(emitter, " ");
            vitte_c17_ast_emit_generic_form_instance_name(
                emitter,
                &emitter->generic_forms[dependency_index]);
            vitte_c17_ast_write(emitter, ";\n");
            emitter->generic_forms[dependency_index].forward_declared = true;
        }
        return !emitter->failed;
    }
    return !emitter->failed;
}

static bool
vitte_c17_ast_emit_generic_form_layout_ordered(
    vitte_c17_ast_emitter_t *emitter,
    size_t instance_index,
    size_t depth)
{
    vitte_c17_ast_generic_form_instance_t *instance;
    const vitte_ast_node_t *declaration;
    size_t parameter_count;
    size_t saved_substitution_count;
    size_t parameter_index;
    size_t field_index;

    if (emitter == NULL || emitter->failed ||
        instance_index >= emitter->generic_form_count ||
        depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        if (emitter != NULL) {
            emitter->failed = true;
        }
        return false;
    }
    instance = &emitter->generic_forms[instance_index];
    if (instance->layout_state == 2u) {
        return true;
    }
    if (instance->layout_state == 1u) {
        emitter->failed = true;
        return false;
    }
    declaration = instance->declaration;
    if (declaration != NULL &&
        declaration->kind == VITTE_AST_NODE_PICK_DECL) {
        return vitte_c17_ast_emit_generic_pick_layout_ordered(
            emitter, instance_index, depth);
    }
    parameter_count = vitte_c17_ast_generic_form_parameter_count(
        emitter, declaration, NULL);
    if (declaration == NULL || parameter_count == 0u ||
        parameter_count != instance->argument_count ||
        instance->type_expression == NULL ||
        instance->arguments == NULL ||
        parameter_count > SIZE_MAX -
            emitter->generic_substitution_count ||
        emitter->generic_substitution_count + parameter_count >
            emitter->generic_substitution_capacity) {
        emitter->failed = true;
        return false;
    }
    instance->layout_state = 1u;
    saved_substitution_count = emitter->generic_substitution_count;
    for (parameter_index = 0u;
         parameter_index < parameter_count;
         ++parameter_index) {
        const vitte_ast_node_t *parameter_name =
            vitte_c17_ast_generic_form_parameter_name(
                emitter, declaration, parameter_index);
        const vitte_ast_node_t *argument =
            instance->arguments[parameter_index];
        if (parameter_name == NULL || argument == NULL) {
            emitter->generic_substitution_count =
                saved_substitution_count;
            emitter->failed = true;
            return false;
        }
        emitter->generic_parameter_names[
            emitter->generic_substitution_count] = parameter_name;
        emitter->generic_arguments[
            emitter->generic_substitution_count] = argument;
        ++emitter->generic_substitution_count;
    }

    for (field_index = 1u;
         field_index < declaration->child_count && !emitter->failed;
         ++field_index) {
        const vitte_ast_node_t *field =
            vitte_c17_ast_child(emitter, declaration, field_index);
        if (field == NULL || field->kind != VITTE_AST_NODE_FIELD_DECL) {
            continue;
        }
        if (field->child_count < 2u ||
            !vitte_c17_ast_emit_generic_form_type_dependencies(
                emitter,
                vitte_c17_ast_child(emitter, field, 1u),
                true,
                depth + 1u)) {
            emitter->generic_substitution_count =
                saved_substitution_count;
            return false;
        }
    }

    /* Dependency discovery can grow generic_forms and move its allocation. */
    instance = &emitter->generic_forms[instance_index];
    if (!emitter->failed) {
        vitte_c17_ast_write(emitter, "struct ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, " {\n");
        for (field_index = 1u;
             field_index < declaration->child_count && !emitter->failed;
             ++field_index) {
            const vitte_ast_node_t *field =
                vitte_c17_ast_child(emitter, declaration, field_index);
            if (field == NULL || field->kind != VITTE_AST_NODE_FIELD_DECL) {
                continue;
            }
            if (field->child_count < 2u) {
                emitter->failed = true;
                break;
            }
            vitte_c17_ast_write(emitter, "    ");
            vitte_c17_ast_emit_declarator(
                emitter,
                vitte_c17_ast_child(emitter, field, 1u),
                vitte_c17_ast_child(emitter, field, 0u));
            vitte_c17_ast_write(emitter, ";\n");
        }
        vitte_c17_ast_write(emitter, "};\n\n");
    }
    emitter->generic_substitution_count = saved_substitution_count;
    if (emitter->failed) {
        return false;
    }
    instance->layout_state = 2u;
    return true;
}

static bool
vitte_c17_ast_emit_generic_pick_layout_ordered(
    vitte_c17_ast_emitter_t *emitter,
    size_t instance_index,
    size_t depth)
{
    vitte_c17_ast_generic_form_instance_t *instance;
    const vitte_ast_node_t *declaration;
    size_t parameter_count;
    size_t saved_substitution_count;
    size_t parameter_index;
    size_t variant_index;
    size_t variant_count = 0u;
    bool has_payload = false;

    if (emitter == NULL || emitter->failed ||
        instance_index >= emitter->generic_form_count ||
        depth > VITTE_C17_AST_GENERIC_FORM_TYPE_DEPTH_MAX) {
        if (emitter != NULL) {
            emitter->failed = true;
        }
        return false;
    }
    instance = &emitter->generic_forms[instance_index];
    if (instance->layout_state == 2u) {
        return true;
    }
    if (instance->layout_state == 1u) {
        emitter->failed = true;
        return false;
    }
    declaration = instance->declaration;
    parameter_count = vitte_c17_ast_generic_form_parameter_count(
        emitter, declaration, NULL);
    if (declaration == NULL ||
        declaration->kind != VITTE_AST_NODE_PICK_DECL ||
        parameter_count == 0u ||
        parameter_count != instance->argument_count ||
        instance->type_expression == NULL || instance->arguments == NULL ||
        parameter_count > SIZE_MAX - emitter->generic_substitution_count ||
        emitter->generic_substitution_count + parameter_count >
            emitter->generic_substitution_capacity) {
        emitter->failed = true;
        return false;
    }

    instance->layout_state = 1u;
    saved_substitution_count = emitter->generic_substitution_count;
    for (parameter_index = 0u;
         parameter_index < parameter_count;
         ++parameter_index) {
        const vitte_ast_node_t *parameter_name =
            vitte_c17_ast_generic_form_parameter_name(
                emitter, declaration, parameter_index);
        const vitte_ast_node_t *argument =
            instance->arguments[parameter_index];
        if (parameter_name == NULL || argument == NULL) {
            emitter->generic_substitution_count = saved_substitution_count;
            emitter->failed = true;
            return false;
        }
        emitter->generic_parameter_names[
            emitter->generic_substitution_count] = parameter_name;
        emitter->generic_arguments[
            emitter->generic_substitution_count] = argument;
        ++emitter->generic_substitution_count;
    }


    for (variant_index = 1u;
         variant_index < declaration->child_count && !emitter->failed;
         ++variant_index) {
        const vitte_ast_node_t *variant =
            vitte_c17_ast_child(emitter, declaration, variant_index);
        size_t payload_index;
        if (variant != NULL &&
            (variant->kind == VITTE_AST_NODE_GENERIC_PARAM ||
             variant->kind == VITTE_AST_NODE_WHERE_CLAUSE)) {
            continue;
        }
        if (variant == NULL ||
            variant->kind != VITTE_AST_NODE_VARIANT_DECL) {
            emitter->failed = true;
            break;
        }
        ++variant_count;
        if (variant->child_count > 1u) {
            has_payload = true;
        }
        for (payload_index = 1u;
             payload_index < variant->child_count && !emitter->failed;
             ++payload_index) {
            if (!vitte_c17_ast_emit_generic_form_type_dependencies(
                    emitter,
                    vitte_c17_ast_child(emitter, variant, payload_index),
                    true,
                    depth + 1u)) {
                break;
            }
        }
    }
    if (variant_count == 0u) {
        emitter->failed = true;
    }
    /* Dependency discovery can grow generic_forms and move its allocation. */
    instance = &emitter->generic_forms[instance_index];
    if (!emitter->failed && !has_payload) {
        vitte_c17_ast_write(emitter, "typedef enum ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, " {\n");
        for (variant_index = 1u;
             variant_index < declaration->child_count && !emitter->failed;
             ++variant_index) {
            const vitte_ast_node_t *variant =
                vitte_c17_ast_child(emitter, declaration, variant_index);
            if (variant == NULL ||
                variant->kind == VITTE_AST_NODE_GENERIC_PARAM ||
                variant->kind == VITTE_AST_NODE_WHERE_CLAUSE) {
                continue;
            }
            vitte_c17_ast_write(emitter, "    ");
            vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
            vitte_c17_ast_write(emitter, "_");
            vitte_c17_ast_write_token(
                emitter, vitte_c17_ast_child(emitter, variant, 0u));
            vitte_c17_ast_write(
                emitter,
                variant_index + 1u < declaration->child_count ? ",\n" : "\n");
        }
        vitte_c17_ast_write(emitter, "} ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, ";\n\n");
    } else if (!emitter->failed) {
        vitte_c17_ast_write(emitter, "typedef enum ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, "_tag {\n");
        for (variant_index = 1u;
             variant_index < declaration->child_count && !emitter->failed;
             ++variant_index) {
            const vitte_ast_node_t *variant =
                vitte_c17_ast_child(emitter, declaration, variant_index);
            if (variant == NULL ||
                variant->kind == VITTE_AST_NODE_GENERIC_PARAM ||
                variant->kind == VITTE_AST_NODE_WHERE_CLAUSE) {
                continue;
            }
            vitte_c17_ast_write(emitter, "    ");
            vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
            vitte_c17_ast_write(emitter, "_tag_");
            vitte_c17_ast_write_token(
                emitter, vitte_c17_ast_child(emitter, variant, 0u));
            vitte_c17_ast_write(
                emitter,
                variant_index + 1u < declaration->child_count ? ",\n" : "\n");
        }
        vitte_c17_ast_write(emitter, "} ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, "_tag;\nstruct ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, " {\n    ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, "_tag tag;\n    union {\n");
        for (variant_index = 1u;
             variant_index < declaration->child_count && !emitter->failed;
             ++variant_index) {
            const vitte_ast_node_t *variant =
                vitte_c17_ast_child(emitter, declaration, variant_index);
            if (variant == NULL ||
                variant->kind == VITTE_AST_NODE_GENERIC_PARAM ||
                variant->kind == VITTE_AST_NODE_WHERE_CLAUSE) {
                continue;
            }
            const vitte_ast_node_t *variant_name =
                vitte_c17_ast_child(emitter, variant, 0u);
            size_t payload_index;
            if (variant->child_count == 1u) {
                continue;
            }
            vitte_c17_ast_write(emitter, "        struct {\n");
            for (payload_index = 1u;
                 payload_index < variant->child_count && !emitter->failed;
                 ++payload_index) {
                const vitte_ast_node_t *payload_type =
                    vitte_c17_ast_child(emitter, variant, payload_index);
                vitte_c17_ast_write(emitter, "            ");
                if (payload_type != NULL &&
                    payload_type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
                    payload_type->child_count == 2u) {
                    vitte_c17_ast_emit_type(
                        emitter,
                        vitte_c17_ast_child(emitter, payload_type, 1u));
                    vitte_c17_ast_write(emitter, " ");
                    vitte_c17_ast_write_payload_field_name(
                        emitter, payload_index - 1u);
                    vitte_c17_ast_write(emitter, "[");
                    vitte_c17_ast_emit_expression(
                        emitter,
                        vitte_c17_ast_child(emitter, payload_type, 0u));
                    vitte_c17_ast_write(emitter, "]");
                } else {
                    vitte_c17_ast_emit_type(emitter, payload_type);
                    vitte_c17_ast_write(emitter, " ");
                    vitte_c17_ast_write_payload_field_name(
                        emitter, payload_index - 1u);
                }
                vitte_c17_ast_write(emitter, ";\n");
            }
            vitte_c17_ast_write(emitter, "        } ");
            vitte_c17_ast_write_token(emitter, variant_name);
            vitte_c17_ast_write(emitter, ";\n");
        }
        vitte_c17_ast_write(emitter, "    } payload;\n};\n\n");
    }
    emitter->generic_substitution_count = saved_substitution_count;
    if (emitter->failed) {
        return false;
    }
    instance = &emitter->generic_forms[instance_index];
    instance->layout_state = 2u;
    return true;
}

static void
vitte_c17_ast_emit_generic_form_instances(
    vitte_c17_ast_emitter_t *emitter)
{
    size_t index;

    for (index = 0u;
         index < emitter->generic_form_count && !emitter->failed;
         ++index) {
        if (emitter->generic_forms[index].declaration != NULL &&
            emitter->generic_forms[index].declaration->kind ==
                VITTE_AST_NODE_FORM_DECL) {
            (void)vitte_c17_ast_emit_generic_form_layout_ordered(
                emitter, index, 0u);
        } else if (emitter->generic_forms[index].declaration != NULL &&
                   emitter->generic_forms[index].declaration->kind ==
                       VITTE_AST_NODE_PICK_DECL) {
            (void)vitte_c17_ast_emit_generic_pick_layout_ordered(
                emitter, index, 0u);
        }
    }
}

static bool
vitte_c17_ast_pick_has_generic_parameters(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    size_t index;

    if (node == NULL) {
        return false;
    }
    for (index = 1u; index < node->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, node, index);
        if (child != NULL &&
            (child->kind == VITTE_AST_NODE_GENERIC_PARAM ||
             child->kind == VITTE_AST_NODE_WHERE_CLAUSE)) {
            return true;
        }
    }
    return false;
}

static bool
vitte_c17_ast_emit_pick(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *name;
    size_t variant_count = 0u;
    size_t index;
    bool has_payload;

    name = vitte_c17_ast_child(emitter, node, 0u);
    if (name == NULL || node->kind != VITTE_AST_NODE_PICK_DECL) {
        return false;
    }
    if (vitte_c17_ast_pick_has_generic_parameters(emitter, node)) {
        emitter->failed = true;
        return false;
    }
    for (index = 1u; index < node->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, node, index);
        if (child == NULL || child->kind != VITTE_AST_NODE_VARIANT_DECL) {
            emitter->failed = true;
            return false;
        }
        ++variant_count;
    }
    if (variant_count == 0u) {
        emitter->failed = true;
        return false;
    }
    has_payload = vitte_c17_ast_pick_has_payload(emitter, node);

    if (!has_payload) {
        size_t emitted = 0u;

        vitte_c17_ast_write(emitter, "typedef enum ");
        vitte_c17_ast_emit_declaration_name(emitter, node, name);
        vitte_c17_ast_write(emitter, " {\n");
        for (index = 1u; index < node->child_count; ++index) {
            const vitte_ast_node_t *variant =
                vitte_c17_ast_child(emitter, node, index);
            vitte_c17_ast_write(emitter, "    ");
            vitte_c17_ast_emit_declaration_name(emitter, node, name);
            vitte_c17_ast_write(emitter, "_");
            vitte_c17_ast_write_token(
                emitter,
                vitte_c17_ast_child(emitter, variant, 0u));
            ++emitted;
            vitte_c17_ast_write(
                emitter,
                emitted < variant_count ? ",\n" : "\n");
        }
        vitte_c17_ast_write(emitter, "} ");
        vitte_c17_ast_emit_declaration_name(emitter, node, name);
        vitte_c17_ast_write(emitter, ";\n\n");
        return !emitter->failed;
    }

    vitte_c17_ast_write(emitter, "typedef enum ");
    vitte_c17_ast_emit_declaration_name(emitter, node, name);
    vitte_c17_ast_write(emitter, "_tag {\n");
    {
        size_t emitted = 0u;
        for (index = 1u; index < node->child_count; ++index) {
            const vitte_ast_node_t *variant =
                vitte_c17_ast_child(emitter, node, index);
            vitte_c17_ast_write(emitter, "    ");
            vitte_c17_ast_emit_declaration_name(emitter, node, name);
            vitte_c17_ast_write(emitter, "_tag_");
            vitte_c17_ast_write_token(
                emitter,
                vitte_c17_ast_child(emitter, variant, 0u));
            ++emitted;
            vitte_c17_ast_write(
                emitter,
                emitted < variant_count ? ",\n" : "\n");
        }
    }
    vitte_c17_ast_write(emitter, "} ");
    vitte_c17_ast_emit_declaration_name(emitter, node, name);
    vitte_c17_ast_write(emitter, "_tag;\n\nstruct ");
    vitte_c17_ast_emit_declaration_name(emitter, node, name);
    vitte_c17_ast_write(emitter, " {\n    ");
    vitte_c17_ast_emit_declaration_name(emitter, node, name);
    vitte_c17_ast_write(emitter, "_tag tag;\n    union {\n");
    for (index = 1u; index < node->child_count; ++index) {
        const vitte_ast_node_t *variant =
            vitte_c17_ast_child(emitter, node, index);
        const vitte_ast_node_t *variant_name =
            vitte_c17_ast_child(emitter, variant, 0u);
        size_t payload_index;

        if (variant->child_count == 1u) {
            continue;
        }
        vitte_c17_ast_write(emitter, "        struct {\n");
        for (payload_index = 1u;
             payload_index < variant->child_count;
             ++payload_index) {
            const vitte_ast_node_t *payload_type =
                vitte_c17_ast_child(emitter, variant, payload_index);
            vitte_c17_ast_write(emitter, "            ");
            if (payload_type != NULL &&
                payload_type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
                payload_type->child_count == 2u) {
                vitte_c17_ast_emit_type(
                    emitter,
                    vitte_c17_ast_child(emitter, payload_type, 1u));
                vitte_c17_ast_write(emitter, " ");
                vitte_c17_ast_write_payload_field_name(
                    emitter, payload_index - 1u);
                vitte_c17_ast_write(emitter, "[");
                vitte_c17_ast_emit_expression(
                    emitter,
                    vitte_c17_ast_child(emitter, payload_type, 0u));
                vitte_c17_ast_write(emitter, "]");
            } else {
                vitte_c17_ast_emit_type(emitter, payload_type);
                vitte_c17_ast_write(emitter, " ");
                vitte_c17_ast_write_payload_field_name(
                    emitter, payload_index - 1u);
            }
            vitte_c17_ast_write(emitter, ";\n");
        }
        vitte_c17_ast_write(emitter, "        } ");
        vitte_c17_ast_write_token(emitter, variant_name);
        vitte_c17_ast_write(emitter, ";\n");
    }
    vitte_c17_ast_write(emitter, "    } payload;\n};\n\n");
    return !emitter->failed;
}

static void
vitte_c17_ast_emit_pick_forward_declarations_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);
        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_PICK_DECL &&
            !vitte_c17_ast_pick_has_generic_parameters(
                emitter, declaration) &&
            vitte_c17_ast_pick_has_payload(emitter, declaration)) {
            const vitte_ast_node_t *name =
                vitte_c17_ast_child(emitter, declaration, 0u);
            if (name == NULL) {
                emitter->failed = true;
                return;
            }
            vitte_c17_ast_write(emitter, "typedef struct ");
            vitte_c17_ast_emit_declaration_name(
                emitter, declaration, name);
            vitte_c17_ast_write(emitter, " ");
            vitte_c17_ast_emit_declaration_name(
                emitter, declaration, name);
            vitte_c17_ast_write(emitter, ";\n");
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count > 1u) {
            const vitte_ast_node_t *body =
                vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL || body->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
                return;
            }
            if (emitter->space_depth >=
                sizeof(emitter->space_stack) /
                    sizeof(emitter->space_stack[0])) {
                emitter->failed = true;
                return;
            }
            emitter->space_stack[emitter->space_depth++] = declaration;
            vitte_c17_ast_emit_pick_forward_declarations_in_scope(
                emitter, body);
            --emitter->space_depth;
        }
    }
}

static void
vitte_c17_ast_emit_generic_pick_forward_declarations(
    vitte_c17_ast_emitter_t *emitter)
{
    size_t index;

    for (index = 0u;
         index < emitter->generic_form_count && !emitter->failed;
         ++index) {
        vitte_c17_ast_generic_form_instance_t *instance =
            &emitter->generic_forms[index];
        if (instance->declaration == NULL ||
            instance->declaration->kind != VITTE_AST_NODE_PICK_DECL ||
            !vitte_c17_ast_pick_has_payload(
                emitter, instance->declaration) ||
            instance->forward_declared) {
            continue;
        }
        vitte_c17_ast_write(emitter, "typedef struct ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_emit_generic_form_instance_name(emitter, instance);
        vitte_c17_ast_write(emitter, ";\n");
        instance->forward_declared = true;
    }
}

static void
vitte_c17_ast_emit_picks_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    bool with_payload)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration;

        declaration = vitte_c17_ast_child(emitter, scope, index);
        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_PICK_DECL &&
            !vitte_c17_ast_pick_has_generic_parameters(
                emitter, declaration) &&
            vitte_c17_ast_pick_has_payload(emitter, declaration) ==
                with_payload) {
            (void)vitte_c17_ast_emit_pick(emitter, declaration);
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count > 1u) {
            const vitte_ast_node_t *body;

            body = vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL ||
                body->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
                return;
            }
            if (emitter->space_depth >=
                sizeof(emitter->space_stack) /
                    sizeof(emitter->space_stack[0])) {
                emitter->failed = true;
                return;
            }
            emitter->space_stack[emitter->space_depth++] = declaration;
            vitte_c17_ast_emit_picks_in_scope(
                emitter, body, with_payload);
            --emitter->space_depth;
        }
    }
}

static bool
vitte_c17_ast_emit_constant(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *name;
    const vitte_ast_node_t *type;
    const vitte_ast_node_t *initializer;

    name = vitte_c17_ast_child(emitter, node, 0u);
    type = vitte_c17_ast_child(emitter, node, 1u);
    initializer =
        node->child_count > 2u
            ? vitte_c17_ast_child(emitter, node, 2u)
            : vitte_c17_ast_child(emitter, node, 1u);
    if (name == NULL || type == NULL || initializer == NULL) {
        return false;
    }

    if (type->kind == VITTE_AST_NODE_ARRAY_TYPE &&
        type->child_count == 1u &&
        initializer->kind == VITTE_AST_NODE_ARRAY_EXPR) {
        const vitte_ast_node_t *element_type;
        size_t index;

        element_type = vitte_c17_ast_child(emitter, type, 0u);
        if (element_type == NULL) {
            return false;
        }
        if (initializer->child_count != 0u) {
            vitte_c17_ast_write(emitter, "static ");
            vitte_c17_ast_emit_type(emitter, element_type);
            vitte_c17_ast_write(emitter, " const ");
            vitte_c17_ast_emit_declaration_name(emitter, node, name);
            vitte_c17_ast_write(
                emitter,
                "_data[] VITTE_C17_AST_UNUSED = {");
            for (index = 0u; index < initializer->child_count; ++index) {
                if (index != 0u) {
                    vitte_c17_ast_write(emitter, ", ");
                }
                vitte_c17_ast_emit_expression(
                    emitter,
                    vitte_c17_ast_child(emitter, initializer, index));
            }
            vitte_c17_ast_write(emitter, "};\n");
        }
        vitte_c17_ast_write(emitter, "static const VitteArray ");
        vitte_c17_ast_emit_declaration_name(emitter, node, name);
        vitte_c17_ast_write(
            emitter,
            " VITTE_C17_AST_UNUSED = { ");
        if (initializer->child_count != 0u) {
            vitte_c17_ast_write(emitter, "(void *)");
            vitte_c17_ast_emit_declaration_name(emitter, node, name);
            vitte_c17_ast_write(emitter, "_data, ");
        } else {
            vitte_c17_ast_write(emitter, "NULL, ");
        }
        if (fprintf(
                emitter->output,
                "%zuu, %zuu, sizeof(",
                initializer->child_count,
                initializer->child_count) < 0) {
            emitter->failed = true;
        }
        vitte_c17_ast_emit_type(emitter, element_type);
        vitte_c17_ast_write(emitter, ") } ;\n");
        return !emitter->failed;
    }

    if (node->kind == VITTE_AST_NODE_STATIC_DECL) {
        vitte_c17_ast_write(emitter, "static ");
        if (vitte_c17_ast_node_has_thread_local_attribute(
                emitter,
                node)) {
            vitte_c17_ast_write(emitter, "_Thread_local ");
        }
        if ((node->flags & VITTE_AST_FLAG_MUTABLE) == 0u) {
            if (vitte_c17_ast_is_string_type(emitter, type)) {
                vitte_c17_ast_emit_type(emitter, type);
                vitte_c17_ast_write(emitter, " const ");
            } else {
                vitte_c17_ast_write(emitter, "const ");
                vitte_c17_ast_emit_type(emitter, type);
                vitte_c17_ast_write(emitter, " ");
            }
        } else {
            vitte_c17_ast_emit_type(emitter, type);
            vitte_c17_ast_write(emitter, " ");
        }
    } else {
        vitte_c17_ast_write(
            emitter,
            vitte_c17_ast_is_string_type(emitter, type)
                ? "static "
                : "static const ");
        vitte_c17_ast_emit_type(emitter, type);
        vitte_c17_ast_write(emitter, " ");
    }
    vitte_c17_ast_emit_declaration_name(emitter, node, name);
    if (!(node->kind == VITTE_AST_NODE_STATIC_DECL &&
          (node->flags & VITTE_AST_FLAG_MUTABLE) != 0u)) {
        vitte_c17_ast_write(
            emitter,
            " VITTE_C17_AST_UNUSED");
    }
    vitte_c17_ast_write(emitter, " = ");
    vitte_c17_ast_emit_expression(emitter, initializer);
    vitte_c17_ast_write(emitter, ";\n");
    return !emitter->failed;
}

static void
vitte_c17_ast_emit_constants_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }
    for (index = 0u; index < scope->child_count; ++index) {
        const vitte_ast_node_t *declaration;

        declaration = vitte_c17_ast_child(emitter, scope, index);
        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_CONST_DECL ||
            declaration->kind == VITTE_AST_NODE_STATIC_DECL) {
            (void)vitte_c17_ast_emit_constant(emitter, declaration);
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count > 1u) {
            const vitte_ast_node_t *body;

            body = vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL ||
                body->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
                return;
            }
            if (emitter->space_depth >=
                sizeof(emitter->space_stack) /
                    sizeof(emitter->space_stack[0])) {
                emitter->failed = true;
                return;
            }
            emitter->space_stack[emitter->space_depth++] = declaration;
            vitte_c17_ast_emit_constants_in_scope(emitter, body);
            --emitter->space_depth;
        }
    }
}

static bool
vitte_c17_ast_is_terminal_intrinsic(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *name;

    if (node == NULL ||
        node->kind != VITTE_AST_NODE_EXTERN_PROC_DECL) {
        return false;
    }
    name = vitte_c17_ast_child(emitter, node, 0u);
    return vitte_c17_ast_token_is(emitter, name, "terminal_clear") ||
           vitte_c17_ast_token_is(emitter, name, "terminal_hide_cursor") ||
           vitte_c17_ast_token_is(emitter, name, "terminal_show_cursor") ||
           vitte_c17_ast_token_is(emitter, name, "terminal_print") ||
           vitte_c17_ast_token_is(emitter, name, "terminal_print_i64") ||
           vitte_c17_ast_token_is(emitter, name, "terminal_key_pressed") ||
           vitte_c17_ast_token_is(emitter, name, "terminal_read_key") ||
           vitte_c17_ast_token_is(emitter, name, "terminal_sleep_ms");
}

static bool
vitte_c17_ast_is_intrinsic(
    const vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node)
{
    const vitte_ast_node_t *name;

    if (node == NULL ||
        node->kind != VITTE_AST_NODE_EXTERN_PROC_DECL) {
        return false;
    }
    name = vitte_c17_ast_child(emitter, node, 0u);
    return vitte_c17_ast_token_is(emitter, name, "io_print") ||
           vitte_c17_ast_token_is(emitter, name, "io_print_i64") ||
           vitte_c17_ast_token_is(emitter, name, "io_read_i64") ||
           vitte_c17_ast_token_is(emitter, name, "random_i64") ||
           vitte_c17_ast_token_is(emitter, name, "malloc") ||
           vitte_c17_ast_token_is(emitter, name, "free") ||
           vitte_c17_ast_token_is(emitter, name, "memmove") ||
           vitte_c17_ast_is_terminal_intrinsic(emitter, node);
}

static bool
vitte_c17_ast_emit_function(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function)
{
    const vitte_ast_node_t *body;
    const vitte_ast_node_t *previous;
    const vitte_ast_node_t *return_type;
    unsigned int previous_async_signature_mode;

    body =
        vitte_c17_ast_find_child_kind(
            emitter,
            function,
            VITTE_AST_NODE_BLOCK);
    if (body == NULL) {
        return false;
    }

    previous = emitter->function;
    emitter->function = function;
    return_type = vitte_c17_ast_function_return_type(emitter, function);
    vitte_c17_ast_emit_source_line(emitter, function);
    previous_async_signature_mode = emitter->async_signature_mode;
    if ((function->flags & VITTE_AST_FLAG_ASYNC) != 0u) {
        emitter->async_signature_mode = 2u;
    }
    vitte_c17_ast_emit_function_signature(
        emitter,
        function,
        false);
    emitter->async_signature_mode = previous_async_signature_mode;
    vitte_c17_ast_emit_coalesce_temporaries(
        emitter,
        body,
        0u);
    vitte_c17_ast_emit_block_contents(
        emitter,
        body,
        1u);
    if (return_type != NULL &&
        !vitte_c17_ast_type_is_named(emitter, return_type, "void") &&
        !vitte_c17_ast_type_is_named(emitter, return_type, "unit")) {
        vitte_c17_ast_write(emitter, "    abort();\n");
    }
    vitte_c17_ast_write(emitter, "}\n\n");
    emitter->function = previous;
    return !emitter->failed;
}

static bool
vitte_c17_ast_generic_impl_bindings(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *implementation,
    const vitte_ast_node_t *target,
    const vitte_ast_node_t *concrete_type,
    const vitte_ast_node_t **arguments,
    size_t argument_capacity,
    size_t *argument_count)
{
    size_t generic_count = 0u;
    size_t index;
    size_t type_count = 0u;
    const vitte_ast_node_t *implementation_target = NULL;

    if (emitter == NULL || implementation == NULL || target == NULL ||
        concrete_type == NULL || arguments == NULL ||
        argument_count == NULL ||
        target->kind != VITTE_AST_NODE_TYPE_EXPR ||
        concrete_type->kind != VITTE_AST_NODE_TYPE_EXPR ||
        target->child_count != concrete_type->child_count ||
        target->child_count < 2u) {
        return false;
    }
    for (index = 0u; index < implementation->child_count; ++index) {
        const vitte_ast_node_t *child =
            vitte_c17_ast_child(emitter, implementation, index);
        if (child != NULL &&
            child->kind == VITTE_AST_NODE_GENERIC_PARAM) {
            ++generic_count;
        }
        if (vitte_c17_ast_is_type_node(child)) {
            ++type_count;
            if (type_count == 2u) {
                implementation_target = child;
            }
        }
    }
    if (generic_count == 0u || generic_count > argument_capacity ||
        implementation_target != target ||
        !vitte_c17_ast_type_syntax_equal(
            emitter,
            vitte_c17_ast_child(emitter, target, 0u),
            vitte_c17_ast_child(emitter, concrete_type, 0u),
            0u)) {
        return false;
    }
    for (index = 0u; index < generic_count; ++index) {
        arguments[index] = NULL;
    }
    for (index = 1u; index < target->child_count; ++index) {
        const vitte_ast_node_t *pattern =
            vitte_c17_ast_child(emitter, target, index);
        const vitte_ast_node_t *actual =
            vitte_c17_ast_child(emitter, concrete_type, index);
        size_t generic_index;
        bool is_parameter = false;

        if (pattern == NULL || actual == NULL) {
            return false;
        }
        for (generic_index = 0u;
             generic_index < generic_count;
             ++generic_index) {
            const vitte_ast_node_t *parameter_name = NULL;
            size_t child_index;
            size_t found = 0u;

            for (child_index = 0u;
                 child_index < implementation->child_count;
                 ++child_index) {
                const vitte_ast_node_t *child =
                    vitte_c17_ast_child(
                        emitter, implementation, child_index);
                if (child != NULL &&
                    child->kind == VITTE_AST_NODE_GENERIC_PARAM) {
                    if (found == generic_index) {
                        parameter_name =
                            vitte_c17_ast_child(emitter, child, 0u);
                        break;
                    }
                    ++found;
                }
            }
            if (parameter_name != NULL &&
                pattern->kind == VITTE_AST_NODE_TYPE_EXPR &&
                pattern->child_count == 1u) {
                const vitte_ast_node_t *parameter_path =
                    vitte_c17_ast_child(emitter, pattern, 0u);
                if (parameter_path != NULL &&
                    parameter_path->kind == VITTE_AST_NODE_PATH &&
                    parameter_path->child_count == 1u &&
                    vitte_c17_ast_token_equal(
                        emitter,
                        parameter_name,
                        vitte_c17_ast_child(
                            emitter, parameter_path, 0u))) {
                    is_parameter = true;
                    if (arguments[generic_index] == NULL) {
                        arguments[generic_index] = actual;
                    } else if (!vitte_c17_ast_type_syntax_equal(
                                   emitter,
                                   arguments[generic_index],
                                   actual,
                                   0u)) {
                        return false;
                    }
                    break;
                }
            }
        }
        if (!is_parameter &&
            !vitte_c17_ast_type_syntax_equal(
                emitter, pattern, actual, 0u)) {
            return false;
        }
    }
    for (index = 0u; index < generic_count; ++index) {
        if (arguments[index] == NULL) {
            return false;
        }
    }
    *argument_count = generic_count;
    return true;
}

static void
vitte_c17_ast_emit_impl_methods_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    bool prototypes)
{
    size_t declaration_index;

    if (emitter == NULL || scope == NULL || emitter->failed) {
        return;
    }
    for (declaration_index = 0u;
         declaration_index < scope->child_count && !emitter->failed;
         ++declaration_index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, declaration_index);

        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_IMPL_DECL) {
            bool has_generic_parameters = false;
            size_t method_index;
            size_t method_count = 0u;

            for (method_index = 0u;
                 method_index < declaration->child_count;
                 ++method_index) {
                const vitte_ast_node_t *member = vitte_c17_ast_child(
                    emitter, declaration, method_index);
                if (member != NULL &&
                    member->kind == VITTE_AST_NODE_GENERIC_PARAM) {
                    has_generic_parameters = true;
                }
                if (member != NULL &&
                    member->kind == VITTE_AST_NODE_PROC_DECL) {
                    ++method_count;
                }
            }
            if (method_count != 0u && has_generic_parameters) {
                const vitte_ast_node_t *target = NULL;
                size_t type_count = 0u;
                size_t child_index;
                size_t instance_index;

                for (child_index = 0u;
                     child_index < declaration->child_count;
                     ++child_index) {
                    const vitte_ast_node_t *child =
                        vitte_c17_ast_child(
                            emitter, declaration, child_index);
                    if (vitte_c17_ast_is_type_node(child)) {
                        ++type_count;
                        if (type_count == 2u) {
                            target = child;
                            break;
                        }
                    }
                }
                if (target == NULL) {
                    emitter->failed = true;
                    return;
                }
                for (instance_index = 0u;
                     instance_index < emitter->generic_form_count &&
                     !emitter->failed;
                     ++instance_index) {
                    const vitte_c17_ast_generic_form_instance_t *instance =
                        &emitter->generic_forms[instance_index];
                    const vitte_ast_node_t *bindings[128];
                    size_t binding_count = 0u;
                    size_t saved_substitution_count =
                        emitter->generic_substitution_count;
                    size_t generic_index = 0u;

                    if (instance->type_expression == NULL ||
                        !vitte_c17_ast_generic_impl_bindings(
                            emitter,
                            declaration,
                            target,
                            instance->type_expression,
                            bindings,
                            sizeof(bindings) / sizeof(bindings[0]),
                            &binding_count)) {
                        continue;
                    }
                    if (saved_substitution_count >
                            emitter->generic_substitution_capacity ||
                        binding_count >
                            emitter->generic_substitution_capacity -
                                saved_substitution_count) {
                        emitter->failed = true;
                        return;
                    }
                    for (child_index = 0u;
                         child_index < declaration->child_count;
                         ++child_index) {
                        const vitte_ast_node_t *generic_parameter =
                            vitte_c17_ast_child(
                                emitter, declaration, child_index);
                        if (generic_parameter != NULL &&
                            generic_parameter->kind ==
                                VITTE_AST_NODE_GENERIC_PARAM) {
                            size_t substitution_index =
                                saved_substitution_count + generic_index;
                            emitter->generic_parameter_names[
                                substitution_index] =
                                vitte_c17_ast_child(
                                    emitter, generic_parameter, 0u);
                            emitter->generic_arguments[
                                substitution_index] =
                                bindings[generic_index];
                            emitter->generic_c_type_names[
                                substitution_index] = NULL;
                            ++generic_index;
                        }
                    }
                    emitter->generic_substitution_count =
                        saved_substitution_count + binding_count;
                    for (method_index = 0u;
                         method_index < declaration->child_count &&
                         !emitter->failed;
                         ++method_index) {
                        const vitte_ast_node_t *method =
                            vitte_c17_ast_child(
                                emitter, declaration, method_index);
                        const char *previous_override;
                        char method_name[128];

                        if (method == NULL ||
                            method->kind != VITTE_AST_NODE_PROC_DECL) {
                            continue;
                        }
                        if (snprintf(
                                method_name,
                                sizeof(method_name),
                                "vitte_impl_method_%zu_g%zu",
                                (size_t)method->id,
                                instance_index) < 0) {
                            emitter->failed = true;
                            break;
                        }
                        previous_override =
                            emitter->function_c_name_override;
                        emitter->function_c_name_override = method_name;
                        if (prototypes) {
                            vitte_c17_ast_emit_function_signature(
                                emitter, method, true);
                        } else if (!vitte_c17_ast_emit_function(
                                       emitter, method)) {
                            emitter->failed = true;
                        }
                        emitter->function_c_name_override =
                            previous_override;
                    }
                    while (emitter->generic_substitution_count >
                           saved_substitution_count) {
                        --emitter->generic_substitution_count;
                        emitter->generic_parameter_names[
                            emitter->generic_substitution_count] = NULL;
                        emitter->generic_arguments[
                            emitter->generic_substitution_count] = NULL;
                        emitter->generic_c_type_names[
                            emitter->generic_substitution_count] = NULL;
                    }
                }
                continue;
            }
            for (method_index = 0u;
                 method_index < declaration->child_count &&
                     !emitter->failed;
                 ++method_index) {
                const vitte_ast_node_t *method = vitte_c17_ast_child(
                    emitter, declaration, method_index);
                const char *previous_override;
                char method_name[64];
                int name_length;

                if (method == NULL ||
                    method->kind != VITTE_AST_NODE_PROC_DECL) {
                    continue;
                }
                name_length = snprintf(
                    method_name,
                    sizeof(method_name),
                    "vitte_impl_method_%zu",
                    (size_t)method->id);
                if (name_length < 0 ||
                    (size_t)name_length >= sizeof(method_name)) {
                    emitter->failed = true;
                    return;
                }
                previous_override =
                    emitter->function_c_name_override;
                emitter->function_c_name_override = method_name;
                if (prototypes) {
                    vitte_c17_ast_emit_function_signature(
                        emitter, method, true);
                } else if (!vitte_c17_ast_emit_function(
                               emitter, method)) {
                    emitter->failed = true;
                }
                emitter->function_c_name_override = previous_override;
            }
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count >= 2u) {
            const vitte_ast_node_t *body = vitte_c17_ast_child(
                emitter, declaration, 1u);
            if (body != NULL &&
                body->kind == VITTE_AST_NODE_BLOCK) {
                if (emitter->space_depth >=
                    sizeof(emitter->space_stack) /
                        sizeof(emitter->space_stack[0])) {
                    emitter->failed = true;
                    return;
                }
                emitter->space_stack[emitter->space_depth++] = declaration;
                vitte_c17_ast_emit_impl_methods_in_scope(
                    emitter, body, prototypes);
                --emitter->space_depth;
            }
        }
    }
}

static bool
vitte_c17_ast_prepare_generic_proc_instance(
    vitte_c17_ast_emitter_t *emitter,
    size_t instance_index,
    size_t *saved_substitution_count)
{
    const vitte_c17_ast_generic_proc_instance_t *instance;
    size_t parameter_count;
    size_t index;

    if (emitter == NULL || saved_substitution_count == NULL ||
        instance_index >= emitter->generic_proc_count) {
        return false;
    }
    instance = &emitter->generic_procs[instance_index];
    parameter_count = vitte_c17_ast_generic_form_parameter_count(
        emitter, instance->declaration, NULL);
    if (emitter->generic_substitution_count >
            emitter->generic_substitution_capacity ||
        parameter_count != instance->argument_count ||
        parameter_count > emitter->generic_substitution_capacity -
            emitter->generic_substitution_count) {
        emitter->failed = true;
        return false;
    }
    *saved_substitution_count = emitter->generic_substitution_count;
    for (index = 0u; index < parameter_count; ++index) {
        const vitte_ast_node_t *parameter_name =
            vitte_c17_ast_generic_form_parameter_name(
                emitter, instance->declaration, index);
        size_t substitution_index = emitter->generic_substitution_count;
        if (parameter_name == NULL ||
            (instance->argument_c_types[index] == NULL &&
             instance->argument_type_nodes[index] == NULL)) {
            emitter->failed = true;
            emitter->generic_substitution_count =
                *saved_substitution_count;
            return false;
        }
        emitter->generic_parameter_names[substitution_index] = parameter_name;
        emitter->generic_arguments[substitution_index] =
            instance->argument_type_nodes[index];
        emitter->generic_c_type_names[substitution_index] =
            instance->argument_c_types[index];
        ++emitter->generic_substitution_count;
    }
    emitter->generic_proc_c_name = instance->c_name;
    return true;
}

static void
vitte_c17_ast_restore_generic_proc_instance(
    vitte_c17_ast_emitter_t *emitter,
    size_t saved_substitution_count)
{
    size_t index;
    if (emitter == NULL) { return; }
    for (index = saved_substitution_count;
         index < emitter->generic_substitution_count;
         ++index) {
        emitter->generic_parameter_names[index] = NULL;
        emitter->generic_arguments[index] = NULL;
        emitter->generic_c_type_names[index] = NULL;
    }
    emitter->generic_substitution_count = saved_substitution_count;
    emitter->generic_proc_c_name = NULL;
}

static void
vitte_c17_ast_collect_nested_generic_proc_instances(
    vitte_c17_ast_emitter_t *emitter)
{
    size_t instance_index = 0u;

    while (emitter != NULL &&
           instance_index < emitter->generic_proc_count &&
           !emitter->failed) {
        const vitte_ast_node_t *procedure =
            emitter->generic_procs[instance_index].declaration;
        const vitte_ast_node_t *previous_function = emitter->function;
        size_t saved_substitution_count;
        size_t node_index;

        if (procedure == NULL ||
            !vitte_c17_ast_prepare_generic_proc_instance(
                emitter,
                instance_index,
                &saved_substitution_count)) {
            emitter->failed = true;
            break;
        }
        emitter->function = procedure;
        emitter->generic_proc_context_active = true;
        emitter->generic_proc_context_index = instance_index;
        for (node_index = 0u;
             node_index < emitter->parser->node_count && !emitter->failed;
             ++node_index) {
            const vitte_ast_node_t *node =
                &emitter->parser->nodes[node_index];
            if (node->kind == VITTE_AST_NODE_CALL_EXPR &&
                vitte_c17_ast_enclosing_procedure(emitter, node) ==
                    procedure) {
                vitte_c17_ast_register_generic_proc_call(emitter, node);
            }
        }
        emitter->generic_proc_context_active = false;
        emitter->function = previous_function;
        vitte_c17_ast_restore_generic_proc_instance(
            emitter, saved_substitution_count);
        ++instance_index;
    }
}

static void
vitte_c17_ast_emit_generic_proc_prototypes(
    vitte_c17_ast_emitter_t *emitter)
{
    size_t index;
    for (index = 0u;
         index < emitter->generic_proc_count && !emitter->failed;
         ++index) {
        size_t saved_substitution_count;
        if (!vitte_c17_ast_prepare_generic_proc_instance(
                emitter, index, &saved_substitution_count)) {
            emitter->failed = true;
            break;
        }
        vitte_c17_ast_emit_function_signature(
            emitter,
            emitter->generic_procs[index].declaration,
            true);
        vitte_c17_ast_restore_generic_proc_instance(
            emitter, saved_substitution_count);
    }
}

static void
vitte_c17_ast_emit_generic_proc_definitions(
    vitte_c17_ast_emitter_t *emitter)
{
    size_t index;
    for (index = 0u;
         index < emitter->generic_proc_count && !emitter->failed;
         ++index) {
        size_t saved_substitution_count;
        if (!vitte_c17_ast_prepare_generic_proc_instance(
                emitter, index, &saved_substitution_count)) {
            emitter->failed = true;
            break;
        }
        emitter->generic_proc_context_active = true;
        emitter->generic_proc_context_index = index;
        if (!vitte_c17_ast_emit_function(
                emitter,
                emitter->generic_procs[index].declaration)) {
            emitter->failed = true;
        }
        emitter->generic_proc_context_active = false;
        vitte_c17_ast_restore_generic_proc_instance(
            emitter, saved_substitution_count);
    }
}

static void
vitte_c17_ast_emit_space_functions(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope,
    bool prototypes)
{
    size_t index;

    if (scope == NULL || emitter->failed) {
        return;
    }

    for (index = 0u;
         index < scope->child_count && !emitter->failed;
         ++index) {
        const vitte_ast_node_t *declaration;

        declaration = vitte_c17_ast_child(emitter, scope, index);
        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind != VITTE_AST_NODE_SPACE_DECL) {
            continue;
        }
        if (declaration->child_count < 2u) {
            continue;
        }
        if (emitter->space_depth >=
            sizeof(emitter->space_stack) /
                sizeof(emitter->space_stack[0])) {
            emitter->failed = true;
            return;
        }

        emitter->space_stack[emitter->space_depth++] = declaration;
        {
            const vitte_ast_node_t *body;
            size_t child_index;

            body = vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL ||
                body->kind != VITTE_AST_NODE_BLOCK) {
                emitter->failed = true;
            } else {
                for (child_index = 0u;
                     child_index < body->child_count &&
                     !emitter->failed;
                     ++child_index) {
                    const vitte_ast_node_t *member;

                    member = vitte_c17_ast_child(
                        emitter,
                        body,
                        child_index);
                    if (member == NULL) {
                        emitter->failed = true;
                        break;
                    }
                    if (vitte_c17_ast_is_core_pointer_generic(
                            emitter,
                            member)) {
                        continue;
                    }
                    if (member->kind == VITTE_AST_NODE_PROC_DECL) {
                        size_t generic_parameter_count;
                        generic_parameter_count =
                            vitte_c17_ast_generic_form_parameter_count(
                                emitter,
                                member,
                                NULL);
                        if (generic_parameter_count != 0u) {
                            if ((member->flags & VITTE_AST_FLAG_ASYNC) != 0u) {
                                emitter->failed = true;
                            }
                            continue;
                        }
                        if (prototypes) {
                            if ((member->flags & VITTE_AST_FLAG_ASYNC) != 0u) {
                                vitte_c17_ast_emit_async_task_prototypes(
                                    emitter, member);
                            } else {
                                vitte_c17_ast_emit_function_signature(
                                    emitter,
                                    member,
                                    true);
                            }
                        } else if (!vitte_c17_ast_emit_function(
                                       emitter,
                                       member)) {
                            emitter->failed = true;
                        }
                    } else if (member->kind ==
                                   VITTE_AST_NODE_EXTERN_PROC_DECL &&
                               prototypes &&
                               !vitte_c17_ast_is_intrinsic(
                                   emitter,
                                   member)) {
                        vitte_c17_ast_emit_function_signature(
                            emitter,
                            member,
                            true);
                    }
                }
                vitte_c17_ast_emit_space_functions(
                    emitter,
                    body,
                    prototypes);
            }
        }
        --emitter->space_depth;
    }
}

static void
vitte_c17_ast_emit_async_task_helper_definitions(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *function)
{
    const vitte_ast_node_t *return_type;
    size_t index;
    size_t parameter_index;
    unsigned int previous_mode;
    bool result_is_void;

    if (function == NULL || function->kind != VITTE_AST_NODE_PROC_DECL ||
        (function->flags & VITTE_AST_FLAG_ASYNC) == 0u || emitter->failed) {
        return;
    }
    if (vitte_c17_ast_generic_form_parameter_count(
            emitter, function, NULL) != 0u) {
        emitter->failed = true;
        return;
    }
    return_type = vitte_c17_ast_function_return_type(emitter, function);
    result_is_void = vitte_c17_ast_async_result_is_void(
        emitter, function);

    previous_mode = emitter->async_signature_mode;
    emitter->async_signature_mode = 1u;
    vitte_c17_ast_emit_function_signature(emitter, function, false);
    emitter->async_signature_mode = previous_mode;
    vitte_c17_ast_write(emitter, "    ");
    vitte_c17_ast_write_async_task_type_name(emitter, function);
    vitte_c17_ast_write(emitter, " task = { 0 };\n");
    parameter_index = 0u;
    for (index = 1u; index < function->child_count; ++index) {
        const vitte_ast_node_t *parameter =
            vitte_c17_ast_child(emitter, function, index);
        if (parameter == NULL ||
            parameter->kind != VITTE_AST_NODE_PARAMETER) {
            continue;
        }
        (void)fprintf(
            emitter->output,
            "    task.argument_%zu = ",
            parameter_index++);
        vitte_c17_ast_write_token(
            emitter,
            vitte_c17_ast_child(emitter, parameter, 0u));
        vitte_c17_ast_write(emitter, ";\n");
    }
    vitte_c17_ast_write(emitter, "    return task;\n}\n\n");

    vitte_c17_ast_write(emitter, "static ");
    if (result_is_void) {
        vitte_c17_ast_write(emitter, "void ");
    } else {
        vitte_c17_ast_emit_type(emitter, return_type);
        vitte_c17_ast_write(emitter, " ");
    }
    vitte_c17_ast_write_async_task_function_name(
        emitter, function, "await");
    vitte_c17_ast_write(emitter, "(");
    vitte_c17_ast_write_async_task_type_name(emitter, function);
    vitte_c17_ast_write(emitter, " *task) {\n    if (!task->completed) {\n        ");
    if (!result_is_void) {
        vitte_c17_ast_write(emitter, "task->result = ");
    }
    vitte_c17_ast_write_async_task_function_name(
        emitter, function, "run");
    vitte_c17_ast_write(emitter, "(");
    parameter_index = 0u;
    for (index = 1u; index < function->child_count; ++index) {
        const vitte_ast_node_t *parameter =
            vitte_c17_ast_child(emitter, function, index);
        if (parameter == NULL ||
            parameter->kind != VITTE_AST_NODE_PARAMETER) {
            continue;
        }
        if (parameter_index != 0u) {
            vitte_c17_ast_write(emitter, ", ");
        }
        (void)fprintf(
            emitter->output,
            "task->argument_%zu",
            parameter_index++);
    }
    vitte_c17_ast_write(
        emitter,
        ");\n        task->completed = true;\n    }\n");
    if (!result_is_void) {
        vitte_c17_ast_write(emitter, "    return task->result;\n");
    }
    vitte_c17_ast_write(emitter, "}\n\nstatic ");
    if (result_is_void) {
        vitte_c17_ast_write(emitter, "void ");
    } else {
        vitte_c17_ast_emit_type(emitter, return_type);
        vitte_c17_ast_write(emitter, " ");
    }
    vitte_c17_ast_write_async_task_function_name(
        emitter, function, "await_value");
    vitte_c17_ast_write(emitter, "(");
    vitte_c17_ast_write_async_task_type_name(emitter, function);
    vitte_c17_ast_write(emitter, " task) {\n    ");
    if (!result_is_void) {
        vitte_c17_ast_write(emitter, "return ");
    }
    vitte_c17_ast_write_async_task_function_name(
        emitter, function, "await");
    vitte_c17_ast_write(emitter, "(&task);\n}\n\n");
}

static void
vitte_c17_ast_emit_async_task_helpers_in_scope(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *scope)
{
    size_t index;
    if (scope == NULL || emitter->failed) { return; }
    for (index = 0u; index < scope->child_count && !emitter->failed;
         ++index) {
        const vitte_ast_node_t *declaration =
            vitte_c17_ast_child(emitter, scope, index);
        if (declaration == NULL) {
            emitter->failed = true;
            return;
        }
        if (declaration->kind == VITTE_AST_NODE_PROC_DECL) {
            vitte_c17_ast_emit_async_task_helper_definitions(
                emitter, declaration);
        } else if (declaration->kind == VITTE_AST_NODE_SPACE_DECL &&
                   declaration->child_count >= 2u) {
            const vitte_ast_node_t *body =
                vitte_c17_ast_child(emitter, declaration, 1u);
            if (body == NULL || body->kind != VITTE_AST_NODE_BLOCK ||
                emitter->space_depth >=
                    sizeof(emitter->space_stack) /
                        sizeof(emitter->space_stack[0])) {
                emitter->failed = true;
                return;
            }
            emitter->space_stack[emitter->space_depth++] = declaration;
            vitte_c17_ast_emit_async_task_helpers_in_scope(
                emitter, body);
            --emitter->space_depth;
        }
    }
}

static bool
vitte_c17_ast_emit_test(
    vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *test,
    size_t test_index)
{
    const vitte_ast_node_t *body;
    const vitte_ast_node_t *previous;

    body =
        vitte_c17_ast_find_child_kind(
            emitter,
            test,
            VITTE_AST_NODE_BLOCK);
    if (body == NULL) {
        return false;
    }

    vitte_c17_ast_emit_source_line(emitter, test);
    if (fprintf(
            emitter->output,
            "static void vitte_test_%zu(void) {\n",
            test_index) < 0) {
        emitter->failed = true;
        return false;
    }

    previous = emitter->function;
    emitter->function = test;
    vitte_c17_ast_emit_block_contents(
        emitter,
        body,
        1u);
    vitte_c17_ast_write(emitter, "}\n\n");
    emitter->function = previous;
    return !emitter->failed;
}

bool
vitte_c17_emit_ast_with_options(
    FILE *output,
    const vitte_parser_t *parser,
    const vitte_c17_compile_options_t *options)
{
    vitte_c17_ast_emitter_t emitter;
    vitte_c17_compile_options_t default_options;
    const vitte_ast_node_t *root;
    size_t index;
    size_t test_count;
    bool has_main;
    bool has_terminal_api;

    if (output == NULL ||
        parser == NULL ||
        vitte_parser_root(parser) == VITTE_AST_INVALID_ID) {
        return false;
    }

    memset(&default_options, 0, sizeof(default_options));
    if (options == NULL) {
        options = &default_options;
    }

    memset(&emitter, 0, sizeof(emitter));
    emitter.output = output;
    emitter.parser = parser;
    emitter.source_path = options->source_path;
    emitter.debug_info = options->debug_info;
    emitter.debug_runtime = options->debug_runtime;
    emitter.emit_line_directives = options->emit_line_directives;

    root =
        vitte_parser_get_node(
            parser,
            vitte_parser_root(parser));
    if (root == NULL ||
        root->kind != VITTE_AST_NODE_TRANSLATION_UNIT) {
        return false;
    }
    if (vitte_c17_ast_contains_unlowered_declaration(
            &emitter,
            root)) {
        return false;
    }
    if (vitte_c17_ast_contains_unsupported_attributes(
            &emitter,
            root)) {
        return false;
    }
    if (!vitte_c17_ast_build_parent_index(&emitter)) {
        return false;
    }
    vitte_c17_ast_collect_generic_form_instances(
        &emitter,
        root,
        root,
        0u);
    vitte_c17_ast_collect_generic_proc_instances(&emitter);
    vitte_c17_ast_collect_nested_generic_proc_instances(&emitter);
    if (emitter.failed) {
        vitte_c17_ast_emitter_release_generic_state(&emitter);
        return false;
    }

    has_terminal_api = false;
    for (index = 0u; index < root->child_count; ++index) {
        const vitte_ast_node_t *node;

        node = vitte_c17_ast_child(&emitter, root, index);
        if (vitte_c17_ast_is_terminal_intrinsic(&emitter, node)) {
            has_terminal_api = true;
            break;
        }
    }

    vitte_c17_ast_write(
        &emitter,
        "#if defined(__APPLE__) || defined(__unix__)\n"
        "#define _POSIX_C_SOURCE 200809L\n"
        "#endif\n"
        "#include <assert.h>\n"
        "#include <inttypes.h>\n"
        "#include <stdbool.h>\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <time.h>\n"
        "#if defined(__APPLE__) || defined(__unix__)\n"
        "#include <arpa/inet.h>\n"
        "#include <errno.h>\n"
        "#include <netinet/in.h>\n"
        "#include <sys/socket.h>\n"
        "#include <sys/select.h>\n"
        "#include <termios.h>\n"
        "#include <unistd.h>\n"
        "#endif\n"
        "\n"
        "#define vitte_core_pointer_is_null(value) ((value) == NULL)\n"
        "#define vitte_core_pointer_read(source) (*(source))\n"
        "#define vitte_core_pointer_write(destination, ...) ((void)(*(destination) = (__VA_ARGS__)))\n"
        "static inline void *vitte_optional_require(const void *value) { if (value == NULL) abort(); return (void *)value; }\n"
        "\n"
        "#if defined(__GNUC__) || defined(__clang__)\n"
        "#define VITTE_C17_AST_UNUSED __attribute__((__unused__))\n"
        "#define VITTE_C17_AST_COLD __attribute__((__cold__))\n"
        "#else\n"
        "#define VITTE_C17_AST_UNUSED\n"
        "#define VITTE_C17_AST_COLD\n"
        "#endif\n"
        "\n"
        "typedef struct VitteArray {\n"
        "    void *data;\n"
        "    size_t length;\n"
        "    size_t capacity;\n"
        "    size_t element_size;\n"
        "} VitteArray;\n"
        "\n");

    vitte_c17_ast_write(
        &emitter,
        "static inline VITTE_C17_AST_UNUSED VitteArray vitte_array_from(\n"
        "    const void *items, size_t length, size_t element_size) {\n"
        "    VitteArray result = { NULL, length, length, element_size };\n"
        "    if (length == 0u) { return result; }\n"
        "    if (element_size == 0u || length > SIZE_MAX / element_size) { abort(); }\n"
        "    result.data = malloc(length * element_size);\n"
        "    if (result.data == NULL) { abort(); }\n"
        "    memcpy(result.data, items, length * element_size);\n"
        "    return result;\n"
        "}\n"
        "static bool vitte_array_reserve(\n"
        "    VitteArray *array, size_t required, size_t element_size) {\n"
        "    void *data;\n"
        "    if (array == NULL || element_size == 0u) { return false; }\n"
        "    if (array->element_size != 0u && array->element_size != element_size) { return false; }\n"
        "    if (required <= array->capacity) { array->element_size = element_size; return true; }\n"
        "    if (required > SIZE_MAX / element_size) { return false; }\n"
        "    data = realloc(array->data, required * element_size);\n"
        "    if (data == NULL) { return false; }\n"
        "    array->data = data;\n"
        "    array->capacity = required;\n"
        "    array->element_size = element_size;\n"
        "    return true;\n"
        "}\n"
        "static bool vitte_array_try_shrink_to(VitteArray *array, size_t minimum_capacity) {\n"
        "    void *data;\n"
        "    size_t target;\n"
        "    if (array == NULL || minimum_capacity > SIZE_MAX / (array->element_size == 0u ? 1u : array->element_size)) { return false; }\n"
        "    target = minimum_capacity < array->length ? array->length : minimum_capacity;\n"
        "    if (target == array->capacity) { return true; }\n"
        "    if (target == 0u) { free(array->data); array->data = NULL; array->capacity = 0u; return true; }\n"
        "    if (array->element_size == 0u) { return false; }\n"
        "    data = realloc(array->data, target * array->element_size);\n"
        "    if (data == NULL) { return false; }\n"
        "    array->data = data; array->capacity = target; return true;\n"
        "}\n"
        "static bool vitte_array_push(\n"
        "    VitteArray *array, const void *item, size_t element_size) {\n"
        "    size_t capacity;\n"
        "    if (array == NULL || item == NULL || element_size == 0u) { return false; }\n"
        "    if (array->element_size != 0u && array->element_size != element_size) { return false; }\n"
        "    if (array->length == array->capacity) {\n"
        "        capacity = array->capacity < 8u ? 8u : array->capacity * 2u;\n"
        "        if (capacity < array->capacity || capacity > SIZE_MAX / element_size) { return false; }\n"
        "        if (!vitte_array_reserve(array, capacity, element_size)) { return false; }\n"
        "    }\n"
        "    memcpy((unsigned char *)array->data + array->length * element_size, item, element_size);\n"
        "    array->element_size = element_size;\n"
        "    ++array->length;\n"
        "    return true;\n"
        "}\n"
        "static inline VITTE_C17_AST_UNUSED const char *vitte_u64_to_string(uint64_t value) {\n"
        "    static _Thread_local char buffers[8][21];\n"
        "    static _Thread_local size_t next_buffer;\n"
        "    char *buffer = buffers[next_buffer];\n"
        "    size_t index = 20u;\n"
        "    next_buffer = (next_buffer + 1u) % 8u;\n"
        "    buffer[index] = '\\0';\n"
        "    do { buffer[--index] = (char)('0' + (value % 10u)); value /= 10u; } while (value != 0u);\n"
        "    return buffer + index;\n"
        "}\n"
        "static inline VITTE_C17_AST_UNUSED const char *vitte_i64_to_string(int64_t value) {\n"
        "    static _Thread_local char buffers[8][22];\n"
        "    static _Thread_local size_t next_buffer;\n"
        "    char *buffer = buffers[next_buffer];\n"
        "    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;\n"
        "    size_t index = 21u;\n"
        "    next_buffer = (next_buffer + 1u) % 8u;\n"
        "    buffer[index] = '\\0';\n"
        "    do { buffer[--index] = (char)('0' + (magnitude % 10u)); magnitude /= 10u; } while (magnitude != 0u);\n"
        "    if (value < 0) { buffer[--index] = '-'; }\n"
        "    return buffer + index;\n"
        "}\n"
        "\n");

    vitte_c17_ast_write(
        &emitter,
        "static inline VITTE_C17_AST_UNUSED const char *vitte_string_from(const char *value) {\n"
        "    size_t length;\n"
        "    char *copy;\n"
        "    if (value == NULL) { value = \"\"; }\n"
        "    length = strlen(value);\n"
        "    if (length == SIZE_MAX) { abort(); }\n"
        "    copy = (char *)malloc(length + 1u);\n"
        "    if (copy == NULL) { abort(); }\n"
        "    memcpy(copy, value, length + 1u);\n"
        "    return copy;\n"
        "}\n"
        "static inline VITTE_C17_AST_UNUSED const char *vitte_string_from_byte(uint8_t value) {\n"
        "    char *copy = (char *)malloc(2u);\n"
        "    if (copy == NULL) { abort(); }\n"
        "    copy[0] = (char)value;\n"
        "    copy[1] = '\\0';\n"
        "    return copy;\n"
        "}\n"
        "static inline VITTE_C17_AST_UNUSED const char *vitte_string_from_bytes(const void *value, size_t length) {\n"
        "    char *copy;\n"
        "    if (value == NULL && length != 0u) { abort(); }\n"
        "    if (length == SIZE_MAX) { abort(); }\n"
        "    copy = (char *)malloc(length + 1u);\n"
        "    if (copy == NULL) { abort(); }\n"
        "    if (length != 0u) { memcpy(copy, value, length); }\n"
        "    copy[length] = '\\0';\n"
        "    return copy;\n"
        "}\n"
        "static inline VITTE_C17_AST_UNUSED const char *vitte_string_from_slice(\n"
        "    const char *value, size_t begin, size_t end) {\n"
        "    size_t length;\n"
        "    size_t count;\n"
        "    char *copy;\n"
        "    if (value == NULL) { value = \"\"; }\n"
        "    length = strlen(value);\n"
        "    if (begin > length) { begin = length; }\n"
        "    if (end > length) { end = length; }\n"
        "    if (end < begin) { end = begin; }\n"
        "    count = end - begin;\n"
        "    if (count == SIZE_MAX) { abort(); }\n"
        "    copy = (char *)malloc(count + 1u);\n"
        "    if (copy == NULL) { abort(); }\n"
        "    memcpy(copy, value + begin, count);\n"
        "    copy[count] = '\\0';\n"
        "    return copy;\n"
        "}\n"
        "\n"
        "static inline VITTE_C17_AST_UNUSED const char *vitte_string_concat(\n"
        "    const char *left, const char *right) {\n"
        "    size_t left_length;\n"
        "    size_t right_length;\n"
        "    char *result;\n"
        "    if (left == NULL) { left = \"\"; }\n"
        "    if (right == NULL) { right = \"\"; }\n"
        "    left_length = strlen(left);\n"
        "    right_length = strlen(right);\n"
        "    if (right_length == SIZE_MAX ||\n"
        "        left_length > SIZE_MAX - right_length - 1u) { abort(); }\n"
        "    result = (char *)malloc(left_length + right_length + 1u);\n"
        "    if (result == NULL) { abort(); }\n"
        "    memcpy(result, left, left_length);\n"
        "    memcpy(result + left_length, right, right_length + 1u);\n"
        "    return result;\n"
        "}\n"
        "static inline VITTE_C17_AST_UNUSED const char *vitte_string_concat_char(\n"
        "    const char *left, char right) {\n"
        "    size_t left_length;\n"
        "    char *result;\n"
        "    if (left == NULL) { left = \"\"; }\n"
        "    left_length = strlen(left);\n"
        "    if (left_length > SIZE_MAX - 2u) { abort(); }\n"
        "    result = (char *)malloc(left_length + 2u);\n"
        "    if (result == NULL) { abort(); }\n"
        "    memcpy(result, left, left_length);\n"
        "    result[left_length] = right;\n"
        "    result[left_length + 1u] = '\\0';\n"
        "    return result;\n"
        "}\n"
        "static bool vitte_string_equal(\n"
        "    const char *left, const char *right) {\n"
        "    if (left == right) { return true; }\n"
        "    if (left == NULL || right == NULL) { return false; }\n"
        "    return strcmp(left, right) == 0;\n"
        "}\n"
        "static inline VITTE_C17_AST_UNUSED const char *vitte_string_coalesce(\n"
        "    const char *value, const char *fallback) {\n"
        "    return value != NULL ? value : fallback;\n"
        "}\n\n");

    if (emitter.debug_runtime) {
        vitte_c17_ast_emit_debug_runtime(&emitter);
    }

    vitte_c17_ast_emit_opaque_forward_declarations_in_scope(
        &emitter,
        root);
    vitte_c17_ast_write(&emitter, "\n");

    vitte_c17_ast_emit_forms_in_scope(
        &emitter,
        root,
        true);
    vitte_c17_ast_write(&emitter, "\n");

    vitte_c17_ast_emit_pick_forward_declarations_in_scope(
        &emitter,
        root);
    vitte_c17_ast_emit_generic_pick_forward_declarations(&emitter);
    vitte_c17_ast_write(&emitter, "\n");
    vitte_c17_ast_emit_picks_in_scope(&emitter, root, false);

    vitte_c17_ast_emit_function_type_aliases_in_scope(&emitter, root);
    vitte_c17_ast_write(&emitter, "\n");

    {
        size_t state_count = parser->node_count + 1u;
        unsigned char *form_states = (unsigned char *)calloc(
            state_count,
            sizeof(*form_states));
        if (form_states == NULL) {
            emitter.failed = true;
        } else {
            vitte_c17_ast_emit_layout_types_topologically(
                &emitter,
                root,
                root,
                form_states,
                state_count);
            free(form_states);
        }
    }
    vitte_c17_ast_emit_generic_form_instances(&emitter);

    vitte_c17_ast_emit_pick_equality_helpers_in_scope(
        &emitter,
        root);

    vitte_c17_ast_emit_constants_in_scope(&emitter, root);
    vitte_c17_ast_write(&emitter, "\n");

    for (index = 0u; index < root->child_count; ++index) {
        const vitte_ast_node_t *node;

        node = vitte_c17_ast_child(&emitter, root, index);
        if (node != NULL && node->kind == VITTE_AST_NODE_PROC_DECL &&
            vitte_c17_ast_generic_form_parameter_count(
                &emitter,
                node,
                NULL) == 0u) {
            if ((node->flags & VITTE_AST_FLAG_ASYNC) != 0u) {
                vitte_c17_ast_emit_async_task_prototypes(&emitter, node);
            } else {
                vitte_c17_ast_emit_function_signature(
                    &emitter,
                    node,
                    true);
            }
        } else if (node != NULL &&
                   node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL &&
                   !vitte_c17_ast_is_intrinsic(&emitter, node)) {
            vitte_c17_ast_emit_function_signature(
                &emitter,
                node,
                true);
        }
    }
    vitte_c17_ast_emit_space_functions(
        &emitter,
        root,
        true);
    vitte_c17_ast_emit_impl_methods_in_scope(
        &emitter,
        root,
        true);
    vitte_c17_ast_emit_generic_proc_prototypes(&emitter);

    vitte_c17_ast_write(
        &emitter,
        "\n"
        "static bool vitte_string_is_space(unsigned char value) {\n"
        "    return value == (unsigned char)' ' ||\n"
        "           value == (unsigned char)'\\t' ||\n"
        "           value == (unsigned char)'\\n' ||\n"
        "           value == (unsigned char)'\\r' ||\n"
        "           value == (unsigned char)'\\f' ||\n"
        "           value == (unsigned char)'\\v';\n"
        "}\n"
        "size_t vitte_string_length(const char *value) {\n"
        "    return value == NULL ? 0u : strlen(value);\n"
        "}\n"
        "const char *vitte_string_trim(const char *value) {\n"
        "    const char *begin;\n"
        "    const char *end;\n"
        "    char *copy;\n"
        "    size_t length;\n"
        "    if (value == NULL) { return \"\"; }\n"
        "    begin = value;\n"
        "    while (*begin != '\\0' &&\n"
        "           vitte_string_is_space((unsigned char)*begin)) {\n"
        "        ++begin;\n"
        "    }\n"
        "    end = value + strlen(value);\n"
        "    while (end > begin &&\n"
        "           vitte_string_is_space((unsigned char)end[-1])) {\n"
        "        --end;\n"
        "    }\n"
        "    length = (size_t)(end - begin);\n"
        "    copy = (char *)malloc(length + 1u);\n"
        "    if (copy == NULL) { return \"\"; }\n"
        "    memcpy(copy, begin, length);\n"
        "    copy[length] = '\\0';\n"
        "    return copy;\n"
        "}\n"
        "bool vitte_string_starts_with(\n"
        "    const char *value,\n"
        "    const char *prefix) {\n"
        "    size_t prefix_length;\n"
        "    if (value == NULL || prefix == NULL) { return false; }\n"
        "    prefix_length = strlen(prefix);\n"
        "    return strlen(value) >= prefix_length &&\n"
        "           memcmp(value, prefix, prefix_length) == 0;\n"
        "}\n"
        "bool vitte_string_ends_with(\n"
        "    const char *value,\n"
        "    const char *suffix) {\n"
        "    size_t value_length;\n"
        "    size_t suffix_length;\n"
        "    if (value == NULL || suffix == NULL) { return false; }\n"
        "    value_length = strlen(value);\n"
        "    suffix_length = strlen(suffix);\n"
        "    return value_length >= suffix_length &&\n"
        "           memcmp(value + value_length - suffix_length,\n"
        "                  suffix, suffix_length) == 0;\n"
        "}\n"
        "void io_print(const char *text) {\n"
        "    if (text != NULL) { (void)fputs(text, stdout); }\n"
        "}\n"
        "void io_print_i64(int64_t value) {\n"
        "    (void)printf(\"%\" PRId64, value);\n"
        "}\n"
        "int64_t io_read_i64(void) {\n"
        "    int64_t value = 0;\n"
        "    if (scanf(\"%\" SCNd64, &value) != 1) { return 0; }\n"
        "    return value;\n"
        "}\n"
        "int64_t random_i64(int64_t minimum, int64_t maximum) {\n"
        "    uint64_t width;\n"
        "    uint64_t offset;\n"
        "    if (maximum < minimum) { return minimum; }\n"
        "    width = (uint64_t)maximum - (uint64_t)minimum + UINT64_C(1);\n"
        "    if (width == 0u) { return (int64_t)rand(); }\n"
        "    offset = (uint64_t)rand() % width;\n"
        "    return minimum + (int64_t)offset;\n"
        "}\n\n");

    if (has_terminal_api) {
        vitte_c17_ast_write(
            &emitter,
        "void terminal_clear(void) {\n"
        "    (void)fputs(\"\\x1b[2J\\x1b[H\", stdout);\n"
        "    (void)fflush(stdout);\n"
        "}\n"
        "void terminal_print(const char *text) {\n"
        "    if (text != NULL) { (void)fputs(text, stdout); }\n"
        "    (void)fflush(stdout);\n"
        "}\n"
        "void terminal_print_i64(int64_t value) {\n"
        "    (void)printf(\"%\" PRId64, value);\n"
        "    (void)fflush(stdout);\n"
        "}\n"
        "#if defined(__APPLE__) || defined(__unix__)\n"
        "static struct termios vitte_terminal_original;\n"
        "static bool vitte_terminal_original_valid;\n"
        "void terminal_hide_cursor(void) {\n"
        "    struct termios raw;\n"
        "    if (!vitte_terminal_original_valid &&\n"
        "        isatty(STDIN_FILENO) != 0 &&\n"
        "        tcgetattr(STDIN_FILENO, &vitte_terminal_original) == 0) {\n"
        "        raw = vitte_terminal_original;\n"
        "        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);\n"
        "        raw.c_cc[VMIN] = 0;\n"
        "        raw.c_cc[VTIME] = 0;\n"
        "        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {\n"
        "            vitte_terminal_original_valid = true;\n"
        "        }\n"
        "    }\n"
        "    (void)fputs(\"\\x1b[?25l\", stdout);\n"
        "    (void)fflush(stdout);\n"
        "}\n"
        "void terminal_show_cursor(void) {\n"
        "    if (vitte_terminal_original_valid) {\n"
        "        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH,\n"
        "                        &vitte_terminal_original);\n"
        "        vitte_terminal_original_valid = false;\n"
        "    }\n"
        "    (void)fputs(\"\\x1b[?25h\", stdout);\n"
        "    (void)fflush(stdout);\n"
        "}\n"
        "bool terminal_key_pressed(void) {\n"
        "    fd_set read_set;\n"
        "    struct timeval timeout = { 0, 0 };\n"
        "    int result;\n"
        "    FD_ZERO(&read_set);\n"
        "    FD_SET(STDIN_FILENO, &read_set);\n"
        "    result = select(STDIN_FILENO + 1, &read_set, NULL, NULL,\n"
        "                    &timeout);\n"
        "    return result > 0 && FD_ISSET(STDIN_FILENO, &read_set);\n"
        "}\n"
        "char terminal_read_key(void) {\n"
        "    unsigned char key;\n"
        "    return read(STDIN_FILENO, &key, 1u) == 1 ? (char)key : '\\0';\n"
        "}\n"
        "void terminal_sleep_ms(int64_t milliseconds) {\n"
        "    struct timespec delay;\n"
        "    struct timespec remaining;\n"
        "    if (milliseconds <= 0) { return; }\n"
        "    delay.tv_sec = (time_t)(milliseconds / INT64_C(1000));\n"
        "    delay.tv_nsec = (long)((milliseconds % INT64_C(1000)) *\n"
        "                           INT64_C(1000000));\n"
        "    while (nanosleep(&delay, &remaining) != 0 && errno == EINTR) {\n"
        "        delay = remaining;\n"
        "    }\n"
        "}\n"
        "#else\n"
        "void terminal_hide_cursor(void) { }\n"
        "void terminal_show_cursor(void) { }\n"
        "bool terminal_key_pressed(void) { return false; }\n"
        "char terminal_read_key(void) { return '\\0'; }\n"
        "void terminal_sleep_ms(int64_t milliseconds) {\n"
        "    clock_t start = clock();\n"
        "    clock_t ticks = (clock_t)((double)milliseconds *\n"
        "                              (double)CLOCKS_PER_SEC / 1000.0);\n"
        "    while (milliseconds > 0 && clock() - start < ticks) { }\n"
        "}\n"
        "#endif\n\n");
    }

    vitte_c17_ast_emit_async_task_helpers_in_scope(&emitter, root);

    has_main = false;
    for (index = 0u; index < root->child_count; ++index) {
        const vitte_ast_node_t *node;
        const vitte_ast_node_t *name;

        node = vitte_c17_ast_child(&emitter, root, index);
        if (node == NULL ||
            node->kind != VITTE_AST_NODE_PROC_DECL ||
            vitte_c17_ast_generic_form_parameter_count(
                &emitter,
                node,
                NULL) != 0u) {
            continue;
        }
        name = vitte_c17_ast_child(&emitter, node, 0u);
        if (vitte_c17_ast_token_is(&emitter, name, "main")) {
            has_main = true;
            break;
        }
    }
    test_count = 0u;
    for (index = 0u; index < root->child_count; ++index) {
        const vitte_ast_node_t *node;

        node = vitte_c17_ast_child(&emitter, root, index);
        if (node != NULL && node->kind == VITTE_AST_NODE_PROC_DECL &&
            vitte_c17_ast_generic_form_parameter_count(
                &emitter,
                node,
                NULL) == 0u) {
            if (!vitte_c17_ast_emit_function(&emitter, node)) {
                break;
            }
        } else if (node != NULL &&
                   node->kind == VITTE_AST_NODE_TEST_DECL) {
            if (!has_main) {
                if (!vitte_c17_ast_emit_test(
                        &emitter,
                        node,
                        test_count)) {
                    break;
                }
                ++test_count;
            }
        } else if (node != NULL &&
                   node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL &&
                   !vitte_c17_ast_is_intrinsic(&emitter, node)) {
            continue;
        } else if (node != NULL &&
                   node->kind != VITTE_AST_NODE_SPACE_DECL &&
                   node->kind != VITTE_AST_NODE_USE_DECL &&
                   node->kind != VITTE_AST_NODE_TEST_DECL &&
                   node->kind != VITTE_AST_NODE_PROC_DECL &&
                   node->kind != VITTE_AST_NODE_FORM_DECL &&
                   node->kind != VITTE_AST_NODE_PICK_DECL &&
                   node->kind != VITTE_AST_NODE_TRAIT_DECL &&
                   node->kind != VITTE_AST_NODE_IMPL_DECL &&
                   node->kind != VITTE_AST_NODE_OPAQUE_DECL &&
                   node->kind != VITTE_AST_NODE_CONST_DECL &&
                   node->kind != VITTE_AST_NODE_STATIC_DECL &&
                   node->kind != VITTE_AST_NODE_EXTERN_PROC_DECL &&
                   !(node->kind == VITTE_AST_NODE_TYPE_DECL &&
                     (node->flags & VITTE_AST_FLAG_TYPE_ALIAS) != 0u)) {
            emitter.failed = true;
            break;
        }
    }
    vitte_c17_ast_emit_space_functions(
        &emitter,
        root,
        false);
    vitte_c17_ast_emit_impl_methods_in_scope(
        &emitter,
        root,
        false);
    vitte_c17_ast_emit_generic_proc_definitions(&emitter);

    if (has_main && !emitter.failed) {
        vitte_c17_ast_write(
            &emitter,
            "int main(void) {\n"
            "    srand((unsigned int)1u);\n"
            "    return (int)vitte_entry_main();\n"
            "}\n");
    } else if (test_count != 0u && !emitter.failed) {
        vitte_c17_ast_write(
            &emitter,
            "int main(void) {\n");
        for (index = 0u; index < test_count; ++index) {
            vitte_c17_ast_indent(&emitter, 1u);
            if (fprintf(
                    emitter.output,
                    "vitte_test_%zu();\n",
                    index) < 0) {
                emitter.failed = true;
                break;
            }
        }
        vitte_c17_ast_write(
            &emitter,
            "    return 0;\n"
            "}\n");
    }

    {
        bool success = !emitter.failed && fflush(output) == 0;
        vitte_c17_ast_emitter_release_generic_state(&emitter);
        return success;
    }
}

bool
vitte_c17_emit_ast(
    FILE *output,
    const vitte_parser_t *parser)
{
    return vitte_c17_emit_ast_with_options(output, parser, NULL);
}

vitte_c17_compile_result_t
vitte_c17_compile_ast_result_with_options(
    const vitte_parser_t *parser,
    const char *output_path,
    bool emit_c,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_compile_options_t *options)
{
    vitte_c17_compile_options_t default_options;

    if (parser == NULL ||
        (run && (run_argc < 0 || (run_argc != 0 && run_argv == NULL)))) {
        return VITTE_C17_COMPILE_INVALID_ARGUMENT;
    }
    memset(&default_options, 0, sizeof(default_options));
    if (options == NULL) {
        options = &default_options;
    }

    if (emit_c) {
        FILE *output;
        bool close_output;
        bool success;

        close_output = output_path != NULL;
        output = close_output
            ? fopen(output_path, "wb")
            : stdout;
        if (output == NULL) {
            return VITTE_C17_COMPILE_OUTPUT_FAILED;
        }

        success = vitte_c17_emit_ast_with_options(output, parser, options);
        if (close_output && fclose(output) != 0) {
            return VITTE_C17_COMPILE_OUTPUT_FAILED;
        }
        return success
            ? VITTE_C17_COMPILE_SUCCESS
            : VITTE_C17_COMPILE_EMIT_FAILED;
    }

#if defined(__APPLE__) || defined(__unix__)
    {
        char source_path[] = "/tmp/vitte-source-XXXXXX";
        char object_path[] = "/tmp/vitte-object-XXXXXX";
        char executable_path[] = "/tmp/vitte-program-XXXXXX";
        int source_descriptor;
        int object_descriptor;
        int executable_descriptor;
        FILE *source_file;
        bool success;
        const char *compile_arguments[18];
        size_t compile_argument_count;
        const char *link_arguments[8];
        size_t link_argument_count;
        vitte_c17_compile_result_t result;

        result = VITTE_C17_COMPILE_OUTPUT_FAILED;
        source_descriptor = mkstemp(source_path);
        if (source_descriptor < 0) {
            return result;
        }

        source_file = fdopen(source_descriptor, "w");
        if (source_file == NULL) {
            (void)close(source_descriptor);
            (void)unlink(source_path);
            return result;
        }

        success = vitte_c17_emit_ast_with_options(
            source_file,
            parser,
            options);
        if (fclose(source_file) != 0) {
            (void)unlink(source_path);
            return VITTE_C17_COMPILE_OUTPUT_FAILED;
        }
        if (!success) {
            (void)unlink(source_path);
            return VITTE_C17_COMPILE_EMIT_FAILED;
        }

        object_descriptor = mkstemp(object_path);
        if (object_descriptor < 0) {
            (void)unlink(source_path);
            return result;
        }
        if (close(object_descriptor) != 0) {
            (void)unlink(source_path);
            (void)unlink(object_path);
            return result;
        }

        if (run) {
            executable_descriptor = mkstemp(executable_path);
            if (executable_descriptor < 0) {
                (void)unlink(source_path);
                return result;
            }
            if (close(executable_descriptor) != 0 ||
                unlink(executable_path) != 0) {
                (void)unlink(source_path);
                (void)unlink(object_path);
                (void)unlink(executable_path);
                return result;
            }
        } else {
            if (output_path == NULL) {
                output_path = "a.out";
            }
            if (output_path[0] == '\0') {
                (void)unlink(source_path);
                (void)unlink(object_path);
                return VITTE_C17_COMPILE_INVALID_ARGUMENT;
            }
            executable_path[0] = '\0';
        }

        compile_argument_count = 0u;
        compile_arguments[compile_argument_count++] = "cc";
        if (options->debug_info) {
            /* Keep the generated C easy to inspect and preserve frame roots. */
            compile_arguments[compile_argument_count++] = "-g";
            compile_arguments[compile_argument_count++] = "-O0";
            compile_arguments[compile_argument_count++] =
                "-fno-omit-frame-pointer";
            compile_arguments[compile_argument_count++] = "-fno-inline";
            if (options->debug_runtime) {
                compile_arguments[compile_argument_count++] =
                    "-DVITTE_DEBUG_RUNTIME";
            }
        }
        compile_arguments[compile_argument_count++] = "-std=c17";
        compile_arguments[compile_argument_count++] = "-x";
        compile_arguments[compile_argument_count++] = "c";
        compile_arguments[compile_argument_count++] = "-c";
        compile_arguments[compile_argument_count++] = "-o";
        compile_arguments[compile_argument_count++] = object_path;
        compile_arguments[compile_argument_count++] = source_path;
        compile_arguments[compile_argument_count] = NULL;
        success = vitte_platform_run(compile_arguments, true);
        (void)unlink(source_path);
        if (!success) {
            (void)unlink(object_path);
            return VITTE_C17_COMPILE_C_COMPILATION_FAILED;
        }

        link_argument_count = 0u;
        link_arguments[link_argument_count++] = "cc";
        link_arguments[link_argument_count++] = object_path;
        link_arguments[link_argument_count++] = "-o";
        link_arguments[link_argument_count++] =
            run ? executable_path : output_path;
        link_arguments[link_argument_count] = NULL;
        success = vitte_platform_run(link_arguments, true);
        (void)unlink(object_path);
        if (!success) {
            if (run) {
                (void)unlink(executable_path);
            }
            return VITTE_C17_COMPILE_LINK_FAILED;
        }

        if (run) {
            char **run_arguments;
            size_t argument_count;
            size_t index;

            argument_count = (size_t)run_argc;
            if (argument_count > SIZE_MAX / sizeof(*run_arguments) - 2u) {
                (void)unlink(executable_path);
                return VITTE_C17_COMPILE_INVALID_ARGUMENT;
            }
            run_arguments = (char **)calloc(
                argument_count + 2u,
                sizeof(*run_arguments));
            if (run_arguments == NULL) {
                (void)unlink(executable_path);
                return VITTE_C17_COMPILE_OUTPUT_FAILED;
            }
            run_arguments[0] = executable_path;
            for (index = 0u; index < argument_count; ++index) {
                run_arguments[index + 1u] = run_argv[index];
            }
            success = vitte_platform_run(
                (const char *const *)run_arguments,
                true);
            free(run_arguments);
            (void)unlink(executable_path);
            return success
                ? VITTE_C17_COMPILE_SUCCESS
                : VITTE_C17_COMPILE_RUNTIME_FAILED;
        }

        return VITTE_C17_COMPILE_SUCCESS;
    }
#else
    (void)output_path;
    return VITTE_C17_COMPILE_UNSUPPORTED;
#endif
}

vitte_c17_compile_result_t
vitte_c17_compile_source_result_with_options(
    const char *source,
    size_t source_length,
    const char *output_path,
    bool emit_c,
    bool emit_object,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_compile_options_t *options)
{
    vitte_c17_compile_options_t default_options;

    if ((source == NULL && source_length != 0u) ||
        (run && (run_argc < 0 || (run_argc != 0 && run_argv == NULL))) ||
        (emit_object && run)) {
        return VITTE_C17_COMPILE_INVALID_ARGUMENT;
    }

    memset(&default_options, 0, sizeof(default_options));
    if (options == NULL) {
        options = &default_options;
    }

    if (emit_c) {
        FILE *output;
        bool close_output;
        bool succeeded;

        close_output = output_path != NULL;
        output = close_output ? fopen(output_path, "wb") : stdout;
        if (output == NULL) {
            return VITTE_C17_COMPILE_OUTPUT_FAILED;
        }
        succeeded = source_length == 0u ||
            fwrite(source, 1u, source_length, output) == source_length;
        if (close_output && fclose(output) != 0) {
            succeeded = false;
        }
        return succeeded
            ? VITTE_C17_COMPILE_SUCCESS
            : VITTE_C17_COMPILE_OUTPUT_FAILED;
    }

#if defined(__APPLE__) || defined(__unix__)
    {
        char source_path[] = "/tmp/vitte-native-source-XXXXXX";
        char executable_path[] = "/tmp/vitte-native-program-XXXXXX";
        int source_descriptor;
        FILE *source_file;
        const char *compile_arguments[18];
        size_t compile_argument_count;
        const char *compile_output_path;
        bool succeeded;

        source_descriptor = mkstemp(source_path);
        if (source_descriptor < 0) {
            return VITTE_C17_COMPILE_OUTPUT_FAILED;
        }
        source_file = fdopen(source_descriptor, "wb");
        if (source_file == NULL) {
            (void)close(source_descriptor);
            (void)unlink(source_path);
            return VITTE_C17_COMPILE_OUTPUT_FAILED;
        }
        succeeded = source_length == 0u ||
            fwrite(source, 1u, source_length, source_file) == source_length;
        if (fclose(source_file) != 0) {
            succeeded = false;
        }
        if (!succeeded) {
            (void)unlink(source_path);
            return VITTE_C17_COMPILE_OUTPUT_FAILED;
        }

        compile_output_path = output_path;
        if (run) {
            int executable_descriptor = mkstemp(executable_path);

            if (executable_descriptor < 0) {
                (void)unlink(source_path);
                return VITTE_C17_COMPILE_OUTPUT_FAILED;
            }
            if (close(executable_descriptor) != 0 ||
                unlink(executable_path) != 0) {
                (void)unlink(source_path);
                (void)unlink(executable_path);
                return VITTE_C17_COMPILE_OUTPUT_FAILED;
            }
            compile_output_path = executable_path;
        } else if (compile_output_path == NULL) {
            if (emit_object) {
                (void)unlink(source_path);
                return VITTE_C17_COMPILE_INVALID_ARGUMENT;
            }
            compile_output_path = "a.out";
        }
        if (compile_output_path[0] == '\0') {
            (void)unlink(source_path);
            return VITTE_C17_COMPILE_INVALID_ARGUMENT;
        }

        compile_argument_count = 0u;
        compile_arguments[compile_argument_count++] = "cc";
        if (options->debug_info) {
            compile_arguments[compile_argument_count++] = "-g";
            compile_arguments[compile_argument_count++] = "-O0";
            compile_arguments[compile_argument_count++] =
                "-fno-omit-frame-pointer";
            compile_arguments[compile_argument_count++] = "-fno-inline";
            if (options->debug_runtime) {
                compile_arguments[compile_argument_count++] =
                    "-DVITTE_DEBUG_RUNTIME";
            }
        }
        compile_arguments[compile_argument_count++] = "-std=c17";
        compile_arguments[compile_argument_count++] = "-x";
        compile_arguments[compile_argument_count++] = "c";
        if (emit_object) {
            compile_arguments[compile_argument_count++] = "-c";
        }
        compile_arguments[compile_argument_count++] = source_path;
        compile_arguments[compile_argument_count++] = "-o";
        compile_arguments[compile_argument_count++] = compile_output_path;
        compile_arguments[compile_argument_count] = NULL;

        succeeded = vitte_platform_run(compile_arguments, true);
        (void)unlink(source_path);
        if (!succeeded) {
            if (run) {
                (void)unlink(executable_path);
            }
            return VITTE_C17_COMPILE_C_COMPILATION_FAILED;
        }
        if (emit_object || !run) {
            return VITTE_C17_COMPILE_SUCCESS;
        }

        {
            char **run_arguments;
            size_t argument_count = (size_t)run_argc;
            size_t index;

            if (argument_count > SIZE_MAX / sizeof(*run_arguments) - 2u) {
                (void)unlink(executable_path);
                return VITTE_C17_COMPILE_INVALID_ARGUMENT;
            }
            run_arguments = (char **)calloc(
                argument_count + 2u,
                sizeof(*run_arguments));
            if (run_arguments == NULL) {
                (void)unlink(executable_path);
                return VITTE_C17_COMPILE_OUTPUT_FAILED;
            }
            run_arguments[0] = executable_path;
            for (index = 0u; index < argument_count; ++index) {
                run_arguments[index + 1u] = run_argv[index];
            }
            succeeded = vitte_platform_run(
                (const char *const *)run_arguments,
                true);
            free(run_arguments);
            (void)unlink(executable_path);
            return succeeded
                ? VITTE_C17_COMPILE_SUCCESS
                : VITTE_C17_COMPILE_RUNTIME_FAILED;
        }
    }
#else
    (void)output_path;
    (void)emit_object;
    (void)run;
    (void)run_argc;
    (void)run_argv;
    return VITTE_C17_COMPILE_UNSUPPORTED;
#endif
}

bool
vitte_c17_compile_ast_with_options(
    const vitte_parser_t *parser,
    const char *output_path,
    bool emit_c,
    bool run,
    int run_argc,
    char **run_argv,
    const vitte_c17_compile_options_t *options)
{
    return vitte_c17_compile_ast_result_with_options(
               parser,
               output_path,
               emit_c,
               run,
               run_argc,
               run_argv,
               options) == VITTE_C17_COMPILE_SUCCESS;
}

bool
vitte_c17_compile_ast(
    const vitte_parser_t *parser,
    const char *output_path,
    bool emit_c,
    bool run,
    int run_argc,
    char **run_argv)
{
    return vitte_c17_compile_ast_with_options(
        parser,
        output_path,
        emit_c,
        run,
        run_argc,
        run_argv,
        NULL);
}

vitte_c17_compile_result_t
vitte_c17_compile_ast_object_result_with_options(
    const vitte_parser_t *parser,
    const char *output_path,
    const vitte_c17_compile_options_t *options)
{
#if defined(__APPLE__) || defined(__unix__)
    char source_path[] = "/tmp/vitte-object-source-XXXXXX";
    int source_descriptor;
    FILE *source_file;
    const char *compile_arguments[14];
    size_t compile_argument_count;
    bool success;

    if (parser == NULL || output_path == NULL || output_path[0] == '\0') {
        return VITTE_C17_COMPILE_INVALID_ARGUMENT;
    }
    source_descriptor = mkstemp(source_path);
    if (source_descriptor < 0) {
        return VITTE_C17_COMPILE_OUTPUT_FAILED;
    }
    source_file = fdopen(source_descriptor, "w");
    if (source_file == NULL) {
        (void)close(source_descriptor);
        (void)unlink(source_path);
        return VITTE_C17_COMPILE_OUTPUT_FAILED;
    }
    success = vitte_c17_emit_ast_with_options(source_file, parser, options);
    if (fclose(source_file) != 0) {
        (void)unlink(source_path);
        return VITTE_C17_COMPILE_OUTPUT_FAILED;
    }
    if (!success) {
        (void)unlink(source_path);
        return VITTE_C17_COMPILE_EMIT_FAILED;
    }

    compile_argument_count = 0u;
    compile_arguments[compile_argument_count++] = "cc";
    if (options != NULL && options->debug_info) {
        compile_arguments[compile_argument_count++] = "-g";
        compile_arguments[compile_argument_count++] = "-O0";
        compile_arguments[compile_argument_count++] = "-fno-omit-frame-pointer";
        compile_arguments[compile_argument_count++] = "-fno-inline";
    }
    compile_arguments[compile_argument_count++] = "-std=c17";
    compile_arguments[compile_argument_count++] = "-c";
    compile_arguments[compile_argument_count++] = "-x";
    compile_arguments[compile_argument_count++] = "c";
    compile_arguments[compile_argument_count++] = source_path;
    compile_arguments[compile_argument_count++] = "-o";
    compile_arguments[compile_argument_count++] = output_path;
    compile_arguments[compile_argument_count] = NULL;
    success = vitte_platform_run(compile_arguments, true);
    (void)unlink(source_path);
    if (!success) {
        (void)unlink(output_path);
        return VITTE_C17_COMPILE_C_COMPILATION_FAILED;
    }
    return VITTE_C17_COMPILE_SUCCESS;
#else
    (void)parser;
    (void)output_path;
    (void)options;
    return VITTE_C17_COMPILE_UNSUPPORTED;
#endif
}

bool
vitte_c17_compile_ast_object_with_options(
    const vitte_parser_t *parser,
    const char *output_path,
    const vitte_c17_compile_options_t *options)
{
    return vitte_c17_compile_ast_object_result_with_options(
               parser,
               output_path,
               options) == VITTE_C17_COMPILE_SUCCESS;
}
