#include "semantic/semantic.h"

#include <stdint.h>
#include <string.h>

static const char *display_block_name(const char *block) {
    return block != NULL && block[0] != '\0' ? block : "<blank>";
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

static const Symbol *find_previous_member(const Context *context, size_t unit_index,
                                          size_t symbol_index, const char *block,
                                          size_t common_index, Unit **owner) {
    size_t u;
    for (u = 0U; u <= unit_index; ++u) {
        Unit *unit = &context->units.items[u];
        const size_t limit = u == unit_index ? symbol_index : unit->symbol_count;
        size_t s;
        for (s = 0U; s < limit; ++s) {
            const Symbol *candidate = &unit->symbols[s];
            if (candidate->common_block != NULL && candidate->common_index == common_index &&
                strcmp(candidate->common_block, block) == 0) {
                *owner = unit;
                return candidate;
            }
        }
    }
    return NULL;
}

static int derived_storage_compatible(const Symbol *left, const Symbol *right) {
    if (left->type != TYPE_DERIVED)
        return 1;
    if (left->derived_type == right->derived_type)
        return 1;
    return left->c_type != NULL && right->c_type != NULL &&
           strcmp(left->c_type, right->c_type) == 0;
}

static void validate_member_contract(Context *context, Unit *unit, const Symbol *symbol) {
    if (symbol->argument || symbol->parameter || symbol->external || symbol->allocatable ||
        symbol->pointer || symbol->automatic_character || symbol->deferred_character) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
            "COMMON entity '%s' must have static non-dummy, non-parameter storage", symbol->name);
    }
    if (symbol->rank != 0U) {
        uint64_t count;
        if (!common_element_count(unit, symbol, &count))
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
                                     "COMMON array '%s' requires constant explicit shape",
                                     symbol->name);
    }
    if (symbol->type == TYPE_CHARACTER) {
        int64_t length;
        if (!common_character_length(unit, symbol, &length))
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
                                     "COMMON CHARACTER entity '%s' requires constant length",
                                     symbol->name);
    }
}

static void validate_member_match(Context *context, Unit *unit, const Symbol *symbol,
                                  Unit *canonical_unit, const Symbol *canonical) {
    uint64_t count;
    uint64_t canonical_count;
    int64_t length;
    int64_t canonical_length;
    const int shape_known = common_element_count(unit, symbol, &count);
    const int canonical_shape_known =
        common_element_count(canonical_unit, canonical, &canonical_count);
    const int length_known = common_character_length(unit, symbol, &length);
    const int canonical_length_known =
        common_character_length(canonical_unit, canonical, &canonical_length);
    if (symbol->type == canonical->type && symbol->kind == canonical->kind &&
        symbol->rank == canonical->rank && shape_known && canonical_shape_known &&
        count == canonical_count && length_known && canonical_length_known &&
        length == canonical_length && derived_storage_compatible(symbol, canonical))
        return;
    f2c_diagnostic_span_code(
        context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->common_span, 1,
        "COMMON block '%s' member %zu ('%s') is storage-incompatible with '%s' in another "
        "program unit",
        display_block_name(symbol->common_block), symbol->common_index + 1U, symbol->name,
        canonical->name);
}

void f2c_validate_common_storage(Context *context) {
    size_t u;
    if (context == NULL)
        return;
    for (u = 0U; u < context->units.count; ++u) {
        Unit *unit = &context->units.items[u];
        size_t s;
        for (s = 0U; s < unit->symbol_count; ++s) {
            const Symbol *symbol = &unit->symbols[s];
            const Symbol *canonical;
            Unit *canonical_unit = NULL;
            if (symbol->common_block == NULL)
                continue;
            validate_member_contract(context, unit, symbol);
            canonical = find_previous_member(context, u, s, symbol->common_block,
                                             symbol->common_index, &canonical_unit);
            if (canonical != NULL)
                validate_member_match(context, unit, symbol, canonical_unit, canonical);
        }
    }
}
