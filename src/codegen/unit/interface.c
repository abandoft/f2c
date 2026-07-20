#include "codegen/unit/private.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void f2c_unit_indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

Symbol *f2c_unit_function_result(Unit *unit) {
    return unit != NULL && unit->kind == UNIT_FUNCTION && unit->result_name != NULL
               ? f2c_find_symbol(unit, unit->result_name)
               : NULL;
}

const char *f2c_unit_function_return_type(Unit *unit) {
    Symbol *result = f2c_unit_function_result(unit);
    return result != NULL && result->type == TYPE_DERIVED
               ? f2c_symbol_c_type(result)
               : f2c_c_type_kind(unit->return_type, unit->return_kind);
}

int f2c_unit_has_allocatable_result(Unit *unit) {
    Symbol *result = f2c_unit_function_result(unit);
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
        else if (procedure->external_parameter_descriptor[parameter])
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
            !procedure->external_parameter_pointer[parameter] &&
            !procedure->external_parameter_descriptor[parameter])
            f2c_buffer_append(output, ", size_t");
    }
    f2c_buffer_append(output, ")");
}

void f2c_unit_emit_named_signature(Buffer *output, Unit *unit, const char *name,
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
                          : f2c_unit_function_return_type(unit),
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
        } else if (symbol != NULL && f2c_symbol_uses_descriptor(symbol)) {
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
            !f2c_symbol_uses_descriptor(symbol))
            f2c_buffer_printf(output, ", size_t f2c_len_%s", f2c_symbol_c_name(unit, symbol));
    }
    f2c_buffer_append(output, ")");
}

void f2c_unit_emit_signature(Buffer *output, Unit *unit) {
    f2c_unit_emit_named_signature(output, unit, unit->name, 0);
}

void f2c_emit_procedure_prototype(Buffer *output, Unit *unit) {
    if (unit == NULL || unit->kind == UNIT_PROGRAM || unit->kind == UNIT_BLOCK_DATA)
        return;
    f2c_unit_emit_signature(output, unit);
    f2c_buffer_append(output, ";\n");
}

static int is_defined_unit(Context *context, const char *name) {
    size_t i;
    for (i = 0U; i < context->units.count; ++i) {
        const Unit *unit = &context->units.items[i];
        if ((unit->kind == UNIT_SUBROUTINE || unit->kind == UNIT_FUNCTION) &&
            strcmp(unit->name, name) == 0)
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
                    else if (symbol->external_parameter_descriptor[parameter])
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
                        !symbol->external_parameter_pointer[parameter] &&
                        !symbol->external_parameter_descriptor[parameter])
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
        if (context->units.items[i].kind == UNIT_SUBROUTINE ||
            context->units.items[i].kind == UNIT_FUNCTION) {
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
                              "    ptrdiff_t stride[15];\n"
                              "    size_t character_length;\n"
                              "} f2c_descriptor;\n\n"
                              "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    for (i = 0U; i < context->units.count; ++i) {
        if (context->units.items[i].kind == UNIT_SUBROUTINE ||
            context->units.items[i].kind == UNIT_FUNCTION) {
            f2c_unit_emit_signature(output, &context->units.items[i]);
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

static void append_common_storage_name(Buffer *output, const char *block) {
    if (block[0] == '\0')
        f2c_buffer_append(output, "f2c_blank_common");
    else
        f2c_buffer_printf(output, "f2c_common_%s", block);
}

static Symbol *find_common_member(Unit *unit, const char *block, size_t member_index) {
    size_t symbol_index;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        Symbol *candidate = &unit->symbols[symbol_index];
        if (candidate->common_block != NULL && candidate->common_index == member_index &&
            strcmp(candidate->common_block, block) == 0)
            return candidate;
    }
    return NULL;
}

static int common_symbol_has_initializer(const Symbol *symbol) {
    return symbol != NULL &&
           (symbol->initializer_expression != NULL || symbol->data_element_initializers != NULL ||
            symbol->data_initializer);
}

static int unit_has_common_block(const Unit *unit, const char *block) {
    size_t symbol_index;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index)
        if (unit->symbols[symbol_index].common_block != NULL &&
            strcmp(unit->symbols[symbol_index].common_block, block) == 0)
            return 1;
    return 0;
}

static Unit *find_common_initializer_owner(Context *context, const char *block,
                                           size_t *owner_index) {
    size_t unit_index;
    for (unit_index = 0U; unit_index < context->units.count; ++unit_index) {
        Unit *unit = &context->units.items[unit_index];
        size_t symbol_index;
        for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
            Symbol *candidate = &unit->symbols[symbol_index];
            if (candidate->common_block != NULL && strcmp(candidate->common_block, block) == 0 &&
                common_symbol_has_initializer(candidate)) {
                *owner_index = unit_index;
                return unit;
            }
        }
    }
    return NULL;
}

static void append_common_view_type_name(Buffer *output, const char *block, size_t unit_index) {
    if (block[0] == '\0')
        f2c_buffer_printf(output, "f2c_blank_common_view_%zu", unit_index);
    else
        f2c_buffer_printf(output, "f2c_common_%s_view_%zu", block, unit_index);
}

static uint64_t common_view_extent(const Unit *unit, const char *block) {
    uint64_t extent = 0U;
    uint64_t maximum_alignment = 1U;
    size_t symbol_index;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        const Symbol *symbol = &unit->symbols[symbol_index];
        uint64_t end;
        if (symbol->common_block == NULL || strcmp(symbol->common_block, block) != 0 ||
            symbol->common_size > UINT64_MAX - symbol->common_offset)
            continue;
        end = symbol->common_offset + symbol->common_size;
        if (end > extent)
            extent = end;
        if (symbol->common_alignment > maximum_alignment)
            maximum_alignment = symbol->common_alignment;
    }
    if (maximum_alignment != 0U && extent % maximum_alignment != 0U)
        extent += maximum_alignment - extent % maximum_alignment;
    return extent;
}

static void emit_common_field(Buffer *output, Unit *unit, const Symbol *member) {
    f2c_buffer_printf(output, "    _Alignas(%llu) %s field_%zu",
                      (unsigned long long)member->common_alignment, f2c_symbol_c_type(member),
                      member->common_index);
    if (member->type == TYPE_CHARACTER) {
        const uint64_t code_unit_size = (uint64_t)(member->kind > 0 ? member->kind : 1);
        f2c_buffer_printf(output, "[%llu]",
                          (unsigned long long)(member->common_size / code_unit_size));
    } else if (member->rank != 0U) {
        size_t dimension;
        f2c_buffer_append(output, "[F2C_MAX(1, ");
        for (dimension = 0U; dimension < member->rank; ++dimension) {
            char *lower =
                f2c_emit_typed_expression(unit, member->dimensions[dimension].lower_expression);
            char *upper =
                f2c_emit_typed_expression(unit, member->dimensions[dimension].upper_expression);
            f2c_buffer_printf(output, "%s((%s) - (%s) + 1)", dimension == 0U ? "" : " * ", upper,
                              lower);
            free(lower);
            free(upper);
        }
        f2c_buffer_append(output, ")]");
    }
    f2c_buffer_append(output, ";\n");
}

static void emit_common_view_definition(Context *context, Unit *unit, size_t unit_index,
                                        const char *block) {
    size_t member_index = 0U;
    Symbol *member;
    f2c_buffer_append(&context->output, "struct ");
    append_common_view_type_name(&context->output, block, unit_index);
    f2c_buffer_append(&context->output, " {\n");
    while ((member = find_common_member(unit, block, member_index)) != NULL) {
        emit_common_field(&context->output, unit, member);
        ++member_index;
    }
    f2c_buffer_append(&context->output, "};\n");
    member_index = 0U;
    while ((member = find_common_member(unit, block, member_index)) != NULL) {
        f2c_buffer_append(&context->output, "_Static_assert(offsetof(struct ");
        append_common_view_type_name(&context->output, block, unit_index);
        f2c_buffer_printf(&context->output, ", field_%zu) == %lluU, \"COMMON field offset\");\n",
                          member_index, (unsigned long long)member->common_offset);
        ++member_index;
    }
    f2c_buffer_append(&context->output, "_Static_assert(sizeof(struct ");
    append_common_view_type_name(&context->output, block, unit_index);
    f2c_buffer_printf(&context->output, ") == %lluU, \"COMMON view size\");\n",
                      (unsigned long long)common_view_extent(unit, block));
}

void f2c_emit_common_blocks(Context *context) {
    size_t unit_index;
    int emitted_macro = 0;
    for (unit_index = 0U; unit_index < context->units.count; ++unit_index) {
        Unit *unit = &context->units.items[unit_index];
        size_t symbol_index;
        for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
            Symbol *first = &unit->symbols[symbol_index];
            size_t view_index;
            size_t initializer_owner_index = 0U;
            Unit *initializer_owner;
            if (first->common_block == NULL ||
                common_block_emitted_before(context, unit_index, symbol_index, first->common_block))
                continue;
            if (!emitted_macro) {
                f2c_buffer_append(
                    &context->output,
                    "#if defined(_MSC_VER)\n#define F2C_COMMON_STORAGE __declspec(selectany)\n"
                    "#elif defined(__GNUC__) || defined(__clang__)\n"
                    "#define F2C_COMMON_STORAGE __attribute__((weak))\n"
                    "#else\n#define F2C_COMMON_STORAGE\n#endif\n"
                    "#define F2C_COMMON_INITIALIZED_STORAGE\n");
                emitted_macro = 1;
            }
            for (view_index = 0U; view_index < context->units.count; ++view_index)
                if (unit_has_common_block(&context->units.items[view_index], first->common_block))
                    emit_common_view_definition(context, &context->units.items[view_index],
                                                view_index, first->common_block);
            initializer_owner = find_common_initializer_owner(context, first->common_block,
                                                              &initializer_owner_index);
            f2c_buffer_append(&context->output, initializer_owner != NULL
                                                    ? "F2C_COMMON_INITIALIZED_STORAGE union "
                                                    : "F2C_COMMON_STORAGE union ");
            append_common_storage_name(&context->output, first->common_block);
            f2c_buffer_append(&context->output, "_storage {\n");
            for (view_index = 0U; view_index < context->units.count; ++view_index) {
                if (!unit_has_common_block(&context->units.items[view_index], first->common_block))
                    continue;
                f2c_buffer_append(&context->output, "    struct ");
                append_common_view_type_name(&context->output, first->common_block, view_index);
                f2c_buffer_printf(&context->output, " view_%zu;\n", view_index);
            }
            f2c_buffer_append(&context->output, "} ");
            append_common_storage_name(&context->output, first->common_block);
            if (initializer_owner != NULL) {
                size_t member_index = 0U;
                int emitted_initializer = 0;
                Symbol *initializer_symbol;
                f2c_buffer_printf(&context->output, " = { .view_%zu = {\n",
                                  initializer_owner_index);
                while ((initializer_symbol = find_common_member(
                            initializer_owner, first->common_block, member_index)) != NULL) {
                    char *initializer;
                    ++member_index;
                    if (!common_symbol_has_initializer(initializer_symbol))
                        continue;
                    initializer =
                        f2c_unit_static_storage_initializer(initializer_owner, initializer_symbol);
                    if (initializer == NULL) {
                        f2c_diagnostic_span_code(
                            context, F2C_DIAGNOSTIC_UNSUPPORTED,
                            initializer_symbol->declaration_span.begin.line != 0U
                                ? &initializer_symbol->declaration_span
                                : &initializer_symbol->common_span,
                            1, "COMMON initializer for '%s' cannot be emitted as static C17 data",
                            initializer_symbol->name);
                        continue;
                    }
                    f2c_buffer_printf(&context->output, "    .field_%zu = %s,\n",
                                      initializer_symbol->common_index, initializer);
                    free(initializer);
                    emitted_initializer = 1;
                }
                if (!emitted_initializer)
                    f2c_buffer_append(&context->output, "    0\n");
                f2c_buffer_append(&context->output, "} }");
            }
            f2c_buffer_append(&context->output, ";\n");
        }
    }
    if (emitted_macro)
        f2c_buffer_append(&context->output, "\n");
}
