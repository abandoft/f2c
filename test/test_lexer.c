#include "internal/f2c.h"

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

static void test_complete_statement_tokens(void) {
    static const char source[] = "integer(kind=8), allocatable :: values(:)";
    static const F2cTokenKind expected[] = {
        F2C_TOKEN_IDENTIFIER,   F2C_TOKEN_LEFT_PAREN,  F2C_TOKEN_IDENTIFIER, F2C_TOKEN_OPERATOR,
        F2C_TOKEN_NUMBER,       F2C_TOKEN_RIGHT_PAREN, F2C_TOKEN_COMMA,      F2C_TOKEN_IDENTIFIER,
        F2C_TOKEN_DOUBLE_COLON, F2C_TOKEN_IDENTIFIER,  F2C_TOKEN_LEFT_PAREN, F2C_TOKEN_COLON,
        F2C_TOKEN_RIGHT_PAREN,
    };
    F2cLexer lexer;
    size_t index;
    f2c_lexer_init(&lexer, source, 41U, 3U);
    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        f2c_lexer_next(&lexer);
        expect(lexer.token.kind == expected[index],
               "the full-source lexer emits the expected declaration token");
        expect(lexer.token.line == 41U, "tokens retain their physical source line");
    }
    f2c_lexer_next(&lexer);
    expect(lexer.token.kind == F2C_TOKEN_END, "the declaration token stream terminates exactly");
    expect(lexer.error_at == NULL, "a valid declaration has no lexical error");
    expect(lexer.token.column == strlen(source) + 3U,
           "end tokens retain an exact one-based source column");
}

static void test_legacy_and_literal_boundaries(void) {
    static const char source[] = "print *, 'a'';b'; value = 5HHELLO, mask = z'00ff'";
    F2cLexer lexer;
    int saw_string = 0;
    int saw_separator = 0;
    int saw_hollerith = 0;
    int saw_boz = 0;
    f2c_lexer_init(&lexer, source, 7U, 1U);
    do {
        f2c_lexer_next(&lexer);
        saw_string |=
            lexer.token.kind == F2C_TOKEN_STRING && f2c_token_equals(&lexer.token, "'a'';b'");
        saw_separator |= lexer.token.kind == F2C_TOKEN_SEMICOLON;
        saw_hollerith |=
            lexer.token.kind == F2C_TOKEN_HOLLERITH && f2c_token_equals(&lexer.token, "5HHELLO");
        saw_boz |= lexer.token.kind == F2C_TOKEN_BOZ && f2c_token_equals(&lexer.token, "z'00ff'");
    } while (lexer.token.kind != F2C_TOKEN_END && lexer.token.kind != F2C_TOKEN_INVALID);
    expect(saw_string, "doubled quotes and embedded semicolons stay inside one token");
    expect(saw_separator, "a real statement separator has a dedicated token");
    expect(saw_hollerith, "legacy Hollerith constants have an exact lexical boundary");
    expect(saw_boz, "binary/octal/hex literal forms have a dedicated token");
    expect(lexer.error_at == NULL, "legacy and modern literal tokens coexist without ambiguity");
}

static void test_shared_argument_and_expression_lexing(void) {
    static const char arguments[] = "(first, call(1, 'x,y'), [2, 3], , last)";
    Unit unit;
    char **parts;
    size_t count = 0U;
    F2cExpr *hollerith;
    F2cExpr *boz;
    char *hollerith_c;
    char *boz_c;
    int supported = 0;
    memset(&unit, 0, sizeof(unit));
    parts = f2c_split_actual_arguments(arguments, &count);
    expect(count == 5U, "actual-argument splitting shares balanced lexical tokens");
    expect(parts != NULL && strcmp(parts[1], "call(1, 'x,y')") == 0,
           "nested commas and string commas never split an argument");
    expect(parts != NULL && strcmp(parts[2], "[2, 3]") == 0,
           "array-constructor commas never split an argument");
    expect(parts != NULL && parts[3][0] == '\0', "omitted actual arguments remain explicit");
    while (count != 0U)
        free(parts[--count]);
    free(parts);

    hollerith = f2c_parse_expression_ast(&unit, "5HHELLO", NULL);
    hollerith_c = f2c_emit_expression_ast(&unit, hollerith, &supported);
    expect(supported && hollerith_c != NULL && strcmp(hollerith_c, "\"HELLO\"") == 0,
           "the expression parser consumes Hollerith tokens from the shared lexer");
    free(hollerith_c);
    f2c_expr_free(hollerith);

    boz = f2c_parse_expression_ast(&unit, "z'00ff'", NULL);
    boz_c = f2c_emit_expression_ast(&unit, boz, &supported);
    expect(supported && boz_c != NULL && strstr(boz_c, "0x000000FF") != NULL,
           "BOZ tokens lower to an explicit fixed-width C17 bit pattern");
    free(boz_c);
    f2c_expr_free(boz);
}

int main(void) {
    test_complete_statement_tokens();
    test_legacy_and_literal_boundaries();
    test_shared_argument_and_expression_lexing();
    if (failures != 0) {
        fprintf(stderr, "%d lexer test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all lexer tests passed");
    return EXIT_SUCCESS;
}
