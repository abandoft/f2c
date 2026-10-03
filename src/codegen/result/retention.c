#include "codegen/result/retention.h"

#include "codegen/lowering/private.h"
#include "codegen/names.h"

#include <stdlib.h>
#include <string.h>

static int has_retained_value(const F2cExpr *expression, int root) {
    if (expression == NULL)
        return 0;
    if (!root && expression->owned_temporary_kind != F2C_OWNED_TEMPORARY_NONE &&
        f2c_expression_temporary_release_kind(expression) != F2C_TEMPORARY_BORROWED_REFERENCE &&
        f2c_expression_temporary_release_kind(expression) != F2C_TEMPORARY_STACK_VALUE)
        return 1;
    for (size_t child = 0U; child < expression->child_count; ++child)
        if (has_retained_value(expression->children[child], 0))
            return 1;
    return 0;
}

static int begin_scope(F2cResultRetentionScope *scope, Unit *unit, int active, const char *name,
                       Buffer *output, int depth) {
    if (scope == NULL || unit == NULL || name == NULL || output == NULL)
        return 0;
    memset(scope, 0, sizeof(*scope));
    scope->unit = unit;
    if (!active)
        return 1;
    static const char *const suffixes[] = {
        "retained_inline", "retained",    "retained_heap", "retained_count", "retained_capacity",
        "new_capacity",    "replacement", "index",         "value"};
    scope->name =
        f2c_codegen_local_family(unit, name, suffixes, sizeof(suffixes) / sizeof(suffixes[0]));
    if (scope->name == NULL)
        return 0;
    scope->active = 1;
    name = scope->name;
    f2c_array_indent(output, depth);
    f2c_buffer_printf(
        output, "struct %s_retained_record { void *data; size_t count; size_t temporary; };\n",
        name);
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "struct %s_retained_record %s_retained_inline[8];\n", name, name);
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "struct %s_retained_record *%s_retained = %s_retained_inline;\n",
                      name, name, name);
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "struct %s_retained_record *%s_retained_heap = NULL;\n", name, name);
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "size_t %s_retained_count = 0U, %s_retained_capacity = 8U;\n", name,
                      name);
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "(void)%s_retained; (void)%s_retained_capacity;\n", name, name);
    return 1;
}

int f2c_result_retention_begin(F2cResultRetentionScope *scope, Unit *unit,
                               const F2cExpr *expression, const char *name, Buffer *output,
                               int depth) {
    return expression != NULL &&
           begin_scope(scope, unit, has_retained_value(expression, 1), name, output, depth);
}

int f2c_result_retention_begin_values(F2cResultRetentionScope *scope, Unit *unit,
                                      F2cExpr *const *expressions, size_t count, const char *name,
                                      Buffer *output, int depth) {
    int active = 0;
    if (count != 0U && expressions == NULL)
        return 0;
    for (size_t index = 0U; index < count; ++index)
        if (has_retained_value(expressions[index], 0))
            active = 1;
    return begin_scope(scope, unit, active, name, output, depth);
}

static int register_temporary(F2cResultRetentionScope *scope, size_t temporary) {
    for (size_t index = 0U; index < scope->count; ++index)
        if (scope->temporaries[index] == temporary)
            return 1;
    if (scope->count == scope->capacity) {
        const size_t capacity = scope->capacity == 0U ? 4U : scope->capacity * 2U;
        if (capacity < scope->capacity || capacity > SIZE_MAX / sizeof(*scope->temporaries))
            return 0;
        size_t *replacement =
            (size_t *)realloc(scope->temporaries, capacity * sizeof(*scope->temporaries));
        if (replacement == NULL)
            return 0;
        scope->temporaries = replacement;
        scope->capacity = capacity;
    }
    scope->temporaries[scope->count++] = temporary;
    return 1;
}

static char *retained_count(Unit *unit, const F2cExpr *expression, const char *storage) {
    Buffer count = {0};
    const char *extent = f2c_lowering_extent(unit, expression);
    if (expression->rank == 0U)
        return f2c_strdup("1U");
    if (extent != NULL) {
        f2c_buffer_printf(&count, "(size_t)(%s)", extent);
    } else {
        f2c_buffer_printf(&count, "f2c_inquiry_size(%zuU, (const size_t[]){", expression->rank);
        for (size_t dimension = 0U; dimension < expression->rank; ++dimension)
            f2c_buffer_printf(&count, "%s(size_t)%s_extent_%zu", dimension == 0U ? "" : ", ",
                              storage, dimension + 1U);
        f2c_buffer_append(&count, "})");
    }
    return f2c_buffer_take(&count);
}

static void emit_growth(Buffer *output, const char *name, int depth) {
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "if (%s_retained_count == %s_retained_capacity) {\n", name, name);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "if (%s_retained_capacity > SIZE_MAX / 2U / "
                      "sizeof(*%s_retained)) abort();\n",
                      name, name);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output, "const size_t %s_new_capacity = %s_retained_capacity * 2U;\n", name,
                      name);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "struct %s_retained_record *%s_replacement = "
                      "(struct %s_retained_record *)realloc(%s_retained_heap, "
                      "%s_new_capacity * sizeof(*%s_retained));\n",
                      name, name, name, name, name, name);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output, "if (%s_replacement == NULL) abort();\n", name);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "if (%s_retained_heap == NULL) "
                      "memcpy(%s_replacement, %s_retained_inline, "
                      "%s_retained_count * sizeof(*%s_retained));\n",
                      name, name, name, name, name);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "%s_retained_heap = %s_replacement; "
                      "%s_retained = %s_replacement; "
                      "%s_retained_capacity = %s_new_capacity;\n",
                      name, name, name, name, name, name);
    f2c_array_indent(output, depth);
    f2c_buffer_append(output, "}\n");
}

static int merge_retention(F2cResultRetentionScope *scope, const F2cResultRetentionScope *nested,
                           Buffer *output, int depth) {
    if (nested == NULL || !nested->active)
        return 1;
    if (nested == scope || nested->unit != scope->unit || !scope->active)
        return 0;
    for (size_t index = 0U; index < nested->count; ++index)
        if (!register_temporary(scope, nested->temporaries[index]))
            return 0;
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output,
                      "for (size_t %s_index = 0U; "
                      "%s_index < %s_retained_count; ++%s_index) {\n",
                      scope->name, scope->name, nested->name, scope->name);
    emit_growth(output, scope->name, depth + 1);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "%s_retained[%s_retained_count++] = (struct %s_retained_record){"
                      "%s_retained[%s_index].data, "
                      "%s_retained[%s_index].count, "
                      "%s_retained[%s_index].temporary};\n",
                      scope->name, scope->name, scope->name, nested->name, scope->name,
                      nested->name, scope->name, nested->name, scope->name);
    f2c_array_indent(output, depth);
    f2c_buffer_append(output, "}\n");
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "free(%s_retained_heap);\n", nested->name);
    return 1;
}

int f2c_result_retention_capture(F2cResultRetentionScope *scope, const F2cArrayCleanupList *cleanup,
                                 Buffer *output, int depth) {
    if (scope == NULL || cleanup == NULL || output == NULL ||
        (cleanup->count != 0U && !scope->active))
        return 0;
    for (size_t index = 0U; index < cleanup->count; ++index) {
        const F2cArrayCleanupAction *action = &cleanup->items[index];
        const F2cExpr *expression = action->expression;
        if (expression == NULL ||
            !f2c_array_owned_temporary_valid(scope->unit, expression,
                                             expression->owned_temporary_kind) ||
            action->temporary != expression->owned_temporary_index ||
            !merge_retention(scope, action->retention, output, depth) ||
            !register_temporary(scope, action->temporary))
            return 0;
        const char *storage =
            expression->owned_temporary_kind == F2C_OWNED_TEMPORARY_FUNCTION_RESULT
                ? f2c_lowering_owned_storage(scope->unit, expression)
                : f2c_lowering_code(scope->unit, expression);
        char *count = storage != NULL ? retained_count(scope->unit, expression, storage) : NULL;
        if (count == NULL)
            return 0;
        emit_growth(output, scope->name, depth);
        f2c_array_indent(output, depth);
        f2c_buffer_printf(output,
                          "%s_retained[%s_retained_count++] = "
                          "(struct %s_retained_record){%s, %s, %zuU};\n",
                          scope->name, scope->name, scope->name, storage, count, action->temporary);
        free(count);
    }
    return 1;
}

int f2c_result_retention_release(const F2cResultRetentionScope *scope, Buffer *output, int depth) {
    if (scope == NULL || output == NULL)
        return 0;
    if (!scope->active)
        return 1;
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "while (%s_retained_count != 0U) {\n", scope->name);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "struct %s_retained_record %s_value = "
                      "%s_retained[--%s_retained_count];\n",
                      scope->name, scope->name, scope->name, scope->name);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output, "switch (%s_value.temporary) {\n", scope->name);
    for (size_t index = 0U; index < scope->count; ++index) {
        const size_t temporary = scope->temporaries[index];
        if (temporary >= scope->unit->owned_temporary_count)
            return 0;
        const F2cOwnedTemporary *owned = &scope->unit->owned_temporaries[temporary];
        f2c_array_indent(output, depth + 1);
        f2c_buffer_printf(output, "case %zuU:\n", temporary);
        if (owned->release_kind == F2C_TEMPORARY_FINALIZE_VALUE ||
            owned->release_kind == F2C_TEMPORARY_DISCARD_SNAPSHOT) {
            if (owned->derived_type == NULL || owned->derived_type->c_name == NULL)
                return 0;
            const int snapshot = owned->release_kind == F2C_TEMPORARY_DISCARD_SNAPSHOT;
            f2c_array_indent(output, depth + 2);
            f2c_buffer_printf(output,
                              "f2c_%s_array_%s((%s *)%s_value.data, "
                              "%s_value.count",
                              snapshot ? "discard" : "destroy", owned->derived_type->c_name,
                              owned->derived_type->c_name, scope->name, scope->name);
            if (!snapshot)
                f2c_buffer_printf(output, ", %zuU", owned->rank);
            f2c_buffer_append(output, ");\n");
        }
        f2c_array_indent(output, depth + 2);
        f2c_buffer_append(output, "break;\n");
    }
    f2c_array_indent(output, depth + 1);
    f2c_buffer_append(output, "default: abort();\n");
    f2c_array_indent(output, depth + 1);
    f2c_buffer_append(output, "}\n");
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output, "free(%s_value.data);\n", scope->name);
    f2c_array_indent(output, depth);
    f2c_buffer_append(output, "}\n");
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "free(%s_retained_heap);\n", scope->name);
    return 1;
}

void f2c_result_retention_clear(F2cResultRetentionScope *scope) {
    if (scope == NULL)
        return;
    free(scope->name);
    free(scope->temporaries);
    memset(scope, 0, sizeof(*scope));
}
