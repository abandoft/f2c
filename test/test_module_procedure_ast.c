#include "ast/declaration/module_procedure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ParsedLine {
    Line line;
    F2cToken *tokens;
} ParsedLine;

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static int parsed_line_init(ParsedLine *parsed, const char *source) {
    F2cTokenStream stream;
    size_t count = 0U;
    size_t capacity = 0U;
    memset(parsed, 0, sizeof(*parsed));
    parsed->line.text = (char *)source;
    parsed->line.source_name = "module-procedure-ast.f90";
    parsed->line.number = 23U;
    f2c_token_stream_init(&stream, source, parsed->line.number, 1U);
    for (;;) {
        F2cToken *replacement;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (stream.token.kind == F2C_TOKEN_INVALID)
            return 0;
        if (count == capacity) {
            const size_t next = capacity == 0U ? 16U : capacity * 2U;
            replacement = (F2cToken *)realloc(parsed->tokens, next * sizeof(*replacement));
            if (replacement == NULL)
                return 0;
            parsed->tokens = replacement;
            capacity = next;
        }
        stream.token.span.begin.source_name = parsed->line.source_name;
        stream.token.span.end.source_name = parsed->line.source_name;
        parsed->tokens[count++] = stream.token;
    }
    parsed->line.tokens = parsed->tokens;
    parsed->line.token_count = count;
    return 1;
}

static void parsed_line_discard(ParsedLine *parsed) {
    free(parsed->tokens);
    memset(parsed, 0, sizeof(*parsed));
}

static void test_complete_statement(void) {
    ParsedLine parsed;
    F2cModuleProcedureStatementSyntax syntax;
    expect(parsed_line_init(&parsed, "module procedure :: Solve_Integer, solve_real"),
           "MODULE PROCEDURE list tokenizes");
    expect(f2c_parse_module_procedure_statement_syntax(&parsed.line, &syntax) ==
                   F2C_MODULE_PROCEDURE_PARSED &&
               syntax.double_colon != NULL && syntax.name_count == 2U,
           "MODULE PROCEDURE AST retains its optional separator and every specific name");
    expect(syntax.names[0]->span.begin.line == 23U &&
               f2c_token_equals(syntax.names[0], "solve_integer") &&
               f2c_token_equals(syntax.names[1], "solve_real"),
           "specific procedure names preserve source spans and case-insensitive identity");
    f2c_module_procedure_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);

    expect(parsed_line_init(&parsed, "100 module procedure solve"),
           "labeled MODULE PROCEDURE tokenizes");
    expect(f2c_parse_module_procedure_statement_syntax(&parsed.line, &syntax) ==
                   F2C_MODULE_PROCEDURE_PARSED &&
               syntax.name_count == 1U && syntax.module_keyword->span.begin.column == 5U,
           "statement labels do not disturb the structured MODULE PROCEDURE range");
    f2c_module_procedure_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void expect_invalid(const char *source, F2cModuleProcedureStatementError error,
                           const char *token_text, const char *message) {
    ParsedLine parsed;
    F2cModuleProcedureStatementSyntax syntax;
    expect(parsed_line_init(&parsed, source), "invalid MODULE PROCEDURE tokenizes");
    expect(f2c_parse_module_procedure_statement_syntax(&parsed.line, &syntax) ==
                   F2C_MODULE_PROCEDURE_INVALID &&
               syntax.error == error && syntax.error_token != NULL &&
               f2c_token_equals(syntax.error_token, token_text),
           message);
    f2c_module_procedure_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_invalid_statements(void) {
    expect_invalid("module procedure", F2C_MODULE_PROCEDURE_ERROR_EMPTY_LIST, "procedure",
                   "an empty MODULE PROCEDURE list fails at its keyword");
    expect_invalid("module procedure ::", F2C_MODULE_PROCEDURE_ERROR_EMPTY_LIST,
                   "::", "a trailing double colon requires a specific name");
    expect_invalid("module procedure first second", F2C_MODULE_PROCEDURE_ERROR_SEPARATOR, "second",
                   "adjacent specific names require a comma");
    expect_invalid("module procedure first, first", F2C_MODULE_PROCEDURE_ERROR_DUPLICATE_NAME,
                   "first", "duplicate specific names fail at the repeated token");
    expect_invalid("module procedure first,", F2C_MODULE_PROCEDURE_ERROR_TRAILING_COMMA, ",",
                   "a trailing comma is rejected explicitly");
    expect_invalid("module procedure first, 1", F2C_MODULE_PROCEDURE_ERROR_NAME, "1",
                   "every specific designator must be an identifier");
}

static void test_nonmatches(void) {
    static const char *const sources[] = {
        "procedure(interface) :: callback",
        "module subroutine solve(value)",
        "call procedure()",
    };
    size_t index;
    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        ParsedLine parsed;
        F2cModuleProcedureStatementSyntax syntax;
        expect(parsed_line_init(&parsed, sources[index]), "nonmatching statement tokenizes");
        expect(f2c_parse_module_procedure_statement_syntax(&parsed.line, &syntax) ==
                   F2C_MODULE_PROCEDURE_NOT_MATCHED,
               "only the exact MODULE PROCEDURE prefix is classified as a binding statement");
        f2c_module_procedure_statement_syntax_discard(&syntax);
        parsed_line_discard(&parsed);
    }
}

static void test_large_specific_list(void) {
    char source[4096];
    size_t length;
    size_t index;
    ParsedLine parsed;
    F2cModuleProcedureStatementSyntax syntax;
    int written = snprintf(source, sizeof(source), "module procedure :: ");
    expect(written > 0 && (size_t)written < sizeof(source),
           "large MODULE PROCEDURE prefix is bounded");
    if (written <= 0 || (size_t)written >= sizeof(source))
        return;
    length = (size_t)written;
    for (index = 0U; index < 128U; ++index) {
        written = snprintf(source + length, sizeof(source) - length, "%sspecific_%03zu",
                           index == 0U ? "" : ", ", index);
        expect(written > 0 && (size_t)written < sizeof(source) - length,
               "large specific name append is bounded");
        if (written <= 0 || (size_t)written >= sizeof(source) - length)
            return;
        length += (size_t)written;
    }
    expect(parsed_line_init(&parsed, source), "large MODULE PROCEDURE list tokenizes");
    expect(f2c_parse_module_procedure_statement_syntax(&parsed.line, &syntax) ==
                   F2C_MODULE_PROCEDURE_PARSED &&
               syntax.name_count == 128U,
           "specific procedure storage grows dynamically without a fixed item limit");
    f2c_module_procedure_statement_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

int main(void) {
    test_complete_statement();
    test_invalid_statements();
    test_nonmatches();
    test_large_specific_list();
    if (failures != 0) {
        fprintf(stderr, "%d MODULE PROCEDURE AST test(s) failed\n", failures);
        return 1;
    }
    puts("all MODULE PROCEDURE AST tests passed");
    return 0;
}
