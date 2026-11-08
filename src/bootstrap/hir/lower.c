#include "lower.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct vitte_hir_lower_symbol {
    vitte_hir_string_id_t name;
    vitte_hir_symbol_id_t symbol;
} vitte_hir_lower_symbol_t;

typedef struct vitte_hir_lower_proc_return {
    vitte_hir_string_id_t name;
    vitte_hir_type_id_t type;
} vitte_hir_lower_proc_return_t;

typedef struct vitte_hir_lower_context {
    vitte_hir_t *hir;
    const vitte_parser_t *parser;
    vitte_hir_type_id_t unit_type;
    vitte_hir_type_id_t never_type;
    vitte_hir_type_id_t bool_type;
    vitte_hir_type_id_t i64_type;
    vitte_hir_type_id_t u64_type;
    vitte_hir_type_id_t f64_type;
    vitte_hir_type_id_t string_type;
    vitte_hir_lower_symbol_t *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    vitte_hir_lower_proc_return_t *proc_returns;
    size_t proc_return_count;
    size_t proc_return_capacity;
} vitte_hir_lower_context_t;

static vitte_hir_span_t
vitte_hir_lower_span(const vitte_parser_span_t *span)
{
    vitte_hir_span_t result;
    memset(&result, 0, sizeof(result));
    if (span != NULL) {
        result.file_id = span->file_id;
        result.begin = span->begin;
        result.end = span->end;
        result.valid = span->valid;
    }
    return result;
}

static const vitte_ast_node_t *
vitte_hir_lower_node(const vitte_hir_lower_context_t *context,
                    vitte_ast_node_id_t id)
{
    return context == NULL || context->parser == NULL
        ? NULL
        : vitte_parser_get_node(context->parser, id);
}

static const vitte_token_t *
vitte_hir_lower_token(const vitte_hir_lower_context_t *context,
                     const vitte_ast_node_t *node)
{
    return context == NULL ? NULL : vitte_parser_node_token(context->parser, node);
}

static vitte_hir_string_id_t
vitte_hir_lower_intern_node_text(const vitte_hir_lower_context_t *context,
                                 const vitte_ast_node_t *node)
{
    const vitte_token_t *token = vitte_hir_lower_token(context, node);
    char path[1024];
    size_t position = 0u;
    size_t index;
    if (node != NULL && node->kind == VITTE_AST_NODE_PATH && node->child_count != 0u) {
        for (index = 0u; index < node->child_count; ++index) {
            const vitte_ast_node_t *segment = vitte_hir_lower_node(context, node->children[index]);
            const vitte_token_t *segment_token = vitte_hir_lower_token(context, segment);
            if (segment_token == NULL || position + segment_token->length + 3u >= sizeof(path)) return VITTE_HIR_INVALID_ID;
            if (index != 0u) { path[position++] = ':'; path[position++] = ':'; }
            memcpy(path + position, segment_token->lexeme, segment_token->length);
            position += segment_token->length;
        }
        return vitte_hir_intern_string(context->hir, path, position);
    }
    return token == NULL ? VITTE_HIR_INVALID_ID :
        vitte_hir_intern_string(context->hir, token->lexeme, token->length);
}

static bool
vitte_hir_lower_list_push(vitte_hir_node_list_t *list, vitte_hir_node_id_t id)
{
    vitte_hir_node_id_t *items;
    size_t capacity;
    if (list == NULL || id == VITTE_HIR_INVALID_ID) return false;
    if (list->count == list->capacity) {
        capacity = list->capacity == 0u ? 4u : list->capacity * 2u;
        if (capacity < list->count + 1u) return false;
        items = (vitte_hir_node_id_t *)realloc(list->items,
                                               capacity * sizeof(*items));
        if (items == NULL) return false;
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = id;
    return true;
}

static void
vitte_hir_lower_list_destroy(vitte_hir_node_list_t *list)
{
    if (list != NULL) {
        free(list->items);
        list->items = NULL;
        list->count = 0u;
        list->capacity = 0u;
    }
}

static vitte_hir_node_id_t
vitte_hir_lower_add(vitte_hir_lower_context_t *context,
                    vitte_hir_node_t *node)
{
    vitte_hir_node_id_t id;
    if (context == NULL || node == NULL) return VITTE_HIR_INVALID_ID;
    id = vitte_hir_add_node(context->hir, node);
    return id;
}

static vitte_hir_type_id_t
vitte_hir_lower_primitive(vitte_hir_lower_context_t *context,
                          vitte_hir_type_kind_t kind)
{
    vitte_hir_type_t type;
    memset(&type, 0, sizeof(type));
    type.kind = kind;
    return vitte_hir_add_type(context->hir, &type);
}

static vitte_hir_type_id_t
vitte_hir_lower_type(vitte_hir_lower_context_t *context,
                     vitte_ast_node_id_t id)
{
    const vitte_ast_node_t *node = vitte_hir_lower_node(context, id);
    const vitte_token_t *token;
    const vitte_hir_string_t *interned_name;
    vitte_hir_string_id_t interned_name_id;
    vitte_hir_type_t type;
    vitte_hir_type_id_t element;
    const char *name;

    if (node == NULL) return context->i64_type;
    if (node->kind == VITTE_AST_NODE_TYPE_EXPR && node->child_count != 0u) {
        return vitte_hir_lower_type(context, node->children[0]);
    }
    token = vitte_hir_lower_token(context, node);
    interned_name_id = vitte_hir_lower_intern_node_text(context, node);
    interned_name = interned_name_id == VITTE_HIR_INVALID_ID
        ? NULL : vitte_hir_get_string(context->hir, interned_name_id);
    name = interned_name == NULL
        ? (token == NULL ? NULL : token->lexeme) : interned_name->data;
    if (name != NULL) {
        size_t name_length = interned_name == NULL ? token->length : interned_name->length;
        if (name_length == 5u && memcmp(name, "never", 5u) == 0) return context->never_type;
        if ((name_length == 4u && memcmp(name, "void", 4u) == 0) ||
            (name_length == 4u && memcmp(name, "unit", 4u) == 0)) return context->unit_type;
        if (name_length == 4u && memcmp(name, "bool", 4u) == 0) return context->bool_type;
        if (name_length == 2u && memcmp(name, "u8", 2u) == 0) {
            memset(&type, 0, sizeof(type));
            type.kind = VITTE_HIR_TYPE_U8;
            return vitte_hir_add_type(context->hir, &type);
        }
        if (name_length == 3u && memcmp(name, "u16", 3u) == 0) {
            memset(&type, 0, sizeof(type));
            type.kind = VITTE_HIR_TYPE_U16;
            return vitte_hir_add_type(context->hir, &type);
        }
        if (name_length == 3u && memcmp(name, "u32", 3u) == 0) {
            memset(&type, 0, sizeof(type));
            type.kind = VITTE_HIR_TYPE_U32;
            return vitte_hir_add_type(context->hir, &type);
        }
        if (name_length == 3u && memcmp(name, "u64", 3u) == 0) {
            memset(&type, 0, sizeof(type));
            type.kind = VITTE_HIR_TYPE_U64;
            return vitte_hir_add_type(context->hir, &type);
        }
        if (name_length == 5u && memcmp(name, "usize", 5u) == 0) return context->u64_type;
        if (name_length == 3u && memcmp(name, "f64", 3u) == 0) return context->f64_type;
        if (name_length == 6u && memcmp(name, "double", 6u) == 0) return context->f64_type;
        if (name_length == 6u && memcmp(name, "string", 6u) == 0) return context->string_type;
        if (name_length == 3u && memcmp(name, "str", 3u) == 0) return context->string_type;
        if ((name_length == 3u && memcmp(name, "i32", 3u) == 0) ||
            (name_length == 3u && memcmp(name, "int", 3u) == 0)) {
            return context->i64_type;
        }
    }
    if (node->kind == VITTE_AST_NODE_POINTER_TYPE ||
        node->kind == VITTE_AST_NODE_REFERENCE_TYPE) {
        element = node->child_count == 0u
            ? context->i64_type
            : vitte_hir_lower_type(context, node->children[0]);
        memset(&type, 0, sizeof(type));
        type.kind = node->kind == VITTE_AST_NODE_POINTER_TYPE
            ? VITTE_HIR_TYPE_POINTER : VITTE_HIR_TYPE_REFERENCE;
        type.element_type = element;
        type.mutable_reference = (node->flags & VITTE_AST_FLAG_MUTABLE) != 0u;
        return vitte_hir_add_type(context->hir, &type);
    }
    if (node->kind == VITTE_AST_NODE_ARRAY_TYPE) {
        size_t element_index = node->child_count == 1u ? 0u : 1u;
        if (node->child_count == 0u) return context->i64_type;
        memset(&type, 0, sizeof(type));
        type.kind = node->child_count == 1u
            ? VITTE_HIR_TYPE_SLICE : VITTE_HIR_TYPE_ARRAY;
        type.element_type = vitte_hir_lower_type(context, node->children[element_index]);
        return vitte_hir_add_type(context->hir, &type);
    }
    return context->i64_type;
}

static vitte_hir_symbol_id_t
vitte_hir_lower_find_symbol(const vitte_hir_lower_context_t *context,
                            vitte_hir_string_id_t name)
{
    size_t index;
    if (context == NULL) return VITTE_HIR_INVALID_ID;
    for (index = context->symbol_count; index > 0u; --index) {
        if (context->symbols[index - 1u].name == name) {
            return context->symbols[index - 1u].symbol;
        }
    }
    return VITTE_HIR_INVALID_ID;
}

static vitte_hir_type_id_t
vitte_hir_lower_find_proc_return(const vitte_hir_lower_context_t *context,
                                 vitte_hir_string_id_t name)
{
    size_t index;
    if (context == NULL || name == VITTE_HIR_INVALID_ID) return VITTE_HIR_INVALID_ID;
    for (index = context->proc_return_count; index > 0u; --index) {
        if (context->proc_returns[index - 1u].name == name) {
            return context->proc_returns[index - 1u].type;
        }
    }
    return VITTE_HIR_INVALID_ID;
}

static bool
vitte_hir_lower_set_proc_return(vitte_hir_lower_context_t *context,
                                vitte_hir_string_id_t name,
                                vitte_hir_type_id_t type)
{
    vitte_hir_lower_proc_return_t *items;
    size_t index;
    size_t capacity;
    if (context == NULL || name == VITTE_HIR_INVALID_ID || type == VITTE_HIR_INVALID_ID) return false;
    for (index = 0u; index < context->proc_return_count; ++index) {
        if (context->proc_returns[index].name == name) {
            context->proc_returns[index].type = type;
            return true;
        }
    }
    if (context->proc_return_count == context->proc_return_capacity) {
        capacity = context->proc_return_capacity == 0u ? 8u : context->proc_return_capacity * 2u;
        items = (vitte_hir_lower_proc_return_t *)realloc(
            context->proc_returns, capacity * sizeof(*items));
        if (items == NULL) return false;
        context->proc_returns = items;
        context->proc_return_capacity = capacity;
    }
    context->proc_returns[context->proc_return_count].name = name;
    context->proc_returns[context->proc_return_count].type = type;
    ++context->proc_return_count;
    return true;
}

static vitte_hir_symbol_id_t
vitte_hir_lower_symbol(vitte_hir_lower_context_t *context,
                       vitte_ast_node_id_t name_id,
                       vitte_hir_symbol_kind_t kind,
                       vitte_hir_type_id_t type,
                       vitte_hir_visibility_t visibility,
                       vitte_hir_mutability_t mutability)
{
    const vitte_ast_node_t *name_node = vitte_hir_lower_node(context, name_id);
    vitte_hir_string_id_t name;
    vitte_hir_symbol_t symbol;
    vitte_hir_symbol_id_t id;
    vitte_hir_lower_symbol_t *symbols;
    size_t capacity;

    if (name_node == NULL) return VITTE_HIR_INVALID_ID;
    name = vitte_hir_lower_intern_node_text(context, name_node);
    if (name == VITTE_HIR_INVALID_ID) return VITTE_HIR_INVALID_ID;
    /* Locals and parameters are declarations with lexical scope. Reusing a
     * same-named symbol from an earlier procedure carries its old type into
     * HIR (for example, `text: *const u8` shadowing a later `text: str`). */
    if (kind != VITTE_HIR_SYMBOL_LOCAL &&
        kind != VITTE_HIR_SYMBOL_PARAMETER) {
        id = vitte_hir_lower_find_symbol(context, name);
        if (id != VITTE_HIR_INVALID_ID) return id;
    }
    memset(&symbol, 0, sizeof(symbol));
    symbol.kind = kind;
    symbol.name = name;
    symbol.type = type;
    symbol.visibility = visibility;
    symbol.mutability = mutability;
    symbol.span = vitte_hir_lower_span(&name_node->span);
    id = vitte_hir_add_symbol(context->hir, &symbol);
    if (id == VITTE_HIR_INVALID_ID) return id;
    if (context->symbol_count == context->symbol_capacity) {
        capacity = context->symbol_capacity == 0u ? 16u : context->symbol_capacity * 2u;
        symbols = (vitte_hir_lower_symbol_t *)realloc(
            context->symbols, capacity * sizeof(*symbols));
        if (symbols == NULL) return VITTE_HIR_INVALID_ID;
        context->symbols = symbols;
        context->symbol_capacity = capacity;
    }
    context->symbols[context->symbol_count].name = name;
    context->symbols[context->symbol_count].symbol = id;
    ++context->symbol_count;
    return id;
}

static vitte_hir_symbol_id_t
vitte_hir_lower_member_symbol(vitte_hir_lower_context_t *context,
                              vitte_ast_node_id_t name_id)
{
    const vitte_ast_node_t *name_node = vitte_hir_lower_node(context, name_id);
    vitte_hir_symbol_t symbol;
    if (name_node == NULL) return VITTE_HIR_INVALID_ID;
    memset(&symbol, 0, sizeof(symbol));
    symbol.kind = VITTE_HIR_SYMBOL_FIELD;
    symbol.name = vitte_hir_lower_intern_node_text(context, name_node);
    if (symbol.name == VITTE_HIR_INVALID_ID) return VITTE_HIR_INVALID_ID;
    symbol.type = context->i64_type;
    symbol.visibility = VITTE_HIR_VISIBILITY_PRIVATE;
    symbol.mutability = VITTE_HIR_MUTABILITY_IMMUTABLE;
    symbol.span = vitte_hir_lower_span(&name_node->span);
    return vitte_hir_add_symbol(context->hir, &symbol);
}

static vitte_hir_node_id_t
vitte_hir_lower_declaration(vitte_hir_lower_context_t *, vitte_ast_node_id_t);

static bool
vitte_hir_lower_predeclare_node(vitte_hir_lower_context_t *context,
                                vitte_ast_node_id_t id)
{
    const vitte_ast_node_t *node = vitte_hir_lower_node(context, id);
    size_t index;
    if (node == NULL) return false;
    if (node->kind == VITTE_AST_NODE_SPACE_DECL) {
        if (node->child_count < 2u) return true;
        node = vitte_hir_lower_node(context, node->children[1]);
        if (node == NULL) return false;
        for (index = 0u; index < node->child_count; ++index) {
            if (!vitte_hir_lower_predeclare_node(context, node->children[index])) return false;
        }
        return true;
    }
    if (node->kind == VITTE_AST_NODE_PROC_DECL ||
        node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL ||
        node->kind == VITTE_AST_NODE_CONST_DECL ||
        node->kind == VITTE_AST_NODE_STATIC_DECL ||
        node->kind == VITTE_AST_NODE_TYPE_DECL) {
        vitte_hir_symbol_kind_t kind =
            node->kind == VITTE_AST_NODE_PROC_DECL ||
            node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL
                ? VITTE_HIR_SYMBOL_PROC
                : node->kind == VITTE_AST_NODE_TYPE_DECL
                    ? VITTE_HIR_SYMBOL_TYPE
                    : node->kind == VITTE_AST_NODE_CONST_DECL
                        ? VITTE_HIR_SYMBOL_CONST : VITTE_HIR_SYMBOL_STATIC;
        if (node->child_count == 0u ||
            vitte_hir_lower_symbol(
                context,
                node->children[0],
                kind,
                context->i64_type,
                (node->flags & VITTE_AST_FLAG_PUBLIC) != 0u
                    ? VITTE_HIR_VISIBILITY_PUBLIC : VITTE_HIR_VISIBILITY_PRIVATE,
                kind == VITTE_HIR_SYMBOL_STATIC
                    ? VITTE_HIR_MUTABILITY_MUTABLE : VITTE_HIR_MUTABILITY_IMMUTABLE) == VITTE_HIR_INVALID_ID) {
            return false;
        }
    }
    return true;
}

static bool
vitte_hir_lower_bind_alias(vitte_hir_lower_context_t *context,
                           vitte_hir_string_id_t name,
                           vitte_hir_symbol_id_t symbol)
{
    vitte_hir_lower_symbol_t *symbols;
    size_t capacity;
    if (context == NULL || name == VITTE_HIR_INVALID_ID ||
        symbol == VITTE_HIR_INVALID_ID) return false;
    if (context->symbol_count == context->symbol_capacity) {
        capacity = context->symbol_capacity == 0u ? 16u : context->symbol_capacity * 2u;
        symbols = (vitte_hir_lower_symbol_t *)realloc(
            context->symbols, capacity * sizeof(*symbols));
        if (symbols == NULL) return false;
        context->symbols = symbols;
        context->symbol_capacity = capacity;
    }
    context->symbols[context->symbol_count].name = name;
    context->symbols[context->symbol_count].symbol = symbol;
    ++context->symbol_count;
    return true;
}

static bool
vitte_hir_lower_bind_space_aliases(vitte_hir_lower_context_t *context,
                                   vitte_ast_node_id_t id,
                                   vitte_hir_string_id_t parent)
{
    const vitte_ast_node_t *space = vitte_hir_lower_node(context, id);
    const vitte_ast_node_t *body;
    vitte_hir_string_id_t prefix;
    size_t index;
    if (space == NULL || space->kind != VITTE_AST_NODE_SPACE_DECL ||
        space->child_count == 0u) return false;
    prefix = vitte_hir_lower_intern_node_text(
        context, vitte_hir_lower_node(context, space->children[0]));
    if (parent != VITTE_HIR_INVALID_ID) {
        const vitte_hir_string_t *parent_string = vitte_hir_get_string(context->hir, parent);
        const vitte_hir_string_t *prefix_string = vitte_hir_get_string(context->hir, prefix);
        char qualified[1024];
        int written;
        if (parent_string == NULL || prefix_string == NULL) return false;
        written = snprintf(qualified, sizeof(qualified), "%s::%s",
            parent_string->data, prefix_string->data);
        if (written < 0 || (size_t)written >= sizeof(qualified)) return false;
        prefix = vitte_hir_intern_string(context->hir, qualified, (size_t)written);
    }
    if (space->child_count < 2u) return true;
    body = vitte_hir_lower_node(context, space->children[1]);
    if (body == NULL) return false;
    for (index = 0u; index < body->child_count; ++index) {
        const vitte_ast_node_t *item = vitte_hir_lower_node(context, body->children[index]);
        if (item == NULL || item->child_count == 0u) return false;
        if (item->kind == VITTE_AST_NODE_SPACE_DECL) {
            if (!vitte_hir_lower_bind_space_aliases(context, item->id, prefix)) return false;
        } else if (item->kind == VITTE_AST_NODE_PROC_DECL ||
                   item->kind == VITTE_AST_NODE_EXTERN_PROC_DECL ||
                   item->kind == VITTE_AST_NODE_CONST_DECL ||
                   item->kind == VITTE_AST_NODE_STATIC_DECL ||
                   item->kind == VITTE_AST_NODE_TYPE_DECL) {
            const vitte_hir_string_t *prefix_string = vitte_hir_get_string(context->hir, prefix);
            const vitte_hir_string_t *name_string = vitte_hir_get_string(
                context->hir, vitte_hir_lower_intern_node_text(
                    context, vitte_hir_lower_node(context, item->children[0])));
            char qualified[1024];
            int written;
            vitte_hir_symbol_id_t symbol;
            vitte_hir_string_id_t qualified_id;
            if (prefix_string == NULL || name_string == NULL) return false;
            symbol = vitte_hir_lower_find_symbol(context,
                vitte_hir_lower_intern_node_text(
                    context, vitte_hir_lower_node(context, item->children[0])));
            written = snprintf(qualified, sizeof(qualified), "%s::%s",
                prefix_string->data, name_string->data);
            if (written < 0 || (size_t)written >= sizeof(qualified)) return false;
            qualified_id = vitte_hir_intern_string(context->hir, qualified, (size_t)written);
            if (!vitte_hir_lower_bind_alias(context, qualified_id, symbol)) return false;
        }
    }
    return true;
}

static bool
vitte_hir_lower_collect_proc_returns(vitte_hir_lower_context_t *context,
                                     vitte_ast_node_id_t id)
{
    const vitte_ast_node_t *node = vitte_hir_lower_node(context, id);
    size_t index;
    if (node == NULL) return false;
    if (node->kind == VITTE_AST_NODE_SPACE_DECL) {
        if (node->child_count < 2u) return true;
        node = vitte_hir_lower_node(context, node->children[1]);
        if (node == NULL) return false;
        for (index = 0u; index < node->child_count; ++index) {
            if (!vitte_hir_lower_collect_proc_returns(context, node->children[index])) return false;
        }
        return true;
    }
    if (node->kind == VITTE_AST_NODE_PROC_DECL ||
        node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) {
        vitte_hir_type_id_t return_type = context->unit_type;
        vitte_hir_string_id_t name = vitte_hir_lower_intern_node_text(
            context, vitte_hir_lower_node(context, node->children[0]));
        for (index = 1u; index < node->child_count; ++index) {
            const vitte_ast_node_t *part = vitte_hir_lower_node(context, node->children[index]);
            if (part != NULL && vitte_ast_node_kind_is_type(part->kind)) {
                return_type = vitte_hir_lower_type(context, node->children[index]);
            }
        }
        return vitte_hir_lower_set_proc_return(context, name, return_type);
    }
    return true;
}

static bool
vitte_hir_lower_append_declarations(vitte_hir_lower_context_t *context,
                                    vitte_hir_node_t *module,
                                    vitte_ast_node_id_t id)
{
    const vitte_ast_node_t *node = vitte_hir_lower_node(context, id);
    size_t index;
    if (node == NULL || module == NULL) return false;
    if (node->kind == VITTE_AST_NODE_SPACE_DECL) {
        if (node->child_count < 2u) return true;
        node = vitte_hir_lower_node(context, node->children[1]);
        if (node == NULL) return false;
        for (index = 0u; index < node->child_count; ++index) {
            if (!vitte_hir_lower_append_declarations(
                    context, module, node->children[index])) return false;
        }
        return true;
    }
    if (node->kind == VITTE_AST_NODE_SPACE_DECL) return true;
    {
        vitte_hir_node_id_t child = vitte_hir_lower_declaration(context, id);
        return child != VITTE_HIR_INVALID_ID &&
               vitte_hir_lower_list_push(&module->as.module.items, child);
    }
}

static vitte_hir_binary_operator_t
vitte_hir_lower_binary(vitte_token_kind_t kind)
{
    switch (kind) {
        case VITTE_TOKEN_PLUS: return VITTE_HIR_BINARY_ADD;
        case VITTE_TOKEN_MINUS: return VITTE_HIR_BINARY_SUBTRACT;
        case VITTE_TOKEN_STAR: return VITTE_HIR_BINARY_MULTIPLY;
        case VITTE_TOKEN_SLASH: return VITTE_HIR_BINARY_DIVIDE;
        case VITTE_TOKEN_PERCENT: return VITTE_HIR_BINARY_REMAINDER;
        case VITTE_TOKEN_AMP: return VITTE_HIR_BINARY_BIT_AND;
        case VITTE_TOKEN_PIPE: return VITTE_HIR_BINARY_BIT_OR;
        case VITTE_TOKEN_CARET: return VITTE_HIR_BINARY_BIT_XOR;
        case VITTE_TOKEN_SHIFT_LEFT: return VITTE_HIR_BINARY_SHIFT_LEFT;
        case VITTE_TOKEN_SHIFT_RIGHT: return VITTE_HIR_BINARY_SHIFT_RIGHT;
        case VITTE_TOKEN_EQUAL_EQUAL: return VITTE_HIR_BINARY_EQUAL;
        case VITTE_TOKEN_BANG_EQUAL: return VITTE_HIR_BINARY_NOT_EQUAL;
        case VITTE_TOKEN_LESS: return VITTE_HIR_BINARY_LESS;
        case VITTE_TOKEN_LESS_EQUAL: return VITTE_HIR_BINARY_LESS_EQUAL;
        case VITTE_TOKEN_GREATER: return VITTE_HIR_BINARY_GREATER;
        case VITTE_TOKEN_GREATER_EQUAL: return VITTE_HIR_BINARY_GREATER_EQUAL;
        case VITTE_TOKEN_KW_AND:
        case VITTE_TOKEN_AMP_AMP: return VITTE_HIR_BINARY_AND;
        case VITTE_TOKEN_KW_OR:
        case VITTE_TOKEN_PIPE_PIPE: return VITTE_HIR_BINARY_OR;
        default: return VITTE_HIR_BINARY_INVALID;
    }
}

static vitte_hir_binary_operator_t
vitte_hir_lower_compound_assignment(vitte_token_kind_t kind)
{
    switch (kind) {
        case VITTE_TOKEN_PLUS_EQUAL: return VITTE_HIR_BINARY_ADD;
        case VITTE_TOKEN_MINUS_EQUAL: return VITTE_HIR_BINARY_SUBTRACT;
        case VITTE_TOKEN_STAR_EQUAL: return VITTE_HIR_BINARY_MULTIPLY;
        case VITTE_TOKEN_SLASH_EQUAL: return VITTE_HIR_BINARY_DIVIDE;
        case VITTE_TOKEN_PERCENT_EQUAL: return VITTE_HIR_BINARY_REMAINDER;
        case VITTE_TOKEN_AMP_EQUAL: return VITTE_HIR_BINARY_BIT_AND;
        case VITTE_TOKEN_PIPE_EQUAL: return VITTE_HIR_BINARY_BIT_OR;
        case VITTE_TOKEN_CARET_EQUAL: return VITTE_HIR_BINARY_BIT_XOR;
        case VITTE_TOKEN_SHIFT_LEFT_EQUAL: return VITTE_HIR_BINARY_SHIFT_LEFT;
        case VITTE_TOKEN_SHIFT_RIGHT_EQUAL: return VITTE_HIR_BINARY_SHIFT_RIGHT;
        default: return VITTE_HIR_BINARY_INVALID;
    }
}

static vitte_hir_unary_operator_t
vitte_hir_lower_unary(vitte_token_kind_t kind)
{
    switch (kind) {
        case VITTE_TOKEN_PLUS: return VITTE_HIR_UNARY_PLUS;
        case VITTE_TOKEN_MINUS: return VITTE_HIR_UNARY_NEGATE;
        case VITTE_TOKEN_KW_NOT:
        case VITTE_TOKEN_BANG: return VITTE_HIR_UNARY_NOT;
        case VITTE_TOKEN_TILDE: return VITTE_HIR_UNARY_BIT_NOT;
        default: return VITTE_HIR_UNARY_INVALID;
    }
}

static vitte_hir_node_id_t vitte_hir_lower_expr(vitte_hir_lower_context_t *, vitte_ast_node_id_t);
static vitte_hir_node_id_t vitte_hir_lower_statement(vitte_hir_lower_context_t *, vitte_ast_node_id_t);

static vitte_hir_string_id_t
vitte_hir_lower_string_literal(vitte_hir_lower_context_t *context,
                               const vitte_token_t *token)
{
    char *decoded;
    size_t begin = 0u;
    size_t end;
    size_t read;
    size_t write = 0u;
    vitte_hir_string_id_t result;
    if (context == NULL || token == NULL) return VITTE_HIR_INVALID_ID;
    end = token->length;
    if (end >= 2u && token->lexeme[0] == '"' && token->lexeme[end - 1u] == '"') {
        begin = 1u;
        --end;
    }
    decoded = (char *)malloc(end - begin + 1u);
    if (decoded == NULL) return VITTE_HIR_INVALID_ID;
    for (read = begin; read < end; ++read) {
        if (token->lexeme[read] == '\\' && read + 1u < end) {
            ++read;
            switch (token->lexeme[read]) {
                case 'n': decoded[write++] = '\n'; break;
                case 'r': decoded[write++] = '\r'; break;
                case 't': decoded[write++] = '\t'; break;
                case '0': decoded[write++] = '\0'; break;
                default: decoded[write++] = token->lexeme[read]; break;
            }
        } else {
            decoded[write++] = token->lexeme[read];
        }
    }
    result = vitte_hir_intern_string(context->hir, decoded, write);
    free(decoded);
    return result;
}

static vitte_hir_node_id_t
vitte_hir_lower_expr(vitte_hir_lower_context_t *context, vitte_ast_node_id_t id)
{
    const vitte_ast_node_t *node = vitte_hir_lower_node(context, id);
    vitte_hir_node_t result;
    vitte_hir_node_id_t child;
    vitte_hir_string_id_t name;
    const vitte_token_t *token;
    size_t index;
    char *end;
    uint64_t integer;

    if (node == NULL) return VITTE_HIR_INVALID_ID;
    memset(&result, 0, sizeof(result));
    result.span = vitte_hir_lower_span(&node->span);
    result.type = context->i64_type;
    token = vitte_hir_lower_token(context, node);
    switch (node->kind) {
        case VITTE_AST_NODE_INTEGER_LITERAL:
            result.kind = VITTE_HIR_NODE_LITERAL;
            result.as.literal.value.kind = VITTE_HIR_LITERAL_SIGNED_INTEGER;
            errno = 0;
            integer = token == NULL ? 0u : strtoull(token->lexeme, &end, 0);
            result.as.literal.value.as.signed_integer =
                (errno == 0 && end != token->lexeme) ? (int64_t)integer : 0;
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_FLOAT_LITERAL:
            result.kind = VITTE_HIR_NODE_LITERAL;
            result.type = context->f64_type;
            result.as.literal.value.kind = VITTE_HIR_LITERAL_F64;
            result.as.literal.value.as.f64 = token == NULL ? 0.0 : strtod(token->lexeme, &end);
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_STRING_LITERAL:
            result.kind = VITTE_HIR_NODE_LITERAL;
            result.type = context->string_type;
            result.as.literal.value.kind = VITTE_HIR_LITERAL_STRING;
            result.as.literal.value.as.string = vitte_hir_lower_string_literal(context, token);
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_CHARACTER_LITERAL:
            result.kind = VITTE_HIR_NODE_LITERAL;
            result.as.literal.value.kind = VITTE_HIR_LITERAL_SIGNED_INTEGER;
            if (token != NULL && token->length >= 3u) {
                size_t position = 1u;
                char character = token->lexeme[position];
                if (character == '\\' && position + 1u < token->length - 1u) {
                    ++position;
                    switch (token->lexeme[position]) {
                        case 'n': character = '\n'; break;
                        case 'r': character = '\r'; break;
                        case 't': character = '\t'; break;
                        default: character = token->lexeme[position]; break;
                    }
                }
                result.as.literal.value.as.signed_integer = (unsigned char)character;
            }
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_BOOL_LITERAL:
            result.kind = VITTE_HIR_NODE_LITERAL;
            result.type = context->bool_type;
            result.as.literal.value.kind = VITTE_HIR_LITERAL_BOOL;
            result.as.literal.value.as.boolean = token != NULL && token->length == 4u &&
                memcmp(token->lexeme, "true", 4u) == 0;
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_NULL_LITERAL:
            result.kind = VITTE_HIR_NODE_LITERAL;
            result.as.literal.value.kind = VITTE_HIR_LITERAL_NULL;
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_IDENTIFIER:
        case VITTE_AST_NODE_PATH:
            name = vitte_hir_lower_intern_node_text(context, node);
            if (name == VITTE_HIR_INVALID_ID) return VITTE_HIR_INVALID_ID;
            child = vitte_hir_lower_find_symbol(context, name);
            if (child == VITTE_HIR_INVALID_ID) {
                child = vitte_hir_lower_symbol(context, id, VITTE_HIR_SYMBOL_LOCAL,
                                               context->i64_type, VITTE_HIR_VISIBILITY_PRIVATE,
                                               VITTE_HIR_MUTABILITY_IMMUTABLE);
            }
            {
                const vitte_hir_symbol_t *referenced =
                    vitte_hir_get_symbol(context->hir, child);
                if (referenced == NULL) return VITTE_HIR_INVALID_ID;
                result.type = referenced->type;
            }
            result.kind = VITTE_HIR_NODE_SYMBOL;
            result.as.symbol_ref.symbol = child;
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_GROUP_EXPR:
            return node->child_count == 0u ? VITTE_HIR_INVALID_ID :
                vitte_hir_lower_expr(context, node->children[0]);
        case VITTE_AST_NODE_SIZEOF_EXPR: {
            vitte_hir_type_id_t queried_type;

            if (node->child_count != 1u) return VITTE_HIR_INVALID_ID;
            if ((node->flags &
                 VITTE_AST_FLAG_TYPE_QUERY_TYPE_OPERAND) != 0u) {
                queried_type = vitte_hir_lower_type(
                    context,
                    node->children[0]);
            } else {
                const vitte_hir_node_t *operand;

                child = vitte_hir_lower_expr(
                    context,
                    node->children[0]);
                operand = vitte_hir_get_node(context->hir, child);
                if (operand == NULL) return VITTE_HIR_INVALID_ID;
                queried_type = operand->type;
            }
            if (queried_type == VITTE_HIR_INVALID_ID) {
                return VITTE_HIR_INVALID_ID;
            }
            result.kind = VITTE_HIR_NODE_SIZEOF;
            result.type = context->i64_type;
            result.as.type_query.queried_type = queried_type;
            return vitte_hir_lower_add(context, &result);
        }
        case VITTE_AST_NODE_ALIGNOF_EXPR: {
            vitte_hir_type_id_t queried_type;

            if (node->child_count != 1u) return VITTE_HIR_INVALID_ID;
            queried_type = vitte_hir_lower_type(
                context,
                node->children[0]);
            if (queried_type == VITTE_HIR_INVALID_ID) {
                return VITTE_HIR_INVALID_ID;
            }
            result.kind = VITTE_HIR_NODE_ALIGNOF;
            result.type = context->i64_type;
            result.as.type_query.queried_type = queried_type;
            return vitte_hir_lower_add(context, &result);
        }
        case VITTE_AST_NODE_UNARY_EXPR:
            if (node->child_count == 0u) return VITTE_HIR_INVALID_ID;
            if (node->operator_kind == VITTE_TOKEN_STAR) {
                const vitte_hir_node_t *operand;
                const vitte_hir_type_t *operand_type;

                result.kind = VITTE_HIR_NODE_DEREF;
                result.as.unary_expression.expression = vitte_hir_lower_expr(
                    context, node->children[0]);
                operand = vitte_hir_get_node(
                    context->hir, result.as.unary_expression.expression);
                operand_type = operand == NULL
                    ? NULL : vitte_hir_get_type(context->hir, operand->type);
                if (operand_type == NULL ||
                    (operand_type->kind != VITTE_HIR_TYPE_POINTER &&
                     operand_type->kind != VITTE_HIR_TYPE_REFERENCE) ||
                    operand_type->element_type == VITTE_HIR_INVALID_ID) {
                    return VITTE_HIR_INVALID_ID;
                }
                result.type = operand_type->element_type;
                return vitte_hir_lower_add(context, &result);
            }
            result.kind = VITTE_HIR_NODE_UNARY;
            result.as.unary.op = vitte_hir_lower_unary(node->operator_kind);
            result.as.unary.operand = vitte_hir_lower_expr(context, node->children[0]);
            return result.as.unary.op == VITTE_HIR_UNARY_INVALID ||
                   result.as.unary.operand == VITTE_HIR_INVALID_ID
                ? VITTE_HIR_INVALID_ID : vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_BINARY_EXPR:
        case VITTE_AST_NODE_ASSIGN_EXPR:
            if (node->child_count < 2u) return VITTE_HIR_INVALID_ID;
            result.kind = node->kind == VITTE_AST_NODE_ASSIGN_EXPR
                ? VITTE_HIR_NODE_ASSIGN : VITTE_HIR_NODE_BINARY;
            if (result.kind == VITTE_HIR_NODE_ASSIGN) {
                result.as.assignment.target = vitte_hir_lower_expr(context, node->children[0]);
                result.as.assignment.value = vitte_hir_lower_expr(context, node->children[1]);
                if (result.as.assignment.target == VITTE_HIR_INVALID_ID ||
                    result.as.assignment.value == VITTE_HIR_INVALID_ID) {
                    return VITTE_HIR_INVALID_ID;
                }
                if (node->operator_kind != VITTE_TOKEN_EQUAL) {
                    vitte_hir_node_t compound;
                    vitte_hir_binary_operator_t compound_operator =
                        vitte_hir_lower_compound_assignment(node->operator_kind);
                    const vitte_hir_node_t *target = vitte_hir_get_node(
                        context->hir, result.as.assignment.target);
                    if (compound_operator == VITTE_HIR_BINARY_INVALID || target == NULL) {
                        return VITTE_HIR_INVALID_ID;
                    }
                    memset(&compound, 0, sizeof(compound));
                    compound.kind = VITTE_HIR_NODE_BINARY;
                    compound.span = result.span;
                    compound.type = target->type;
                    compound.as.binary.op = compound_operator;
                    compound.as.binary.left = result.as.assignment.target;
                    compound.as.binary.right = result.as.assignment.value;
                    result.as.assignment.value = vitte_hir_lower_add(context, &compound);
                    if (result.as.assignment.value == VITTE_HIR_INVALID_ID) {
                        return VITTE_HIR_INVALID_ID;
                    }
                }
                return vitte_hir_lower_add(context, &result);
            }
            result.as.binary.op = vitte_hir_lower_binary(node->operator_kind);
            result.as.binary.left = vitte_hir_lower_expr(context, node->children[0]);
            result.as.binary.right = vitte_hir_lower_expr(context, node->children[1]);
            if (result.as.binary.op == VITTE_HIR_BINARY_INVALID ||
                result.as.binary.left == VITTE_HIR_INVALID_ID ||
                result.as.binary.right == VITTE_HIR_INVALID_ID) {
                return VITTE_HIR_INVALID_ID;
            }
            if (result.as.binary.op == VITTE_HIR_BINARY_ADD) {
                const vitte_hir_node_t *left = vitte_hir_get_node(
                    context->hir, result.as.binary.left);
                const vitte_hir_node_t *right = vitte_hir_get_node(
                    context->hir, result.as.binary.right);
                const vitte_hir_type_t *left_type = left == NULL
                    ? NULL : vitte_hir_get_type(context->hir, left->type);
                const vitte_hir_type_t *right_type = right == NULL
                    ? NULL : vitte_hir_get_type(context->hir, right->type);
                if (left_type != NULL && right_type != NULL &&
                    left_type->kind == VITTE_HIR_TYPE_STRING &&
                    right_type->kind == VITTE_HIR_TYPE_STRING) {
                    result.type = context->string_type;
                }
            }
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_CALL_EXPR:
            if (node->child_count == 0u) return VITTE_HIR_INVALID_ID;
            result.kind = VITTE_HIR_NODE_CALL;
            {
                const vitte_ast_node_t *callee = vitte_hir_lower_node(context, node->children[0]);
                vitte_hir_string_id_t callee_name = vitte_hir_lower_intern_node_text(context, callee);
                vitte_hir_type_id_t return_type = vitte_hir_lower_find_proc_return(context, callee_name);
                if (return_type != VITTE_HIR_INVALID_ID) result.type = return_type;
                if (callee != NULL && callee->kind == VITTE_AST_NODE_MEMBER_EXPR &&
                    callee->child_count >= 2u) {
                    const vitte_token_t *member_token = vitte_hir_lower_token(
                        context, vitte_hir_lower_node(context, callee->children[1]));
                    if (member_token != NULL && member_token->length == 3u &&
                        memcmp(member_token->lexeme, "len", 3u) == 0) {
                        result.type = context->u64_type;
                    } else if (member_token != NULL && member_token->length == 5u &&
                               memcmp(member_token->lexeme, "slice", 5u) == 0) {
                        result.type = context->string_type;
                    }
                }
            }
            result.as.call.callee = vitte_hir_lower_expr(context, node->children[0]);
            if (result.as.call.callee == VITTE_HIR_INVALID_ID) return VITTE_HIR_INVALID_ID;
            for (index = 1u; index < node->child_count; ++index) {
                child = vitte_hir_lower_expr(context, node->children[index]);
                if (!vitte_hir_lower_list_push(&result.as.call.arguments, child)) {
                    vitte_hir_lower_list_destroy(&result.as.call.arguments);
                    return VITTE_HIR_INVALID_ID;
                }
            }
            child = vitte_hir_lower_add(context, &result);
            vitte_hir_lower_list_destroy(&result.as.call.arguments);
            return child;
        case VITTE_AST_NODE_INDEX_EXPR:
            if (node->child_count < 2u) return VITTE_HIR_INVALID_ID;
            result.kind = VITTE_HIR_NODE_INDEX;
            result.as.index.object = vitte_hir_lower_expr(context, node->children[0]);
            result.as.index.index = vitte_hir_lower_expr(context, node->children[1]);
            if (result.as.index.object == VITTE_HIR_INVALID_ID ||
                result.as.index.index == VITTE_HIR_INVALID_ID) {
                return VITTE_HIR_INVALID_ID;
            }
            {
                const vitte_hir_node_t *object = vitte_hir_get_node(
                    context->hir, result.as.index.object);
                const vitte_hir_type_t *object_type = object == NULL
                    ? NULL : vitte_hir_get_type(context->hir, object->type);
                if (object_type != NULL &&
                    (object_type->kind == VITTE_HIR_TYPE_POINTER ||
                     object_type->kind == VITTE_HIR_TYPE_REFERENCE ||
                     object_type->kind == VITTE_HIR_TYPE_ARRAY ||
                     object_type->kind == VITTE_HIR_TYPE_SLICE)) {
                    result.type = object_type->element_type;
                } else if (object_type != NULL &&
                           object_type->kind == VITTE_HIR_TYPE_STRING) {
                    result.type = vitte_hir_lower_primitive(
                        context, VITTE_HIR_TYPE_U8);
                }
            }
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_MEMBER_EXPR:
            if (node->child_count < 2u) return VITTE_HIR_INVALID_ID;
            result.kind = VITTE_HIR_NODE_MEMBER;
            {
                const vitte_token_t *member_token = vitte_hir_lower_token(
                    context, vitte_hir_lower_node(context, node->children[1]));
                if (member_token != NULL && member_token->length == 6u &&
                    memcmp(member_token->lexeme, "length", 6u) == 0) {
                    result.type = context->u64_type;
                }
            }
            result.as.member.object = vitte_hir_lower_expr(context, node->children[0]);
            result.as.member.member = vitte_hir_lower_member_symbol(
                context, node->children[1]);
            if (result.as.member.object == VITTE_HIR_INVALID_ID ||
                result.as.member.member == VITTE_HIR_INVALID_ID) {
                return VITTE_HIR_INVALID_ID;
            }
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_CAST_EXPR:
            if (node->child_count < 2u) return VITTE_HIR_INVALID_ID;
            result.kind = VITTE_HIR_NODE_CAST;
            result.as.cast.expression = vitte_hir_lower_expr(context, node->children[0]);
            result.as.cast.destination_type = vitte_hir_lower_type(context, node->children[1]);
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_ARRAY_EXPR:
            result.kind = VITTE_HIR_NODE_ARRAY;
            for (index = 0u; index < node->child_count; ++index) {
                child = vitte_hir_lower_expr(context, node->children[index]);
                if (!vitte_hir_lower_list_push(&result.as.aggregate.elements, child)) {
                    vitte_hir_lower_list_destroy(&result.as.aggregate.elements);
                    return VITTE_HIR_INVALID_ID;
                }
            }
            child = vitte_hir_lower_add(context, &result);
            vitte_hir_lower_list_destroy(&result.as.aggregate.elements);
            return child;
        default:
            return VITTE_HIR_INVALID_ID;
    }
}

static vitte_hir_node_id_t
vitte_hir_lower_statement(vitte_hir_lower_context_t *context, vitte_ast_node_id_t id)
{
    const vitte_ast_node_t *node = vitte_hir_lower_node(context, id);
    vitte_hir_node_t result;
    vitte_hir_node_id_t child;
    size_t index;
    if (node == NULL) return VITTE_HIR_INVALID_ID;
    memset(&result, 0, sizeof(result));
    result.span = vitte_hir_lower_span(&node->span);
    switch (node->kind) {
        case VITTE_AST_NODE_BLOCK:
        case VITTE_AST_NODE_BLOCK_EXPR:
            result.kind = VITTE_HIR_NODE_BLOCK;
            for (index = 0u; index < node->child_count; ++index) {
                child = vitte_hir_lower_statement(context, node->children[index]);
                if (child == VITTE_HIR_INVALID_ID ||
                    !vitte_hir_lower_list_push(&result.as.block.statements, child)) {
                    vitte_hir_lower_list_destroy(&result.as.block.statements);
                    return VITTE_HIR_INVALID_ID;
                }
            }
            child = vitte_hir_lower_add(context, &result);
            vitte_hir_lower_list_destroy(&result.as.block.statements);
            return child;
        case VITTE_AST_NODE_LET_STMT: {
            vitte_hir_type_id_t type = context->i64_type;
            vitte_hir_symbol_id_t symbol;
            vitte_hir_node_id_t initializer = VITTE_HIR_INVALID_ID;
            const vitte_ast_node_t *annotation = node->child_count > 1u
                ? vitte_hir_lower_node(context, node->children[1]) : NULL;
            bool has_annotation = annotation != NULL &&
                vitte_ast_node_kind_is_type(annotation->kind);

            if (node->child_count == 0u) return VITTE_HIR_INVALID_ID;
            if (has_annotation) {
                type = vitte_hir_lower_type(context, node->children[1]);
            }
            if (node->child_count > 1u) {
                const vitte_ast_node_t *last = vitte_hir_lower_node(context,
                    node->children[node->child_count - 1u]);
                if (last != NULL && vitte_ast_node_kind_is_expression(last->kind)) {
                    initializer = vitte_hir_lower_expr(
                        context, node->children[node->child_count - 1u]);
                    if (initializer == VITTE_HIR_INVALID_ID) {
                        return VITTE_HIR_INVALID_ID;
                    }
                }
            }
            if (!has_annotation && initializer != VITTE_HIR_INVALID_ID) {
                const vitte_hir_node_t *initializer_node = vitte_hir_get_node(
                    context->hir, initializer);
                if (initializer_node == NULL ||
                    initializer_node->type == VITTE_HIR_INVALID_ID) {
                    return VITTE_HIR_INVALID_ID;
                }
                type = initializer_node->type;
            }
            symbol = vitte_hir_lower_symbol(context, node->children[0],
                VITTE_HIR_SYMBOL_LOCAL, type, VITTE_HIR_VISIBILITY_PRIVATE,
                (node->flags & VITTE_AST_FLAG_MUTABLE) != 0u
                    ? VITTE_HIR_MUTABILITY_MUTABLE
                    : VITTE_HIR_MUTABILITY_IMMUTABLE);
            if (symbol == VITTE_HIR_INVALID_ID) return VITTE_HIR_INVALID_ID;
            result.kind = VITTE_HIR_NODE_LET;
            result.as.let_stmt.symbol = symbol;
            result.as.let_stmt.declared_type = type;
            result.as.let_stmt.initializer = initializer;
            result.as.let_stmt.mutability = (node->flags & VITTE_AST_FLAG_MUTABLE) != 0u
                ? VITTE_HIR_MUTABILITY_MUTABLE : VITTE_HIR_MUTABILITY_IMMUTABLE;
            return vitte_hir_lower_add(context, &result);
        }
        case VITTE_AST_NODE_RETURN_STMT:
            result.kind = VITTE_HIR_NODE_RETURN;
            if (node->child_count != 0u) result.as.return_stmt.value = vitte_hir_lower_expr(context, node->children[0]);
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_EXPR_STMT:
            if (node->child_count == 0u) return VITTE_HIR_INVALID_ID;
            {
                const vitte_ast_node_t *expression = vitte_hir_lower_node(
                    context, node->children[0]);
                if (expression != NULL &&
                    expression->kind == VITTE_AST_NODE_ASSIGN_EXPR) {
                    return vitte_hir_lower_expr(context, node->children[0]);
                }
            }
            result.kind = VITTE_HIR_NODE_EXPR_STMT;
            result.as.expression_statement.expression = vitte_hir_lower_expr(context, node->children[0]);
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_IF_STMT:
            if (node->child_count < 2u) return VITTE_HIR_INVALID_ID;
            result.kind = VITTE_HIR_NODE_IF;
            result.as.if_expr.condition = vitte_hir_lower_expr(context, node->children[0]);
            result.as.if_expr.then_branch = vitte_hir_lower_statement(context, node->children[1]);
            if (node->child_count > 2u) result.as.if_expr.else_branch = vitte_hir_lower_statement(context, node->children[2]);
            if (result.as.if_expr.condition == VITTE_HIR_INVALID_ID ||
                result.as.if_expr.then_branch == VITTE_HIR_INVALID_ID ||
                (node->child_count > 2u &&
                 result.as.if_expr.else_branch == VITTE_HIR_INVALID_ID)) {
                return VITTE_HIR_INVALID_ID;
            }
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_WHILE_STMT:
            if (node->child_count < 2u) return VITTE_HIR_INVALID_ID;
            result.kind = VITTE_HIR_NODE_WHILE;
            result.as.while_stmt.condition = vitte_hir_lower_expr(context, node->children[0]);
            result.as.while_stmt.body = vitte_hir_lower_statement(context, node->children[1]);
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_LOOP_STMT:
            if (node->child_count == 0u) return VITTE_HIR_INVALID_ID;
            result.kind = VITTE_HIR_NODE_LOOP;
            result.as.loop_stmt.body = vitte_hir_lower_statement(context, node->children[0]);
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_BREAK_STMT:
            result.kind = VITTE_HIR_NODE_BREAK;
            if (node->child_count != 0u) result.as.break_stmt.value = vitte_hir_lower_expr(context, node->children[0]);
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_CONTINUE_STMT:
            result.kind = VITTE_HIR_NODE_CONTINUE;
            return vitte_hir_lower_add(context, &result);
        default:
            return vitte_hir_lower_expr(context, id);
    }
}

static vitte_hir_node_id_t
vitte_hir_lower_declaration(vitte_hir_lower_context_t *context, vitte_ast_node_id_t id)
{
    const vitte_ast_node_t *node = vitte_hir_lower_node(context, id);
    vitte_hir_node_t result;
    vitte_hir_node_id_t child;
    vitte_hir_symbol_id_t symbol;
    size_t index;
    if (node == NULL) return VITTE_HIR_INVALID_ID;
    memset(&result, 0, sizeof(result));
    result.span = vitte_hir_lower_span(&node->span);
    switch (node->kind) {
        case VITTE_AST_NODE_USE_DECL: {
            const vitte_ast_node_t *path = node->child_count == 0u ? NULL :
                vitte_hir_lower_node(context, node->children[0]);
            result.kind = VITTE_HIR_NODE_USE;
            result.as.use_decl.path = vitte_hir_lower_intern_node_text(context, path);
            return vitte_hir_lower_add(context, &result);
        }
        case VITTE_AST_NODE_CONST_DECL:
        case VITTE_AST_NODE_STATIC_DECL:
            if (node->child_count < 2u) return VITTE_HIR_INVALID_ID;
            symbol = vitte_hir_lower_symbol(context, node->children[0],
                node->kind == VITTE_AST_NODE_CONST_DECL ? VITTE_HIR_SYMBOL_CONST : VITTE_HIR_SYMBOL_STATIC,
                context->i64_type, (node->flags & VITTE_AST_FLAG_PUBLIC) != 0u
                    ? VITTE_HIR_VISIBILITY_PUBLIC : VITTE_HIR_VISIBILITY_PRIVATE,
                node->kind == VITTE_AST_NODE_STATIC_DECL ? VITTE_HIR_MUTABILITY_MUTABLE : VITTE_HIR_MUTABILITY_IMMUTABLE);
            if (symbol == VITTE_HIR_INVALID_ID) return symbol;
            result.kind = node->kind == VITTE_AST_NODE_CONST_DECL
                ? VITTE_HIR_NODE_CONST_DECL : VITTE_HIR_NODE_STATIC_DECL;
            if (result.kind == VITTE_HIR_NODE_CONST_DECL) {
                result.as.constant.symbol = symbol;
                result.as.constant.value = vitte_hir_lower_expr(context, node->children[node->child_count - 1u]);
                result.as.constant.visibility = (node->flags & VITTE_AST_FLAG_PUBLIC) != 0u
                    ? VITTE_HIR_VISIBILITY_PUBLIC : VITTE_HIR_VISIBILITY_PRIVATE;
            } else {
                result.as.static_decl.symbol = symbol;
                result.as.static_decl.value = vitte_hir_lower_expr(context, node->children[node->child_count - 1u]);
                result.as.static_decl.visibility = (node->flags & VITTE_AST_FLAG_PUBLIC) != 0u
                    ? VITTE_HIR_VISIBILITY_PUBLIC : VITTE_HIR_VISIBILITY_PRIVATE;
                result.as.static_decl.mutability = VITTE_HIR_MUTABILITY_MUTABLE;
            }
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_TYPE_DECL:
            if (node->child_count < 2u) return VITTE_HIR_INVALID_ID;
            symbol = vitte_hir_lower_symbol(context, node->children[0], VITTE_HIR_SYMBOL_TYPE,
                context->i64_type, (node->flags & VITTE_AST_FLAG_PUBLIC) != 0u
                    ? VITTE_HIR_VISIBILITY_PUBLIC : VITTE_HIR_VISIBILITY_PRIVATE,
                VITTE_HIR_MUTABILITY_IMMUTABLE);
            result.kind = VITTE_HIR_NODE_TYPE_DECL;
            result.as.type_decl.symbol = symbol;
            result.as.type_decl.aliased_type = vitte_hir_lower_type(context, node->children[node->child_count - 1u]);
            result.as.type_decl.visibility = (node->flags & VITTE_AST_FLAG_PUBLIC) != 0u
                ? VITTE_HIR_VISIBILITY_PUBLIC : VITTE_HIR_VISIBILITY_PRIVATE;
            return vitte_hir_lower_add(context, &result);
        case VITTE_AST_NODE_PROC_DECL:
        case VITTE_AST_NODE_EXTERN_PROC_DECL:
            if (node->child_count == 0u) return VITTE_HIR_INVALID_ID;
            symbol = vitte_hir_lower_symbol(context, node->children[0], VITTE_HIR_SYMBOL_PROC,
                context->i64_type, (node->flags & VITTE_AST_FLAG_PUBLIC) != 0u
                    ? VITTE_HIR_VISIBILITY_PUBLIC : VITTE_HIR_VISIBILITY_PRIVATE,
                VITTE_HIR_MUTABILITY_IMMUTABLE);
            result.kind = node->kind == VITTE_AST_NODE_PROC_DECL
                ? VITTE_HIR_NODE_PROC_DECL : VITTE_HIR_NODE_EXTERN_PROC_DECL;
            result.as.procedure.symbol = symbol;
            result.as.procedure.return_type = context->unit_type;
            result.as.procedure.visibility = (node->flags & VITTE_AST_FLAG_PUBLIC) != 0u
                ? VITTE_HIR_VISIBILITY_PUBLIC : VITTE_HIR_VISIBILITY_PRIVATE;
            result.as.procedure.is_extern = node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL;
            for (index = 1u; index < node->child_count; ++index) {
                const vitte_ast_node_t *part = vitte_hir_lower_node(context, node->children[index]);
                if (part == NULL) continue;
                if (part->kind == VITTE_AST_NODE_PARAMETER && part->child_count >= 2u) {
                    vitte_hir_symbol_id_t parameter = vitte_hir_lower_symbol(context, part->children[0],
                        VITTE_HIR_SYMBOL_PARAMETER, vitte_hir_lower_type(context, part->children[1]),
                        VITTE_HIR_VISIBILITY_PRIVATE, VITTE_HIR_MUTABILITY_IMMUTABLE);
                    vitte_hir_node_t pattern;
                    memset(&pattern, 0, sizeof(pattern)); pattern.kind = VITTE_HIR_NODE_PATTERN_BINDING;
                    pattern.span = vitte_hir_lower_span(&part->span); pattern.as.binding_pattern.symbol = parameter;
                    child = vitte_hir_lower_add(context, &pattern);
                    if (!vitte_hir_lower_list_push(&result.as.procedure.parameters, child)) goto failure;
                } else if (vitte_ast_node_kind_is_type(part->kind)) {
                    result.as.procedure.return_type = vitte_hir_lower_type(context, node->children[index]);
                    if (!vitte_hir_lower_set_proc_return(
                            context,
                            vitte_hir_get_symbol(context->hir, symbol)->name,
                            result.as.procedure.return_type)) goto failure;
                } else if (part->kind == VITTE_AST_NODE_BLOCK) {
                    result.as.procedure.body = vitte_hir_lower_statement(context, node->children[index]);
                }
            }
            child = vitte_hir_lower_add(context, &result);
            vitte_hir_lower_list_destroy(&result.as.procedure.parameters);
            return child;
        default:
            return VITTE_HIR_INVALID_ID;
    }
failure:
    vitte_hir_lower_list_destroy(&result.as.procedure.parameters);
    return VITTE_HIR_INVALID_ID;
}

bool
vitte_hir_lower_ast(vitte_hir_t *hir, const vitte_parser_t *parser)
{
    vitte_hir_lower_context_t context;
    vitte_hir_node_t module;
    vitte_hir_node_id_t root;
    vitte_hir_node_id_t child;
    size_t index;
    const vitte_ast_node_t *translation;
    bool module_name_set = false;

    if (hir == NULL || parser == NULL || !vitte_parser_is_valid(parser) ||
        !vitte_parser_has_root(parser) || !vitte_hir_is_valid(hir) ||
        hir->state != VITTE_HIR_STATE_BUILDING) return false;
    memset(&context, 0, sizeof(context));
    memset(&module, 0, sizeof(module));
    context.hir = hir;
    context.parser = parser;
    context.unit_type = vitte_hir_lower_primitive(&context, VITTE_HIR_TYPE_UNIT);
    context.never_type = vitte_hir_lower_primitive(&context, VITTE_HIR_TYPE_NEVER);
    context.bool_type = vitte_hir_lower_primitive(&context, VITTE_HIR_TYPE_BOOL);
    context.i64_type = vitte_hir_lower_primitive(&context, VITTE_HIR_TYPE_I64);
    context.u64_type = vitte_hir_lower_primitive(&context, VITTE_HIR_TYPE_U64);
    context.f64_type = vitte_hir_lower_primitive(&context, VITTE_HIR_TYPE_F64);
    context.string_type = vitte_hir_lower_primitive(&context, VITTE_HIR_TYPE_STRING);
    if (context.unit_type == 0u || context.never_type == 0u || context.bool_type == 0u || context.i64_type == 0u ||
        context.u64_type == 0u ||
        context.f64_type == 0u || context.string_type == 0u) goto failure;
    translation = vitte_hir_lower_node(&context, vitte_parser_root(parser));
    if (translation == NULL) goto failure;
    for (index = 0u; index < translation->child_count; ++index) {
        if (!vitte_hir_lower_collect_proc_returns(
                &context, translation->children[index])) goto failure;
    }
    for (index = 0u; index < translation->child_count; ++index) {
        if (!vitte_hir_lower_predeclare_node(
                &context, translation->children[index])) goto failure;
    }
    for (index = 0u; index < translation->child_count; ++index) {
        const vitte_ast_node_t *item = vitte_hir_lower_node(
            &context, translation->children[index]);
        if (item != NULL && item->kind == VITTE_AST_NODE_SPACE_DECL &&
            !vitte_hir_lower_bind_space_aliases(
                &context, item->id, VITTE_HIR_INVALID_ID)) goto failure;
    }
    module.kind = VITTE_HIR_NODE_MODULE;
    module.span = vitte_hir_lower_span(&translation->span);
    module.as.module.name = vitte_hir_intern_string(hir, "main", 4u);
    for (index = 0u; index < translation->child_count; ++index) {
        const vitte_ast_node_t *source_item = vitte_hir_lower_node(&context, translation->children[index]);
        if (source_item != NULL && source_item->kind == VITTE_AST_NODE_SPACE_DECL) {
            if (!module_name_set && source_item->child_count != 0u) {
                vitte_hir_string_id_t module_name = vitte_hir_lower_intern_node_text(
                    &context, vitte_hir_lower_node(&context, source_item->children[0]));
                if (module_name != VITTE_HIR_INVALID_ID) {
                    module.as.module.name = module_name;
                    module_name_set = true;
                }
            }
            if (!vitte_hir_lower_append_declarations(
                    &context, &module, translation->children[index])) goto failure;
            continue;
        }
        child = vitte_hir_lower_declaration(&context, translation->children[index]);
        if (child == VITTE_HIR_INVALID_ID ||
            !vitte_hir_lower_list_push(&module.as.module.items, child)) goto failure;
    }
    root = vitte_hir_lower_add(&context, &module);
    vitte_hir_lower_list_destroy(&module.as.module.items);
    free(context.symbols);
    free(context.proc_returns);
    if (root == VITTE_HIR_INVALID_ID || !vitte_hir_add_root(hir, root)) return false;
    return vitte_hir_seal(hir);
failure:
    vitte_hir_lower_list_destroy(&module.as.module.items);
    free(context.symbols);
    free(context.proc_returns);
    return false;
}
