/* Contract checks are language semantics and do not depend on C assert/NDEBUG. */
static bool
vitte_c17_ast_has_contract(const vitte_c17_ast_emitter_t *emitter,
                           vitte_ast_flags_t flags)
{
    size_t index;
    if (emitter->function == NULL) return false;
    for (index = 0u; index < emitter->function->child_count; ++index) {
        const vitte_ast_node_t *child = vitte_c17_ast_child(emitter, emitter->function, index);
        if (child != NULL && (child->flags & flags) != 0u) return true;
    }
    return false;
}

static bool
vitte_c17_ast_contract_name(vitte_c17_ast_emitter_t *emitter,
    const vitte_ast_node_t *node, const char *role, char *name, size_t capacity)
{
    size_t attempt;
    for (attempt = 0u; attempt <= emitter->parser->token_count; ++attempt) {
        int length = snprintf(name, capacity, "vitte_internal_contract_%llu_%s_%zu",
            (unsigned long long)node->id, role, attempt);
        if (length < 0 || (size_t)length >= capacity) break;
        if (!vitte_c17_ast_generated_name_is_used(emitter, name, (size_t)length)) return true;
    }
    emitter->failed = true;
    return false;
}

static bool
vitte_c17_ast_contract_identifier(vitte_c17_ast_emitter_t *emitter,
                                 const vitte_ast_node_t *node)
{
    const vitte_token_t *token;
    size_t index;
    if (!emitter->in_contract || emitter->function == NULL ||
        (node->kind != VITTE_AST_NODE_IDENTIFIER &&
         !(node->kind == VITTE_AST_NODE_PATH && node->child_count == 1u))) return false;
    token = vitte_c17_ast_token(emitter, node);
    if (token == NULL) return false;
    if (token->length == 6u && memcmp(token->lexeme, "result", 6u) == 0 &&
        emitter->contract_result_name != NULL) {
        vitte_c17_ast_write(emitter, emitter->contract_result_name);
        return true;
    }
    for (index = 0u; index < emitter->function->child_count; ++index) {
        const vitte_ast_node_t *parameter = vitte_c17_ast_child(emitter, emitter->function, index);
        const vitte_ast_node_t *type, *name;
        const vitte_token_t *parameter_token;
        char snapshot[128];
        if (parameter == NULL || parameter->kind != VITTE_AST_NODE_PARAMETER) continue;
        name = vitte_c17_ast_child(emitter, parameter, 0u);
        type = vitte_c17_ast_child(emitter, parameter, 1u);
        parameter_token = vitte_c17_ast_token(emitter, name);
        if (parameter_token == NULL || parameter_token->length != token->length ||
            memcmp(parameter_token->lexeme, token->lexeme, token->length) != 0) continue;
        if (!vitte_c17_ast_contract_name(emitter, parameter, "parameter", snapshot, sizeof(snapshot))) return true;
        if (type != NULL && type->kind == VITTE_AST_NODE_REFERENCE_TYPE)
            vitte_c17_ast_write(emitter, "(*");
        vitte_c17_ast_write(emitter, snapshot);
        if (type != NULL && type->kind == VITTE_AST_NODE_REFERENCE_TYPE)
            vitte_c17_ast_write(emitter, ")");
        return true;
    }
    return false;
}

static void
vitte_c17_ast_contract_literal(vitte_c17_ast_emitter_t *emitter,
                               const char *text, size_t length)
{
    size_t index;
    vitte_c17_ast_write(emitter, "\"");
    for (index = 0u; index < length; ++index) {
        unsigned char byte = (unsigned char)text[index];
        if (byte == '\\' || byte == '"') {
            (void)fputc('\\', emitter->output);
            (void)fputc((int)byte, emitter->output);
        } else if (byte < 32u || byte >= 127u) {
            (void)fprintf(emitter->output, "\\%03o", (unsigned int)byte);
        } else {
            (void)fputc((int)byte, emitter->output);
        }
    }
    vitte_c17_ast_write(emitter, "\"");
    if (ferror(emitter->output)) emitter->failed = true;
}

static void
vitte_c17_ast_emit_contracts(vitte_c17_ast_emitter_t *emitter,
    vitte_ast_flags_t flag, const char *result, unsigned int indent)
{
    size_t index;
    bool previous_contract = emitter->in_contract;
    const char *previous_result = emitter->contract_result_name;
    if (emitter->function == NULL) return;
    for (index = 0u; index < emitter->function->child_count && !emitter->failed; ++index) {
        const vitte_ast_node_t *clause = vitte_c17_ast_child(emitter, emitter->function, index);
        const vitte_ast_node_t *procedure_name;
        const vitte_token_t *name;
        const char *path = emitter->source_path != NULL ? emitter->source_path : "<source>";
        bool requires = flag == VITTE_AST_FLAG_CONTRACT_REQUIRES;
        char header[192];
        size_t token_index;
        if (clause == NULL || (clause->flags & flag) == 0u) continue;
        emitter->in_contract = true;
        emitter->contract_result_name = result;
        vitte_c17_ast_indent(emitter, indent);
        vitte_c17_ast_write(emitter, "if (!(");
        vitte_c17_ast_emit_expression(emitter, clause);
        vitte_c17_ast_write(emitter, ")) {\n");
        emitter->in_contract = previous_contract;
        emitter->contract_result_name = previous_result;
        vitte_c17_ast_indent(emitter, indent + 1u);
        vitte_c17_ast_write(emitter, "fputs(");
        (void)snprintf(header, sizeof(header),
            "error[%s]: %s\n  phase: contract\n  procedure: ",
            requires ? "E0701" : "E0702",
            requires ? "precondition not satisfied" : "postcondition failed");
        vitte_c17_ast_contract_literal(emitter, header, strlen(header));
        procedure_name = vitte_c17_ast_child(emitter, emitter->function, 0u);
        name = vitte_c17_ast_token(emitter, procedure_name);
        if (name != NULL) vitte_c17_ast_contract_literal(emitter, name->lexeme, name->length);
        vitte_c17_ast_contract_literal(emitter, "\n  --> ", 7u);
        vitte_c17_ast_contract_literal(emitter, path, strlen(path));
        (void)snprintf(header, sizeof(header), ":%zu:%zu\n  contract: %s `",
            clause->span.line, clause->span.column, requires ? "requires" : "ensures");
        vitte_c17_ast_contract_literal(emitter, header, strlen(header));
        for (token_index = 0u; token_index < emitter->parser->token_count; ++token_index) {
            const vitte_token_t *token = &emitter->parser->tokens[token_index];
            if (token->span.begin >= clause->span.begin && token->span.end <= clause->span.end &&
                token->length != 0u) {
                vitte_c17_ast_contract_literal(emitter, token->lexeme, token->length);
                vitte_c17_ast_contract_literal(emitter, " ", 1u);
            }
        }
        vitte_c17_ast_contract_literal(emitter, "`\n", 2u);
        {
            const char *help = requires
                ? "  help: Ensure the arguments and program state satisfy this requires clause before calling the procedure.\n"
                : "  help: Ensure every normal return satisfies this ensures clause after deferred actions have completed.\n";
            vitte_c17_ast_contract_literal(emitter, help, strlen(help));
        }
        vitte_c17_ast_write(emitter, ", stderr);\n");
        vitte_c17_ast_indent(emitter, indent + 1u);
        vitte_c17_ast_write(emitter, "exit(EXIT_FAILURE);\n");
        vitte_c17_ast_indent(emitter, indent);
        vitte_c17_ast_write(emitter, "}\n");
    }
    emitter->in_contract = previous_contract;
    emitter->contract_result_name = previous_result;
}

static void
vitte_c17_ast_emit_contract_entry(vitte_c17_ast_emitter_t *emitter)
{
    size_t index;
    if (!vitte_c17_ast_has_contract(emitter,
            VITTE_AST_FLAG_CONTRACT_REQUIRES | VITTE_AST_FLAG_CONTRACT_ENSURES)) return;
    for (index = 0u; index < emitter->function->child_count; ++index) {
        const vitte_ast_node_t *parameter = vitte_c17_ast_child(emitter, emitter->function, index);
        const vitte_ast_node_t *type;
        char snapshot[128];
        if (parameter == NULL || parameter->kind != VITTE_AST_NODE_PARAMETER) continue;
        type = vitte_c17_ast_child(emitter, parameter, 1u);
        if (!vitte_c17_ast_contract_name(emitter, parameter, "parameter", snapshot, sizeof(snapshot))) return;
        vitte_c17_ast_indent(emitter, 1u);
        if (type != NULL && type->kind == VITTE_AST_NODE_ARRAY_TYPE && type->child_count > 1u) {
            vitte_c17_ast_emit_type(emitter, vitte_c17_ast_child(emitter, type, type->child_count - 1u));
            vitte_c17_ast_write(emitter, " *");
        } else {
            vitte_c17_ast_emit_type(emitter, type);
        }
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_write(emitter, snapshot);
        vitte_c17_ast_write(emitter, " = ");
        vitte_c17_ast_write_token(emitter, vitte_c17_ast_child(emitter, parameter, 0u));
        vitte_c17_ast_write(emitter, ";\n    (void)");
        vitte_c17_ast_write(emitter, snapshot);
        vitte_c17_ast_write(emitter, ";\n");
    }
    vitte_c17_ast_emit_contracts(emitter, VITTE_AST_FLAG_CONTRACT_REQUIRES, NULL, 1u);
}

static bool
vitte_c17_ast_emit_contract_return(vitte_c17_ast_emitter_t *emitter,
                                  const vitte_ast_node_t *node, unsigned int indent)
{
    const vitte_ast_node_t *type;
    const vitte_ast_node_t *value = vitte_c17_ast_child(emitter, node, 0u);
    bool is_void;
    char result[128];
    if (!vitte_c17_ast_has_contract(emitter, VITTE_AST_FLAG_CONTRACT_ENSURES)) return false;
    if (value != NULL && vitte_c17_ast_expression_returns_never(emitter, value)) return false;
    type = vitte_c17_ast_function_return_type(emitter, emitter->function);
    is_void = type == NULL || vitte_c17_ast_type_is_named(emitter, type, "void") ||
        vitte_c17_ast_type_is_named(emitter, type, "unit");
    if (!vitte_c17_ast_contract_name(emitter, node, "result", result, sizeof(result))) return true;
    vitte_c17_ast_write(emitter, "{\n");
    if (value != NULL) {
        vitte_c17_ast_indent(emitter, indent + 1u);
        if (!is_void) {
            vitte_c17_ast_emit_type(emitter, type);
            vitte_c17_ast_write(emitter, " ");
            vitte_c17_ast_write(emitter, result);
            vitte_c17_ast_write(emitter, " = ");
        }
        if (!is_void && value->kind == VITTE_AST_NODE_ARRAY_EXPR &&
            type->kind == VITTE_AST_NODE_ARRAY_TYPE && type->child_count == 1u)
            vitte_c17_ast_emit_form_array_initializer(emitter, value, type);
        else vitte_c17_ast_emit_expression(emitter, value);
        vitte_c17_ast_write(emitter, ";\n");
    }
    vitte_c17_ast_emit_deferred_actions(emitter, 0u, indent + 1u);
    vitte_c17_ast_emit_contracts(emitter, VITTE_AST_FLAG_CONTRACT_ENSURES,
        is_void ? NULL : result, indent + 1u);
    vitte_c17_ast_indent(emitter, indent + 1u);
    vitte_c17_ast_write(emitter, "return");
    if (!is_void) {
        vitte_c17_ast_write(emitter, " ");
        vitte_c17_ast_write(emitter, result);
    }
    vitte_c17_ast_write(emitter, ";\n");
    vitte_c17_ast_indent(emitter, indent);
    vitte_c17_ast_write(emitter, "}\n");
    return true;
}
