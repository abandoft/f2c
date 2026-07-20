#include "codegen/io/private.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void f2c_io_indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

char *f2c_io_emit_item_expression(Unit *unit, const F2cIoItem *item) {
    int supported = 0;
    char *result = item->expression != NULL
                       ? f2c_emit_expression_ast(unit, item->expression, &supported)
                       : NULL;
    if (!supported || result == NULL) {
        free(result);
        return NULL;
    }
    return result;
}

char *f2c_io_emit_required_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

int f2c_io_begin_unaligned_input(Context *context, Unit *unit, const F2cIoItem *item, int depth,
                                 F2cIoItem *lowered_item, F2cExpr *lowered_expression) {
    const F2cExpr *expression = item != NULL ? item->expression : NULL;
    const Symbol *symbol = expression != NULL ? expression->symbol : NULL;
    const char *suffix;
    char *address;
    int supported = 1;
    if (context == NULL || unit == NULL || expression == NULL || symbol == NULL ||
        !symbol->equivalence_unaligned || expression->rank != 0U || expression->lowered_c != NULL ||
        (expression->kind != F2C_EXPR_NAME && expression->kind != F2C_EXPR_ARRAY_REFERENCE))
        return 0;
    suffix = f2c_unaligned_access_suffix(symbol);
    address =
        suffix != NULL ? f2c_emit_unaligned_designator_address(unit, expression, &supported) : NULL;
    if (!supported || address == NULL) {
        free(address);
        return 0;
    }
    *lowered_expression = *expression;
    lowered_expression->lowered_c = "f2c_unaligned_io_value";
    *lowered_item = *item;
    lowered_item->expression = lowered_expression;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "unsigned char *f2c_unaligned_io_address = %s;\n", address);
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "%s f2c_unaligned_io_value = f2c_unaligned_load_%s("
                      "f2c_unaligned_io_address);\n",
                      f2c_symbol_c_type(symbol), suffix);
    free(address);
    return 1;
}

void f2c_io_end_unaligned_input(Context *context, const Symbol *symbol, int depth) {
    const char *suffix = f2c_unaligned_access_suffix(symbol);
    if (context == NULL || suffix == NULL)
        return;
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "f2c_unaligned_store_%s(f2c_unaligned_io_address, "
                      "f2c_unaligned_io_value);\n",
                      suffix);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

char *f2c_io_c_string_literal(const char *text, size_t length) {
    Buffer result = {0};
    size_t i;
    f2c_buffer_append(&result, "\"");
    for (i = 0U; i < length; ++i) {
        const unsigned char value = (unsigned char)text[i];
        if (value == '\\' || value == '"') {
            const char escape[2] = {'\\', (char)value};
            f2c_buffer_append_n(&result, escape, sizeof(escape));
        } else if (value == '\n') {
            f2c_buffer_append(&result, "\\n");
        } else if (value == '\r') {
            f2c_buffer_append(&result, "\\r");
        } else if (value == '\t') {
            f2c_buffer_append(&result, "\\t");
        } else if (isprint(value)) {
            const char character = (char)value;
            f2c_buffer_append_n(&result, &character, 1U);
        } else {
            f2c_buffer_printf(&result, "\\%03o", (unsigned)value);
        }
    }
    f2c_buffer_append(&result, "\"");
    return f2c_buffer_take(&result);
}

static F2cTypeBinding *find_type_binding(F2cDerivedType *derived, const char *name) {
    size_t index;
    if (derived == NULL || name == NULL)
        return NULL;
    for (index = 0U; index < derived->binding_count; ++index)
        if (strcmp(derived->bindings[index].name, name) == 0)
            return &derived->bindings[index];
    return find_type_binding(derived->parent, name);
}

F2cTypeBinding *f2c_io_defined_binding(F2cDerivedType *derived, F2cDefinedIoKind kind) {
    F2cDerivedType *owner;
    for (owner = derived; owner != NULL; owner = owner->parent) {
        const char *name = owner->defined_io_bindings[kind];
        if (name != NULL)
            return find_type_binding(derived, name);
    }
    return NULL;
}

static char *defined_io_designator(const char *value, F2cDerivedType *derived,
                                   const F2cTypeBinding *binding) {
    Buffer result = {0};
    F2cDerivedType *owner = derived;
    if (value == NULL || derived == NULL || binding == NULL || binding->storage_owner == NULL)
        return NULL;
    f2c_buffer_printf(&result, "(%s)", value);
    while (owner != NULL && owner != binding->storage_owner) {
        f2c_buffer_append(&result, ".parent");
        owner = owner->parent;
    }
    if (owner == NULL) {
        free(result.data);
        return NULL;
    }
    f2c_buffer_printf(&result, ".%s", binding->name);
    return f2c_buffer_take(&result);
}

int f2c_io_emit_defined_io_call(Context *context, const char *value, F2cDerivedType *derived,
                                F2cDefinedIoKind kind, const char *unit_number, const char *iotype,
                                const char *v_list, const char *v_list_count, const char *status,
                                int depth) {
    F2cTypeBinding *binding = f2c_io_defined_binding(derived, kind);
    char *callee;
    const int formatted =
        kind == F2C_DEFINED_IO_READ_FORMATTED || kind == F2C_DEFINED_IO_WRITE_FORMATTED;
    if (binding == NULL)
        return 0;
    callee = defined_io_designator(value, derived, binding);
    if (callee == NULL)
        return 0;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const int32_t f2c_dtio_unit = (int32_t)(%s); "
                      "int32_t f2c_dtio_iostat = 0; char f2c_dtio_iomsg[256]; "
                      "memset(f2c_dtio_iomsg, ' ', sizeof(f2c_dtio_iomsg));\n",
                      unit_number != NULL ? unit_number : "0");
    if (formatted) {
        f2c_io_indent(&context->output, depth + 1);
        if (v_list == NULL)
            f2c_buffer_append(&context->output, "const int32_t f2c_dtio_empty_v_list[1] = {0}; ");
        f2c_buffer_printf(&context->output,
                          "const int32_t *f2c_dtio_v_list = %s; "
                          "const size_t f2c_dtio_v_list_count = (size_t)(%s);\n",
                          v_list != NULL ? v_list : "f2c_dtio_empty_v_list",
                          v_list_count != NULL ? v_list_count : "0U");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "f2c_descriptor f2c_dtio_v_list_descriptor = {"
                          ".data = f2c_implicit_mutable_actual(f2c_dtio_v_list), "
                          ".element_size = sizeof(*f2c_dtio_v_list), .rank = 1U};\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "f2c_dtio_v_list_descriptor.lower[0] = 1; "
                                            "f2c_dtio_v_list_descriptor.extent[0] = "
                                            "(int64_t)f2c_dtio_v_list_count; "
                                            "f2c_dtio_v_list_descriptor.stride[0] = 1;\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "++f2c_child_io_depth;\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "%s((void *)&(%s), &f2c_dtio_unit, %s, "
                          "&f2c_dtio_v_list_descriptor, "
                          "&f2c_dtio_iostat, f2c_dtio_iomsg, strlen(%s), "
                          "sizeof(f2c_dtio_iomsg));\n",
                          callee, value, iotype != NULL ? iotype : "\"LISTDIRECTED\"",
                          iotype != NULL ? iotype : "\"LISTDIRECTED\"");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "--f2c_child_io_depth;\n");
    } else {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "++f2c_child_io_depth;\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "%s((void *)&(%s), &f2c_dtio_unit, &f2c_dtio_iostat, "
                          "f2c_dtio_iomsg, sizeof(f2c_dtio_iomsg));\n",
                          callee, value);
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "--f2c_child_io_depth;\n");
    }
    if (status != NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (f2c_dtio_iostat != 0) %s = 0;\n", status);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(callee);
    return 1;
}

static void emit_list_derived(Context *context, Unit *unit, const char *file, const char *value,
                              F2cDerivedType *derived, int input, const char *status,
                              F2cDefinedIoKind defined_kind, const char *unit_number,
                              const char *iotype, int depth);

static void emit_list_component(Context *context, Unit *unit, const char *file, Symbol *component,
                                const char *owner, int input, const char *status,
                                F2cDefinedIoKind defined_kind, const char *unit_number,
                                const char *iotype, int depth) {
    Buffer base = {0};
    Buffer index_name = {0};
    char *count = component->rank != 0U ? f2c_symbol_element_count(unit, component) : NULL;
    char *character_length =
        component->type == TYPE_CHARACTER ? f2c_symbol_character_length(unit, component) : NULL;
    F2cExpr expression = {0};
    F2cIoItem item = {0};
    f2c_buffer_printf(&base, "(%s).%s", owner, f2c_symbol_c_name(unit, component));
    expression.kind = F2C_EXPR_COMPONENT;
    expression.type = component->type;
    expression.type_kind = component->kind;
    expression.symbol = component;
    expression.derived_type = component->derived_type;
    expression.value_category = F2C_VALUE_VARIABLE;
    expression.definable = 1;
    expression.shape.kind = F2C_SHAPE_SCALAR;
    expression.lowered_character_length_c = character_length;
    item.expression = &expression;
    if (component->rank == 0U) {
        expression.lowered_c = base.data;
        if (component->type == TYPE_DERIVED && component->derived_type != NULL)
            emit_list_derived(context, unit, file, base.data, component->derived_type, input,
                              status, defined_kind, unit_number, iotype, depth);
        else
            f2c_io_emit_item(context, unit, file, &item, input, status, 0, defined_kind,
                             unit_number, iotype, depth);
    } else {
        f2c_buffer_printf(&index_name, "f2c_list_component_%d", depth);
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
            expression.lowered_c = element.data;
            if (component->type == TYPE_DERIVED && component->derived_type != NULL)
                emit_list_derived(context, unit, file, element.data, component->derived_type, input,
                                  status, defined_kind, unit_number, iotype, depth + 1);
            else
                f2c_io_emit_item(context, unit, file, &item, input, status, 0, defined_kind,
                                 unit_number, iotype, depth + 1);
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

static void emit_default_list_derived(Context *context, Unit *unit, const char *file,
                                      const char *value, F2cDerivedType *derived, int input,
                                      const char *status, F2cDefinedIoKind defined_kind,
                                      const char *unit_number, const char *iotype, int depth) {
    size_t index;
    if (derived->parent != NULL) {
        Buffer parent = {0};
        f2c_buffer_printf(&parent, "(%s).parent", value);
        emit_default_list_derived(context, unit, file, parent.data, derived->parent, input, status,
                                  defined_kind, unit_number, iotype, depth);
        free(parent.data);
    }
    for (index = 0U; index < derived->component_count; ++index)
        emit_list_component(context, unit, file, &derived->components[index], value, input, status,
                            defined_kind, unit_number, iotype, depth);
}

static void emit_list_derived(Context *context, Unit *unit, const char *file, const char *value,
                              F2cDerivedType *derived, int input, const char *status,
                              F2cDefinedIoKind defined_kind, const char *unit_number,
                              const char *iotype, int depth) {
    if (f2c_io_defined_binding(derived, defined_kind) == NULL) {
        emit_default_list_derived(context, unit, file, value, derived, input, status, defined_kind,
                                  unit_number, iotype, depth);
        return;
    }
    if (!input && iotype != NULL && strcmp(iotype, "\"LISTDIRECTED\"") == 0) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "(void)f2c_stream_putc(' ', %s);\n", file);
    }
    if (!f2c_io_emit_defined_io_call(context, value, derived, defined_kind, unit_number, iotype,
                                     NULL, "0U", status, depth)) {
        f2c_io_indent(&context->output, depth);
        if (status != NULL)
            f2c_buffer_printf(&context->output, "%s = F2C_IO_STATUS_RECORD;\n", status);
        else
            f2c_buffer_append(&context->output, "f2c_io_abort_unhandled("
                                                "F2C_IO_STATUS_RECORD, \"defined I/O\");\n");
    }
}

void f2c_io_emit_item(Context *context, Unit *unit, const char *file, const F2cIoItem *item,
                      int input, const char *status, int record_input,
                      F2cDefinedIoKind defined_kind, const char *unit_number, const char *iotype,
                      int depth) {
    const F2cExpr *expression;
    Symbol *symbol;
    int simple_name;
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
                f2c_io_emit_item(context, unit, file, &item->children[i], input, status, 0,
                                 defined_kind, unit_number, iotype, depth + 1);
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
            f2c_io_emit_item(context, unit, file, &lowered_item, input, status, record_input,
                             defined_kind, unit_number, iotype, depth + 1);
            f2c_io_end_unaligned_input(context, item->expression->symbol, depth);
            return;
        }
    }
    expression = item->expression;
    symbol = expression != NULL ? expression->symbol : NULL;
    simple_name = expression != NULL && expression->kind == F2C_EXPR_NAME;
    if (expression != NULL && expression->type == TYPE_DERIVED &&
        expression->derived_type != NULL) {
        if (simple_name && symbol != NULL && symbol->rank != 0U) {
            char *count = f2c_symbol_element_count(unit, symbol);
            const char *name = f2c_symbol_c_name(unit, symbol);
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_dtio_index = 0U; f2c_dtio_index < %s; "
                              "++f2c_dtio_index) {\n",
                              count != NULL ? count : "0U");
            {
                Buffer value = {0};
                f2c_buffer_printf(&value, "%s[f2c_dtio_index]", name);
                emit_list_derived(context, unit, file, value.data, symbol->derived_type, input,
                                  status, defined_kind, unit_number, iotype, depth + 1);
                free(value.data);
            }
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(count);
        } else {
            char *value = f2c_io_emit_item_expression(unit, item);
            if (value != NULL)
                emit_list_derived(context, unit, file, value, expression->derived_type, input,
                                  status, defined_kind, unit_number, iotype, depth);
            free(value);
        }
        return;
    }
    if (input) {
        if (simple_name && symbol != NULL && symbol->rank != 0U) {
            char *element_count = f2c_symbol_element_count(unit, symbol);
            char *character_length =
                symbol->type == TYPE_CHARACTER ? f2c_symbol_character_length(unit, symbol) : NULL;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_io_index = 0U; f2c_io_index < %s; "
                              "++f2c_io_index) {\n",
                              element_count != NULL ? element_count : "0U");
            {
                Buffer value = {0};
                char *unaligned_address = NULL;
                if (symbol->type == TYPE_CHARACTER)
                    f2c_buffer_printf(&value, "%s + f2c_io_index * (size_t)(%s)",
                                      f2c_symbol_c_name(unit, symbol),
                                      character_length != NULL ? character_length : "1U");
                else if (symbol->equivalence_unaligned)
                    unaligned_address =
                        f2c_emit_unaligned_linear_address(unit, symbol, "f2c_io_index");
                else
                    f2c_buffer_printf(&value, "%s[f2c_io_index]", f2c_symbol_c_name(unit, symbol));
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
                    f2c_io_emit_namelist_value(context, unit, file, symbol,
                                               "f2c_unaligned_io_value", NULL, 1, depth + 2);
                    f2c_io_indent(&context->output, depth + 2);
                    f2c_buffer_printf(&context->output,
                                      "f2c_unaligned_store_%s(f2c_unaligned_io_address, "
                                      "f2c_unaligned_io_value);\n",
                                      suffix);
                    f2c_io_indent(&context->output, depth + 1);
                    f2c_buffer_append(&context->output, "}\n");
                } else {
                    f2c_io_emit_namelist_value(context, unit, file, symbol,
                                               value.data != NULL ? value.data
                                                                  : f2c_symbol_c_name(unit, symbol),
                                               NULL, 1, depth + 1);
                }
                free(unaligned_address);
                free(value.data);
            }
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(element_count);
            free(character_length);
        } else if (expression != NULL && expression->type == TYPE_LOGICAL) {
            char *value = f2c_io_emit_item_expression(unit, item);
            if (value == NULL)
                return;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{ bool f2c_io_logical = false; ");
            if (status != NULL)
                f2c_buffer_printf(&context->output,
                                  "%s = f2c_read_bool(%s, &f2c_io_logical); if (%s > 0) %s = "
                                  "f2c_io_logical ? 1 : 0; }\n",
                                  status, file, status, value);
            else
                f2c_buffer_printf(&context->output,
                                  "if (f2c_read_bool(%s, &f2c_io_logical) > 0) %s = "
                                  "f2c_io_logical ? 1 : 0; }\n",
                                  file, value);
            free(value);
        } else if (expression != NULL && expression->type == TYPE_CHARACTER &&
                   expression->rank == 0U) {
            char *value = f2c_io_emit_item_expression(unit, item);
            char *length = f2c_character_length_expression(unit, expression);
            char *pointer =
                value != NULL ? f2c_character_source_pointer(unit, expression, value) : NULL;
            if (value != NULL && length != NULL && pointer != NULL) {
                f2c_io_indent(&context->output, depth);
                if (record_input && simple_name && symbol != NULL && !symbol->argument) {
                    if (status != NULL)
                        f2c_buffer_printf(&context->output,
                                          "%s = f2c_read_record(%s, %s, (size_t)(%s));\n", status,
                                          file, pointer, length);
                    else
                        f2c_buffer_printf(&context->output,
                                          "(void)f2c_read_record(%s, %s, (size_t)(%s));\n", file,
                                          pointer, length);
                } else if (status != NULL) {
                    f2c_buffer_printf(&context->output,
                                      "%s = f2c_read_character(%s, %s, (size_t)(%s));\n", status,
                                      file, pointer, length);
                } else {
                    f2c_buffer_printf(&context->output,
                                      "(void)f2c_read_character(%s, %s, (size_t)(%s));\n", file,
                                      pointer, length);
                }
            }
            free(value);
            free(length);
            free(pointer);
        } else {
            char *value = f2c_io_emit_item_expression(unit, item);
            if (value == NULL)
                return;
            f2c_io_indent(&context->output, depth);
            if (status != NULL)
                f2c_buffer_printf(&context->output, "%s = F2C_READ(%s, &%s);\n", status, file,
                                  value);
            else
                f2c_buffer_printf(&context->output, "(void)F2C_READ(%s, &%s);\n", file, value);
            free(value);
        }
    } else {
        if (expression != NULL && expression->type == TYPE_LOGICAL && expression->rank == 0U) {
            char *value = f2c_io_emit_item_expression(unit, item);
            if (value != NULL) {
                f2c_io_indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "f2c_write_bool(%s, (%s) != 0);\n", file,
                                  value);
            }
            free(value);
        } else if (expression != NULL && expression->type == TYPE_CHARACTER &&
                   expression->rank == 0U) {
            char *value = f2c_io_emit_item_expression(unit, item);
            char *length = f2c_character_length_expression(unit, expression);
            char *pointer =
                value != NULL ? f2c_character_source_pointer(unit, expression, value) : NULL;
            if (value != NULL && length != NULL && pointer != NULL) {
                f2c_io_indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "f2c_write_character(%s, %s, (size_t)(%s));\n",
                                  file, pointer, length);
            }
            free(value);
            free(length);
            free(pointer);
        } else if (simple_name && symbol != NULL && symbol->rank != 0U) {
            char *element_count = f2c_symbol_element_count(unit, symbol);
            char *unaligned_value =
                symbol->equivalence_unaligned
                    ? f2c_emit_unaligned_linear_load(unit, symbol, "f2c_io_index")
                    : NULL;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{\n");
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "size_t f2c_io_index; for (f2c_io_index = 0; f2c_io_index < %s; "
                              "++f2c_io_index) {\n",
                              element_count);
            f2c_io_indent(&context->output, depth + 2);
            if (symbol->type == TYPE_LOGICAL) {
                if (unaligned_value != NULL)
                    f2c_buffer_printf(&context->output, "f2c_write_bool(%s, (%s) != 0);\n", file,
                                      unaligned_value);
                else
                    f2c_buffer_printf(&context->output,
                                      "f2c_write_bool(%s, %s[f2c_io_index] != 0);\n", file,
                                      f2c_symbol_c_name(unit, symbol));
            } else if (unaligned_value != NULL) {
                f2c_buffer_printf(&context->output, "F2C_WRITE(%s, (%s));\n", file,
                                  unaligned_value);
            } else {
                f2c_buffer_printf(&context->output, "F2C_WRITE(%s, (%s[f2c_io_index]));\n", file,
                                  f2c_symbol_c_name(unit, symbol));
            }
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "}\n");
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(element_count);
            free(unaligned_value);
        } else {
            char *value;
            if (simple_name && symbol != NULL && symbol->type == TYPE_CHARACTER &&
                (symbol->argument || symbol->character_length != NULL || symbol->rank != 0U))
                value = f2c_strdup(f2c_symbol_c_name(unit, symbol));
            else
                value = f2c_io_emit_item_expression(unit, item);
            if (value == NULL)
                return;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "F2C_WRITE(%s, (%s));\n", file, value);
            free(value);
        }
    }
}

const F2cIoControl *f2c_io_control(const F2cStatement *statement, F2cIoControlKind kind,
                                   size_t positional) {
    size_t i;
    for (i = 0U; i < statement->control_count; ++i) {
        const F2cIoControl *control = &statement->io_controls[i];
        if (control->kind == kind)
            return control;
        if (control->kind == F2C_IO_CONTROL_POSITIONAL && i == positional)
            return control;
    }
    return NULL;
}
