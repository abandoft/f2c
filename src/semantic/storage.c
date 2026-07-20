#include "semantic/semantic.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *display_block_name(const char *block) {
    return block != NULL && block[0] != '\0' ? block : "<blank>";
}

static const F2cStatement *statement_body(const F2cStatement *statement) {
    return statement != NULL && statement->kind == F2C_STMT_LABEL ? statement->nested : statement;
}

static int symbol_has_storage_initializer(const Symbol *symbol) {
    return symbol != NULL &&
           (symbol->initializer_expression != NULL || symbol->data_element_initializers != NULL ||
            symbol->data_initializer);
}

static const char *symbol_common_storage_block(const Symbol *symbol) {
    if (symbol == NULL)
        return NULL;
    return symbol->equivalence_common_block != NULL ? symbol->equivalence_common_block
                                                    : symbol->common_block;
}

static void validate_block_data_unit(Context *context, Unit *unit) {
    size_t index;
    if (unit == NULL || unit->kind != UNIT_BLOCK_DATA)
        return;
    if (unit->internal) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &unit->header_span, 1,
                                 "BLOCK DATA must be an external program unit");
    }
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *statement = statement_body(&unit->statements[index]);
        if (statement == NULL || statement->kind == F2C_STMT_EMPTY ||
            statement->kind == F2C_STMT_DECLARATION || statement->kind == F2C_STMT_DATA)
            continue;
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                 "BLOCK DATA may contain specification statements and DATA "
                                 "initialization only");
    }
    for (index = 0U; index < unit->symbol_count; ++index) {
        const Symbol *symbol = &unit->symbols[index];
        const char *block = symbol_common_storage_block(symbol);
        if (!symbol_has_storage_initializer(symbol) || symbol->parameter)
            continue;
        if (block == NULL || block[0] == '\0') {
            const F2cSourceSpan *span = symbol->declaration_span.begin.line != 0U
                                            ? &symbol->declaration_span
                                            : &unit->header_span;
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, span, 1,
                "initialized BLOCK DATA entity '%s' must belong to a named COMMON block",
                symbol->name);
        }
    }
}

static void validate_module_specification_unit(Context *context, Unit *unit) {
    size_t index;
    if (unit == NULL || unit->kind != UNIT_MODULE)
        return;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *statement = statement_body(&unit->statements[index]);
        if (statement == NULL || statement->kind == F2C_STMT_EMPTY ||
            statement->kind == F2C_STMT_DECLARATION || statement->kind == F2C_STMT_DATA)
            continue;
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                 "a MODULE specification part cannot contain executable "
                                 "statements");
    }
}

static void validate_common_initialization_owner(Context *context, size_t unit_index,
                                                 size_t symbol_index, Unit *unit,
                                                 const Symbol *symbol) {
    const char *block = symbol_common_storage_block(symbol);
    size_t previous_unit;
    if (!symbol_has_storage_initializer(symbol) || block == NULL)
        return;
    if (unit->kind != UNIT_BLOCK_DATA) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC,
            symbol->declaration_span.begin.line != 0U ? &symbol->declaration_span
                                                      : &symbol->common_span,
            1, "COMMON entity '%s' may be initialized only in BLOCK DATA", symbol->name);
        return;
    }
    if (block[0] == '\0') {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC,
                                 symbol->common_block != NULL ? &symbol->common_span
                                                              : &symbol->declaration_span,
                                 1, "blank COMMON entity '%s' cannot be initialized", symbol->name);
        return;
    }
    for (previous_unit = 0U; previous_unit < unit_index; ++previous_unit) {
        Unit *candidate_unit = &context->units.items[previous_unit];
        size_t candidate_index;
        if (candidate_unit->kind != UNIT_BLOCK_DATA)
            continue;
        for (candidate_index = 0U; candidate_index < candidate_unit->symbol_count;
             ++candidate_index) {
            const Symbol *candidate = &candidate_unit->symbols[candidate_index];
            const char *candidate_block = symbol_common_storage_block(candidate);
            if (candidate_block != NULL && symbol_has_storage_initializer(candidate) &&
                strcmp(candidate_block, block) == 0) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC,
                    symbol->common_block != NULL ? &symbol->common_span : &symbol->declaration_span,
                    1, "COMMON block '%s' is initialized by more than one BLOCK DATA program unit",
                    display_block_name(block));
                return;
            }
        }
    }
    if (symbol->common_block == NULL && symbol->equivalence_common_block != NULL) {
        size_t candidate_index;
        for (candidate_index = 0U; candidate_index < unit->symbol_count; ++candidate_index) {
            const Symbol *candidate = &unit->symbols[candidate_index];
            const char *candidate_block = symbol_common_storage_block(candidate);
            if (candidate_index == symbol_index || candidate_block == NULL ||
                strcmp(candidate_block, block) != 0 || !symbol_has_storage_initializer(candidate))
                continue;
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_UNSUPPORTED, &symbol->declaration_span, 1,
                "COMMON block '%s' DATA initializers require more than one overlapping C17 "
                "storage view",
                display_block_name(block));
            return;
        }
    }
}

static int checked_multiply(uint64_t left, uint64_t right, uint64_t *result) {
    if (left != 0U && right > UINT64_MAX / left)
        return 0;
    *result = left * right;
    return 1;
}

static int common_element_count(Unit *unit, const Symbol *symbol, uint64_t *count) {
    uint64_t total = UINT64_C(1);
    size_t dimension;
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        int64_t lower;
        int64_t upper;
        uint64_t extent;
        const int lower_known =
            symbol->dimensions[dimension].lower_expression != NULL
                ? f2c_evaluate_integer_constant(
                      unit, symbol->dimensions[dimension].lower_expression, &lower)
            : symbol->dimension_lower_syntax[dimension].count != 0U
                ? f2c_evaluate_integer_syntax(unit, symbol->dimension_lower_syntax[dimension],
                                              &lower)
                : (lower = 1, 1);
        const int upper_known =
            symbol->dimensions[dimension].upper_expression != NULL
                ? f2c_evaluate_integer_constant(
                      unit, symbol->dimensions[dimension].upper_expression, &upper)
                : f2c_evaluate_integer_syntax(unit, symbol->dimension_upper_syntax[dimension],
                                              &upper);
        if (symbol->dimensions[dimension].kind != F2C_DIMENSION_EXPLICIT || !lower_known ||
            !upper_known)
            return 0;
        if (upper < lower) {
            extent = 0U;
        } else {
            extent = (uint64_t)upper - (uint64_t)lower;
            if (extent == UINT64_MAX)
                return 0;
            ++extent;
        }
        if (!checked_multiply(total, extent, &total))
            return 0;
    }
    *count = total;
    return 1;
}

static int common_character_length(Unit *unit, const Symbol *symbol, int64_t *length) {
    char *end = NULL;
    long long parsed;
    if (symbol->type != TYPE_CHARACTER) {
        *length = 0;
        return 1;
    }
    if (symbol->character_length_expression != NULL)
        return f2c_evaluate_integer_constant(unit, symbol->character_length_expression, length) &&
               *length >= 0;
    if (symbol->character_length_syntax.count != 0U)
        return f2c_evaluate_integer_syntax(unit, symbol->character_length_syntax, length) &&
               *length >= 0;
    if (symbol->character_length == NULL)
        return 0;
    parsed = strtoll(symbol->character_length, &end, 10);
    if (end == symbol->character_length || *end != '\0' || parsed < 0)
        return 0;
    *length = (int64_t)parsed;
    return 1;
}

static int resolved_kind(const Symbol *symbol) {
    return symbol->kind > 0 ? symbol->kind : f2c_default_kind(symbol->type);
}

static int common_element_layout(Unit *unit, const Symbol *symbol, uint64_t *size,
                                 uint64_t *alignment) {
    uint64_t character_length;
    int64_t signed_character_length;
    const int kind = resolved_kind(symbol);
    switch (symbol->type) {
    case TYPE_INTEGER:
    case TYPE_LOGICAL:
    case TYPE_REAL:
    case TYPE_DOUBLE:
        if (kind != 1 && kind != 2 && kind != 4 && kind != 8 && kind != 16)
            return 0;
        *size = (uint64_t)kind;
        *alignment = kind > 8 ? UINT64_C(16) : (uint64_t)kind;
        return 1;
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        if (kind != 4 && kind != 8 && kind != 16)
            return 0;
        *size = (uint64_t)kind * UINT64_C(2);
        *alignment = kind > 8 ? UINT64_C(16) : (uint64_t)kind;
        return 1;
    case TYPE_CHARACTER:
        if ((kind != 1 && kind != 4) ||
            !common_character_length(unit, symbol, &signed_character_length) ||
            signed_character_length <= 0)
            return 0;
        character_length = (uint64_t)signed_character_length;
        if (!checked_multiply(character_length, (uint64_t)kind, size))
            return 0;
        *alignment = (uint64_t)kind;
        return 1;
    case TYPE_DERIVED:
    case TYPE_UNKNOWN:
    default:
        return 0;
    }
}

static int common_symbol_layout(Unit *unit, const Symbol *symbol, uint64_t *size,
                                uint64_t *alignment) {
    uint64_t element_size;
    uint64_t element_count;
    if (!common_element_layout(unit, symbol, &element_size, alignment) ||
        !common_element_count(unit, symbol, &element_count) || element_count == 0U)
        return 0;
    return checked_multiply(element_size, element_count, size);
}

static int validate_member_contract(Context *context, Unit *unit, const Symbol *symbol) {
    uint64_t size;
    uint64_t alignment;
    int valid = 1;
    if (symbol->argument || symbol->parameter || symbol->external || symbol->allocatable ||
        symbol->pointer || symbol->automatic_character || symbol->deferred_character) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
            "COMMON entity '%s' must have static non-dummy, non-parameter storage", symbol->name);
        valid = 0;
    }
    if (symbol->rank != 0U) {
        uint64_t count;
        if (!common_element_count(unit, symbol, &count) || count == 0U) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
                "COMMON array '%s' requires a nonempty constant explicit shape", symbol->name);
            valid = 0;
        }
    }
    if (symbol->type == TYPE_CHARACTER) {
        int64_t length;
        if (!common_character_length(unit, symbol, &length) || length <= 0) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
                "COMMON CHARACTER entity '%s' requires positive constant length", symbol->name);
            valid = 0;
        }
    }
    if (valid && !common_symbol_layout(unit, symbol, &size, &alignment)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, &symbol->common_span, 1,
                                 "COMMON entity '%s' has no portable fixed C17 storage layout",
                                 symbol->name);
        valid = 0;
    }
    return valid;
}

static Symbol *common_member(Unit *unit, const char *block, size_t member_index) {
    size_t symbol_index;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        Symbol *symbol = &unit->symbols[symbol_index];
        if (symbol->common_block != NULL && symbol->common_index == member_index &&
            strcmp(symbol->common_block, block) == 0)
            return symbol;
    }
    return NULL;
}

static int checked_align(uint64_t value, uint64_t alignment, uint64_t *result) {
    const uint64_t remainder = alignment != 0U ? value % alignment : 0U;
    const uint64_t padding = remainder != 0U ? alignment - remainder : 0U;
    if (value > UINT64_MAX - padding)
        return 0;
    *result = value + padding;
    return 1;
}

static int common_block_layout(Context *context, Unit *unit, const char *block, uint64_t *extent) {
    uint64_t cursor = 0U;
    uint64_t maximum_alignment = 1U;
    size_t member_index = 0U;
    Symbol *symbol;
    int valid = 1;
    while ((symbol = common_member(unit, block, member_index)) != NULL) {
        uint64_t size = 0U;
        uint64_t alignment = 1U;
        if (!validate_member_contract(context, unit, symbol) ||
            !common_symbol_layout(unit, symbol, &size, &alignment) ||
            !checked_align(cursor, alignment, &cursor) || size > UINT64_MAX - cursor) {
            if (size > UINT64_MAX - cursor)
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                                         &symbol->common_span, 1,
                                         "COMMON block '%s' exceeds the supported storage size",
                                         display_block_name(block));
            valid = 0;
        } else {
            symbol->common_offset = cursor;
            symbol->common_size = size;
            symbol->common_alignment = alignment;
            cursor += size;
            if (alignment > maximum_alignment)
                maximum_alignment = alignment;
        }
        ++member_index;
    }
    if (!checked_align(cursor, maximum_alignment, extent)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, &unit->header_span, 1,
                                 "COMMON block '%s' exceeds the supported storage size",
                                 display_block_name(block));
        return 0;
    }
    return valid;
}

static Symbol *first_common_member(Unit *unit, const char *block) {
    return common_member(unit, block, 0U);
}

static int block_seen_earlier_in_unit(const Unit *unit, size_t symbol_index, const char *block) {
    size_t index;
    for (index = 0U; index < symbol_index; ++index)
        if (unit->symbols[index].common_block != NULL &&
            strcmp(unit->symbols[index].common_block, block) == 0)
            return 1;
    return 0;
}

static int unit_common_extent(Unit *unit, const char *block, uint64_t *extent) {
    uint64_t maximum_alignment = 1U;
    uint64_t end = 0U;
    size_t index;
    for (index = 0U; index < unit->symbol_count; ++index) {
        const Symbol *symbol = &unit->symbols[index];
        uint64_t offset;
        uint64_t candidate;
        const int direct = symbol->common_block != NULL && strcmp(symbol->common_block, block) == 0;
        const int associated = symbol->equivalence_common_block != NULL &&
                               strcmp(symbol->equivalence_common_block, block) == 0;
        if (!direct && !associated)
            continue;
        offset = associated ? symbol->equivalence_common_offset : symbol->common_offset;
        if ((associated ? symbol->equivalence_size : symbol->common_size) > UINT64_MAX - offset)
            return 0;
        candidate = offset + (associated ? symbol->equivalence_size : symbol->common_size);
        if (candidate > end)
            end = candidate;
        if ((associated ? symbol->equivalence_alignment : symbol->common_alignment) >
            maximum_alignment)
            maximum_alignment =
                associated ? symbol->equivalence_alignment : symbol->common_alignment;
    }
    return checked_align(end, maximum_alignment, extent);
}

static Unit *previous_common_owner(Context *context, size_t unit_index, const char *block,
                                   uint64_t *extent) {
    size_t index;
    for (index = 0U; index < unit_index; ++index) {
        Unit *unit = &context->units.items[index];
        if (first_common_member(unit, block) != NULL && unit_common_extent(unit, block, extent))
            return unit;
    }
    return NULL;
}

static void assign_common_c_name(Context *context, size_t unit_index, Symbol *symbol) {
    Buffer name = {0};
    if (symbol->common_block[0] == '\0')
        f2c_buffer_printf(&name, "f2c_blank_common.view_%zu.field_%zu", unit_index,
                          symbol->common_index);
    else
        f2c_buffer_printf(&name, "f2c_common_%s.view_%zu.field_%zu", symbol->common_block,
                          unit_index, symbol->common_index);
    free(symbol->c_name);
    symbol->c_name = f2c_buffer_take(&name);
    if (symbol->c_name == NULL)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &symbol->common_span, 1,
                                 "out of memory naming COMMON entity '%s'", symbol->name);
}

static int equivalence_common_group_seen(const Unit *unit, size_t symbol_index, const char *block,
                                         size_t group_index) {
    size_t index;
    for (index = 0U; index < symbol_index; ++index) {
        const Symbol *symbol = &unit->symbols[index];
        if (symbol->equivalence_associated && symbol->equivalence_group == group_index &&
            symbol->equivalence_common_block != NULL &&
            strcmp(symbol->equivalence_common_block, block) == 0)
            return 1;
    }
    return 0;
}

static void assign_equivalence_common_c_name(Context *context, size_t unit_index,
                                             size_t symbol_index, Symbol *symbol) {
    Buffer name = {0};
    if (symbol->equivalence_common_block[0] == '\0')
        f2c_buffer_printf(&name, "f2c_blank_common.equivalence_%zu_%zu_%zu.value", unit_index,
                          symbol->equivalence_group, symbol_index);
    else
        f2c_buffer_printf(&name, "f2c_common_%s.equivalence_%zu_%zu_%zu.value",
                          symbol->equivalence_common_block, unit_index, symbol->equivalence_group,
                          symbol_index);
    free(symbol->c_name);
    symbol->c_name = f2c_buffer_take(&name);
    if (symbol->c_name == NULL)
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &symbol->declaration_span, 1,
            "out of memory naming COMMON-associated EQUIVALENCE entity '%s'", symbol->name);
}

static int extend_common_with_equivalence(Context *context, Unit *unit, size_t unit_index,
                                          const char *block, uint64_t *extent) {
    uint64_t maximum_alignment = 1U;
    size_t symbol_index;
    int valid = 1;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        const Symbol *symbol = &unit->symbols[symbol_index];
        if (symbol->common_block != NULL && strcmp(symbol->common_block, block) == 0 &&
            symbol->common_alignment > maximum_alignment)
            maximum_alignment = symbol->common_alignment;
    }
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        Symbol *first = &unit->symbols[symbol_index];
        size_t anchor_index = SIZE_MAX;
        uint64_t shift = 0U;
        int shift_negative = 0;
        size_t candidate_index;
        if (!first->equivalence_associated || first->equivalence_common_block == NULL ||
            strcmp(first->equivalence_common_block, block) != 0 ||
            equivalence_common_group_seen(unit, symbol_index, block, first->equivalence_group))
            continue;
        for (candidate_index = 0U; candidate_index < unit->symbol_count; ++candidate_index) {
            Symbol *candidate = &unit->symbols[candidate_index];
            uint64_t candidate_shift;
            int candidate_negative;
            if (!candidate->equivalence_associated ||
                candidate->equivalence_group != first->equivalence_group ||
                candidate->common_block == NULL || strcmp(candidate->common_block, block) != 0)
                continue;
            candidate_negative = candidate->common_offset < candidate->equivalence_offset;
            candidate_shift = candidate_negative
                                  ? candidate->equivalence_offset - candidate->common_offset
                                  : candidate->common_offset - candidate->equivalence_offset;
            if (anchor_index == SIZE_MAX) {
                anchor_index = candidate_index;
                shift = candidate_shift;
                shift_negative = candidate_negative;
            } else if (shift != candidate_shift || shift_negative != candidate_negative) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &candidate->common_span, 1,
                    "EQUIVALENCE constraints disagree with COMMON block '%s' offsets",
                    display_block_name(block));
                valid = 0;
            }
        }
        if (anchor_index == SIZE_MAX) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &first->declaration_span, 1,
                                     "EQUIVALENCE COMMON association has no block anchor");
            valid = 0;
            continue;
        }
        for (candidate_index = 0U; candidate_index < unit->symbol_count; ++candidate_index) {
            Symbol *candidate = &unit->symbols[candidate_index];
            uint64_t offset;
            uint64_t end;
            if (!candidate->equivalence_associated ||
                candidate->equivalence_group != first->equivalence_group)
                continue;
            if (shift_negative) {
                if (candidate->equivalence_offset < shift) {
                    f2c_diagnostic_span_code(
                        context, F2C_DIAGNOSTIC_SEMANTIC, &candidate->declaration_span, 1,
                        "EQUIVALENCE cannot extend COMMON block '%s' before its first storage "
                        "unit",
                        display_block_name(block));
                    valid = 0;
                    continue;
                }
                offset = candidate->equivalence_offset - shift;
            } else {
                if (candidate->equivalence_offset > UINT64_MAX - shift) {
                    f2c_diagnostic_span_code(
                        context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, &candidate->declaration_span, 1,
                        "EQUIVALENCE COMMON offset exceeds the supported size range");
                    valid = 0;
                    continue;
                }
                offset = candidate->equivalence_offset + shift;
            }
            if (candidate->equivalence_alignment == 0U ||
                offset % candidate->equivalence_alignment != 0U) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_UNSUPPORTED, &candidate->declaration_span, 1,
                    "COMMON-associated EQUIVALENCE places '%s' at an unrepresentable alignment",
                    candidate->name);
                valid = 0;
                continue;
            }
            if (candidate->equivalence_size > UINT64_MAX - offset) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                                         &candidate->declaration_span, 1,
                                         "EQUIVALENCE COMMON storage exceeds the supported size "
                                         "range");
                valid = 0;
                continue;
            }
            candidate->equivalence_common_offset = offset;
            end = offset + candidate->equivalence_size;
            if (end > *extent)
                *extent = end;
            if (candidate->equivalence_alignment > maximum_alignment)
                maximum_alignment = candidate->equivalence_alignment;
            assign_equivalence_common_c_name(context, unit_index, candidate_index, candidate);
        }
    }
    if (!checked_align(*extent, maximum_alignment, extent)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, &unit->header_span, 1,
                                 "COMMON storage exceeds the supported size range");
        return 0;
    }
    return valid;
}

static size_t symbol_index_by_name(const Unit *unit, const char *name) {
    size_t index;
    for (index = 0U; index < unit->symbol_count; ++index)
        if (strcmp(unit->symbols[index].name, name) == 0)
            return index;
    return SIZE_MAX;
}

static int equivalence_designator_byte_offset(Unit *unit, const F2cEquivalenceMember *member,
                                              int64_t *offset) {
    const size_t symbol_index = symbol_index_by_name(unit, member->symbol_name);
    uint64_t element_size;
    uint64_t alignment;
    uint64_t byte_offset;
    if (symbol_index == SIZE_MAX || member->element_offset < 0 ||
        !common_element_layout(unit, &unit->symbols[symbol_index], &element_size, &alignment) ||
        !checked_multiply(element_size, (uint64_t)member->element_offset, &byte_offset) ||
        byte_offset > (uint64_t)INT64_MAX)
        return 0;
    *offset = (int64_t)byte_offset;
    return 1;
}

static int checked_signed_add(int64_t left, int64_t right, int64_t *result) {
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right))
        return 0;
    *result = left + right;
    return 1;
}

static int checked_signed_subtract(int64_t left, int64_t right, int64_t *result) {
    if (right == INT64_MIN) {
        if (left >= 0)
            return 0;
        *result = left - right;
        return 1;
    }
    return checked_signed_add(left, -right, result);
}

static void report_equivalence_conflict(Context *context, const F2cEquivalenceMember *member) {
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &member->span, 1,
                             "conflicting EQUIVALENCE storage associations");
}

static int validate_equivalence_member(Context *context, Unit *unit,
                                       const F2cEquivalenceMember *member, size_t symbol_index) {
    Symbol *symbol = &unit->symbols[symbol_index];
    uint64_t size;
    uint64_t alignment;
    int64_t designator_offset;
    if (symbol->argument || symbol->parameter || symbol->external || symbol->allocatable ||
        symbol->pointer || symbol->procedure_pointer || symbol->automatic_character ||
        symbol->deferred_character || symbol->module_entity) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &member->span, 1,
                                 "EQUIVALENCE entity '%s' must have fixed local non-dummy storage",
                                 symbol->name);
        return 0;
    }
    if (!common_symbol_layout(unit, symbol, &size, &alignment) ||
        !equivalence_designator_byte_offset(unit, member, &designator_offset)) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_UNSUPPORTED, &member->span, 1,
            "EQUIVALENCE entity '%s' requires constant nonempty intrinsic storage", symbol->name);
        return 0;
    }
    symbol->equivalence_size = size;
    symbol->equivalence_alignment = alignment;
    return 1;
}

static void assign_equivalence_c_name(Context *context, Unit *unit, size_t group_index,
                                      size_t symbol_index) {
    Symbol *symbol = &unit->symbols[symbol_index];
    Buffer name = {0};
    f2c_buffer_printf(&name, "f2c_equivalence_%zu.view_%zu.value", group_index, symbol_index);
    free(symbol->c_name);
    symbol->c_name = f2c_buffer_take(&name);
    if (symbol->c_name == NULL)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &symbol->declaration_span,
                                 1, "out of memory naming EQUIVALENCE entity '%s'", symbol->name);
}

void f2c_resolve_equivalence_storage(Context *context, Unit *unit) {
    unsigned char *involved;
    unsigned char *known;
    unsigned char *assigned;
    unsigned char *conflict_reported;
    int64_t *offsets;
    size_t declaration_index;
    size_t component_index = 0U;
    size_t seed;
    if (context == NULL || unit == NULL || unit->equivalence_group_count == 0U)
        return;
    involved = (unsigned char *)calloc(unit->symbol_count, sizeof(*involved));
    known = (unsigned char *)calloc(unit->symbol_count, sizeof(*known));
    assigned = (unsigned char *)calloc(unit->symbol_count, sizeof(*assigned));
    conflict_reported =
        (unsigned char *)calloc(unit->equivalence_group_count, sizeof(*conflict_reported));
    offsets = (int64_t *)calloc(unit->symbol_count, sizeof(*offsets));
    if (involved == NULL || known == NULL || assigned == NULL || conflict_reported == NULL ||
        offsets == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &unit->header_span, 1,
                                 "out of memory resolving EQUIVALENCE storage");
        goto cleanup;
    }
    for (declaration_index = 0U; declaration_index < unit->equivalence_group_count;
         ++declaration_index) {
        F2cEquivalenceGroup *group = &unit->equivalence_groups[declaration_index];
        size_t member_index;
        for (member_index = 0U; member_index < group->member_count; ++member_index) {
            const size_t symbol_index =
                symbol_index_by_name(unit, group->members[member_index].symbol_name);
            if (symbol_index == SIZE_MAX ||
                !validate_equivalence_member(context, unit, &group->members[member_index],
                                             symbol_index))
                continue;
            involved[symbol_index] = 1U;
        }
    }
    for (seed = 0U; seed < unit->symbol_count; ++seed) {
        int changed = 1;
        int64_t minimum_offset = 0;
        uint64_t extent = 0U;
        uint64_t maximum_alignment = 1U;
        size_t root_index = SIZE_MAX;
        const char *component_common_block = NULL;
        size_t symbol_index;
        if (!involved[seed] || assigned[seed])
            continue;
        known[seed] = 1U;
        offsets[seed] = 0;
        while (changed) {
            changed = 0;
            for (declaration_index = 0U; declaration_index < unit->equivalence_group_count;
                 ++declaration_index) {
                F2cEquivalenceGroup *group = &unit->equivalence_groups[declaration_index];
                int anchor_known = 0;
                int64_t anchor = 0;
                size_t member_index;
                for (member_index = 0U; member_index < group->member_count; ++member_index) {
                    const size_t candidate =
                        symbol_index_by_name(unit, group->members[member_index].symbol_name);
                    int64_t designator_offset;
                    if (candidate == SIZE_MAX || !known[candidate] ||
                        !equivalence_designator_byte_offset(unit, &group->members[member_index],
                                                            &designator_offset))
                        continue;
                    if (!checked_signed_add(offsets[candidate], designator_offset, &anchor)) {
                        if (!conflict_reported[declaration_index])
                            report_equivalence_conflict(context, &group->members[member_index]);
                        conflict_reported[declaration_index] = 1U;
                    } else {
                        anchor_known = 1;
                    }
                    break;
                }
                if (!anchor_known)
                    continue;
                for (member_index = 0U; member_index < group->member_count; ++member_index) {
                    const size_t candidate =
                        symbol_index_by_name(unit, group->members[member_index].symbol_name);
                    int64_t designator_offset;
                    int64_t expected;
                    if (candidate == SIZE_MAX || !involved[candidate] ||
                        !equivalence_designator_byte_offset(unit, &group->members[member_index],
                                                            &designator_offset) ||
                        !checked_signed_subtract(anchor, designator_offset, &expected)) {
                        if (!conflict_reported[declaration_index])
                            report_equivalence_conflict(context, &group->members[member_index]);
                        conflict_reported[declaration_index] = 1U;
                        continue;
                    }
                    if (!known[candidate]) {
                        known[candidate] = 1U;
                        offsets[candidate] = expected;
                        changed = 1;
                    } else if (offsets[candidate] != expected &&
                               !conflict_reported[declaration_index]) {
                        report_equivalence_conflict(context, &group->members[member_index]);
                        conflict_reported[declaration_index] = 1U;
                    }
                }
            }
        }
        minimum_offset = offsets[seed];
        for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
            if (!known[symbol_index] || assigned[symbol_index])
                continue;
            if (offsets[symbol_index] < minimum_offset)
                minimum_offset = offsets[symbol_index];
            if (unit->symbols[symbol_index].common_block != NULL) {
                if (component_common_block == NULL)
                    component_common_block = unit->symbols[symbol_index].common_block;
                else if (strcmp(component_common_block, unit->symbols[symbol_index].common_block) !=
                         0)
                    f2c_diagnostic_span_code(
                        context, F2C_DIAGNOSTIC_SEMANTIC, &unit->symbols[symbol_index].common_span,
                        1, "one EQUIVALENCE group cannot associate different COMMON blocks");
            }
        }
        for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
            Symbol *symbol = &unit->symbols[symbol_index];
            uint64_t normalized;
            uint64_t end;
            if (!known[symbol_index] || assigned[symbol_index])
                continue;
            normalized = (uint64_t)offsets[symbol_index] - (uint64_t)minimum_offset;
            if (symbol->equivalence_alignment == 0U ||
                normalized % symbol->equivalence_alignment != 0U) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_UNSUPPORTED, &symbol->declaration_span, 1,
                    "EQUIVALENCE places '%s' at an alignment not representable by portable C17",
                    symbol->name);
                continue;
            }
            if (symbol->equivalence_size > UINT64_MAX - normalized) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                                         &symbol->declaration_span, 1,
                                         "EQUIVALENCE storage exceeds the supported size range");
                continue;
            }
            end = normalized + symbol->equivalence_size;
            symbol->equivalence_associated = 1;
            symbol->equivalence_group = component_index;
            symbol->equivalence_offset = normalized;
            if (component_common_block != NULL) {
                free(symbol->equivalence_common_block);
                symbol->equivalence_common_block = f2c_strdup(component_common_block);
                if (symbol->equivalence_common_block == NULL)
                    f2c_diagnostic_span_code(
                        context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &symbol->declaration_span, 1,
                        "out of memory recording EQUIVALENCE COMMON association");
            }
            if (root_index == SIZE_MAX)
                root_index = symbol_index;
            if (end > extent)
                extent = end;
            if (symbol->equivalence_alignment > maximum_alignment)
                maximum_alignment = symbol->equivalence_alignment;
        }
        if (!checked_align(extent, maximum_alignment, &extent)) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, &unit->header_span, 1,
                                     "EQUIVALENCE storage exceeds the supported size range");
        }
        for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
            Symbol *symbol = &unit->symbols[symbol_index];
            if (!known[symbol_index] || assigned[symbol_index])
                continue;
            assigned[symbol_index] = 1U;
            if (!symbol->equivalence_associated || root_index == SIZE_MAX)
                continue;
            if (symbol_index != root_index) {
                int64_t alias_offset;
                free(symbol->alias_to);
                symbol->alias_to = f2c_strdup(unit->symbols[root_index].name);
                if (checked_signed_subtract(offsets[symbol_index], offsets[root_index],
                                            &alias_offset))
                    symbol->alias_offset = alias_offset;
                else
                    f2c_diagnostic_span_code(
                        context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, &symbol->declaration_span, 1,
                        "EQUIVALENCE alias offset exceeds the supported size range");
                if (symbol->alias_to == NULL)
                    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                             &symbol->declaration_span, 1,
                                             "out of memory recording EQUIVALENCE root");
            }
            assign_equivalence_c_name(context, unit, component_index, symbol_index);
        }
        ++component_index;
    }

cleanup:
    free(involved);
    free(known);
    free(assigned);
    free(conflict_reported);
    free(offsets);
}

void f2c_validate_project_storage(Context *context) {
    size_t u;
    if (context == NULL)
        return;
    for (u = 0U; u < context->modules.count; ++u)
        validate_module_specification_unit(context, &context->modules.items[u]);
    for (u = 0U; u < context->units.count; ++u)
        validate_block_data_unit(context, &context->units.items[u]);
    for (u = 0U; u < context->units.count; ++u) {
        Unit *unit = &context->units.items[u];
        size_t s;
        for (s = 0U; s < unit->symbol_count; ++s) {
            Symbol *symbol = &unit->symbols[s];
            uint64_t extent;
            uint64_t canonical_extent;
            Unit *canonical_unit;
            validate_common_initialization_owner(context, u, s, unit, symbol);
            if (symbol->common_block == NULL)
                continue;
            if (block_seen_earlier_in_unit(unit, s, symbol->common_block))
                continue;
            if (!common_block_layout(context, unit, symbol->common_block, &extent))
                continue;
            if (!extend_common_with_equivalence(context, unit, u, symbol->common_block, &extent))
                continue;
            canonical_unit =
                previous_common_owner(context, u, symbol->common_block, &canonical_extent);
            if (symbol->common_block[0] != '\0' && canonical_unit != NULL &&
                canonical_extent != extent) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
                    "COMMON block '%s' has %llu storage bytes but an earlier program unit has "
                    "%llu",
                    display_block_name(symbol->common_block), (unsigned long long)extent,
                    (unsigned long long)canonical_extent);
            }
        }
        for (s = 0U; s < unit->symbol_count; ++s) {
            Symbol *symbol = &unit->symbols[s];
            if (symbol->common_block != NULL && symbol->equivalence_common_block == NULL)
                assign_common_c_name(context, u, symbol);
        }
    }
}
