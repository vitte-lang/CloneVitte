#ifndef VITTE_BOOTSTRAP_CLI_MACRO_EXPANSION_H
#define VITTE_BOOTSTRAP_CLI_MACRO_EXPANSION_H

#include "lexer/lexer.h"

typedef enum vitte_macro_expansion_error {
    VITTE_MACRO_EXPANSION_ERROR_NONE = 0,
    VITTE_MACRO_EXPANSION_ERROR_INVALID_INVOCATION,
    VITTE_MACRO_EXPANSION_ERROR_RECURSION,
    VITTE_MACRO_EXPANSION_ERROR_LIMIT,
    VITTE_MACRO_EXPANSION_ERROR_OUT_OF_MEMORY
} vitte_macro_expansion_error_t;

typedef struct vitte_macro_expansion_result {
    vitte_token_t *tokens;
    size_t token_count;
    vitte_macro_expansion_error_t error;
    vitte_lexer_span_t error_span;
} vitte_macro_expansion_result_t;

bool vitte_macro_expand_tokens(const vitte_token_t *tokens, size_t token_count,
                               vitte_macro_expansion_result_t *result);
void vitte_macro_expansion_result_destroy(vitte_macro_expansion_result_t *result);

#endif
