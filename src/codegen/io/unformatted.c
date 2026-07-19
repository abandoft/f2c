#include "codegen/io/unformatted/private.h"

#include <stdlib.h>

static void emit_binary_operation(Context *context, const char *status, const char *operation,
                                  const char *stream, const char *address, const char *size,
                                  int depth) {
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (%s == F2C_IO_STATUS_OK && !f2c_binary_%s(%s, %s, "
                      "(size_t)(%s))) %s = F2C_IO_STATUS_RECORD;\n",
                      status, operation, stream, address, size, status);
}

static void emit_binary_character(Context *context, Unit *unit, const F2cExpr *expression,
                                  const char *value, int input, const char *stream,
                                  const char *status, int depth) {
    char *pointer = f2c_character_source_pointer(unit, expression, value);
    char *length = f2c_character_length_expression(unit, expression);
    if (pointer != NULL && length != NULL)
        emit_binary_operation(context, status, input ? "read" : "write", stream, pointer, length,
                              depth);
    free(pointer);
    free(length);
}

static void emit_binary_logical(Context *context, const F2cExpr *expression, const char *value,
                                int input, const char *stream, const char *status, int depth) {
    const int kind = expression->type_kind > 0 ? expression->type_kind : 4;
    const char *integer_type = f2c_c_type_kind(TYPE_INTEGER, kind);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "{ %s f2c_unformatted_logical", integer_type);
    if (!input)
        f2c_buffer_printf(&context->output, " = (%s) ? (%s)1 : (%s)0", value, integer_type,
                          integer_type);
    f2c_buffer_append(&context->output, ";\n");
    emit_binary_operation(context, status, input ? "read" : "write", stream,
                          "&f2c_unformatted_logical", "sizeof(f2c_unformatted_logical)", depth + 1);
    if (input) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK) %s = f2c_unformatted_logical != 0;\n",
                          status, value);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

static void emit_binary_complex(Context *context, const F2cExpr *expression, const char *value,
                                int input, const char *stream, const char *status, int depth) {
    const int kind = expression->type_kind > 0 ? expression->type_kind
                                               : (expression->type == TYPE_DOUBLE_COMPLEX ? 8 : 4);
    const char *real_type = f2c_c_type_kind(TYPE_REAL, kind);
    const char *real_part = kind == 4 ? "crealf" : "creal";
    const char *imaginary_part = kind == 4 ? "cimagf" : "cimag";
    const char *constructor = kind == 4 ? "f2c_make_c" : "f2c_make_z";
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{ ");
    if (!input)
        f2c_buffer_printf(&context->output, "%s f2c_unformatted_value = (%s); ",
                          f2c_expression_c_type(expression), value);
    f2c_buffer_printf(&context->output, "%s f2c_unformatted_real", real_type);
    if (!input)
        f2c_buffer_printf(&context->output, " = (%s)%s(f2c_unformatted_value)", real_type,
                          real_part);
    f2c_buffer_append(&context->output, ", f2c_unformatted_imaginary");
    if (!input)
        f2c_buffer_printf(&context->output, " = (%s)%s(f2c_unformatted_value)", real_type,
                          imaginary_part);
    f2c_buffer_append(&context->output, ";\n");
    emit_binary_operation(context, status, input ? "read" : "write", stream,
                          "&f2c_unformatted_real", "sizeof(f2c_unformatted_real)", depth + 1);
    emit_binary_operation(context, status, input ? "read" : "write", stream,
                          "&f2c_unformatted_imaginary", "sizeof(f2c_unformatted_imaginary)",
                          depth + 1);
    if (input) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK) %s = %s(f2c_unformatted_real, "
                          "f2c_unformatted_imaginary);\n",
                          status, value, constructor);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

static void emit_binary_numeric(Context *context, const F2cExpr *expression, const char *value,
                                int input, const char *stream, const char *status, int depth) {
    const char *type = f2c_expression_c_type(expression);
    if (input) {
        Buffer address = {0};
        Buffer size = {0};
        f2c_buffer_printf(&address, "&(%s)", value);
        f2c_buffer_printf(&size, "sizeof(%s)", value);
        emit_binary_operation(context, status, "read", stream, address.data, size.data, depth);
        free(address.data);
        free(size.data);
        return;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "{ %s f2c_unformatted_value = (%s)(%s);\n", type, type,
                      value);
    emit_binary_operation(context, status, "write", stream, "&f2c_unformatted_value",
                          "sizeof(f2c_unformatted_value)", depth + 1);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

void f2c_io_emit_unformatted_scalar(Context *context, Unit *unit, const F2cExpr *expression,
                                    const char *value, int input, const char *stream,
                                    const char *status, int depth) {
    if (expression->type == TYPE_CHARACTER) {
        emit_binary_character(context, unit, expression, value, input, stream, status, depth);
    } else if (expression->type == TYPE_LOGICAL) {
        emit_binary_logical(context, expression, value, input, stream, status, depth);
    } else if (expression->type == TYPE_COMPLEX || expression->type == TYPE_DOUBLE_COMPLEX) {
        emit_binary_complex(context, expression, value, input, stream, status, depth);
    } else {
        emit_binary_numeric(context, expression, value, input, stream, status, depth);
    }
}

void f2c_io_emit_unformatted_derived_scalar(Context *context, Unit *unit, F2cDerivedType *derived,
                                            const char *value, int input, const char *stream,
                                            const char *unit_number, const char *status, int depth);

static void emit_default_component(Context *context, Unit *unit, Symbol *component,
                                   const char *owner, int input, const char *stream,
                                   const char *unit_number, const char *status, int depth) {
    const char *name = f2c_symbol_c_name(unit, component);
    char *count = component->rank != 0U ? f2c_symbol_element_count(unit, component) : NULL;
    char *character_length =
        component->type == TYPE_CHARACTER ? f2c_symbol_character_length(unit, component) : NULL;
    Buffer base = {0};
    Buffer index_name = {0};
    F2cExpr expression = {0};
    f2c_buffer_printf(&base, "(%s).%s", owner, name);
    expression.kind = F2C_EXPR_COMPONENT;
    expression.type = component->type;
    expression.type_kind = component->kind;
    expression.symbol = component;
    expression.derived_type = component->derived_type;
    expression.value_category = F2C_VALUE_VARIABLE;
    expression.definable = 1;
    expression.shape.kind = F2C_SHAPE_SCALAR;
    expression.lowered_character_length_c = character_length;
    if (component->rank == 0U) {
        if (component->type == TYPE_DERIVED && component->derived_type != NULL)
            f2c_io_emit_unformatted_derived_scalar(context, unit, component->derived_type,
                                                   base.data, input, stream, unit_number, status,
                                                   depth);
        else
            f2c_io_emit_unformatted_scalar(context, unit, &expression, base.data, input, stream,
                                           status, depth);
    } else {
        f2c_buffer_printf(&index_name, "f2c_component_index_%d", depth);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "for (size_t %s = 0U; %s < %s; ++%s) {\n",
                          index_name.data, index_name.data, count != NULL ? count : "0U",
                          index_name.data);
        {
            Buffer element = {0};
            if (component->type == TYPE_CHARACTER)
                f2c_buffer_printf(&element, "%s + %s * (size_t)(%s)", base.data, index_name.data,
                                  character_length != NULL ? character_length : "1U");
            else
                f2c_buffer_printf(&element, "%s[%s]", base.data, index_name.data);
            if (component->type == TYPE_DERIVED && component->derived_type != NULL)
                f2c_io_emit_unformatted_derived_scalar(context, unit, component->derived_type,
                                                       element.data, input, stream, unit_number,
                                                       status, depth + 1);
            else
                f2c_io_emit_unformatted_scalar(context, unit, &expression, element.data, input,
                                               stream, status, depth + 1);
            free(element.data);
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    free(base.data);
    free(index_name.data);
    free(count);
    free(character_length);
}

void f2c_io_emit_unformatted_derived_scalar(Context *context, Unit *unit, F2cDerivedType *derived,
                                            const char *value, int input, const char *stream,
                                            const char *unit_number, const char *status,
                                            int depth) {
    const F2cDefinedIoKind kind =
        input ? F2C_DEFINED_IO_READ_UNFORMATTED : F2C_DEFINED_IO_WRITE_UNFORMATTED;
    size_t index;
    if (f2c_io_defined_binding(derived, kind) != NULL) {
        if (!f2c_io_emit_defined_io_call(context, value, derived, kind, unit_number, NULL, NULL,
                                         "0U", status, depth)) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = F2C_IO_STATUS_RECORD;\n", status);
        }
        return;
    }
    if (derived->parent != NULL) {
        Buffer parent = {0};
        f2c_buffer_printf(&parent, "(%s).parent", value);
        f2c_io_emit_unformatted_derived_scalar(context, unit, derived->parent, parent.data, input,
                                               stream, unit_number, status, depth);
        free(parent.data);
    }
    for (index = 0U; index < derived->component_count; ++index)
        emit_default_component(context, unit, &derived->components[index], value, input, stream,
                               unit_number, status, depth);
}

static int emit_derived_item(Context *context, Unit *unit, const F2cIoItem *item,
                             F2cDerivedType *derived, int input, const char *stream,
                             const char *unit_number, const char *status, int depth) {
    char *value = f2c_io_emit_item_expression(unit, item);
    if (value == NULL)
        return 0;
    if (!input && f2c_io_defined_binding(derived, F2C_DEFINED_IO_WRITE_UNFORMATTED) == NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "{ %s f2c_unformatted_derived_value = (%s);\n",
                          derived->c_name, value);
        f2c_io_emit_unformatted_derived_scalar(context, unit, derived,
                                               "f2c_unformatted_derived_value", input, stream,
                                               unit_number, status, depth + 1);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    } else {
        f2c_io_emit_unformatted_derived_scalar(context, unit, derived, value, input, stream,
                                               unit_number, status, depth);
    }
    free(value);
    return 1;
}

static int emit_implied_do(Context *context, Unit *unit, const F2cIoItem *item, int input,
                           const char *stream, const char *unit_number, const char *status,
                           int depth) {
    const char *iterator_type = f2c_expression_c_type(item->iterator);
    char *iterator = f2c_io_emit_required_expression(unit, item->iterator);
    char *initial = f2c_io_emit_required_expression(unit, item->initial);
    char *limit = f2c_io_emit_required_expression(unit, item->limit);
    char *step = f2c_io_emit_required_expression(unit, item->step);
    size_t index;
    int result = 0;
    if (iterator == NULL || initial == NULL || limit == NULL || step == NULL)
        goto cleanup;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "int64_t f2c_io_do_value = (int64_t)(%s)(%s);\n",
                      iterator_type, initial);
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const int64_t f2c_io_do_limit = (int64_t)(%s)(%s);\n",
                      iterator_type, limit);
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const int64_t f2c_io_do_step = (int64_t)(%s)(%s);\n",
                      iterator_type, step);
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "if (f2c_io_do_step == 0) %s = F2C_IO_STATUS_OVERFLOW;\n",
                      status);
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "while (%s == F2C_IO_STATUS_OK && "
                      "(f2c_io_do_step > 0 ? f2c_io_do_value <= f2c_io_do_limit : "
                      "f2c_io_do_step < 0 && f2c_io_do_value >= f2c_io_do_limit)) {\n",
                      status);
    f2c_io_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s = (%s)f2c_io_do_value;\n", iterator, iterator_type);
    for (index = 0U; index < item->child_count; ++index)
        if (!f2c_io_emit_unformatted_item(context, unit, &item->children[index], input, stream,
                                          unit_number, status, depth + 2))
            goto cleanup;
    f2c_io_indent(&context->output, depth + 2);
    f2c_buffer_append(
        &context->output,
        "if (f2c_io_do_step > 0) { const uint64_t f2c_io_do_distance = "
        "(uint64_t)f2c_io_do_limit - (uint64_t)f2c_io_do_value; if "
        "((uint64_t)f2c_io_do_step > f2c_io_do_distance) break; } else { const uint64_t "
        "f2c_io_do_magnitude = UINT64_C(0) - (uint64_t)f2c_io_do_step; const uint64_t "
        "f2c_io_do_distance = (uint64_t)f2c_io_do_value - (uint64_t)f2c_io_do_limit; if "
        "(f2c_io_do_magnitude > f2c_io_do_distance) break; }\n");
    f2c_io_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "f2c_io_do_value += f2c_io_do_step;\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    free(iterator);
    free(initial);
    free(limit);
    free(step);
    return result;
}

int f2c_io_emit_unformatted_item(Context *context, Unit *unit, const F2cIoItem *item, int input,
                                 const char *stream, const char *unit_number, const char *status,
                                 int depth) {
    const F2cExpr *expression;
    char *value;
    if (item == NULL)
        return 0;
    if (item->implied_do)
        return emit_implied_do(context, unit, item, input, stream, unit_number, status, depth);
    expression = item->expression;
    if (expression == NULL)
        return 0;
    if (expression->rank != 0U)
        return f2c_io_emit_unformatted_array(context, unit, item, input, stream, unit_number,
                                             status, depth);
    if (expression->type == TYPE_DERIVED && expression->derived_type != NULL) {
        return emit_derived_item(context, unit, item, expression->derived_type, input, stream,
                                 unit_number, status, depth);
    }
    value = f2c_io_emit_item_expression(unit, item);
    if (value == NULL)
        return 0;
    f2c_io_emit_unformatted_scalar(context, unit, expression, value, input, stream, status, depth);
    free(value);
    return 1;
}
