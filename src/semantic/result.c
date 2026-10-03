#include "semantic/semantic.h"

#include "internal/f2c.h"

static const Symbol *function_result(const Unit *unit) {
    return unit != NULL && unit->kind == UNIT_FUNCTION && unit->result_name != NULL
               ? f2c_find_symbol((Unit *)unit, unit->result_name)
               : NULL;
}

F2cFunctionResultKind f2c_unit_result_kind(const Unit *unit) {
    const Symbol *result = function_result(unit);
    if (result == NULL)
        return F2C_FUNCTION_RESULT_NONE;
    return result->pointer       ? F2C_FUNCTION_RESULT_POINTER
           : result->allocatable ? F2C_FUNCTION_RESULT_ALLOCATABLE
           : result->rank != 0U  ? F2C_FUNCTION_RESULT_ARRAY_VALUE
                                 : F2C_FUNCTION_RESULT_SCALAR_VALUE;
}

F2cFunctionResultKind f2c_procedure_result_kind(const Symbol *procedure) {
    if (procedure == NULL || procedure->external_subroutine)
        return F2C_FUNCTION_RESULT_NONE;
    return procedure->external_result_pointer       ? F2C_FUNCTION_RESULT_POINTER
           : procedure->external_result_allocatable ? F2C_FUNCTION_RESULT_ALLOCATABLE
           : procedure->external_result_rank != 0U  ? F2C_FUNCTION_RESULT_ARRAY_VALUE
                                                    : F2C_FUNCTION_RESULT_SCALAR_VALUE;
}

int f2c_result_kind_uses_descriptor(F2cFunctionResultKind kind) {
    return kind == F2C_FUNCTION_RESULT_ARRAY_VALUE || kind == F2C_FUNCTION_RESULT_ALLOCATABLE ||
           kind == F2C_FUNCTION_RESULT_POINTER;
}

int f2c_result_kind_owns_storage(F2cFunctionResultKind kind) {
    return kind == F2C_FUNCTION_RESULT_ARRAY_VALUE || kind == F2C_FUNCTION_RESULT_ALLOCATABLE;
}

void f2c_bind_expression_result(F2cExpr *expression) {
    if (expression == NULL || expression->kind != F2C_EXPR_CALL)
        return;
    expression->result_kind = f2c_unit_result_kind(expression->resolved_procedure);
    if (expression->result_kind == F2C_FUNCTION_RESULT_NONE)
        expression->result_kind = f2c_procedure_result_kind(expression->symbol);
    if (expression->result_kind == F2C_FUNCTION_RESULT_POINTER) {
        expression->value_category = F2C_VALUE_VARIABLE;
        expression->definable = 1;
    }
}

int f2c_unit_has_descriptor_result(const Unit *unit) {
    return f2c_result_kind_uses_descriptor(f2c_unit_result_kind(unit));
}

int f2c_procedure_has_descriptor_result(const Symbol *procedure) {
    return f2c_result_kind_uses_descriptor(f2c_procedure_result_kind(procedure));
}

int f2c_expression_has_allocatable_result(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           expression->result_kind == F2C_FUNCTION_RESULT_ALLOCATABLE;
}

int f2c_expression_has_pointer_result(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           expression->result_kind == F2C_FUNCTION_RESULT_POINTER;
}

int f2c_expression_has_descriptor_result(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           f2c_result_kind_uses_descriptor(expression->result_kind);
}
