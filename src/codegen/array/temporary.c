#include "codegen/array/private.h"

#include <stdlib.h>
#include <string.h>

static int trivial_scalar(const F2cExpr *expression) {
    return expression->kind == F2C_EXPR_INVALID || expression->kind == F2C_EXPR_INTEGER_LITERAL ||
           expression->kind == F2C_EXPR_REAL_LITERAL ||
           expression->kind == F2C_EXPR_STRING_LITERAL ||
           expression->kind == F2C_EXPR_LOGICAL_LITERAL || expression->kind == F2C_EXPR_NAME ||
           expression->kind == F2C_EXPR_ABSENT_ARGUMENT;
}

int f2c_array_hoist_scalar_subexpressions(Unit *unit, F2cExpr *expression, size_t identifier,
                                          const char *role, size_t *temporary, Buffer *prelude,
                                          int depth, int root) {
    size_t child;
    if (unit == NULL || role == NULL || temporary == NULL || prelude == NULL)
        return 0;
    if (expression == NULL)
        return 1;
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR)
        return 1;
    if (!root && expression->rank == 0U && !trivial_scalar(expression) &&
        expression->kind != F2C_EXPR_KEYWORD_ARGUMENT &&
        expression->kind != F2C_EXPR_ARRAY_SECTION) {
        Buffer name = {0};
        char *code;
        int supported = 0;
        if (expression->type == TYPE_CHARACTER || expression->type == TYPE_DERIVED ||
            expression->type == TYPE_UNKNOWN)
            return 0;
        code = f2c_emit_expression_ast(unit, expression, &supported);
        if (!supported || code == NULL) {
            free(code);
            return 0;
        }
        f2c_buffer_printf(&name, "f2c_array_%s_%zu_%zu", role, identifier, (*temporary)++);
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "const %s %s = %s;\n", f2c_expression_c_type(expression),
                          name.data, code);
        free(code);
        free(expression->lowered_c);
        expression->lowered_c = f2c_buffer_take(&name);
        return expression->lowered_c != NULL;
    }
    if (expression->kind == F2C_EXPR_IMPLIED_DO && expression->child_count >= 3U) {
        const size_t value_count = expression->child_count - 3U;
        for (child = value_count; child < expression->child_count; ++child)
            if (!f2c_array_hoist_scalar_subexpressions(unit, expression->children[child],
                                                       identifier, role, temporary, prelude, depth,
                                                       0))
                return 0;
        return 1;
    }
    for (child = 0U; child < expression->child_count; ++child)
        if (!f2c_array_hoist_scalar_subexpressions(unit, expression->children[child], identifier,
                                                   role, temporary, prelude, depth, 0))
            return 0;
    return 1;
}

static int flat_array_constructor(const F2cExpr *expression) {
    size_t child;
    if (expression == NULL || expression->kind != F2C_EXPR_ARRAY_CONSTRUCTOR)
        return 0;
    for (child = 0U; child < expression->child_count; ++child)
        if (expression->children[child]->rank != 0U ||
            expression->children[child]->kind == F2C_EXPR_IMPLIED_DO)
            return 0;
    return 1;
}

int f2c_array_materialize_constructors(Context *context, Unit *unit, F2cExpr *expression,
                                       size_t identifier, const char *role, size_t *temporary,
                                       Buffer *prelude, Buffer *cleanup, int depth) {
    size_t child;
    if (context == NULL || unit == NULL || temporary == NULL || prelude == NULL ||
        cleanup == NULL || role == NULL)
        return 0;
    if (expression == NULL)
        return 1;
    if (expression->kind != F2C_EXPR_ARRAY_CONSTRUCTOR) {
        for (child = 0U; child < expression->child_count; ++child)
            if (!f2c_array_materialize_constructors(context, unit, expression->children[child],
                                                    identifier, role, temporary, prelude, cleanup,
                                                    depth))
                return 0;
    }
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR && expression->lowered_c == NULL) {
        Buffer name = {0};
        char *code = NULL;
        int supported = 0;
        if (expression->rank != 1U || expression->type == TYPE_UNKNOWN)
            return 0;
        if (expression->type != TYPE_CHARACTER && expression->type != TYPE_DERIVED &&
            flat_array_constructor(expression)) {
            f2c_buffer_printf(&name, "f2c_array_%s_constructor_%zu_%zu", role, identifier,
                              (*temporary)++);
            f2c_array_indent(prelude, depth);
            if (expression->child_count == 0U) {
                f2c_buffer_printf(prelude, "const %s %s[1] = {0};\n",
                                  f2c_expression_c_type(expression), name.data);
            } else {
                code = f2c_emit_expression_ast(unit, expression, &supported);
                if (!supported || code == NULL) {
                    free(code);
                    free(name.data);
                    return 0;
                }
                f2c_buffer_printf(prelude, "const %s *const %s = %s;\n",
                                  f2c_expression_c_type(expression), name.data, code);
            }
            free(code);
            expression->lowered_c = f2c_buffer_take(&name);
            return expression->lowered_c != NULL;
        } else {
            Buffer count = {0};
            Buffer capacity = {0};
            Buffer character_length = {0};
            Buffer character_length_set = {0};
            const size_t current = (*temporary)++;
            f2c_buffer_printf(&name, "f2c_array_%s_constructor_%zu_%zu", role, identifier, current);
            f2c_buffer_printf(&count, "f2c_array_%s_constructor_count_%zu_%zu", role, identifier,
                              current);
            f2c_buffer_printf(&capacity, "f2c_array_%s_constructor_capacity_%zu_%zu", role,
                              identifier, current);
            if (expression->type == TYPE_CHARACTER) {
                f2c_buffer_printf(&character_length, "f2c_array_%s_constructor_length_%zu_%zu",
                                  role, identifier, current);
                f2c_buffer_printf(&character_length_set,
                                  "f2c_array_%s_constructor_length_set_%zu_%zu", role, identifier,
                                  current);
            }
            if (name.data == NULL || count.data == NULL || capacity.data == NULL ||
                (expression->type == TYPE_CHARACTER &&
                 (character_length.data == NULL || character_length_set.data == NULL)) ||
                (expression->type == TYPE_CHARACTER
                     ? !f2c_array_emit_character_constructor_temporary(
                           context, unit, expression, name.data, count.data, capacity.data,
                           character_length.data, character_length_set.data, prelude, depth)
                 : expression->type == TYPE_DERIVED
                     ? !f2c_array_emit_derived_constructor_temporary(context, unit, expression,
                                                                     name.data, count.data,
                                                                     capacity.data, prelude, depth)
                     : !f2c_array_emit_numeric_constructor_temporary(
                           context, unit, expression, name.data, count.data, capacity.data, prelude,
                           depth))) {
                free(name.data);
                free(count.data);
                free(capacity.data);
                free(character_length.data);
                free(character_length_set.data);
                return 0;
            }
            expression->lowered_c = f2c_buffer_take(&name);
            expression->lowered_extent_c = f2c_buffer_take(&count);
            if (expression->type == TYPE_CHARACTER)
                expression->lowered_character_length_c = f2c_buffer_take(&character_length);
            free(capacity.data);
            free(character_length_set.data);
            if (expression->lowered_c == NULL || expression->lowered_extent_c == NULL ||
                (expression->type == TYPE_CHARACTER &&
                 expression->lowered_character_length_c == NULL))
                return 0;
            f2c_array_indent(cleanup, depth);
            if (expression->type == TYPE_DERIVED) {
                f2c_buffer_printf(cleanup, "f2c_destroy_array_%s(%s, %s, 1U);\n",
                                  expression->derived_type->c_name, expression->lowered_c,
                                  expression->lowered_extent_c);
                f2c_array_indent(cleanup, depth);
            }
            f2c_buffer_printf(cleanup, "free(%s);\n", expression->lowered_c);
        }
    }
    return 1;
}

static int begin_temporary_output(Context *context, Buffer *output, Buffer *saved) {
    if (context == NULL || output == NULL || saved == NULL)
        return 0;
    if (output == &context->output)
        return 1;
    *saved = context->output;
    context->output = *output;
    return 2;
}

static void end_temporary_output(Context *context, Buffer *output, const Buffer *saved,
                                 int output_state) {
    if (output_state != 2)
        return;
    *output = context->output;
    context->output = *saved;
}

static void rollback_temporary_output(Context *context, size_t output_start) {
    context->output.length = output_start;
    if (context->output.data != NULL)
        context->output.data[output_start] = '\0';
}

int f2c_array_emit_numeric_constructor_temporary(Context *context, Unit *unit,
                                                 const F2cExpr *constructor, const char *storage,
                                                 const char *count, const char *capacity,
                                                 Buffer *output, int depth) {
    const size_t output_start = output != NULL ? output->length : 0U;
    Buffer saved_output = {0};
    Symbol target;
    int output_state;
    int result = 0;
    if (context == NULL || unit == NULL || constructor == NULL || storage == NULL ||
        count == NULL || capacity == NULL || output == NULL ||
        constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || constructor->rank != 1U ||
        (!f2c_type_is_numeric(constructor->type) && constructor->type != TYPE_LOGICAL))
        return 0;
    memset(&target, 0, sizeof(target));
    target.type = constructor->type;
    target.kind = constructor->type_kind;
    output_state = begin_temporary_output(context, output, &saved_output);
    if (output_state == 0)
        return 0;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s *%s = NULL;\n", f2c_symbol_c_type(&target), storage);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t %s = 0U;\n", count);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t %s = 0U;\n", capacity);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_array_emit_constructor_values(context, unit, &target, constructor, storage, count,
                                           capacity, NULL, NULL, 0, 1, 0, depth + 1))
        goto cleanup;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    if (!result)
        rollback_temporary_output(context, output_start);
    end_temporary_output(context, output, &saved_output, output_state);
    return result;
}

int f2c_array_emit_character_constructor_temporary(Context *context, Unit *unit,
                                                   const F2cExpr *constructor, const char *storage,
                                                   const char *count, const char *capacity,
                                                   const char *character_length,
                                                   const char *character_length_set, Buffer *output,
                                                   int depth) {
    const size_t output_start = output != NULL ? output->length : 0U;
    Buffer saved_output = {0};
    Symbol target;
    int output_state;
    int result = 0;
    if (context == NULL || unit == NULL || constructor == NULL || storage == NULL ||
        count == NULL || capacity == NULL || character_length == NULL ||
        character_length_set == NULL || output == NULL ||
        constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || constructor->rank != 1U ||
        constructor->type != TYPE_CHARACTER)
        return 0;
    memset(&target, 0, sizeof(target));
    target.type = TYPE_CHARACTER;
    target.kind = constructor->type_kind;
    output_state = begin_temporary_output(context, output, &saved_output);
    if (output_state == 0)
        return 0;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "char *%s = NULL;\n", storage);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t %s = 0U;\n", count);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t %s = 0U;\n", capacity);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t %s = 0U;\n", character_length);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "bool %s = false;\n", character_length_set);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_array_emit_constructor_values(context, unit, &target, constructor, storage, count,
                                           capacity, character_length, character_length_set, 1, 1,
                                           1, depth + 1))
        goto cleanup;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    if (!result)
        rollback_temporary_output(context, output_start);
    end_temporary_output(context, output, &saved_output, output_state);
    return result;
}

int f2c_array_emit_derived_constructor_temporary(Context *context, Unit *unit,
                                                 const F2cExpr *constructor, const char *storage,
                                                 const char *count, const char *capacity,
                                                 Buffer *output, int depth) {
    const size_t output_start = output != NULL ? output->length : 0U;
    Buffer saved_output = {0};
    Symbol target;
    int output_state;
    int result = 0;
    if (context == NULL || unit == NULL || constructor == NULL || storage == NULL ||
        count == NULL || capacity == NULL || output == NULL ||
        constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || constructor->rank != 1U ||
        constructor->type != TYPE_DERIVED || constructor->derived_type == NULL ||
        constructor->derived_type->c_name == NULL)
        return 0;
    memset(&target, 0, sizeof(target));
    target.type = TYPE_DERIVED;
    target.derived_type = constructor->derived_type;
    target.c_type = constructor->derived_type->c_name;
    output_state = begin_temporary_output(context, output, &saved_output);
    if (output_state == 0)
        return 0;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s *%s = NULL;\n", target.c_type, storage);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t %s = 0U;\n", count);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t %s = 0U;\n", capacity);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_array_emit_constructor_values(context, unit, &target, constructor, storage, count,
                                           capacity, NULL, NULL, 0, 1, 0, depth + 1))
        goto cleanup;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    if (!result)
        rollback_temporary_output(context, output_start);
    end_temporary_output(context, output, &saved_output, output_state);
    return result;
}
