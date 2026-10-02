#include "codegen/call/private.h"

#include "codegen/expression/private.h"
#include "codegen/lowering/private.h"

#include <stdlib.h>

char *f2c_call_emit_actual_address(Unit *unit, const F2cExpr *actual, const char *code,
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
    if (actual->kind == F2C_EXPR_COMPONENT && symbol != NULL && symbol->external)
        return f2c_strdup(code);
    if (symbol != NULL && actual->type == TYPE_DERIVED && actual->rank == 0U &&
        (symbol->pointer || symbol->allocatable) &&
        (actual->kind == F2C_EXPR_NAME || actual->kind == F2C_EXPR_COMPONENT)) {
        char *storage = f2c_descriptor_storage_designator(unit, actual);
        if (storage == NULL)
            *supported = 0;
        return storage;
    }
    if (f2c_lowering_argument_materialized(unit, actual)) {
        if (actual->rank != 0U || actual->type == TYPE_DERIVED || actual->type == TYPE_UNKNOWN) {
            *supported = 0;
            return NULL;
        }
        if (actual->type == TYPE_CHARACTER)
            return f2c_strdup(code);
        f2c_buffer_printf(&result, "&%s", code);
        return f2c_buffer_take(&result);
    }
    if (symbol != NULL && symbol->equivalence_unaligned) {
        if (actual->rank != 0U ||
            (actual->kind != F2C_EXPR_NAME && actual->kind != F2C_EXPR_ARRAY_REFERENCE)) {
            *supported = 0;
            return NULL;
        }
        return f2c_emit_scalar_temporary_address(f2c_symbol_c_type(symbol), symbol->type, code);
    }
    if (f2c_lowering_code(unit, actual) != NULL && actual->kind == F2C_EXPR_NAME &&
        actual->symbol == NULL && actual->value_category == F2C_VALUE_VARIABLE) {
        f2c_buffer_printf(&result, "&(%s)", code);
        return f2c_buffer_take(&result);
    }
    if (f2c_lowering_code(unit, actual) != NULL) {
        if (actual->rank != 0U || actual->type == TYPE_CHARACTER || actual->type == TYPE_DERIVED)
            return f2c_strdup(code);
        return f2c_emit_scalar_temporary_address(f2c_expression_c_type(actual), actual->type, code);
    }
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
        return f2c_strdup(code);
    }
    if (actual->type == TYPE_CHARACTER)
        return f2c_strdup(code);
    if (actual->definable && (actual->type == TYPE_DERIVED || actual->kind == F2C_EXPR_COMPONENT)) {
        f2c_buffer_printf(&result, "&(%s)", code);
        return f2c_buffer_take(&result);
    }
    if (actual->type == TYPE_DERIVED && !actual->definable)
        return f2c_expression_derived_actual_pointer(unit, actual, supported);
    return f2c_emit_scalar_temporary_address(
        actual->type != TYPE_UNKNOWN ? f2c_expression_c_type(actual) : f2c_c_type(TYPE_REAL),
        actual->type != TYPE_UNKNOWN ? actual->type : TYPE_REAL, code);
}
