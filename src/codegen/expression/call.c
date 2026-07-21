#include "codegen/expression/private.h"

#include "codegen/array/private.h"
#include "codegen/descriptor/private.h"

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

static char *emit_external_actual(Unit *unit, const F2cExpr *actual, const char *code,
                                  int *supported) {
    Buffer result = {0};
    Symbol *symbol;
    if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        actual = actual->children[0];
    if (actual == NULL)
        return NULL;
    if (actual->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return f2c_strdup("NULL");
    symbol = actual->symbol;
    if (symbol != NULL && symbol->equivalence_unaligned) {
        if (actual->rank != 0U ||
            (actual->kind != F2C_EXPR_NAME && actual->kind != F2C_EXPR_ARRAY_REFERENCE)) {
            *supported = 0;
            return NULL;
        }
        return f2c_emit_scalar_temporary_address(f2c_symbol_c_type(symbol), symbol->type, code);
    }
    if (actual->lowered_c != NULL && actual->kind == F2C_EXPR_NAME && actual->symbol == NULL &&
        actual->value_category == F2C_VALUE_VARIABLE) {
        f2c_buffer_printf(&result, "&(%s)", code);
        return f2c_buffer_take(&result);
    }
    if (actual->lowered_c != NULL)
        return f2c_strdup(code);
    if (actual->kind == F2C_EXPR_NAME && symbol != NULL) {
        if (symbol->parameter) {
            if (symbol->type == TYPE_CHARACTER)
                return f2c_strdup(code);
            return f2c_emit_scalar_temporary_address(f2c_symbol_c_type(symbol), symbol->type, code);
        }
        if (symbol->external && symbol->external_declared)
            return f2c_strdup(f2c_symbol_c_name(unit, symbol));
        if (symbol->argument || symbol->rank != 0U ||
            (symbol->type == TYPE_CHARACTER && symbol->character_length != NULL))
            return f2c_strdup(f2c_symbol_c_name(unit, symbol));
        f2c_buffer_printf(&result, "&%s", f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&result);
    }
    if (actual->kind == F2C_EXPR_ARRAY_REFERENCE) {
        f2c_buffer_printf(&result, "&%s", code);
        return f2c_buffer_take(&result);
    }
    if (actual->kind == F2C_EXPR_SUBSTRING) {
        if (code[0] == '(' && code[1] == '&')
            return f2c_strdup(code);
        f2c_buffer_printf(&result, "&%s", code);
        return f2c_buffer_take(&result);
    }
    if (actual->type == TYPE_CHARACTER)
        return f2c_strdup(code);
    if (actual->type == TYPE_DERIVED && !actual->definable)
        return f2c_expression_derived_actual_pointer(unit, actual, supported);
    return f2c_emit_scalar_temporary_address(
        actual->type != TYPE_UNKNOWN ? f2c_expression_c_type(actual) : f2c_c_type(TYPE_REAL),
        actual->type != TYPE_UNKNOWN ? actual->type : TYPE_REAL, code);
}

static char *emit_type_bound_call(Unit *unit, const F2cExpr *expression, int *supported) {
    const Symbol *procedure = expression->symbol;
    const F2cExpr *callee_expression =
        expression->child_count != 0U ? expression->children[0] : NULL;
    const F2cExpr *passed_object = callee_expression != NULL &&
                                           callee_expression->kind == F2C_EXPR_COMPONENT &&
                                           callee_expression->child_count != 0U
                                       ? callee_expression->children[0]
                                       : NULL;
    const int allocatable_result = procedure != NULL && !procedure->external_subroutine &&
                                   procedure->external_result_allocatable;
    const int character_result = procedure != NULL && !allocatable_result &&
                                 !procedure->external_subroutine &&
                                 procedure->type == TYPE_CHARACTER;
    Buffer result = {0};
    Buffer contiguous_setup = {0};
    Buffer contiguous_cleanup = {0};
    char *callee;
    size_t parameter;
    size_t explicit_argument = 1U;
    size_t derived_actual_count = 0U;
    if (procedure == NULL || !procedure->type_bound || callee_expression == NULL ||
        passed_object == NULL) {
        *supported = 0;
        return NULL;
    }
    for (parameter = 1U; parameter < expression->child_count; ++parameter) {
        const F2cExpr *actual = intrinsic_argument_value(expression->children[parameter]);
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
        (allocatable_result ||
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
        f2c_buffer_printf(&result,
                          "(f2c_character_result_%zu = f2c_character_temporary_resize("
                          "f2c_character_result_%zu, (size_t)(%s)), "
                          "%s(f2c_character_result_%zu, (size_t)(%s)",
                          expression->temporary_index, expression->temporary_index,
                          result_length != NULL ? result_length : "1U", callee,
                          expression->temporary_index,
                          result_length != NULL ? result_length : "1U");
        free(result_length);
    } else {
        f2c_buffer_printf(&result, "%s(", callee);
    }
    for (parameter = 0U; parameter < procedure->external_parameter_count; ++parameter) {
        const F2cExpr *actual;
        char *code;
        char *lowered;
        if (!procedure->type_bound_nopass && parameter == procedure->type_bound_pass_index) {
            actual = passed_object;
        } else {
            if (explicit_argument >= expression->child_count) {
                free(callee);
                free(result.data);
                free(contiguous_setup.data);
                free(contiguous_cleanup.data);
                *supported = 0;
                return NULL;
            }
            actual = expression->children[explicit_argument++];
        }
        actual = intrinsic_argument_value(actual);
        code = f2c_expression_emit(unit, actual, supported);
        if (actual != NULL && actual->symbol != NULL && actual->symbol->equivalence_unaligned &&
            procedure->external_parameter_intents[parameter] != F2C_INTENT_IN) {
            free(code);
            free(callee);
            free(result.data);
            free(contiguous_setup.data);
            free(contiguous_cleanup.data);
            *supported = 0;
            return NULL;
        }
        lowered = *supported && code != NULL
                      ? (procedure->external_parameter_descriptor[parameter]
                             ? f2c_expression_descriptor_actual(
                                   &contiguous_setup, &contiguous_cleanup, unit, actual,
                                   procedure->external_parameter_intents[parameter], supported)
                             : emit_external_actual(unit, actual, code, supported))
                      : NULL;
        free(code);
        if (lowered == NULL) {
            free(callee);
            free(result.data);
            free(contiguous_setup.data);
            free(contiguous_cleanup.data);
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
        if (!procedure->type_bound_nopass && parameter == procedure->type_bound_pass_index)
            actual = passed_object;
        else {
            size_t index = parameter + 1U;
            if (!procedure->type_bound_nopass && parameter > procedure->type_bound_pass_index)
                --index;
            actual = index < expression->child_count ? expression->children[index] : NULL;
        }
        length = actual != NULL ? f2c_character_length_expression(unit, actual) : NULL;
        f2c_buffer_printf(&result, ", %s", length != NULL ? length : "1U");
        free(length);
    }
    if (character_result) {
        char *result_length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(&result, "), f2c_character_result_%zu[(size_t)(%s)] = '\\0'",
                          expression->temporary_index,
                          result_length != NULL ? result_length : "1U");
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
        return f2c_expression_wrap_contiguous_call(expression, allocatable_result,
                                                   &contiguous_setup, &contiguous_cleanup, call,
                                                   supported);
    }
}

char *f2c_expression_call(Unit *unit, const F2cExpr *expression, int *supported) {
    char **arguments = NULL;
    Type *types = NULL;
    Buffer result = {0};
    Buffer contiguous_setup = {0};
    Buffer contiguous_cleanup = {0};
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
    const Symbol *resolved_result = resolved != NULL && resolved->result_name != NULL
                                        ? f2c_find_symbol((Unit *)resolved, resolved->result_name)
                                        : NULL;
    const char *callee =
        resolved != NULL && resolved->name != NULL
            ? resolved->name
            : (expression->symbol != NULL ? f2c_symbol_c_name(unit, expression->symbol)
                                          : expression->text);
    const int allocatable_result =
        resolved_result != NULL
            ? resolved_result->allocatable
            : (expression->symbol != NULL && expression->symbol->external_result_allocatable);
    size_t i;
    size_t derived_actual_count = 0U;
    if (expression->symbol != NULL && expression->symbol->type_bound)
        return emit_type_bound_call(unit, expression, supported);
    if (expression->symbol != NULL && expression->symbol->statement_function)
        return f2c_expression_statement_function(unit, expression, supported);
    if (f2c_intrinsic_is_bit(expression->intrinsic) &&
        expression->intrinsic != F2C_INTRINSIC_MVBITS)
        return f2c_expression_bit_intrinsic(unit, expression, supported);
    if (f2c_intrinsic_is_character(expression->intrinsic))
        return f2c_expression_character_intrinsic(unit, expression, supported);
    if (f2c_intrinsic_is_numeric_model(expression->intrinsic))
        return f2c_expression_numeric_model_intrinsic(unit, expression, supported);
    if (f2c_intrinsic_is_numeric_operation(expression->intrinsic))
        return f2c_expression_numeric_operation_intrinsic(unit, expression, supported);
    if (f2c_intrinsic_is_real_representation(expression->intrinsic))
        return f2c_expression_real_representation_intrinsic(unit, expression, supported);
    if (expression->text != NULL && strcmp(expression->text, "present") == 0 &&
        expression->child_count == 1U && expression->children[0] != NULL &&
        expression->children[0]->kind == F2C_EXPR_NAME && expression->children[0]->symbol != NULL) {
        const Symbol *present_symbol = expression->children[0]->symbol;
        if (present_symbol->allocatable || present_symbol->pointer)
            f2c_buffer_printf(&result, "(f2c_descriptor_%s != NULL)",
                              f2c_symbol_c_name(unit, present_symbol));
        else
            f2c_buffer_printf(&result, "(%s != NULL)", f2c_symbol_c_name(unit, present_symbol));
        return f2c_buffer_take(&result);
    }
    if (expression->text != NULL && strcmp(expression->text, "allocated") == 0 &&
        expression->child_count == 1U && expression->children[0] != NULL &&
        (expression->children[0]->kind == F2C_EXPR_NAME ||
         expression->children[0]->kind == F2C_EXPR_COMPONENT) &&
        expression->children[0]->symbol != NULL && expression->children[0]->symbol->allocatable) {
        char *storage = f2c_expression_emit(unit, expression->children[0], supported);
        if (!*supported || storage == NULL) {
            free(storage);
            return NULL;
        }
        f2c_buffer_printf(&result, "(%s != NULL)", storage);
        free(storage);
        return f2c_buffer_take(&result);
    }
    if (expression->text != NULL && strcmp(expression->text, "associated") == 0 &&
        expression->child_count >= 1U && expression->child_count <= 2U &&
        f2c_intrinsic_argument(expression->children, expression->child_count, "pointer", 0U) !=
            NULL) {
        const F2cExpr *pointer_expression =
            f2c_intrinsic_argument(expression->children, expression->child_count, "pointer", 0U);
        const F2cExpr *target_expression =
            f2c_intrinsic_argument(expression->children, expression->child_count, "target", 1U);
        const Symbol *pointer = pointer_expression->symbol;
        char *pointer_storage =
            pointer != NULL && pointer->procedure_pointer
                ? f2c_expression_emit(unit, pointer_expression, supported)
                : f2c_emit_pointer_designator(unit, pointer_expression, supported);
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
                f2c_buffer_printf(&result, "((size_t)(%s) == (size_t)(%s) && %s == %s)",
                                  pointer_length, target_length, pointer_storage, target_storage);
            } else {
                f2c_buffer_printf(&result, "(%s == %s)", pointer_storage, target_storage);
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
                f2c_buffer_printf(&result, "((size_t)(%s) == (size_t)(%s) && %s)", pointer_length,
                                  target_length, association);
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
    if (expression->text != NULL &&
        (strcmp(expression->text, "size") == 0 ||
         ((strcmp(expression->text, "lbound") == 0 || strcmp(expression->text, "ubound") == 0) &&
          expression->rank == 0U)))
        return f2c_expression_array_inquiry(unit, expression, supported);
    {
        int matched = 0;
        char *reduction = f2c_expression_relation_reduction(unit, expression, supported, &matched);
        if (matched)
            return reduction;
    }
    if (expression->text != NULL &&
        (strcmp(expression->text, "sum") == 0 || strcmp(expression->text, "product") == 0 ||
         strcmp(expression->text, "maxval") == 0 || strcmp(expression->text, "minval") == 0 ||
         strcmp(expression->text, "maxloc") == 0 || strcmp(expression->text, "minloc") == 0 ||
         strcmp(expression->text, "count") == 0 || strcmp(expression->text, "any") == 0 ||
         strcmp(expression->text, "all") == 0) &&
        expression->child_count >= 1U && expression->child_count <= 2U) {
        const F2cExpr *array = intrinsic_argument_value(expression->children[0]);
        const F2cExpr *dimension = expression->child_count == 2U
                                       ? intrinsic_argument_value(expression->children[1])
                                       : NULL;
        const char *macro = strcmp(expression->text, "sum") == 0       ? "F2C_SUM"
                            : strcmp(expression->text, "product") == 0 ? "F2C_PRODUCT"
                            : strcmp(expression->text, "maxval") == 0  ? "F2C_MAXIMUM"
                            : strcmp(expression->text, "minval") == 0  ? "F2C_MINIMUM"
                            : strcmp(expression->text, "maxloc") == 0  ? "F2C_MAXIMUM_LOCATION"
                            : strcmp(expression->text, "minloc") == 0  ? "F2C_MINIMUM_LOCATION"
                            : strcmp(expression->text, "count") == 0   ? "f2c_count_l"
                            : strcmp(expression->text, "any") == 0     ? "f2c_any_l"
                                                                       : "f2c_all_l";
        char *pointer = NULL;
        char *count = NULL;
        char *stride = NULL;
        char *dimension_code = NULL;
        if (dimension != NULL && (dimension->type != TYPE_INTEGER || dimension->rank != 0U)) {
            *supported = 0;
            return NULL;
        }
        if (!f2c_expression_array_view(unit, array, &pointer, &count, &stride, supported)) {
            free(pointer);
            free(count);
            free(stride);
            *supported = 0;
            return NULL;
        }
        if (dimension != NULL)
            dimension_code = f2c_expression_emit(unit, dimension, supported);
        if (!*supported || (dimension != NULL && dimension_code == NULL)) {
            free(pointer);
            free(count);
            free(stride);
            free(dimension_code);
            return NULL;
        }
        if (dimension_code != NULL)
            f2c_buffer_printf(&result, "((%s) == 1 ? ", dimension_code);
        f2c_buffer_printf(&result, "%s(%s, %s, %s)", macro, pointer, count, stride);
        if (dimension_code != NULL)
            f2c_buffer_printf(&result, " : (abort(), (%s)0))", f2c_expression_c_type(expression));
        free(pointer);
        free(count);
        free(stride);
        free(dimension_code);
        return f2c_buffer_take(&result);
    }
    if (expression->text != NULL && strcmp(expression->text, "dot_product") == 0 &&
        expression->child_count == 2U) {
        const F2cExpr *left_array = intrinsic_argument_value(expression->children[0]);
        const F2cExpr *right_array = intrinsic_argument_value(expression->children[1]);
        char *left_pointer = NULL;
        char *left_count = NULL;
        char *left_stride = NULL;
        char *right_pointer = NULL;
        char *right_count = NULL;
        char *right_stride = NULL;
        if (!f2c_expression_array_view(unit, left_array, &left_pointer, &left_count, &left_stride,
                                       supported) ||
            !f2c_expression_array_view(unit, right_array, &right_pointer, &right_count,
                                       &right_stride, supported)) {
            free(left_pointer);
            free(left_count);
            free(left_stride);
            free(right_pointer);
            free(right_count);
            free(right_stride);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result,
                          "((%s) == (%s) ? %s(%s, %s, %s, %s, %s) : "
                          "(abort(), (%s)0))",
                          left_count, right_count,
                          expression->type == TYPE_LOGICAL ? "F2C_LOGICAL_DOT" : "F2C_DOT",
                          left_pointer, left_stride, right_pointer, right_stride, left_count,
                          f2c_expression_c_type(expression));
        free(left_pointer);
        free(left_count);
        free(left_stride);
        free(right_pointer);
        free(right_count);
        free(right_stride);
        return f2c_buffer_take(&result);
    }
    if ((strcmp(expression->text, "maxloc") == 0 || strcmp(expression->text, "maxval") == 0) &&
        expression->child_count != 0U &&
        expression->children[0]->kind == F2C_EXPR_ARRAY_REFERENCE) {
        const F2cExpr *array = expression->children[0];
        const F2cExpr *section = NULL;
        char *reference;
        char *lower;
        char *upper;
        char *stride;
        for (i = 0U; i < array->child_count; ++i) {
            if (array->children[i]->kind == F2C_EXPR_ARRAY_SECTION) {
                section = array->children[i];
                break;
            }
        }
        if (section != NULL && section->child_count == 3U &&
            section->children[1]->kind != F2C_EXPR_INVALID) {
            reference = f2c_expression_emit_array_reference(unit, array, supported);
            lower =
                section->children[0]->kind == F2C_EXPR_INVALID
                    ? (i < array->symbol->rank ? f2c_symbol_dimension_lower(unit, array->symbol, i)
                                               : f2c_strdup("1"))
                    : f2c_expression_emit(unit, section->children[0], supported);
            upper = f2c_expression_emit(unit, section->children[1], supported);
            stride = section->children[2]->kind == F2C_EXPR_INVALID
                         ? f2c_strdup("1")
                         : f2c_expression_emit(unit, section->children[2], supported);
            if (*supported && reference != NULL && lower != NULL && upper != NULL &&
                stride != NULL) {
                f2c_buffer_printf(&result, "%s(&%s, (int32_t)((((%s) - (%s)) / (%s)) + 1))",
                                  strcmp(expression->text, "maxloc") == 0 ? "F2C_MAXLOC"
                                                                          : "F2C_MAXVAL",
                                  reference, upper, lower, stride);
                free(reference);
                free(lower);
                free(upper);
                free(stride);
                return f2c_buffer_take(&result);
            }
            free(reference);
            free(lower);
            free(upper);
            free(stride);
            *supported = 0;
            return NULL;
        }
    }
    if (!f2c_expression_children(unit, expression, &arguments, &types)) {
        *supported = 0;
        return NULL;
    }
    if (f2c_is_intrinsic_name(expression->text)) {
        char *intrinsic = f2c_emit_intrinsic(expression->text, arguments, types,
                                             expression->child_count, expression->type);
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
         expression->rank != 0U || expression->type == TYPE_UNKNOWN || allocatable_result)) {
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
    if (expression->type == TYPE_CHARACTER && !allocatable_result) {
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
                ? resolved_dummy->intent
                : (expression->symbol != NULL && i < expression->symbol->external_parameter_count
                       ? expression->symbol->external_parameter_intents[i]
                       : F2C_INTENT_UNSPECIFIED);
        const F2cExpr *actual_expression = intrinsic_argument_value(expression->children[i]);
        if (actual_expression != NULL && actual_expression->symbol != NULL &&
            actual_expression->symbol->equivalence_unaligned && intent != F2C_INTENT_IN) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            free(f2c_buffer_take(&result));
            free(contiguous_setup.data);
            free(contiguous_cleanup.data);
            *supported = 0;
            return NULL;
        }
        char *actual =
            descriptor
                ? f2c_expression_descriptor_actual(&contiguous_setup, &contiguous_cleanup, unit,
                                                   expression->children[i], intent, supported)
                : emit_external_actual(unit, expression->children[i], arguments[i], supported);
        char *bridged;
        if (actual == NULL) {
            f2c_expression_free_arguments(arguments, types, expression->child_count);
            free(f2c_buffer_take(&result));
            free(contiguous_setup.data);
            free(contiguous_cleanup.data);
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
            free(contiguous_setup.data);
            free(contiguous_cleanup.data);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(
            &result, "%s%s",
            i == 0U && (expression->type != TYPE_CHARACTER || allocatable_result) ? "" : ", ",
            actual);
        free(actual);
    }
    if (!f2c_emit_host_capture_actuals(
            &result, unit, capture_procedure,
            expression->child_count != 0U ||
                (expression->type == TYPE_CHARACTER && !allocatable_result))) {
        f2c_expression_free_arguments(arguments, types, expression->child_count);
        free(f2c_buffer_take(&result));
        free(contiguous_setup.data);
        free(contiguous_cleanup.data);
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
        free(contiguous_setup.data);
        free(contiguous_cleanup.data);
        *supported = 0;
        return NULL;
    }
    if (expression->type == TYPE_CHARACTER && !allocatable_result) {
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
    return f2c_expression_wrap_contiguous_call(expression, allocatable_result, &contiguous_setup,
                                               &contiguous_cleanup, f2c_buffer_take(&result),
                                               supported);
}
