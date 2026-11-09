/* Bounded AST evaluation for bootstrap diagnostics. Unknown values stay unknown;
 * this evaluator never executes user procedures or reads mutable bindings. */
typedef struct vitte_sema_eval_binding {
    const vitte_token_t *name;
    vitte_constant_t value;
} vitte_sema_eval_binding_t;

typedef struct vitte_sema_eval {
    const vitte_sema_t *sema;
    const vitte_sema_eval_binding_t *bindings;
    size_t binding_count;
    bool dependencies_only;
    const vitte_constant_t *result;
    size_t steps;
    vitte_ast_node_id_t failure_node;
    vitte_ast_node_id_t cycle_node;
    vitte_constant_error_t error;
    vitte_ast_node_id_t active[128];
    size_t active_count;
} vitte_sema_eval_t;

static vitte_constant_t
vitte_sema_eval_node(vitte_sema_eval_t *eval,
                     const vitte_ast_node_t *node);

static const vitte_ast_node_t *
vitte_sema_initializer(const vitte_sema_t *context,
                       const vitte_ast_node_t *declaration)
{
    size_t index;
    if (declaration == NULL) return NULL;
    for (index = 1u; index < declaration->child_count; ++index) {
        const vitte_ast_node_t *child = vitte_sema_node(context, declaration->children[index]);
        if (child == NULL) continue;
        switch (child->kind) {
            case VITTE_AST_NODE_TYPE_EXPR:
            case VITTE_AST_NODE_POINTER_TYPE:
            case VITTE_AST_NODE_REFERENCE_TYPE:
            case VITTE_AST_NODE_OPTIONAL_TYPE:
            case VITTE_AST_NODE_ARRAY_TYPE:
                continue;
            default:
                return child;
        }
    }
    return NULL;
}

static vitte_constant_binary_operator_t
vitte_sema_eval_binary_operator(vitte_token_kind_t op)
{
    switch (op) {
        case VITTE_TOKEN_PLUS: return VITTE_CONSTANT_BINARY_ADD;
        case VITTE_TOKEN_MINUS: return VITTE_CONSTANT_BINARY_SUBTRACT;
        case VITTE_TOKEN_STAR: return VITTE_CONSTANT_BINARY_MULTIPLY;
        case VITTE_TOKEN_SLASH: return VITTE_CONSTANT_BINARY_DIVIDE;
        case VITTE_TOKEN_PERCENT: return VITTE_CONSTANT_BINARY_REMAINDER;
        case VITTE_TOKEN_AMP: return VITTE_CONSTANT_BINARY_BIT_AND;
        case VITTE_TOKEN_PIPE: return VITTE_CONSTANT_BINARY_BIT_OR;
        case VITTE_TOKEN_CARET: return VITTE_CONSTANT_BINARY_BIT_XOR;
        case VITTE_TOKEN_SHIFT_LEFT: return VITTE_CONSTANT_BINARY_SHIFT_LEFT;
        case VITTE_TOKEN_SHIFT_RIGHT: return VITTE_CONSTANT_BINARY_SHIFT_RIGHT;
        case VITTE_TOKEN_AMP_AMP:
        case VITTE_TOKEN_KW_AND: return VITTE_CONSTANT_BINARY_LOGICAL_AND;
        case VITTE_TOKEN_PIPE_PIPE:
        case VITTE_TOKEN_KW_OR: return VITTE_CONSTANT_BINARY_LOGICAL_OR;
        case VITTE_TOKEN_EQUAL_EQUAL: return VITTE_CONSTANT_BINARY_EQUAL;
        case VITTE_TOKEN_BANG_EQUAL: return VITTE_CONSTANT_BINARY_NOT_EQUAL;
        case VITTE_TOKEN_LESS: return VITTE_CONSTANT_BINARY_LESS;
        case VITTE_TOKEN_LESS_EQUAL: return VITTE_CONSTANT_BINARY_LESS_EQUAL;
        case VITTE_TOKEN_GREATER: return VITTE_CONSTANT_BINARY_GREATER;
        case VITTE_TOKEN_GREATER_EQUAL: return VITTE_CONSTANT_BINARY_GREATER_EQUAL;
        default: return VITTE_CONSTANT_BINARY_INVALID;
    }
}

static vitte_constant_t
vitte_sema_eval_fold(vitte_sema_eval_t *eval, const vitte_ast_node_t *node,
                     vitte_constant_result_t folded)
{
    if (folded.status == VITTE_CONSTANT_FOLD_SUCCESS) return folded.value;
    if (folded.status == VITTE_CONSTANT_FOLD_ERROR &&
        eval->error == VITTE_CONSTANT_ERROR_NONE &&
        (folded.error == VITTE_CONSTANT_ERROR_OVERFLOW ||
         folded.error == VITTE_CONSTANT_ERROR_UNDERFLOW ||
         folded.error == VITTE_CONSTANT_ERROR_INVALID_SHIFT ||
         folded.error == VITTE_CONSTANT_ERROR_DIVISION_BY_ZERO ||
         folded.error == VITTE_CONSTANT_ERROR_REMAINDER_BY_ZERO)) {
        eval->error = folded.error;
        eval->failure_node = node->id;
    }
    return vitte_constant_make_invalid();
}

static vitte_constant_t
vitte_sema_eval_node_impl(vitte_sema_eval_t *eval, const vitte_ast_node_t *node)
{
    const vitte_sema_t *context = eval->sema;
    const vitte_token_t *token = vitte_sema_node_token(context, node);
    const vitte_sema_node_info_t *info = vitte_sema_get_node_info(context, node->id);
    vitte_constant_t unknown = vitte_constant_make_invalid();
    size_t index;
    if (info != NULL && info->type_id == context->type_error && !eval->dependencies_only) return unknown;
    switch (node->kind) {
        case VITTE_AST_NODE_BOOL_LITERAL:
            return token != NULL ? vitte_constant_make_bool(token->kind == VITTE_TOKEN_KW_TRUE) : unknown;
        case VITTE_AST_NODE_INTEGER_LITERAL: {
            uint64_t value;
            if (!vitte_sema_parse_unsigned_integer_literal(context, node, &value)) return unknown;
            if (info != NULL && (info->type_id == context->type_u64 || info->type_id == context->type_u8))
                return vitte_constant_make_u64(value);
            if (value <= (uint64_t)INT64_MAX) return vitte_constant_make_i64((int64_t)value);
            return unknown;
        }
        case VITTE_AST_NODE_NULL_LITERAL:
            return vitte_constant_make_null();
        case VITTE_AST_NODE_GROUP_EXPR:
            return node->child_count == 1u
                ? vitte_sema_eval_node(eval, vitte_sema_node(context, node->children[0])) : unknown;
        case VITTE_AST_NODE_IDENTIFIER:
        case VITTE_AST_NODE_PATH: {
            const vitte_symbol_t *symbol;
            if ((node->kind == VITTE_AST_NODE_IDENTIFIER || node->child_count == 1u) && token != NULL) {
                if (eval->result != NULL && token->length == 6u &&
                    memcmp(token->lexeme, "result", 6u) == 0) return *eval->result;
                for (index = 0u; index < eval->binding_count; ++index) {
                    const vitte_token_t *name = eval->bindings[index].name;
                    if (name != NULL && name->length == token->length &&
                        memcmp(name->lexeme, token->lexeme, token->length) == 0)
                        return eval->bindings[index].value;
                }
            }
            symbol = info != NULL ? vitte_scope_get_symbol(&context->scopes, info->symbol_id) : NULL;
            if (symbol == NULL || (symbol->flags & VITTE_SYMBOL_FLAG_MUTABLE) != 0u ||
                (symbol->kind != VITTE_SYMBOL_KIND_CONST && symbol->kind != VITTE_SYMBOL_KIND_LOCAL))
                return unknown;
            return vitte_sema_eval_node(eval, vitte_sema_initializer(context,
                (const vitte_ast_node_t *)symbol->payload));
        }
        case VITTE_AST_NODE_UNARY_EXPR: {
            vitte_constant_t operand;
            vitte_constant_unary_operator_t op;
            const vitte_ast_node_t *child;
            uint64_t magnitude;
            if (node->child_count != 1u) return unknown;
            child = vitte_sema_node(context, node->children[0]);
            /* The signed minimum is spelled using a positive magnitude that
             * does not fit in i64 before applying its unary minus. */
            if (node->operator_kind == VITTE_TOKEN_MINUS && child != NULL &&
                child->kind == VITTE_AST_NODE_INTEGER_LITERAL &&
                vitte_sema_parse_unsigned_integer_literal(context, child, &magnitude) &&
                magnitude == (uint64_t)INT64_MAX + UINT64_C(1))
                return vitte_constant_make_i64(INT64_MIN);
            switch (node->operator_kind) {
                case VITTE_TOKEN_PLUS: op = VITTE_CONSTANT_UNARY_PLUS; break;
                case VITTE_TOKEN_MINUS: op = VITTE_CONSTANT_UNARY_NEGATE; break;
                case VITTE_TOKEN_BANG:
                case VITTE_TOKEN_KW_NOT: op = VITTE_CONSTANT_UNARY_LOGICAL_NOT; break;
                case VITTE_TOKEN_TILDE: op = VITTE_CONSTANT_UNARY_BITWISE_NOT; break;
                default: return unknown;
            }
            operand = vitte_sema_eval_node(eval, child);
            if (operand.kind == VITTE_CONSTANT_KIND_INVALID) return unknown;
            return vitte_sema_eval_fold(eval, node, vitte_constant_fold_unary(op, &operand));
        }
        case VITTE_AST_NODE_BINARY_EXPR: {
            vitte_constant_t left, right;
            vitte_constant_binary_operator_t op = vitte_sema_eval_binary_operator(node->operator_kind);
            if (node->child_count != 2u || op == VITTE_CONSTANT_BINARY_INVALID) return unknown;
            left = vitte_sema_eval_node(eval, vitte_sema_node(context, node->children[0]));
            if (left.kind == VITTE_CONSTANT_KIND_BOOL &&
                ((op == VITTE_CONSTANT_BINARY_LOGICAL_AND && !left.as.boolean) ||
                 (op == VITTE_CONSTANT_BINARY_LOGICAL_OR && left.as.boolean))) return left;
            right = vitte_sema_eval_node(eval, vitte_sema_node(context, node->children[1]));
            if ((op == VITTE_CONSTANT_BINARY_DIVIDE || op == VITTE_CONSTANT_BINARY_REMAINDER) &&
                info != NULL && (info->type_id == context->type_i64 || info->type_id == context->type_u64 ||
                info->type_id == context->type_u8) &&
                ((right.kind == VITTE_CONSTANT_KIND_I64 && right.as.i64 == 0) ||
                 (right.kind == VITTE_CONSTANT_KIND_U64 && right.as.u64 == 0u)) &&
                eval->error == VITTE_CONSTANT_ERROR_NONE) {
                eval->error = op == VITTE_CONSTANT_BINARY_DIVIDE
                    ? VITTE_CONSTANT_ERROR_DIVISION_BY_ZERO : VITTE_CONSTANT_ERROR_REMAINDER_BY_ZERO;
                eval->failure_node = node->id;
                return unknown;
            }
            if (left.kind == VITTE_CONSTANT_KIND_INVALID || right.kind == VITTE_CONSTANT_KIND_INVALID)
                return unknown;
            /* Comparing a contextual u64 literal with a non-negative i64
             * constant is exact; other mixed numeric values remain unknown. */
            if (left.kind == VITTE_CONSTANT_KIND_U64 && right.kind == VITTE_CONSTANT_KIND_I64 && right.as.i64 >= 0)
                right = vitte_constant_make_u64((uint64_t)right.as.i64);
            else if (right.kind == VITTE_CONSTANT_KIND_U64 && left.kind == VITTE_CONSTANT_KIND_I64 && left.as.i64 >= 0)
                left = vitte_constant_make_u64((uint64_t)left.as.i64);
            return vitte_sema_eval_fold(eval, node, vitte_constant_fold_binary(op, &left, &right));
        }
        default:
            return unknown;
    }
}

static vitte_constant_t
vitte_sema_eval_node(vitte_sema_eval_t *eval, const vitte_ast_node_t *node)
{
    vitte_constant_t value;
    size_t index;
    if (node == NULL || eval->steps++ >= 4096u || eval->active_count >= 128u)
        return vitte_constant_make_invalid();
    for (index = 0u; index < eval->active_count; ++index) {
        if (eval->active[index] == node->id) {
            eval->cycle_node = node->id;
            return vitte_constant_make_invalid();
        }
    }
    eval->active[eval->active_count++] = node->id;
    value = vitte_sema_eval_node_impl(eval, node);
    --eval->active_count;
    return value;
}

static vitte_constant_t
vitte_sema_known_value(const vitte_sema_t *context, const vitte_ast_node_t *node)
{
    vitte_sema_eval_t eval;
    memset(&eval, 0, sizeof(eval));
    eval.sema = context;
    return vitte_sema_eval_node(&eval, node);
}
