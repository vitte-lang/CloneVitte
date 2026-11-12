/* Outcomes compose through blocks and loops; break/continue belong to the
 * innermost loop and never terminate the surrounding procedure. */
#define VITTE_FLOW_NEXT 1u
#define VITTE_FLOW_RETURN 2u
#define VITTE_FLOW_BREAK 4u
#define VITTE_FLOW_CONTINUE 8u
#define VITTE_FLOW_UNKNOWN 16u

typedef enum vitte_sema_exit_flow {
    VITTE_SEMA_EXIT_FALLTHROUGH,
    VITTE_SEMA_EXIT_TERMINATED,
    VITTE_SEMA_EXIT_UNKNOWN
} vitte_sema_exit_flow_t;

static unsigned int
vitte_sema_flow(const vitte_sema_t *context, const vitte_ast_node_t *node, size_t depth)
{
    size_t index;
    if (node == NULL || depth >= 128u) return VITTE_FLOW_UNKNOWN;
    switch (node->kind) {
        case VITTE_AST_NODE_RETURN_STMT: return VITTE_FLOW_RETURN;
        case VITTE_AST_NODE_BREAK_STMT: return VITTE_FLOW_BREAK;
        case VITTE_AST_NODE_CONTINUE_STMT: return VITTE_FLOW_CONTINUE;
        case VITTE_AST_NODE_BLOCK:
        case VITTE_AST_NODE_UNSAFE_BLOCK: {
            unsigned int flow = VITTE_FLOW_NEXT;
            for (index = 0u; index < node->child_count && (flow & VITTE_FLOW_NEXT) != 0u; ++index) {
                flow = (flow & ~VITTE_FLOW_NEXT) | vitte_sema_flow(context,
                    vitte_sema_node(context, node->children[index]), depth + 1u);
            }
            return flow;
        }
        case VITTE_AST_NODE_IF_STMT: {
            vitte_constant_t condition;
            unsigned int left, right;
            if (node->child_count < 2u) return VITTE_FLOW_UNKNOWN;
            condition = vitte_sema_known_value(context, vitte_sema_node(context, node->children[0]));
            left = vitte_sema_flow(context, vitte_sema_node(context, node->children[1]), depth + 1u);
            right = node->child_count >= 3u
                ? vitte_sema_flow(context, vitte_sema_node(context, node->children[2]), depth + 1u)
                : VITTE_FLOW_NEXT;
            return condition.kind == VITTE_CONSTANT_KIND_BOOL ? (condition.as.boolean ? left : right) : left | right;
        }
        case VITTE_AST_NODE_WHILE_STMT:
        case VITTE_AST_NODE_LOOP_STMT:
        case VITTE_AST_NODE_FOR_STMT: {
            bool infinite = node->kind == VITTE_AST_NODE_LOOP_STMT;
            unsigned int body;
            if (node->child_count == 0u) return VITTE_FLOW_UNKNOWN;
            if (node->kind == VITTE_AST_NODE_WHILE_STMT) {
                vitte_constant_t condition = vitte_sema_known_value(context,
                    vitte_sema_node(context, node->children[0]));
                if (condition.kind == VITTE_CONSTANT_KIND_BOOL) {
                    if (!condition.as.boolean) return VITTE_FLOW_NEXT;
                    infinite = true;
                }
            }
            body = vitte_sema_flow(context,
                vitte_sema_node(context, node->children[node->child_count - 1u]), depth + 1u);
            return (body & (VITTE_FLOW_RETURN | VITTE_FLOW_UNKNOWN)) |
                (!infinite || (body & VITTE_FLOW_BREAK) != 0u ? VITTE_FLOW_NEXT : 0u);
        }
        case VITTE_AST_NODE_MATCH_STMT: {
            unsigned int outcomes = 0u;
            bool wildcard = false, has_true = false, has_false = false;
            for (index = 1u; index < node->child_count; ++index) {
                const vitte_ast_node_t *arm = vitte_sema_node(context, node->children[index]);
                const vitte_ast_node_t *pattern;
                const vitte_token_t *token;
                const vitte_ast_node_t *body;
                if (arm == NULL || arm->child_count < 2u) return VITTE_FLOW_UNKNOWN;
                pattern = vitte_sema_node(context, arm->children[0]);
                token = vitte_sema_node_token(context, pattern);
                body = vitte_sema_node(context, arm->children[arm->child_count - 1u]);
                if (body != NULL && body->kind != VITTE_AST_NODE_BLOCK &&
                    body->kind != VITTE_AST_NODE_RETURN_STMT && body->kind != VITTE_AST_NODE_IF_STMT &&
                    body->kind != VITTE_AST_NODE_EXPR_STMT) {
                    /* Bare match arm expressions are implicit procedure results. */
                    outcomes |= VITTE_FLOW_RETURN;
                } else outcomes |= vitte_sema_flow(context, body, depth + 1u);
                if (arm->child_count == 2u && token != NULL) {
                    if (token->length == 1u && token->lexeme[0] == '_') wildcard = true;
                    if (token->kind == VITTE_TOKEN_KW_TRUE) has_true = true;
                    if (token->kind == VITTE_TOKEN_KW_FALSE) has_false = true;
                }
                if (wildcard) break;
            }
            if (!wildcard && !(has_true && has_false)) outcomes |= VITTE_FLOW_UNKNOWN;
            return outcomes;
        }
        case VITTE_AST_NODE_ASM_STMT:
            return VITTE_FLOW_UNKNOWN;
        case VITTE_AST_NODE_EXPR_STMT:
        case VITTE_AST_NODE_LET_STMT:
            for (index = 0u; index < node->child_count; ++index) {
                const vitte_sema_node_info_t *info = vitte_sema_get_node_info(context, node->children[index]);
                if (info != NULL && info->type_id == context->type_never) return VITTE_FLOW_RETURN;
                if (info != NULL && info->type_id == context->type_error) return VITTE_FLOW_UNKNOWN;
            }
            return VITTE_FLOW_NEXT;
        default:
            return VITTE_FLOW_NEXT;
    }
}

static vitte_sema_exit_flow_t
vitte_sema_exit_flow(const vitte_sema_t *context, const vitte_ast_node_t *node, size_t depth)
{
    unsigned int flow = vitte_sema_flow(context, node, depth);
    if ((flow & VITTE_FLOW_UNKNOWN) != 0u) return VITTE_SEMA_EXIT_UNKNOWN;
    if ((flow & VITTE_FLOW_NEXT) != 0u) return VITTE_SEMA_EXIT_FALLTHROUGH;
    return VITTE_SEMA_EXIT_TERMINATED;
}
