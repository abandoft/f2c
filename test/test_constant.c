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

static int evaluate_character_source(Unit *unit, const char *source, char **value, size_t *length) {
    F2cExpr *expression = parse_canonical_expression(unit, source);
    const int result = f2c_evaluate_character_constant(unit, expression, value, length);
    f2c_expr_free(expression);
    return result;
}

static void test_character_intrinsics(Unit *unit) {
    char *characters = NULL;
    size_t length = 0U;
    int64_t value = 0;
    expect(evaluate_source(unit, "ichar('X',kind=8)", &value) && value == 88,
           "ICHAR folds with a keyword result kind");
    expect(evaluate_source(unit, "iachar(achar(65))", &value) && value == 65,
           "ACHAR and IACHAR fold when nested");
    expect(evaluate_source(unit, "len_trim(adjustl(' A '))", &value) && value == 1,
           "ADJUSTL preserves length and composes with LEN_TRIM");
    expect(evaluate_source(unit, "index('FORTRAN','R',back=.TrUe.)", &value) && value == 5,
           "INDEX honors a case-insensitive logical BACK constant");
    expect(evaluate_source(unit, "index('ABC','')", &value) && value == 1,
           "forward INDEX locates an empty substring at one");
    expect(evaluate_source(unit, "index('ABC','',back=.true.)", &value) && value == 4,
           "backward INDEX locates an empty substring after the string");
    expect(evaluate_source(unit, "scan('FORTRAN','TR')", &value) && value == 3,
           "SCAN folds the first set member");
    expect(evaluate_source(unit, "scan('FORTRAN','TR',back=.true.)", &value) && value == 5,
           "backward SCAN folds the last set member");
    expect(evaluate_source(unit, "verify('ABBA','A')", &value) && value == 2,
           "VERIFY folds the first nonmember");
    expect(evaluate_source(unit, "verify('ABBA','A',back=.true.)", &value) && value == 3,
           "backward VERIFY folds the last nonmember");
    expect(evaluate_source(unit, "len(repeat('xy',3),kind=2)", &value) && value == 6,
           "REPEAT length folds through LEN with an explicit result kind");
    expect(evaluate_character_source(unit, "trim(adjustl('  AB  '))//repeat('x',2)", &characters,
                                     &length) &&
               length == 4U && memcmp(characters, "ABxx", 4U) == 0,
           "character transformations and concatenation fold as bytes");
    free(characters);
    characters = NULL;
    expect(evaluate_character_source(unit, "char(0)//'A'", &characters, &length) && length == 2U &&
               characters[0] == '\0' && characters[1] == 'A',
           "character constants preserve embedded zero bytes");
    free(characters);
    expect(!evaluate_character_source(unit, "repeat('x',-1)", &characters, &length),
           "negative REPEAT counts are not folded");
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
    expect(
        evaluate_source(&unit, "bit_size(0_1)+bit_size(0_2)+bit_size(0_4)+bit_size(0_8)", &value) &&
            value == 120,
        "BIT_SIZE is folded from the target INTEGER kind");
    expect(evaluate_source(&unit, "iand(-1_1,85_1)", &value) && value == 85,
           "IAND is folded in the target kind bit model");
    expect(evaluate_source(&unit, "ior(64_1,3_1)", &value) && value == 67,
           "IOR is folded in the target kind bit model");
    expect(evaluate_source(&unit, "ieor(85_1,15_1)", &value) && value == 90,
           "IEOR is folded in the target kind bit model");
    expect(evaluate_source(&unit, "not(0_1)", &value) && value == -1,
           "NOT preserves the signed target-kind bit pattern");
    expect(evaluate_source(&unit, "ibset(0_1,7)", &value) && value == -128,
           "IBSET folds the sign bit without host signed shifts");
    expect(evaluate_source(&unit, "ibset(0_8,63)", &value) && value == INT64_MIN,
           "IBSET folds the 64-bit sign position without signed overflow");
    expect(evaluate_source(&unit, "ibclr(-1_1,7)", &value) && value == 127,
           "IBCLR folds the sign bit without host signed masks");
    expect(evaluate_source(&unit, "ibits(len=4,i=-1_1,pos=4)", &value) && value == 15,
           "IBITS supports canonical keyword binding and slices");
    expect(evaluate_source(&unit, "ishft(-1_1,1)", &value) && value == -2,
           "ISHFT left shifts the finite target bit sequence");
    expect(evaluate_source(&unit, "ishft(-1_1,-8)", &value) && value == 0,
           "ISHFT handles a full-width logical right shift");
    expect(evaluate_source(&unit, "ishft(-1_8,-64)", &value) && value == 0,
           "ISHFT handles a full 64-bit logical right shift");
    expect(evaluate_source(&unit, "ishftc(1_1,-1)", &value) && value == -128,
           "ISHFTC rotates a full-width target bit sequence");
    expect(evaluate_source(&unit, "ishftc(1_8,-1)", &value) && value == INT64_MIN,
           "ISHFTC rotates into the 64-bit sign position without overflow");
    expect(evaluate_source(&unit, "ishftc(-16_1,1,4)", &value) && value == -16,
           "ISHFTC preserves bits outside the selected field");
    expect(evaluate_source(&unit, "btest(128_2,7)", &value) && value == 1,
           "BTEST folds to a logical truth value");
    expect(!evaluate_source(&unit, "ibits(0_1,7,2)", &value),
           "invalid IBITS ranges are not folded");
    expect(!evaluate_source(&unit, "ishftc(1_1,2,1)", &value),
           "invalid ISHFTC rotations are not folded");
    expect(!evaluate_source(&unit, "9223372036854775807+1", &value),
           "constant addition overflow is rejected");
    expect(!evaluate_source(&unit, "1/0", &value), "constant division by zero is rejected");
    expect(!evaluate_source(&unit, "cycle_a", &value),
           "cyclic parameter references terminate with failure");
    test_character_intrinsics(&unit);

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
