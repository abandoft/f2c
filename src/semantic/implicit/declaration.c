#include "frontend/declaration/private.h"
#include "semantic/implicit/private.h"

#include <stdlib.h>
#include <string.h>

static size_t statement_start(const Line *line) {
    return line != NULL && line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U
                                                                                              : 0U;
}

static int token_letter(const F2cToken *token, int *letter) {
    unsigned char value;
    if (token == NULL || token->kind != F2C_TOKEN_IDENTIFIER || token->length != 1U)
        return 0;
    value = (unsigned char)token->begin[0];
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z')
        value = (unsigned char)(value - (unsigned char)'A' + (unsigned char)'a');
    if (value < (unsigned char)'a' || value > (unsigned char)'z')
        return 0;
    *letter = (int)value;
    return 1;
}

static int parse_letter_range(const Line *line, size_t begin, size_t end, int *first, int *last) {
    if (begin >= end || !token_letter(&line->tokens[begin], first))
        return 0;
    if (end == begin + 1U) {
        *last = *first;
        return 1;
    }
    return end == begin + 3U && line->tokens[begin + 1U].kind == F2C_TOKEN_OPERATOR &&
           f2c_token_equals(&line->tokens[begin + 1U], "-") &&
           token_letter(&line->tokens[begin + 2U], last);
}

static int is_letter_range_group(const Line *line, size_t open, size_t close) {
    size_t begin = open + 1U;
    size_t index;
    if (begin == close)
        return 0;
    for (index = begin; index <= close; ++index) {
        const int at_end = index == close;
        if (at_end || line->tokens[index].kind == F2C_TOKEN_COMMA) {
            int first;
            int last;
            if (!parse_letter_range(line, begin, index, &first, &last))
                return 0;
            begin = index + 1U;
        }
    }
    return 1;
}

static int find_letter_range_group(const Line *line, size_t begin, size_t *open_out,
                                   size_t *close_out) {
    size_t index;
    for (index = begin; index < line->token_count; ++index) {
        size_t close;
        if (line->tokens[index].kind != F2C_TOKEN_LEFT_PAREN)
            continue;
        if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close))
            return 0;
        if (is_letter_range_group(line, index, close) &&
            (close + 1U == line->token_count || line->tokens[close + 1U].kind == F2C_TOKEN_COMMA)) {
            *open_out = index;
            *close_out = close;
            return 1;
        }
        index = close;
    }
    return 0;
}

static int collect_letter_mask(Context *context, Unit *unit, const Line *line, size_t open,
                               size_t close, uint32_t *mask) {
    size_t begin = open + 1U;
    size_t index;
    int valid = 1;
    *mask = 0U;
    if (unit->implicit_none) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[open], 1,
                                  "IMPLICIT type mapping conflicts with IMPLICIT NONE(TYPE)");
        valid = 0;
    }
    for (index = begin; index <= close; ++index) {
        const int at_end = index == close;
        if (at_end || line->tokens[index].kind == F2C_TOKEN_COMMA) {
            int first;
            int last;
            int letter;
            if (!parse_letter_range(line, begin, index, &first, &last) || last < first) {
                char *range = f2c_token_range_text(f2c_line_token_range(line, begin, index));
                if (range != NULL)
                    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                              begin < close ? &line->tokens[begin]
                                                            : &line->tokens[open],
                                              1, "invalid IMPLICIT letter range '%s'", range);
                else
                    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                              begin < close ? &line->tokens[begin]
                                                            : &line->tokens[open],
                                              1, "invalid IMPLICIT letter range");
                free(range);
                valid = 0;
                begin = index + 1U;
                continue;
            }
            for (letter = first; letter <= last; ++letter) {
                const uint32_t bit = UINT32_C(1) << (unsigned)(letter - 'a');
                if ((*mask & bit) != 0U) {
                    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                              &line->tokens[begin], 1,
                                              "letter '%c' appears more than once in an IMPLICIT "
                                              "mapping",
                                              letter);
                    valid = 0;
                }
                if ((unit->implicit_explicit_mask & bit) != 0U) {
                    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                              &line->tokens[begin], 1,
                                              "letter '%c' appears in more than one IMPLICIT "
                                              "mapping",
                                              letter);
                    valid = 0;
                }
                *mask |= bit;
            }
            begin = index + 1U;
        }
    }
    return valid;
}

static int apply_letter_mapping(Context *context, Unit *unit, const Line *line,
                                const F2cDeclarationTypeSpec *type, size_t open, size_t close) {
    char *lengths[26] = {0};
    uint32_t mask;
    size_t letter;
    const char *character_length = type->character_length != NULL ? type->character_length : "1";
    if (!collect_letter_mask(context, unit, line, open, close, &mask))
        return 0;
    if (type->type == TYPE_CHARACTER) {
        for (letter = 0U; letter < 26U; ++letter) {
            if ((mask & (UINT32_C(1) << (unsigned)letter)) == 0U)
                continue;
            lengths[letter] = f2c_strdup(character_length);
            if (lengths[letter] == NULL) {
                size_t cleanup;
                for (cleanup = 0U; cleanup < 26U; ++cleanup)
                    free(lengths[cleanup]);
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line,
                                          &line->tokens[open], 1,
                                          "out of memory storing implicit CHARACTER length");
                return 0;
            }
        }
    }
    for (letter = 0U; letter < 26U; ++letter) {
        const uint32_t bit = UINT32_C(1) << (unsigned)letter;
        if ((mask & bit) == 0U)
            continue;
        unit->implicit_types[letter] = type->type;
        unit->implicit_kinds[letter] = type->kind;
        free(unit->implicit_character_lengths[letter]);
        unit->implicit_character_lengths[letter] = lengths[letter];
        unit->implicit_character_length_syntax[letter] = type->character_length_syntax;
    }
    unit->implicit_explicit_mask |= mask;
    return 1;
}

static void parse_none(Context *context, Unit *unit, const Line *line, size_t begin) {
    int none_type = 0;
    int none_external = 0;
    int valid = 1;
    if (begin == line->token_count) {
        none_type = 1;
    } else {
        size_t close;
        size_t index;
        int expect_specification = 1;
        if (line->tokens[begin].kind != F2C_TOKEN_LEFT_PAREN ||
            !f2c_token_matching_delimiter(line->tokens, line->token_count, begin, &close) ||
            close + 1U != line->token_count || close == begin + 1U) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[begin], 1,
                                      "malformed IMPLICIT NONE specification");
            return;
        }
        for (index = begin + 1U; index < close; ++index) {
            const F2cToken *token = &line->tokens[index];
            if (expect_specification) {
                if (token->kind != F2C_TOKEN_IDENTIFIER) {
                    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                              "malformed IMPLICIT NONE specification list");
                    valid = 0;
                } else if (f2c_token_equals(token, "type")) {
                    if (none_type) {
                        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, token, 1,
                                                  "duplicate TYPE in IMPLICIT NONE specification");
                        valid = 0;
                    }
                    none_type = 1;
                } else if (f2c_token_equals(token, "external")) {
                    if (none_external) {
                        f2c_diagnostic_token_code(
                            context, F2C_DIAGNOSTIC_SEMANTIC, line, token, 1,
                            "duplicate EXTERNAL in IMPLICIT NONE specification");
                        valid = 0;
                    }
                    none_external = 1;
                } else {
                    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                              "unknown IMPLICIT NONE specification");
                    valid = 0;
                }
            } else if (token->kind != F2C_TOKEN_COMMA) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                          "malformed IMPLICIT NONE specification list");
                valid = 0;
            }
            expect_specification = !expect_specification;
        }
        if (expect_specification) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[close], 1,
                                      "malformed IMPLICIT NONE specification list");
            valid = 0;
        }
    }
    if (none_type && unit->implicit_none) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[begin - 1U],
                                  1, "duplicate IMPLICIT NONE(TYPE) specification");
        valid = 0;
    }
    if (none_type && unit->implicit_explicit_mask != 0U) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[begin - 1U],
                                  1, "IMPLICIT NONE(TYPE) conflicts with an IMPLICIT type mapping");
        valid = 0;
    }
    if (none_external && unit->implicit_none_external) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[begin - 1U],
                                  1, "duplicate IMPLICIT NONE(EXTERNAL) specification");
        valid = 0;
    }
    if (valid) {
        unit->implicit_none |= none_type;
        unit->implicit_none_external |= none_external;
    }
}

void f2c_parse_implicit_statement(Context *context, Unit *unit, const Line *line) {
    size_t index = statement_start(line);
    if (!f2c_line_token_equals(line, index, "implicit"))
        return;
    ++index;
    if (index == line->token_count) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[index - 1U],
                                  1, "IMPLICIT statement has no type specifications");
        return;
    }
    if (f2c_line_token_equals(line, index, "none")) {
        parse_none(context, unit, line, index + 1U);
        return;
    }
    while (index < line->token_count) {
        F2cDeclarationTypeSpec type;
        Line type_view = *line;
        size_t range_open;
        size_t range_close;
        memset(&type, 0, sizeof(type));
        if (!find_letter_range_group(line, index, &range_open, &range_close)) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[index], 1,
                                      "malformed IMPLICIT type specification");
            return;
        }
        type_view.token_count = range_open;
        if (!f2c_parse_type_spec_tokens(context, unit, &type_view, index, &type) ||
            type.end != range_open || type.type == TYPE_DERIVED) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[index], 1,
                                      "malformed IMPLICIT type specification");
            f2c_release_type_spec(&type);
            return;
        }
        (void)apply_letter_mapping(context, unit, line, &type, range_open, range_close);
        f2c_release_type_spec(&type);
        index = range_close + 1U;
        if (index == line->token_count)
            break;
        if (line->tokens[index].kind != F2C_TOKEN_COMMA || ++index == line->token_count) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                      &line->tokens[index - 1U], 1,
                                      "malformed IMPLICIT type specification list");
            return;
        }
    }
}
