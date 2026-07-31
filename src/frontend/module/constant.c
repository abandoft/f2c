#include "frontend/module/constant.h"

#include "internal/f2c.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const F2cModuleConstant *find_constant(const F2cModuleConstant *constants,
                                              size_t constant_count, const char *name) {
    size_t index;
    for (index = 0U; index < constant_count; ++index)
        if (strcmp(constants[index].name, name) == 0)
            return &constants[index];
    return NULL;
}

static F2cExpr *parse_compiler_constant(Unit *unit, const char *source) {
    F2cTokenStream stream;
    F2cToken *tokens = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    F2cExpr *expression = NULL;
    const char *error_at = NULL;
    f2c_token_stream_init(&stream, source, 1U, 1U);
    for (;;) {
        F2cToken *replacement;
        size_t next;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (stream.token.kind == F2C_TOKEN_INVALID)
            goto cleanup;
        if (count == capacity) {
            next = capacity == 0U ? 8U : capacity * 2U;
            if (next < capacity || next > SIZE_MAX / sizeof(*tokens))
                goto cleanup;
            replacement = (F2cToken *)realloc(tokens, next * sizeof(*tokens));
            if (replacement == NULL)
                goto cleanup;
            tokens = replacement;
            capacity = next;
        }
        tokens[count++] = stream.token;
    }
    expression = f2c_parse_expression_tokens(unit, tokens, count, source, &error_at);
    if (error_at != NULL) {
        f2c_expr_free(expression);
        expression = NULL;
    }

cleanup:
    free(tokens);
    return expression;
}

int f2c_use_name_is_renamed(const F2cUseStatementSyntax *syntax, const char *name) {
    size_t index;
    if (syntax == NULL)
        return 0;
    for (index = 0U; index < syntax->item_count; ++index) {
        const F2cUseAssociationSyntax *association = &syntax->items[index];
        if (association->renamed && association->remote.kind == F2C_USE_DESIGNATOR_NAME &&
            f2c_token_equals(association->remote.name, name))
            return 1;
    }
    return 0;
}

static void assign_kind_type(Symbol *symbol) {
    int64_t value;
    if (symbol == NULL || symbol->type != TYPE_INTEGER ||
        !f2c_evaluate_integer_constant(NULL, symbol->initializer_expression, &value))
        return;
    if (value == f2c_default_kind(TYPE_REAL))
        symbol->kind_type = TYPE_REAL;
    else if (value == f2c_default_kind(TYPE_DOUBLE))
        symbol->kind_type = TYPE_DOUBLE;
}

static void import_constant(Context *context, Unit *unit, const char *local_name,
                            const char *remote_name, const char *module_name,
                            const char *c_name_prefix, const F2cModuleConstant *constants,
                            size_t constant_count, const F2cSourceSpan *span) {
    const F2cModuleConstant *constant = find_constant(constants, constant_count, remote_name);
    Symbol *symbol;
    Buffer c_name = {0};
    char *resolved_c_name;
    if (constant == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, span, 1, "%s has no member '%s'",
                                 module_name, remote_name);
        return;
    }
    f2c_buffer_printf(&c_name, "%s_%s", c_name_prefix, remote_name);
    resolved_c_name = f2c_buffer_take(&c_name);
    if (resolved_c_name == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                                 "out of memory importing '%s'", local_name);
        return;
    }
    symbol = f2c_find_symbol(unit, local_name);
    if (symbol != NULL) {
        const int same_entity = symbol->use_associated && symbol->c_name != NULL &&
                                strcmp(symbol->c_name, resolved_c_name) == 0;
        free(resolved_c_name);
        if (!same_entity)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, span, 1,
                                     "USE local name '%s' denotes conflicting entities",
                                     local_name);
        return;
    }
    symbol = f2c_ensure_symbol(unit, local_name);
    if (symbol == NULL) {
        free(resolved_c_name);
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                                 "out of memory importing '%s'", local_name);
        return;
    }
    symbol->type = constant->type;
    symbol->kind = f2c_default_kind(constant->type);
    symbol->value_category = F2C_VALUE_CONSTANT;
    symbol->parameter = 1;
    symbol->module_entity = 1;
    symbol->use_associated = 1;
    symbol->access = F2C_ACCESS_UNSPECIFIED;
    memset(&symbol->access_span, 0, sizeof(symbol->access_span));
    free(symbol->c_name);
    symbol->c_name = resolved_c_name;
    free(symbol->initializer);
    symbol->initializer = f2c_strdup(constant->initializer);
    f2c_expr_free(symbol->initializer_expression);
    symbol->initializer_expression = parse_compiler_constant(unit, constant->initializer);
    assign_kind_type(symbol);
    if (symbol->initializer == NULL || symbol->initializer_expression == NULL)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                                 "unable to build typed module constant '%s'", local_name);
    if (constant->type == TYPE_CHARACTER) {
        free(symbol->character_length);
        symbol->character_length = f2c_strdup("1");
        if (symbol->character_length == NULL)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                                     "out of memory importing CHARACTER constant '%s'", local_name);
    }
}

void f2c_import_constant_module(Context *context, Unit *unit, const F2cUseStatementSyntax *syntax,
                                const char *module_name, const char *c_name_prefix,
                                const F2cModuleConstant *constants, size_t constant_count) {
    size_t index;
    if (syntax->only_token == NULL) {
        for (index = 0U; index < constant_count; ++index) {
            const F2cModuleConstant *constant = &constants[index];
            if (!f2c_use_name_is_renamed(syntax, constant->name))
                import_constant(context, unit, constant->name, constant->name, module_name,
                                c_name_prefix, constants, constant_count,
                                &syntax->module_name->span);
        }
    }
    for (index = 0U; index < syntax->item_count; ++index) {
        const F2cUseAssociationSyntax *association = &syntax->items[index];
        char *local_name;
        char *remote_name;
        if (association->local.kind != F2C_USE_DESIGNATOR_NAME ||
            association->remote.kind != F2C_USE_DESIGNATOR_NAME) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, &association->span, 1,
                                     "%s exposes only named constants", module_name);
            continue;
        }
        local_name = f2c_token_text(association->local.name);
        remote_name = f2c_token_text(association->remote.name);
        if (local_name == NULL || remote_name == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &association->span, 1,
                                     "out of memory importing %s", module_name);
            free(local_name);
            free(remote_name);
            return;
        }
        import_constant(context, unit, local_name, remote_name, module_name, c_name_prefix,
                        constants, constant_count, &association->span);
        free(local_name);
        free(remote_name);
    }
}
