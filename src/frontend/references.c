#include "frontend/private.h"

#include <stdlib.h>
#include <string.h>

static size_t statement_start(const Line *line) {
    if (line != NULL && line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER)
        return 1U;
    return 0U;
}

static int left_delimiter(F2cTokenKind kind) {
    return kind == F2C_TOKEN_LEFT_PAREN || kind == F2C_TOKEN_LEFT_BRACKET ||
           kind == F2C_TOKEN_ARRAY_BEGIN;
}

static size_t top_level_assignment(const Line *line, size_t start) {
    size_t index;
    if (line == NULL)
        return SIZE_MAX;
    for (index = start; index < line->token_count; ++index) {
        size_t close;
        if (left_delimiter(line->tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close))
                return SIZE_MAX;
            index = close;
        } else if (line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
                   f2c_token_equals(&line->tokens[index], "=")) {
            return index;
        }
    }
    return SIZE_MAX;
}

static int leading_designator_assignment(const Line *line, size_t name_index, size_t open_index) {
    size_t close;
    const size_t assignment = top_level_assignment(line, statement_start(line));
    return name_index == statement_start(line) && assignment != SIZE_MAX &&
           f2c_token_matching_delimiter(line->tokens, line->token_count, open_index, &close) &&
           close < assignment;
}

static Symbol *symbol_for_token(Unit *unit, const F2cToken *token) {
    char *name;
    Symbol *symbol;
    if (unit == NULL || token == NULL || token->kind != F2C_TOKEN_IDENTIFIER)
        return NULL;
    name = f2c_token_text(token);
    symbol = name != NULL ? f2c_find_symbol(unit, name) : NULL;
    free(name);
    return symbol;
}

void f2c_mark_call_targets(Unit *unit, const Line *line) {
    const size_t start = statement_start(line);
    size_t call_index;
    if (unit == NULL || line == NULL)
        return;
    for (call_index = start; call_index + 1U < line->token_count; ++call_index) {
        const size_t name_index = call_index + 1U;
        char *name;
        Symbol *symbol;
        if (!f2c_line_token_equals(line, call_index, "call") ||
            (call_index > start && line->tokens[call_index - 1U].kind == F2C_TOKEN_PERCENT) ||
            line->tokens[name_index].kind != F2C_TOKEN_IDENTIFIER ||
            (name_index + 1U < line->token_count &&
             line->tokens[name_index + 1U].kind == F2C_TOKEN_PERCENT))
            continue;
        name = f2c_token_text(&line->tokens[name_index]);
        if (name == NULL)
            return;
        if (!f2c_is_intrinsic_name(name)) {
            symbol = f2c_ensure_symbol_impl(unit, name);
            if (symbol != NULL) {
                symbol->external = 1;
                symbol->external_subroutine = 1;
            }
        }
        free(name);
    }
}

void f2c_mark_function_references(Unit *unit, const Line *line) {
    const size_t start = statement_start(line);
    size_t index;
    if (unit == NULL || line == NULL || f2c_declaration_tokens(line) ||
        f2c_line_token_equals(line, start, "use"))
        return;
    for (index = start; index + 1U < line->token_count; ++index) {
        Symbol *symbol;
        if (line->tokens[index].kind != F2C_TOKEN_IDENTIFIER ||
            line->tokens[index + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
            (index > start && line->tokens[index - 1U].kind == F2C_TOKEN_PERCENT) ||
            (index > start && f2c_line_token_equals(line, index - 1U, "call")) ||
            leading_designator_assignment(line, index, index + 1U))
            continue;
        symbol = symbol_for_token(unit, &line->tokens[index]);
        if (symbol == NULL || symbol->argument || symbol->parameter || symbol->rank != 0U ||
            symbol->type == TYPE_CHARACTER || symbol->statement_function ||
            f2c_token_equals(&line->tokens[index], "if") ||
            f2c_is_intrinsic_name(symbol->name))
            continue;
        symbol->external = 1;
        symbol->external_subroutine = 0;
    }
}

static void clear_statement_function(Symbol *function) {
    size_t index;
    for (index = 0U; index < function->statement_function_argument_count; ++index)
        free(function->statement_function_arguments[index]);
    free(function->statement_function_arguments);
    function->statement_function_arguments = NULL;
    function->statement_function_argument_count = 0U;
    free(function->statement_function_text);
    function->statement_function_text = NULL;
    f2c_expr_free(function->statement_function_expression);
    function->statement_function_expression = NULL;
}

int f2c_mark_statement_function_symbols(Unit *unit, const Line *line) {
    const size_t start = statement_start(line);
    const size_t name_index = start;
    const size_t open_index = start + 1U;
    size_t close;
    size_t index;
    Symbol *function;
    if (unit == NULL || line == NULL || open_index >= line->token_count ||
        line->tokens[name_index].kind != F2C_TOKEN_IDENTIFIER ||
        line->tokens[open_index].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, open_index, &close) ||
        close + 1U >= line->token_count ||
        line->tokens[close + 1U].kind != F2C_TOKEN_OPERATOR ||
        !f2c_token_equals(&line->tokens[close + 1U], "="))
        return 0;
    function = symbol_for_token(unit, &line->tokens[name_index]);
    if (function == NULL || function->rank != 0U)
        return 0;
    {
        int expect_identifier = 1;
        for (index = open_index + 1U; index < close; ++index) {
            if ((expect_identifier && line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) ||
                (!expect_identifier && line->tokens[index].kind != F2C_TOKEN_COMMA))
                return 0;
            expect_identifier = !expect_identifier;
        }
        if (close != open_index + 1U && expect_identifier)
            return 0;
    }
    if (function->statement_function) {
        if (unit->context != NULL)
            f2c_diagnostic_token_code(unit->context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                      &line->tokens[name_index], 1,
                                   "duplicate statement-function definition '%s'",
                                   function->name);
        return 1;
    }
    clear_statement_function(function);
    function->statement_function = 1;
    function->external = 0;
    function->external_subroutine = 0;
    function->statement_function_line = line->number;
    for (index = open_index + 1U; index < close; ++index) {
        Symbol *argument;
        char **replacement;
        char *name;
        if (line->tokens[index].kind == F2C_TOKEN_COMMA)
            continue;
        if (line->tokens[index].kind != F2C_TOKEN_IDENTIFIER ||
            (index != open_index + 1U && line->tokens[index - 1U].kind != F2C_TOKEN_COMMA) ||
            (index + 1U != close && line->tokens[index + 1U].kind != F2C_TOKEN_COMMA)) {
            if (unit->context != NULL)
                f2c_diagnostic_token_code(unit->context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[index], 1,
                                       "malformed statement-function dummy argument list");
            clear_statement_function(function);
            function->statement_function = 0;
            return 1;
        }
        name = f2c_token_text(&line->tokens[index]);
        if (name != NULL) {
            size_t previous;
            for (previous = 0U; previous < function->statement_function_argument_count;
                 ++previous) {
                if (strcmp(name, function->statement_function_arguments[previous]) == 0) {
                    if (unit->context != NULL)
                        f2c_diagnostic_token_code(
                            unit->context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[index], 1,
                            "duplicate dummy argument '%s' in statement function '%s'", name,
                            function->name);
                    free(name);
                    clear_statement_function(function);
                    function->statement_function = 0;
                    return 1;
                }
            }
        }
        replacement =
            function->statement_function_argument_count < SIZE_MAX / sizeof(*replacement)
                ? (char **)realloc(function->statement_function_arguments,
                                   (function->statement_function_argument_count + 1U) *
                                       sizeof(*replacement))
                : NULL;
        if (name == NULL || replacement == NULL) {
            free(name);
            if (unit->context != NULL)
                f2c_diagnostic_code(unit->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                                    "out of memory recording statement function '%s'",
                                    function->name);
            clear_statement_function(function);
            function->statement_function = 0;
            return 1;
        }
        function->statement_function_arguments = replacement;
        function->statement_function_arguments[function->statement_function_argument_count++] =
            name;
        argument = symbol_for_token(unit, &line->tokens[index]);
        if (argument != NULL)
            argument->statement_dummy = 1;
    }
    if (close + 2U >= line->token_count) {
        if (unit->context != NULL)
            f2c_diagnostic_token_code(unit->context, F2C_DIAGNOSTIC_SYNTAX, line,
                                      &line->tokens[close + 1U], 1,
                                   "statement function '%s' requires a result expression",
                                   function->name);
        clear_statement_function(function);
        function->statement_function = 0;
        return 1;
    }
    function->statement_function_text = f2c_token_range_text(
        f2c_line_token_range(line, close + 2U, line->token_count));
    if (function->statement_function_text == NULL && unit->context != NULL) {
        f2c_diagnostic_code(unit->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                            "out of memory recording statement function '%s'", function->name);
        clear_statement_function(function);
        function->statement_function = 0;
    }
    return 1;
}

static size_t actual_argument_count(const Line *line, size_t begin, size_t end) {
    size_t count = begin == end ? 0U : 1U;
    size_t index;
    for (index = begin; index < end; ++index) {
        size_t close;
        if (left_delimiter(line->tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(line->tokens, end, index, &close))
                return 0U;
            index = close;
        } else if (line->tokens[index].kind == F2C_TOKEN_COMMA) {
            ++count;
        }
    }
    return count;
}

static const F2cExpr *actual_value(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   expression->child_count == 1U
               ? expression->children[0]
               : expression;
}

static void record_actual_parameter(Symbol *external, size_t parameter,
                                    const F2cExpr *expression) {
    const F2cExpr *value = actual_value(expression);
    Symbol *actual = value != NULL ? value->symbol : NULL;
    Type type = value != NULL ? value->type : TYPE_UNKNOWN;
    if (type == TYPE_UNKNOWN && actual != NULL)
        type = actual->type;
    if (type == TYPE_UNKNOWN)
        type = TYPE_DOUBLE;
    external->external_parameter_types[parameter] = type;
    external->external_parameter_kinds[parameter] =
        value != NULL && value->type_kind != 0 ? value->type_kind : f2c_default_kind(type);
    external->external_parameter_ranks[parameter] = value != NULL ? value->rank : 0U;
    if (actual != NULL && actual->external && actual->external_declared &&
        value->kind == F2C_EXPR_NAME)
        external->external_parameter_procedures[parameter] = actual;
}

static int record_external_signature(Unit *unit, Symbol *external, const Line *line,
                                     size_t open, size_t close) {
    const size_t count = actual_argument_count(line, open + 1U, close);
    size_t parameter = 0U;
    size_t begin = open + 1U;
    size_t index;
    if (!f2c_symbol_resize_external_parameters(external, count)) {
        if (unit->context != NULL)
            f2c_diagnostic_code(unit->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                                "out of memory while inferring the interface of '%s'",
                                external->name);
        return 0;
    }
    for (index = begin; index <= close && parameter < count; ++index) {
        size_t nested_close;
        F2cExpr *expression;
        if (index < close && left_delimiter(line->tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(line->tokens, close, index, &nested_close))
                return 0;
            index = nested_close;
            continue;
        }
        if (index < close && line->tokens[index].kind != F2C_TOKEN_COMMA)
            continue;
        expression = f2c_parse_expression_tokens(unit, &line->tokens[begin], index - begin,
                                                 line->text, NULL);
        record_actual_parameter(external, parameter++, expression);
        f2c_expr_free(expression);
        begin = index + 1U;
    }
    external->external_parameter_count = count;
    external->external_signature_observed = 1;
    return 1;
}

void f2c_infer_external_signature(Unit *unit, Symbol *external, const Line *line) {
    const size_t start = statement_start(line);
    size_t index;
    if (unit == NULL || external == NULL || line == NULL || external->external_signature_explicit ||
        external->external_signature_observed)
        return;
    for (index = start; index + 1U < line->token_count; ++index) {
        size_t close;
        if (line->tokens[index].kind != F2C_TOKEN_IDENTIFIER ||
            !f2c_token_equals(&line->tokens[index], external->name) ||
            line->tokens[index + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
            (index > start && line->tokens[index - 1U].kind == F2C_TOKEN_PERCENT) ||
            leading_designator_assignment(line, index, index + 1U) ||
            !f2c_token_matching_delimiter(line->tokens, line->token_count, index + 1U, &close))
            continue;
        (void)record_external_signature(unit, external, line, index + 1U, close);
        return;
    }
}
