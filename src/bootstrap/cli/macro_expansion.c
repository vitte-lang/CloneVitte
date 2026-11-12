#include "macro_expansion.h"

#include <stdlib.h>
#include <string.h>

#define VITTE_MACRO_EXPANSION_MAX_DEPTH 64u
#define VITTE_MACRO_EXPANSION_MAX_TOKENS VITTE_LEXER_DEFAULT_MAX_TOKENS

typedef struct vitte_macro_definition {
    size_t begin;
    size_t end;
    size_t body_begin;
    size_t body_end;
    size_t *parameters;
    size_t parameter_count;
    const vitte_token_t *name;
    const vitte_token_t *tokens;
} vitte_macro_definition_t;

typedef struct vitte_macro_output {
    vitte_token_t *tokens;
    size_t count;
    size_t capacity;
    size_t expansions;
    vitte_macro_definition_t *definitions;
    size_t definition_count;
    vitte_macro_expansion_result_t *result;
} vitte_macro_output_t;

static bool
vitte_macro_same_name(const vitte_token_t *left, const vitte_token_t *right)
{
    return left != NULL && right != NULL &&
        left->kind == VITTE_TOKEN_IDENTIFIER &&
        right->kind == VITTE_TOKEN_IDENTIFIER &&
        left->length == right->length &&
        memcmp(left->lexeme, right->lexeme, left->length) == 0;
}

static bool
vitte_macro_token_is(const vitte_token_t *tokens, size_t count,
                     size_t index, vitte_token_kind_t kind)
{
    return index < count && tokens[index].kind == kind;
}

static bool
vitte_macro_is_declaration_modifier(vitte_token_kind_t kind)
{
    return kind == VITTE_TOKEN_KW_PUB || kind == VITTE_TOKEN_KW_EXPORT ||
        kind == VITTE_TOKEN_KW_ASYNC || kind == VITTE_TOKEN_KW_UNSAFE ||
        kind == VITTE_TOKEN_KW_COMPTIME;
}

static bool
vitte_macro_find_close(const vitte_token_t *tokens, size_t count,
                       size_t open, vitte_token_kind_t left,
                       vitte_token_kind_t right, size_t *close)
{
    size_t depth = 0u, index;
    for (index = open; index < count; ++index) {
        if (tokens[index].kind == left) ++depth;
        else if (tokens[index].kind == right && --depth == 0u) {
            *close = index;
            return true;
        }
    }
    return false;
}

static void
vitte_macro_set_error(vitte_macro_output_t *output,
                      vitte_macro_expansion_error_t error,
                      const vitte_token_t *token)
{
    if (output->result->error != VITTE_MACRO_EXPANSION_ERROR_NONE) return;
    output->result->error = error;
    if (token != NULL) output->result->error_span = token->span;
}

static bool
vitte_macro_append(vitte_macro_output_t *output, const vitte_token_t *token)
{
    vitte_token_t *grown;
    size_t capacity;
    if (output->count >= VITTE_MACRO_EXPANSION_MAX_TOKENS) {
        vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_LIMIT, token);
        return false;
    }
    if (output->count == output->capacity) {
        capacity = output->capacity == 0u ? 64u : output->capacity * 2u;
        if (capacity > VITTE_MACRO_EXPANSION_MAX_TOKENS)
            capacity = VITTE_MACRO_EXPANSION_MAX_TOKENS;
        if (capacity > SIZE_MAX / sizeof(*grown)) {
            vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_LIMIT, token);
            return false;
        }
        grown = (vitte_token_t *)realloc(output->tokens, capacity * sizeof(*grown));
        if (grown == NULL) {
            vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_OUT_OF_MEMORY, token);
            return false;
        }
        output->tokens = grown;
        output->capacity = capacity;
    }
    output->tokens[output->count++] = *token;
    return true;
}

static bool
vitte_macro_buffer_append(vitte_macro_output_t *output,
                          vitte_token_t **tokens, size_t *count,
                          size_t *capacity, const vitte_token_t *token)
{
    vitte_token_t *grown;
    size_t next;
    if (*count >= VITTE_MACRO_EXPANSION_MAX_TOKENS) {
        vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_LIMIT, token);
        return false;
    }
    if (*count == *capacity) {
        next = *capacity == 0u ? 32u : *capacity * 2u;
        if (next > VITTE_MACRO_EXPANSION_MAX_TOKENS)
            next = VITTE_MACRO_EXPANSION_MAX_TOKENS;
        if (next > SIZE_MAX / sizeof(*grown)) {
            vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_LIMIT, token);
            return false;
        }
        grown = (vitte_token_t *)realloc(*tokens, next * sizeof(*grown));
        if (grown == NULL) {
            vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_OUT_OF_MEMORY, token);
            return false;
        }
        *tokens = grown;
        *capacity = next;
    }
    (*tokens)[(*count)++] = *token;
    return true;
}

static vitte_macro_definition_t *
vitte_macro_lookup(vitte_macro_output_t *output, const vitte_token_t *name)
{
    size_t index;
    for (index = 0u; index < output->definition_count; ++index) {
        if (vitte_macro_same_name(name, output->definitions[index].name))
            return &output->definitions[index];
    }
    return NULL;
}

static bool
vitte_macro_split_arguments(const vitte_token_t *tokens, size_t begin, size_t end,
                            size_t *starts, size_t *ends, size_t capacity,
                            size_t *argument_count)
{
    size_t index, start = begin, round = 0u, square = 0u, curly = 0u;
    *argument_count = 0u;
    if (begin == end) return true;
    for (index = begin; index < end; ++index) {
        vitte_token_kind_t kind = tokens[index].kind;
        if (kind == VITTE_TOKEN_LEFT_PAREN) ++round;
        else if (kind == VITTE_TOKEN_RIGHT_PAREN && round != 0u) --round;
        else if (kind == VITTE_TOKEN_LEFT_BRACKET) ++square;
        else if (kind == VITTE_TOKEN_RIGHT_BRACKET && square != 0u) --square;
        else if (kind == VITTE_TOKEN_LEFT_BRACE) ++curly;
        else if (kind == VITTE_TOKEN_RIGHT_BRACE && curly != 0u) --curly;
        if (kind == VITTE_TOKEN_COMMA && round == 0u && square == 0u &&
            curly == 0u) {
            if (*argument_count >= capacity) return false;
            starts[*argument_count] = start;
            ends[*argument_count] = index;
            ++*argument_count;
            start = index + 1u;
        }
    }
    if (*argument_count >= capacity) return false;
    starts[*argument_count] = start;
    ends[*argument_count] = end;
    ++*argument_count;
    return true;
}

static bool
vitte_macro_expand_range(vitte_macro_output_t *output,
                         const vitte_token_t *tokens,
                         size_t begin, size_t end, size_t depth);

static bool
vitte_macro_expand_invocation(vitte_macro_output_t *output,
                              const vitte_token_t *tokens, size_t token_count,
                              size_t name_index, size_t *after, size_t depth)
{
    vitte_macro_definition_t *definition;
    size_t open, close, starts_count = 0u, index, param;
    size_t *starts = NULL, *ends = NULL;
    vitte_token_t *substituted = NULL;
    size_t substituted_count = 0u, substituted_capacity = 0u;
    const vitte_token_t *name = &tokens[name_index];
    bool ok = false;

    if (name_index + 2u >= token_count ||
        tokens[name_index + 1u].kind != VITTE_TOKEN_BANG ||
        tokens[name_index + 2u].kind != VITTE_TOKEN_LEFT_PAREN) return false;
    definition = vitte_macro_lookup(output, name);
    if (definition == NULL) {
        vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_INVALID_INVOCATION, name);
        return false;
    }
    if (!vitte_macro_find_close(tokens, token_count, name_index + 2u,
            VITTE_TOKEN_LEFT_PAREN, VITTE_TOKEN_RIGHT_PAREN, &close)) {
        vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_INVALID_INVOCATION, name);
        return false;
    }
    open = name_index + 3u;
    starts = (size_t *)malloc((close - open + 1u) * sizeof(*starts));
    ends = (size_t *)malloc((close - open + 1u) * sizeof(*ends));
    if (starts == NULL || ends == NULL) {
        vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_OUT_OF_MEMORY, name);
        goto done;
    }
    if (!vitte_macro_split_arguments(tokens, open, close, starts, ends,
                                    close - open + 1u, &starts_count) ||
        starts_count != definition->parameter_count) {
        vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_INVALID_INVOCATION, name);
        goto done;
    }
    if (depth >= VITTE_MACRO_EXPANSION_MAX_DEPTH) {
        vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_RECURSION, name);
        goto done;
    }
    if (++output->expansions > VITTE_MACRO_EXPANSION_MAX_TOKENS) {
        vitte_macro_set_error(output, VITTE_MACRO_EXPANSION_ERROR_LIMIT, name);
        goto done;
    }
    for (index = definition->body_begin; index < definition->body_end; ++index) {
        const vitte_token_t *body_token = &definition->tokens[index];
        bool replaced = false;
        if (body_token->kind == VITTE_TOKEN_IDENTIFIER) {
            for (param = 0u; param < definition->parameter_count; ++param) {
                const vitte_token_t *parameter = &definition->tokens[definition->parameters[param]];
                if (!vitte_macro_same_name(body_token, parameter)) continue;
                for (size_t arg = starts[param]; arg < ends[param]; ++arg) {
                    if (!vitte_macro_buffer_append(output, &substituted,
                            &substituted_count, &substituted_capacity, &tokens[arg])) goto done;
                }
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            if (!vitte_macro_buffer_append(output, &substituted,
                    &substituted_count, &substituted_capacity, body_token)) goto done;
        }
    }
    if (!vitte_macro_expand_range(output, substituted,
                                  0u, substituted_count, depth + 1u)) goto done;
    *after = close + 1u;
    ok = true;
done:
    free(starts);
    free(ends);
    free(substituted);
    return ok;
}

static bool
vitte_macro_expand_range(vitte_macro_output_t *output,
                         const vitte_token_t *tokens,
                         size_t begin, size_t end, size_t depth)
{
    size_t index = begin;
    while (index < end && output->result->error == VITTE_MACRO_EXPANSION_ERROR_NONE) {
        if (tokens[index].kind == VITTE_TOKEN_KW_MACRO) {
            size_t def_index;
            for (def_index = 0u; def_index < output->definition_count; ++def_index) {
                vitte_macro_definition_t *definition = &output->definitions[def_index];
                if (definition->begin == index) {
                    index = definition->end;
                    break;
                }
            }
            if (def_index < output->definition_count) continue;
        }
        if (tokens[index].kind == VITTE_TOKEN_IDENTIFIER && index + 1u < end &&
            tokens[index + 1u].kind == VITTE_TOKEN_BANG) {
            size_t after = index;
            if (index + 2u < end && tokens[index + 2u].kind == VITTE_TOKEN_LEFT_PAREN) {
                if (!vitte_macro_expand_invocation(output, tokens, end, index,
                        &after, depth)) return false;
                index = after;
                continue;
            }
        }
        if (!vitte_macro_append(output, &tokens[index])) return false;
        ++index;
    }
    return output->result->error == VITTE_MACRO_EXPANSION_ERROR_NONE;
}

static bool
vitte_macro_collect_definitions(vitte_macro_output_t *output,
                                const vitte_token_t *tokens, size_t count)
{
    size_t index, macro_count = 0u;
    for (index = 0u; index < count; ++index)
        if (tokens[index].kind == VITTE_TOKEN_KW_MACRO) ++macro_count;
    output->definitions = (vitte_macro_definition_t *)calloc(macro_count, sizeof(*output->definitions));
    if (output->definitions == NULL && macro_count != 0u) return false;
    for (index = 0u; index < count; ++index) {
        size_t cursor, param_close = SIZE_MAX, body_close;
        vitte_macro_definition_t *definition;
        if (tokens[index].kind != VITTE_TOKEN_KW_MACRO) continue;
        if (index + 1u >= count || tokens[index + 1u].kind != VITTE_TOKEN_IDENTIFIER)
            return false;
        cursor = index + 2u;
        if (vitte_macro_token_is(tokens, count, cursor, VITTE_TOKEN_LEFT_PAREN)) {
            if (!vitte_macro_find_close(tokens, count, cursor, VITTE_TOKEN_LEFT_PAREN,
                                        VITTE_TOKEN_RIGHT_PAREN, &param_close)) return false;
            cursor = param_close + 1u;
        }
        while (cursor < count && tokens[cursor].kind != VITTE_TOKEN_LEFT_BRACE &&
               tokens[cursor].kind != VITTE_TOKEN_EOF) ++cursor;
        if (cursor >= count || tokens[cursor].kind != VITTE_TOKEN_LEFT_BRACE ||
            !vitte_macro_find_close(tokens, count, cursor, VITTE_TOKEN_LEFT_BRACE,
                                    VITTE_TOKEN_RIGHT_BRACE, &body_close)) return false;
        definition = &output->definitions[output->definition_count++];
        definition->begin = index;
        while (definition->begin != 0u &&
               vitte_macro_is_declaration_modifier(tokens[definition->begin - 1u].kind))
            --definition->begin;
        definition->end = body_close + 1u;
        definition->body_begin = cursor + 1u;
        definition->body_end = body_close;
        definition->name = &tokens[index + 1u];
        definition->tokens = tokens;
        if (param_close != SIZE_MAX && param_close != index + 3u) {
            size_t parameter_capacity = param_close - (index + 3u) + 1u;
            size_t *starts = (size_t *)malloc(parameter_capacity * sizeof(*starts));
            size_t *ends = (size_t *)malloc(parameter_capacity * sizeof(*ends));
            size_t groups = 0u, group;
            if (starts == NULL || ends == NULL) {
                free(starts);
                free(ends);
                return false;
            }
            if (!vitte_macro_split_arguments(tokens, index + 3u, param_close,
                    starts, ends, parameter_capacity, &groups)) {
                free(starts);
                free(ends);
                return false;
            }
            definition->parameters = (size_t *)malloc(groups * sizeof(*definition->parameters));
            if (definition->parameters == NULL) {
                free(starts);
                free(ends);
                return false;
            }
            for (group = 0u; group < groups; ++group) {
                size_t token_index = starts[group];
                if (token_index >= ends[group] || tokens[token_index].kind != VITTE_TOKEN_IDENTIFIER) {
                    free(starts);
                    free(ends);
                    return false;
                }
                definition->parameters[definition->parameter_count++] = token_index;
            }
            free(starts);
            free(ends);
        }
        index = body_close;
    }
    /* Reject ambiguous definitions before the first invocation. */
    for (index = 0u; index < output->definition_count; ++index) {
        size_t prior;
        for (prior = 0u; prior < index; ++prior) {
            if (vitte_macro_same_name(output->definitions[index].name,
                                      output->definitions[prior].name)) return false;
        }
    }
    return true;
}

bool
vitte_macro_expand_tokens(const vitte_token_t *tokens, size_t token_count,
                          vitte_macro_expansion_result_t *result)
{
    vitte_macro_output_t output;
    bool ok;
    if (result == NULL) return false;
    memset(result, 0, sizeof(*result));
    if (tokens == NULL || token_count == 0u) {
        result->error = VITTE_MACRO_EXPANSION_ERROR_INVALID_INVOCATION;
        return false;
    }
    memset(&output, 0, sizeof(output));
    output.result = result;
    if (!vitte_macro_collect_definitions(&output, tokens, token_count)) {
        result->error = VITTE_MACRO_EXPANSION_ERROR_INVALID_INVOCATION;
        for (size_t index = 0u; index < token_count; ++index) {
            if (tokens[index].kind == VITTE_TOKEN_KW_MACRO) {
                result->error_span = tokens[index].span;
                break;
            }
        }
        ok = false;
        goto done;
    }
    ok = vitte_macro_expand_range(&output, tokens,
                                 0u, token_count - 1u, 0u);
    if (ok) {
        if (!vitte_macro_append(&output, &tokens[token_count - 1u])) ok = false;
    }
    result->tokens = output.tokens;
    result->token_count = output.count;
done:
    if (!ok) {
        free(output.tokens);
        result->tokens = NULL;
        result->token_count = 0u;
    }
    if (output.definitions != NULL) {
        size_t index;
        for (index = 0u; index < output.definition_count; ++index)
            free(output.definitions[index].parameters);
        free(output.definitions);
    }
    return ok;
}

void
vitte_macro_expansion_result_destroy(vitte_macro_expansion_result_t *result)
{
    if (result == NULL) return;
    free(result->tokens);
    memset(result, 0, sizeof(*result));
}
