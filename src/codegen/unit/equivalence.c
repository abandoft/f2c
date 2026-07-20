#include "codegen/unit/private.h"

#include <stdlib.h>

static uint64_t equivalence_group_extent(const Unit *unit, size_t group_index) {
    uint64_t extent = 0U;
    uint64_t maximum_alignment = 1U;
    size_t symbol_index;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        const Symbol *symbol = &unit->symbols[symbol_index];
        uint64_t end;
        if (!symbol->equivalence_associated || symbol->equivalence_group != group_index ||
            symbol->equivalence_size > UINT64_MAX - symbol->equivalence_offset)
            continue;
        end = symbol->equivalence_offset + symbol->equivalence_size;
        if (end > extent)
            extent = end;
        if (symbol->equivalence_alignment > maximum_alignment)
            maximum_alignment = symbol->equivalence_alignment;
    }
    if (maximum_alignment != 0U && extent % maximum_alignment != 0U)
        extent += maximum_alignment - extent % maximum_alignment;
    return extent;
}

static int equivalence_group_is_common(const Unit *unit, size_t group_index) {
    size_t symbol_index;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        const Symbol *symbol = &unit->symbols[symbol_index];
        if (symbol->equivalence_associated && symbol->equivalence_group == group_index &&
            symbol->equivalence_common_block != NULL)
            return 1;
    }
    return 0;
}

static void emit_equivalence_value_declaration(Buffer *output, Unit *unit, const Symbol *symbol) {
    f2c_buffer_printf(output, "    _Alignas(%llu) %s value",
                      (unsigned long long)symbol->equivalence_alignment, f2c_symbol_c_type(symbol));
    if (symbol->type == TYPE_CHARACTER) {
        const uint64_t code_unit_size = (uint64_t)(symbol->kind > 0 ? symbol->kind : 1);
        f2c_buffer_printf(output, "[%llu]",
                          (unsigned long long)(symbol->equivalence_size / code_unit_size));
    } else if (symbol->rank != 0U) {
        size_t dimension;
        f2c_buffer_append(output, "[F2C_MAX(1, ");
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            char *lower =
                f2c_emit_typed_expression(unit, symbol->dimensions[dimension].lower_expression);
            char *upper =
                f2c_emit_typed_expression(unit, symbol->dimensions[dimension].upper_expression);
            f2c_buffer_printf(output, "%s((%s) - (%s) + 1)", dimension == 0U ? "" : " * ", upper,
                              lower);
            free(lower);
            free(upper);
        }
        f2c_buffer_append(output, ")]");
    }
    f2c_buffer_append(output, ";\n");
}

void f2c_unit_emit_equivalence_declarations(Context *context, Unit *unit) {
    size_t group_count = 0U;
    size_t symbol_index;
    size_t group_index;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        const Symbol *symbol = &unit->symbols[symbol_index];
        if (symbol->equivalence_associated && symbol->equivalence_group + 1U > group_count)
            group_count = symbol->equivalence_group + 1U;
    }
    for (group_index = 0U; group_index < group_count; ++group_index) {
        const uint64_t extent = equivalence_group_extent(unit, group_index);
        const Symbol *initializer_symbol = NULL;
        char *initializer = NULL;
        int persistent = unit->save_all;
        if (equivalence_group_is_common(unit, group_index))
            continue;
        for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
            const Symbol *symbol = &unit->symbols[symbol_index];
            uint64_t suffix;
            if (!symbol->equivalence_associated || symbol->equivalence_group != group_index)
                continue;
            suffix = extent - symbol->equivalence_offset - symbol->equivalence_size;
            if (symbol->saved || symbol->initializer_expression != NULL ||
                symbol->data_element_initializers != NULL)
                persistent = 1;
            if (symbol->initializer_expression != NULL ||
                symbol->data_element_initializers != NULL) {
                if (initializer_symbol == NULL)
                    initializer_symbol = symbol;
                else
                    f2c_diagnostic_span_code(
                        context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->declaration_span, 1,
                        "an EQUIVALENCE storage group cannot have multiple declaration "
                        "initializers");
            }
            f2c_unit_indent(&context->output, 1);
            f2c_buffer_printf(&context->output, "struct f2c_equivalence_%zu_view_%zu {\n",
                              group_index, symbol_index);
            if (symbol->equivalence_offset != 0U) {
                f2c_unit_indent(&context->output, 2);
                f2c_buffer_printf(&context->output, "unsigned char prefix[%llu];\n",
                                  (unsigned long long)symbol->equivalence_offset);
            }
            f2c_unit_indent(&context->output, 1);
            emit_equivalence_value_declaration(&context->output, unit, symbol);
            if (suffix != 0U) {
                f2c_unit_indent(&context->output, 2);
                f2c_buffer_printf(&context->output, "unsigned char suffix[%llu];\n",
                                  (unsigned long long)suffix);
            }
            f2c_unit_indent(&context->output, 1);
            f2c_buffer_append(&context->output, "};\n");
            f2c_unit_indent(&context->output, 1);
            f2c_buffer_printf(
                &context->output,
                "_Static_assert(offsetof(struct f2c_equivalence_%zu_view_%zu, value) == %lluU, "
                "\"EQUIVALENCE value offset\");\n",
                group_index, symbol_index, (unsigned long long)symbol->equivalence_offset);
            f2c_unit_indent(&context->output, 1);
            f2c_buffer_printf(
                &context->output,
                "_Static_assert(sizeof(struct f2c_equivalence_%zu_view_%zu) == %lluU, "
                "\"EQUIVALENCE view size\");\n",
                group_index, symbol_index, (unsigned long long)extent);
        }
        f2c_unit_indent(&context->output, 1);
        if (persistent)
            f2c_buffer_append(&context->output, "static ");
        f2c_buffer_append(&context->output, "union {\n");
        for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
            const Symbol *symbol = &unit->symbols[symbol_index];
            if (!symbol->equivalence_associated || symbol->equivalence_group != group_index)
                continue;
            f2c_unit_indent(&context->output, 2);
            f2c_buffer_printf(&context->output, "struct f2c_equivalence_%zu_view_%zu view_%zu;\n",
                              group_index, symbol_index, symbol_index);
        }
        f2c_unit_indent(&context->output, 1);
        f2c_buffer_printf(&context->output, "} f2c_equivalence_%zu", group_index);
        if (initializer_symbol != NULL) {
            initializer = f2c_unit_static_storage_initializer(unit, initializer_symbol);
            if (initializer == NULL)
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_UNSUPPORTED, &initializer_symbol->declaration_span, 1,
                    "EQUIVALENCE initializer for '%s' cannot be emitted as static C17 data",
                    initializer_symbol->name);
        }
        if (initializer != NULL) {
            const size_t initializer_index = (size_t)(initializer_symbol - unit->symbols);
            f2c_buffer_printf(&context->output, " = {.view_%zu = {.value = %s}}", initializer_index,
                              initializer);
        } else {
            f2c_buffer_append(&context->output, " = {0}");
        }
        f2c_buffer_append(&context->output, ";\n");
        free(initializer);
    }
}
