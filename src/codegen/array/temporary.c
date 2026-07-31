#include "codegen/array/private.h"

#include "codegen/lowering/private.h"
#include "codegen/transform/private.h"

#include <stdlib.h>
#include <string.h>

static int begin_temporary_output(Context *context, Buffer *output, Buffer *saved);
static void end_temporary_output(Context *context, Buffer *output, const Buffer *saved,
                                 int output_state);
static void rollback_temporary_output(Context *context, size_t output_start);

static int trivial_scalar(const F2cExpr *expression) {
    return expression->kind == F2C_EXPR_INVALID || expression->kind == F2C_EXPR_INTEGER_LITERAL ||
           expression->kind == F2C_EXPR_REAL_LITERAL ||
           expression->kind == F2C_EXPR_STRING_LITERAL ||
           expression->kind == F2C_EXPR_LOGICAL_LITERAL || expression->kind == F2C_EXPR_NAME ||
           expression->kind == F2C_EXPR_ABSENT_ARGUMENT;
}

static int array_inquiry_call(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           (expression->intrinsic == F2C_INTRINSIC_SHAPE ||
            expression->intrinsic == F2C_INTRINSIC_LBOUND ||
            expression->intrinsic == F2C_INTRINSIC_UBOUND);
}

static int transfer_mold_argument(const F2cExpr *call, size_t child) {
    const F2cExpr *argument;
    if (call == NULL || call->kind != F2C_EXPR_CALL ||
        call->intrinsic != F2C_INTRINSIC_TRANSFER || child >= call->child_count)
        return 0;
    argument = call->children[child];
    if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT)
        return argument->text != NULL && strcmp(argument->text, "mold") == 0;
    return child == 1U;
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
        if (expression->type == TYPE_DERIVED || expression->type == TYPE_CHARACTER)
            return 1;
        if (expression->type == TYPE_UNKNOWN)
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
        f2c_array_indent(prelude, depth);
        f2c_buffer_printf(prelude, "(void)%s;\n", name.data);
        free(code);
        return f2c_lowering_take_code(unit, expression, f2c_buffer_take(&name));
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

static int array_transform_call(const Unit *unit, const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL && expression->rank != 0U &&
           f2c_lowering_code(unit, expression) == NULL &&
           f2c_intrinsic_is_transformational(expression->intrinsic);
}

int f2c_array_contains_unmaterialized_value(const Unit *unit, const F2cExpr *expression) {
    size_t child;
    if (array_transform_call(unit, expression) || f2c_array_function_result_call(unit, expression))
        return 1;
    if (expression == NULL)
        return 0;
    for (child = 0U; child < expression->child_count; ++child)
        if (f2c_array_contains_unmaterialized_value(unit, expression->children[child]))
            return 1;
    return 0;
}

static int set_named_array_temporary(Unit *unit, F2cExpr *expression, Buffer *name) {
    Buffer length = {0};
    if (unit == NULL || expression == NULL || name == NULL || name->data == NULL)
        return 0;
    if (expression->type == TYPE_CHARACTER)
        f2c_buffer_printf(&length, "f2c_char_len_%s", name->data);
    if (!f2c_lowering_take_code(unit, expression, f2c_buffer_take(name)) ||
        !f2c_lowering_set_array_temporary(unit, expression, 1) ||
        (expression->type == TYPE_CHARACTER &&
         !f2c_lowering_take_character_length(unit, expression, f2c_buffer_take(&length)))) {
        free(length.data);
        return 0;
    }
    return 1;
}

static int materialize_transform(Context *context, Unit *unit, F2cExpr *expression,
                                 size_t identifier, const char *role, size_t *temporary,
                                 Buffer *prelude, F2cArrayCleanupList *cleanup, int depth) {
    const size_t output_start = prelude->length;
    const size_t current = expression->owned_temporary_index;
    const size_t previous_errors = context->result.error_count;
    Buffer saved_output = {0};
    Buffer name = {0};
    F2cExpr left = {0};
    Symbol target;
    size_t dimension;
    int output_state;
    int emitted;
    if (!array_transform_call(unit, expression))
        return 1;
    if (!f2c_array_owned_temporary_valid(unit, expression,
                                         F2C_OWNED_TEMPORARY_TRANSFORMATIONAL_RESULT))
        return 0;
    if (expression->type == TYPE_UNKNOWN ||
        (expression->type == TYPE_DERIVED &&
         (expression->derived_type == NULL || expression->derived_type->c_name == NULL)))
        return 0;
    if (!f2c_array_hoist_scalar_subexpressions(unit, expression, identifier, role, temporary,
                                               prelude, depth, 1))
        return 0;
    memset(&target, 0, sizeof(target));
    f2c_buffer_printf(&name, "f2c_array_%s_transform_%zu_%zu", role, identifier, current);
    if (name.data == NULL)
        return 0;
    target.c_name = name.data;
    target.type = expression->type;
    target.kind = expression->type_kind;
    target.rank = expression->rank;
    target.allocatable = 1;
    target.deferred_character = expression->type == TYPE_CHARACTER;
    target.derived_type = expression->derived_type;
    target.c_type = expression->type == TYPE_DERIVED ? expression->derived_type->c_name : NULL;
    left.kind = F2C_EXPR_NAME;
    left.type = expression->type;
    left.type_kind = expression->type_kind;
    left.rank = expression->rank;
    left.symbol = &target;
    left.derived_type = expression->derived_type;
    output_state = begin_temporary_output(context, prelude, &saved_output);
    if (output_state == 0) {
        free(name.data);
        return 0;
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s *%s = NULL;\n", f2c_symbol_c_type(&target), name.data);
    if (target.deferred_character) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "size_t f2c_char_len_%s = 0U;\n", name.data);
    }
    for (dimension = 0U; dimension < target.rank; ++dimension) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "int32_t %s_lower_%zu = 1, %s_extent_%zu = 0;\n",
                          name.data, dimension + 1U, name.data, dimension + 1U);
    }
    emitted = f2c_emit_transform_assignment(context, unit, &left, expression, identifier, depth);
    if (!emitted || context->result.error_count != previous_errors) {
        rollback_temporary_output(context, output_start);
        end_temporary_output(context, prelude, &saved_output, output_state);
        free(name.data);
        return 0;
    }
    for (dimension = 0U; dimension < target.rank; ++dimension) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "(void)%s_lower_%zu; (void)%s_extent_%zu;\n", name.data,
                          dimension + 1U, name.data, dimension + 1U);
    }
    end_temporary_output(context, prelude, &saved_output, output_state);
    if (!set_named_array_temporary(unit, expression, &name))
        return 0;
    return f2c_array_cleanup_append(unit, cleanup, expression, depth);
}

static int scalar_context_requires_elemental_temporary(const Unit *unit, const F2cExpr *expression,
                                                       const char *role) {
    return expression != NULL && role != NULL && strcmp(role, "scalar") == 0 &&
           expression->rank != 0U && f2c_lowering_code(unit, expression) == NULL &&
           (expression->kind == F2C_EXPR_UNARY || expression->kind == F2C_EXPR_BINARY);
}

static int materialize_elemental_value(Context *context, Unit *unit, F2cExpr *expression,
                                       size_t identifier, const char *role, size_t *temporary,
                                       Buffer *prelude, F2cArrayCleanupList *cleanup, int depth) {
    const size_t output_start = prelude->length;
    const size_t current = expression->owned_temporary_index;
    const size_t previous_errors = context->result.error_count;
    Buffer saved_output = {0};
    Buffer name = {0};
    Symbol target;
    int output_state;
    int emitted;
    (void)temporary;
    if (!scalar_context_requires_elemental_temporary(unit, expression, role))
        return 1;
    if (!f2c_array_owned_temporary_valid(unit, expression,
                                         F2C_OWNED_TEMPORARY_ELEMENTAL_ARRAY_VALUE))
        return 0;
    if (expression->type == TYPE_UNKNOWN ||
        (expression->type == TYPE_DERIVED &&
         (expression->derived_type == NULL || expression->derived_type->c_name == NULL)))
        return 0;
    memset(&target, 0, sizeof(target));
    f2c_buffer_printf(&name, "f2c_array_%s_elemental_%zu_%zu", role, identifier, current);
    if (name.data == NULL)
        return 0;
    target.c_name = name.data;
    target.type = expression->type;
    target.kind = expression->type_kind;
    target.rank = expression->rank;
    target.allocatable = 1;
    target.deferred_character = expression->type == TYPE_CHARACTER;
    target.derived_type = expression->derived_type;
    target.c_type = expression->type == TYPE_DERIVED ? expression->derived_type->c_name : NULL;
    output_state = begin_temporary_output(context, prelude, &saved_output);
    if (output_state == 0) {
        free(name.data);
        return 0;
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s *%s = NULL;\n", f2c_symbol_c_type(&target), name.data);
    if (target.deferred_character) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "size_t f2c_char_len_%s = 0U;\n", name.data);
    }
    for (size_t dimension = 0U; dimension < target.rank; ++dimension) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "int32_t %s_lower_%zu = 1, %s_extent_%zu = 0;\n",
                          name.data, dimension + 1U, name.data, dimension + 1U);
    }
    emitted =
        f2c_array_emit_elemental_assignment(context, unit, &target, expression, identifier, depth);
    if (!emitted || context->result.error_count != previous_errors) {
        rollback_temporary_output(context, output_start);
        end_temporary_output(context, prelude, &saved_output, output_state);
        free(name.data);
        return 0;
    }
    for (size_t dimension = 0U; dimension < target.rank; ++dimension) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "(void)%s_lower_%zu; (void)%s_extent_%zu;\n", name.data,
                          dimension + 1U, name.data, dimension + 1U);
    }
    end_temporary_output(context, prelude, &saved_output, output_state);
    if (!set_named_array_temporary(unit, expression, &name))
        return 0;
    return f2c_array_cleanup_append(unit, cleanup, expression, depth);
}

int f2c_array_materialize_constructors(Context *context, Unit *unit, F2cExpr *expression,
                                       size_t identifier, const char *role, size_t *temporary,
                                       Buffer *prelude, F2cArrayCleanupList *cleanup, int depth) {
    size_t child;
    if (context == NULL || unit == NULL || temporary == NULL || prelude == NULL ||
        cleanup == NULL || role == NULL)
        return 0;
    if (expression == NULL)
        return 1;
    if (array_inquiry_call(expression))
        return 1;
    if (expression->kind != F2C_EXPR_ARRAY_CONSTRUCTOR) {
        for (child = 0U; child < expression->child_count; ++child) {
            if (transfer_mold_argument(expression, child))
                continue;
            if (!f2c_array_materialize_constructors(context, unit, expression->children[child],
                                                    identifier, role, temporary, prelude, cleanup,
                                                    depth))
                return 0;
        }
    }
    if (!f2c_array_materialize_function_result(unit, expression, identifier, role, temporary,
                                               prelude, cleanup, depth))
        return 0;
    if (!materialize_transform(context, unit, expression, identifier, role, temporary, prelude,
                               cleanup, depth))
        return 0;
    if (!materialize_elemental_value(context, unit, expression, identifier, role, temporary,
                                     prelude, cleanup, depth))
        return 0;
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR &&
        f2c_lowering_code(unit, expression) == NULL) {
        Buffer name = {0};
        char *code = NULL;
        int supported = 0;
        if (expression->rank != 1U || expression->type == TYPE_UNKNOWN)
            return 0;
        if (!f2c_array_owned_temporary_valid(unit, expression,
                                             F2C_OWNED_TEMPORARY_ARRAY_CONSTRUCTOR))
            return 0;
        if (expression->type != TYPE_CHARACTER && expression->type != TYPE_DERIVED &&
            flat_array_constructor(expression)) {
            f2c_buffer_printf(&name, "f2c_array_%s_constructor_%zu_%zu", role, identifier,
                              expression->owned_temporary_index);
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
            return f2c_lowering_take_code(unit, expression, f2c_buffer_take(&name));
        } else {
            Buffer count = {0};
            Buffer capacity = {0};
            Buffer character_length = {0};
            Buffer character_length_set = {0};
            const size_t current = expression->owned_temporary_index;
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
            if (!f2c_lowering_take_code(unit, expression, f2c_buffer_take(&name)) ||
                !f2c_lowering_take_extent(unit, expression, f2c_buffer_take(&count)) ||
                (expression->type == TYPE_CHARACTER &&
                 !f2c_lowering_take_character_length(unit, expression,
                                                     f2c_buffer_take(&character_length)))) {
                free(capacity.data);
                free(character_length.data);
                free(character_length_set.data);
                return 0;
            }
            free(capacity.data);
            free(character_length_set.data);
            if (!f2c_array_cleanup_append(unit, cleanup, expression, depth))
                return 0;
        }
    }
    return 1;
}

int f2c_array_emit_prepared_transform_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                                 const F2cExpr *right, size_t line, int depth) {
    const size_t output_start = context != NULL ? context->output.length : 0U;
    F2cExpr *prepared = NULL;
    Buffer prelude = {0};
    F2cArrayCleanupList cleanup = {0};
    size_t temporary = 0U;
    size_t child;
    int emitted = 0;
    if (context == NULL || unit == NULL || left == NULL || right == NULL ||
        right->kind != F2C_EXPR_CALL || right->rank == 0U)
        return 0;
    for (child = 0U; child < right->child_count; ++child)
        if (f2c_array_contains_unmaterialized_value(unit, right->children[child]))
            break;
    if (child == right->child_count)
        return 0;
    prepared = f2c_array_clone_expression(unit, right);
    if (prepared == NULL)
        goto done;
    for (child = 0U; child < prepared->child_count; ++child)
        if (!f2c_array_materialize_constructors(context, unit, prepared->children[child], line,
                                                "transform", &temporary, &prelude, &cleanup,
                                                depth + 1))
            goto done;
    if (prelude.length == 0U)
        goto done;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_buffer_append(&context->output, prelude.data);
    if (!f2c_emit_transform_assignment(context, unit, left, prepared, line, depth + 1)) {
        context->output.length = output_start;
        if (context->output.data != NULL)
            context->output.data[output_start] = '\0';
        goto done;
    }
    (void)f2c_array_cleanup_emit(&context->output, unit, &cleanup);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    emitted = 1;

done:
    f2c_codegen_expression_free(unit, prepared);
    free(prelude.data);
    f2c_array_cleanup_clear(&cleanup);
    return emitted;
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
