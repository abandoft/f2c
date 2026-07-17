#include "internal/f2c.h"

#include <stdlib.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static int has_prior_case(const F2cStatement *statement) {
    const F2cStatement *select = statement->construct_owner;
    const F2cStatement *candidate;
    if (select == NULL || select->kind != F2C_STMT_SELECT_CASE)
        return 0;
    for (candidate = select + 1; candidate < statement; ++candidate) {
        if (candidate->kind == F2C_STMT_CASE && candidate->construct_owner == select)
            return 1;
    }
    return 0;
}

static size_t select_identifier(const Unit *unit, const F2cStatement *select) {
    return unit != NULL && select != NULL && select >= unit->statements &&
                   select < unit->statements + unit->statement_count
               ? (size_t)(select - unit->statements)
               : 0U;
}

static void emit_character_comparison(Buffer *output, Unit *unit, const F2cStatement *select,
                                      const F2cExpr *endpoint, const char *operator_text) {
    const size_t identifier = select_identifier(unit, select);
    char *value = f2c_emit_typed_expression(unit, endpoint);
    char *pointer = f2c_character_source_pointer(unit, endpoint, value);
    char *length = f2c_character_length_expression(unit, endpoint);
    f2c_buffer_printf(output,
                      "f2c_character_compare(f2c_select_case_pointer_%zu, "
                      "f2c_select_case_length_%zu, %s, (size_t)(%s)) %s 0",
                      identifier, identifier, pointer != NULL ? pointer : "NULL",
                      length != NULL ? length : "0U", operator_text);
    free(value);
    free(pointer);
    free(length);
}

static void emit_comparison(Buffer *output, Unit *unit, const F2cStatement *select,
                            const F2cExpr *endpoint, const char *operator_text) {
    const size_t identifier = select_identifier(unit, select);
    char *value;
    if (select->expression->type == TYPE_CHARACTER) {
        emit_character_comparison(output, unit, select, endpoint, operator_text);
        return;
    }
    value = f2c_emit_typed_expression(unit, endpoint);
    f2c_buffer_printf(output, "f2c_select_case_value_%zu %s (%s)", identifier, operator_text,
                      value != NULL ? value : "0");
    free(value);
}

static void emit_range_condition(Buffer *output, Unit *unit, const F2cStatement *select,
                                 const F2cCaseRange *range) {
    if (!range->has_colon) {
        emit_comparison(output, unit, select, range->lower, "==");
        return;
    }
    f2c_buffer_append(output, "(");
    if (range->lower != NULL)
        emit_comparison(output, unit, select, range->lower, ">=");
    if (range->lower != NULL && range->upper != NULL)
        f2c_buffer_append(output, " && ");
    if (range->upper != NULL)
        emit_comparison(output, unit, select, range->upper, "<=");
    f2c_buffer_append(output, ")");
}

static void emit_case_condition(Buffer *output, Unit *unit, const F2cStatement *select,
                                const F2cStatement *case_statement) {
    size_t i;
    f2c_buffer_append(output, "(");
    for (i = 0U; i < case_statement->case_range_count; ++i) {
        if (i != 0U)
            f2c_buffer_append(output, " || ");
        emit_range_condition(output, unit, select, &case_statement->case_ranges[i]);
    }
    f2c_buffer_append(output, ")");
}

static void emit_default_condition(Buffer *output, Unit *unit, const F2cStatement *select) {
    const F2cStatement *candidate;
    int emitted = 0;
    f2c_buffer_append(output, "!(");
    for (candidate = select + 1; candidate < unit->statements + unit->statement_count;
         ++candidate) {
        if (candidate->kind == F2C_STMT_CASE && candidate->construct_owner == select &&
            !candidate->case_default) {
            if (emitted)
                f2c_buffer_append(output, " || ");
            emit_case_condition(output, unit, select, candidate);
            emitted = 1;
        }
    }
    if (!emitted)
        f2c_buffer_append(output, "0");
    f2c_buffer_append(output, ")");
}

int f2c_emit_select_case_begin(Context *context, Unit *unit, const F2cStatement *statement,
                               int *depth) {
    const size_t identifier = select_identifier(unit, statement);
    char *selector = f2c_emit_typed_expression(unit, statement->expression);
    if (selector == NULL)
        return 0;
    indent(&context->output, *depth);
    f2c_buffer_append(&context->output, "{\n");
    ++*depth;
    indent(&context->output, *depth);
    if (statement->expression->type == TYPE_CHARACTER) {
        char *pointer = f2c_character_source_pointer(unit, statement->expression, selector);
        char *length = f2c_character_length_expression(unit, statement->expression);
        if (pointer == NULL || length == NULL) {
            free(selector);
            free(pointer);
            free(length);
            return 0;
        }
        f2c_buffer_printf(&context->output, "const char *const f2c_select_case_pointer_%zu = %s;\n",
                          identifier, pointer);
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_select_case_length_%zu = (size_t)(%s);\n", identifier,
                          length);
        free(pointer);
        free(length);
    } else {
        f2c_buffer_printf(&context->output, "const %s f2c_select_case_value_%zu = (%s);\n",
                          f2c_expression_c_type(statement->expression), identifier, selector);
    }
    free(selector);
    return 1;
}

int f2c_emit_case_begin(Context *context, Unit *unit, const F2cStatement *statement, int *depth) {
    const F2cStatement *select = statement->construct_owner;
    if (select == NULL || select->kind != F2C_STMT_SELECT_CASE)
        return 0;
    if (has_prior_case(statement)) {
        if (*depth > 1)
            --*depth;
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "} else ");
    } else {
        indent(&context->output, *depth);
    }
    f2c_buffer_append(&context->output, "if (");
    if (statement->case_default)
        emit_default_condition(&context->output, unit, select);
    else
        emit_case_condition(&context->output, unit, select, statement);
    f2c_buffer_append(&context->output, ") {\n");
    ++*depth;
    return 1;
}

int f2c_emit_select_case_end(Context *context, const F2cStatement *statement, int *depth) {
    const F2cStatement *select = statement->construct_owner;
    if (select == NULL || select->kind != F2C_STMT_SELECT_CASE)
        return 0;
    if (has_prior_case(statement)) {
        if (*depth > 1)
            --*depth;
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    if (*depth > 1)
        --*depth;
    indent(&context->output, *depth);
    f2c_buffer_append(&context->output, "}\n");
    return 1;
}
