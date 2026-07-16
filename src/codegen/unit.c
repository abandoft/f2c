#include "internal/f2c.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static Symbol *function_result_symbol(Unit *unit) {
    return unit != NULL && unit->kind == UNIT_FUNCTION && unit->result_name != NULL
               ? f2c_find_symbol(unit, unit->result_name)
               : NULL;
}

static const char *function_return_c_type(Unit *unit) {
    Symbol *result = function_result_symbol(unit);
    return result != NULL && result->type == TYPE_DERIVED
               ? f2c_symbol_c_type(result)
               : f2c_c_type_kind(unit->return_type, unit->return_kind);
}

int f2c_unit_has_allocatable_result(Unit *unit) {
    Symbol *result = function_result_symbol(unit);
    return result != NULL && result->allocatable;
}

void f2c_emit_procedure_pointer_type(Buffer *output, const Symbol *procedure, const char *name) {
    size_t parameter;
    const int allocatable_result =
        !procedure->external_subroutine && procedure->external_result_allocatable;
    const int character_result =
        !allocatable_result && !procedure->external_subroutine && procedure->type == TYPE_CHARACTER;
    f2c_buffer_printf(output, "%s (*",
                      allocatable_result ? "f2c_descriptor"
                      : procedure->external_subroutine || character_result
                          ? "void"
                          : f2c_symbol_c_type(procedure));
    if (name != NULL)
        f2c_buffer_append(output, name);
    f2c_buffer_append(output, ")(");
    if (character_result)
        f2c_buffer_append(output, "char *, size_t");
    if (procedure->external_parameter_count == 0U && !character_result)
        f2c_buffer_append(output, "void");
    for (parameter = 0U; parameter < procedure->external_parameter_count; ++parameter) {
        Symbol *nested = procedure->external_parameter_procedures[parameter];
        if (parameter != 0U || character_result)
            f2c_buffer_append(output, ", ");
        if (nested != NULL)
            f2c_emit_procedure_pointer_type(output, nested, NULL);
        else if (procedure->external_parameter_allocatable[parameter] ||
                 procedure->external_parameter_pointer[parameter])
            f2c_buffer_append(output, "f2c_descriptor *");
        else if (procedure->type_bound && parameter == procedure->type_bound_pass_index &&
                 !procedure->type_bound_nopass)
            f2c_buffer_printf(output, "%svoid *",
                              procedure->external_parameter_const[parameter] ? "const " : "");
        else
            f2c_buffer_printf(
                output, "%s%s *", procedure->external_parameter_const[parameter] ? "const " : "",
                procedure->external_parameter_types[parameter] == TYPE_DERIVED &&
                        procedure->external_parameter_derived_types[parameter] != NULL
                    ? procedure->external_parameter_derived_types[parameter]->c_name
                    : f2c_c_type_kind(procedure->external_parameter_types[parameter],
                                      procedure->external_parameter_kinds[parameter]));
    }
    for (parameter = 0U; parameter < procedure->external_parameter_count; ++parameter) {
        if (procedure->external_parameter_types[parameter] == TYPE_CHARACTER &&
            !procedure->external_parameter_allocatable[parameter] &&
            !procedure->external_parameter_pointer[parameter])
            f2c_buffer_append(output, ", size_t");
    }
    f2c_buffer_append(output, ")");
}

static void emit_named_signature(Buffer *output, Unit *unit, const char *name,
                                 int restricted_arguments) {
    size_t i;
    const int allocatable_result = f2c_unit_has_allocatable_result(unit);
    const int character_result =
        !allocatable_result && unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER;
    if (unit->kind == UNIT_PROGRAM) {
        f2c_buffer_append(output, "int main(void)");
        return;
    }
    f2c_buffer_printf(output, "%s %s(",
                      allocatable_result ? "f2c_descriptor"
                      : unit->kind == UNIT_SUBROUTINE || character_result
                          ? "void"
                          : function_return_c_type(unit),
                      name);
    if (character_result)
        f2c_buffer_printf(output, "char *%sf2c_result, size_t f2c_result_len",
                          restricted_arguments ? "F2C_RESTRICT " : "");
    if (unit->argument_count == 0U && !character_result) {
        f2c_buffer_append(output, "void");
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        const char *qualifier = symbol != NULL && symbol->intent == F2C_INTENT_IN ? "const " : "";
        if (i != 0U || character_result)
            f2c_buffer_append(output, ", ");
        if (symbol != NULL && symbol->external) {
            f2c_emit_procedure_pointer_type(output, symbol, f2c_symbol_c_name(unit, symbol));
        } else if (symbol != NULL && (symbol->allocatable || symbol->pointer)) {
            f2c_buffer_printf(output, "f2c_descriptor *f2c_descriptor_%s",
                              f2c_symbol_c_name(unit, symbol));
        } else {
            f2c_buffer_printf(output, "%s%s *%s%s", qualifier,
                              symbol != NULL ? f2c_symbol_c_type(symbol) : f2c_c_type(TYPE_REAL),
                              restricted_arguments && symbol != NULL ? "F2C_RESTRICT " : "",
                              symbol != NULL ? f2c_symbol_c_name(unit, symbol)
                                             : unit->arguments[i]);
        }
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        if (symbol != NULL && !symbol->external && symbol->type == TYPE_CHARACTER &&
            !symbol->allocatable && !symbol->pointer)
            f2c_buffer_printf(output, ", size_t f2c_len_%s", f2c_symbol_c_name(unit, symbol));
    }
    f2c_buffer_append(output, ")");
}

static void emit_signature(Buffer *output, Unit *unit) {
    emit_named_signature(output, unit, unit->name, 0);
}

void f2c_emit_procedure_prototype(Buffer *output, Unit *unit) {
    if (unit == NULL || unit->kind == UNIT_PROGRAM)
        return;
    emit_signature(output, unit);
    f2c_buffer_append(output, ";\n");
}

static int is_defined_unit(Context *context, const char *name) {
    size_t i;
    for (i = 0U; i < context->units.count; ++i) {
        if (strcmp(context->units.items[i].name, name) == 0)
            return 1;
    }
    return 0;
}

static void emit_external_prototypes(Context *context) {
    size_t u;
    for (u = 0U; u < context->units.count; ++u) {
        Unit *unit = &context->units.items[u];
        size_t s;
        for (s = 0U; s < unit->symbol_count; ++s) {
            Symbol *symbol = &unit->symbols[s];
            size_t previous_unit;
            int already_emitted = 0;
            if (!symbol->external || symbol->argument || symbol->procedure_pointer ||
                is_defined_unit(context, f2c_symbol_c_name(unit, symbol)))
                continue;
            for (previous_unit = 0U; previous_unit <= u && !already_emitted; ++previous_unit) {
                Unit *previous = &context->units.items[previous_unit];
                size_t limit = previous_unit == u ? s : previous->symbol_count;
                size_t previous_symbol;
                for (previous_symbol = 0U; previous_symbol < limit; ++previous_symbol) {
                    if (previous->symbols[previous_symbol].external &&
                        strcmp(f2c_symbol_c_name(previous, &previous->symbols[previous_symbol]),
                               f2c_symbol_c_name(unit, symbol)) == 0) {
                        already_emitted = 1;
                        break;
                    }
                }
            }
            if (!already_emitted) {
                size_t parameter;
                const int allocatable_result =
                    !symbol->external_subroutine && symbol->external_result_allocatable;
                const int character_result = !allocatable_result && !symbol->external_subroutine &&
                                             symbol->type == TYPE_CHARACTER;
                f2c_buffer_printf(&context->output, "extern %s %s(",
                                  allocatable_result ? "f2c_descriptor"
                                  : symbol->external_subroutine || character_result
                                      ? "void"
                                      : f2c_symbol_c_type(symbol),
                                  f2c_symbol_c_name(unit, symbol));
                if (character_result)
                    f2c_buffer_append(&context->output, "char *, size_t");
                if (symbol->external_parameter_count == 0U && !character_result) {
                    f2c_buffer_append(&context->output, "void");
                }
                for (parameter = 0U; parameter < symbol->external_parameter_count; ++parameter) {
                    Symbol *procedure = symbol->external_parameter_procedures[parameter];
                    if (parameter != 0U || character_result)
                        f2c_buffer_append(&context->output, ", ");
                    if (procedure != NULL)
                        f2c_emit_procedure_pointer_type(&context->output, procedure, NULL);
                    else if (symbol->external_parameter_allocatable[parameter] ||
                             symbol->external_parameter_pointer[parameter])
                        f2c_buffer_append(&context->output, "f2c_descriptor *");
                    else
                        f2c_buffer_printf(
                            &context->output, "%s%s *",
                            symbol->external_parameter_const[parameter] ? "const " : "",
                            f2c_c_type_kind(symbol->external_parameter_types[parameter],
                                            symbol->external_parameter_kinds[parameter]));
                }
                for (parameter = 0U; parameter < symbol->external_parameter_count; ++parameter) {
                    if (symbol->external_parameter_types[parameter] == TYPE_CHARACTER &&
                        !symbol->external_parameter_allocatable[parameter] &&
                        !symbol->external_parameter_pointer[parameter])
                        f2c_buffer_append(&context->output, ", size_t");
                }
                f2c_buffer_append(&context->output, ");\n");
            }
        }
    }
}

void f2c_emit_prototypes(Context *context) {
    size_t i;
    for (i = 0U; i < context->units.count; ++i) {
        if (context->units.items[i].kind != UNIT_PROGRAM) {
            f2c_emit_procedure_prototype(&context->output, &context->units.items[i]);
        }
    }
    emit_external_prototypes(context);
}

void f2c_emit_interface_header(Context *context) {
    size_t i;
    uint32_t hash = UINT32_C(2166136261);
    char guard[64];
    Buffer *output = &context->header;
    for (i = 0U; i < context->units.count; ++i) {
        const unsigned char *name = (const unsigned char *)context->units.items[i].name;
        while (*name != '\0') {
            hash = (hash ^ (uint32_t)*name) * UINT32_C(16777619);
            ++name;
        }
        hash = (hash ^ (uint32_t)context->units.items[i].kind) * UINT32_C(16777619);
    }
    (void)snprintf(guard, sizeof(guard), "F2C_GENERATED_INTERFACE_%08X_H", (unsigned int)hash);
    f2c_buffer_printf(output, "#ifndef %s\n#define %s\n\n", guard, guard);
    f2c_buffer_append(output, "#include <stdbool.h>\n"
                              "#include <stddef.h>\n"
                              "#include <stdint.h>\n"
                              "#include <complex.h>\n\n"
                              "#ifndef F2C_GENERATED_COMPLEX_TYPES\n"
                              "#define F2C_GENERATED_COMPLEX_TYPES\n"
                              "#if defined(_MSC_VER) && !defined(__clang__)\n"
                              "typedef _Fcomplex f2c_complex_float;\n"
                              "typedef _Dcomplex f2c_complex_double;\n"
                              "typedef _Lcomplex f2c_complex_long_double;\n"
                              "#define F2C_COMPLEX_FLOAT_INITIALIZER(real_part, imag_part) "
                              "{(real_part), (imag_part)}\n"
                              "#define F2C_COMPLEX_DOUBLE_INITIALIZER(real_part, imag_part) "
                              "{(real_part), (imag_part)}\n"
                              "#else\n"
                              "typedef float complex f2c_complex_float;\n"
                              "typedef double complex f2c_complex_double;\n"
                              "typedef long double complex f2c_complex_long_double;\n"
                              "#define F2C_COMPLEX_FLOAT_INITIALIZER(real_part, imag_part) "
                              "((real_part) + (imag_part) * I)\n"
                              "#define F2C_COMPLEX_DOUBLE_INITIALIZER(real_part, imag_part) "
                              "((real_part) + (imag_part) * I)\n"
                              "#endif\n"
                              "#endif\n\n"
                              "typedef struct f2c_descriptor {\n"
                              "    void *data;\n"
                              "    size_t element_size;\n"
                              "    size_t rank;\n"
                              "    int64_t lower[15];\n"
                              "    int64_t extent[15];\n"
                              "    size_t character_length;\n"
                              "} f2c_descriptor;\n\n"
                              "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    for (i = 0U; i < context->units.count; ++i) {
        if (context->units.items[i].kind != UNIT_PROGRAM) {
            emit_signature(output, &context->units.items[i]);
            f2c_buffer_append(output, ";\n");
        }
    }
    f2c_buffer_append(output, "\n#ifdef __cplusplus\n}\n#endif\n\n");
    f2c_buffer_printf(output, "#endif /* %s */\n", guard);
}

static int common_block_emitted_before(Context *context, size_t unit_index, size_t symbol_index,
                                       const char *block) {
    size_t u;
    for (u = 0U; u <= unit_index; ++u) {
        Unit *unit = &context->units.items[u];
        const size_t limit = u == unit_index ? symbol_index : unit->symbol_count;
        size_t s;
        for (s = 0U; s < limit; ++s) {
            if (unit->symbols[s].common_block != NULL &&
                strcmp(unit->symbols[s].common_block, block) == 0)
                return 1;
        }
    }
    return 0;
}

void f2c_emit_common_blocks(Context *context) {
    size_t u;
    int emitted_macro = 0;
    for (u = 0U; u < context->units.count; ++u) {
        Unit *unit = &context->units.items[u];
        size_t s;
        for (s = 0U; s < unit->symbol_count; ++s) {
            Symbol *first = &unit->symbols[s];
            size_t member_count = 0U;
            size_t member_index;
            if (first->common_block == NULL ||
                common_block_emitted_before(context, u, s, first->common_block))
                continue;
            if (!emitted_macro) {
                f2c_buffer_append(
                    &context->output,
                    "#if defined(_MSC_VER)\n#define F2C_COMMON_STORAGE __declspec(selectany)\n"
                    "#elif defined(__GNUC__) || defined(__clang__)\n"
                    "#define F2C_COMMON_STORAGE __attribute__((weak))\n"
                    "#else\n#define F2C_COMMON_STORAGE\n#endif\n");
                emitted_macro = 1;
            }
            for (member_index = 0U; member_index < unit->symbol_count; ++member_index) {
                Symbol *candidate = &unit->symbols[member_index];
                if (candidate->common_block != NULL &&
                    strcmp(candidate->common_block, first->common_block) == 0 &&
                    candidate->common_index + 1U > member_count)
                    member_count = candidate->common_index + 1U;
            }
            f2c_buffer_printf(&context->output, "F2C_COMMON_STORAGE struct f2c_common_%s {\n",
                              first->common_block);
            for (member_index = 0U; member_index < member_count; ++member_index) {
                Symbol *member = NULL;
                size_t candidate_index;
                for (candidate_index = 0U; candidate_index < unit->symbol_count;
                     ++candidate_index) {
                    Symbol *candidate = &unit->symbols[candidate_index];
                    if (candidate->common_block != NULL &&
                        strcmp(candidate->common_block, first->common_block) == 0 &&
                        candidate->common_index == member_index) {
                        member = candidate;
                        break;
                    }
                }
                if (member == NULL)
                    continue;
                f2c_buffer_printf(&context->output, "    %s field_%zu", f2c_symbol_c_type(member),
                                  member_index);
                if (member->type == TYPE_CHARACTER && member->rank == 0U &&
                    member->character_length != NULL) {
                    char *length = f2c_emit_cached_expression(
                        unit, member->character_length_expression, member->character_length);
                    f2c_buffer_printf(&context->output, "[(%s) + 1]", length);
                    free(length);
                } else if (member->rank != 0U) {
                    size_t d;
                    f2c_buffer_append(&context->output, "[F2C_MAX(1, ");
                    if (member->type == TYPE_CHARACTER) {
                        char *length = member->character_length != NULL
                                           ? f2c_emit_cached_expression(
                                                 unit, member->character_length_expression,
                                                 member->character_length)
                                           : f2c_strdup("1U");
                        f2c_buffer_printf(&context->output, "(size_t)(%s) * ", length);
                        free(length);
                    }
                    for (d = 0U; d < member->rank; ++d) {
                        char *lower =
                            f2c_emit_cached_expression(unit, member->dimensions[d].lower_expression,
                                                       member->dimensions[d].lower);
                        char *upper =
                            f2c_emit_cached_expression(unit, member->dimensions[d].upper_expression,
                                                       member->dimensions[d].upper);
                        f2c_buffer_printf(&context->output, "%s((%s) - (%s) + 1)",
                                          d == 0U ? "" : " * ", upper, lower);
                        free(lower);
                        free(upper);
                    }
                    f2c_buffer_append(&context->output, ")]");
                }
                f2c_buffer_append(&context->output, ";\n");
            }
            f2c_buffer_printf(&context->output, "} f2c_common_%s;\n", first->common_block);
        }
    }
    if (emitted_macro)
        f2c_buffer_append(&context->output, "\n");
}

static void emit_declarations(Context *context, Unit *unit) {
    Buffer *output = &context->output;
    size_t i;
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        size_t dimension;
        const char *name;
        if (!symbol->argument || (!symbol->allocatable && !symbol->pointer))
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        indent(output, 1);
        f2c_buffer_printf(output,
                          "if (f2c_descriptor_%s != NULL && (f2c_descriptor_%s->rank != %zuU || "
                          "f2c_descriptor_%s->element_size != sizeof(%s))) abort();\n",
                          name, name, symbol->rank, name, f2c_symbol_c_type(symbol));
        if (!symbol->optional) {
            indent(output, 1);
            f2c_buffer_printf(output, "if (f2c_descriptor_%s == NULL) abort();\n", name);
        }
        indent(output, 1);
        f2c_buffer_printf(output,
                          "%s *%s = f2c_descriptor_%s != NULL ? (%s *)"
                          "f2c_descriptor_%s->data : NULL;\n",
                          f2c_symbol_c_type(symbol), name, name, f2c_symbol_c_type(symbol), name);
        if (symbol->deferred_character) {
            indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_len_%s = f2c_descriptor_%s != NULL ? "
                              "f2c_descriptor_%s->character_length : 0U;\n",
                              name, name, name);
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            indent(output, 1);
            f2c_buffer_printf(
                output,
                "if (f2c_descriptor_%s != NULL && (f2c_descriptor_%s->lower[%zu] < "
                "INT32_MIN || f2c_descriptor_%s->lower[%zu] > INT32_MAX || "
                "f2c_descriptor_%s->extent[%zu] < 0 || f2c_descriptor_%s->extent[%zu] > "
                "INT32_MAX)) abort();\n",
                name, name, dimension, name, dimension, name, dimension, name, dimension);
            indent(output, 1);
            f2c_buffer_printf(output,
                              "int32_t %s_lower_%zu = f2c_descriptor_%s != NULL ? "
                              "(int32_t)f2c_descriptor_%s->lower[%zu] : 1;\n",
                              name, dimension + 1U, name, name, dimension);
            indent(output, 1);
            f2c_buffer_printf(output,
                              "int32_t %s_extent_%zu = f2c_descriptor_%s != NULL ? "
                              "(int32_t)f2c_descriptor_%s->extent[%zu] : 0;\n",
                              name, dimension + 1U, name, name, dimension);
        }
    }
    {
        Symbol *result = function_result_symbol(unit);
        if (result != NULL && result->allocatable) {
            size_t dimension;
            const char *name = f2c_symbol_c_name(unit, result);
            indent(output, 1);
            f2c_buffer_printf(output, "%s *%s = NULL;\n", f2c_symbol_c_type(result), name);
            if (result->deferred_character) {
                indent(output, 1);
                f2c_buffer_printf(output, "size_t f2c_char_len_%s = 0U;\n", name);
            }
            for (dimension = 0U; dimension < result->rank; ++dimension) {
                indent(output, 1);
                f2c_buffer_printf(output, "int32_t %s_lower_%zu = 1;\n", name, dimension + 1U);
                indent(output, 1);
                f2c_buffer_printf(output, "int32_t %s_extent_%zu = 0;\n", name, dimension + 1U);
            }
            indent(output, 1);
            f2c_buffer_append(output, "f2c_descriptor f2c_result_descriptor = {0};\n");
        }
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const int persistent = unit->save_all || symbol->saved || symbol->initializer != NULL;
        char *initializer = NULL;
        if (symbol->procedure_pointer && !symbol->argument && !symbol->module_entity) {
            indent(output, 1);
            if (persistent)
                f2c_buffer_append(output, "static ");
            f2c_emit_procedure_pointer_type(output, symbol, f2c_symbol_c_name(unit, symbol));
            f2c_buffer_append(output, " = NULL;\n");
            continue;
        }
        if (symbol->argument || symbol->parameter || symbol->external || symbol->module_entity ||
            symbol->common_block != NULL || symbol->alias_to != NULL ||
            symbol->statement_function ||
            (unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
             strcmp(symbol->name, unit->result_name) == 0)) {
            continue;
        }
        indent(output, 1);
        if (persistent)
            f2c_buffer_append(output, "static ");
        if (symbol->parameter)
            f2c_buffer_append(output, "const ");
        if (symbol->allocatable || symbol->pointer) {
            size_t d;
            f2c_buffer_printf(output, "%s *%s = NULL;\n", f2c_symbol_c_type(symbol),
                              f2c_symbol_c_name(unit, symbol));
            if (symbol->deferred_character) {
                indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "size_t f2c_char_len_%s = 0U;\n",
                                  f2c_symbol_c_name(unit, symbol));
            }
            for (d = 0U; d < symbol->rank; ++d) {
                indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "int32_t %s_lower_%zu = 1;\n",
                                  f2c_symbol_c_name(unit, symbol), d + 1U);
                indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "int32_t %s_extent_%zu = 0;\n",
                                  f2c_symbol_c_name(unit, symbol), d + 1U);
            }
            continue;
        }
        if (symbol->automatic_character) {
            char *length = f2c_emit_cached_expression(unit, symbol->character_length_expression,
                                                      symbol->character_length);
            char *count =
                symbol->rank != 0U ? f2c_symbol_element_count(unit, symbol) : f2c_strdup("1U");
            const char *name = f2c_symbol_c_name(unit, symbol);
            f2c_buffer_printf(output, "int64_t f2c_char_len_value_%s = (int64_t)(%s);\n", name,
                              length != NULL ? length : "0");
            indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_len_%s = f2c_char_len_value_%s > 0 ? "
                              "(size_t)f2c_char_len_value_%s : 0U;\n",
                              name, name, name);
            indent(output, 1);
            f2c_buffer_printf(output, "size_t f2c_char_count_%s = (size_t)(%s);\n", name,
                              count != NULL ? count : "0U");
            indent(output, 1);
            f2c_buffer_printf(output,
                              "if (f2c_char_len_%s != 0U && f2c_char_count_%s > "
                              "SIZE_MAX / f2c_char_len_%s) abort();\n",
                              name, name, name);
            indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_bytes_%s = f2c_char_count_%s * "
                              "f2c_char_len_%s;\n",
                              name, name, name);
            if (symbol->rank == 0U) {
                indent(output, 1);
                f2c_buffer_printf(output, "if (f2c_char_bytes_%s == SIZE_MAX) abort();\n", name);
                indent(output, 1);
                f2c_buffer_printf(output, "++f2c_char_bytes_%s;\n", name);
            }
            indent(output, 1);
            f2c_buffer_printf(output,
                              "char *%s = (char *)malloc(f2c_char_bytes_%s == 0U ? 1U : "
                              "f2c_char_bytes_%s);\n",
                              name, name, name);
            indent(output, 1);
            f2c_buffer_printf(output, "if (%s == NULL) abort();\n", name);
            indent(output, 1);
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
            char *length = f2c_emit_cached_expression(unit, symbol->character_length_expression,
                                                      symbol->character_length);
            f2c_buffer_printf(output, "[(%s) + 1]", length);
            free(length);
        } else if (symbol->rank != 0U) {
            size_t d;
            f2c_buffer_append(output, "[F2C_MAX(1, ");
            if (symbol->type == TYPE_CHARACTER) {
                char *length =
                    symbol->character_length != NULL
                        ? f2c_emit_cached_expression(unit, symbol->character_length_expression,
                                                     symbol->character_length)
                        : f2c_strdup("1U");
                f2c_buffer_printf(output, "(size_t)(%s) * ", length);
                free(length);
            }
            for (d = 0U; d < symbol->rank; ++d) {
                char *lo;
                char *hi;
                lo = f2c_emit_cached_expression(unit, symbol->dimensions[d].lower_expression,
                                                symbol->dimensions[d].lower);
                hi = f2c_emit_cached_expression(unit, symbol->dimensions[d].upper_expression,
                                                symbol->dimensions[d].upper);
                f2c_buffer_printf(output, "%s((%s) - (%s) + 1)", d == 0U ? "" : " * ", hi, lo);
                free(lo);
                free(hi);
            }
            f2c_buffer_append(output, ")]");
        }
        if (symbol->initializer != NULL) {
            if (symbol->type == TYPE_CHARACTER) {
                int supported = 0;
                initializer = f2c_character_declaration_initializer(unit, symbol, &supported);
                if (!supported) {
                    f2c_diagnostic(context, symbol->declaration_line, 1,
                                   "unsupported non-constant or shape-incompatible CHARACTER "
                                   "declaration initializer for '%s'",
                                   symbol->name);
                }
            } else {
                initializer = f2c_emit_cached_expression(unit, symbol->initializer_expression,
                                                         symbol->initializer);
            }
        }
        if (initializer != NULL)
            f2c_buffer_printf(output, " = %s", initializer);
        else if (symbol->initializer != NULL)
            f2c_buffer_append(output, " = {0}");
        else if (!symbol->parameter && symbol->rank == 0U)
            f2c_buffer_append(output, " = {0}");
        f2c_buffer_append(output, ";\n");
        if (symbol->scope_begin_line != 0U && symbol->type == TYPE_DERIVED &&
            symbol->derived_type != NULL && !persistent) {
            indent(output, 1);
            f2c_buffer_printf(output, "bool f2c_scope_live_%s = false;\n",
                              f2c_symbol_c_name(unit, symbol));
        }
        if (symbol->statement_dummy) {
            indent(output, 1);
            f2c_buffer_printf(output, "(void)%s;\n", f2c_symbol_c_name(unit, symbol));
        }
        if (symbol->initializer == NULL && !symbol->parameter && symbol->rank != 0U &&
            !unit->save_all && !symbol->saved) {
            indent(output, 1);
            f2c_buffer_printf(output, "memset(%s, 0, sizeof(%s));\n",
                              f2c_symbol_c_name(unit, symbol), f2c_symbol_c_name(unit, symbol));
        }
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL &&
            symbol->scope_begin_line == 0U) {
            const char *name = f2c_symbol_c_name(unit, symbol);
            if (symbol->rank == 0U) {
                indent(output, 1);
                f2c_buffer_printf(output, "f2c_initialize_%s(&%s);\n", symbol->derived_type->c_name,
                                  name);
            } else {
                char *count = f2c_symbol_element_count(unit, symbol);
                indent(output, 1);
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
        if (!symbol->argument || symbol->type != TYPE_DERIVED || symbol->derived_type == NULL ||
            symbol->intent != F2C_INTENT_OUT)
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        count = symbol->rank == 0U ? f2c_strdup("1U") : f2c_symbol_element_count(unit, symbol);
        indent(output, 1);
        if (symbol->pointer) {
            f2c_buffer_printf(output, "%s = NULL;\n", name);
        } else if (symbol->allocatable) {
            f2c_buffer_printf(output, "if (%s != NULL) {\n", name);
            indent(output, 2);
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            indent(output, 2);
            f2c_buffer_printf(output, "free(%s); %s = NULL;\n", name, name);
            indent(output, 1);
            f2c_buffer_append(output, "}\n");
        } else {
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            indent(output, 1);
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s));\n",
                              symbol->polymorphic ? "f2c_initialize_dynamic"
                                                  : "f2c_initialize_dynamic",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U");
        }
        free(count);
    }
    if (unit->kind == UNIT_FUNCTION && unit->return_type != TYPE_CHARACTER &&
        !f2c_unit_has_allocatable_result(unit)) {
        indent(output, 1);
        Symbol *result = function_result_symbol(unit);
        f2c_buffer_printf(output, "%s f2c_result = {0};\n", function_return_c_type(unit));
        if (result != NULL && result->type == TYPE_DERIVED && result->derived_type != NULL) {
            indent(output, 1);
            f2c_buffer_printf(output, "f2c_initialize_%s(&f2c_result);\n",
                              result->derived_type->c_name);
        }
    }
}

static int has_local_declaration(Unit *unit, Symbol *symbol) {
    return !symbol->argument && !symbol->parameter && !symbol->external && !symbol->module_entity &&
           symbol->common_block == NULL && symbol->alias_to == NULL &&
           !symbol->statement_function &&
           !(unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
             strcmp(symbol->name, unit->result_name) == 0);
}

static int is_character_temporary(const F2cExpr *expression) {
    const int function_call = expression != NULL && expression->kind == F2C_EXPR_CALL &&
                              expression->type == TYPE_CHARACTER && expression->text != NULL &&
                              !f2c_is_intrinsic_name(expression->text);
    const int concatenation = expression != NULL && expression->kind == F2C_EXPR_BINARY &&
                              expression->type == TYPE_CHARACTER && expression->text != NULL &&
                              strcmp(expression->text, "//") == 0;
    return function_call || concatenation;
}

static void assign_character_temporary(F2cExpr *expression, void *state) {
    size_t *next = (size_t *)state;
    if (is_character_temporary(expression))
        expression->temporary_index = (*next)++;
}

static void emit_character_temporary(F2cExpr *expression, void *state) {
    Buffer *output = (Buffer *)state;
    if (!is_character_temporary(expression))
        return;
    indent(output, 1);
    f2c_buffer_printf(output, "char *f2c_character_result_%zu = NULL;\n",
                      expression->temporary_index);
}

static void prepare_character_temporaries(Unit *unit) {
    size_t i;
    size_t next = 0U;
    for (i = 0U; i < unit->statement_count; ++i)
        f2c_visit_statement_expressions(&unit->statements[i], assign_character_temporary, &next);
}

static void emit_character_temporaries(Buffer *output, Unit *unit) {
    size_t i;
    for (i = 0U; i < unit->statement_count; ++i)
        f2c_visit_statement_expressions(&unit->statements[i], emit_character_temporary, output);
}

typedef struct CharacterTemporaryCleanupEmitter {
    Buffer *output;
    int depth;
} CharacterTemporaryCleanupEmitter;

static int block_scoped_symbol(const Unit *unit, const Symbol *symbol) {
    return symbol->scope_begin_line != 0U && !unit->save_all && !symbol->saved &&
           symbol->initializer == NULL && !symbol->argument && !symbol->module_entity;
}

static void emit_block_symbol_cleanup(Buffer *output, Unit *unit, Symbol *symbol, int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    size_t dimension;
    if (!block_scoped_symbol(unit, symbol))
        return;
    if (symbol->allocatable) {
        indent(output, depth);
        f2c_buffer_printf(output, "if (%s != NULL) {\n", name);
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
            char *count = f2c_symbol_element_count(unit, symbol);
            indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            free(count);
        }
        indent(output, depth + 1);
        f2c_buffer_printf(output, "free(%s); %s = NULL;\n", name, name);
        if (symbol->deferred_character) {
            indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_char_len_%s = 0U;\n", name);
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            indent(output, depth + 1);
            f2c_buffer_printf(output, "%s_lower_%zu = 1; %s_extent_%zu = 0;\n", name,
                              dimension + 1U, name, dimension + 1U);
        }
        indent(output, depth);
        f2c_buffer_append(output, "}\n");
    } else if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        indent(output, depth);
        f2c_buffer_printf(output, "if (f2c_scope_live_%s) {\n", name);
        indent(output, depth + 1);
        if (symbol->rank == 0U) {
            f2c_buffer_printf(output, "f2c_destroy_%s(&%s);\n", symbol->derived_type->c_name, name);
        } else {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            free(count);
        }
        indent(output, depth + 1);
        f2c_buffer_printf(output, "f2c_scope_live_%s = false;\n", name);
        indent(output, depth);
        f2c_buffer_append(output, "}\n");
    }
}

void f2c_emit_block_scope_begin(Buffer *output, Unit *unit, size_t line, int depth) {
    size_t i;
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const char *name;
        if (!block_scoped_symbol(unit, symbol) || symbol->scope_begin_line != line ||
            symbol->allocatable || symbol->type != TYPE_DERIVED || symbol->derived_type == NULL)
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        indent(output, depth);
        if (symbol->rank == 0U) {
            f2c_buffer_printf(output, "f2c_initialize_%s(&%s);\n", symbol->derived_type->c_name,
                              name);
        } else {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_buffer_printf(output,
                              "for (size_t f2c_scope_index = 0U; "
                              "f2c_scope_index < (size_t)(%s); ++f2c_scope_index) "
                              "f2c_initialize_%s(&%s[f2c_scope_index]);\n",
                              count != NULL ? count : "0U", symbol->derived_type->c_name, name);
            free(count);
        }
        indent(output, depth);
        f2c_buffer_printf(output, "f2c_scope_live_%s = true;\n", name);
    }
}

void f2c_emit_block_scope_end(Buffer *output, Unit *unit, size_t line, int depth) {
    size_t i = unit->symbol_count;
    while (i != 0U) {
        Symbol *symbol = &unit->symbols[--i];
        if (symbol->scope_end_line == line)
            emit_block_symbol_cleanup(output, unit, symbol, depth);
    }
}

void f2c_emit_scope_transfer_cleanup(Buffer *output, Unit *unit, size_t source_line,
                                     size_t target_line, int depth) {
    size_t i = unit->symbol_count;
    while (i != 0U) {
        Symbol *symbol = &unit->symbols[--i];
        const int source_inside =
            source_line > symbol->scope_begin_line && source_line < symbol->scope_end_line;
        const int target_inside =
            target_line > symbol->scope_begin_line && target_line < symbol->scope_end_line;
        if (block_scoped_symbol(unit, symbol) && source_inside && !target_inside)
            emit_block_symbol_cleanup(output, unit, symbol, depth);
    }
}

static void emit_character_temporary_cleanup(F2cExpr *expression, void *state) {
    CharacterTemporaryCleanupEmitter *emitter = (CharacterTemporaryCleanupEmitter *)state;
    if (!is_character_temporary(expression))
        return;
    indent(emitter->output, emitter->depth);
    f2c_buffer_printf(emitter->output, "free(f2c_character_result_%zu);\n",
                      expression->temporary_index);
}

void f2c_emit_unit_cleanup(Buffer *output, Unit *unit, int depth) {
    size_t i;
    CharacterTemporaryCleanupEmitter emitter = {output, depth};
    Symbol *function_result = function_result_symbol(unit);
    for (i = 0U; i < unit->statement_count; ++i)
        f2c_visit_statement_expressions(&unit->statements[i], emit_character_temporary_cleanup,
                                        &emitter);
    if (function_result != NULL && function_result->allocatable) {
        const char *name = f2c_symbol_c_name(unit, function_result);
        size_t dimension;
        char *character_length = function_result->type == TYPE_CHARACTER
                                     ? f2c_symbol_character_length(unit, function_result)
                                     : NULL;
        indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.data = %s;\n", name);
        indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.element_size = sizeof(%s);\n",
                          f2c_symbol_c_type(function_result));
        indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.rank = %zuU;\n", function_result->rank);
        indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.character_length = (size_t)(%s);\n",
                          character_length != NULL ? character_length : "0U");
        for (dimension = 0U; dimension < function_result->rank; ++dimension) {
            indent(output, depth);
            f2c_buffer_printf(output, "f2c_result_descriptor.lower[%zu] = %s_lower_%zu;\n",
                              dimension, name, dimension + 1U);
            indent(output, depth);
            f2c_buffer_printf(output, "f2c_result_descriptor.extent[%zu] = %s_extent_%zu;\n",
                              dimension, name, dimension + 1U);
        }
        free(character_length);
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        size_t dimension;
        if (symbol == function_result || symbol->external)
            continue;
        if ((symbol->allocatable || symbol->pointer) && symbol->argument) {
            const char *name = f2c_symbol_c_name(unit, symbol);
            indent(output, depth);
            f2c_buffer_printf(output, "if (f2c_descriptor_%s != NULL) {\n", name);
            indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_descriptor_%s->data = %s;\n", name, name);
            indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_descriptor_%s->element_size = sizeof(%s);\n", name,
                              f2c_symbol_c_type(symbol));
            indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_descriptor_%s->rank = %zuU;\n", name, symbol->rank);
            if (symbol->deferred_character) {
                indent(output, depth + 1);
                f2c_buffer_printf(
                    output, "f2c_descriptor_%s->character_length = f2c_char_len_%s;\n", name, name);
            }
            for (dimension = 0U; dimension < symbol->rank; ++dimension) {
                indent(output, depth + 1);
                f2c_buffer_printf(output, "f2c_descriptor_%s->lower[%zu] = %s_lower_%zu;\n", name,
                                  dimension, name, dimension + 1U);
                indent(output, depth + 1);
                f2c_buffer_printf(output, "f2c_descriptor_%s->extent[%zu] = %s_extent_%zu;\n", name,
                                  dimension, name, dimension + 1U);
            }
            indent(output, depth);
            f2c_buffer_append(output, "}\n");
            continue;
        }
        if (symbol->allocatable && !symbol->argument && !unit->save_all && !symbol->saved &&
            symbol->initializer == NULL) {
            indent(output, depth);
            if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
                char *count = f2c_symbol_element_count(unit, symbol);
                f2c_buffer_printf(output,
                                  "if (%s != NULL) f2c_destroy_array_%s(%s, (size_t)(%s), "
                                  "%zuU);\n",
                                  f2c_symbol_c_name(unit, symbol), symbol->derived_type->c_name,
                                  f2c_symbol_c_name(unit, symbol), count != NULL ? count : "0U",
                                  symbol->rank);
                indent(output, depth);
                free(count);
            }
            f2c_buffer_printf(output, "free(%s);\n", f2c_symbol_c_name(unit, symbol));
            continue;
        }
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL && !symbol->argument &&
            !symbol->module_entity && !unit->save_all && !symbol->saved &&
            symbol->initializer == NULL) {
            indent(output, depth);
            if (symbol->scope_begin_line != 0U) {
                f2c_buffer_printf(output, "if (f2c_scope_live_%s) {\n",
                                  f2c_symbol_c_name(unit, symbol));
                indent(output, depth + 1);
            }
            if (symbol->rank == 0U) {
                f2c_buffer_printf(output, "f2c_destroy_%s(&%s);\n", symbol->derived_type->c_name,
                                  f2c_symbol_c_name(unit, symbol));
            } else {
                char *count = f2c_symbol_element_count(unit, symbol);
                f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                                  symbol->derived_type->c_name, f2c_symbol_c_name(unit, symbol),
                                  count != NULL ? count : "0U", symbol->rank);
                free(count);
            }
            if (symbol->scope_begin_line != 0U) {
                indent(output, depth);
                f2c_buffer_append(output, "}\n");
            }
            continue;
        }
        if (!symbol->automatic_character)
            continue;
        indent(output, depth);
        f2c_buffer_printf(output, "free(%s);\n", f2c_symbol_c_name(unit, symbol));
    }
}

static void emit_unused_suppression(Buffer *output, Unit *unit) {
    size_t i;
    if (unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER &&
        !f2c_unit_has_allocatable_result(unit)) {
        indent(output, 1);
        f2c_buffer_append(output, "(void)f2c_result;\n");
        indent(output, 1);
        f2c_buffer_append(output, "(void)f2c_result_len;\n");
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        indent(output, 1);
        f2c_buffer_printf(output, "(void)%s;\n",
                          symbol != NULL ? f2c_symbol_c_name(unit, symbol) : unit->arguments[i]);
        if (symbol != NULL && !symbol->external && symbol->type == TYPE_CHARACTER &&
            !symbol->allocatable && !symbol->pointer) {
            indent(output, 1);
            f2c_buffer_printf(output, "(void)f2c_len_%s;\n", f2c_symbol_c_name(unit, symbol));
        }
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        size_t dimension;
        if (!has_local_declaration(unit, symbol))
            continue;
        indent(output, 1);
        f2c_buffer_printf(output, "(void)%s;\n", f2c_symbol_c_name(unit, symbol));
        if (!symbol->allocatable && !symbol->pointer)
            continue;
        if (symbol->deferred_character) {
            indent(output, 1);
            f2c_buffer_printf(output, "(void)f2c_char_len_%s;\n", f2c_symbol_c_name(unit, symbol));
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            indent(output, 1);
            f2c_buffer_printf(output, "(void)%s_lower_%zu;\n", f2c_symbol_c_name(unit, symbol),
                              dimension + 1U);
            indent(output, 1);
            f2c_buffer_printf(output, "(void)%s_extent_%zu;\n", f2c_symbol_c_name(unit, symbol),
                              dimension + 1U);
        }
    }
}

static char *restricted_body_name(const Unit *unit) {
    Buffer result = {0};
    f2c_buffer_printf(&result, "f2c_restricted_body_%s", unit->name);
    return f2c_buffer_take(&result);
}

static void emit_wrapper_arguments(Buffer *output, Unit *unit) {
    const int character_result = unit->kind == UNIT_FUNCTION &&
                                 unit->return_type == TYPE_CHARACTER &&
                                 !f2c_unit_has_allocatable_result(unit);
    size_t i;
    int emitted = 0;
    if (character_result) {
        f2c_buffer_append(output, "f2c_result, f2c_result_len");
        emitted = 1;
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        if (emitted)
            f2c_buffer_append(output, ", ");
        if (symbol != NULL && (symbol->allocatable || symbol->pointer))
            f2c_buffer_printf(output, "f2c_descriptor_%s", f2c_symbol_c_name(unit, symbol));
        else
            f2c_buffer_append(output, symbol != NULL ? f2c_symbol_c_name(unit, symbol)
                                                     : unit->arguments[i]);
        emitted = 1;
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        if (symbol == NULL || symbol->external || symbol->type != TYPE_CHARACTER ||
            symbol->allocatable || symbol->pointer)
            continue;
        if (emitted)
            f2c_buffer_append(output, ", ");
        f2c_buffer_printf(output, "f2c_len_%s", f2c_symbol_c_name(unit, symbol));
        emitted = 1;
    }
}

static void emit_restricted_wrapper(Buffer *output, Unit *unit, const char *body_name) {
    const int returns_value = f2c_unit_has_allocatable_result(unit) ||
                              (unit->kind == UNIT_FUNCTION && unit->return_type != TYPE_CHARACTER);
    f2c_buffer_append(output, "static ");
    emit_named_signature(output, unit, body_name, 1);
    f2c_buffer_append(output, ";\n");
    emit_signature(output, unit);
    f2c_buffer_append(output, " {\n    ");
    if (returns_value)
        f2c_buffer_append(output, "return ");
    f2c_buffer_printf(output, "%s(", body_name);
    emit_wrapper_arguments(output, unit);
    f2c_buffer_append(output, ");\n}\n");
}

void f2c_emit_unit(Context *context, Unit *unit) {
    size_t i;
    int depth = 1;
    char *body_name = NULL;
    prepare_character_temporaries(unit);
    if (unit->kind == UNIT_PROGRAM) {
        emit_signature(&context->output, unit);
    } else {
        body_name = restricted_body_name(unit);
        emit_restricted_wrapper(&context->output, unit, body_name);
        f2c_buffer_append(&context->output, "static ");
        emit_named_signature(&context->output, unit, body_name, 1);
    }
    f2c_buffer_append(&context->output, " {\n");
    emit_declarations(context, unit);
    emit_character_temporaries(&context->output, unit);
    emit_unused_suppression(&context->output, unit);
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        const size_t statement_index = i - unit->begin - 1U;
        if (!f2c_unit_line_is_active(unit, &context->lines.items[i]))
            continue;
        if (unit->options.emit_source_comments &&
            !f2c_declaration_line(context->lines.items[i].text)) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "/* Fortran %zu: %s */\n",
                              context->lines.items[i].number, context->lines.items[i].text);
        }
        if (statement_index < unit->statement_count)
            (void)f2c_emit_statement(context, unit, &unit->statements[statement_index],
                                     &context->lines.items[i], &depth);
    }
    while (depth > 1) {
        --depth;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_emit_unit_cleanup(&context->output, unit, 1);
    if (unit->kind == UNIT_PROGRAM) {
        f2c_buffer_append(&context->output, "    return 0;\n");
    } else if (f2c_unit_has_allocatable_result(unit)) {
        f2c_buffer_append(&context->output, "    return f2c_result_descriptor;\n");
    } else if (unit->kind == UNIT_FUNCTION && unit->return_type != TYPE_CHARACTER) {
        f2c_buffer_append(&context->output, "    return f2c_result;\n");
    }
    f2c_buffer_append(&context->output, "}\n\n");
    free(body_name);
}
