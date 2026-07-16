#include "internal/f2c.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_space(const char *cursor) {
    while (isspace((unsigned char)*cursor))
        ++cursor;
    return cursor;
}

static const char *matching_parenthesis(const char *open) {
    const char *cursor = open;
    int depth = 0;
    if (open == NULL || *open != '(')
        return NULL;
    do {
        if (*cursor == '(')
            ++depth;
        else if (*cursor == ')')
            --depth;
        ++cursor;
    } while (*cursor != '\0' && depth != 0);
    return depth == 0 ? cursor - 1 : NULL;
}

static int parse_letter_range_syntax(const char *text, int *first, int *last) {
    const char *cursor = skip_space(text);
    if (!isalpha((unsigned char)*cursor))
        return 0;
    *first = tolower((unsigned char)*cursor++);
    cursor = skip_space(cursor);
    if (*cursor == '-') {
        cursor = skip_space(cursor + 1);
        if (!isalpha((unsigned char)*cursor))
            return 0;
        *last = tolower((unsigned char)*cursor++);
    } else {
        *last = *first;
    }
    cursor = skip_space(cursor);
    return *cursor == '\0' && *first >= 'a' && *first <= 'z' && *last >= 'a' && *last <= 'z';
}

static int parse_letter_range(const char *text, int *first, int *last) {
    return parse_letter_range_syntax(text, first, last) && *last >= *first;
}

static int is_letter_range_group(const char *open, const char *close) {
    char *group;
    char **ranges;
    size_t count = 0U;
    size_t i;
    int valid = 1;
    if (open == NULL || close == NULL || close <= open)
        return 0;
    group = f2c_strdup_n(open, (size_t)(close - open + 1));
    if (group == NULL)
        return 0;
    ranges = f2c_split_arguments(group, &count);
    if (ranges == NULL || count == 0U)
        valid = 0;
    for (i = 0U; i < count; ++i) {
        int first;
        int last;
        if (!parse_letter_range_syntax(ranges[i], &first, &last))
            valid = 0;
        free(ranges[i]);
    }
    free(ranges);
    free(group);
    return valid;
}

static int selector_requests_double(const char *begin, const char *end) {
    char *selector = f2c_strdup_n(begin, (size_t)(end - begin));
    char *clean;
    int result = 0;
    if (selector == NULL)
        return 0;
    clean = f2c_trim(selector);
    if (strncmp(clean, "kind", strlen("kind")) == 0) {
        clean = (char *)skip_space(clean + strlen("kind"));
        if (*clean == '=')
            clean = (char *)skip_space(clean + 1);
    }
    result = strcmp(clean, "8") == 0 || strcmp(clean, "dp") == 0 || strcmp(clean, "real64") == 0;
    free(selector);
    return result;
}

static int selector_kind_value(const char *begin, const char *end) {
    char *selector = f2c_strdup_n(begin, (size_t)(end - begin));
    char *clean;
    char *value_end = NULL;
    long value;
    int kind = 0;
    if (selector == NULL)
        return 0;
    clean = f2c_trim(selector);
    if (strncmp(clean, "kind", strlen("kind")) == 0) {
        clean = (char *)skip_space(clean + strlen("kind"));
        if (*clean == '=')
            clean = (char *)skip_space(clean + 1);
    }
    value = strtol(clean, &value_end, 10);
    if (value_end != clean && *skip_space(value_end) == '\0' && value > 0 && value <= INT_MAX)
        kind = (int)value;
    else if (strcmp(clean, "dp") == 0 || strcmp(clean, "real64") == 0 ||
             strcmp(clean, "int64") == 0)
        kind = 8;
    else if (strcmp(clean, "sp") == 0 || strcmp(clean, "real32") == 0 ||
             strcmp(clean, "int32") == 0)
        kind = 4;
    free(selector);
    return kind;
}

static char *normalized_character_selector(const char *begin, const char *end) {
    char *selector = f2c_strdup_n(begin, (size_t)(end - begin));
    char *clean;
    if (selector == NULL)
        return NULL;
    clean = f2c_trim(selector);
    if (strncmp(clean, "len", strlen("len")) == 0) {
        clean = (char *)skip_space(clean + strlen("len"));
        if (*clean == '=')
            clean = (char *)skip_space(clean + 1);
    }
    if (clean != selector)
        memmove(selector, clean, strlen(clean) + 1U);
    return selector;
}

static Type parse_implicit_type(const char *text, const char **ranges_out,
                                char **character_length_out, int *kind_out) {
    const char *cursor = skip_space(text);
    Type type = TYPE_UNKNOWN;
    *character_length_out = NULL;
    *kind_out = 0;
    if (f2c_starts_word(cursor, "double precision")) {
        type = TYPE_DOUBLE;
        cursor += strlen("double precision");
    } else if (f2c_starts_word(cursor, "double complex")) {
        type = TYPE_DOUBLE_COMPLEX;
        cursor += strlen("double complex");
    } else if (f2c_starts_word(cursor, "integer")) {
        type = TYPE_INTEGER;
        cursor += strlen("integer");
    } else if (f2c_starts_word(cursor, "real")) {
        type = TYPE_REAL;
        cursor += strlen("real");
    } else if (f2c_starts_word(cursor, "complex")) {
        type = TYPE_COMPLEX;
        cursor += strlen("complex");
    } else if (f2c_starts_word(cursor, "logical")) {
        type = TYPE_LOGICAL;
        cursor += strlen("logical");
    } else if (f2c_starts_word(cursor, "character")) {
        type = TYPE_CHARACTER;
        cursor += strlen("character");
    } else {
        return TYPE_UNKNOWN;
    }

    cursor = skip_space(cursor);
    if (*cursor == '*') {
        if (type == TYPE_CHARACTER) {
            const char *selector_begin = skip_space(cursor + 1);
            const char *selector_end;
            if (*selector_begin == '(') {
                const char *close = matching_parenthesis(selector_begin);
                if (close == NULL)
                    return TYPE_UNKNOWN;
                selector_end = close;
                *character_length_out =
                    normalized_character_selector(selector_begin + 1, selector_end);
                cursor = skip_space(close + 1);
            } else {
                selector_end = selector_begin;
                while (isalnum((unsigned char)*selector_end) || *selector_end == '_')
                    ++selector_end;
                if (selector_end == selector_begin)
                    return TYPE_UNKNOWN;
                *character_length_out = normalized_character_selector(selector_begin, selector_end);
                cursor = skip_space(selector_end);
            }
            if (*character_length_out == NULL)
                return TYPE_UNKNOWN;
        } else {
            char *end = NULL;
            long width = strtol(cursor + 1, &end, 10);
            if (end == cursor + 1)
                return TYPE_UNKNOWN;
            if ((type == TYPE_REAL && width == 8) || (type == TYPE_COMPLEX && width == 16))
                type = type == TYPE_REAL ? TYPE_DOUBLE : TYPE_DOUBLE_COMPLEX;
            *kind_out =
                (int)((type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX) ? width / 2 : width);
            cursor = skip_space(end);
        }
    } else if (*cursor == '(') {
        const char *close = matching_parenthesis(cursor);
        if (close == NULL)
            return TYPE_UNKNOWN;
        if (!is_letter_range_group(cursor, close)) {
            if (type == TYPE_CHARACTER) {
                *character_length_out = normalized_character_selector(cursor + 1, close);
                if (*character_length_out == NULL)
                    return TYPE_UNKNOWN;
            } else if (selector_requests_double(cursor + 1, close)) {
                if (type == TYPE_REAL)
                    type = TYPE_DOUBLE;
                else if (type == TYPE_COMPLEX)
                    type = TYPE_DOUBLE_COMPLEX;
                *kind_out = 8;
            } else {
                *kind_out = selector_kind_value(cursor + 1, close);
            }
            cursor = skip_space(close + 1);
        }
    }
    *ranges_out = cursor;
    if (*kind_out == 0)
        *kind_out = f2c_default_kind(type);
    return type;
}

static void apply_ranges(Context *context, Unit *unit, const Line *line, Type type, int kind,
                         const char *character_length, const char *open) {
    const char *close = matching_parenthesis(open);
    char *group;
    char **ranges;
    size_t count = 0U;
    size_t i;
    if (close == NULL || *skip_space(close + 1) != '\0') {
        f2c_diagnostic_at(context, line->number, 1U, 1, "malformed IMPLICIT letter-range list");
        return;
    }
    group = f2c_strdup_n(open, (size_t)(close - open + 1));
    if (group == NULL) {
        f2c_diagnostic(context, line->number, 1, "out of memory parsing IMPLICIT statement");
        return;
    }
    ranges = f2c_split_arguments(group, &count);
    if (ranges == NULL || count == 0U) {
        f2c_diagnostic_at(context, line->number, 1U, 1,
                          "IMPLICIT type specification has no letter ranges");
        free(group);
        return;
    }
    for (i = 0U; i < count; ++i) {
        int first;
        int last;
        int letter;
        if (!parse_letter_range(ranges[i], &first, &last)) {
            f2c_diagnostic_at(context, line->number, 1U, 1, "invalid IMPLICIT letter range '%s'",
                              ranges[i]);
            free(ranges[i]);
            continue;
        }
        for (letter = first; letter <= last; ++letter) {
            const uint32_t bit = UINT32_C(1) << (unsigned)(letter - 'a');
            if (unit->implicit_none) {
                f2c_diagnostic_at(context, line->number, 1U, 1,
                                  "IMPLICIT type mapping conflicts with IMPLICIT NONE(TYPE)");
                break;
            }
            if ((unit->implicit_explicit_mask & bit) != 0U) {
                f2c_diagnostic_at(context, line->number, 1U, 1,
                                  "letter '%c' appears in more than one IMPLICIT mapping", letter);
                continue;
            }
            unit->implicit_types[letter - 'a'] = type;
            unit->implicit_kinds[letter - 'a'] = kind;
            if (type == TYPE_CHARACTER) {
                free(unit->implicit_character_lengths[letter - 'a']);
                unit->implicit_character_lengths[letter - 'a'] =
                    f2c_strdup(character_length != NULL ? character_length : "1");
                if (unit->implicit_character_lengths[letter - 'a'] == NULL)
                    f2c_diagnostic(context, line->number, 1,
                                   "out of memory storing implicit CHARACTER length");
            }
            unit->implicit_explicit_mask |= bit;
        }
        free(ranges[i]);
    }
    free(ranges);
    free(group);
}

static void parse_none(Context *context, Unit *unit, const Line *line, const char *cursor) {
    int none_type = 0;
    int none_external = 0;
    cursor = skip_space(cursor);
    if (*cursor == '\0') {
        none_type = 1;
    } else if (*cursor == '(') {
        const char *close = matching_parenthesis(cursor);
        char *group;
        char **specs;
        size_t count = 0U;
        size_t i;
        if (close == NULL || *skip_space(close + 1) != '\0') {
            f2c_diagnostic_at(context, line->number, 1U, 1,
                              "malformed IMPLICIT NONE specification");
            return;
        }
        group = f2c_strdup_n(cursor, (size_t)(close - cursor + 1));
        specs = group != NULL ? f2c_split_arguments(group, &count) : NULL;
        if (specs == NULL || count == 0U) {
            f2c_diagnostic(context, line->number, 1,
                           "out of memory or empty IMPLICIT NONE specification");
            free(group);
            return;
        }
        for (i = 0U; i < count; ++i) {
            char *spec = f2c_trim(specs[i]);
            if (strcmp(spec, "type") == 0) {
                if (none_type)
                    f2c_diagnostic_at(context, line->number, 1U, 1,
                                      "duplicate TYPE in IMPLICIT NONE specification");
                none_type = 1;
            } else if (strcmp(spec, "external") == 0) {
                if (none_external)
                    f2c_diagnostic_at(context, line->number, 1U, 1,
                                      "duplicate EXTERNAL in IMPLICIT NONE specification");
                none_external = 1;
            } else {
                f2c_diagnostic_at(context, line->number, 1U, 1,
                                  "unknown IMPLICIT NONE specification '%s'", spec);
            }
            free(specs[i]);
        }
        free(specs);
        free(group);
    } else {
        f2c_diagnostic_at(context, line->number, 1U, 1, "malformed IMPLICIT NONE specification");
        return;
    }
    if (none_type) {
        if (unit->implicit_none)
            f2c_diagnostic_at(context, line->number, 1U, 1,
                              "duplicate IMPLICIT NONE(TYPE) specification");
        if (unit->implicit_explicit_mask != 0U)
            f2c_diagnostic_at(context, line->number, 1U, 1,
                              "IMPLICIT NONE(TYPE) conflicts with an IMPLICIT type mapping");
        unit->implicit_none = 1;
    }
    if (none_external) {
        if (unit->implicit_none_external)
            f2c_diagnostic_at(context, line->number, 1U, 1,
                              "duplicate IMPLICIT NONE(EXTERNAL) specification");
        unit->implicit_none_external = 1;
    }
}

static void parse_implicit_statement(Context *context, Unit *unit, const Line *line) {
    const char *cursor;
    char *specifications;
    char **items;
    size_t count = 0U;
    size_t i;
    if (!f2c_starts_word(line->text, "implicit"))
        return;
    cursor = skip_space(line->text + strlen("implicit"));
    if (f2c_starts_word(cursor, "none")) {
        parse_none(context, unit, line, cursor + strlen("none"));
        return;
    }
    specifications = f2c_strdup(cursor);
    if (specifications == NULL) {
        f2c_diagnostic(context, line->number, 1, "out of memory parsing IMPLICIT statement");
        return;
    }
    items = f2c_split_arguments(specifications, &count);
    if (items == NULL || count == 0U) {
        f2c_diagnostic_at(context, line->number, 1U, 1,
                          "IMPLICIT statement has no type specifications");
        free(specifications);
        return;
    }
    for (i = 0U; i < count; ++i) {
        const char *ranges = NULL;
        char *character_length = NULL;
        int kind = 0;
        Type type = parse_implicit_type(items[i], &ranges, &character_length, &kind);
        if (type == TYPE_UNKNOWN || ranges == NULL || *skip_space(ranges) != '(') {
            f2c_diagnostic_at(context, line->number, 1U, 1,
                              "malformed IMPLICIT type specification '%s'", items[i]);
        } else {
            apply_ranges(context, unit, line, type, kind, character_length, skip_space(ranges));
        }
        free(character_length);
        free(items[i]);
    }
    free(items);
    free(specifications);
}

static int is_fortran_keyword(const char *name) {
    static const char *const keywords[] = {
        "allocate",   "assign",  "backspace", "block",     "call",      "case",   "character",
        "class",      "close",   "complex",   "contains",  "continue",  "cycle",  "data",
        "deallocate", "default", "do",        "double",    "else",      "elseif", "end",
        "enddo",      "endfile", "endif",     "endselect", "endwhere",  "error",  "exit",
        "external",   "forall",  "format",    "function",  "go",        "goto",   "if",
        "implicit",   "import",  "inquire",   "integer",   "interface", "is",     "logical",
        "namelist",   "none",    "open",      "nullify",   "precision", "print",  "program",
        "read",       "real",    "result",    "return",    "rewind",    "select", "stop",
        "subroutine", "then",    "to",        "type",      "use",       "where",  "while",
        "write"};
    size_t index;
    for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); ++index) {
        if (strcmp(name, keywords[index]) == 0)
            return 1;
    }
    return 0;
}

static int preceded_by_call(const char *line, const char *name) {
    const char *before = name;
    const size_t length = strlen("call");
    while (before > line && isspace((unsigned char)before[-1]))
        --before;
    if ((size_t)(before - line) < length)
        return 0;
    before -= length;
    return strncmp(before, "call", length) == 0 &&
           (before == line || (!isalnum((unsigned char)before[-1]) && before[-1] != '_'));
}

static int is_namelist_group_reference(Unit *unit, const char *line, const char *begin,
                                       const char *name) {
    const char *cursor = begin;
    const size_t keyword_length = strlen("nml");
    if (f2c_find_namelist(unit, name) == NULL)
        return 0;
    while (cursor > line && isspace((unsigned char)cursor[-1]))
        --cursor;
    if (cursor == line || cursor[-1] != '=')
        return 0;
    --cursor;
    while (cursor > line && isspace((unsigned char)cursor[-1]))
        --cursor;
    if ((size_t)(cursor - line) < keyword_length)
        return 0;
    cursor -= keyword_length;
    return strncmp(cursor, "nml", keyword_length) == 0 &&
           (cursor == line || (!isalnum((unsigned char)cursor[-1]) && cursor[-1] != '_'));
}

static int is_numeric_exponent_fragment(const char *name, const char *begin, const char *line,
                                        const char *after) {
    const char *digit;
    if ((name[0] != 'd' && name[0] != 'e') || begin <= line ||
        (begin[-1] != '.' && !isdigit((unsigned char)begin[-1])))
        return 0;
    if (name[1] == '\0')
        return isdigit((unsigned char)*after) ||
               ((*after == '+' || *after == '-') && isdigit((unsigned char)after[1]));
    for (digit = name + 1; *digit != '\0'; ++digit) {
        if (!isdigit((unsigned char)*digit))
            return 0;
    }
    return 1;
}

static Unit *find_internal_definition(Context *context, const Unit *host, const char *name) {
    const size_t host_index = (size_t)(host - context->units.items);
    size_t unit_index;
    for (unit_index = 0U; unit_index < context->units.count; ++unit_index) {
        Unit *candidate = &context->units.items[unit_index];
        if (candidate->internal && candidate->host_index == host_index &&
            candidate->fortran_name != NULL && strcmp(candidate->fortran_name, name) == 0)
            return candidate;
    }
    return NULL;
}

static Symbol *bind_known_internal(Context *context, Unit *host, Unit *definition,
                                   const char *name) {
    Symbol *symbol = f2c_ensure_symbol(host, name);
    if (symbol == NULL)
        return NULL;
    symbol->external = 1;
    symbol->external_declared = 1;
    symbol->external_subroutine = definition->kind == UNIT_SUBROUTINE;
    if (definition->kind == UNIT_FUNCTION) {
        if (!definition->return_type_explicit) {
            f2c_prepare_implicit_map(context, definition);
            definition->return_type = f2c_implicit_type_for_name(
                definition, definition->result_name != NULL ? definition->result_name : name);
            definition->return_kind = f2c_implicit_kind_for_name(
                definition, definition->result_name != NULL ? definition->result_name : name);
        }
        if (!f2c_copy_function_result_metadata(symbol, definition))
            return NULL;
    }
    return symbol;
}

static void discover_line_symbols(Context *context, Unit *unit, const Line *line) {
    const char *cursor = line->text;
    const char *statement = skip_space(line->text);
    int parenthesis_depth = 0;
    while (isdigit((unsigned char)*statement))
        ++statement;
    statement = skip_space(statement);
    if (f2c_declaration_line(line->text) || f2c_starts_word(line->text, "use") ||
        f2c_starts_word(line->text, "contains") || f2c_starts_word(statement, "format"))
        return;
    while (*cursor != '\0') {
        if (*cursor == '\'' || *cursor == '"') {
            const char quote = *cursor++;
            while (*cursor != '\0') {
                if (*cursor == quote) {
                    ++cursor;
                    if (*cursor == quote) {
                        ++cursor;
                        continue;
                    }
                    break;
                }
                ++cursor;
            }
            continue;
        }
        if (*cursor == '(') {
            ++parenthesis_depth;
            ++cursor;
            continue;
        }
        if (*cursor == ')') {
            if (parenthesis_depth > 0)
                --parenthesis_depth;
            ++cursor;
            continue;
        }
        if (isalpha((unsigned char)*cursor) || *cursor == '_') {
            const char *begin = cursor;
            const char *before;
            const char *after;
            char *name;
            Symbol *symbol;
            Unit *internal;
            while (isalnum((unsigned char)*cursor) || *cursor == '_')
                ++cursor;
            name = f2c_strdup_n(begin, (size_t)(cursor - begin));
            if (name == NULL) {
                f2c_diagnostic(context, line->number, 1,
                               "out of memory discovering implicit symbols");
                return;
            }
            after = skip_space(cursor);
            before = begin;
            while (before > line->text && isspace((unsigned char)before[-1]))
                --before;
            if (name[0] == '_' || is_numeric_exponent_fragment(name, begin, line->text, after) ||
                (begin > line->text && (isalnum((unsigned char)begin[-1]) || begin[-1] == '_')) ||
                is_fortran_keyword(name) || f2c_is_intrinsic_name(name) ||
                f2c_find_derived_type(unit, name) != NULL ||
                (before > line->text && before[-1] == '.' && *after == '.') ||
                (before > line->text && before[-1] == '%') ||
                is_namelist_group_reference(unit, line->text, begin, name) ||
                (*after == '=' && after[1] != '=' && parenthesis_depth > 0 &&
                 f2c_find_symbol(unit, name) == NULL) ||
                *after == ':' || strcmp(name, unit->name) == 0 ||
                (unit->fortran_name != NULL && strcmp(name, unit->fortran_name) == 0)) {
                free(name);
                continue;
            }
            symbol = f2c_find_symbol(unit, name);
            internal = find_internal_definition(context, unit, name);
            if (internal != NULL) {
                symbol = bind_known_internal(context, unit, internal, name);
            } else if (*after == '(' &&
                       (symbol == NULL || (symbol->rank == 0U && symbol->type != TYPE_CHARACTER))) {
                symbol = f2c_ensure_symbol(unit, name);
                if (symbol != NULL && !preceded_by_call(line->text, begin)) {
                    symbol->external = 1;
                    symbol->external_subroutine = 0;
                }
            } else if (!preceded_by_call(line->text, begin)) {
                symbol = f2c_ensure_symbol(unit, name);
            }
            if (symbol == NULL) {
                f2c_diagnostic(context, line->number, 1,
                               "out of memory discovering implicit symbol '%s'", name);
                free(name);
                return;
            }
            if (symbol->first_seen_line == 0U)
                symbol->first_seen_line = line->number;
            free(name);
            continue;
        }
        ++cursor;
    }
}

void f2c_prepare_implicit_map(Context *context, Unit *unit) {
    size_t letter;
    size_t line_index;
    if (unit->implicit_map_initialized)
        return;
    for (letter = 0U; letter < 26U; ++letter) {
        unit->implicit_types[letter] =
            letter >= (size_t)('i' - 'a') && letter <= (size_t)('n' - 'a') ? TYPE_INTEGER
                                                                           : TYPE_REAL;
        unit->implicit_kinds[letter] = f2c_default_kind(unit->implicit_types[letter]);
    }
    unit->implicit_map_initialized = 1;
    for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
        if (f2c_unit_line_is_active(unit, &context->lines.items[line_index]))
            parse_implicit_statement(context, unit, &context->lines.items[line_index]);
    }
}

void f2c_discover_implicit_symbols(Context *context, Unit *unit) {
    size_t line_index;
    for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
        if (f2c_unit_line_is_active(unit, &context->lines.items[line_index]))
            discover_line_symbols(context, unit, &context->lines.items[line_index]);
    }
}

Type f2c_implicit_type_for_name(const Unit *unit, const char *name) {
    int first;
    if (unit == NULL || name == NULL || name[0] == '\0' || unit->implicit_none)
        return TYPE_UNKNOWN;
    first = tolower((unsigned char)name[0]);
    if (first < 'a' || first > 'z')
        return TYPE_UNKNOWN;
    return unit->implicit_types[first - 'a'];
}

int f2c_implicit_kind_for_name(const Unit *unit, const char *name) {
    int first;
    if (unit == NULL || name == NULL || name[0] == '\0' || unit->implicit_none)
        return 0;
    first = tolower((unsigned char)name[0]);
    if (first < 'a' || first > 'z')
        return 0;
    return unit->implicit_kinds[first - 'a'];
}

const char *f2c_implicit_character_length_for_name(const Unit *unit, const char *name) {
    int first;
    if (unit == NULL || name == NULL || name[0] == '\0')
        return NULL;
    first = tolower((unsigned char)name[0]);
    if (first < 'a' || first > 'z')
        return NULL;
    return unit->implicit_character_lengths[first - 'a'];
}

void f2c_validate_implicit_external(Context *context, Unit *unit) {
    size_t symbol_index;
    if (!unit->implicit_none_external)
        return;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        const Symbol *symbol = &unit->symbols[symbol_index];
        if (symbol->external && !symbol->external_declared) {
            f2c_diagnostic(context,
                           symbol->first_seen_line != 0U ? symbol->first_seen_line
                                                         : context->lines.items[unit->begin].number,
                           1,
                           "procedure '%s' requires an EXTERNAL declaration or explicit "
                           "interface under IMPLICIT NONE(EXTERNAL)",
                           symbol->name);
        }
    }
}
