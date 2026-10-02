#include "codegen/call/private.h"

#include "codegen/array/private.h"
#include "codegen/lowering/private.h"

#include <stdlib.h>

static F2cExpr *actual_value(F2cExpr *actual) {
    return actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U
               ? actual->children[0]
               : actual;
}

int f2c_call_expression_requires_materialization(Unit *unit, const F2cExpr *expression) {
    const Symbol *callee;
    size_t argument;
    if (expression == NULL || expression->kind != F2C_EXPR_CALL || expression->rank != 0U ||
        expression->intrinsic != F2C_INTRINSIC_NONE ||
        f2c_lowering_code(unit, expression) != NULL || (callee = expression->symbol) == NULL ||
        callee->type_bound || callee->external_elemental ||
        (expression->resolved_procedure != NULL && expression->resolved_procedure->elemental))
        return 0;
    for (argument = 0U; argument < expression->child_count; ++argument)
        if (f2c_call_actual_requires_materialization(unit, callee, expression->children[argument],
                                                     argument))
            return 1;
    if (expression->type == TYPE_CHARACTER && callee->character_length_scope != NULL) {
        for (argument = 0U; argument < expression->child_count; ++argument) {
            const F2cExpr *actual = actual_value(expression->children[argument]);
            if (actual != NULL && actual->rank == 0U && !actual->definable &&
                (actual->kind == F2C_EXPR_CALL || actual->kind == F2C_EXPR_UNARY ||
                 actual->kind == F2C_EXPR_BINARY))
                return 1;
        }
    }
    return 0;
}

int f2c_call_materialize_expression(Unit *unit, F2cExpr *expression, size_t identifier,
                                    const char *role, size_t *temporary, Buffer *prelude,
                                    int depth) {
    const Symbol *callee;
    Buffer name = {0};
    Buffer setup = {0};
    Buffer cleanup = {0};
    char *code = NULL;
    char *character_length = NULL;
    Buffer length_name = {0};
    int supported = 0;
    int success = 0;
    size_t argument;
    if (!f2c_call_expression_requires_materialization(unit, expression))
        return 1;
    if (expression->type == TYPE_UNKNOWN ||
        (expression->type == TYPE_DERIVED && expression->derived_type == NULL))
        return 0;
    callee = expression->symbol;
    f2c_buffer_printf(&name, "f2c_call_%s_%zu_%zu", role, identifier, (*temporary)++);
    if (name.data == NULL)
        goto done;
    for (argument = 0U; argument < expression->child_count; ++argument) {
        F2cExpr *actual = actual_value(expression->children[argument]);
        if (actual != NULL &&
            !f2c_array_hoist_scalar_subexpressions(unit, actual, identifier, role, temporary,
                                                   &setup, depth + 1, actual->definable))
            goto done;
    }
    for (argument = 0U; argument < expression->child_count; ++argument) {
        F2cExpr *actual = actual_value(expression->children[argument]);
        F2cDescriptorView view = {0};
        const F2cIntent intent = argument < callee->external_parameter_count
                                     ? callee->external_parameter_intents[argument]
                                     : F2C_INTENT_UNSPECIFIED;
        if (!f2c_call_actual_requires_materialization(unit, callee, actual, argument))
            continue;
        if (!f2c_call_actual_permits_copy(unit, callee, actual, argument) ||
            (argument < callee->external_parameter_count &&
             (callee->external_parameter_allocatable[argument] ||
              callee->external_parameter_pointer[argument])) ||
            !f2c_descriptor_materialize_view(&setup, &cleanup, unit, actual, intent, 0U, argument,
                                             depth + 1, &view) ||
            !f2c_call_cache_actual_view(&setup, unit, actual, &view, depth + 1)) {
            f2c_descriptor_view_free(&view);
            goto done;
        }
        f2c_descriptor_view_free(&view);
    }
    if (expression->type == TYPE_CHARACTER) {
        character_length = f2c_character_length_expression(unit, expression);
        f2c_buffer_printf(&length_name, "%s_character_length", name.data);
        if (character_length == NULL || length_name.data == NULL ||
            !f2c_lowering_copy_character_length(unit, expression, length_name.data))
            goto done;
    }
    code = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported || code == NULL)
        goto done;
    f2c_array_indent(prelude, depth);
    f2c_buffer_printf(prelude, "%s%s %s;\n", f2c_expression_c_type(expression),
                      expression->type == TYPE_CHARACTER ? " *" : "", name.data);
    if (character_length != NULL) {
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "size_t %s = 0U;\n", length_name.data);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "(void)%s;\n", length_name.data);
    }
    f2c_array_indent(prelude, depth);
    f2c_buffer_append(prelude, "{\n");
    f2c_buffer_append(prelude, setup.data != NULL ? setup.data : "");
    if (character_length != NULL) {
        f2c_array_indent(prelude, depth + 1);
        f2c_buffer_printf(prelude, "%s = (size_t)(%s);\n", length_name.data, character_length);
    }
    f2c_array_indent(prelude, depth + 1);
    f2c_buffer_printf(prelude, "%s = %s;\n", name.data, code);
    f2c_buffer_append(prelude, cleanup.data != NULL ? cleanup.data : "");
    f2c_array_indent(prelude, depth);
    f2c_buffer_append(prelude, "}\n");
    success = f2c_lowering_take_code(unit, expression, f2c_buffer_take(&name));
done:
    free(name.data);
    free(setup.data);
    free(cleanup.data);
    free(code);
    free(character_length);
    free(length_name.data);
    return success;
}
