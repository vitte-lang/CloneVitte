/* A bounded local-reference pass for the bootstrap semantic model. It tracks
 * direct local/parameter roots and lexical aliases through their last use.
 * Aggregate fields, raw pointers, closures and interprocedural provenance are
 * intentionally outside this pass. Keep those cases unknown rather than
 * turning incomplete provenance into a hard compile error. */
typedef struct vitte_sema_reference_loan {
    vitte_symbol_id_t root;
    vitte_symbol_id_t borrower;
    vitte_scope_id_t borrower_scope;
    vitte_parser_span_t origin;
    size_t start;
    size_t end;
    bool is_mutable;
    bool temporary;
} vitte_sema_reference_loan_t;

typedef struct vitte_sema_reference_state {
    vitte_sema_t *context;
    const bool *dead;
    const vitte_ast_node_t *procedure;
    vitte_sema_reference_loan_t *loans;
    size_t loan_count;
    size_t loan_capacity;
    size_t temporary_end;
    struct vitte_sema_moved_value *moves;
    size_t move_count;
    size_t move_capacity;
    size_t *last_uses;
    size_t last_use_capacity;
} vitte_sema_reference_state_t;

typedef struct vitte_sema_reference_origin {
    vitte_symbol_id_t root;
    vitte_symbol_id_t through_borrower;
    vitte_scope_id_t owner_scope;
    bool is_static;
    bool known;
} vitte_sema_reference_origin_t;

typedef struct vitte_sema_moved_value {
    vitte_symbol_id_t symbol;
    size_t start;
    size_t end;
} vitte_sema_moved_value_t;

static const vitte_sema_type_t *
vitte_sema_reference_type(vitte_sema_reference_state_t *state,
                          const vitte_ast_node_t *node)
{
    const vitte_sema_node_info_t *info;
    if (node == NULL) return NULL;
    info = vitte_sema_node_info_private(state->context, node->id);
    if (info == NULL) return NULL;
    return vitte_sema_type_private(state->context, info->type_id);
}

static bool
vitte_sema_reference_diagnose_symbol(vitte_sema_reference_state_t *state,
                                     vitte_sema_diagnostic_code_t code,
                                     const vitte_ast_node_t *node,
                                     vitte_symbol_id_t symbol_id,
                                     const char *help)
{
    size_t index;
    if (node == NULL) return false;
    for (index = 0u; index < state->context->diagnostic_count; ++index) {
        const vitte_sema_diagnostic_t *existing = &state->context->diagnostics[index];
        if (existing->code == code && existing->node_id == node->id) return true;
    }
    if (!vitte_sema_add_diagnostic(state->context, VITTE_SEMA_DIAGNOSTIC_ERROR,
            code, node->span, node->id, symbol_id,
            VITTE_SEMA_INVALID_TYPE_ID, VITTE_SEMA_INVALID_TYPE_ID)) return false;
    if (help != NULL)
        (void)snprintf(state->context->diagnostics[state->context->diagnostic_count - 1u].help,
            sizeof(state->context->diagnostics[state->context->diagnostic_count - 1u].help),
            "%s", help);
    return true;
}

static bool
vitte_sema_reference_diagnose(vitte_sema_reference_state_t *state,
                              vitte_sema_diagnostic_code_t code,
                              const vitte_ast_node_t *node,
                              const char *help)
{
    return vitte_sema_reference_diagnose_symbol(state, code, node,
        VITTE_SCOPE_INVALID_SYMBOL_ID, help);
}

static const vitte_sema_reference_loan_t *
vitte_sema_reference_find_loan(vitte_sema_reference_state_t *state,
                               vitte_symbol_id_t borrower,
                               const vitte_ast_node_t *at)
{
    size_t index;
    const vitte_sema_node_info_t *at_info = at != NULL
        ? vitte_sema_node_info_private(state->context, at->id) : NULL;
    const vitte_sema_reference_loan_t *found = NULL;
    for (index = 0u; index < state->loan_count; ++index) {
        const vitte_sema_reference_loan_t *loan = &state->loans[index];
        if (loan->borrower != borrower || at == NULL || loan->start > at->span.begin ||
            loan->end < at->span.begin) continue;
        if (loan->borrower_scope != VITTE_SCOPE_INVALID_SCOPE_ID && at_info != NULL &&
            at_info->scope_id != VITTE_SCOPE_INVALID_SCOPE_ID &&
            !vitte_scope_is_ancestor(&state->context->scopes,
                loan->borrower_scope, at_info->scope_id)) continue;
        if (found == NULL || found->start <= loan->start) found = loan;
    }
    return found;
}

static vitte_sema_reference_origin_t
vitte_sema_reference_resolve_place(vitte_sema_reference_state_t *state,
                                   const vitte_ast_node_t *node,
                                   size_t depth)
{
    vitte_sema_reference_origin_t result;
    memset(&result, 0, sizeof(result));
    result.root = VITTE_SCOPE_INVALID_SYMBOL_ID;
    result.through_borrower = VITTE_SCOPE_INVALID_SYMBOL_ID;
    result.owner_scope = VITTE_SCOPE_INVALID_SCOPE_ID;
    if (node == NULL || depth >= 64u) return result;
    if (node->kind == VITTE_AST_NODE_GROUP_EXPR && node->child_count == 1u)
        return vitte_sema_reference_resolve_place(state,
            vitte_sema_node(state->context, node->children[0]), depth + 1u);
    if ((node->kind == VITTE_AST_NODE_IDENTIFIER || node->kind == VITTE_AST_NODE_PATH) &&
        node->id != VITTE_AST_INVALID_ID) {
        const vitte_sema_node_info_t *info = vitte_sema_node_info_private(state->context, node->id);
        const vitte_symbol_t *symbol;
        const vitte_sema_reference_loan_t *loan;
        if (info == NULL || info->symbol_id == VITTE_SCOPE_INVALID_SYMBOL_ID) return result;
        symbol = vitte_scope_get_symbol(&state->context->scopes, info->symbol_id);
        if (symbol == NULL) return result;
        loan = vitte_sema_reference_find_loan(state, info->symbol_id, node);
        if (loan != NULL) {
            result.root = loan->root;
            result.through_borrower = info->symbol_id;
            {
                const vitte_symbol_t *owner = vitte_scope_get_symbol(
                    &state->context->scopes, loan->root);
                result.owner_scope = owner != NULL ? owner->scope_id : VITTE_SCOPE_INVALID_SCOPE_ID;
                result.is_static = owner != NULL && (owner->kind == VITTE_SYMBOL_KIND_STATIC ||
                    owner->kind == VITTE_SYMBOL_KIND_CONST);
            }
            result.known = true;
            return result;
        }
        result.root = info->symbol_id;
        result.owner_scope = symbol->scope_id;
        result.is_static = symbol->kind == VITTE_SYMBOL_KIND_STATIC ||
                           symbol->kind == VITTE_SYMBOL_KIND_CONST;
        result.known = true;
        return result;
    }
    /* A member or indexed place may alias only part of its base aggregate.
     * Treating it as the entire base caused false exclusivity diagnostics in
     * ordinary field access, so this bootstrap pass does not infer that root. */
    if (node->kind == VITTE_AST_NODE_UNARY_EXPR && node->operator_kind == VITTE_TOKEN_STAR &&
        node->child_count == 1u) {
        const vitte_ast_node_t *operand = vitte_sema_node(state->context, node->children[0]);
        const vitte_sema_type_t *type = vitte_sema_reference_type(state, operand);
        if (operand != NULL && operand->kind == VITTE_AST_NODE_UNARY_EXPR &&
            (operand->operator_kind == VITTE_TOKEN_AMP || operand->operator_kind == VITTE_TOKEN_KW_REF) &&
            operand->child_count == 1u)
            return vitte_sema_reference_resolve_place(state,
                vitte_sema_node(state->context, operand->children[0]), depth + 1u);
        if (type != NULL && type->kind == VITTE_SEMA_TYPE_REFERENCE)
            return vitte_sema_reference_resolve_place(state, operand, depth + 1u);
    }
    return result;
}

static vitte_sema_reference_origin_t
vitte_sema_reference_resolve_value(vitte_sema_reference_state_t *state,
                                   const vitte_ast_node_t *node,
                                   size_t depth)
{
    vitte_sema_reference_origin_t unknown;
    memset(&unknown, 0, sizeof(unknown));
    unknown.root = VITTE_SCOPE_INVALID_SYMBOL_ID;
    unknown.through_borrower = VITTE_SCOPE_INVALID_SYMBOL_ID;
    unknown.owner_scope = VITTE_SCOPE_INVALID_SCOPE_ID;
    if (node == NULL || depth >= 64u) return unknown;
    if (node->kind == VITTE_AST_NODE_GROUP_EXPR && node->child_count == 1u)
        return vitte_sema_reference_resolve_value(state,
            vitte_sema_node(state->context, node->children[0]), depth + 1u);
    if (node->kind == VITTE_AST_NODE_UNARY_EXPR && node->operator_kind == VITTE_TOKEN_KW_MOVE &&
        node->child_count == 1u)
        return vitte_sema_reference_resolve_value(state,
            vitte_sema_node(state->context, node->children[0]), depth + 1u);
    if (node->kind == VITTE_AST_NODE_UNARY_EXPR &&
        (node->operator_kind == VITTE_TOKEN_AMP || node->operator_kind == VITTE_TOKEN_KW_REF) &&
        node->child_count == 1u)
        return vitte_sema_reference_resolve_place(state,
            vitte_sema_node(state->context, node->children[0]), depth + 1u);
    {
        const vitte_sema_type_t *type = vitte_sema_reference_type(state, node);
        if (type != NULL && type->kind == VITTE_SEMA_TYPE_REFERENCE)
            return vitte_sema_reference_resolve_place(state, node, depth + 1u);
    }
    return unknown;
}

static const vitte_sema_moved_value_t *
vitte_sema_reference_find_move(vitte_sema_reference_state_t *state,
                               vitte_symbol_id_t symbol,
                               const vitte_ast_node_t *node)
{
    size_t index;
    const vitte_sema_moved_value_t *found = NULL;
    if (node == NULL) return NULL;
    for (index = 0u; index < state->move_count; ++index) {
        const vitte_sema_moved_value_t *move = &state->moves[index];
        if (move->symbol == symbol && move->start <= node->span.begin &&
            move->end > node->span.begin && (found == NULL || found->start < move->start))
            found = move;
    }
    return found;
}

static bool
vitte_sema_reference_check_moved_symbol(vitte_sema_reference_state_t *state,
                                        vitte_symbol_id_t symbol,
                                        const vitte_ast_node_t *node)
{
    if (vitte_sema_reference_find_move(state, symbol, node) == NULL) return false;
    vitte_sema_reference_diagnose_symbol(state, VITTE_SEMA_DIAGNOSTIC_USE_AFTER_MOVE,
        node, symbol, "Initialize this binding again before using it after the move.");
    return true;
}

static bool
vitte_sema_reference_check_use(vitte_sema_reference_state_t *state,
                               const vitte_ast_node_t *node)
{
    const vitte_sema_node_info_t *info;
    vitte_sema_reference_origin_t origin;
    const vitte_sema_moved_value_t *move;
    if (node == NULL || (node->kind != VITTE_AST_NODE_IDENTIFIER && node->kind != VITTE_AST_NODE_PATH))
        return false;
    info = vitte_sema_node_info_private(state->context, node->id);
    if (info == NULL || info->symbol_id == VITTE_SCOPE_INVALID_SYMBOL_ID) return false;
    move = vitte_sema_reference_find_move(state, info->symbol_id, node);
    if (move != NULL) return vitte_sema_reference_check_moved_symbol(state, info->symbol_id, node);
    origin = vitte_sema_reference_resolve_place(state, node, 0u);
    if (origin.known && origin.root != info->symbol_id) {
        move = vitte_sema_reference_find_move(state, origin.root, node);
        if (move != NULL) {
            vitte_sema_reference_diagnose_symbol(state, VITTE_SEMA_DIAGNOSTIC_USE_AFTER_MOVE,
                node, origin.root, "The value referenced here was moved. Reinitialize its owner before using this reference.");
            return true;
        }
    }
    return false;
}

static bool
vitte_sema_reference_direct_symbol(vitte_sema_reference_state_t *state,
                                   const vitte_ast_node_t *node,
                                   vitte_symbol_id_t *symbol_id)
{
    size_t depth = 0u;
    while (node != NULL && node->kind == VITTE_AST_NODE_GROUP_EXPR &&
        node->child_count == 1u && depth++ < 64u)
        node = vitte_sema_node(state->context, node->children[0]);
    if (node == NULL || (node->kind != VITTE_AST_NODE_IDENTIFIER && node->kind != VITTE_AST_NODE_PATH))
        return false;
    {
        const vitte_sema_node_info_t *info = vitte_sema_node_info_private(state->context, node->id);
        if (info == NULL || info->symbol_id == VITTE_SCOPE_INVALID_SYMBOL_ID) return false;
        if (symbol_id != NULL) *symbol_id = info->symbol_id;
    }
    return true;
}

static void
vitte_sema_reference_mark_moved(vitte_sema_reference_state_t *state,
                                vitte_symbol_id_t symbol,
                                const vitte_ast_node_t *move_node)
{
    size_t index;
    if (symbol == VITTE_SCOPE_INVALID_SYMBOL_ID || move_node == NULL ||
        state->move_count >= state->move_capacity) return;
    state->moves[state->move_count].symbol = symbol;
    state->moves[state->move_count].start = move_node->span.end;
    state->moves[state->move_count].end = SIZE_MAX;
    ++state->move_count;
    for (index = 0u; index < state->loan_count; ++index) {
        if (state->loans[index].borrower == symbol &&
            state->loans[index].start <= move_node->span.begin &&
            state->loans[index].end >= move_node->span.begin)
            state->loans[index].end = move_node->span.end;
    }
}

static void
vitte_sema_reference_end_move_on_assignment(vitte_sema_reference_state_t *state,
                                            vitte_symbol_id_t symbol,
                                            size_t position)
{
    size_t index;
    for (index = 0u; index < state->move_count; ++index) {
        vitte_sema_moved_value_t *move = &state->moves[index];
        if (move->symbol == symbol && move->start <= position && move->end > position)
            move->end = position;
    }
}

static bool
vitte_sema_reference_live_at(vitte_sema_reference_state_t *state,
                             const vitte_sema_reference_loan_t *loan,
                             const vitte_ast_node_t *node)
{
    const vitte_sema_node_info_t *info;
    if (loan == NULL || node == NULL || loan->start > node->span.begin ||
        loan->end < node->span.begin) return false;
    info = vitte_sema_node_info_private(state->context, node->id);
    if (loan->borrower_scope != VITTE_SCOPE_INVALID_SCOPE_ID && info != NULL &&
        info->scope_id != VITTE_SCOPE_INVALID_SCOPE_ID &&
        !vitte_scope_is_ancestor(&state->context->scopes,
            loan->borrower_scope, info->scope_id)) return false;
    return true;
}

static void
vitte_sema_reference_check_access(vitte_sema_reference_state_t *state,
                                  const vitte_ast_node_t *node,
                                  bool write)
{
    vitte_sema_reference_origin_t origin =
        vitte_sema_reference_resolve_place(state, node, 0u);
    size_t index;
    if (!origin.known) return;
    for (index = 0u; index < state->loan_count; ++index) {
        const vitte_sema_reference_loan_t *loan = &state->loans[index];
        /* Call-argument borrows are bounded to expression evaluation. Their
         * sibling access order is not fully represented in the bootstrap AST
         * pass, so only let-bound local borrows participate in access checks. */
        if (loan->temporary) continue;
        if (loan->root != origin.root || !vitte_sema_reference_live_at(state, loan, node)) continue;
        if (origin.through_borrower != VITTE_SCOPE_INVALID_SYMBOL_ID &&
            origin.through_borrower == loan->borrower) continue;
        if (loan->is_mutable || write) {
            vitte_sema_reference_diagnose_symbol(state,
                VITTE_SEMA_DIAGNOSTIC_BORROWED_MUTATION, node,
                loan->borrower != VITTE_SCOPE_INVALID_SYMBOL_ID ? loan->borrower : loan->root,
                write ? "End or stop using the live reference before mutating this value."
                      : "This value is exclusively borrowed. Use the mutable reference or wait until it is no longer used.");
            return;
        }
    }
}

static size_t
vitte_sema_reference_last_use(vitte_sema_reference_state_t *state,
                              vitte_symbol_id_t borrower,
                              size_t after)
{
    size_t last = after;
    if (state->last_uses != NULL && borrower < state->last_use_capacity &&
        state->last_uses[borrower] >= after)
        last = state->last_uses[borrower];
    return last;
}

static void
vitte_sema_reference_index_uses(vitte_sema_reference_state_t *state,
                                const vitte_ast_node_t *node,
                                size_t depth)
{
    size_t index;
    if (node == NULL || depth >= 128u || node->id == VITTE_AST_INVALID_ID ||
        node->id > state->context->parser->node_count ||
        (state->dead != NULL && state->dead[node->id])) return;
    if (node->kind == VITTE_AST_NODE_PROC_DECL || node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) return;
    if (node->kind == VITTE_AST_NODE_IDENTIFIER || node->kind == VITTE_AST_NODE_PATH) {
        const vitte_sema_node_info_t *info = vitte_sema_node_info_private(state->context, node->id);
        if (info != NULL && info->symbol_id != VITTE_SCOPE_INVALID_SYMBOL_ID &&
            info->symbol_id < state->last_use_capacity &&
            node->span.end > state->last_uses[info->symbol_id])
            state->last_uses[info->symbol_id] = node->span.end;
    }
    for (index = 0u; index < node->child_count; ++index)
        vitte_sema_reference_index_uses(state,
            vitte_sema_node(state->context, node->children[index]), depth + 1u);
}

static size_t
vitte_sema_reference_count_nodes(vitte_sema_reference_state_t *state,
                                const vitte_ast_node_t *node,
                                size_t depth)
{
    size_t index;
    size_t count = 0u;
    if (node == NULL || depth >= 128u || node->id == VITTE_AST_INVALID_ID ||
        node->id > state->context->parser->node_count ||
        (state->dead != NULL && state->dead[node->id]) ||
        node->kind == VITTE_AST_NODE_PROC_DECL ||
        node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) return 0u;
    count = 1u;
    for (index = 0u; index < node->child_count; ++index) {
        size_t child_count = vitte_sema_reference_count_nodes(state,
            vitte_sema_node(state->context, node->children[index]), depth + 1u);
        if (child_count > SIZE_MAX - count) return state->context->parser->node_count;
        count += child_count;
    }
    return count;
}

static bool
vitte_sema_reference_owner_outlives(vitte_sema_reference_state_t *state,
                                    vitte_sema_reference_origin_t origin,
                                    vitte_scope_id_t borrower_scope)
{
    if (!origin.known) return false;
    if (origin.is_static) return true;
    return origin.owner_scope != VITTE_SCOPE_INVALID_SCOPE_ID &&
        borrower_scope != VITTE_SCOPE_INVALID_SCOPE_ID &&
        vitte_scope_is_ancestor(&state->context->scopes,
            origin.owner_scope, borrower_scope);
}

static size_t
vitte_sema_reference_add_loan(vitte_sema_reference_state_t *state,
                              vitte_sema_reference_origin_t origin,
                              vitte_symbol_id_t borrower,
                              vitte_scope_id_t borrower_scope,
                              const vitte_ast_node_t *site,
                              size_t start, size_t end,
                              bool is_mutable, bool temporary,
                              bool check_conflict)
{
    size_t index;
    if (!origin.known || state->loan_count >= state->loan_capacity) return SIZE_MAX;
    if (check_conflict) {
        for (index = 0u; index < state->loan_count; ++index) {
            const vitte_sema_reference_loan_t *existing = &state->loans[index];
            if (existing->root != origin.root || existing->end < start || existing->start > end ||
                (borrower_scope != VITTE_SCOPE_INVALID_SCOPE_ID &&
                 existing->borrower_scope != VITTE_SCOPE_INVALID_SCOPE_ID &&
                 !vitte_scope_is_ancestor(&state->context->scopes, existing->borrower_scope, borrower_scope) &&
                 !vitte_scope_is_ancestor(&state->context->scopes, borrower_scope, existing->borrower_scope))) continue;
            if (temporary && existing->borrower == origin.through_borrower &&
                origin.through_borrower != VITTE_SCOPE_INVALID_SYMBOL_ID)
                continue;
            if (existing->is_mutable || is_mutable) {
                vitte_sema_reference_diagnose_symbol(state, VITTE_SEMA_DIAGNOSTIC_BORROW_CONFLICT,
                    site, existing->borrower != VITTE_SCOPE_INVALID_SYMBOL_ID ? existing->borrower : existing->root,
                    "End the existing reference before creating an overlapping exclusive reference.");
                break;
            }
        }
    }
    state->loans[state->loan_count].root = origin.root;
    state->loans[state->loan_count].borrower = borrower;
    state->loans[state->loan_count].borrower_scope = borrower_scope;
    state->loans[state->loan_count].origin = site != NULL ? site->span : (vitte_parser_span_t){0};
    state->loans[state->loan_count].start = start;
    state->loans[state->loan_count].end = end;
    state->loans[state->loan_count].is_mutable = is_mutable;
    state->loans[state->loan_count].temporary = temporary;
    return state->loan_count++;
}

static void
vitte_sema_reference_scan(vitte_sema_reference_state_t *state,
                          const vitte_ast_node_t *node, size_t depth);

static void
vitte_sema_reference_scan_move(vitte_sema_reference_state_t *state,
                               const vitte_ast_node_t *node,
                               const vitte_ast_node_t *operand,
                               size_t depth)
{
    vitte_symbol_id_t symbol_id;
    const vitte_sema_type_t *type = vitte_sema_reference_type(state, operand);
    if (vitte_sema_reference_direct_symbol(state, operand, &symbol_id)) {
        const vitte_symbol_t *symbol = vitte_scope_get_symbol(&state->context->scopes, symbol_id);
        size_t index;
        if (vitte_sema_reference_check_use(state, operand)) return;
        if (type != NULL && type->kind == VITTE_SEMA_TYPE_REFERENCE) {
            vitte_sema_reference_mark_moved(state, symbol_id, node);
            return;
        }
        if (symbol == NULL || (symbol->kind != VITTE_SYMBOL_KIND_LOCAL &&
            symbol->kind != VITTE_SYMBOL_KIND_PARAMETER)) {
            vitte_sema_reference_diagnose_symbol(state, VITTE_SEMA_DIAGNOSTIC_MOVE_BORROWED,
                node, symbol_id, "Move a local value or parameter; static and indirect places cannot be moved by the bootstrap.");
            return;
        }
        for (index = 0u; index < state->loan_count; ++index) {
            const vitte_sema_reference_loan_t *loan = &state->loans[index];
            if (loan->root == symbol_id && vitte_sema_reference_live_at(state, loan, node)) {
                vitte_sema_reference_diagnose_symbol(state, VITTE_SEMA_DIAGNOSTIC_MOVE_BORROWED,
                    node, loan->borrower != VITTE_SCOPE_INVALID_SYMBOL_ID ? loan->borrower : symbol_id,
                    "End all references to this value before moving it.");
                break;
            }
        }
        vitte_sema_reference_mark_moved(state, symbol_id, node);
        return;
    }
    {
        vitte_sema_reference_origin_t origin = vitte_sema_reference_resolve_place(state, operand, 0u);
        if (origin.known) {
            (void)vitte_sema_reference_check_moved_symbol(state, origin.root, node);
            vitte_sema_reference_diagnose_symbol(state, VITTE_SEMA_DIAGNOSTIC_MOVE_BORROWED,
                node, origin.root,
                "Moving a field or a value through a reference is not supported yet; move an unborrowed local binding instead.");
        }
    }
    vitte_sema_reference_scan(state, operand, depth + 1u);
}

static void
vitte_sema_reference_scan_address_effects(vitte_sema_reference_state_t *state,
                                          const vitte_ast_node_t *node,
                                          size_t depth)
{
    size_t index;
    if (node == NULL || depth >= 64u) return;
    if (node->kind == VITTE_AST_NODE_INDEX_EXPR && node->child_count > 1u) {
        vitte_sema_reference_scan(state,
            vitte_sema_node(state->context, node->children[1]), depth + 1u);
        return;
    }
    if (node->kind == VITTE_AST_NODE_GROUP_EXPR && node->child_count == 1u) {
        vitte_sema_reference_scan_address_effects(state,
            vitte_sema_node(state->context, node->children[0]), depth + 1u);
        return;
    }
    for (index = 0u; index < node->child_count; ++index) {
        const vitte_ast_node_t *child = vitte_sema_node(state->context, node->children[index]);
        if (child != NULL && child->kind == VITTE_AST_NODE_INDEX_EXPR)
            vitte_sema_reference_scan_address_effects(state, child, depth + 1u);
    }
}

static bool
vitte_sema_reference_local_symbol(vitte_sema_reference_state_t *state,
                                  const vitte_ast_node_t *node,
                                  vitte_symbol_id_t *symbol_id,
                                  vitte_scope_id_t *scope_id)
{
    const vitte_sema_node_info_t *info = node != NULL
        ? vitte_sema_node_info_private(state->context, node->id) : NULL;
    if (info == NULL || info->symbol_id == VITTE_SCOPE_INVALID_SYMBOL_ID) return false;
    if (symbol_id != NULL) *symbol_id = info->symbol_id;
    if (scope_id != NULL) {
        const vitte_symbol_t *symbol = vitte_scope_get_symbol(&state->context->scopes, info->symbol_id);
        *scope_id = symbol != NULL ? symbol->scope_id : info->scope_id;
    }
    return true;
}

static void
vitte_sema_reference_check_return(vitte_sema_reference_state_t *state,
                                  const vitte_ast_node_t *node)
{
    const vitte_sema_node_info_t *procedure_info = vitte_sema_node_info_private(
        state->context, state->procedure->id);
    const vitte_sema_type_t *function_type = procedure_info != NULL
        ? vitte_sema_type_private(state->context, procedure_info->type_id) : NULL;
    const vitte_sema_type_t *return_type = function_type != NULL
        ? vitte_sema_type_private(state->context, function_type->return_type) : NULL;
    const vitte_ast_node_t *value;
    const vitte_ast_node_t *owner_declaration;
    const vitte_sema_type_t *owner_type;
    vitte_sema_reference_origin_t origin;
    const vitte_symbol_t *owner;
    if (return_type == NULL || return_type->kind != VITTE_SEMA_TYPE_REFERENCE || node->child_count == 0u) return;
    value = vitte_sema_node(state->context, node->children[0]);
    origin = vitte_sema_reference_resolve_value(state, value, 0u);
    owner = origin.known ? vitte_scope_get_symbol(&state->context->scopes, origin.root) : NULL;
    owner_declaration = owner != NULL ? (const vitte_ast_node_t *)owner->payload : NULL;
    owner_type = vitte_sema_reference_type(state, owner_declaration);
    if (!origin.known) return;
    if ((!origin.is_static && owner != NULL && owner->kind == VITTE_SYMBOL_KIND_LOCAL) ||
        (!origin.is_static && owner != NULL && owner->kind == VITTE_SYMBOL_KIND_PARAMETER &&
         (owner_type == NULL || owner_type->kind != VITTE_SEMA_TYPE_REFERENCE))) {
        vitte_sema_reference_diagnose(state, VITTE_SEMA_DIAGNOSTIC_REFERENCE_ESCAPE, node,
            "Return a reference to a static value or to data borrowed from a reference parameter.");
    }
}

static void
vitte_sema_reference_scan(vitte_sema_reference_state_t *state,
                          const vitte_ast_node_t *node, size_t depth)
{
    size_t index;
    if (node == NULL || depth >= 128u || node->id == VITTE_AST_INVALID_ID ||
        node->id > state->context->parser->node_count ||
        (state->dead != NULL && state->dead[node->id])) return;
    if (node->kind == VITTE_AST_NODE_PROC_DECL || node->kind == VITTE_AST_NODE_EXTERN_PROC_DECL) return;
    if (node->kind == VITTE_AST_NODE_LET_STMT) {
        const vitte_ast_node_t *binding = node->child_count != 0u
            ? vitte_sema_node(state->context, node->children[0]) : NULL;
        const vitte_ast_node_t *initializer = vitte_sema_initializer(state->context, node);
        vitte_symbol_id_t borrower;
        vitte_scope_id_t borrower_scope;
        const vitte_sema_node_info_t *let_info = vitte_sema_node_info_private(state->context, node->id);
        const vitte_sema_type_t *reference_type = let_info != NULL
            ? vitte_sema_type_private(state->context, let_info->type_id) : NULL;
        if (binding != NULL && binding->kind == VITTE_AST_NODE_PATTERN) binding = NULL;
        if (initializer != NULL) vitte_sema_reference_scan(state, initializer, depth + 1u);
        if (reference_type == NULL || reference_type->kind != VITTE_SEMA_TYPE_REFERENCE ||
            initializer == NULL ||
            !vitte_sema_reference_local_symbol(state, node, &borrower, &borrower_scope)) return;
        {
            vitte_sema_reference_origin_t origin = vitte_sema_reference_resolve_value(state, initializer, 0u);
            size_t last_use = vitte_sema_reference_last_use(state, borrower, node->span.end);
            bool direct_borrow = initializer != NULL && initializer->kind == VITTE_AST_NODE_UNARY_EXPR &&
                (initializer->operator_kind == VITTE_TOKEN_AMP || initializer->operator_kind == VITTE_TOKEN_KW_REF);
            const vitte_ast_node_t *site = initializer != NULL ? initializer : node;
            if (!origin.known) return;
            if (!vitte_sema_reference_owner_outlives(state, origin, borrower_scope)) {
                vitte_sema_reference_diagnose(state, VITTE_SEMA_DIAGNOSTIC_REFERENCE_ESCAPE, site,
                    "The referenced value is declared in a shorter scope. Move the reference into a scope that does not outlive its owner.");
            }
            vitte_sema_reference_add_loan(state, origin, borrower, borrower_scope, site,
                node->span.end, last_use, reference_type->is_mutable, false, !direct_borrow);
        }
        return;
    }
    if (node->kind == VITTE_AST_NODE_RETURN_STMT) {
        vitte_sema_reference_check_return(state, node);
        for (index = 0u; index < node->child_count; ++index)
            vitte_sema_reference_scan(state, vitte_sema_node(state->context, node->children[index]), depth + 1u);
        return;
    }
    if (node->kind == VITTE_AST_NODE_CALL_EXPR) {
        size_t argument;
        size_t saved_end = state->temporary_end;
        state->temporary_end = node->span.end;
        for (argument = 1u; argument < node->child_count; ++argument) {
            const vitte_ast_node_t *left = vitte_sema_node(state->context, node->children[argument]);
            const vitte_sema_type_t *left_type = vitte_sema_reference_type(state, left);
            vitte_sema_reference_origin_t left_origin;
            size_t other;
            if (left_type == NULL || left_type->kind != VITTE_SEMA_TYPE_REFERENCE) continue;
            left_origin = vitte_sema_reference_resolve_value(state, left, 0u);
            if (!left_origin.known) continue;
            for (other = argument + 1u; other < node->child_count; ++other) {
                const vitte_ast_node_t *right = vitte_sema_node(state->context, node->children[other]);
                const vitte_sema_type_t *right_type = vitte_sema_reference_type(state, right);
                vitte_sema_reference_origin_t right_origin;
                if (right_type == NULL || right_type->kind != VITTE_SEMA_TYPE_REFERENCE) continue;
                right_origin = vitte_sema_reference_resolve_value(state, right, 0u);
                if (right_origin.known && right_origin.root == left_origin.root &&
                    (left_type->is_mutable || right_type->is_mutable)) {
                    vitte_sema_reference_diagnose(state, VITTE_SEMA_DIAGNOSTIC_BORROW_CONFLICT, right,
                        "Pass references to distinct values, or end the other exclusive borrow before this call.");
                }
            }
        }
        for (index = 0u; index < node->child_count; ++index)
            vitte_sema_reference_scan(state, vitte_sema_node(state->context, node->children[index]), depth + 1u);
        state->temporary_end = saved_end;
        return;
    }
    if (node->kind == VITTE_AST_NODE_ASSIGN_EXPR && node->child_count >= 2u) {
        const vitte_ast_node_t *left = vitte_sema_node(state->context, node->children[0]);
        const vitte_sema_type_t *target_type = vitte_sema_reference_type(state, left);
        vitte_symbol_id_t target_symbol = VITTE_SCOPE_INVALID_SYMBOL_ID;
        bool direct_target = vitte_sema_reference_direct_symbol(state, left, &target_symbol);
        bool plain_reinitialize = direct_target && node->operator_kind == VITTE_TOKEN_EQUAL;
        bool rebind = left != NULL && target_type != NULL && target_type->kind == VITTE_SEMA_TYPE_REFERENCE &&
            direct_target;
        if (!direct_target) vitte_sema_reference_scan(state, left, depth + 1u);
        else if (!plain_reinitialize) (void)vitte_sema_reference_check_use(state, left);
        if (!rebind) vitte_sema_reference_check_access(state, left, true);
        vitte_sema_reference_scan(state, vitte_sema_node(state->context, node->children[1]), depth + 1u);
        if (plain_reinitialize)
            vitte_sema_reference_end_move_on_assignment(state, target_symbol, node->span.end);
        {
            const vitte_sema_type_t *value_type = vitte_sema_reference_type(state,
                vitte_sema_node(state->context, node->children[1]));
        if (direct_target && target_type != NULL && target_type->kind == VITTE_SEMA_TYPE_REFERENCE &&
                value_type != NULL && value_type->kind == VITTE_SEMA_TYPE_REFERENCE) {
                vitte_sema_reference_origin_t origin = vitte_sema_reference_resolve_value(state,
                    vitte_sema_node(state->context, node->children[1]), 0u);
                vitte_scope_id_t target_scope;
                if (origin.known &&
                    vitte_sema_reference_local_symbol(state, left, &target_symbol, &target_scope)) {
                    size_t loan_index;
                    const vitte_ast_node_t *site = vitte_sema_node(state->context, node->children[1]);
                    if (!vitte_sema_reference_owner_outlives(state, origin, target_scope))
                        vitte_sema_reference_diagnose(state, VITTE_SEMA_DIAGNOSTIC_REFERENCE_ESCAPE, node,
                            "The assigned reference would outlive the value it refers to.");
                    for (loan_index = 0u; loan_index < state->loan_count; ++loan_index)
                        if (state->loans[loan_index].borrower == target_symbol &&
                            state->loans[loan_index].start <= node->span.begin &&
                            state->loans[loan_index].end >= node->span.begin)
                            state->loans[loan_index].end = node->span.begin;
                    vitte_sema_reference_add_loan(state, origin, target_symbol, target_scope,
                        site != NULL ? site : node, node->span.end,
                        vitte_sema_reference_last_use(state, target_symbol, node->span.end),
                        target_type->is_mutable, false, true);
                }
            }
        }
        return;
    }
    if (node->kind == VITTE_AST_NODE_UNARY_EXPR && node->operator_kind == VITTE_TOKEN_KW_MOVE &&
        node->child_count == 1u) {
        vitte_sema_reference_scan_move(state, node,
            vitte_sema_node(state->context, node->children[0]), depth + 1u);
        return;
    }
    if (node->kind == VITTE_AST_NODE_UNARY_EXPR && node->child_count == 1u &&
        (node->operator_kind == VITTE_TOKEN_AMP || node->operator_kind == VITTE_TOKEN_KW_REF)) {
        const vitte_ast_node_t *operand = vitte_sema_node(state->context, node->children[0]);
        vitte_sema_reference_origin_t origin = vitte_sema_reference_resolve_place(state, operand, 0u);
        const vitte_sema_type_t *type = vitte_sema_reference_type(state, node);
        const vitte_sema_node_info_t *info = vitte_sema_node_info_private(state->context, node->id);
        size_t end = state->temporary_end > node->span.end ? state->temporary_end : node->span.end;
        bool call_temporary = state->temporary_end > node->span.end;
        if (origin.known) (void)vitte_sema_reference_check_moved_symbol(state, origin.root, node);
        if (origin.known && type != NULL && type->kind == VITTE_SEMA_TYPE_REFERENCE)
            vitte_sema_reference_add_loan(state, origin, VITTE_SCOPE_INVALID_SYMBOL_ID,
                info != NULL ? info->scope_id : VITTE_SCOPE_INVALID_SCOPE_ID,
                node, node->span.begin, end, type->is_mutable, true, !call_temporary);
        vitte_sema_reference_scan_address_effects(state, operand, depth + 1u);
        return;
    }
    if (node->kind == VITTE_AST_NODE_IDENTIFIER || node->kind == VITTE_AST_NODE_PATH) {
        (void)vitte_sema_reference_check_use(state, node);
        vitte_sema_reference_check_access(state, node, false);
        return;
    }
    for (index = 0u; index < node->child_count; ++index)
        vitte_sema_reference_scan(state, vitte_sema_node(state->context, node->children[index]), depth + 1u);
}

static void
vitte_sema_check_references_proc(vitte_sema_t *context,
                                const vitte_ast_node_t *procedure,
                                const bool *dead)
{
    vitte_sema_reference_state_t state;
    size_t index;
    memset(&state, 0, sizeof(state));
    state.context = context;
    state.dead = dead;
    state.procedure = procedure;
    state.temporary_end = 0u;
    state.loan_capacity = 1u;
    for (index = 0u; index < procedure->child_count; ++index) {
        const vitte_ast_node_t *child = vitte_sema_node(context, procedure->children[index]);
        if (child != NULL && child->kind == VITTE_AST_NODE_BLOCK) {
            size_t body_nodes = vitte_sema_reference_count_nodes(&state, child, 0u);
            if (body_nodes > SIZE_MAX - state.loan_capacity) {
                state.loan_capacity = context->parser->node_count + 1u;
                break;
            }
            state.loan_capacity += body_nodes;
        }
    }
    state.move_capacity = state.loan_capacity;
    state.last_use_capacity = context->scopes.symbol_count + 1u;
    state.loans = (vitte_sema_reference_loan_t *)calloc(state.loan_capacity,
        sizeof(*state.loans));
    state.moves = (vitte_sema_moved_value_t *)calloc(state.move_capacity,
        sizeof(*state.moves));
    state.last_uses = (size_t *)calloc(state.last_use_capacity,
        sizeof(*state.last_uses));
    if (state.loans == NULL || state.moves == NULL || state.last_uses == NULL) {
        free(state.loans);
        free(state.moves);
        free(state.last_uses);
        (void)vitte_sema_fail(context, VITTE_SEMA_ERROR_OUT_OF_MEMORY);
        return;
    }
    for (index = 0u; index < procedure->child_count; ++index) {
        const vitte_ast_node_t *parameter = vitte_sema_node(context, procedure->children[index]);
        const vitte_sema_node_info_t *info;
        const vitte_sema_type_t *type;
        const vitte_symbol_t *symbol;
        vitte_sema_reference_origin_t origin;
        if (parameter == NULL || parameter->kind != VITTE_AST_NODE_PARAMETER) continue;
        info = vitte_sema_node_info_private(context, parameter->id);
        type = info != NULL ? vitte_sema_type_private(context, info->type_id) : NULL;
        symbol = info != NULL ? vitte_scope_get_symbol(&context->scopes, info->symbol_id) : NULL;
        if (type == NULL || type->kind != VITTE_SEMA_TYPE_REFERENCE || symbol == NULL) continue;
        memset(&origin, 0, sizeof(origin));
        origin.root = symbol->id;
        origin.owner_scope = symbol->scope_id;
        origin.known = true;
        (void)vitte_sema_reference_add_loan(&state, origin, symbol->id, symbol->scope_id,
            parameter, procedure->span.begin, procedure->span.end,
            type->is_mutable, false, false);
    }
    for (index = 0u; index < procedure->child_count; ++index) {
        const vitte_ast_node_t *child = vitte_sema_node(context, procedure->children[index]);
        if (child != NULL && child->kind == VITTE_AST_NODE_BLOCK) {
            vitte_sema_reference_index_uses(&state, child, 0u);
            vitte_sema_reference_scan(&state, child, 0u);
        }
    }
    free(state.loans);
    free(state.moves);
    free(state.last_uses);
}
