#include "internal/f2c.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static Symbol *add_parameter(Symbol *symbols, size_t index, const char *name,
                             const char *initializer) {
    Symbol *symbol = &symbols[index];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = (char *)name;
    symbol->c_name = (char *)name;
    symbol->type = TYPE_INTEGER;
    symbol->parameter = 1;
    symbol->initializer = (char *)initializer;
    return symbol;
}

static F2cExpr *parse_canonical_expression(Unit *unit, const char *source) {
    F2cTokenStream stream;
    F2cToken *tokens = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    F2cExpr *expression = NULL;
    const char *error_at = NULL;
    f2c_token_stream_init(&stream, source, 1U, 1U);
    for (;;) {
        F2cToken *replacement;
        const size_t next = capacity == 0U ? 8U : capacity * 2U;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (stream.token.kind == F2C_TOKEN_INVALID)
            goto cleanup;
        if (count == capacity) {
            replacement = (F2cToken *)realloc(tokens, next * sizeof(*tokens));
            if (replacement == NULL)
                goto cleanup;
            tokens = replacement;
            capacity = next;
        }
        tokens[count++] = stream.token;
    }
    expression = f2c_parse_expression_tokens(unit, tokens, count, source, &error_at);
    if (error_at != NULL) {
        f2c_expr_free(expression);
        expression = NULL;
    }

cleanup:
    free(tokens);
    return expression;
}

static int evaluate_source(Unit *unit, const char *source, int64_t *value) {
    F2cExpr *expression = parse_canonical_expression(unit, source);
    const int result = f2c_evaluate_integer_constant(unit, expression, value);
    f2c_expr_free(expression);
    return result;
}

int main(void) {
    Symbol symbols[3];
    Unit unit;
    int64_t value = 0;
    memset(&unit, 0, sizeof(unit));
    add_parameter(symbols, 0U, "base", "4");
    add_parameter(symbols, 1U, "cycle_a", "cycle_b");
    add_parameter(symbols, 2U, "cycle_b", "cycle_a");
    unit.symbols = symbols;
    unit.symbol_count = 3U;
    symbols[0].initializer_expression = parse_canonical_expression(&unit, "4");
    symbols[1].initializer_expression = parse_canonical_expression(&unit, "cycle_b");
    symbols[2].initializer_expression = parse_canonical_expression(&unit, "cycle_a");

    expect(evaluate_source(&unit, "base*2+3**2", &value) && value == 17,
           "parameter arithmetic and integer powers are folded");
    expect(evaluate_source(&unit, "len('A''B')+max(2,5)", &value) && value == 8,
           "character LEN and variadic MAX are folded");
    expect(!evaluate_source(&unit, "9223372036854775807+1", &value),
           "constant addition overflow is rejected");
    expect(!evaluate_source(&unit, "1/0", &value),
           "constant division by zero is rejected");
    expect(!evaluate_source(&unit, "cycle_a", &value),
           "cyclic parameter references terminate with failure");

    f2c_expr_free(symbols[0].initializer_expression);
    f2c_expr_free(symbols[1].initializer_expression);
    f2c_expr_free(symbols[2].initializer_expression);

    if (failures != 0) {
        fprintf(stderr, "%d constant-evaluation test(s) failed\n", failures);
        return 1;
    }
    puts("all constant-evaluation tests passed");
    return 0;
}
