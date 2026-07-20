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
        if (!symbol_has_storage_initializer(symbol) || symbol->parameter)
            continue;
        if (symbol->common_block == NULL || symbol->common_block[0] == '\0') {
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
    size_t previous_unit;
    if (!symbol_has_storage_initializer(symbol))
        return;
    if (unit->kind != UNIT_BLOCK_DATA) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC,
            symbol->declaration_span.begin.line != 0U ? &symbol->declaration_span
                                                      : &symbol->common_span,
            1, "COMMON entity '%s' may be initialized only in BLOCK DATA", symbol->name);
        return;
    }
    if (symbol->common_block[0] == '\0') {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
                                 "blank COMMON entity '%s' cannot be initialized", symbol->name);
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
            if (candidate->common_block != NULL && symbol_has_storage_initializer(candidate) &&
                strcmp(candidate->common_block, symbol->common_block) == 0) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
                    "COMMON block '%s' is initialized by more than one BLOCK DATA program unit",
                    display_block_name(symbol->common_block));
                return;
            }
        }
    }
    (void)symbol_index;
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
        if (symbol->dimensions[dimension].kind != F2C_DIMENSION_EXPLICIT ||
            symbol->dimensions[dimension].lower_expression == NULL ||
            symbol->dimensions[dimension].upper_expression == NULL ||
            !f2c_evaluate_integer_constant(unit, symbol->dimensions[dimension].lower_expression,
                                           &lower) ||
            !f2c_evaluate_integer_constant(unit, symbol->dimensions[dimension].upper_expression,
                                           &upper))
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
    if (symbol->type != TYPE_CHARACTER) {
        *length = 0;
        return 1;
    }
    return symbol->character_length_expression != NULL &&
           f2c_evaluate_integer_constant(unit, symbol->character_length_expression, length) &&
           *length >= 0;
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
        uint64_t candidate;
        if (symbol->common_block == NULL || strcmp(symbol->common_block, block) != 0)
            continue;
        if (symbol->common_size > UINT64_MAX - symbol->common_offset)
            return 0;
        candidate = symbol->common_offset + symbol->common_size;
        if (candidate > end)
            end = candidate;
        if (symbol->common_alignment > maximum_alignment)
            maximum_alignment = symbol->common_alignment;
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
            if (symbol->common_block == NULL)
                continue;
            validate_common_initialization_owner(context, u, s, unit, symbol);
            if (block_seen_earlier_in_unit(unit, s, symbol->common_block))
                continue;
            if (!common_block_layout(context, unit, symbol->common_block, &extent))
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
            if (symbol->common_block != NULL)
                assign_common_c_name(context, u, symbol);
        }
    }
}
