#include "codegen/expression/private.h"
#include "codegen/storage/private.h"

#include "codegen/array/private.h"
#include "codegen/call/private.h"
#include "codegen/descriptor/private.h"
#include "codegen/literal/integer.h"
#include "codegen/lowering/private.h"
#include "ir/call.h"

#include <stdlib.h>
#include <string.h>

char *f2c_expression_emit(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_emit_array_reference(Unit *unit, const F2cExpr *expression, int *supported);

void f2c_expression_append_component(Buffer *output, const char *base,
                                     const F2cDerivedType *dynamic_type, const Symbol *component) {
    const F2cDerivedType *owner = dynamic_type;
    f2c_buffer_printf(output, "(%s)", base);
    while (owner != NULL && owner != component->derived_owner) {
        f2c_buffer_append(output, ".parent");
        owner = owner->parent;
    }
    f2c_buffer_printf(output, ".%s",
                      component->c_name != NULL ? component->c_name : component->name);
}

static const F2cExpr *intrinsic_argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static char *emit_call_body(Unit *unit, const F2cExpr *expression, int *supported) {
    char **arguments = NULL;
    Type *types = NULL;
    Buffer result = {0};
    Buffer call_setup = {0};
    Buffer call_cleanup = {0};
    const Unit *resolved = expression->resolved_procedure != NULL &&
                                   !expression->resolved_procedure->interface_abstract
                               ? expression->resolved_procedure
                               : NULL;
    const Unit *capture_procedure =
        resolved != NULL && resolved->internal
            ? resolved
            : (expression->symbol != NULL && expression->symbol->procedure_interface != NULL &&
                       expression->symbol->procedure_interface->internal
                   ? expression->symbol->procedure_interface
                   : NULL);
    const char *callee =
        resolved != NULL && resolved->name != NULL
            ? resolved->name
            : (expression->symbol != NULL ? f2c_symbol_c_name(unit, expression->symbol)
                                          : expression->text);
    const int descriptor_result = f2c_expression_has_descriptor_result(expression);
    const int intrinsic_call =
        (expression->intrinsic != F2C_INTRINSIC_NONE ||
         (expression->text != NULL && f2c_is_intrinsic_name(expression->text))) &&
        (expression->symbol == NULL || !expression->symbol->external_declared);
    size_t i;
    size_t derived_actual_count = 0U;
    if (expression->symbol != NULL && expression->symbol->type_bound)
        return f2c_call_bound_expression(unit, expression, supported);
    if (expression->symbol != NULL && expression->symbol->statement_function)
        return f2c_expression_statement_function(unit, expression, supported);
    if (intrinsic_call && expression->intrinsic == F2C_INTRINSIC_FINDLOC &&
        expression->rank == 0U) {
        int64_t constant;
        if (f2c_evaluate_integer_constant(unit, expression, &constant))
            return f2c_integer_constant_literal(constant, expression->type_kind);
    }
    if (intrinsic_call && expression->intrinsic == F2C_INTRINSIC_ETIME)
        return f2c_expression_etime(unit, expression, supported);
    if (intrinsic_call && f2c_intrinsic_is_bit(expression->intrinsic) &&
        expression->intrinsic != F2C_INTRINSIC_MVBITS)
        return f2c_expression_bit_intrinsic(unit, expression, supported);
    if (intrinsic_call && f2c_intrinsic_is_character(expression->intrinsic))
        return f2c_expression_character_intrinsic(unit, expression, supported);
    if (intrinsic_call && f2c_intrinsic_is_conversion(expression->intrinsic))
        return f2c_expression_conversion_intrinsic(unit, expression, supported);
    if (intrinsic_call && f2c_intrinsic_is_mathematical(expression->intrinsic))
        return f2c_expression_mathematical_intrinsic(unit, expression, supported);
    if (intrinsic_call && f2c_intrinsic_is_numeric_model(expression->intrinsic))
        return f2c_expression_numeric_model_intrinsic(unit, expression, supported);
    if (intrinsic_call && f2c_intrinsic_is_numeric_operation(expression->intrinsic))
        return f2c_expression_numeric_operation_intrinsic(unit, expression, supported);
    if (intrinsic_call && f2c_intrinsic_is_real_representation(expression->intrinsic))
        return f2c_expression_real_representation_intrinsic(unit, expression, supported);
    if (expression->intrinsic == F2C_INTRINSIC_PRESENT && expression->child_count == 1U &&
        expression->children[0] != NULL && expression->children[0]->kind == F2C_EXPR_NAME &&
        expression->children[0]->symbol != NULL) {
        const Symbol *present_symbol = expression->children[0]->symbol;
        if (present_symbol->allocatable || present_symbol->pointer)
            f2c_buffer_printf(&result, "(f2c_descriptor_%s != NULL)",
                              f2c_symbol_c_name(unit, present_symbol));
        else
            f2c_buffer_printf(&result, "(%s != NULL)", f2c_symbol_c_name(unit, present_symbol));
        return f2c_buffer_take(&result);
    }
    if (expression->intrinsic == F2C_INTRINSIC_ALLOCATED && expression->child_count == 1U &&
        expression->children[0] != NULL &&
        (expression->children[0]->kind == F2C_EXPR_NAME ||
         expression->children[0]->kind == F2C_EXPR_COMPONENT) &&
        expression->children[0]->symbol != NULL && expression->children[0]->symbol->allocatable) {
        const F2cStorageReference reference = f2c_ir_storage_reference(expression->children[0]);
        char *storage = f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
        if (storage == NULL) {
            *supported = 0;
            free(storage);
            return NULL;
        }
        f2c_buffer_printf(&result, "(%s != NULL)", storage);
        free(storage);
        return f2c_buffer_take(&result);
    }
    if (expression->intrinsic == F2C_INTRINSIC_ASSOCIATED && expression->child_count >= 1U &&
        expression->child_count <= 2U &&
        f2c_intrinsic_argument(expression->children, expression->child_count, "pointer", 0U) !=
            NULL) {
        const F2cExpr *pointer_expression =
            f2c_intrinsic_argument(expression->children, expression->child_count, "pointer", 0U);
        const F2cExpr *target_expression =
            f2c_intrinsic_argument(expression->children, expression->child_count, "target", 1U);
        const Symbol *pointer = pointer_expression->symbol;
        const F2cStorageReference reference = f2c_ir_storage_reference(pointer_expression);
        char *pointer_storage =
            pointer != NULL && pointer->procedure_pointer
                ? f2c_expression_emit(unit, pointer_expression, supported)
                : f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
        if (pointer == NULL || (!pointer->pointer && !pointer->procedure_pointer) || !*supported ||
            pointer_storage == NULL) {
            free(pointer_storage);
            *supported = 0;
            return NULL;
        }
        if (target_expression == NULL) {
            f2c_buffer_printf(&result, "(%s != NULL)", pointer_storage);
        } else if (pointer->procedure_pointer && target_expression->kind == F2C_EXPR_NAME &&
                   target_expression->symbol != NULL) {
            f2c_buffer_printf(&result, "(%s == %s)", pointer_storage,
                              f2c_symbol_c_name(unit, target_expression->symbol));
        } else if (pointer->rank == 0U) {
            char *target_storage =
                f2c_expression_associated_scalar_target(unit, target_expression, supported);
            char *pointer_length = NULL;
            char *target_length = NULL;
            if (!*supported || target_storage == NULL) {
                free(target_storage);
                free(pointer_storage);
                *supported = 0;
                return NULL;
            }
            if (pointer->type == TYPE_CHARACTER) {
                pointer_length = f2c_character_length_expression(unit, pointer_expression);
                target_length = f2c_character_length_expression(unit, target_expression);
                if (pointer_length == NULL || target_length == NULL) {
                    free(pointer_length);
                    free(target_length);
                    free(target_storage);
                    free(pointer_storage);
                    *supported = 0;
                    return NULL;
                }
                f2c_buffer_printf(&result,
                                  "f2c_character_associated_target(%s, (size_t)(%s), "
                                  "%s, (size_t)(%s))",
                                  pointer_storage, pointer_length, target_storage, target_length);
            } else {
                f2c_buffer_printf(&result, "((const void *)(%s) == (const void *)(%s))",
                                  pointer_storage, target_storage);
            }
            free(pointer_length);
            free(target_length);
            free(target_storage);
        } else {
            char *association = f2c_expression_associated_array_target(
                unit, pointer_expression, target_expression, pointer_storage, supported);
            char *pointer_length = NULL;
            char *target_length = NULL;
            if (!*supported || association == NULL) {
                free(association);
                free(pointer_storage);
                *supported = 0;
                return NULL;
            }
            if (pointer->type == TYPE_CHARACTER) {
                pointer_length = f2c_character_length_expression(unit, pointer_expression);
                target_length = f2c_character_length_expression(unit, target_expression);
                if (pointer_length == NULL || target_length == NULL) {
                    free(pointer_length);
                    free(target_length);
                    free(association);
                    free(pointer_storage);
                    *supported = 0;
                    return NULL;
                }
                f2c_buffer_printf(
                    &result, "(f2c_character_target_lengths((size_t)(%s), (size_t)(%s)) && %s)",
                    pointer_length, target_length, association);
            } else {
                f2c_buffer_append(&result, association);
            }
            free(pointer_length);
            free(target_length);
            free(association);
        }
        free(pointer_storage);
        return f2c_buffer_take(&result);
    }
    if (expression->intrinsic == F2C_INTRINSIC_SIZE ||
        ((expression->intrinsic == F2C_INTRINSIC_LBOUND ||
          expression->intrinsic == F2C_INTRINSIC_UBOUND) &&
         expression->rank == 0U))
        return f2c_expression_array_inquiry(unit, expression, supported);
    {
        int matched = 0;
        char *reduction = f2c_expression_relation_reduction(unit, expression, supported, &matched);
        if (matched)
            return reduction;
    }
    if (f2c_intrinsic_is_reduction(expression->intrinsic))
        return f2c_expression_reduction_intrinsic(unit, expression, supported);
    if (intrinsic_call && expression->intrinsic == F2C_INTRINSIC_TRANSFER && expression->rank == 0U)
        return f2c_expression_transfer_intrinsic(unit, expression, supported);
    if (!f2c_expression_children(unit, expression, &arguments, &types)) {
        *supported = 0;
        return NULL;
    }
    if (intrinsic_call) {
        char *intrinsic = f2c_emit_intrinsic(expression->text, expression->intrinsic, arguments,
                                             types, expression->child_count, expression->type);
        f2c_expression_free_arguments(arguments, types, expression->child_count);
        return intrinsic;
    }
    for (i = 0U; i < expression->child_count; ++i) {
        const F2cExpr *actual = intrinsic_argument_value(expression->children[i]);
        if (actual != NULL && actual->type == TYPE_DERIVED && actual->derived_type != NULL &&
            actual->rank == 0U && !actual->definable) {
            if (actual->temporary_index == SIZE_MAX) {
                f2c_expression_free_arguments(arguments, types, expression->child_count);
                *supported = 0;
                return NULL;
            }
            ++derived_actual_count;
        }
    }
    if (derived_actual_count != 0U &&
        ((expression->type == TYPE_DERIVED ? expression->statement_temporary_index == SIZE_MAX
                                           : expression->temporary_index == SIZE_MAX) ||
         expression->rank != 0U || expression->type == TYPE_UNKNOWN || descriptor_result)) {
        f2c_expression_free_arguments(arguments, types, expression->child_count);
        *supported = 0;
        return NULL;
    }
    if (derived_actual_count != 0U && expression->type == TYPE_DERIVED)
        f2c_buffer_printf(&result,
                          "(f2c_materialize_move_%s(&f2c_derived_result_%zu, "
                          "&f2c_derived_result_live_%zu, ",
                          expression->derived_type->c_name, expression->statement_temporary_index,
                          expression->statement_temporary_index);
    else if (derived_actual_count != 0U && expression->type != TYPE_CHARACTER)
        f2c_buffer_printf(&result, "(f2c_expression_result_%zu = ", expression->temporary_index);
    if (expression->type == TYPE_CHARACTER && !descriptor_result) {
        char *result_length;
        if (expression->temporary_index == SIZE_MAX) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            *supported = 0;
            return NULL;
        }
        result_length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(&result,
                          "(f2c_character_result_%zu = f2c_character_temporary_resize("
                          "f2c_character_result_%zu, (size_t)(%s)), "
                          "%s(f2c_character_result_%zu, (size_t)(%s)",
                          expression->temporary_index, expression->temporary_index,
                          result_length != NULL ? result_length : "1U",
                          callee != NULL ? callee : "", expression->temporary_index,
                          result_length != NULL ? result_length : "1U");
        free(result_length);
    } else {
        f2c_buffer_printf(&result, "%s(", callee != NULL ? callee : "");
    }
    for (i = 0U; i < expression->child_count; ++i) {
        const Symbol *resolved_dummy =
            resolved != NULL && i < resolved->argument_count
                ? f2c_find_symbol((Unit *)resolved, resolved->arguments[i])
                : NULL;
        const int descriptor =
            resolved_dummy != NULL
                ? f2c_symbol_uses_descriptor(resolved_dummy)
                : (expression->symbol != NULL && i < expression->symbol->external_parameter_count &&
                   expression->symbol->external_parameter_descriptor[i]);
        const F2cIntent intent =
            resolved_dummy != NULL
                ? resolved_dummy->value ? F2C_INTENT_IN : resolved_dummy->intent
                : (expression->symbol != NULL && i < expression->symbol->external_parameter_count
                       ? expression->symbol->external_parameter_value[i]
                             ? F2C_INTENT_IN
                             : expression->symbol->external_parameter_intents[i]
                       : F2C_INTENT_UNSPECIFIED);
        const F2cExpr *actual_expression = intrinsic_argument_value(expression->children[i]);
        if (actual_expression != NULL && actual_expression->symbol != NULL &&
            actual_expression->symbol->equivalence_unaligned && intent != F2C_INTENT_IN) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            free(f2c_buffer_take(&result));
            free(call_setup.data);
            free(call_cleanup.data);
            *supported = 0;
            return NULL;
        }
        char *actual = descriptor ? f2c_expression_descriptor_actual(&call_setup, &call_cleanup,
                                                                     unit, expression->children[i],
                                                                     intent, supported)
                                  : f2c_call_emit_actual_address(unit, expression->children[i],
                                                                 arguments[i], supported);
        char *bridged;
        if (actual == NULL) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            free(f2c_buffer_take(&result));
            free(call_setup.data);
            free(call_cleanup.data);
            *supported = 0;
            return NULL;
        }
        bridged = f2c_bridge_implicit_mutable_actual(expression->symbol, i, expression->children[i],
                                                     actual);
        free(actual);
        actual = bridged;
        if (actual == NULL) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            free(f2c_buffer_take(&result));
            free(call_setup.data);
            free(call_cleanup.data);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(
            &result, "%s%s",
            i == 0U && (expression->type != TYPE_CHARACTER || descriptor_result) ? "" : ", ",
            actual);
        free(actual);
    }
    if (!f2c_emit_host_capture_expression_descriptors(
            &call_setup, &call_cleanup, unit, capture_procedure,
            expression->host_descriptor_temporary_begin) ||
        !f2c_emit_host_capture_expression_actuals(
            &result, unit, capture_procedure, expression->host_descriptor_temporary_begin,
            expression->child_count != 0U ||
                (expression->type == TYPE_CHARACTER && !descriptor_result))) {
        f2c_expression_free_arguments(arguments, types, expression->child_count);
        free(f2c_buffer_take(&result));
        free(call_setup.data);
        free(call_cleanup.data);
        *supported = 0;
        return NULL;
    }
    for (i = 0U; i < expression->child_count; ++i) {
        const F2cExpr *actual = expression->children[i];
        const Symbol *resolved_dummy =
            resolved != NULL && i < resolved->argument_count
                ? f2c_find_symbol((Unit *)resolved, resolved->arguments[i])
                : NULL;
        const int descriptor =
            resolved_dummy != NULL
                ? f2c_symbol_uses_descriptor(resolved_dummy)
                : (expression->symbol != NULL && i < expression->symbol->external_parameter_count &&
                   expression->symbol->external_parameter_descriptor[i]);
        char *length;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            actual->child_count == 1U)
            actual = actual->children[0];
        if (actual == NULL || actual->type != TYPE_CHARACTER || descriptor ||
            (actual->kind == F2C_EXPR_NAME && actual->symbol != NULL && actual->symbol->external))
            continue;
        length = f2c_character_length_expression(unit, actual);
        f2c_buffer_printf(&result, ", %s", length != NULL ? length : "1U");
        free(length);
    }
    if (!f2c_emit_host_capture_lengths(&result, unit, capture_procedure)) {
        f2c_expression_free_arguments(arguments, types, expression->child_count);
        free(f2c_buffer_take(&result));
        free(call_setup.data);
        free(call_cleanup.data);
        *supported = 0;
        return NULL;
    }
    if (expression->type == TYPE_CHARACTER && !descriptor_result) {
        char *result_length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(&result, "), f2c_character_result_%zu[(size_t)(%s)] = '\\0'",
                          expression->temporary_index,
                          result_length != NULL ? result_length : "1U");
        if (derived_actual_count != 0U)
            f2c_expression_append_derived_actual_releases(&result, expression, 0U);
        f2c_buffer_printf(&result, ", f2c_character_result_%zu)", expression->temporary_index);
        free(result_length);
    } else {
        f2c_buffer_append(&result, ")");
        if (derived_actual_count != 0U && expression->type == TYPE_DERIVED)
            f2c_buffer_append(&result, ")");
    }
    if (derived_actual_count != 0U && expression->type != TYPE_CHARACTER) {
        f2c_expression_append_derived_actual_releases(&result, expression, 0U);
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
    f2c_expression_free_arguments(arguments, types, expression->child_count);
    return f2c_expression_wrap_managed_call(expression, descriptor_result, &call_setup,
                                            &call_cleanup, f2c_buffer_take(&result), supported);
}

char *f2c_expression_call(Unit *unit, const F2cExpr *expression, int *supported) {
    F2cExpr *lowering_expression;
    Buffer setup = {0};
    Buffer cleanup = {0};
    size_t argument;
    char *call;
    if (expression == NULL || supported == NULL)
        return NULL;
    lowering_expression = f2c_array_clone_expression(unit, expression);
    if (lowering_expression == NULL) {
        *supported = 0;
        return NULL;
    }
    for (argument = 0U; argument < lowering_expression->child_count; ++argument) {
        F2cExpr *actual =
            (F2cExpr *)intrinsic_argument_value(lowering_expression->children[argument]);
        const F2cExpr *mold =
            lowering_expression->intrinsic == F2C_INTRINSIC_TRANSFER
                ? f2c_intrinsic_argument(lowering_expression->children,
                                         lowering_expression->child_count, "mold", 1U)
                : NULL;
        Buffer name = {0};
        char *code;
        if (actual == NULL || actual == mold ||
            actual->ordered_argument_temporary_index == SIZE_MAX ||
            f2c_lowering_argument_materialized(unit, actual))
            continue;
        code = f2c_expression_emit(unit, actual, supported);
        if (!*supported || code == NULL) {
            free(code);
            goto failed;
        }
        f2c_buffer_printf(&name, "f2c_ordered_argument_%zu",
                          actual->ordered_argument_temporary_index);
        if (name.data == NULL) {
            free(code);
            *supported = 0;
            goto failed;
        }
        f2c_buffer_printf(&setup, "%s = (%s), ", name.data, code);
        free(code);
        if (setup.failed) {
            free(name.data);
            *supported = 0;
            goto failed;
        }
        if (!f2c_lowering_take_code(unit, actual, f2c_buffer_take(&name)) ||
            !f2c_lowering_set_argument_materialized(unit, actual, 1)) {
            *supported = 0;
            goto failed;
        }
    }
    call = emit_call_body(unit, lowering_expression, supported);
    f2c_codegen_expression_free(unit, lowering_expression);
    if (!*supported || call == NULL) {
        free(call);
        free(setup.data);
        return NULL;
    }
    return f2c_expression_wrap_managed_call(expression, 0, &setup, &cleanup, call, supported);

failed:
    f2c_codegen_expression_free(unit, lowering_expression);
    free(setup.data);
    return NULL;
}
