#include "codegen/unit/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int f2c_unit_expression_is_character_temporary(const F2cExpr *expression) {
    const int function_call = expression != NULL && expression->kind == F2C_EXPR_CALL &&
                              expression->type == TYPE_CHARACTER && expression->text != NULL &&
                              !f2c_is_intrinsic_name(expression->text);
    const int intrinsic_call = expression != NULL && expression->kind == F2C_EXPR_CALL &&
                               (expression->intrinsic == F2C_INTRINSIC_ADJUSTL ||
                                expression->intrinsic == F2C_INTRINSIC_ADJUSTR ||
                                expression->intrinsic == F2C_INTRINSIC_REPEAT ||
                                expression->intrinsic == F2C_INTRINSIC_TRIM);
    const int concatenation = expression != NULL && expression->kind == F2C_EXPR_BINARY &&
                              expression->type == TYPE_CHARACTER && expression->text != NULL &&
                              strcmp(expression->text, "//") == 0;
    return function_call || intrinsic_call || concatenation;
}

int f2c_unit_statement_is_function_definition(const Unit *unit, size_t statement) {
    const Context *context = unit != NULL ? unit->context : NULL;
    const size_t line_index = unit != NULL ? unit->begin + statement + 1U : SIZE_MAX;
    const Line *line;
    size_t start;
    size_t close;
    char *name;
    Symbol *symbol;
    if (context == NULL || line_index >= context->lines.count)
        return 0;
    line = &context->lines.items[line_index];
    start = line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
    if (start + 3U >= line->token_count || line->tokens[start].kind != F2C_TOKEN_IDENTIFIER ||
        line->tokens[start + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, start + 1U, &close) ||
        close + 1U >= line->token_count || line->tokens[close + 1U].kind != F2C_TOKEN_OPERATOR ||
        !f2c_token_equals(&line->tokens[close + 1U], "="))
        return 0;
    name = f2c_token_text(&line->tokens[start]);
    if (name == NULL)
        return 0;
    symbol = f2c_find_symbol((Unit *)unit, name);
    free(name);
    return symbol != NULL && symbol->statement_function &&
           symbol->statement_function_line == line->number;
}

static const F2cExpr *actual_value(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   expression->child_count == 1U
               ? expression->children[0]
               : expression;
}

static int user_procedure_call(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           expression->intrinsic == F2C_INTRINSIC_NONE && expression->text != NULL &&
           (expression->resolved_procedure != NULL || expression->symbol != NULL ||
            !f2c_is_intrinsic_name(expression->text));
}

static int derived_actual_temporary(const F2cExpr *expression) {
    return expression != NULL && expression->type == TYPE_DERIVED &&
           expression->derived_type != NULL && expression->rank == 0U && !expression->definable;
}

static int call_has_derived_actual(const F2cExpr *expression) {
    const size_t first =
        expression != NULL && expression->symbol != NULL && expression->symbol->type_bound ? 1U
                                                                                           : 0U;
    size_t child;
    if (!user_procedure_call(expression))
        return 0;
    for (child = first; child < expression->child_count; ++child)
        if (derived_actual_temporary(actual_value(expression->children[child])))
            return 1;
    return 0;
}

static int materialized_call_result(const F2cExpr *expression) {
    return call_has_derived_actual(expression) && expression->rank == 0U &&
           expression->type != TYPE_UNKNOWN && expression->type != TYPE_CHARACTER &&
           expression->type != TYPE_DERIVED;
}

static int materialized_derived_call_result(const F2cExpr *expression) {
    return call_has_derived_actual(expression) && expression->rank == 0U &&
           expression->type == TYPE_DERIVED && expression->derived_type != NULL;
}

static void assign_expression_temporary(F2cExpr *expression, void *state) {
    size_t *next = (size_t *)state;
    if (f2c_unit_expression_is_character_temporary(expression))
        expression->temporary_index = (*next)++;
    if (user_procedure_call(expression)) {
        const size_t first = expression->symbol != NULL && expression->symbol->type_bound ? 1U : 0U;
        size_t child;
        for (child = first; child < expression->child_count; ++child) {
            F2cExpr *actual = expression->children[child];
            if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                actual->child_count == 1U)
                actual = actual->children[0];
            if (derived_actual_temporary(actual) && actual->temporary_index == SIZE_MAX)
                actual->temporary_index = (*next)++;
        }
        if (materialized_call_result(expression) && expression->temporary_index == SIZE_MAX)
            expression->temporary_index = (*next)++;
        if (materialized_derived_call_result(expression) &&
            expression->statement_temporary_index == SIZE_MAX)
            expression->statement_temporary_index = (*next)++;
    }
}

static void emit_expression_temporary(F2cExpr *expression, void *state) {
    Buffer *output = (Buffer *)state;
    if (f2c_unit_expression_is_character_temporary(expression)) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "char *f2c_character_result_%zu = NULL;\n",
                          expression->temporary_index);
    }
    if (derived_actual_temporary(expression) && expression->temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "%s f2c_derived_actual_%zu = {0};\n",
                          expression->derived_type->c_name, expression->temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "bool f2c_derived_actual_live_%zu = false;\n",
                          expression->temporary_index);
    }
    if (materialized_call_result(expression) && expression->temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "%s f2c_expression_result_%zu = {0};\n",
                          f2c_expression_c_type(expression), expression->temporary_index);
    }
    if (materialized_derived_call_result(expression) &&
        expression->statement_temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "%s f2c_derived_result_%zu = {0};\n",
                          expression->derived_type->c_name, expression->statement_temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "bool f2c_derived_result_live_%zu = false;\n",
                          expression->statement_temporary_index);
    }
}

void f2c_unit_prepare_expression_temporaries(Unit *unit) {
    size_t statement;
    size_t next = 0U;
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!f2c_unit_statement_is_function_definition(unit, statement))
            f2c_visit_statement_expressions(&unit->statements[statement],
                                            assign_expression_temporary, &next);
}

void f2c_unit_emit_expression_temporaries(Buffer *output, Unit *unit) {
    size_t statement;
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!f2c_unit_statement_is_function_definition(unit, statement))
            f2c_visit_statement_expressions(&unit->statements[statement], emit_expression_temporary,
                                            output);
}
