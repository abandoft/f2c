#include "codegen/unit/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int call_has_allocatable_result(const F2cExpr *expression) {
    const Unit *procedure = expression != NULL ? expression->resolved_procedure : NULL;
    const Symbol *result = procedure != NULL && procedure->result_name != NULL
                               ? f2c_find_symbol((Unit *)procedure, procedure->result_name)
                               : NULL;
    return (result != NULL && result->allocatable) ||
           (expression != NULL && expression->symbol != NULL &&
            expression->symbol->external_result_allocatable);
}

int f2c_unit_expression_is_character_temporary(const F2cExpr *expression) {
    const int function_call = expression != NULL && expression->kind == F2C_EXPR_CALL &&
                              expression->type == TYPE_CHARACTER && expression->text != NULL &&
                              !f2c_is_intrinsic_name(expression->text) &&
                              !call_has_allocatable_result(expression);
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
    const int explicitly_external =
        expression != NULL && expression->symbol != NULL && expression->symbol->external_declared;
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           expression->intrinsic == F2C_INTRINSIC_NONE && expression->text != NULL &&
           (expression->resolved_procedure != NULL || explicitly_external ||
            !f2c_is_intrinsic_name(expression->text));
}

static const Unit *capture_procedure(const F2cExpr *expression) {
    const Unit *resolved = expression != NULL && expression->resolved_procedure != NULL &&
                                   !expression->resolved_procedure->interface_abstract
                               ? expression->resolved_procedure
                               : NULL;
    if (resolved != NULL && resolved->internal)
        return resolved;
    return expression != NULL && expression->symbol != NULL &&
                   expression->symbol->procedure_interface != NULL &&
                   expression->symbol->procedure_interface->internal
               ? expression->symbol->procedure_interface
               : NULL;
}

static int derived_actual_temporary(const F2cExpr *expression) {
    return expression != NULL && expression->type == TYPE_DERIVED &&
           expression->derived_type != NULL && expression->rank == 0U && !expression->definable;
}

static int actual_guaranteed_contiguous(const F2cExpr *actual) {
    const Symbol *symbol;
    actual = actual_value(actual);
    if (actual == NULL || actual->kind != F2C_EXPR_NAME || actual->symbol == NULL)
        return 0;
    symbol = actual->symbol;
    if (symbol->pointer || (symbol->argument && f2c_symbol_uses_descriptor(symbol)))
        return symbol->contiguous;
    return 1;
}

static void assign_contiguous_actual(F2cExpr *actual, int descriptor, int contiguous, int pointer,
                                     size_t *next) {
    actual = (F2cExpr *)actual_value(actual);
    if (actual != NULL && actual->rank != 0U && descriptor && contiguous && !pointer &&
        !actual_guaranteed_contiguous(actual) && !actual->has_contiguous_temporary) {
        actual->contiguous_temporary_index = (*next)++;
        actual->has_contiguous_temporary = 1;
    }
}

static void assign_call_contiguous_actuals(F2cExpr *expression, size_t *next) {
    const Unit *resolved = expression->resolved_procedure != NULL &&
                                   !expression->resolved_procedure->interface_abstract
                               ? expression->resolved_procedure
                               : NULL;
    const Symbol *procedure = expression->symbol;
    size_t parameter;
    if (procedure != NULL && procedure->type_bound) {
        const F2cExpr *callee = expression->child_count != 0U ? expression->children[0] : NULL;
        F2cExpr *passed_object =
            callee != NULL && callee->kind == F2C_EXPR_COMPONENT && callee->child_count != 0U
                ? callee->children[0]
                : NULL;
        size_t explicit_argument = 1U;
        for (parameter = 0U; parameter < procedure->external_parameter_count; ++parameter) {
            F2cExpr *actual;
            if (!procedure->type_bound_nopass && parameter == procedure->type_bound_pass_index) {
                actual = passed_object;
            } else {
                actual = explicit_argument < expression->child_count
                             ? expression->children[explicit_argument++]
                             : NULL;
            }
            assign_contiguous_actual(actual, procedure->external_parameter_descriptor[parameter],
                                     procedure->external_parameter_contiguous[parameter],
                                     procedure->external_parameter_pointer[parameter], next);
        }
        return;
    }
    for (parameter = 0U; parameter < expression->child_count; ++parameter) {
        Symbol *dummy = resolved != NULL && parameter < resolved->argument_count
                            ? f2c_find_symbol((Unit *)resolved, resolved->arguments[parameter])
                            : NULL;
        const int known_external =
            procedure != NULL && parameter < procedure->external_parameter_count;
        assign_contiguous_actual(
            expression->children[parameter],
            dummy != NULL ? f2c_symbol_uses_descriptor(dummy)
                          : known_external && procedure->external_parameter_descriptor[parameter],
            dummy != NULL ? dummy->contiguous
                          : known_external && procedure->external_parameter_contiguous[parameter],
            dummy != NULL ? dummy->pointer
                          : known_external && procedure->external_parameter_pointer[parameter],
            next);
    }
}

static int call_has_contiguous_actual(const F2cExpr *expression) {
    size_t child;
    if (!user_procedure_call(expression))
        return 0;
    if (expression->symbol != NULL && expression->symbol->type_bound &&
        expression->child_count != 0U && expression->children[0] != NULL &&
        expression->children[0]->kind == F2C_EXPR_COMPONENT &&
        expression->children[0]->child_count != 0U &&
        expression->children[0]->children[0]->has_contiguous_temporary)
        return 1;
    for (child = expression->symbol != NULL && expression->symbol->type_bound ? 1U : 0U;
         child < expression->child_count; ++child) {
        const F2cExpr *actual = actual_value(expression->children[child]);
        if (actual != NULL && actual->has_contiguous_temporary)
            return 1;
    }
    return 0;
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

static int call_has_managed_lifecycle(const F2cExpr *expression) {
    return call_has_derived_actual(expression) || call_has_contiguous_actual(expression) ||
           (expression != NULL && expression->has_host_descriptor_lifecycle);
}

static int materialized_call_result(const F2cExpr *expression) {
    return call_has_managed_lifecycle(expression) && !call_has_allocatable_result(expression) &&
           expression->rank == 0U && expression->type != TYPE_UNKNOWN &&
           expression->type != TYPE_CHARACTER && expression->type != TYPE_DERIVED;
}

static int materialized_derived_call_result(const F2cExpr *expression) {
    return call_has_managed_lifecycle(expression) && !call_has_allocatable_result(expression) &&
           expression->rank == 0U && expression->type == TYPE_DERIVED &&
           expression->derived_type != NULL;
}

static int materialized_descriptor_call_result(const F2cExpr *expression) {
    return call_has_managed_lifecycle(expression) && call_has_allocatable_result(expression);
}

static const F2cExpr *ordered_binary_operand(const F2cExpr *expression) {
    if (expression == NULL || expression->kind != F2C_EXPR_BINARY ||
        expression->child_count != 2U || expression->rank != 0U)
        return NULL;
    if (expression->children[0] != NULL && expression->children[0]->has_order_sensitive_call)
        return expression->children[0];
    if (expression->children[1] != NULL && expression->children[1]->has_order_sensitive_call)
        return expression->children[1];
    return NULL;
}

static int can_materialize_ordered_operand(const F2cExpr *operand) {
    return operand != NULL && operand->rank == 0U && operand->type != TYPE_UNKNOWN &&
           operand->type != TYPE_DERIVED && !call_has_allocatable_result(operand);
}

static int call_uses_argument_values(const F2cExpr *expression) {
    if (expression == NULL)
        return 0;
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_DIGITS:
    case F2C_INTRINSIC_EPSILON:
    case F2C_INTRINSIC_HUGE:
    case F2C_INTRINSIC_KIND:
    case F2C_INTRINSIC_MAXEXPONENT:
    case F2C_INTRINSIC_MINEXPONENT:
    case F2C_INTRINSIC_PRECISION:
    case F2C_INTRINSIC_RADIX:
    case F2C_INTRINSIC_RANGE:
    case F2C_INTRINSIC_TINY:
        return 0;
    case F2C_INTRINSIC_NONE:
    default:
        return 1;
    }
}

static void assign_ordered_call_arguments(F2cExpr *expression, size_t *next) {
    const size_t first =
        expression != NULL && expression->symbol != NULL && expression->symbol->type_bound ? 1U
                                                                                           : 0U;
    size_t child;
    if (expression == NULL || expression->kind != F2C_EXPR_CALL ||
        (expression->symbol != NULL && expression->symbol->statement_function) ||
        !call_uses_argument_values(expression))
        return;
    for (child = first; child < expression->child_count; ++child) {
        F2cExpr *actual = expression->children[child];
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            actual->child_count == 1U)
            actual = actual->children[0];
        if (actual != NULL && actual->has_order_sensitive_call &&
            can_materialize_ordered_operand(actual) &&
            actual->ordered_argument_temporary_index == SIZE_MAX)
            actual->ordered_argument_temporary_index = (*next)++;
    }
}

typedef struct ExpressionTemporaryAssigner {
    Unit *unit;
    size_t next;
} ExpressionTemporaryAssigner;

static void assign_expression_temporary(F2cExpr *expression, void *state) {
    ExpressionTemporaryAssigner *assigner = (ExpressionTemporaryAssigner *)state;
    size_t *next = &assigner->next;
    size_t child;
    if (f2c_unit_expression_is_character_temporary(expression))
        expression->temporary_index = (*next)++;
    if (user_procedure_call(expression)) {
        const size_t first = expression->symbol != NULL && expression->symbol->type_bound ? 1U : 0U;
        for (child = first; child < expression->child_count; ++child) {
            F2cExpr *actual = expression->children[child];
            if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                actual->child_count == 1U)
                actual = actual->children[0];
            if (derived_actual_temporary(actual) && actual->temporary_index == SIZE_MAX)
                actual->temporary_index = (*next)++;
        }
        assign_call_contiguous_actuals(expression, next);
        {
            const Unit *procedure = capture_procedure(expression);
            const size_t descriptor_count =
                f2c_host_capture_local_descriptor_count(assigner->unit, procedure);
            expression->has_host_descriptor_lifecycle =
                f2c_host_capture_has_descriptor_lifecycle(assigner->unit, procedure);
            if (descriptor_count > SIZE_MAX - *next) {
                f2c_diagnostic(assigner->unit->context,
                               assigner->unit->context->lines.items[assigner->unit->begin].number,
                               1, "host-capture descriptor temporary count overflow");
                return;
            }
            if (descriptor_count != 0U) {
                expression->host_descriptor_temporary_begin = *next;
                expression->host_descriptor_temporary_count = descriptor_count;
                *next += descriptor_count;
            }
        }
        if (materialized_call_result(expression) && expression->temporary_index == SIZE_MAX)
            expression->temporary_index = (*next)++;
        if (materialized_descriptor_call_result(expression) &&
            expression->temporary_index == SIZE_MAX)
            expression->temporary_index = (*next)++;
        if (materialized_derived_call_result(expression) &&
            expression->statement_temporary_index == SIZE_MAX)
            expression->statement_temporary_index = (*next)++;
    }
    assign_ordered_call_arguments(expression, next);
    expression->has_order_sensitive_call = user_procedure_call(expression);
    if (call_uses_argument_values(expression))
        for (child = 0U; child < expression->child_count; ++child)
            if (expression->children[child] != NULL &&
                expression->children[child]->has_order_sensitive_call)
                expression->has_order_sensitive_call = 1;
    if (expression->ordered_temporary_index == SIZE_MAX &&
        can_materialize_ordered_operand(ordered_binary_operand(expression)))
        expression->ordered_temporary_index = (*next)++;
}

static void emit_expression_temporary(F2cExpr *expression, void *state) {
    Buffer *output = (Buffer *)state;
    const F2cExpr *ordered_operand = ordered_binary_operand(expression);
    size_t descriptor;
    for (descriptor = 0U; descriptor < expression->host_descriptor_temporary_count; ++descriptor) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "f2c_descriptor f2c_host_descriptor_%zu = {0};\n",
                          expression->host_descriptor_temporary_begin + descriptor);
    }
    if (expression->has_contiguous_temporary) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "f2c_descriptor f2c_contiguous_source_%zu = {0};\n",
                          expression->contiguous_temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "f2c_descriptor f2c_contiguous_actual_%zu = {0};\n",
                          expression->contiguous_temporary_index);
    }
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
    if (materialized_descriptor_call_result(expression) &&
        expression->temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "f2c_descriptor f2c_expression_descriptor_result_%zu = {0};\n",
                          expression->temporary_index);
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
    if (ordered_operand != NULL && expression->ordered_temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        if (ordered_operand->type == TYPE_CHARACTER)
            f2c_buffer_printf(output, "char *f2c_ordered_value_%zu = NULL;\n",
                              expression->ordered_temporary_index);
        else
            f2c_buffer_printf(output, "%s f2c_ordered_value_%zu = {0};\n",
                              f2c_expression_c_type(ordered_operand),
                              expression->ordered_temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "(void)f2c_ordered_value_%zu;\n",
                          expression->ordered_temporary_index);
    }
    if (expression->ordered_argument_temporary_index != SIZE_MAX) {
        f2c_unit_indent(output, 1);
        if (expression->type == TYPE_CHARACTER)
            f2c_buffer_printf(output, "char *f2c_ordered_argument_%zu = NULL;\n",
                              expression->ordered_argument_temporary_index);
        else
            f2c_buffer_printf(output, "%s f2c_ordered_argument_%zu = {0};\n",
                              f2c_expression_c_type(expression),
                              expression->ordered_argument_temporary_index);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "(void)f2c_ordered_argument_%zu;\n",
                          expression->ordered_argument_temporary_index);
    }
}

void f2c_unit_prepare_expression_temporaries(Unit *unit) {
    size_t statement;
    ExpressionTemporaryAssigner assigner = {unit, 0U};
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!f2c_unit_statement_is_function_definition(unit, statement))
            f2c_visit_statement_expressions(&unit->statements[statement],
                                            assign_expression_temporary, &assigner);
}

void f2c_unit_emit_expression_temporaries(Buffer *output, Unit *unit) {
    size_t statement;
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!f2c_unit_statement_is_function_definition(unit, statement))
            f2c_visit_statement_expressions(&unit->statements[statement], emit_expression_temporary,
                                            output);
}
