#include "codegen/expression/private.h"

#include "codegen/descriptor/private.h"
#include "codegen/lowering/private.h"

#include <stdlib.h>

/* Physical C storage is unqualified. Fortran access attributes belong to each
 * scope, not permanently to that storage: a legal ordinary association must not
 * access a C-declared volatile object through an ordinary lvalue (C 6.7.3p6).
 * Form a qualified lvalue only when performing a scoped read or definition;
 * obtaining an address for argument association does not perform that access.
 * Array/character kernels carry their view's qualifiers separately. */
char *f2c_expression_apply_access(Unit *unit, const F2cExpr *expression, char *storage) {
    Buffer result = {0};
    if (storage == NULL || expression == NULL || expression->rank != 0U ||
        expression->type == TYPE_CHARACTER ||
        (expression->symbol != NULL &&
         (expression->symbol->external || expression->symbol->equivalence_unaligned)) ||
        (f2c_lowering_storage_qualifiers(unit, expression) & F2C_STORAGE_VOLATILE) == 0U)
        return storage;
    f2c_buffer_printf(&result, "(*(volatile %s *)&(%s))", f2c_expression_c_type(expression),
                      storage);
    free(storage);
    return f2c_buffer_take(&result);
}

static char *component_storage(Unit *unit, const F2cExpr *expression, int *supported) {
    Buffer result = {0};
    char *base;
    const F2cExpr *parent;
    if (expression->child_count == 0U || expression->symbol == NULL) {
        *supported = 0;
        return NULL;
    }
    parent = expression->children[0];
    if (expression->child_count > 1U && expression->symbol->rank != 0U) {
        char *indices[F2C_MAX_RANK] = {0};
        char *designator = NULL;
        size_t selector;
        if (expression->child_count != expression->symbol->rank + 1U) {
            *supported = 0;
            return NULL;
        }
        for (selector = 0U; selector < expression->symbol->rank; ++selector) {
            const F2cExpr *index = expression->children[selector + 1U];
            if (index == NULL || index->kind == F2C_EXPR_ARRAY_SECTION || index->rank != 0U) {
                *supported = 0;
                break;
            }
            indices[selector] = f2c_expression_emit(unit, index, supported);
            if (!*supported || indices[selector] == NULL)
                break;
        }
        if (*supported)
            designator = f2c_descriptor_element_designator(unit, expression, indices,
                                                           expression->symbol->rank);
        for (selector = 0U; selector < expression->symbol->rank; ++selector)
            free(indices[selector]);
        if (!*supported || designator == NULL) {
            free(designator);
            *supported = 0;
            return NULL;
        }
        return designator;
    }
    base = f2c_expression_storage_designator(unit, parent, supported);
    if (!*supported || base == NULL)
        return NULL;
    if (parent->kind == F2C_EXPR_NAME && parent->symbol != NULL && parent->symbol->polymorphic &&
        parent->derived_type != NULL && parent->symbol->derived_type != parent->derived_type) {
        Buffer cast = {0};
        f2c_buffer_printf(&cast, "(*((%s *)&(%s)))", parent->derived_type->c_name, base);
        free(base);
        base = f2c_buffer_take(&cast);
    }
    if (expression->symbol->pointer && expression->symbol->rank == 0U)
        f2c_buffer_append(&result, "(*(");
    f2c_expression_append_component(&result, base, parent->derived_type, expression->symbol);
    if (expression->symbol->pointer && expression->symbol->rank == 0U)
        f2c_buffer_append(&result, "))");
    free(base);
    return f2c_buffer_take(&result);
}

char *f2c_expression_emit_component(Unit *unit, const F2cExpr *expression, int *supported) {
    return f2c_expression_apply_access(unit, expression,
                                       component_storage(unit, expression, supported));
}

char *f2c_expression_storage_designator(Unit *unit, const F2cExpr *expression, int *supported) {
    const char *lowered;
    *supported = 1;
    if (expression == NULL) {
        *supported = 0;
        return NULL;
    }
    lowered = f2c_lowering_code(unit, expression);
    if (lowered != NULL)
        return f2c_strdup(lowered);
    switch (expression->kind) {
    case F2C_EXPR_NAME:
        return f2c_expression_name(unit, expression, supported);
    case F2C_EXPR_ARRAY_REFERENCE:
        return f2c_expression_array_storage(unit, expression, supported);
    case F2C_EXPR_COMPONENT:
        return component_storage(unit, expression, supported);
    case F2C_EXPR_SUBSTRING:
        return f2c_expression_substring_storage(unit, expression, supported);
    default:
        /* A computed parent may be a lowering-owned derived temporary. */
        return f2c_expression_emit(unit, expression, supported);
    }
}
