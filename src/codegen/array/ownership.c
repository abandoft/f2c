#include "codegen/array/private.h"

#include <stdlib.h>
#include <string.h>

int f2c_array_owned_temporary_valid(const Unit *unit, const F2cExpr *expression,
                                    F2cOwnedTemporaryKind expected) {
    const F2cOwnedTemporary *owned;
    if (unit == NULL || expression == NULL || !expression->temporary_ownership_analyzed ||
        expression->owned_temporary_index >= unit->owned_temporary_count ||
        expression->owned_temporary_kind == F2C_OWNED_TEMPORARY_NONE)
        return 0;
    owned = &unit->owned_temporaries[expression->owned_temporary_index];
    return owned->kind == expected && owned->kind == expression->owned_temporary_kind &&
           owned->type == expression->type && owned->type_kind == expression->type_kind &&
           owned->rank == expression->rank &&
           owned->owner_statement == expression->lifetime_statement_index &&
           owned->derived_type == expression->derived_type &&
           owned->requires_finalization ==
               (expression->type == TYPE_DERIVED && expression->derived_type != NULL);
}

int f2c_array_cleanup_append(Unit *unit, F2cArrayCleanupList *list, const F2cExpr *expression,
                             int depth) {
    F2cArrayCleanupAction *replacement;
    size_t capacity;
    size_t item;
    if (list == NULL ||
        !f2c_array_owned_temporary_valid(unit, expression, expression->owned_temporary_kind) ||
        expression->lowered_c == NULL)
        return 0;
    for (item = 0U; item < list->count; ++item)
        if (list->items[item].temporary == expression->owned_temporary_index)
            return 0;
    if (list->count == list->capacity) {
        capacity = list->capacity == 0U ? 4U : list->capacity * 2U;
        if (capacity < list->capacity || capacity > SIZE_MAX / sizeof(*replacement))
            return 0;
        replacement =
            (F2cArrayCleanupAction *)realloc(list->items, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        list->items = replacement;
        list->capacity = capacity;
    }
    list->items[list->count++] =
        (F2cArrayCleanupAction){expression, expression->owned_temporary_index, depth};
    return 1;
}

static int emit_action(Buffer *output, Unit *unit, const F2cArrayCleanupAction *action) {
    const F2cExpr *expression;
    const F2cOwnedTemporary *owned;
    size_t dimension;
    if (output == NULL || unit == NULL || action == NULL ||
        action->temporary >= unit->owned_temporary_count)
        return 0;
    expression = action->expression;
    owned = &unit->owned_temporaries[action->temporary];
    if (!f2c_array_owned_temporary_valid(unit, expression, owned->kind) ||
        action->temporary != expression->owned_temporary_index)
        return 0;
    f2c_array_indent(output, action->depth);
    if (owned->requires_finalization) {
        f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, ", owned->derived_type->c_name,
                          expression->lowered_c);
        if (expression->lowered_extent_c != NULL) {
            f2c_buffer_printf(output, "(size_t)(%s)", expression->lowered_extent_c);
        } else {
            f2c_buffer_printf(output, "f2c_inquiry_size(%zuU, (const size_t[]){", owned->rank);
            for (dimension = 0U; dimension < owned->rank; ++dimension)
                f2c_buffer_printf(output, "%s(size_t)%s_extent_%zu", dimension == 0U ? "" : ", ",
                                  expression->lowered_c, dimension + 1U);
            f2c_buffer_append(output, "})");
        }
        f2c_buffer_printf(output, ", %zuU);\n", owned->rank);
        f2c_array_indent(output, action->depth);
    }
    f2c_buffer_printf(output, "free(%s);\n", expression->lowered_c);
    return 1;
}

int f2c_array_cleanup_emit(Buffer *output, Unit *unit, const F2cArrayCleanupList *list) {
    size_t index;
    if (output == NULL || unit == NULL || list == NULL)
        return 0;
    for (index = list->count; index != 0U; --index) {
        if (!emit_action(output, unit, &list->items[index - 1U])) {
            const F2cArrayCleanupAction *action = &list->items[index - 1U];
            const F2cSourceSpan *span =
                action->expression != NULL ? &action->expression->span : &unit->header_span;
            f2c_diagnostic_span_code(
                unit->context, F2C_DIAGNOSTIC_INTERNAL, span, 1,
                "code generation rejected an invalid owned-array cleanup action");
            return 0;
        }
    }
    return 1;
}

void f2c_array_cleanup_clear(F2cArrayCleanupList *list) {
    if (list == NULL)
        return;
    free(list->items);
    memset(list, 0, sizeof(*list));
}
