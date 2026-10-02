#include "codegen/call/private.h"

#include "codegen/expression/private.h"
#include "ir/call.h"

#include <stdlib.h>

char *f2c_call_bound_expression(Unit *unit, const F2cExpr *expression, int *supported) {
    const Symbol *procedure = expression->symbol;
    const F2cExpr *callee_expression =
        expression->child_count != 0U ? expression->children[0] : NULL;
    const F2cExpr *passed_object = f2c_call_passed_object(expression);
    const int descriptor_result = f2c_procedure_has_descriptor_result(procedure);
    const int character_result = procedure != NULL && !descriptor_result &&
                                 !procedure->external_subroutine &&
                                 procedure->type == TYPE_CHARACTER;
    Buffer result = {0};
    Buffer call_setup = {0};
    Buffer call_cleanup = {0};
    char *callee;
    size_t parameter;
    size_t derived_actual_count = 0U;
    if (procedure == NULL || !procedure->type_bound || callee_expression == NULL ||
        passed_object == NULL) {
        *supported = 0;
        return NULL;
    }
    for (parameter = 1U; parameter < expression->child_count; ++parameter) {
        const F2cExpr *actual =
            f2c_call_parameter_actual(expression, f2c_call_child_parameter(expression, parameter));
        if (actual != NULL && actual->type == TYPE_DERIVED && actual->derived_type != NULL &&
            actual->rank == 0U && !actual->definable) {
            if (actual->temporary_index == SIZE_MAX) {
                *supported = 0;
                return NULL;
            }
            ++derived_actual_count;
        }
    }
    if (derived_actual_count != 0U &&
        (descriptor_result ||
         (expression->type == TYPE_DERIVED ? expression->statement_temporary_index == SIZE_MAX
                                           : expression->temporary_index == SIZE_MAX) ||
         expression->rank != 0U || expression->type == TYPE_UNKNOWN)) {
        *supported = 0;
        return NULL;
    }
    callee = f2c_expression_emit(unit, callee_expression, supported);
    if (!*supported || callee == NULL)
        return NULL;
    if (derived_actual_count != 0U && expression->type == TYPE_DERIVED)
        f2c_buffer_printf(&result,
                          "(f2c_materialize_move_%s(&f2c_derived_result_%zu, "
                          "&f2c_derived_result_live_%zu, ",
                          expression->derived_type->c_name, expression->statement_temporary_index,
                          expression->statement_temporary_index);
    else if (derived_actual_count != 0U && !character_result)
        f2c_buffer_printf(&result, "(f2c_expression_result_%zu = ", expression->temporary_index);
    if (character_result) {
        char *result_length;
        if (expression->temporary_index == SIZE_MAX) {
            free(callee);
            *supported = 0;
            return NULL;
        }
        result_length = f2c_character_length_expression(unit, expression);
        if (result_length == NULL) {
            free(callee);
            free(result.data);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result,
                          "(f2c_character_result_%zu = f2c_character_temporary_resize("
                          "f2c_character_result_%zu, (size_t)(%s)), "
                          "%s(f2c_character_result_%zu, (size_t)(%s)",
                          expression->temporary_index, expression->temporary_index, result_length,
                          callee, expression->temporary_index, result_length);
        free(result_length);
    } else {
        f2c_buffer_printf(&result, "%s(", callee);
    }
    for (parameter = 0U; parameter < procedure->external_parameter_count; ++parameter) {
        const F2cExpr *actual;
        char *code;
        char *lowered;
        actual = f2c_call_parameter_actual(expression, parameter);
        if (actual == NULL) {
            free(callee);
            free(result.data);
            free(call_setup.data);
            free(call_cleanup.data);
            *supported = 0;
            return NULL;
        }
        code = f2c_expression_emit(unit, actual, supported);
        if (actual != NULL && actual->symbol != NULL && actual->symbol->equivalence_unaligned &&
            !procedure->external_parameter_value[parameter] &&
            procedure->external_parameter_intents[parameter] != F2C_INTENT_IN) {
            free(code);
            free(callee);
            free(result.data);
            free(call_setup.data);
            free(call_cleanup.data);
            *supported = 0;
            return NULL;
        }
        lowered = *supported && code != NULL
                      ? (procedure->external_parameter_descriptor[parameter]
                             ? f2c_expression_descriptor_actual(
                                   &call_setup, &call_cleanup, unit, actual,
                                   procedure->external_parameter_value[parameter]
                                       ? F2C_INTENT_IN
                                       : procedure->external_parameter_intents[parameter],
                                   supported)
                             : f2c_call_emit_actual_address(unit, actual, code, supported))
                      : NULL;
        free(code);
        if (lowered == NULL) {
            free(callee);
            free(result.data);
            free(call_setup.data);
            free(call_cleanup.data);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, "%s%s", parameter == 0U && !character_result ? "" : ", ",
                          lowered);
        free(lowered);
    }
    for (parameter = 0U; parameter < procedure->external_parameter_count; ++parameter) {
        const F2cExpr *actual;
        char *length;
        if (procedure->external_parameter_types[parameter] != TYPE_CHARACTER ||
            procedure->external_parameter_allocatable[parameter] ||
            procedure->external_parameter_pointer[parameter] ||
            procedure->external_parameter_descriptor[parameter])
            continue;
        actual = f2c_call_parameter_actual(expression, parameter);
        length = actual != NULL ? f2c_character_length_expression(unit, actual) : NULL;
        if (length == NULL) {
            free(callee);
            free(result.data);
            free(call_setup.data);
            free(call_cleanup.data);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, ", %s", length);
        free(length);
    }
    if (character_result) {
        char *result_length = f2c_character_length_expression(unit, expression);
        if (result_length == NULL) {
            free(callee);
            free(result.data);
            free(call_setup.data);
            free(call_cleanup.data);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, "), f2c_character_result_%zu[(size_t)(%s)] = '\\0'",
                          expression->temporary_index, result_length);
        if (derived_actual_count != 0U)
            f2c_expression_append_derived_actual_releases(&result, expression, 1U);
        f2c_buffer_printf(&result, ", f2c_character_result_%zu)", expression->temporary_index);
        free(result_length);
    } else {
        f2c_buffer_append(&result, ")");
        if (derived_actual_count != 0U && expression->type == TYPE_DERIVED)
            f2c_buffer_append(&result, ")");
    }
    if (derived_actual_count != 0U && !character_result) {
        f2c_expression_append_derived_actual_releases(&result, expression, 1U);
        if (expression->type == TYPE_DERIVED)
            f2c_buffer_printf(&result,
                              ", f2c_take_%s(&f2c_derived_result_%zu, "
                              "&f2c_derived_result_live_%zu))",
                              expression->derived_type->c_name,
                              expression->statement_temporary_index,
                              expression->statement_temporary_index);
        else
            f2c_buffer_printf(&result, ", f2c_expression_result_%zu)", expression->temporary_index);
    }
    {
        char *call = f2c_buffer_take(&result);
        free(callee);
        return f2c_expression_wrap_managed_call(expression, descriptor_result, &call_setup,
                                                &call_cleanup, call, supported);
    }
}
