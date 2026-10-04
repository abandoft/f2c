#include "codegen/descriptor/private.h"
#include "codegen/names.h"
#include "codegen/storage/private.h"
#include "codegen/unit/private.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void f2c_unit_emit_declarations(Context *context, Unit *unit) {
    Buffer *output = &context->output;
    size_t i;
    f2c_unit_emit_equivalence_declarations(context, unit);
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        size_t dimension;
        const char *name;
        if (!symbol->argument || !f2c_symbol_uses_descriptor(symbol))
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output,
                          "if (f2c_descriptor_%s != NULL && (f2c_descriptor_%s->rank != %zuU || "
                          "f2c_descriptor_%s->element_size != sizeof(%s))) abort();\n",
                          name, name, symbol->rank, name, f2c_symbol_c_type(symbol));
        if (!symbol->optional) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "if (f2c_descriptor_%s == NULL) abort();\n", name);
        }
        if (symbol->pointer || symbol->allocatable) {
            if (symbol->type == TYPE_CHARACTER && !symbol->deferred_character &&
                symbol->character_length != NULL && strcmp(symbol->character_length, "*") == 0) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output,
                                  "const size_t f2c_len_%s = f2c_descriptor_state_character_length("
                                  "f2c_descriptor_%s, %uU);\n",
                                  name, name, f2c_symbol_storage_qualifiers(symbol));
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "(void)f2c_len_%s;\n", name);
            }
            continue;
        }
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(
            output,
            "%s%s *%s = f2c_descriptor_%s != NULL ? (%s%s *)"
            "f2c_descriptor_%s->%s : NULL;\n",
            symbol->intent == F2C_INTENT_IN && !symbol->pointer ? "const " : "",
            f2c_symbol_c_type(symbol), name, name,
            symbol->intent == F2C_INTENT_IN && !symbol->pointer ? "const " : "",
            f2c_symbol_c_type(symbol), name,
            f2c_descriptor_address_member(F2C_STORAGE_UNQUALIFIED,
                                          symbol->intent == F2C_INTENT_IN && !symbol->pointer));
        if (symbol->pointer) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "bool %s_deallocatable = f2c_descriptor_%s != NULL && "
                              "f2c_descriptor_%s->deallocatable;\n",
                              name, name, name);
        }
        if (symbol->deferred_character) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_len_%s = f2c_descriptor_%s != NULL ? "
                              "f2c_descriptor_%s->character_length : 0U;\n",
                              name, name, name);
        } else if (symbol->type == TYPE_CHARACTER && symbol->character_length != NULL &&
                   strcmp(symbol->character_length, "*") == 0) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "const size_t f2c_len_%s = f2c_descriptor_%s != NULL ? "
                              "f2c_descriptor_%s->character_length : 0U;\n",
                              name, name, name);
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            char *declared_lower = !symbol->allocatable && !symbol->pointer
                                       ? f2c_emit_typed_expression(
                                             unit, symbol->dimensions[dimension].lower_expression)
                                       : NULL;
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(
                output,
                "if (f2c_descriptor_%s != NULL && (f2c_descriptor_%s->lower[%zu] < "
                "INT32_MIN || f2c_descriptor_%s->lower[%zu] > INT32_MAX || "
                "f2c_descriptor_%s->extent[%zu] < 0 || f2c_descriptor_%s->extent[%zu] > "
                "INT32_MAX || (f2c_descriptor_%s->extent[%zu] > 1 && "
                "f2c_descriptor_%s->stride[%zu] == 0))) abort();\n",
                name, name, dimension, name, dimension, name, dimension, name, dimension, name,
                dimension, name, dimension);
            f2c_unit_indent(output, 1);
            if (symbol->allocatable || symbol->pointer)
                f2c_buffer_printf(output,
                                  "int32_t %s_lower_%zu = f2c_descriptor_%s != NULL ? "
                                  "(int32_t)f2c_descriptor_%s->lower[%zu] : 1;\n",
                                  name, dimension + 1U, name, name, dimension);
            else
                f2c_buffer_printf(output, "const int32_t %s_lower_%zu = (int32_t)(%s);\n", name,
                                  dimension + 1U, declared_lower != NULL ? declared_lower : "1");
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "int32_t %s_extent_%zu = f2c_descriptor_%s != NULL ? "
                              "(int32_t)f2c_descriptor_%s->extent[%zu] : 0;\n",
                              name, dimension + 1U, name, name, dimension);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "%sptrdiff_t %s_stride_%zu = f2c_descriptor_%s != NULL ? "
                              "f2c_descriptor_%s->stride[%zu] : 0;\n",
                              symbol->allocatable || symbol->pointer ? "" : "const ", name,
                              dimension + 1U, name, name, dimension);
            free(declared_lower);
        }
        if (symbol->contiguous) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "if (f2c_descriptor_%s != NULL && "
                              "!f2c_descriptor_is_contiguous(%zuU, (const size_t[]){",
                              name, symbol->rank);
            for (dimension = 0U; dimension < symbol->rank; ++dimension)
                f2c_buffer_printf(output, "%s(size_t)f2c_descriptor_%s->extent[%zu]",
                                  dimension == 0U ? "" : ", ", name, dimension);
            f2c_buffer_printf(output, "}, f2c_descriptor_%s->stride)) abort();\n", name);
        }
    }
    f2c_unit_emit_value_copies(output, unit, 1);
    {
        Symbol *result = f2c_unit_function_result(unit);
        if (result != NULL && f2c_unit_has_descriptor_result(unit)) {
            size_t dimension;
            const char *name = f2c_symbol_c_name(unit, result);
            if (f2c_symbol_is_automatic_array(unit, result)) {
                f2c_unit_emit_automatic_array_declaration(output, unit, result, 1);
            } else {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "%s *%s = NULL;\n", f2c_symbol_c_type(result), name);
                if (result->pointer) {
                    f2c_unit_indent(output, 1);
                    f2c_buffer_printf(output, "bool %s_deallocatable = false;\n", name);
                }
                if (result->deferred_character) {
                    f2c_unit_indent(output, 1);
                    f2c_buffer_printf(output, "size_t f2c_char_len_%s = 0U;\n", name);
                }
                for (dimension = 0U; dimension < result->rank; ++dimension) {
                    f2c_unit_indent(output, 1);
                    f2c_buffer_printf(output, "int32_t %s_lower_%zu = 1;\n", name, dimension + 1U);
                    f2c_unit_indent(output, 1);
                    f2c_buffer_printf(output, "int32_t %s_extent_%zu = 0;\n", name, dimension + 1U);
                    if (result->pointer) {
                        f2c_unit_indent(output, 1);
                        f2c_buffer_printf(output, "ptrdiff_t %s_stride_%zu = 0;\n", name,
                                          dimension + 1U);
                    }
                }
            }
            f2c_unit_indent(output, 1);
            f2c_buffer_append(output, "f2c_descriptor f2c_result_descriptor = {0};\n");
        }
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const int persistent = unit->save_all || symbol->saved || symbol->initializer != NULL;
        char *initializer = NULL;
        if (symbol->parameter && symbol->rank != 0U && !symbol->module_entity &&
            symbol->alias_to == NULL) {
            if (!f2c_unit_emit_parameter_array(context, unit, symbol))
                return;
            continue;
        }
        if (symbol->host_associated && !symbol->host_capture)
            continue;
        if (symbol->procedure_pointer && !symbol->argument && !symbol->module_entity) {
            f2c_unit_indent(output, 1);
            if (persistent)
                f2c_buffer_append(output, "static ");
            f2c_emit_procedure_pointer_type(output, symbol, f2c_symbol_c_name(unit, symbol));
            f2c_buffer_append(output, " = NULL;\n");
            continue;
        }
        if (symbol->argument || symbol->parameter || symbol->external ||
            symbol->intrinsic != NULL || symbol->module_entity || symbol->host_associated ||
            symbol->common_block != NULL || symbol->equivalence_associated ||
            symbol->alias_to != NULL || symbol->statement_function ||
            (unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
             strcmp(symbol->name, unit->result_name) == 0)) {
            continue;
        }
        if (f2c_symbol_is_automatic_array(unit, symbol)) {
            f2c_unit_emit_automatic_array_declaration(output, unit, symbol, 1);
            continue;
        }
        f2c_unit_indent(output, 1);
        if (persistent)
            f2c_buffer_append(output, "static ");
        if (symbol->parameter)
            f2c_buffer_append(output, "const ");
        if (symbol->allocatable || symbol->pointer) {
            size_t d;
            f2c_buffer_printf(output, "%s *%s = NULL;\n", f2c_symbol_c_type(symbol),
                              f2c_symbol_c_name(unit, symbol));
            if (symbol->pointer) {
                f2c_unit_indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "bool %s_deallocatable = false;\n",
                                  f2c_symbol_c_name(unit, symbol));
            }
            if (symbol->deferred_character) {
                f2c_unit_indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "size_t f2c_char_len_%s = 0U;\n",
                                  f2c_symbol_c_name(unit, symbol));
            }
            for (d = 0U; d < symbol->rank; ++d) {
                f2c_unit_indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "int32_t %s_lower_%zu = 1;\n",
                                  f2c_symbol_c_name(unit, symbol), d + 1U);
                f2c_unit_indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "int32_t %s_extent_%zu = 0;\n",
                                  f2c_symbol_c_name(unit, symbol), d + 1U);
                if (symbol->pointer) {
                    f2c_unit_indent(output, 1);
                    if (persistent)
                        f2c_buffer_append(output, "static ");
                    f2c_buffer_printf(output, "ptrdiff_t %s_stride_%zu = 0;\n",
                                      f2c_symbol_c_name(unit, symbol), d + 1U);
                }
            }
            continue;
        }
        if (symbol->automatic_character) {
            char *length = f2c_emit_typed_expression(unit, symbol->character_length_expression);
            char *count =
                symbol->rank != 0U ? f2c_symbol_element_count(unit, symbol) : f2c_strdup("1U");
            const char *name = f2c_symbol_c_name(unit, symbol);
            f2c_buffer_printf(output, "int64_t f2c_char_len_value_%s = (int64_t)(%s);\n", name,
                              length != NULL ? length : "0");
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_len_%s = f2c_character_parameter_length("
                              "f2c_char_len_value_%s);\n",
                              name, name);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "size_t f2c_char_count_%s = (size_t)(%s);\n", name,
                              count != NULL ? count : "0U");
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "if (f2c_char_len_%s != 0U && f2c_char_count_%s > "
                              "SIZE_MAX / f2c_char_len_%s) abort();\n",
                              name, name, name);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_bytes_%s = f2c_char_count_%s * "
                              "f2c_char_len_%s;\n",
                              name, name, name);
            if (symbol->rank == 0U) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "if (f2c_char_bytes_%s == SIZE_MAX) abort();\n", name);
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "++f2c_char_bytes_%s;\n", name);
            }
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "char *%s = (char *)malloc(f2c_char_bytes_%s == 0U ? 1U : "
                              "f2c_char_bytes_%s);\n",
                              name, name, name);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "if (%s == NULL) abort();\n", name);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "if (f2c_char_bytes_%s != 0U) memset(%s, 0, "
                              "f2c_char_bytes_%s);\n",
                              name, name, name);
            free(length);
            free(count);
            continue;
        }
        f2c_buffer_printf(output, "%s %s", f2c_symbol_c_type(symbol),
                          f2c_symbol_c_name(unit, symbol));
        if (symbol->type == TYPE_CHARACTER && symbol->rank == 0U &&
            symbol->character_length != NULL) {
            char *length = f2c_symbol_character_length(unit, symbol);
            f2c_buffer_printf(output, "[(%s) + 1]", length);
            free(length);
        } else if (symbol->rank != 0U) {
            size_t d;
            f2c_buffer_append(output, "[F2C_MAX(1, ");
            if (symbol->type == TYPE_CHARACTER) {
                char *length = symbol->character_length != NULL
                                   ? f2c_symbol_character_length(unit, symbol)
                                   : f2c_strdup("1U");
                f2c_buffer_printf(output, "(size_t)(%s) * ", length);
                free(length);
            }
            for (d = 0U; d < symbol->rank; ++d) {
                char *lo;
                char *hi;
                lo = f2c_emit_typed_expression(unit, symbol->dimensions[d].lower_expression);
                hi = f2c_emit_typed_expression(unit, symbol->dimensions[d].upper_expression);
                f2c_buffer_printf(output, "%s((%s) - (%s) + 1)", d == 0U ? "" : " * ", hi, lo);
                free(lo);
                free(hi);
            }
            f2c_buffer_append(output, ")]");
        }
        const size_t initializer_error_count = context->result.error_count;
        if (symbol->data_element_initializers != NULL) {
            initializer = f2c_unit_data_array_initializer(unit, symbol);
            if (initializer == NULL)
                f2c_diagnostic(context, symbol->declaration_line, 1,
                               "typed DATA initializer for array '%s' cannot be emitted",
                               symbol->name);
        } else if (symbol->initializer != NULL || (persistent && symbol->type == TYPE_DERIVED)) {
            if (symbol->type == TYPE_CHARACTER) {
                int supported = 0;
                if (symbol->rank != 0U) {
                    initializer = f2c_unit_static_storage_initializer(unit, symbol);
                    supported = initializer != NULL;
                } else
                    initializer = f2c_character_declaration_initializer(unit, symbol, &supported);
                if (!supported) {
                    f2c_diagnostic(context, symbol->declaration_line, 1,
                                   "unsupported non-constant or shape-incompatible CHARACTER "
                                   "declaration initializer for '%s'",
                                   symbol->name);
                }
            } else {
                initializer = symbol->rank != 0U || symbol->type == TYPE_DERIVED ||
                                      symbol->type == TYPE_COMPLEX ||
                                      symbol->type == TYPE_DOUBLE_COMPLEX
                                  ? f2c_unit_static_storage_initializer(unit, symbol)
                                  : f2c_emit_typed_expression(unit, symbol->initializer_expression);
            }
        }
        if (initializer != NULL)
            f2c_buffer_printf(output, " = %s", initializer);
        else if (symbol->initializer != NULL || symbol->data_element_initializers != NULL) {
            if (context->result.error_count == initializer_error_count)
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_UNSUPPORTED, &symbol->declaration_span, 1,
                    "declaration initializer for '%s' cannot be materialized as typed C17 data",
                    symbol->name);
            return;
        } else if (!symbol->parameter && symbol->rank == 0U)
            f2c_buffer_append(output, " = {0}");
        f2c_buffer_append(output, ";\n");
        if (symbol->scope_begin_line != 0U && symbol->type == TYPE_DERIVED &&
            symbol->derived_type != NULL && !persistent) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "bool f2c_scope_live_%s = false;\n",
                              f2c_symbol_c_name(unit, symbol));
        }
        if (symbol->initializer == NULL && !symbol->parameter && symbol->rank != 0U &&
            !unit->save_all && !symbol->saved) {
            const char *name = f2c_symbol_c_name(unit, symbol);
            f2c_unit_indent(output, 1);
            if (symbol->volatile_entity) {
                char *index_name = f2c_codegen_local_name(unit, "f2c_zero_index");
                if (index_name == NULL) {
                    output->failed = 1;
                    return;
                }
                /* libc byte stores cannot preserve a volatile object's access semantics. */
                f2c_buffer_printf(output,
                                  "for (size_t %s = 0U; "
                                  "%s < sizeof(%s) / sizeof(%s[0]); "
                                  "++%s) ((volatile %s *)%s)[%s] = (%s){0};\n",
                                  index_name, index_name, name, name, index_name,
                                  f2c_symbol_c_type(symbol), name, index_name,
                                  f2c_symbol_c_type(symbol));
                free(index_name);
            } else {
                f2c_buffer_printf(output, "memset(%s, 0, sizeof(%s));\n", name, name);
            }
        }
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL &&
            symbol->scope_begin_line == 0U && !persistent) {
            const char *name = f2c_symbol_c_name(unit, symbol);
            if (symbol->rank == 0U) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "f2c_initialize_%s(&%s);\n", symbol->derived_type->c_name,
                                  name);
            } else {
                char *count = f2c_symbol_element_count(unit, symbol);
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output,
                                  "for (size_t f2c_derived_index = 0U; "
                                  "f2c_derived_index < (size_t)(%s); ++f2c_derived_index) "
                                  "f2c_initialize_%s(&%s[f2c_derived_index]);\n",
                                  count != NULL ? count : "0U", symbol->derived_type->c_name, name);
                free(count);
            }
        }
        free(initializer);
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const char *name;
        char *count;
        if (!symbol->argument || symbol->intent != F2C_INTENT_OUT)
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        if (symbol->pointer || symbol->allocatable) {
            if (!f2c_storage_emit_dummy_entry(output, unit, symbol, 1))
                output->failed = 1;
            continue;
        }
        if (symbol->type != TYPE_DERIVED || symbol->derived_type == NULL)
            continue;
        count = symbol->rank == 0U ? f2c_strdup("1U") : f2c_symbol_element_count(unit, symbol);
        f2c_unit_indent(output, 1);
        if (symbol->allocatable) {
            f2c_buffer_printf(output, "if (%s != NULL) {\n", name);
            f2c_unit_indent(output, 2);
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            f2c_unit_indent(output, 2);
            f2c_buffer_printf(output, "free(%s); %s = NULL;\n", name, name);
            f2c_unit_indent(output, 1);
            f2c_buffer_append(output, "}\n");
        } else {
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s));\n",
                              symbol->polymorphic ? "f2c_initialize_dynamic"
                                                  : "f2c_initialize_dynamic",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U");
        }
        free(count);
    }
    if (unit->kind == UNIT_FUNCTION && unit->return_type != TYPE_CHARACTER &&
        !f2c_unit_has_descriptor_result(unit)) {
        f2c_unit_indent(output, 1);
        Symbol *result = f2c_unit_function_result(unit);
        f2c_buffer_printf(output, "%s f2c_result = {0};\n", f2c_unit_function_return_type(unit));
        if (result != NULL && result->type == TYPE_DERIVED && result->derived_type != NULL) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "f2c_initialize_%s(&f2c_result);\n",
                              result->derived_type->c_name);
        }
    }
}
