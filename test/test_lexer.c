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

static Line tokenized_line(const char *source) {
    Line line;
    F2cTokenStream stream;
    size_t capacity = 0U;
    memset(&line, 0, sizeof(line));
    line.text = (char *)source;
    line.number = 1U;
    f2c_token_stream_init(&stream, source, 1U, 1U);
    for (;;) {
        F2cToken *replacement;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (line.token_count == capacity) {
            const size_t next = capacity == 0U ? 8U : capacity * 2U;
            replacement = (F2cToken *)realloc(line.tokens, next * sizeof(*replacement));
            if (replacement == NULL) {
                free(line.tokens);
                line.tokens = NULL;
                line.token_count = 0U;
                return line;
            }
            line.tokens = replacement;
            capacity = next;
        }
        line.tokens[line.token_count++] = stream.token;
    }
    return line;
}

static void release_tokenized_line(Line *line) {
    free(line->tokens);
    line->tokens = NULL;
    line->token_count = 0U;
}

static void test_complete_statement_tokens(void) {
    static const char source[] = "integer(kind=8), allocatable :: values(:)";
    static const F2cTokenKind expected[] = {
        F2C_TOKEN_IDENTIFIER,   F2C_TOKEN_LEFT_PAREN,  F2C_TOKEN_IDENTIFIER, F2C_TOKEN_OPERATOR,
        F2C_TOKEN_NUMBER,       F2C_TOKEN_RIGHT_PAREN, F2C_TOKEN_COMMA,      F2C_TOKEN_IDENTIFIER,
        F2C_TOKEN_DOUBLE_COLON, F2C_TOKEN_IDENTIFIER,  F2C_TOKEN_LEFT_PAREN, F2C_TOKEN_COLON,
        F2C_TOKEN_RIGHT_PAREN,
    };
    F2cTokenStream lexer;
    size_t index;
    f2c_token_stream_init(&lexer, source, 41U, 3U);
    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        f2c_token_stream_next(&lexer);
        expect(lexer.token.kind == expected[index],
               "the full-source lexer emits the expected declaration token");
        expect(lexer.token.line == 41U, "tokens retain their physical source line");
    }
    f2c_token_stream_next(&lexer);
    expect(lexer.token.kind == F2C_TOKEN_END, "the declaration token stream terminates exactly");
    expect(lexer.error_at == NULL, "a valid declaration has no lexical error");
    expect(lexer.token.column == strlen(source) + 3U,
           "end tokens retain an exact one-based source column");
}

static void test_legacy_and_literal_boundaries(void) {
    static const char source[] = "print *, 'a'';b'; value = 5HHELLO, mask = z'00ff'";
    F2cTokenStream lexer;
    int saw_string = 0;
    int saw_separator = 0;
    int saw_hollerith = 0;
    int saw_boz = 0;
    f2c_token_stream_init(&lexer, source, 7U, 1U);
    do {
        f2c_token_stream_next(&lexer);
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

static void test_literal_kind_and_boz_validation(void) {
    static const char *const valid_boz[] = {"b'101001'", "o\"707\"", "z'00aF'", "x\"Ff\""};
    static const char *const invalid_boz[] = {"b''", "b'102'", "o'8'", "z'0g'", "x'+'", "z'123"};
    F2cTokenStream lexer;
    Unit unit;
    F2cExpr *literal;
    char *generated;
    size_t index;
    int supported = 0;

    for (index = 0U; index < sizeof(valid_boz) / sizeof(valid_boz[0]); ++index) {
        f2c_token_stream_init(&lexer, valid_boz[index], 1U, 1U);
        f2c_token_stream_next(&lexer);
        expect(lexer.token.kind == F2C_TOKEN_BOZ,
               "each valid BOZ base alphabet produces one canonical token");
        f2c_token_stream_next(&lexer);
        expect(lexer.token.kind == F2C_TOKEN_END && lexer.error_at == NULL,
               "a valid BOZ literal consumes its complete spelling");
    }
    for (index = 0U; index < sizeof(invalid_boz) / sizeof(invalid_boz[0]); ++index) {
        f2c_token_stream_init(&lexer, invalid_boz[index], 1U, 1U);
        f2c_token_stream_next(&lexer);
        expect(lexer.token.kind == F2C_TOKEN_INVALID && lexer.error_at != NULL,
               "invalid BOZ digits are rejected by the canonical lexer");
    }

    memset(&unit, 0, sizeof(unit));
    literal = f2c_parse_expression_ast(&unit, "1_'AbC'", NULL);
    generated = f2c_emit_expression_ast(&unit, literal, &supported);
    expect(literal != NULL && literal->kind == F2C_EXPR_STRING_LITERAL && literal->type_kind == 1,
           "a numeric character kind prefix is retained on the typed expression");
    expect(supported && generated != NULL && strcmp(generated, "\"AbC\"") == 0,
           "a character kind prefix does not leak into or alter the literal payload");
    free(generated);
    f2c_expr_free(literal);

    f2c_token_stream_init(&lexer, "left.eq.right .and. .not.done", 1U, 1U);
    f2c_token_stream_next(&lexer);
    expect(lexer.token.kind == F2C_TOKEN_IDENTIFIER,
           "an identifier adjacent to a dotted operator retains its boundary");
    f2c_token_stream_next(&lexer);
    expect(lexer.token.kind == F2C_TOKEN_OPERATOR && f2c_token_equals(&lexer.token, ".eq."),
           "a no-blank relational dotted operator is recognized exactly");
    f2c_token_stream_next(&lexer);
    expect(lexer.token.kind == F2C_TOKEN_IDENTIFIER,
           "the right operand adjacent to a dotted operator retains its boundary");
    f2c_token_stream_next(&lexer);
    expect(lexer.token.kind == F2C_TOKEN_OPERATOR && f2c_token_equals(&lexer.token, ".and."),
           "a no-blank logical dotted operator is recognized exactly");

    f2c_token_stream_init(&lexer, "42_int64 1.25_real64", 1U, 1U);
    f2c_token_stream_next(&lexer);
    expect(lexer.token.kind == F2C_TOKEN_NUMBER && f2c_token_equals(&lexer.token, "42_int64"),
           "an integer kind suffix remains part of one numeric token");
    f2c_token_stream_next(&lexer);
    expect(lexer.token.kind == F2C_TOKEN_NUMBER && f2c_token_equals(&lexer.token, "1.25_real64"),
           "a real kind suffix remains part of one numeric token");
}

static void test_shared_argument_and_expression_lexing(void) {
    Unit unit;
    F2cExpr *hollerith;
    F2cExpr *boz;
    char *hollerith_c;
    char *boz_c;
    int supported = 0;
    memset(&unit, 0, sizeof(unit));
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

static void test_pretokenized_expression_path(void) {
    static const char source[] = "value + 2 * scale";
    F2cToken tokens[8];
    F2cTokenStream stream;
    Symbol symbols[2];
    Unit unit;
    F2cExpr *expression;
    const char *error_at = NULL;
    size_t count = 0U;
    memset(&unit, 0, sizeof(unit));
    memset(symbols, 0, sizeof(symbols));
    symbols[0].name = "value";
    symbols[0].c_name = "value";
    symbols[0].type = TYPE_INTEGER;
    symbols[0].kind = 4;
    symbols[1].name = "scale";
    symbols[1].c_name = "scale";
    symbols[1].type = TYPE_INTEGER;
    symbols[1].kind = 4;
    unit.symbols = symbols;
    unit.symbol_count = 2U;

    f2c_token_stream_init(&stream, source, 19U, 5U);
    for (;;) {
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        expect(count < sizeof(tokens) / sizeof(tokens[0]),
               "pretokenized expression fits the test token buffer");
        if (count < sizeof(tokens) / sizeof(tokens[0]))
            tokens[count++] = stream.token;
    }
    expression = f2c_parse_expression_tokens(&unit, tokens, count, source, &error_at);
    expect(expression != NULL && error_at == NULL && expression->kind == F2C_EXPR_BINARY,
           "the AST parser consumes the canonical pretokenized stream");
    expect(expression != NULL && expression->span.begin.line == 19U &&
               expression->span.begin.column == 5U && expression->span.end.line == 19U,
           "pretokenized AST nodes retain physical source coordinates");
    f2c_expr_free(expression);
}

static void test_token_cursor_and_ranges(void) {
    static const char source[] = "(alpha + [beta, gamma])";
    F2cToken tokens[16];
    F2cTokenStream stream;
    F2cTokenCursor cursor;
    F2cToken deep[140];
    size_t count = 0U;
    size_t close = 0U;
    size_t index;
    char *text;
    f2c_token_stream_init(&stream, source, 3U, 1U);
    for (;;) {
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (count < sizeof(tokens) / sizeof(tokens[0]))
            tokens[count++] = stream.token;
    }
    f2c_token_cursor_init(&cursor, tokens, count);
    expect(f2c_token_cursor_consume(&cursor, F2C_TOKEN_LEFT_PAREN, NULL),
           "the token cursor consumes an expected delimiter");
    expect(f2c_token_cursor_peek(&cursor, 0U) != NULL &&
               f2c_token_equals(f2c_token_cursor_peek(&cursor, 0U), "alpha"),
           "the token cursor exposes bounded lookahead");
    expect(f2c_token_cursor_take(&cursor) == &tokens[1],
           "taking a token advances the canonical cursor exactly once");
    expect(f2c_token_matching_delimiter(tokens, count, 0U, &close) && close == count - 1U,
           "mixed nested delimiters resolve to the matching closing token");
    expect(f2c_token_range_balanced(tokens, count),
           "a well-formed canonical token range reports balanced delimiters");
    text =
        f2c_token_range_text((F2cTokenRange){source, sizeof(source) - 1U, tokens + 1U, count - 2U});
    expect(text != NULL && strcmp(text, "alpha + [beta, gamma]") == 0,
           "token ranges retain the exact original source spelling");
    free(text);

    for (index = 0U; index < 70U; ++index)
        deep[index].kind = F2C_TOKEN_LEFT_PAREN;
    for (index = 70U; index < 140U; ++index)
        deep[index].kind = F2C_TOKEN_RIGHT_PAREN;
    expect(f2c_token_matching_delimiter(deep, 140U, 0U, &close) && close == 139U,
           "delimiter matching has no hidden fixed nesting limit");
    deep[139].kind = F2C_TOKEN_RIGHT_BRACKET;
    expect(!f2c_token_range_balanced(deep, 140U),
           "mismatched delimiter kinds are rejected deterministically");

    tokens[0].begin = source + sizeof(source) - 1U;
    tokens[0].length = 1U;
    text = f2c_token_range_text((F2cTokenRange){source, sizeof(source) - 1U, tokens, 1U});
    expect(text == NULL, "token ranges cannot read beyond their owning source buffer");
    free(text);

    f2c_token_cursor_init(&cursor, NULL, 1U);
    expect(f2c_token_cursor_peek(&cursor, 0U) == NULL,
           "a token cursor rejects an inconsistent null token buffer");
}

static void test_top_level_token_ranges(void) {
    static const char source[] = "alpha, call(beta, gamma), 'delta,epsilon'";
    Line line = tokenized_line(source);
    F2cTokenRange *items = NULL;
    size_t count = 0U;
    size_t comma;
    char *middle;
    F2cTokenRange range = {source, sizeof(source) - 1U, line.tokens, line.token_count};
    expect(f2c_token_range_split_top_level(range, F2C_TOKEN_COMMA, NULL, &items, &count) &&
               count == 3U,
           "top-level token splitting ignores nested and quoted commas");
    middle = count == 3U ? f2c_token_range_text(items[1]) : NULL;
    expect(middle != NULL && strcmp(middle, "call(beta, gamma)") == 0,
           "split token ranges retain exact nested source spelling");
    comma = f2c_token_range_find_top_level(range, 0U, F2C_TOKEN_COMMA, NULL);
    expect(comma != SIZE_MAX && comma == items[0].count,
           "top-level token search returns a stable token-relative position");
    free(middle);
    free(items);
    release_tokenized_line(&line);
}

static void test_statement_syntax_predicates(void) {
    Line module = tokenized_line("module numerics");
    Line module_procedure = tokenized_line("module procedure solve");
    Line quoted_end = tokenized_line("print *, 'end module numerics'");
    Line end_if = tokenized_line("end if");
    Line labeled_end = tokenized_line("120 end subroutine solve");
    Line derived = tokenized_line("type, abstract, extends(base) :: child");
    Line declaration = tokenized_line("type(child) :: value");
    Line contains_name = tokenized_line("contains_value = 'contains'");
    Line abstract_interface = tokenized_line("abstract interface");
    F2cModuleHeaderSyntax module_syntax;
    F2cUnitEndSyntax end_syntax;

    expect(f2c_parse_module_header_syntax(&module, &module_syntax) == F2C_MODULE_HEADER_PARSED &&
               f2c_token_equals(module_syntax.name, "numerics"),
           "module headers and names are classified from canonical tokens");
    expect(f2c_parse_module_header_syntax(&module_procedure, &module_syntax) ==
               F2C_MODULE_HEADER_NOT_MATCHED,
           "MODULE PROCEDURE cannot be mistaken for a module definition");
    expect(f2c_parse_unit_end_syntax(&quoted_end, &end_syntax) == F2C_UNIT_END_NOT_MATCHED,
           "keywords inside a character literal cannot terminate a module");
    expect(f2c_parse_unit_end_syntax(&end_if, &end_syntax) == F2C_UNIT_END_NOT_MATCHED,
           "END IF cannot terminate an enclosing procedure");
    expect(f2c_parse_unit_end_syntax(&labeled_end, &end_syntax) == F2C_UNIT_END_PARSED &&
               end_syntax.has_kind && end_syntax.kind == F2C_UNIT_SYNTAX_SUBROUTINE &&
               end_syntax.name != NULL && f2c_token_equals(end_syntax.name, "solve"),
           "labeled procedure terminators retain their unit kind");
    expect(f2c_derived_type_start_tokens(&derived),
           "attributed derived-type definitions are recognized structurally");
    expect(!f2c_derived_type_start_tokens(&declaration),
           "TYPE(name) declarations cannot open a derived-type definition");
    expect(!f2c_contains_tokens(&contains_name),
           "identifiers and strings containing CONTAINS cannot change scope");
    expect(f2c_interface_start_tokens(&abstract_interface) &&
               f2c_abstract_interface_tokens(&abstract_interface),
           "ABSTRACT INTERFACE is represented by one shared syntax predicate");

    release_tokenized_line(&module);
    release_tokenized_line(&module_procedure);
    release_tokenized_line(&quoted_end);
    release_tokenized_line(&end_if);
    release_tokenized_line(&labeled_end);
    release_tokenized_line(&derived);
    release_tokenized_line(&declaration);
    release_tokenized_line(&contains_name);
    release_tokenized_line(&abstract_interface);
}

int main(void) {
    test_complete_statement_tokens();
    test_legacy_and_literal_boundaries();
    test_literal_kind_and_boz_validation();
    test_shared_argument_and_expression_lexing();
    test_pretokenized_expression_path();
    test_token_cursor_and_ranges();
    test_top_level_token_ranges();
    test_statement_syntax_predicates();
    if (failures != 0) {
        fprintf(stderr, "%d lexer test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all lexer tests passed");
    return EXIT_SUCCESS;
}
