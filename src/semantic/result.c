#include "semantic/semantic.h"

#include "internal/f2c.h"

static const Symbol *function_result(const Unit *unit) {
    return unit != NULL && unit->kind == UNIT_FUNCTION && unit->result_name != NULL
               ? f2c_find_symbol((Unit *)unit, unit->result_name)
               : NULL;
}

int f2c_unit_has_descriptor_result(const Unit *unit) {
    const Symbol *result = function_result(unit);
    return result != NULL && (result->allocatable || result->pointer || result->rank != 0U);
}

int f2c_procedure_has_descriptor_result(const Symbol *procedure) {
    return procedure != NULL && !procedure->external_subroutine &&
           (procedure->external_result_allocatable || procedure->external_result_pointer ||
            procedure->external_result_rank != 0U);
}

int f2c_expression_has_allocatable_result(const F2cExpr *expression) {
    const Unit *procedure = expression != NULL ? expression->resolved_procedure : NULL;
    const Symbol *result = function_result(procedure);
    return (result != NULL && result->allocatable) ||
           (expression != NULL && expression->symbol != NULL &&
            expression->symbol->external_result_allocatable);
}

int f2c_expression_has_descriptor_result(const F2cExpr *expression) {
    const Unit *procedure = expression != NULL ? expression->resolved_procedure : NULL;
    const Symbol *result = function_result(procedure);
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           ((result != NULL && (result->allocatable || result->pointer || result->rank != 0U)) ||
            f2c_procedure_has_descriptor_result(expression->symbol));
}
