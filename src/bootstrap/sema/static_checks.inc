#include "references.inc"

/* Static diagnostics run after all declarations are typed, so forward calls
 * and contextual literal conversions see the same complete semantic model. */
static void
vitte_sema_contract_diagnostic(vitte_sema_t *context,
    vitte_sema_diagnostic_code_t code, const vitte_ast_node_t *site,
    const vitte_ast_node_t *clause, vitte_symbol_id_t procedure)
{
    if (vitte_sema_add_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_ERROR,
            code, site->span, site->id, procedure,
            VITTE_SEMA_INVALID_TYPE_ID, VITTE_SEMA_INVALID_TYPE_ID)) {
        vitte_sema_diagnostic_t *diagnostic = &context->diagnostics[context->diagnostic_count - 1u];
        diagnostic->contract_flags = clause->flags;
        diagnostic->contract_span = clause->span;
        diagnostic->contract_symbol_id = procedure;
    }
}

static void
vitte_sema_check_call_contracts(vitte_sema_t *context, const vitte_ast_node_t *call)
{
    const vitte_sema_node_info_t *call_info = vitte_sema_get_node_info(context, call->id);
    const vitte_sema_node_info_t *callee_info;
    const vitte_sema_type_t *callee_type;
    const vitte_symbol_t *symbol;
    const vitte_ast_node_t *procedure;
    vitte_sema_eval_binding_t bindings[128];
    size_t binding_count = 0u, argument_index = 1u, index;
    if (call->child_count == 0u || call_info == NULL || call_info->type_id == context->type_error) return;
    callee_info = vitte_sema_get_node_info(context, call->children[0]);
    callee_type = callee_info != NULL ? vitte_sema_type_private(context, callee_info->type_id) : NULL;
    symbol = callee_type != NULL ? vitte_scope_get_symbol(&context->scopes, callee_type->symbol_id) : NULL;
    procedure = symbol != NULL ? (const vitte_ast_node_t *)symbol->payload : NULL;
    if (procedure == NULL || (procedure->kind != VITTE_AST_NODE_PROC_DECL &&
        procedure->kind != VITTE_AST_NODE_EXTERN_PROC_DECL)) return;
    for (index = 0u; index < procedure->child_count; ++index) {
        const vitte_ast_node_t *parameter = vitte_sema_node(context, procedure->children[index]);
        if (parameter == NULL || parameter->kind != VITTE_AST_NODE_PARAMETER) continue;
        if (binding_count >= 128u || argument_index >= call->child_count || parameter->child_count == 0u) return;
        bindings[binding_count].name = vitte_sema_node_token(context,
            vitte_sema_node(context, parameter->children[0]));
        bindings[binding_count].value = vitte_sema_known_value(context,
            vitte_sema_node(context, call->children[argument_index++]));
        ++binding_count;
    }
    if (argument_index != call->child_count) return;
    for (index = 0u; index < procedure->child_count; ++index) {
        const vitte_ast_node_t *clause = vitte_sema_node(context, procedure->children[index]);
        vitte_sema_eval_t eval;
        vitte_constant_t value, declaration_value;
        if (clause == NULL || (clause->flags & VITTE_AST_FLAG_CONTRACT_REQUIRES) == 0u) continue;
        /* A declaration-level contradiction already explains every call. */
        declaration_value = vitte_sema_known_value(context, clause);
        if (declaration_value.kind == VITTE_CONSTANT_KIND_BOOL && !declaration_value.as.boolean) continue;
        memset(&eval, 0, sizeof(eval));
        eval.sema = context;
        eval.bindings = bindings;
        eval.binding_count = binding_count;
        value = vitte_sema_eval_node(&eval, clause);
        if (value.kind == VITTE_CONSTANT_KIND_BOOL && !value.as.boolean)
            vitte_sema_contract_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_CONTRACT_PRECONDITION,
                call, clause, symbol->id);
    }
}

static void
vitte_sema_check_return_contracts(vitte_sema_t *context,
    const vitte_ast_node_t *node, const vitte_ast_node_t *procedure,
    vitte_symbol_id_t symbol, const bool *dead, size_t depth)
{
    size_t index;
    if (node == NULL || depth >= 128u || node->id == VITTE_AST_INVALID_ID ||
        node->id > context->parser->node_count || dead[node->id]) return;
    if (node->kind == VITTE_AST_NODE_PROC_DECL || node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) return;
    if (node->kind == VITTE_AST_NODE_MATCH_ARM && node->child_count >= 2u) {
        const vitte_ast_node_t *body = vitte_sema_node(context, node->children[node->child_count - 1u]);
        if (body != NULL && (body->kind == VITTE_AST_NODE_PATH ||
            body->kind == VITTE_AST_NODE_INTEGER_LITERAL || body->kind == VITTE_AST_NODE_BOOL_LITERAL ||
            body->kind == VITTE_AST_NODE_FLOAT_LITERAL || body->kind == VITTE_AST_NODE_STRING_LITERAL ||
            body->kind == VITTE_AST_NODE_CHARACTER_LITERAL || body->kind == VITTE_AST_NODE_NULL_LITERAL ||
            body->kind == VITTE_AST_NODE_UNARY_EXPR ||
            body->kind == VITTE_AST_NODE_BINARY_EXPR || body->kind == VITTE_AST_NODE_CALL_EXPR ||
            body->kind == VITTE_AST_NODE_GROUP_EXPR)) {
            vitte_ast_node_t implicit_return = *body;
            vitte_ast_node_id_t value = body->id;
            implicit_return.kind = VITTE_AST_NODE_RETURN_STMT;
            implicit_return.children = &value;
            implicit_return.child_count = 1u;
            vitte_sema_check_return_contracts(context, &implicit_return, procedure, symbol, dead, depth + 1u);
            return;
        }
    }
    if (node->kind == VITTE_AST_NODE_RETURN_STMT && node->child_count == 1u) {
        vitte_constant_t result = vitte_sema_known_value(context,
            vitte_sema_node(context, node->children[0]));
        if (result.kind == VITTE_CONSTANT_KIND_INVALID) return;
        for (index = 0u; index < procedure->child_count; ++index) {
            const vitte_ast_node_t *clause = vitte_sema_node(context, procedure->children[index]);
            vitte_sema_eval_t eval;
            vitte_constant_t value, declaration_value;
            if (clause == NULL || (clause->flags & VITTE_AST_FLAG_CONTRACT_ENSURES) == 0u) continue;
            declaration_value = vitte_sema_known_value(context, clause);
            if (declaration_value.kind == VITTE_CONSTANT_KIND_BOOL && !declaration_value.as.boolean) continue;
            memset(&eval, 0, sizeof(eval));
            eval.sema = context;
            eval.result = &result;
            value = vitte_sema_eval_node(&eval, clause);
            if (value.kind == VITTE_CONSTANT_KIND_BOOL && !value.as.boolean)
                vitte_sema_contract_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_CONTRACT_POSTCONDITION,
                    node, clause, symbol);
        }
        return;
    }
    for (index = 0u; index < node->child_count; ++index)
        vitte_sema_check_return_contracts(context, vitte_sema_node(context, node->children[index]),
            procedure, symbol, dead, depth + 1u);
}

static void
vitte_sema_check_contract_effects(vitte_sema_t *context, const vitte_ast_node_t *node,
    const vitte_ast_node_t *clause, vitte_symbol_id_t symbol, size_t depth)
{
    size_t index;
    if (node == NULL || depth >= 128u) return;
    if (node->kind == VITTE_AST_NODE_ASSIGN_EXPR || node->kind == VITTE_AST_NODE_AWAIT_EXPR ||
        node->kind == VITTE_AST_NODE_TRY_EXPR) {
        vitte_sema_contract_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_CONTRACT_EFFECT, node, clause, symbol);
        return;
    }
    for (index = 0u; index < node->child_count; ++index)
        vitte_sema_check_contract_effects(context, vitte_sema_node(context, node->children[index]),
            clause, symbol, depth + 1u);
}

static void
vitte_sema_check_match(vitte_sema_t *context, const vitte_ast_node_t *node)
{
    const vitte_sema_node_info_t *subject;
    bool has_true = false, has_false = false, covered = false;
    size_t index;
    if (node->child_count == 0u) return;
    subject = vitte_sema_get_node_info(context, node->children[0]);
    if (subject == NULL || subject->type_id == context->type_error) return;
    for (index = 1u; index < node->child_count; ++index) {
        const vitte_ast_node_t *arm = vitte_sema_node(context, node->children[index]);
        const vitte_ast_node_t *pattern;
        const vitte_token_t *token;
        bool duplicate = covered;
        bool false_guard = false;
        size_t covering_line = 0u;
        bool unguarded;
        if (arm == NULL || arm->child_count < 2u) continue;
        pattern = vitte_sema_node(context, arm->children[0]);
        token = vitte_sema_node_token(context, pattern);
        if (pattern != NULL) {
            const vitte_sema_node_info_t *pattern_info = vitte_sema_get_node_info(context, pattern->id);
            if (pattern_info != NULL && pattern_info->type_id == context->type_error) return;
        }
        unguarded = arm->child_count == 2u;
        if (token != NULL && token->kind == VITTE_TOKEN_KW_TRUE) {
            duplicate |= has_true;
            has_true |= unguarded;
        } else if (token != NULL && token->kind == VITTE_TOKEN_KW_FALSE) {
            duplicate |= has_false;
            has_false |= unguarded;
        } else if (token != NULL && token->length == 1u && token->lexeme[0] == '_') {
            covered |= unguarded;
        }
        /* Only an earlier unguarded arm proves that a value is covered.
         * Compare evaluated integers, so alternate radix spellings agree. */
        if (!duplicate && pattern != NULL) {
            size_t previous;
            vitte_constant_t value = vitte_sema_known_value(context, pattern);
            for (previous = 1u; previous < index; ++previous) {
                const vitte_ast_node_t *earlier = vitte_sema_node(context, node->children[previous]);
                const vitte_ast_node_t *earlier_pattern;
                const vitte_token_t *earlier_token;
                vitte_constant_t earlier_value;
                vitte_constant_result_t equal;
                if (earlier == NULL || earlier->child_count != 2u) continue;
                earlier_pattern = vitte_sema_node(context, earlier->children[0]);
                if (earlier_pattern == NULL) continue;
                earlier_value = vitte_sema_known_value(context, earlier_pattern);
                if (value.kind != VITTE_CONSTANT_KIND_INVALID &&
                    earlier_value.kind == value.kind) {
                    equal = vitte_constant_fold_binary(VITTE_CONSTANT_BINARY_EQUAL, &value, &earlier_value);
                    if (equal.status == VITTE_CONSTANT_FOLD_SUCCESS &&
                        equal.value.kind == VITTE_CONSTANT_KIND_BOOL && equal.value.as.boolean)
                        duplicate = true;
                }
                /* Exact text equality is sufficient for string and character
                 * literals; distinct escape spellings remain conservative. */
                earlier_token = vitte_sema_node_token(context, earlier_pattern);
                if (token != NULL && earlier_token != NULL &&
                    (pattern->kind == VITTE_AST_NODE_STRING_LITERAL ||
                     pattern->kind == VITTE_AST_NODE_CHARACTER_LITERAL) &&
                    earlier_pattern->kind == pattern->kind && token->length == earlier_token->length &&
                    memcmp(token->lexeme, earlier_token->lexeme, token->length) == 0)
                    duplicate = true;
                if (duplicate) {
                    covering_line = earlier_pattern->span.line;
                    break;
                }
            }
        }
        if (arm->child_count == 3u) {
            vitte_constant_t guard = vitte_sema_known_value(context,
                vitte_sema_node(context, arm->children[1]));
            if (guard.kind == VITTE_CONSTANT_KIND_BOOL && !guard.as.boolean) {
                duplicate = true;
                false_guard = true;
            }
        }
        if (duplicate && pattern != NULL &&
            vitte_sema_add_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_ERROR,
                VITTE_SEMA_DIAGNOSTIC_MATCH_UNREACHABLE, pattern->span, pattern->id,
                VITTE_SCOPE_INVALID_SYMBOL_ID, VITTE_SEMA_INVALID_TYPE_ID, VITTE_SEMA_INVALID_TYPE_ID)) {
            char *help = context->diagnostics[context->diagnostic_count - 1u].help;
            if (false_guard)
                (void)snprintf(help, 512u,
                    "This arm's guard is always false. Correct the guard or remove the arm.");
            else if (covering_line != 0u)
                (void)snprintf(help, 512u,
                    "An unguarded arm on line %zu already covers this value. Remove the duplicate or use a distinct pattern.",
                    covering_line);
        }
        if (subject->type_id == context->type_bool && has_true && has_false) covered = true;
    }
    if (subject->type_id == context->type_bool && !covered) {
        if (vitte_sema_add_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_ERROR,
                VITTE_SEMA_DIAGNOSTIC_MATCH_NON_EXHAUSTIVE, node->span, node->id,
                VITTE_SCOPE_INVALID_SYMBOL_ID, VITTE_SEMA_INVALID_TYPE_ID, VITTE_SEMA_INVALID_TYPE_ID)) {
            (void)snprintf(context->diagnostics[context->diagnostic_count - 1u].help, 512u,
                "Cover both `true` and `false`, or add an unguarded `_` arm. A guarded arm does not cover every value.");
        }
    }
}

static void
vitte_sema_mark_dead(const vitte_sema_t *context, const vitte_ast_node_t *node,
                     bool inherited, bool *dead, size_t depth)
{
    size_t index;
    bool terminated = false;
    vitte_constant_t condition = vitte_constant_make_invalid();
    if (node == NULL || depth >= 128u || node->id == VITTE_AST_INVALID_ID ||
        node->id > context->parser->node_count) return;
    dead[node->id] = inherited;
    if (node->child_count != 0u &&
        (node->kind == VITTE_AST_NODE_BINARY_EXPR || node->kind == VITTE_AST_NODE_IF_STMT ||
         node->kind == VITTE_AST_NODE_IF_EXPR || node->kind == VITTE_AST_NODE_WHILE_STMT))
        condition = vitte_sema_known_value(context, vitte_sema_node(context, node->children[0]));
    for (index = 0u; index < node->child_count; ++index) {
        const vitte_ast_node_t *child = vitte_sema_node(context, node->children[index]);
        bool child_dead = inherited || terminated;
        if (index != 0u && condition.kind == VITTE_CONSTANT_KIND_BOOL) {
            if (node->kind == VITTE_AST_NODE_BINARY_EXPR) {
                vitte_constant_binary_operator_t op = vitte_sema_eval_binary_operator(node->operator_kind);
                child_dead |= (op == VITTE_CONSTANT_BINARY_LOGICAL_AND && !condition.as.boolean) ||
                              (op == VITTE_CONSTANT_BINARY_LOGICAL_OR && condition.as.boolean);
            } else if (node->kind == VITTE_AST_NODE_IF_STMT || node->kind == VITTE_AST_NODE_IF_EXPR) {
                child_dead |= index == 1u ? !condition.as.boolean : condition.as.boolean;
            } else if (node->kind == VITTE_AST_NODE_WHILE_STMT) child_dead |= !condition.as.boolean;
        }
        vitte_sema_mark_dead(context, child, child_dead, dead, depth + 1u);
        if (node->kind == VITTE_AST_NODE_BLOCK &&
            vitte_sema_exit_flow(context, child, 0u) == VITTE_SEMA_EXIT_TERMINATED) terminated = true;
    }
}

static void
vitte_sema_check_static_program(vitte_sema_t *context)
{
    size_t index;
    bool *dead = calloc(context->parser->node_count + 1u, sizeof(*dead));
    bool *minimum = calloc(context->parser->node_count + 1u, sizeof(*minimum));
    if (dead == NULL || minimum == NULL) {
        free(dead);
        free(minimum);
        (void)vitte_sema_fail(context, VITTE_SEMA_ERROR_OUT_OF_MEMORY);
        return;
    }
    /* Unvisited nodes beyond the traversal budget remain excluded. */
    for (index = 0u; index <= context->parser->node_count; ++index) dead[index] = true;
    vitte_sema_mark_dead(context, vitte_sema_node(context, vitte_parser_root(context->parser)), false, dead, 0u);
    for (index = 0u; index < context->parser->node_count; ++index) {
        const vitte_ast_node_t *node = &context->parser->nodes[index];
        size_t depth = 0u;
        uint64_t magnitude;
        if (node->kind != VITTE_AST_NODE_UNARY_EXPR || node->operator_kind != VITTE_TOKEN_MINUS ||
            node->child_count != 1u) continue;
        node = vitte_sema_node(context, node->children[0]);
        while (node != NULL && node->kind == VITTE_AST_NODE_GROUP_EXPR && node->child_count == 1u && depth++ < 128u)
            node = vitte_sema_node(context, node->children[0]);
        if (node != NULL && node->id <= context->parser->node_count && vitte_sema_parse_unsigned_integer_literal(context, node, &magnitude) &&
            magnitude == (uint64_t)INT64_MAX + UINT64_C(1)) minimum[node->id] = true;
    }
    for (index = 0u; index < context->parser->node_count; ++index) {
        const vitte_ast_node_t *node = &context->parser->nodes[index];
        const vitte_sema_node_info_t *info = vitte_sema_get_node_info(context, node->id);
        if (context->diagnostic_count >= context->max_diagnostics) break;
        if (node->id == VITTE_AST_INVALID_ID || node->id > context->parser->node_count || dead[node->id]) continue;
        if (node->kind == VITTE_AST_NODE_INTEGER_LITERAL && info != NULL &&
            info->type_id == context->type_i64 && !minimum[node->id]) {
            uint64_t magnitude;
            if (vitte_sema_parse_unsigned_integer_literal(context, node, &magnitude) && magnitude > (uint64_t)INT64_MAX) {
                if (vitte_sema_add_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_ERROR,
                        VITTE_SEMA_DIAGNOSTIC_CONSTANT_OVERFLOW, node->span, node->id,
                        VITTE_SCOPE_INVALID_SYMBOL_ID, VITTE_SEMA_INVALID_TYPE_ID, VITTE_SEMA_INVALID_TYPE_ID)) {
                    (void)snprintf(context->diagnostics[context->diagnostic_count - 1u].help, 512u,
                        "This integer literal does not fit in i64. Use a representable value or an explicit u64 context.");
                }
            }
        } else if (node->kind == VITTE_AST_NODE_BINARY_EXPR || node->kind == VITTE_AST_NODE_UNARY_EXPR) {
            vitte_sema_eval_t eval;
            memset(&eval, 0, sizeof(eval));
            eval.sema = context;
            (void)vitte_sema_eval_node(&eval, node);
            if (eval.failure_node == node->id) {
                vitte_sema_diagnostic_code_t code =
                    eval.error == VITTE_CONSTANT_ERROR_DIVISION_BY_ZERO ||
                    eval.error == VITTE_CONSTANT_ERROR_REMAINDER_BY_ZERO
                        ? VITTE_SEMA_DIAGNOSTIC_CONSTANT_DIVIDE_BY_ZERO
                        : eval.error == VITTE_CONSTANT_ERROR_INVALID_SHIFT
                            ? VITTE_SEMA_DIAGNOSTIC_INVALID_BINARY_OPERANDS
                            : VITTE_SEMA_DIAGNOSTIC_CONSTANT_OVERFLOW;
                if (vitte_sema_add_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_ERROR,
                        code, node->span, node->id, VITTE_SCOPE_INVALID_SYMBOL_ID,
                        VITTE_SEMA_INVALID_TYPE_ID, VITTE_SEMA_INVALID_TYPE_ID) &&
                    eval.error == VITTE_CONSTANT_ERROR_INVALID_SHIFT) {
                    (void)snprintf(context->diagnostics[context->diagnostic_count - 1u].help, 512u,
                        "Use a shift count from 0 through 63 for a 64-bit integer.");
                }
            }
        } else if (node->kind == VITTE_AST_NODE_INDEX_EXPR && node->child_count == 2u &&
                   info != NULL && info->type_id != context->type_error) {
            const vitte_sema_node_info_t *base = vitte_sema_get_node_info(context, node->children[0]);
            const vitte_sema_type_t *type = base != NULL ? vitte_sema_type_private(context, base->type_id) : NULL;
            vitte_constant_t subscript = vitte_sema_known_value(context,
                vitte_sema_node(context, node->children[1]));
            bool invalid = subscript.kind == VITTE_CONSTANT_KIND_I64 && subscript.as.i64 < 0;
            uint64_t offset = subscript.kind == VITTE_CONSTANT_KIND_U64
                ? subscript.as.u64 : subscript.kind == VITTE_CONSTANT_KIND_I64 && subscript.as.i64 >= 0
                    ? (uint64_t)subscript.as.i64 : 0u;
            bool known = subscript.kind == VITTE_CONSTANT_KIND_I64 || subscript.kind == VITTE_CONSTANT_KIND_U64;
            /* Zero also represents an unresolved generic length in the
             * current type model; do not invent a bound for that case. */
            if (type != NULL && type->kind == VITTE_SEMA_TYPE_ARRAY && type->array_length != 0u && known &&
                (invalid || offset >= (uint64_t)type->array_length)) {
                if (vitte_sema_add_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_ERROR,
                        VITTE_SEMA_DIAGNOSTIC_INDEX_OUT_OF_BOUNDS, node->span, node->id,
                        VITTE_SCOPE_INVALID_SYMBOL_ID, VITTE_SEMA_INVALID_TYPE_ID, VITTE_SEMA_INVALID_TYPE_ID)) {
                    (void)snprintf(context->diagnostics[context->diagnostic_count - 1u].help, 512u,
                        "This array has %zu element(s). Use a non-negative index smaller than %zu.",
                        type->array_length, type->array_length);
                }
            }
        } else if (node->kind == VITTE_AST_NODE_CONST_DECL) {
            vitte_sema_eval_t eval;
            memset(&eval, 0, sizeof(eval));
            eval.sema = context;
            eval.dependencies_only = true;
            (void)vitte_sema_eval_node(&eval, vitte_sema_initializer(context, node));
            if (eval.cycle_node != VITTE_AST_INVALID_ID)
                (void)vitte_sema_add_diagnostic(context, VITTE_SEMA_DIAGNOSTIC_ERROR,
                    VITTE_SEMA_DIAGNOSTIC_CONSTANT_CYCLE, node->span, node->id,
                    info != NULL ? info->symbol_id : VITTE_SCOPE_INVALID_SYMBOL_ID,
                    VITTE_SEMA_INVALID_TYPE_ID, VITTE_SEMA_INVALID_TYPE_ID);
        } else if (node->kind == VITTE_AST_NODE_MATCH_STMT) {
            vitte_sema_check_match(context, node);
        } else if (node->kind == VITTE_AST_NODE_CALL_EXPR) {
            vitte_sema_check_call_contracts(context, node);
        } else if (node->kind == VITTE_AST_NODE_PROC_DECL || node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) {
            size_t child_index;
            vitte_symbol_id_t symbol = info != NULL ? info->symbol_id : VITTE_SCOPE_INVALID_SYMBOL_ID;
            vitte_sema_check_references_proc(context, node, dead);
            for (child_index = 0u; child_index < node->child_count; ++child_index) {
                const vitte_ast_node_t *child = vitte_sema_node(context, node->children[child_index]);
                vitte_constant_t value;
                if (child == NULL) continue;
                if ((child->flags & (VITTE_AST_FLAG_CONTRACT_REQUIRES | VITTE_AST_FLAG_CONTRACT_ENSURES)) != 0u) {
                    vitte_sema_check_contract_effects(context, child, child, symbol, 0u);
                    value = vitte_sema_known_value(context, child);
                    if (value.kind == VITTE_CONSTANT_KIND_BOOL && !value.as.boolean)
                        vitte_sema_contract_diagnostic(context,
                            (child->flags & VITTE_AST_FLAG_CONTRACT_ENSURES) != 0u
                                ? VITTE_SEMA_DIAGNOSTIC_CONTRACT_POSTCONDITION
                                : VITTE_SEMA_DIAGNOSTIC_CONTRACT_CONTRADICTION,
                            child, child, symbol);
                } else if (child->kind == VITTE_AST_NODE_BLOCK) {
                    vitte_sema_check_return_contracts(context, child, node, symbol, dead, 0u);
                }
            }
        }
    }
    free(dead);
    free(minimum);
}
