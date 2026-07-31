#include "semantic/semantic.h"

#include "internal/f2c.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ExpressionTemporaryAssigner {
    Context *context;
    Unit *unit;
    size_t statement;
    size_t next;
    int failed;
} ExpressionTemporaryAssigner;

static F2cOwnedTemporaryKind owned_temporary_kind(const F2cExpr *expression) {
    if (expression == NULL || expression->rank == 0U)
        return F2C_OWNED_TEMPORARY_NONE;
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR)
        return F2C_OWNED_TEMPORARY_ARRAY_CONSTRUCTOR;
    if (expression->kind == F2C_EXPR_CALL &&
        f2c_intrinsic_is_transformational(expression->intrinsic))
        return F2C_OWNED_TEMPORARY_TRANSFORMATIONAL_RESULT;
    if (expression->kind == F2C_EXPR_CALL && expression->intrinsic == F2C_INTRINSIC_NONE &&
        f2c_expression_has_descriptor_result(expression))
        return F2C_OWNED_TEMPORARY_ARRAY_FUNCTION_RESULT;
    if (expression->kind == F2C_EXPR_UNARY || expression->kind == F2C_EXPR_BINARY)
        return F2C_OWNED_TEMPORARY_ELEMENTAL_ARRAY_VALUE;
    return F2C_OWNED_TEMPORARY_NONE;
}

static void clear_owned_temporary_plans(Unit *unit) {
    size_t statement;
    if (unit == NULL)
        return;
    free(unit->owned_temporaries);
    unit->owned_temporaries = NULL;
    unit->owned_temporary_count = 0U;
    unit->owned_temporary_capacity = 0U;
    for (statement = 0U; statement < unit->statement_count; ++statement) {
        free(unit->statements[statement].temporary_plan.owned_temporaries);
        memset(&unit->statements[statement].temporary_plan, 0,
               sizeof(unit->statements[statement].temporary_plan));
    }
}

static int append_statement_owned_temporary(ExpressionTemporaryAssigner *assigner,
                                            F2cExpr *expression, F2cOwnedTemporaryKind kind) {
    F2cOwnedTemporary *catalog_replacement;
    F2cStatementTemporaryPlan *statement_plan;
    size_t *statement_replacement;
    size_t catalog_capacity;
    size_t statement_count;
    const size_t index = assigner->unit->owned_temporary_count;
    if (index == assigner->unit->owned_temporary_capacity) {
        catalog_capacity = assigner->unit->owned_temporary_capacity == 0U
                               ? 8U
                               : assigner->unit->owned_temporary_capacity * 2U;
        if (catalog_capacity < assigner->unit->owned_temporary_capacity ||
            catalog_capacity > SIZE_MAX / sizeof(*catalog_replacement))
            return 0;
        catalog_replacement = (F2cOwnedTemporary *)realloc(
            assigner->unit->owned_temporaries, catalog_capacity * sizeof(*catalog_replacement));
        if (catalog_replacement == NULL)
            return 0;
        assigner->unit->owned_temporaries = catalog_replacement;
        assigner->unit->owned_temporary_capacity = catalog_capacity;
    }
    statement_plan = &assigner->unit->statements[assigner->statement].temporary_plan;
    statement_count = statement_plan->owned_temporary_count;
    if (statement_count == SIZE_MAX ||
        statement_count + 1U > SIZE_MAX / sizeof(*statement_replacement))
        return 0;
    statement_replacement = (size_t *)realloc(
        statement_plan->owned_temporaries, (statement_count + 1U) * sizeof(*statement_replacement));
    if (statement_replacement == NULL)
        return 0;
    statement_plan->owned_temporaries = statement_replacement;
    statement_plan->owned_temporaries[statement_count] = index;
    statement_plan->owned_temporary_count = statement_count + 1U;
    assigner->unit->owned_temporaries[index] =
        (F2cOwnedTemporary){kind,
                            expression->type,
                            expression->type_kind,
                            expression->rank,
                            assigner->statement,
                            expression->span,
                            expression->derived_type,
                            expression->type == TYPE_DERIVED && expression->derived_type != NULL};
    assigner->unit->owned_temporary_count = index + 1U;
    expression->owned_temporary_index = index;
    expression->owned_temporary_kind = kind;
    return 1;
}

int f2c_expression_is_character_temporary(const F2cExpr *expression) {
    const int function_call = expression != NULL && expression->kind == F2C_EXPR_CALL &&
                              expression->type == TYPE_CHARACTER && expression->text != NULL &&
                              !f2c_is_intrinsic_name(expression->text) &&
                              !f2c_expression_has_descriptor_result(expression);
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

int f2c_statement_is_function_definition(const Unit *unit, size_t statement) {
    const size_t line =
        unit != NULL && statement < unit->statement_count ? unit->statements[statement].line : 0U;
    size_t symbol;
    if (line == 0U)
        return 0;
    for (symbol = 0U; symbol < unit->symbol_count; ++symbol)
        if (unit->symbols[symbol].statement_function &&
            unit->symbols[symbol].statement_function_line == line)
            return 1;
    return 0;
}

static int reserve_temporaries(ExpressionTemporaryAssigner *assigner, size_t count,
                               const F2cSourceSpan *span, const char *purpose, size_t *begin) {
    if (assigner->failed)
        return 0;
    if (count > SIZE_MAX - assigner->next) {
        f2c_diagnostic_span_code(assigner->context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, span, 1,
                                 "%s temporary index space is exhausted", purpose);
        assigner->failed = 1;
        return 0;
    }
    if (begin != NULL)
        *begin = count != 0U ? assigner->next : SIZE_MAX;
    assigner->next += count;
    return 1;
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

int f2c_expression_is_derived_actual_temporary(const F2cExpr *expression) {
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

static void assign_contiguous_actual(ExpressionTemporaryAssigner *assigner, F2cExpr *actual,
                                     int descriptor, int contiguous, int pointer) {
    size_t temporary;
    actual = (F2cExpr *)actual_value(actual);
    if (actual == NULL || actual->rank == 0U || !descriptor || !contiguous || pointer ||
        actual_guaranteed_contiguous(actual) || actual->has_contiguous_temporary)
        return;
    if (reserve_temporaries(assigner, 1U, &actual->span, "contiguous-actual", &temporary)) {
        actual->contiguous_temporary_index = temporary;
        actual->has_contiguous_temporary = 1;
    }
}

static void assign_call_contiguous_actuals(ExpressionTemporaryAssigner *assigner,
                                           F2cExpr *expression) {
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
            assign_contiguous_actual(assigner, actual,
                                     procedure->external_parameter_descriptor[parameter],
                                     procedure->external_parameter_contiguous[parameter],
                                     procedure->external_parameter_pointer[parameter]);
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
            assigner, expression->children[parameter],
            dummy != NULL ? f2c_symbol_uses_descriptor(dummy)
                          : known_external && procedure->external_parameter_descriptor[parameter],
            dummy != NULL ? dummy->contiguous
                          : known_external && procedure->external_parameter_contiguous[parameter],
            dummy != NULL ? dummy->pointer
                          : known_external && procedure->external_parameter_pointer[parameter]);
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
        if (f2c_expression_is_derived_actual_temporary(actual_value(expression->children[child])))
            return 1;
    return 0;
}

static int call_has_managed_lifecycle(const F2cExpr *expression) {
    return call_has_derived_actual(expression) || call_has_contiguous_actual(expression) ||
           (expression != NULL && expression->has_host_descriptor_lifecycle);
}

int f2c_expression_has_materialized_call_result(const F2cExpr *expression) {
    return call_has_managed_lifecycle(expression) &&
           !f2c_expression_has_descriptor_result(expression) && expression->rank == 0U &&
           expression->type != TYPE_UNKNOWN && expression->type != TYPE_CHARACTER &&
           expression->type != TYPE_DERIVED;
}

int f2c_expression_has_materialized_derived_result(const F2cExpr *expression) {
    return call_has_managed_lifecycle(expression) &&
           !f2c_expression_has_descriptor_result(expression) && expression->rank == 0U &&
           expression->type == TYPE_DERIVED && expression->derived_type != NULL;
}

int f2c_expression_has_materialized_descriptor_result(const F2cExpr *expression) {
    return call_has_managed_lifecycle(expression) &&
           f2c_expression_has_descriptor_result(expression);
}

const F2cExpr *f2c_expression_ordered_binary_operand(const F2cExpr *expression) {
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
           operand->type != TYPE_DERIVED && !f2c_expression_has_descriptor_result(operand);
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

static void assign_ordered_call_arguments(ExpressionTemporaryAssigner *assigner,
                                          F2cExpr *expression) {
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
        size_t temporary;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            actual->child_count == 1U)
            actual = actual->children[0];
        if (actual != NULL && actual->has_order_sensitive_call &&
            can_materialize_ordered_operand(actual) &&
            actual->ordered_argument_temporary_index == SIZE_MAX &&
            reserve_temporaries(assigner, 1U, &actual->span, "ordered-argument", &temporary))
            actual->ordered_argument_temporary_index = temporary;
    }
}

static void reset_expression_plan(F2cExpr *expression) {
    expression->temporary_index = SIZE_MAX;
    expression->contiguous_temporary_index = SIZE_MAX;
    expression->has_contiguous_temporary = 0;
    expression->host_descriptor_temporary_begin = SIZE_MAX;
    expression->host_descriptor_temporary_count = 0U;
    expression->has_host_descriptor_lifecycle = 0;
    expression->ordered_temporary_index = SIZE_MAX;
    expression->ordered_argument_temporary_index = SIZE_MAX;
    expression->has_order_sensitive_call = 0;
    expression->statement_temporary_index = SIZE_MAX;
    expression->statement_nested_temporary_begin = SIZE_MAX;
    expression->lifetime_statement_index = SIZE_MAX;
    expression->temporary_lifetime_analyzed = 0;
    expression->owned_temporary_index = SIZE_MAX;
    expression->owned_temporary_kind = F2C_OWNED_TEMPORARY_NONE;
    expression->temporary_ownership_analyzed = 0;
}

static void assign_expression_temporary(F2cExpr *expression, void *state) {
    ExpressionTemporaryAssigner *assigner = (ExpressionTemporaryAssigner *)state;
    const F2cOwnedTemporaryKind ownership = owned_temporary_kind(expression);
    size_t child;
    size_t temporary;
    if (expression == NULL)
        return;
    reset_expression_plan(expression);
    if (f2c_expression_is_character_temporary(expression) &&
        reserve_temporaries(assigner, 1U, &expression->span, "character-result", &temporary))
        expression->temporary_index = temporary;
    if (user_procedure_call(expression)) {
        const size_t first = expression->symbol != NULL && expression->symbol->type_bound ? 1U : 0U;
        for (child = first; child < expression->child_count; ++child) {
            F2cExpr *actual = expression->children[child];
            if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                actual->child_count == 1U)
                actual = actual->children[0];
            if (f2c_expression_is_derived_actual_temporary(actual) &&
                actual->temporary_index == SIZE_MAX &&
                reserve_temporaries(assigner, 1U, &actual->span, "derived-actual", &temporary))
                actual->temporary_index = temporary;
        }
        assign_call_contiguous_actuals(assigner, expression);
        {
            const Unit *procedure = capture_procedure(expression);
            const size_t descriptor_count =
                f2c_host_capture_local_descriptor_count(assigner->unit, procedure);
            expression->has_host_descriptor_lifecycle =
                f2c_host_capture_has_descriptor_lifecycle(assigner->unit, procedure);
            if (descriptor_count != 0U &&
                reserve_temporaries(assigner, descriptor_count, &expression->span,
                                    "host-capture descriptor",
                                    &expression->host_descriptor_temporary_begin))
                expression->host_descriptor_temporary_count = descriptor_count;
        }
        if (f2c_expression_has_materialized_call_result(expression) &&
            expression->temporary_index == SIZE_MAX &&
            reserve_temporaries(assigner, 1U, &expression->span, "call-result", &temporary))
            expression->temporary_index = temporary;
        if (f2c_expression_has_materialized_descriptor_result(expression) &&
            expression->temporary_index == SIZE_MAX &&
            reserve_temporaries(assigner, 1U, &expression->span, "descriptor-result", &temporary))
            expression->temporary_index = temporary;
        if (f2c_expression_has_materialized_derived_result(expression) &&
            expression->statement_temporary_index == SIZE_MAX &&
            reserve_temporaries(assigner, 1U, &expression->span, "derived-result", &temporary))
            expression->statement_temporary_index = temporary;
    }
    assign_ordered_call_arguments(assigner, expression);
    expression->has_order_sensitive_call = user_procedure_call(expression);
    if (call_uses_argument_values(expression))
        for (child = 0U; child < expression->child_count; ++child)
            if (expression->children[child] != NULL &&
                expression->children[child]->has_order_sensitive_call)
                expression->has_order_sensitive_call = 1;
    if (expression->ordered_temporary_index == SIZE_MAX &&
        can_materialize_ordered_operand(f2c_expression_ordered_binary_operand(expression)) &&
        reserve_temporaries(assigner, 1U, &expression->span, "ordered-operand", &temporary))
        expression->ordered_temporary_index = temporary;
    if (ownership != F2C_OWNED_TEMPORARY_NONE &&
        !append_statement_owned_temporary(assigner, expression, ownership)) {
        f2c_diagnostic_span_code(assigner->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &expression->span,
                                 1, "out of memory while planning owned expression temporaries");
        assigner->failed = 1;
    }
    expression->lifetime_statement_index = assigner->statement;
    expression->temporary_lifetime_analyzed = !assigner->failed;
    expression->temporary_ownership_analyzed = !assigner->failed;
}

static size_t statement_function_expansion_count(F2cExpr *expression) {
    size_t count = 0U;
    size_t child;
    Symbol *function;
    if (expression == NULL)
        return 0U;
    for (child = 0U; child < expression->child_count; ++child) {
        const size_t nested = statement_function_expansion_count(expression->children[child]);
        if (nested > SIZE_MAX - count)
            return SIZE_MAX;
        count += nested;
    }
    function = expression->kind == F2C_EXPR_CALL ? expression->symbol : NULL;
    if (function == NULL || !function->statement_function)
        return count;
    if (count == SIZE_MAX)
        return count;
    ++count;
    if (!function->statement_function_expanding &&
        function->statement_function_expression != NULL) {
        size_t nested;
        function->statement_function_expanding = 1;
        nested = statement_function_expansion_count(function->statement_function_expression);
        function->statement_function_expanding = 0;
        if (nested > SIZE_MAX - count)
            return SIZE_MAX;
        count += nested;
    }
    return count;
}

int f2c_relocate_statement_function_temporaries(F2cExpr *expression, size_t *next) {
    size_t child;
    Symbol *function;
    size_t nested;
    if (expression == NULL)
        return 1;
    if (next == NULL)
        return 0;
    for (child = 0U; child < expression->child_count; ++child)
        if (!f2c_relocate_statement_function_temporaries(expression->children[child], next))
            return 0;
    function = expression->kind == F2C_EXPR_CALL ? expression->symbol : NULL;
    if (function == NULL || !function->statement_function)
        return 1;
    if (*next == SIZE_MAX)
        return 0;
    expression->statement_temporary_index = (*next)++;
    expression->statement_nested_temporary_begin = *next;
    nested = statement_function_expansion_count(function->statement_function_expression);
    if (nested == SIZE_MAX || nested > SIZE_MAX - *next)
        return 0;
    *next += nested;
    return 1;
}

static void assign_statement_function_temporary(F2cExpr *expression, void *state) {
    ExpressionTemporaryAssigner *assigner = (ExpressionTemporaryAssigner *)state;
    size_t nested;
    size_t temporary;
    size_t nested_begin;
    if (expression == NULL || expression->kind != F2C_EXPR_CALL || expression->symbol == NULL ||
        !expression->symbol->statement_function)
        return;
    if (!reserve_temporaries(assigner, 1U, &expression->span, "statement-function", &temporary))
        return;
    expression->statement_temporary_index = temporary;
    expression->statement_nested_temporary_begin = assigner->next;
    nested = statement_function_expansion_count(expression->symbol->statement_function_expression);
    if (nested == SIZE_MAX) {
        f2c_diagnostic_span_code(assigner->context, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                                 &expression->span, 1,
                                 "statement-function expansion count overflow");
        assigner->failed = 1;
        return;
    }
    if (nested != 0U && reserve_temporaries(assigner, nested, &expression->span,
                                            "nested statement-function", &nested_begin))
        expression->statement_nested_temporary_begin = nested_begin;
}

int f2c_plan_expression_lifetimes(Context *context, Unit *unit) {
    ExpressionTemporaryAssigner expression_assigner = {context, unit, 0U, 0U, 0};
    ExpressionTemporaryAssigner statement_assigner = {context, unit, 0U, 0U, 0};
    size_t statement;
    if (context == NULL || unit == NULL || unit->phase != F2C_UNIT_TYPED_IR)
        return 0;
    clear_owned_temporary_plans(unit);
    unit->expression_lifetimes_analyzed = 0;
    unit->expression_temporary_count = 0U;
    unit->statement_function_temporary_count = 0U;
    for (statement = 0U; statement < unit->statement_count; ++statement) {
        if (f2c_statement_is_function_definition(unit, statement)) {
            unit->statements[statement].temporary_plan.ownership_analyzed = 1;
            continue;
        }
        expression_assigner.statement = statement;
        f2c_visit_statement_expressions(&unit->statements[statement], assign_expression_temporary,
                                        &expression_assigner);
        if (expression_assigner.failed) {
            clear_owned_temporary_plans(unit);
            return 0;
        }
        unit->statements[statement].temporary_plan.ownership_analyzed = 1;
    }
    for (statement = 0U; statement < unit->statement_count; ++statement) {
        if (f2c_statement_is_function_definition(unit, statement))
            continue;
        statement_assigner.statement = statement;
        f2c_visit_statement_expressions(&unit->statements[statement],
                                        assign_statement_function_temporary, &statement_assigner);
        if (statement_assigner.failed)
            return 0;
    }
    unit->expression_temporary_count = expression_assigner.next;
    unit->statement_function_temporary_count = statement_assigner.next;
    unit->expression_lifetimes_analyzed = 1;
    return 1;
}
