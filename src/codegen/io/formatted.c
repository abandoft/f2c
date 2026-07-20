#include "codegen/io/private.h"

#include <stdlib.h>
#include <string.h>

static void emit_formatted_scalar(Context *context, Unit *unit, const F2cExpr *expression,
                                  const Symbol *symbol, const char *value, int input, int depth) {
    Type type =
        expression != NULL ? expression->type : (symbol != NULL ? symbol->type : TYPE_UNKNOWN);
    f2c_io_indent(&context->output, depth);
    if (type == TYPE_CHARACTER) {
        char *length = expression != NULL ? f2c_character_length_expression(unit, expression)
                                          : f2c_symbol_character_length(unit, symbol);
        char *pointer = expression != NULL ? f2c_character_source_pointer(unit, expression, value)
                                           : f2c_strdup(value);
        if (input)
            f2c_buffer_printf(&context->output,
                              "(void)f2c_format_read_character(&f2c_io_format, %s, "
                              "(size_t)(%s));\n",
                              pointer != NULL ? pointer : value, length != NULL ? length : "1U");
        else
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_character(&f2c_io_format, %s, "
                              "(size_t)(%s));\n",
                              pointer != NULL ? pointer : value, length != NULL ? length : "1U");
        free(length);
        free(pointer);
    } else if (type == TYPE_LOGICAL) {
        if (input) {
            f2c_buffer_append(&context->output, "{ bool f2c_formatted_value; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_format_read_logical(&f2c_io_format, "
                              "&f2c_formatted_value) > 0) %s = f2c_formatted_value ? 1 : 0; }\n",
                              value);
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_logical(&f2c_io_format, (%s) != 0);\n", value);
        }
    } else if (type == TYPE_INTEGER) {
        if (input) {
            f2c_buffer_append(&context->output, "{ int64_t f2c_formatted_value; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_format_read_integer(&f2c_io_format, "
                              "&f2c_formatted_value) > 0) %s = (%s)f2c_formatted_value; }\n",
                              value,
                              symbol != NULL ? f2c_symbol_c_type(symbol)
                                             : f2c_expression_c_type(expression));
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_integer(&f2c_io_format, (int64_t)(%s));\n", value);
        }
    } else if (type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX) {
        if (input) {
            f2c_buffer_append(&context->output,
                              "{ double f2c_formatted_real, f2c_formatted_imaginary; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_format_read_real(&f2c_io_format, &f2c_formatted_real) > "
                              "0 && f2c_format_read_real(&f2c_io_format, "
                              "&f2c_formatted_imaginary) > 0) %s = %s(f2c_formatted_real, "
                              "f2c_formatted_imaginary); }\n",
                              value, type == TYPE_COMPLEX ? "f2c_make_c" : "f2c_make_z");
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_real(&f2c_io_format, (double)%s(%s)); "
                              "f2c_format_write_real(&f2c_io_format, (double)%s(%s));\n",
                              type == TYPE_COMPLEX ? "crealf" : "creal", value,
                              type == TYPE_COMPLEX ? "cimagf" : "cimag", value);
        }
    } else {
        if (input) {
            f2c_buffer_append(&context->output, "{ double f2c_formatted_value; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_format_read_real(&f2c_io_format, "
                              "&f2c_formatted_value) > 0) %s = (%s)f2c_formatted_value; }\n",
                              value,
                              symbol != NULL ? f2c_symbol_c_type(symbol)
                                             : f2c_expression_c_type(expression));
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_real(&f2c_io_format, (double)(%s));\n", value);
        }
    }
}

static int default_formatted_derived_supported(const F2cDerivedType *derived) {
    size_t index;
    if (derived == NULL)
        return 0;
    if (derived->parent != NULL && !default_formatted_derived_supported(derived->parent))
        return 0;
    for (index = 0U; index < derived->component_count; ++index) {
        const Symbol *component = &derived->components[index];
        if (component->allocatable || component->pointer || component->procedure_pointer)
            return 0;
        if (component->type == TYPE_DERIVED && component->derived_type != NULL &&
            !default_formatted_derived_supported(component->derived_type))
            return 0;
    }
    return 1;
}

static void emit_formatted_derived(Context *context, Unit *unit, const char *value,
                                   F2cDerivedType *derived, int input, const char *unit_number,
                                   int depth);

static void emit_formatted_component(Context *context, Unit *unit, const Symbol *component,
                                     const char *owner, int input, const char *unit_number,
                                     int depth) {
    Buffer value = {0};
    Buffer index_name = {0};
    char *count =
        component->rank != 0U ? f2c_symbol_element_count(unit, (Symbol *)component) : NULL;
    char *character_length =
        component->type == TYPE_CHARACTER ? f2c_symbol_character_length(unit, component) : NULL;
    f2c_buffer_printf(&value, "(%s).%s", owner, f2c_symbol_c_name(unit, component));
    if (component->rank == 0U) {
        if (component->type == TYPE_DERIVED && component->derived_type != NULL)
            emit_formatted_derived(context, unit, value.data, component->derived_type, input,
                                   unit_number, depth);
        else
            emit_formatted_scalar(context, unit, NULL, component, value.data, input, depth);
    } else {
        f2c_buffer_printf(&index_name, "f2c_format_component_%d", depth);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "for (size_t %s = 0U; %s < %s; ++%s) {\n",
                          index_name.data, index_name.data, count != NULL ? count : "0U",
                          index_name.data);
        {
            Buffer element = {0};
            if (component->type == TYPE_CHARACTER)
                f2c_buffer_printf(&element, "%s + %s * (size_t)(%s)", value.data, index_name.data,
                                  character_length != NULL ? character_length : "1U");
            else
                f2c_buffer_printf(&element, "%s[%s]", value.data, index_name.data);
            if (component->type == TYPE_DERIVED && component->derived_type != NULL)
                emit_formatted_derived(context, unit, element.data, component->derived_type, input,
                                       unit_number, depth + 1);
            else
                emit_formatted_scalar(context, unit, NULL, component, element.data, input,
                                      depth + 1);
            free(element.data);
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    free(value.data);
    free(index_name.data);
    free(count);
    free(character_length);
}

static void emit_default_formatted_derived(Context *context, Unit *unit, const char *value,
                                           F2cDerivedType *derived, int input,
                                           const char *unit_number, int depth) {
    size_t index;
    if (derived->parent != NULL) {
        Buffer parent = {0};
        f2c_buffer_printf(&parent, "(%s).parent", value);
        emit_default_formatted_derived(context, unit, parent.data, derived->parent, input,
                                       unit_number, depth);
        free(parent.data);
    }
    for (index = 0U; index < derived->component_count; ++index)
        emit_formatted_component(context, unit, &derived->components[index], value, input,
                                 unit_number, depth);
}

static void emit_formatted_derived(Context *context, Unit *unit, const char *value,
                                   F2cDerivedType *derived, int input, const char *unit_number,
                                   int depth) {
    const F2cDefinedIoKind kind =
        input ? F2C_DEFINED_IO_READ_FORMATTED : F2C_DEFINED_IO_WRITE_FORMATTED;
    if (f2c_io_defined_binding(derived, kind) == NULL) {
        if (default_formatted_derived_supported(derived))
            emit_default_formatted_derived(context, unit, value, derived, input, unit_number,
                                           depth);
        else {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "f2c_io_format.status = 0;\n");
        }
        return;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{ f2c_format_descriptor f2c_dtio_descriptor;\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_format_take_dt(&f2c_io_format, &f2c_dtio_descriptor)) {\n");
    if (!f2c_io_emit_defined_io_call(context, value, derived, kind, unit_number,
                                     "f2c_dtio_descriptor.iotype", "f2c_dtio_descriptor.v_list",
                                     "f2c_dtio_descriptor.v_list_count", "f2c_io_format.status",
                                     depth + 2)) {
        f2c_io_indent(&context->output, depth + 2);
        f2c_buffer_append(&context->output, "f2c_io_format.status = 0;\n");
    }
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "} else {\n");
    if (default_formatted_derived_supported(derived))
        emit_default_formatted_derived(context, unit, value, derived, input, unit_number,
                                       depth + 2);
    else {
        f2c_io_indent(&context->output, depth + 2);
        f2c_buffer_append(&context->output, "f2c_io_format.status = 0;\n");
    }
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

void f2c_io_emit_formatted_item(Context *context, Unit *unit, const F2cIoItem *item, int input,
                                const char *unit_number, int depth) {
    const F2cExpr *expression;
    Symbol *symbol;
    if (item == NULL)
        return;
    if (item->implied_do) {
        size_t i;
        char *variable = f2c_io_emit_required_expression(unit, item->iterator);
        char *start = f2c_io_emit_required_expression(unit, item->initial);
        char *finish = f2c_io_emit_required_expression(unit, item->limit);
        char *step = f2c_io_emit_required_expression(unit, item->step);
        if (variable != NULL && start != NULL && finish != NULL && step != NULL) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (%s = %s; ((%s) >= 0 ? %s <= %s : %s >= %s); %s += %s) "
                              "{\n",
                              variable, start, step, variable, finish, variable, finish, variable,
                              step);
            for (i = 0U; i < item->child_count; ++i)
                f2c_io_emit_formatted_item(context, unit, &item->children[i], input, unit_number,
                                           depth + 1);
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        free(variable);
        free(start);
        free(finish);
        free(step);
        return;
    }
    if (input) {
        F2cIoItem lowered_item;
        F2cExpr lowered_expression;
        if (f2c_io_begin_unaligned_input(context, unit, item, depth, &lowered_item,
                                         &lowered_expression)) {
            f2c_io_emit_formatted_item(context, unit, &lowered_item, input, unit_number, depth + 1);
            f2c_io_end_unaligned_input(context, item->expression->symbol, depth);
            return;
        }
    }
    expression = item->expression;
    symbol = expression != NULL ? expression->symbol : NULL;
    if (expression == NULL)
        return;
    if (expression->kind == F2C_EXPR_NAME && symbol != NULL && symbol->rank != 0U) {
        char *count = f2c_symbol_element_count(unit, symbol);
        char *character_length =
            symbol->type == TYPE_CHARACTER ? f2c_symbol_character_length(unit, symbol) : NULL;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_format_index = 0U; f2c_format_index < %s; "
                          "++f2c_format_index) {\n",
                          count != NULL ? count : "0U");
        {
            Buffer value = {0};
            char *unaligned_address = NULL;
            if (symbol->type == TYPE_CHARACTER)
                f2c_buffer_printf(&value, "%s + f2c_format_index * (size_t)(%s)",
                                  f2c_symbol_c_name(unit, symbol),
                                  character_length != NULL ? character_length : "1U");
            else if (symbol->equivalence_unaligned)
                unaligned_address =
                    f2c_emit_unaligned_linear_address(unit, symbol, "f2c_format_index");
            else
                f2c_buffer_printf(&value, "%s[f2c_format_index]", f2c_symbol_c_name(unit, symbol));
            if (unaligned_address != NULL) {
                const char *suffix = f2c_unaligned_access_suffix(symbol);
                f2c_io_indent(&context->output, depth + 1);
                f2c_buffer_append(&context->output, "{\n");
                f2c_io_indent(&context->output, depth + 2);
                f2c_buffer_printf(&context->output,
                                  "unsigned char *f2c_unaligned_io_address = %s;\n",
                                  unaligned_address);
                f2c_io_indent(&context->output, depth + 2);
                f2c_buffer_printf(&context->output,
                                  "%s f2c_unaligned_io_value = f2c_unaligned_load_%s("
                                  "f2c_unaligned_io_address);\n",
                                  f2c_symbol_c_type(symbol), suffix);
                emit_formatted_scalar(context, unit, NULL, symbol, "f2c_unaligned_io_value", input,
                                      depth + 2);
                if (input) {
                    f2c_io_indent(&context->output, depth + 2);
                    f2c_buffer_printf(&context->output,
                                      "f2c_unaligned_store_%s(f2c_unaligned_io_address, "
                                      "f2c_unaligned_io_value);\n",
                                      suffix);
                }
                f2c_io_indent(&context->output, depth + 1);
                f2c_buffer_append(&context->output, "}\n");
            } else if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL)
                emit_formatted_derived(context, unit,
                                       value.data != NULL ? value.data
                                                          : f2c_symbol_c_name(unit, symbol),
                                       symbol->derived_type, input, unit_number, depth + 1);
            else
                emit_formatted_scalar(context, unit, NULL, symbol,
                                      value.data != NULL ? value.data
                                                         : f2c_symbol_c_name(unit, symbol),
                                      input, depth + 1);
            free(unaligned_address);
            free(value.data);
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(count);
        free(character_length);
    } else {
        char *value = f2c_io_emit_item_expression(unit, item);
        if (value != NULL) {
            if (expression->type == TYPE_DERIVED && expression->derived_type != NULL)
                emit_formatted_derived(context, unit, value, expression->derived_type, input,
                                       unit_number, depth);
            else
                emit_formatted_scalar(context, unit, expression, symbol, value, input, depth);
        }
        free(value);
    }
}
