#include "semantic/semantic.h"

#include <stdint.h>
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
            const Symbol *symbol = &unit->symbols[s];
            const Symbol *canonical;
            Unit *canonical_unit = NULL;
            if (symbol->common_block == NULL)
                continue;
            validate_common_initialization_owner(context, u, s, unit, symbol);
            validate_member_contract(context, unit, symbol);
            canonical = find_previous_member(context, u, s, symbol->common_block,
                                             symbol->common_index, &canonical_unit);
            if (canonical != NULL)
                validate_member_match(context, unit, symbol, canonical_unit, canonical);
        }
    }
}
