#include "codegen/allocation/private.h"

#include "codegen/descriptor/private.h"
#include "codegen/lowering/private.h"
#include "codegen/statement/private.h"
#include "codegen/value/private.h"

#include <stdlib.h>
#include <string.h>

static char *emit_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

static void model_name(Buffer *name, size_t identifier, const char *suffix) {
    f2c_buffer_printf(name, "f2c_allocate_model_%zu_%s", identifier, suffix);
}

static int expression_is_whole_array(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
           expression->rank != 0U;
}

static int prepare_model_expression(Context *context, Unit *unit, const F2cExpr *expression,
                                    int depth, F2cAllocationModel *model) {
    Buffer prelude = {0};
    size_t temporary = 0U;
    if (expression->rank == 0U && !f2c_array_contains_unmaterialized_value(unit, expression))
        return 1;
    model->owned_expression = f2c_array_clone_expression(unit, expression);
    if (model->owned_expression == NULL ||
        !f2c_array_materialize_constructors(context, unit, model->owned_expression,
                                            model->identifier, "allocate", &temporary, &prelude,
                                            &model->cleanup, depth)) {
        free(prelude.data);
        return 0;
    }
    model->expression = model->owned_expression;
    if (prelude.data != NULL)
        f2c_buffer_append(&context->output, prelude.data);
    free(prelude.data);
    return 1;
}

static int prepare_array_expression(Context *context, Unit *unit, int depth,
                                    F2cAllocationModel *model) {
    size_t dimension;
    if (!f2c_array_value_view(unit, model->expression, &model->array))
        return 0;
    if (model->source &&
        !f2c_array_value_materialize(context, unit, &model->array, "allocate_model", depth))
        return 0;
    for (dimension = 0U; dimension < model->array.rank; ++dimension) {
        Buffer name = {0};
        char *extent;
        model_name(&name, model->identifier, "extent");
        f2c_buffer_printf(&name, "_%zu", dimension + 1U);
        if (name.data == NULL || model->array.extents[dimension] == NULL) {
            free(name.data);
            return 0;
        }
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const size_t %s = (size_t)(%s);\n", name.data,
                          model->array.extents[dimension]);
        /* Explicit allocation bounds need not consume every MOLD extent. */
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "(void)%s;\n", name.data);
        extent = f2c_buffer_take(&name);
        free(model->array.extents[dimension]);
        model->array.extents[dimension] = extent;
    }
    if (model->array.type == TYPE_CHARACTER) {
        Buffer name = {0};
        model_name(&name, model->identifier, "character_length");
        if (name.data == NULL || model->array.element_length == NULL) {
            free(name.data);
            return 0;
        }
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const size_t %s = (size_t)(%s);\n", name.data,
                          model->array.element_length);
        free(model->array.element_length);
        model->array.element_length = f2c_strdup(name.data);
        model->character_length = f2c_buffer_take(&name);
        if (model->array.element_length == NULL || model->character_length == NULL)
            return 0;
    }
    return 1;
}

static int prepare_character_scalar(Context *context, Unit *unit, const F2cExpr *expression,
                                    int depth, F2cAllocationModel *model) {
    Buffer pointer_name = {0};
    Buffer length_name = {0};
    char *code = emit_expression(unit, expression);
    char *pointer = code != NULL ? f2c_character_source_pointer(unit, expression, code) : NULL;
    char *length = f2c_character_length_expression(unit, expression);
    model_name(&pointer_name, model->identifier, "character");
    model_name(&length_name, model->identifier, "character_length");
    if (code == NULL || pointer == NULL || length == NULL || pointer_name.data == NULL ||
        length_name.data == NULL) {
        free(code);
        free(pointer);
        free(length);
        free(pointer_name.data);
        free(length_name.data);
        return 0;
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t %s = (size_t)(%s);\n", length_name.data,
                      length);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const char *const %s = %s;\n", pointer_name.data, pointer);
    model->scalar_name = f2c_buffer_take(&pointer_name);
    model->character_length = f2c_buffer_take(&length_name);
    free(code);
    free(pointer);
    free(length);
    return model->scalar_name != NULL && model->character_length != NULL;
}

static int prepare_derived_scalar(Context *context, Unit *unit, const F2cExpr *expression,
                                  int depth, F2cAllocationModel *model) {
    Buffer name = {0};
    if (expression->derived_type == NULL || expression->derived_type->c_name == NULL)
        return 0;
    model_name(&name, model->identifier, "derived");
    if (name.data == NULL)
        return 0;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s %s;\n", expression->derived_type->c_name, name.data);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "f2c_initialize_%s(&%s);\n",
                      expression->derived_type->c_name, name.data);
    if (!f2c_emit_derived_clone_expression(&context->output, unit, expression, name.data,
                                           "allocate_model", model->identifier, depth)) {
        free(name.data);
        return 0;
    }
    model->scalar_name = f2c_buffer_take(&name);
    return model->scalar_name != NULL;
}

static int prepare_numeric_scalar(Context *context, Unit *unit, const F2cExpr *expression,
                                  int depth, F2cAllocationModel *model) {
    Buffer name = {0};
    char *code = emit_expression(unit, expression);
    model_name(&name, model->identifier, "scalar");
    if (code == NULL || name.data == NULL) {
        free(code);
        free(name.data);
        return 0;
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const %s %s = (%s)(%s);\n",
                      f2c_expression_c_type(expression), name.data,
                      f2c_expression_c_type(expression), code);
    model->scalar_name = f2c_buffer_take(&name);
    free(code);
    return model->scalar_name != NULL;
}

static int append_availability_checks(Buffer *condition, Unit *unit, const F2cExpr *expression) {
    size_t child;
    if (expression == NULL)
        return 1;
    if (expression->symbol != NULL && !expression->symbol->external &&
        (expression->symbol->allocatable || expression->symbol->pointer) &&
        (expression->kind == F2C_EXPR_NAME || expression->kind == F2C_EXPR_ARRAY_REFERENCE ||
         expression->kind == F2C_EXPR_COMPONENT)) {
        char *storage = f2c_descriptor_storage_designator(unit, expression);
        if (storage == NULL)
            return 0;
        f2c_buffer_printf(condition, " && %s != NULL", storage);
        free(storage);
    }
    for (child = 0U; child < expression->child_count; ++child)
        if (!append_availability_checks(condition, unit, expression->children[child]))
            return 0;
    return 1;
}

static int emit_availability(Context *context, Unit *unit, const F2cExpr *expression, int depth,
                             F2cAllocationModel *model) {
    Buffer name = {0};
    Buffer condition = {0};
    model_name(&name, model->identifier, "available");
    f2c_buffer_append(&condition, "true");
    if (name.data == NULL || !append_availability_checks(&condition, unit, expression)) {
        free(name.data);
        free(condition.data);
        return 0;
    }
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const bool %s = %s;\n", name.data, condition.data);
    free(condition.data);
    model->availability = f2c_buffer_take(&name);
    return model->availability != NULL;
}

int f2c_allocation_model_prepare(Context *context, Unit *unit, const F2cStatement *statement,
                                 const F2cExpr *expression, int source, int depth,
                                 F2cAllocationModel *model) {
    size_t identifier;
    if (context == NULL || unit == NULL || statement == NULL || model == NULL)
        return 0;
    memset(model, 0, sizeof(*model));
    if (expression == NULL) {
        model->prepared = 1;
        return 1;
    }
    identifier = f2c_statement_unit_index(unit, statement);
    model->identifier = identifier != SIZE_MAX ? identifier : statement->line;
    model->expression = expression;
    model->source = source;
    if (!emit_availability(context, unit, expression, depth, model))
        return 0;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (%s) {\n", model->availability);
    ++depth;
    model->guarded = 1;
    if (!prepare_model_expression(context, unit, expression, depth, model))
        return 0;
    expression = model->expression;
    if (expression->rank != 0U) {
        if (!prepare_array_expression(context, unit, depth, model))
            return 0;
    } else if (source && expression->type == TYPE_CHARACTER) {
        if (!prepare_character_scalar(context, unit, expression, depth, model))
            return 0;
    } else if (source && expression->type == TYPE_DERIVED) {
        if (!prepare_derived_scalar(context, unit, expression, depth, model))
            return 0;
    } else if (source && !prepare_numeric_scalar(context, unit, expression, depth, model)) {
        return 0;
    } else if (!source && expression->type == TYPE_CHARACTER) {
        Buffer length_name = {0};
        char *length = f2c_character_length_expression(unit, expression);
        model_name(&length_name, model->identifier, "character_length");
        if (length == NULL || length_name.data == NULL) {
            free(length);
            free(length_name.data);
            return 0;
        }
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const size_t %s = (size_t)(%s);\n", length_name.data,
                          length);
        model->character_length = f2c_buffer_take(&length_name);
        free(length);
    }
    model->prepared = 1;
    return 1;
}

char *f2c_allocation_model_lower(Unit *unit, const F2cAllocationModel *model, size_t dimension) {
    if (model == NULL || model->expression == NULL || dimension >= model->expression->rank)
        return NULL;
    if (expression_is_whole_array(model->expression))
        return f2c_symbol_dimension_lower(unit, model->expression->symbol, dimension);
    return f2c_strdup("1");
}

char *f2c_allocation_model_upper(Unit *unit, const F2cAllocationModel *model, size_t dimension) {
    Buffer upper = {0};
    char *lower;
    char *extent;
    if (model == NULL || model->expression == NULL || dimension >= model->expression->rank)
        return NULL;
    lower = f2c_allocation_model_lower(unit, model, dimension);
    extent = f2c_allocation_model_extent(model, dimension);
    if (lower == NULL || extent == NULL) {
        free(lower);
        free(extent);
        return NULL;
    }
    f2c_buffer_printf(&upper, "((int64_t)(%s) + (int64_t)(%s) - INT64_C(1))", lower, extent);
    free(lower);
    free(extent);
    return f2c_buffer_take(&upper);
}

char *f2c_allocation_model_extent(const F2cAllocationModel *model, size_t dimension) {
    if (model == NULL || dimension >= model->array.rank || model->array.extents[dimension] == NULL)
        return NULL;
    return f2c_strdup(model->array.extents[dimension]);
}

char *f2c_allocation_model_character_length(Unit *unit, const F2cAllocationModel *model) {
    (void)unit;
    return model != NULL && model->character_length != NULL ? f2c_strdup(model->character_length)
                                                            : NULL;
}

int f2c_allocation_model_emit_source(Context *context, const Symbol *target,
                                     const F2cAllocationModel *model, int depth) {
    const char *source;
    if (context == NULL || target == NULL || model == NULL || !model->source ||
        model->expression == NULL)
        return model != NULL && (!model->source || model->expression == NULL);
    source = model->expression->rank != 0U ? model->array.pointer : model->scalar_name;
    if (source == NULL)
        return 0;
    if (target->type == TYPE_CHARACTER) {
        const char *source_length = model->character_length;
        if (source_length == NULL)
            return 0;
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_alloc_copy_length = F2C_MIN(f2c_alloc_char_len, %s);\n",
                          source_length);
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "for (size_t f2c_alloc_index = 0U; f2c_alloc_index < "
                                            "f2c_alloc_count; ++f2c_alloc_index) {\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_alloc_source_index = %s;\n",
                          model->expression->rank == 0U ? "0U" : "f2c_alloc_index");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_alloc_copy_length != 0U) memmove(f2c_alloc_storage + "
                          "f2c_alloc_index * f2c_alloc_char_len, %s + "
                          "f2c_alloc_source_index * %s, f2c_alloc_copy_length);\n",
                          source, source_length);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (f2c_alloc_char_len > f2c_alloc_copy_length) memset("
                          "f2c_alloc_storage + f2c_alloc_index * f2c_alloc_char_len + "
                          "f2c_alloc_copy_length, ' ', f2c_alloc_char_len - "
                          "f2c_alloc_copy_length);\n");
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    } else if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "for (size_t f2c_alloc_index = 0U; f2c_alloc_index < "
                                            "f2c_alloc_count; ++f2c_alloc_index)\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "f2c_clone_%s(&f2c_alloc_storage[f2c_alloc_index], &%s%s);\n",
                          target->derived_type->c_name, source,
                          model->expression->rank == 0U ? "" : "[f2c_alloc_index]");
    } else {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "for (size_t f2c_alloc_index = 0U; f2c_alloc_index < "
                                            "f2c_alloc_count; ++f2c_alloc_index)\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_alloc_storage[f2c_alloc_index] = (%s)%s%s;\n",
                          f2c_symbol_c_type(target), source,
                          model->expression->rank == 0U ? "" : "[f2c_alloc_index]");
    }
    return 1;
}

void f2c_allocation_model_emit_cleanup(Context *context, Unit *unit,
                                       const F2cAllocationModel *model, int depth) {
    if (context == NULL || unit == NULL || model == NULL)
        return;
    if (model->source && model->expression != NULL && model->expression->rank == 0U &&
        model->expression->type == TYPE_DERIVED && model->expression->derived_type != NULL &&
        model->scalar_name != NULL) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_discard_array_%s(&%s, 1U);\n",
                          model->expression->derived_type->c_name, model->scalar_name);
    }
    f2c_array_value_emit_cleanup(context, &model->array, depth);
    (void)f2c_array_cleanup_emit(&context->output, unit, &model->cleanup);
}

void f2c_allocation_model_clear(Unit *unit, F2cAllocationModel *model) {
    if (model == NULL)
        return;
    f2c_codegen_expression_free(unit, model->owned_expression);
    f2c_array_value_clear(&model->array);
    f2c_array_cleanup_clear(&model->cleanup);
    free(model->scalar_name);
    free(model->character_length);
    free(model->availability);
    memset(model, 0, sizeof(*model));
}
