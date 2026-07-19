#include "ast/declaration/procedure.h"

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
    parsed->line.source_name = "procedure-ast.f90";
    parsed->line.number = 11U;
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

static void test_complete_declaration(void) {
    static const char source[] =
        "procedure(operation), pointer, optional, intent(inout), nopass :: first, second";
    ParsedLine parsed;
    F2cProcedureDeclarationSyntax syntax;
    expect(parsed_line_init(&parsed, source), "complete PROCEDURE declaration tokenizes");
    expect(f2c_parse_procedure_declaration_syntax(&parsed.line, &syntax) ==
               F2C_PROCEDURE_DECLARATION_PARSED,
           "complete PROCEDURE declaration builds a syntax AST");
    expect(syntax.interface_name != NULL && f2c_token_equals(syntax.interface_name, "operation") &&
               syntax.pointer_attribute != NULL && syntax.optional_attribute != NULL &&
               syntax.intent_attribute != NULL && syntax.nopass_attribute != NULL &&
               syntax.pass_attribute == NULL && syntax.intent == F2C_PROCEDURE_INTENT_INOUT,
           "PROCEDURE attributes retain their individual token identities");
    expect(syntax.entity_count == 2U && f2c_token_equals(syntax.entities[0], "first") &&
               f2c_token_equals(syntax.entities[1], "second"),
           "PROCEDURE entity list is represented structurally");
    expect(syntax.interface_name->span.begin.line == 11U &&
               syntax.interface_name->span.begin.column == 11U &&
               syntax.entities[1]->span.begin.column == 74U,
           "PROCEDURE interface and entities retain exact token spans");
    f2c_procedure_declaration_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_optional_separator(void) {
    ParsedLine parsed;
    F2cProcedureDeclarationSyntax syntax;
    expect(parsed_line_init(&parsed, "procedure(operation) callback"),
           "separator-free PROCEDURE declaration tokenizes");
    expect(f2c_parse_procedure_declaration_syntax(&parsed.line, &syntax) ==
                   F2C_PROCEDURE_DECLARATION_PARSED &&
               syntax.entity_count == 1U && f2c_token_equals(syntax.entities[0], "callback"),
           "PROCEDURE declaration permits an omitted double colon when attributes are absent");
    f2c_procedure_declaration_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void expect_invalid(const char *source, F2cProcedureDeclarationError error,
                           const char *token_text, const char *message) {
    ParsedLine parsed;
    F2cProcedureDeclarationSyntax syntax;
    expect(parsed_line_init(&parsed, source), "invalid PROCEDURE declaration tokenizes");
    expect(f2c_parse_procedure_declaration_syntax(&parsed.line, &syntax) ==
                   F2C_PROCEDURE_DECLARATION_INVALID &&
               syntax.error == error && syntax.error_token != NULL &&
               f2c_token_equals(syntax.error_token, token_text),
           message);
    f2c_procedure_declaration_syntax_discard(&syntax);
    parsed_line_discard(&parsed);
}

static void test_invalid_declarations(void) {
    expect_invalid("procedure() :: callback", F2C_PROCEDURE_DECLARATION_ERROR_INTERFACE, "(",
                   "empty PROCEDURE interface fails at its opening delimiter");
    expect_invalid("procedure(operation), volatile :: callback",
                   F2C_PROCEDURE_DECLARATION_ERROR_UNKNOWN_ATTRIBUTE, "volatile",
                   "unknown PROCEDURE attribute fails at its canonical token");
    expect_invalid("procedure(operation), pointer, pointer :: callback",
                   F2C_PROCEDURE_DECLARATION_ERROR_DUPLICATE_ATTRIBUTE, "pointer",
                   "duplicate PROCEDURE attribute fails at the repeated token");
    expect_invalid("procedure(operation) pointer :: callback",
                   F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE_SEPARATOR, "pointer",
                   "missing attribute comma fails before semantic lowering");
    expect_invalid("procedure(operation) :: first, first",
                   F2C_PROCEDURE_DECLARATION_ERROR_DUPLICATE_ENTITY, "first",
                   "duplicate PROCEDURE entity fails at the repeated name");
    expect_invalid("procedure(operation) :: callback => null()",
                   F2C_PROCEDURE_DECLARATION_ERROR_ENTITY_LIST, "=>",
                   "unsupported procedure-pointer initialization is rejected explicitly");
}

int main(void) {
    test_complete_declaration();
    test_optional_separator();
    test_invalid_declarations();
    if (failures != 0) {
        fprintf(stderr, "%d PROCEDURE AST test(s) failed\n", failures);
        return 1;
    }
    puts("all PROCEDURE declaration AST tests passed");
    return 0;
}
