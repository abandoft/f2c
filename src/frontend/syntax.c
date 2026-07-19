#include "frontend/frontend.h"

static size_t statement_start(const Line *line) {
    if (line != NULL && line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER)
        return 1U;
    return 0U;
}

static int word(const Line *line, size_t relative_index, const char *expected) {
    const size_t start = statement_start(line);
    return start <= SIZE_MAX - relative_index &&
           f2c_line_token_equals(line, start + relative_index, expected);
}

static size_t statement_token_count(const Line *line) {
    const size_t start = statement_start(line);
    return line != NULL && start <= line->token_count ? line->token_count - start : 0U;
}

int f2c_interface_start_tokens(const Line *line) {
    const size_t count = statement_token_count(line);
    return (count != 0U && word(line, 0U, "interface")) ||
           (count >= 2U && word(line, 0U, "abstract") && word(line, 1U, "interface"));
}

int f2c_interface_end_tokens(const Line *line) {
    const size_t count = statement_token_count(line);
    return (count >= 2U && word(line, 0U, "end") && word(line, 1U, "interface")) ||
           (count != 0U && word(line, 0U, "endinterface"));
}

int f2c_abstract_interface_tokens(const Line *line) {
    return statement_token_count(line) >= 2U && word(line, 0U, "abstract") &&
           word(line, 1U, "interface");
}

int f2c_module_procedure_tokens(const Line *line) {
    return statement_token_count(line) >= 2U && word(line, 0U, "module") &&
           word(line, 1U, "procedure");
}

int f2c_contains_tokens(const Line *line) {
    return statement_token_count(line) == 1U && word(line, 0U, "contains");
}

int f2c_derived_type_start_tokens(const Line *line) {
    const size_t start = statement_start(line);
    const size_t count = statement_token_count(line);
    size_t index;
    if (line == NULL || count < 2U || !word(line, 0U, "type") ||
        line->tokens[start + 1U].kind == F2C_TOKEN_LEFT_PAREN || word(line, 1U, "is"))
        return 0;
    if (count == 2U && line->tokens[start + 1U].kind == F2C_TOKEN_IDENTIFIER)
        return 1;
    for (index = start + 1U; index < line->token_count; ++index) {
        if (line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON)
            return index + 1U < line->token_count &&
                   line->tokens[index + 1U].kind == F2C_TOKEN_IDENTIFIER;
    }
    return 0;
}

int f2c_derived_type_end_tokens(const Line *line) {
    const size_t count = statement_token_count(line);
    return (count >= 2U && word(line, 0U, "end") && word(line, 1U, "type")) ||
           (count != 0U && word(line, 0U, "endtype"));
}
