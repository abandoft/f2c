#include "internal/f2c.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

char *f2c_identifier(const char *begin, size_t *consumed) {
    F2cLexer lexer;
    f2c_lexer_init(&lexer, begin, 1U, 1U);
    f2c_lexer_next(&lexer);
    if (lexer.token.kind != F2C_TOKEN_IDENTIFIER) {
        *consumed = 0U;
        return NULL;
    }
    *consumed = lexer.token.length;
    return f2c_token_text(&lexer.token);
}

static char **split_arguments(const char *open, size_t *count, int preserve_empty,
                              int strip_outer_parenthesis) {
    char **items = NULL;
    size_t item_count = 0U;
    size_t capacity = 0U;
    F2cLexer lexer;
    const char *start = open;
    int depth = 0;
    int bracket_depth = 0;
    int saw_separator = 0;
    *count = 0U;
    f2c_lexer_init(&lexer, open, 1U, 1U);
    f2c_lexer_next(&lexer);
    if (strip_outer_parenthesis && lexer.token.kind == F2C_TOKEN_LEFT_PAREN) {
        start = lexer.cursor;
        f2c_lexer_next(&lexer);
    }
    for (;;) {
        const int at_end =
            lexer.token.kind == F2C_TOKEN_END ||
            (lexer.token.kind == F2C_TOKEN_RIGHT_PAREN && depth == 0 && bracket_depth == 0);
        const int separator =
            lexer.token.kind == F2C_TOKEN_COMMA && depth == 0 && bracket_depth == 0;
        if (at_end || separator) {
            char *part = f2c_strdup_n(start, (size_t)(lexer.token.begin - start));
            char *clean;
            char **replacement;
            if (part == NULL) {
                goto fail;
            }
            clean = f2c_trim(part);
            if (*clean != '\0' || (preserve_empty && (!at_end || saw_separator))) {
                if (clean != part) {
                    memmove(part, clean, strlen(clean) + 1U);
                }
                if (item_count == capacity) {
                    capacity = capacity == 0U ? 4U : capacity * 2U;
                    replacement = (char **)realloc(items, capacity * sizeof(*items));
                    if (replacement == NULL) {
                        free(part);
                        goto fail;
                    }
                    items = replacement;
                }
                items[item_count++] = part;
            } else {
                free(part);
            }
            if (at_end) {
                break;
            }
            saw_separator = 1;
            start = lexer.cursor;
        }
        if (!at_end && !separator) {
            if (lexer.token.kind == F2C_TOKEN_LEFT_PAREN)
                ++depth;
            else if (lexer.token.kind == F2C_TOKEN_RIGHT_PAREN && depth > 0)
                --depth;
            else if (lexer.token.kind == F2C_TOKEN_LEFT_BRACKET ||
                     lexer.token.kind == F2C_TOKEN_ARRAY_BEGIN)
                ++bracket_depth;
            else if ((lexer.token.kind == F2C_TOKEN_RIGHT_BRACKET ||
                      lexer.token.kind == F2C_TOKEN_ARRAY_END) &&
                     bracket_depth > 0)
                --bracket_depth;
        }
        f2c_lexer_next(&lexer);
    }
    *count = item_count;
    return items;
fail:
    while (item_count != 0U) {
        free(items[--item_count]);
    }
    free(items);
    return NULL;
}

char **f2c_split_arguments(const char *open, size_t *count) {
    return split_arguments(open, count, 0, 1);
}

char **f2c_split_actual_arguments(const char *open, size_t *count) {
    return split_arguments(open, count, 1, 1);
}

char **f2c_split_comma_list(const char *text, size_t *count) {
    return split_arguments(text, count, 0, 0);
}

int f2c_unit_line_is_active(const Unit *unit, const Line *line) {
    return unit != NULL && line != NULL && (unit->interface_body || line->interface_depth == 0U);
}

static int units_push(Units *units, Unit unit) {
    Unit *replacement;
    size_t capacity;
    if (units->count == units->capacity) {
        capacity = units->capacity == 0U ? 8U : units->capacity * 2U;
        replacement = (Unit *)realloc(units->items, capacity * sizeof(*replacement));
        if (replacement == NULL) {
            return 0;
        }
        units->items = replacement;
        units->capacity = capacity;
    }
    units->items[units->count++] = unit;
    return 1;
}

static const char *skip_declaration_space(const char *cursor) {
    while (isspace((unsigned char)*cursor))
        ++cursor;
    return cursor;
}

/*
 * Parse the length selector following CHARACTER.  Fixed-form sources in
 * particular commonly spell an assumed length as "*( * )"; whitespace is
 * insignificant in that spelling and must not silently turn it into the
 * default length of one.  The returned expression is trimmed and normalized,
 * so all assumed-length forms are represented by the single string "*".
 */
static char *parse_character_length_selector(const char *text, const char **after) {
    const char *cursor = skip_declaration_space(text);
    const char *begin;
    const char *end;
    char *selector;
    char *clean;

    if (*cursor == '*') {
        ++cursor;
        cursor = skip_declaration_space(cursor);
        if (*cursor == '(') {
            int depth = 1;
            begin = ++cursor;
            while (*cursor != '\0' && depth != 0) {
                if (*cursor == '(')
                    ++depth;
                else if (*cursor == ')')
                    --depth;
                if (depth != 0)
                    ++cursor;
            }
            end = cursor;
            if (*cursor == ')')
                ++cursor;
        } else {
            begin = cursor;
            while (isalnum((unsigned char)*cursor) || *cursor == '_')
                ++cursor;
            end = cursor;
        }
    } else if (*cursor == '(') {
        int depth = 1;
        begin = ++cursor;
        while (*cursor != '\0' && depth != 0) {
            if (*cursor == '(')
                ++depth;
            else if (*cursor == ')')
                --depth;
            if (depth != 0)
                ++cursor;
        }
        end = cursor;
        if (*cursor == ')')
            ++cursor;
    } else {
        if (after != NULL)
            *after = text;
        return NULL;
    }

    if (after != NULL)
        *after = cursor;
    selector = f2c_strdup_n(begin, (size_t)(end - begin));
    if (selector == NULL)
        return NULL;
    clean = f2c_trim(selector);
    if (strncmp(clean, "len", strlen("len")) == 0 &&
        (clean[strlen("len")] == '\0' || isspace((unsigned char)clean[strlen("len")]) ||
         clean[strlen("len")] == '=')) {
        clean += strlen("len");
        clean = f2c_trim(clean);
        if (*clean == '=')
            clean = f2c_trim(clean + 1);
    }
    if (clean != selector)
        memmove(selector, clean, strlen(clean) + 1U);
    return selector;
}

static Type parse_type_prefix(const char *line, size_t *length) {
    const char *cursor = line;
    Type type = TYPE_UNKNOWN;
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
    } else if ((f2c_starts_word(cursor, "type") &&
                *skip_declaration_space(cursor + strlen("type")) == '(') ||
               (f2c_starts_word(cursor, "class") &&
                *skip_declaration_space(cursor + strlen("class")) == '(')) {
        type = TYPE_DERIVED;
        cursor = skip_declaration_space(
            cursor + (f2c_starts_word(cursor, "class") ? strlen("class") : strlen("type")));
    }
    if (type == TYPE_CHARACTER) {
        const char *after = cursor;
        char *selector = parse_character_length_selector(cursor, &after);
        if (after != cursor)
            cursor = after;
        free(selector);
    }
    if (type != TYPE_UNKNOWN && *cursor == '(') {
        int depth = 1;
        ++cursor;
        while (*cursor != '\0' && depth != 0) {
            if (*cursor == '(') {
                ++depth;
            } else if (*cursor == ')') {
                --depth;
            }
            ++cursor;
        }
    } else if (type != TYPE_UNKNOWN && *cursor == '*') {
        long byte_size = 0;
        ++cursor;
        if (*cursor == '(') {
            int depth = 1;
            ++cursor;
            while (*cursor != '\0' && depth != 0) {
                if (*cursor == '(')
                    ++depth;
                else if (*cursor == ')')
                    --depth;
                ++cursor;
            }
        } else {
            byte_size = strtol(cursor, NULL, 10);
            while (isdigit((unsigned char)*cursor))
                ++cursor;
        }
        if (type == TYPE_REAL && byte_size == 8)
            type = TYPE_DOUBLE;
        else if (type == TYPE_COMPLEX && byte_size == 16)
            type = TYPE_DOUBLE_COMPLEX;
    }
    *length = (size_t)(cursor - line);
    return type;
}

static int declaration_kind_value(Unit *unit, const char *declaration, size_t type_length,
                                  Type type);

int f2c_parse_unit_header(const char *line, Unit *unit) {
    const char *cursor = line;
    size_t type_length = 0U;
    size_t name_length = 0U;
    Type prefix = parse_type_prefix(cursor, &type_length);
    memset(unit, 0, sizeof(*unit));
    unit->return_type = TYPE_REAL;
    unit->return_kind = f2c_default_kind(TYPE_REAL);
    unit->return_type_explicit = prefix != TYPE_UNKNOWN;
    if (prefix == TYPE_CHARACTER)
        unit->result_character_length =
            parse_character_length_selector(line + strlen("character"), NULL);
    if (prefix != TYPE_UNKNOWN) {
        unit->return_kind = declaration_kind_value(NULL, line, type_length, prefix);
        if (unit->return_kind == 8 && prefix == TYPE_REAL)
            prefix = TYPE_DOUBLE;
        else if (unit->return_kind == 8 && prefix == TYPE_COMPLEX)
            prefix = TYPE_DOUBLE_COMPLEX;
        cursor = skip_declaration_space(line + type_length);
    }
    if (f2c_starts_word(cursor, "recursive")) {
        cursor += strlen("recursive");
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
    }
    if (f2c_starts_word(cursor, "pure")) {
        cursor += strlen("pure");
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
    }
    if (f2c_starts_word(cursor, "program")) {
        unit->kind = UNIT_PROGRAM;
        cursor += strlen("program");
    } else if (f2c_starts_word(cursor, "subroutine")) {
        unit->kind = UNIT_SUBROUTINE;
        cursor += strlen("subroutine");
    } else if (f2c_starts_word(cursor, "function")) {
        unit->kind = UNIT_FUNCTION;
        unit->return_type = prefix == TYPE_UNKNOWN ? TYPE_REAL : prefix;
        if (prefix == TYPE_UNKNOWN)
            unit->return_kind = f2c_default_kind(TYPE_REAL);
        cursor += strlen("function");
    } else {
        return 0;
    }
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    unit->name = f2c_identifier(cursor, &name_length);
    if (unit->name == NULL) {
        return 0;
    }
    cursor += name_length;
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor == '(') {
        unit->arguments = f2c_split_arguments(cursor, &unit->argument_count);
    }
    if (unit->kind == UNIT_FUNCTION) {
        const char *result = strstr(cursor, "result");
        if (result != NULL) {
            result += strlen("result");
            while (isspace((unsigned char)*result)) {
                ++result;
            }
            if (*result == '(') {
                ++result;
                unit->result_name = f2c_identifier(result, &name_length);
            }
        }
        if (unit->result_name == NULL) {
            unit->result_name = f2c_strdup(unit->name);
        }
    }
    return 1;
}

int f2c_discover_units(Context *context) {
    size_t i;
    size_t active_index = (size_t)-1;
    size_t host_index = (size_t)-1;
    size_t interface_depth = 0U;
    size_t derived_type_depth = 0U;
    for (i = 0U; i < context->lines.count; ++i) {
        Unit unit;
        const char *line = context->lines.items[i].text;
        if (active_index == (size_t)-1 && host_index == (size_t)-1) {
            size_t module_index;
            int in_module_specification = 0;
            for (module_index = 0U; module_index < context->modules.count; ++module_index) {
                Unit *module = &context->modules.items[module_index];
                if (i >= module->begin && i <= module->end) {
                    in_module_specification = 1;
                    break;
                }
            }
            if (in_module_specification)
                continue;
        }
        if (active_index != (size_t)-1) {
            Unit *active = &context->units.items[active_index];
            if (f2c_interface_start_line(line)) {
                ++interface_depth;
                context->lines.items[i].interface_depth = interface_depth;
                continue;
            }
            if (interface_depth != 0U) {
                context->lines.items[i].interface_depth = interface_depth;
                if (f2c_interface_end_line(line))
                    --interface_depth;
                continue;
            }
            if (derived_type_depth != 0U && f2c_starts_word(line, "end type")) {
                --derived_type_depth;
                continue;
            }
            if (f2c_starts_word(line, "type") &&
                *skip_declaration_space(line + strlen("type")) != '(' &&
                strstr(line + strlen("type"), "::") != NULL) {
                ++derived_type_depth;
                continue;
            }
            if (derived_type_depth != 0U)
                continue;
            if (!active->internal && f2c_starts_word(line, "contains")) {
                active->end = i;
                host_index = active_index;
                active_index = (size_t)-1;
            } else if (f2c_starts_word(line, "end") && !f2c_starts_word(line, "end if") &&
                       !f2c_starts_word(line, "end do") && !f2c_starts_word(line, "end select") &&
                       !f2c_starts_word(line, "end block") && !f2c_starts_word(line, "end where") &&
                       !f2c_starts_word(line, "end type")) {
                active->end = i;
                active_index = (size_t)-1;
            }
            continue;
        }
        if (host_index != (size_t)-1) {
            if (f2c_parse_unit_header(line, &unit)) {
                Buffer mangled = {0};
                char *source_name = unit.name;
                f2c_buffer_printf(&mangled, "%s__%s", context->units.items[host_index].name,
                                  source_name);
                unit.name = f2c_buffer_take(&mangled);
                unit.fortran_name = source_name;
                unit.internal = 1;
                unit.host_index = host_index;
                unit.begin = i;
                unit.end = context->lines.count;
                unit.options.source_name = f2c_strdup(context->lines.items[i].source_name);
                unit.options.source_form = F2C_SOURCE_AUTO;
                unit.options.emit_source_comments = context->lines.items[i].emit_source_comments;
                if (unit.name == NULL || unit.options.source_name == NULL)
                    return 0;
                if (!units_push(&context->units, unit)) {
                    free(unit.name);
                    free(unit.fortran_name);
                    free((char *)unit.options.source_name);
                    return 0;
                }
                active_index = context->units.count - 1U;
                interface_depth = 0U;
                derived_type_depth = 0U;
            } else if (f2c_starts_word(line, "end") && !f2c_starts_word(line, "end if") &&
                       !f2c_starts_word(line, "end do") && !f2c_starts_word(line, "end select") &&
                       !f2c_starts_word(line, "end block") && !f2c_starts_word(line, "end where") &&
                       !f2c_starts_word(line, "end type")) {
                host_index = (size_t)-1;
            }
            continue;
        }
        if (f2c_parse_unit_header(line, &unit)) {
            unit.begin = i;
            unit.end = context->lines.count;
            unit.host_index = (size_t)-1;
            unit.options.source_name = f2c_strdup(context->lines.items[i].source_name);
            unit.options.source_form = F2C_SOURCE_AUTO;
            unit.options.emit_source_comments = context->lines.items[i].emit_source_comments;
            if (unit.options.source_name == NULL)
                return 0;
            if (!units_push(&context->units, unit)) {
                free((char *)unit.options.source_name);
                return 0;
            }
            active_index = context->units.count - 1U;
            interface_depth = 0U;
            derived_type_depth = 0U;
        }
    }
    if (active_index != (size_t)-1) {
        Unit *active = &context->units.items[active_index];
        f2c_diagnostic(context, context->lines.items[active->begin].number, 1,
                       "unterminated program unit '%s'", active->name);
    } else if (host_index != (size_t)-1) {
        Unit *host = &context->units.items[host_index];
        f2c_diagnostic(context, context->lines.items[host->begin].number, 1,
                       "unterminated host program unit '%s'", host->name);
    }
    if (context->units.count == 0U && !f2c_has_supported_module(context)) {
        f2c_diagnostic(context, context->lines.count == 0U ? 1U : context->lines.items[0].number, 1,
                       "no PROGRAM, SUBROUTINE, or FUNCTION unit found");
    }
    return 1;
}

Symbol *f2c_find_symbol(Unit *unit, const char *name) {
    size_t i;
    if (unit == NULL || name == NULL)
        return NULL;
    for (i = 0U; i < unit->symbol_count; ++i) {
        if (strcmp(unit->symbols[i].name, name) == 0) {
            return &unit->symbols[i];
        }
    }
    return NULL;
}

F2cNamelistGroup *f2c_find_namelist(Unit *unit, const char *name) {
    size_t i;
    if (unit == NULL || name == NULL)
        return NULL;
    for (i = 0U; i < unit->namelist_count; ++i) {
        if (strcmp(unit->namelists[i].name, name) == 0)
            return &unit->namelists[i];
    }
    return NULL;
}

F2cDerivedType *f2c_find_derived_type(Unit *unit, const char *name) {
    size_t i;
    if (unit == NULL || name == NULL)
        return NULL;
    for (i = 0U; i < unit->derived_type_count; ++i) {
        if (strcmp(unit->derived_types[i].name, name) == 0)
            return &unit->derived_types[i];
    }
    for (i = 0U; i < unit->imported_derived_type_count; ++i) {
        if (strcmp(unit->imported_derived_types[i]->name, name) == 0)
            return unit->imported_derived_types[i];
    }
    return NULL;
}

static Symbol *ensure_symbol(Unit *unit, const char *name) {
    Symbol *symbol;
    Symbol *replacement;
    size_t capacity;
    if (name == NULL || name[0] == '\0')
        return NULL;
    symbol = f2c_find_symbol(unit, name);
    if (symbol != NULL) {
        return symbol;
    }
    if (unit->symbol_count == unit->symbol_capacity) {
        capacity = unit->symbol_capacity == 0U ? 16U : unit->symbol_capacity * 2U;
        replacement = (Symbol *)realloc(unit->symbols, capacity * sizeof(*replacement));
        if (replacement == NULL) {
            return NULL;
        }
        unit->symbols = replacement;
        unit->symbol_capacity = capacity;
    }
    symbol = &unit->symbols[unit->symbol_count++];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = f2c_strdup(name);
    {
        static const char *const keywords[] = {
            "auto",       "break",     "case",           "char",
            "const",      "continue",  "default",        "do",
            "double",     "else",      "enum",           "extern",
            "float",      "for",       "goto",           "if",
            "inline",     "int",       "long",           "register",
            "restrict",   "return",    "short",          "signed",
            "sizeof",     "static",    "struct",         "switch",
            "typedef",    "union",     "unsigned",       "void",
            "volatile",   "while",     "_alignas",       "_alignof",
            "_atomic",    "_bool",     "_complex",       "_generic",
            "_imaginary", "_noreturn", "_static_assert", "_thread_local",
            "abs",        "abort",     "calloc",         "cabs",
            "cabsf",      "cexp",      "cexpf",          "clog",
            "clogf",      "conj",      "conjf",          "cos",
            "cosf",       "csin",      "csinf",          "exit",
            "exp",        "expf",      "fabs",           "fabsf",
            "fclose",     "fopen",     "fprintf",        "free",
            "fscanf",     "log",       "logf",           "malloc",
            "memcpy",     "memmove",   "memset",         "pow",
            "powf",       "sin",       "sinf",           "sqrt",
            "sqrtf",      "strtod",    "strtof",         "strtol"};
        size_t keyword;
        int reserved = 0;
        for (keyword = 0U; keyword < sizeof(keywords) / sizeof(keywords[0]); ++keyword) {
            if (strcmp(name, keywords[keyword]) == 0) {
                reserved = 1;
                break;
            }
        }
        if (reserved) {
            Buffer c_name = {0};
            f2c_buffer_printf(&c_name, "%s_value", name);
            symbol->c_name = f2c_buffer_take(&c_name);
        } else {
            symbol->c_name = f2c_strdup(name);
        }
    }
    symbol->type = TYPE_UNKNOWN;
    symbol->value_category = F2C_VALUE_VARIABLE;
    symbol->shape.kind = F2C_SHAPE_SCALAR;
    return symbol->name == NULL || symbol->c_name == NULL ? NULL : symbol;
}

Symbol *f2c_ensure_symbol(Unit *unit, const char *name) { return ensure_symbol(unit, name); }

const char *f2c_symbol_c_name(Unit *unit, const Symbol *symbol) {
    (void)unit;
    return symbol->c_name;
}

static int parse_dimensions(Symbol *symbol, const char *open) {
    size_t count = 0U;
    char **parts = f2c_split_arguments(open, &count);
    size_t i;
    if (parts == NULL && count != 0U)
        return 0;
    if (count > F2C_MAX_RANK) {
        for (i = 0U; i < count; ++i)
            free(parts[i]);
        free(parts);
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        F2cLexer lexer;
        char *colon = NULL;
        int depth = 0;
        f2c_lexer_init(&lexer, parts[i], 1U, 1U);
        do {
            f2c_lexer_next(&lexer);
            if (lexer.token.kind == F2C_TOKEN_LEFT_PAREN ||
                lexer.token.kind == F2C_TOKEN_LEFT_BRACKET ||
                lexer.token.kind == F2C_TOKEN_ARRAY_BEGIN)
                ++depth;
            else if ((lexer.token.kind == F2C_TOKEN_RIGHT_PAREN ||
                      lexer.token.kind == F2C_TOKEN_RIGHT_BRACKET ||
                      lexer.token.kind == F2C_TOKEN_ARRAY_END) &&
                     depth > 0)
                --depth;
            else if (lexer.token.kind == F2C_TOKEN_COLON && depth == 0)
                colon = (char *)lexer.token.begin;
        } while (colon == NULL && lexer.token.kind != F2C_TOKEN_END &&
                 lexer.token.kind != F2C_TOKEN_INVALID);
        if (colon != NULL) {
            *colon = '\0';
            symbol->dimensions[i].lower =
                f2c_strdup(*f2c_trim(parts[i]) == '\0' ? "1" : f2c_trim(parts[i]));
            symbol->dimensions[i].upper =
                f2c_strdup(*f2c_trim(colon + 1) == '\0' ? "*" : f2c_trim(colon + 1));
            if (*f2c_trim(colon + 1) == '\0')
                symbol->dimensions[i].kind = (symbol->allocatable || symbol->pointer)
                                                 ? F2C_DIMENSION_DEFERRED
                                                 : F2C_DIMENSION_ASSUMED_SHAPE;
            else
                symbol->dimensions[i].kind = F2C_DIMENSION_EXPLICIT;
        } else {
            symbol->dimensions[i].lower = f2c_strdup("1");
            symbol->dimensions[i].upper = f2c_strdup(f2c_trim(parts[i]));
            symbol->dimensions[i].kind = strcmp(f2c_trim(parts[i]), "*") == 0
                                             ? F2C_DIMENSION_ASSUMED_SIZE
                                             : F2C_DIMENSION_EXPLICIT;
        }
        free(parts[i]);
    }
    free(parts);
    symbol->rank = count;
    f2c_shape_from_symbol(NULL, &symbol->shape, symbol);
    return 1;
}

static int derived_type_declaration_line(const char *line) {
    const char *cursor;
    if (!f2c_starts_word(line, "type"))
        return 0;
    cursor = skip_declaration_space(line + strlen("type"));
    return *cursor == '(' || *cursor == ',' || *cursor == ':' || isalpha((unsigned char)*cursor) ||
           *cursor == '_';
}

int f2c_declaration_line(const char *line) {
    size_t ignored;
    return parse_type_prefix(line, &ignored) != TYPE_UNKNOWN ||
           f2c_starts_word(line, "dimension") || f2c_starts_word(line, "parameter") ||
           f2c_starts_word(line, "implicit") || f2c_starts_word(line, "external") ||
           f2c_starts_word(line, "intrinsic") || f2c_starts_word(line, "optional") ||
           f2c_starts_word(line, "procedure") || f2c_starts_word(line, "import") ||
           f2c_interface_start_line(line) || f2c_interface_end_line(line) ||
           f2c_starts_word(line, "save") || f2c_starts_word(line, "equivalence") ||
           f2c_starts_word(line, "common") || f2c_starts_word(line, "namelist") ||
           derived_type_declaration_line(line) || f2c_starts_word(line, "end type");
}

static void parse_namelist_declaration(Context *context, Unit *unit, Line *source_line) {
    const char *cursor;
    if (!f2c_starts_word(source_line->text, "namelist"))
        return;
    cursor = skip_declaration_space(source_line->text + strlen("namelist"));
    while (*cursor != '\0') {
        const char *name_end;
        const char *members_end;
        char *name;
        char *members_text;
        char **members;
        size_t member_count = 0U;
        size_t i;
        F2cNamelistGroup *group;
        if (*cursor != '/') {
            f2c_diagnostic(context, source_line->number, 1,
                           "NAMELIST group must begin with '/name/'");
            return;
        }
        name_end = strchr(cursor + 1, '/');
        if (name_end == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "unterminated NAMELIST group name");
            return;
        }
        name = f2c_strdup_n(cursor + 1, (size_t)(name_end - cursor - 1));
        if (name == NULL)
            return;
        {
            char *clean = f2c_trim(name);
            if (clean != name)
                memmove(name, clean, strlen(clean) + 1U);
        }
        members_end = strchr(name_end + 1, '/');
        members_text =
            f2c_strdup_n(name_end + 1, members_end != NULL ? (size_t)(members_end - name_end - 1)
                                                           : strlen(name_end + 1));
        if (members_text == NULL) {
            free(name);
            return;
        }
        members = f2c_split_arguments(members_text, &member_count);
        group = f2c_find_namelist(unit, name);
        if (group != NULL) {
            f2c_diagnostic(context, source_line->number, 1, "duplicate NAMELIST group '%s'", name);
            for (i = 0U; i < member_count; ++i)
                free(members[i]);
            free(members);
            free(members_text);
            free(name);
            return;
        }
        if (unit->namelist_count == unit->namelist_capacity) {
            const size_t capacity =
                unit->namelist_capacity == 0U ? 4U : unit->namelist_capacity * 2U;
            F2cNamelistGroup *replacement =
                (F2cNamelistGroup *)realloc(unit->namelists, capacity * sizeof(*replacement));
            if (replacement == NULL) {
                for (i = 0U; i < member_count; ++i)
                    free(members[i]);
                free(members);
                free(members_text);
                free(name);
                return;
            }
            unit->namelists = replacement;
            unit->namelist_capacity = capacity;
        }
        group = &unit->namelists[unit->namelist_count++];
        memset(group, 0, sizeof(*group));
        group->name = name;
        group->members = members;
        group->member_count = member_count;
        for (i = 0U; i < member_count; ++i) {
            char *clean = f2c_trim(group->members[i]);
            size_t consumed = 0U;
            char *identifier;
            if (clean != group->members[i])
                memmove(group->members[i], clean, strlen(clean) + 1U);
            identifier = f2c_identifier(group->members[i], &consumed);
            if (identifier == NULL || consumed != strlen(group->members[i])) {
                f2c_diagnostic(context, source_line->number, 1,
                               "NAMELIST object '%s' is not a simple named entity",
                               group->members[i]);
            } else if (ensure_symbol(unit, identifier) == NULL) {
                f2c_diagnostic(context, source_line->number, 1,
                               "out of memory recording NAMELIST object");
            }
            free(identifier);
        }
        free(members_text);
        if (members_end == NULL)
            break;
        cursor = members_end;
    }
}

static void parse_common_declaration(Context *context, Unit *unit, Line *source_line) {
    const char *first_slash;
    const char *second_slash;
    char *block;
    char *members_text;
    char **members;
    size_t count = 0U;
    size_t i;
    if (!f2c_starts_word(source_line->text, "common"))
        return;
    first_slash = strchr(source_line->text + strlen("common"), '/');
    second_slash = first_slash != NULL ? strchr(first_slash + 1, '/') : NULL;
    if (first_slash == NULL || second_slash == NULL) {
        f2c_diagnostic(context, source_line->number, 1, "blank COMMON is not yet supported");
        return;
    }
    block = f2c_strdup_n(first_slash + 1, (size_t)(second_slash - first_slash - 1));
    members_text = f2c_strdup(second_slash + 1);
    if (block == NULL || members_text == NULL) {
        free(block);
        free(members_text);
        f2c_diagnostic(context, source_line->number, 1, "out of memory parsing COMMON");
        return;
    }
    {
        char *clean = f2c_trim(block);
        if (clean != block)
            memmove(block, clean, strlen(clean) + 1U);
    }
    members = f2c_split_arguments(members_text, &count);
    for (i = 0U; i < count; ++i) {
        size_t consumed = 0U;
        char *name = f2c_identifier(f2c_trim(members[i]), &consumed);
        Symbol *symbol = name != NULL ? ensure_symbol(unit, name) : NULL;
        if (symbol == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "malformed COMMON member");
        } else {
            Buffer c_name = {0};
            free(symbol->common_block);
            symbol->common_block = f2c_strdup(block);
            symbol->common_index = i;
            f2c_buffer_printf(&c_name, "f2c_common_%s.field_%zu", block, i);
            free(symbol->c_name);
            symbol->c_name = f2c_buffer_take(&c_name);
        }
        free(name);
        free(members[i]);
    }
    free(members);
    free(members_text);
    free(block);
}

static Type normalize_kind_type(Type type) {
    if (type == TYPE_DOUBLE_COMPLEX)
        return TYPE_DOUBLE;
    if (type == TYPE_COMPLEX)
        return TYPE_REAL;
    return type;
}

static Type kind_type_from_initializer(Unit *unit, const char *initializer) {
    const char *cursor = initializer;
    const char *open;
    const char *close;
    char *argument;
    Type type;
    int depth = 0;
    if (cursor == NULL)
        return TYPE_UNKNOWN;
    while (isspace((unsigned char)*cursor))
        ++cursor;
    if (strcmp(cursor, "real64") == 0 || strcmp(cursor, "dp") == 0)
        return TYPE_DOUBLE;
    if (strcmp(cursor, "real32") == 0 || strcmp(cursor, "sp") == 0)
        return TYPE_REAL;
    {
        Symbol *kind_symbol = f2c_find_symbol(unit, cursor);
        if (kind_symbol != NULL && kind_symbol->kind_type != TYPE_UNKNOWN)
            return kind_symbol->kind_type;
    }
    if (!f2c_starts_word(cursor, "kind"))
        return TYPE_UNKNOWN;
    open = strchr(cursor + strlen("kind"), '(');
    if (open == NULL)
        return TYPE_UNKNOWN;
    close = open;
    do {
        if (*close == '(')
            ++depth;
        else if (*close == ')')
            --depth;
        ++close;
    } while (*close != '\0' && depth != 0);
    if (depth != 0)
        return TYPE_UNKNOWN;
    argument = f2c_strdup_n(open + 1, (size_t)((close - 1) - (open + 1)));
    if (argument == NULL)
        return TYPE_UNKNOWN;
    type = normalize_kind_type(f2c_expression_type(unit, f2c_trim(argument)));
    free(argument);
    return type == TYPE_REAL || type == TYPE_DOUBLE ? type : TYPE_UNKNOWN;
}

static Type declaration_kind_type(Unit *unit, const char *declaration, size_t type_length) {
    const char *open = strchr(declaration, '(');
    const char *end = declaration + type_length;
    const char *close;
    char *selector;
    char *clean;
    Symbol *symbol;
    Type type = TYPE_UNKNOWN;
    if (open == NULL || open >= end)
        return TYPE_UNKNOWN;
    close = open + 1;
    while (close < end && *close != ')')
        ++close;
    if (close >= end)
        return TYPE_UNKNOWN;
    selector = f2c_strdup_n(open + 1, (size_t)(close - (open + 1)));
    if (selector == NULL)
        return TYPE_UNKNOWN;
    clean = f2c_trim(selector);
    if (strncmp(clean, "kind", strlen("kind")) == 0) {
        char *kind_value = clean + strlen("kind");
        while (isspace((unsigned char)*kind_value))
            ++kind_value;
        if (*kind_value == '=')
            clean = f2c_trim(kind_value + 1);
    }
    type = kind_type_from_initializer(unit, clean);
    symbol = type == TYPE_UNKNOWN ? f2c_find_symbol(unit, clean) : NULL;
    if (symbol != NULL)
        type = symbol->kind_type;
    if (type == TYPE_UNKNOWN) {
        if (strcmp(clean, "8") == 0 || strcmp(clean, "dp") == 0 || strcmp(clean, "real64") == 0)
            type = TYPE_DOUBLE;
        else if (strcmp(clean, "4") == 0 || strcmp(clean, "sp") == 0 ||
                 strcmp(clean, "real32") == 0)
            type = TYPE_REAL;
    }
    free(selector);
    return type;
}

static int kind_value_from_selector(Unit *unit, const char *selector) {
    char *copy;
    char *clean;
    char *end = NULL;
    long value;
    Symbol *symbol;
    Type kind_type;
    int64_t constant;
    if (selector == NULL)
        return 0;
    copy = f2c_strdup(selector);
    if (copy == NULL)
        return 0;
    clean = f2c_trim(copy);
    value = strtol(clean, &end, 10);
    if (end != clean) {
        end = f2c_trim(end);
        if (*end == '\0' && value > 0 && value <= INT_MAX) {
            free(copy);
            return (int)value;
        }
    }
    if (strcmp(clean, "int8") == 0) {
        free(copy);
        return 1;
    }
    if (strcmp(clean, "int16") == 0) {
        free(copy);
        return 2;
    }
    if (strcmp(clean, "int32") == 0 || strcmp(clean, "real32") == 0 || strcmp(clean, "sp") == 0) {
        free(copy);
        return 4;
    }
    if (strcmp(clean, "int64") == 0 || strcmp(clean, "real64") == 0 || strcmp(clean, "dp") == 0) {
        free(copy);
        return 8;
    }
    symbol = unit != NULL ? f2c_find_symbol(unit, clean) : NULL;
    if (symbol != NULL) {
        if (symbol->kind_type != TYPE_UNKNOWN) {
            value = f2c_default_kind(symbol->kind_type);
            free(copy);
            return (int)value;
        }
        if (symbol->initializer != NULL &&
            f2c_evaluate_integer_text(unit, symbol->initializer, &constant) && constant > 0 &&
            constant <= INT_MAX) {
            free(copy);
            return (int)constant;
        }
    }
    kind_type = kind_type_from_initializer(unit, clean);
    free(copy);
    return kind_type != TYPE_UNKNOWN ? f2c_default_kind(kind_type) : 0;
}

static int declaration_kind_value(Unit *unit, const char *declaration, size_t type_length,
                                  Type type) {
    const char *end = declaration + type_length;
    const char *open = (const char *)memchr(declaration, '(', type_length);
    const char *star = (const char *)memchr(declaration, '*', type_length);
    int kind = 0;
    if (open != NULL && open < end) {
        char **selectors;
        size_t count = 0U;
        size_t i;
        selectors = f2c_split_arguments(open, &count);
        for (i = 0U; i < count; ++i) {
            char *selector = f2c_trim(selectors[i]);
            char *equals = strchr(selector, '=');
            if (equals != NULL) {
                char *key;
                *equals = '\0';
                key = f2c_trim(selector);
                if (strcmp(key, "kind") == 0)
                    kind = kind_value_from_selector(unit, f2c_trim(equals + 1));
            } else if (type != TYPE_CHARACTER && i == 0U) {
                kind = kind_value_from_selector(unit, selector);
            }
            free(selectors[i]);
        }
        free(selectors);
    } else if (star != NULL && type != TYPE_CHARACTER) {
        char *selector = f2c_strdup_n(star + 1, (size_t)(end - star - 1));
        if (selector != NULL) {
            kind = kind_value_from_selector(unit, selector);
            free(selector);
            if ((type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX) && kind > 0)
                kind /= 2;
        }
    }
    if (kind == 0)
        kind = f2c_default_kind(type);
    return kind;
}

static F2cIntent parse_intent_attribute(const char *attributes) {
    const char *cursor = attributes;
    while ((cursor = strstr(cursor, "intent")) != NULL) {
        const char *value = cursor + strlen("intent");
        if ((cursor != attributes && (isalnum((unsigned char)cursor[-1]) || cursor[-1] == '_')) ||
            (isalnum((unsigned char)*value) || *value == '_')) {
            cursor = value;
            continue;
        }
        while (isspace((unsigned char)*value))
            ++value;
        if (*value++ != '(') {
            cursor = value;
            continue;
        }
        while (isspace((unsigned char)*value))
            ++value;
        if (strncmp(value, "inout", strlen("inout")) == 0)
            return F2C_INTENT_INOUT;
        if (strncmp(value, "out", strlen("out")) == 0)
            return F2C_INTENT_OUT;
        if (strncmp(value, "in", strlen("in")) == 0)
            return F2C_INTENT_IN;
        cursor = value;
    }
    return F2C_INTENT_UNSPECIFIED;
}

static int has_attribute_word(const char *attributes, const char *word) {
    const size_t length = strlen(word);
    const char *match = attributes;
    while ((match = strstr(match, word)) != NULL) {
        const char before = match == attributes ? '\0' : match[-1];
        const char after = match[length];
        if ((match == attributes || (!isalnum((unsigned char)before) && before != '_')) &&
            !isalnum((unsigned char)after) && after != '_')
            return 1;
        match += length;
    }
    return 0;
}

static void parse_declaration(Context *context, Unit *unit, Line *source_line) {
    char *copy = f2c_strdup(source_line->text);
    char *cursor = copy;
    size_t type_length = 0U;
    Type type = parse_type_prefix(cursor, &type_length);
    char *double_colon;
    char **variables;
    size_t count = 0U;
    size_t i;
    F2cIntent intent = F2C_INTENT_UNSPECIFIED;
    int parameter = 0;
    int allocatable = 0;
    int pointer = 0;
    int polymorphic = 0;
    int target = 0;
    int optional = 0;
    int saved = 0;
    char *attribute_dimensions = NULL;
    char *character_length = NULL;
    int declared_kind;
    char *derived_type_name = NULL;
    if (copy == NULL || type == TYPE_UNKNOWN) {
        free(copy);
        return;
    }
    polymorphic = f2c_starts_word(copy, "class");
    if (type == TYPE_REAL || type == TYPE_COMPLEX) {
        Type kind_type = declaration_kind_type(unit, copy, type_length);
        if (kind_type == TYPE_DOUBLE)
            type = type == TYPE_REAL ? TYPE_DOUBLE : TYPE_DOUBLE_COMPLEX;
    }
    declared_kind = declaration_kind_value(unit, copy, type_length, type);
    if (type == TYPE_DERIVED) {
        const char *open = strchr(copy, '(');
        const char *close = open != NULL ? strchr(open + 1, ')') : NULL;
        if (open != NULL && close != NULL) {
            derived_type_name = f2c_strdup_n(open + 1, (size_t)(close - open - 1));
            if (derived_type_name != NULL) {
                char *clean_name = f2c_trim(derived_type_name);
                if (clean_name != derived_type_name)
                    memmove(derived_type_name, clean_name, strlen(clean_name) + 1U);
            }
        }
        if (derived_type_name == NULL || f2c_find_derived_type(unit, derived_type_name) == NULL) {
            f2c_diagnostic(context, source_line->number, 1,
                           "derived type '%s' is not declared in this scope",
                           derived_type_name != NULL ? derived_type_name : "<invalid>");
        }
    }
    if (type == TYPE_CHARACTER) {
        character_length = parse_character_length_selector(copy + strlen("character"), NULL);
    }
    cursor += type_length;
    double_colon = strstr(cursor, "::");
    if (double_colon != NULL) {
        char *dimension_attribute = strstr(cursor, "dimension");
        *double_colon = '\0';
        intent = parse_intent_attribute(cursor);
        parameter = has_attribute_word(cursor, "parameter");
        allocatable = has_attribute_word(cursor, "allocatable");
        pointer = has_attribute_word(cursor, "pointer");
        target = has_attribute_word(cursor, "target");
        optional = has_attribute_word(cursor, "optional");
        saved = has_attribute_word(cursor, "save");
        if (dimension_attribute != NULL) {
            char *dimension_open = strchr(dimension_attribute, '(');
            if (dimension_open != NULL)
                attribute_dimensions = dimension_open;
        }
        cursor = double_colon + 2;
    }
    cursor = f2c_trim(cursor);
    while (*cursor == ',') {
        ++cursor;
        cursor = f2c_trim(cursor);
    }
    variables = f2c_split_arguments(cursor, &count);
    for (i = 0U; i < count; ++i) {
        char *item = variables[i];
        char *equals = strchr(item, '=');
        char *open = strchr(item, '(');
        char *length_spec = type == TYPE_CHARACTER ? strchr(item, '*') : NULL;
        char *name_end = item + strlen(item);
        char *name;
        Symbol *symbol;
        char *clean;
        char *item_character_length = NULL;
        if (open != NULL && open < name_end)
            name_end = open;
        if (equals != NULL && equals < name_end)
            name_end = equals;
        if (length_spec != NULL && length_spec < name_end) {
            const char *length_after = length_spec;
            name_end = length_spec;
            item_character_length = parse_character_length_selector(length_spec, &length_after);
            open = strchr(length_after, '(');
        }
        name = f2c_strdup_n(item, (size_t)(name_end - item));
        if (name == NULL) {
            free(item_character_length);
            continue;
        }
        clean = f2c_trim(name);
        if (clean != name) {
            memmove(name, clean, strlen(clean) + 1U);
        }
        {
            size_t identifier_length = 0U;
            char *identifier = f2c_identifier(name, &identifier_length);
            const int valid = identifier != NULL && identifier_length == strlen(name);
            free(identifier);
            if (!valid) {
                f2c_diagnostic(context, source_line->number, 1,
                               "invalid or empty declaration target '%s'", name);
                free(name);
                free(item_character_length);
                continue;
            }
        }
        symbol = ensure_symbol(unit, name);
        if (symbol == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "out of memory while declaring '%s'",
                           name);
            free(name);
            free(item_character_length);
            continue;
        }
        symbol->type = type;
        symbol->kind = declared_kind;
        symbol->value_category = parameter ? F2C_VALUE_CONSTANT : F2C_VALUE_VARIABLE;
        if (symbol->external)
            symbol->external_subroutine = 0;
        symbol->declaration_line = source_line->number;
        symbol->intent = intent;
        symbol->parameter = parameter;
        symbol->allocatable = allocatable;
        symbol->pointer = pointer;
        symbol->polymorphic = polymorphic;
        symbol->target = target;
        symbol->optional = optional;
        symbol->saved |= saved;
        if (type == TYPE_DERIVED) {
            Buffer c_type = {0};
            free(symbol->derived_type_name);
            symbol->derived_type_name =
                derived_type_name != NULL ? f2c_strdup(derived_type_name) : NULL;
            symbol->derived_type = f2c_find_derived_type(unit, derived_type_name);
            if (symbol->derived_type != NULL && symbol->derived_type->c_name != NULL)
                f2c_buffer_append(&c_type, symbol->derived_type->c_name);
            else
                f2c_buffer_printf(&c_type, "f2c_type_%s_%s",
                                  unit->name != NULL ? unit->name : "scope",
                                  derived_type_name != NULL ? derived_type_name : "invalid");
            free(symbol->c_type);
            symbol->c_type = f2c_buffer_take(&c_type);
        }
        if (type == TYPE_CHARACTER && character_length != NULL) {
            free(symbol->character_length);
            symbol->character_length = f2c_strdup(character_length);
        } else if (type == TYPE_CHARACTER && item_character_length != NULL) {
            free(symbol->character_length);
            symbol->character_length = f2c_strdup(item_character_length);
        }
        if (type == TYPE_CHARACTER && symbol->character_length != NULL &&
            strcmp(symbol->character_length, ":") == 0) {
            if (!allocatable && !pointer) {
                f2c_diagnostic(context, source_line->number, 1,
                               "deferred-length CHARACTER entity '%s' must be ALLOCATABLE or "
                               "POINTER",
                               symbol->name);
            } else {
                symbol->deferred_character = 1;
            }
        }
        if (open != NULL && (equals == NULL || open < equals)) {
            (void)parse_dimensions(symbol, open);
        } else if (attribute_dimensions != NULL) {
            (void)parse_dimensions(symbol, attribute_dimensions);
        }
        if (equals != NULL) {
            free(symbol->initializer);
            symbol->initializer = f2c_strdup(f2c_trim(equals + 1));
            symbol->kind_type = kind_type_from_initializer(unit, symbol->initializer);
        }
        f2c_shape_from_symbol(unit, &symbol->shape, symbol);
        free(name);
        free(item_character_length);
    }
    for (i = 0U; i < count; ++i) {
        free(variables[i]);
    }
    free(variables);
    free(character_length);
    free(derived_type_name);
    free(copy);
}

static void parse_optional_declaration(Context *context, Unit *unit, Line *source_line) {
    char *copy;
    char *cursor;
    char **names;
    size_t count = 0U;
    size_t i;
    if (!f2c_starts_word(source_line->text, "optional"))
        return;
    copy = f2c_strdup(source_line->text + strlen("optional"));
    if (copy == NULL) {
        f2c_diagnostic(context, source_line->number, 1, "out of memory in OPTIONAL declaration");
        return;
    }
    cursor = f2c_trim(copy);
    if (cursor[0] == ':' && cursor[1] == ':')
        cursor = f2c_trim(cursor + 2);
    names = f2c_split_arguments(cursor, &count);
    if (count == 0U)
        f2c_diagnostic(context, source_line->number, 1, "OPTIONAL declaration has no entities");
    for (i = 0U; i < count; ++i) {
        size_t consumed = 0U;
        char *name = f2c_identifier(f2c_trim(names[i]), &consumed);
        Symbol *symbol = name != NULL && consumed == strlen(f2c_trim(names[i]))
                             ? ensure_symbol(unit, name)
                             : NULL;
        if (symbol == NULL) {
            f2c_diagnostic(context, source_line->number, 1,
                           "malformed OPTIONAL declaration entity '%s'", names[i]);
        } else {
            symbol->optional = 1;
            symbol->declaration_line = source_line->number;
        }
        free(name);
        free(names[i]);
    }
    free(names);
    free(copy);
}

static int line_in_derived_type(const Unit *unit, size_t line_index) {
    size_t i;
    for (i = 0U; i < unit->derived_type_count; ++i) {
        if (line_index >= unit->derived_types[i].begin && line_index <= unit->derived_types[i].end)
            return 1;
    }
    return 0;
}

static F2cTypeBinding *append_type_binding(F2cDerivedType *derived) {
    F2cTypeBinding *replacement;
    size_t capacity;
    if (derived->binding_count == derived->binding_capacity) {
        capacity = derived->binding_capacity == 0U ? 4U : derived->binding_capacity * 2U;
        replacement = (F2cTypeBinding *)realloc(derived->bindings, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return NULL;
        derived->bindings = replacement;
        derived->binding_capacity = capacity;
    }
    memset(&derived->bindings[derived->binding_count], 0,
           sizeof(derived->bindings[derived->binding_count]));
    return &derived->bindings[derived->binding_count++];
}

static int type_binding_attribute(Context *context, const Line *line, F2cTypeBinding *binding,
                                  char *attribute) {
    char *clean = f2c_trim(attribute);
    if (strcmp(clean, "deferred") == 0) {
        binding->deferred = 1;
    } else if (strcmp(clean, "nopass") == 0) {
        binding->nopass = 1;
    } else if (strcmp(clean, "pass") == 0) {
        binding->nopass = 0;
    } else if (f2c_starts_word(clean, "pass") && strchr(clean, '(') != NULL) {
        const char *open = strchr(clean, '(');
        const char *close = strrchr(clean, ')');
        size_t consumed = 0U;
        char *text = close != NULL && close > open
                         ? f2c_strdup_n(open + 1, (size_t)(close - open - 1))
                         : NULL;
        char *name = text != NULL ? f2c_identifier(f2c_trim(text), &consumed) : NULL;
        if (name == NULL || consumed != strlen(f2c_trim(text != NULL ? text : (char *)""))) {
            f2c_diagnostic(context, line->number, 1, "malformed PASS attribute '%s'", clean);
            free(name);
            free(text);
            return 0;
        }
        free(binding->pass_name);
        binding->pass_name = name;
        free(text);
    } else if (strcmp(clean, "non_overridable") == 0) {
        binding->non_overridable = 1;
    } else if (strcmp(clean, "public") != 0 && strcmp(clean, "private") != 0) {
        f2c_diagnostic(context, line->number, 1, "unsupported type-bound PROCEDURE attribute '%s'",
                       clean);
        return 0;
    }
    return 1;
}

static void parse_type_bound_procedure(Context *context, F2cDerivedType *derived,
                                       const Line *line) {
    const char *cursor = skip_declaration_space(line->text + strlen("procedure"));
    const char *double_colon = strstr(cursor, "::");
    const char *attribute_begin = cursor;
    char *interface_name = NULL;
    char *attributes = NULL;
    char **attribute_items = NULL;
    char **entities = NULL;
    size_t attribute_count = 0U;
    size_t entity_count = 0U;
    size_t i;
    if (*cursor == '(') {
        const char *close = strchr(cursor + 1, ')');
        if (close == NULL || (double_colon != NULL && close > double_colon)) {
            f2c_diagnostic(context, line->number, 1, "malformed type-bound PROCEDURE interface");
            return;
        }
        interface_name = f2c_strdup_n(cursor + 1, (size_t)(close - cursor - 1));
        attribute_begin = close + 1;
    }
    if (double_colon == NULL) {
        f2c_diagnostic(context, line->number, 1, "type-bound PROCEDURE declaration requires '::'");
        free(interface_name);
        return;
    }
    attributes = f2c_strdup_n(attribute_begin, (size_t)(double_colon - attribute_begin));
    attribute_items =
        attributes != NULL ? f2c_split_comma_list(attributes, &attribute_count) : NULL;
    entities = f2c_split_comma_list(double_colon + 2, &entity_count);
    if (entities == NULL || entity_count == 0U) {
        f2c_diagnostic(context, line->number, 1,
                       "type-bound PROCEDURE declaration has no bindings");
        goto cleanup;
    }
    for (i = 0U; i < entity_count; ++i) {
        char *entity = f2c_trim(entities[i]);
        char *arrow = strstr(entity, "=>");
        size_t consumed = 0U;
        char *name;
        char *target;
        F2cTypeBinding *binding;
        if (arrow != NULL)
            *arrow = '\0';
        name = f2c_identifier(f2c_trim(entity), &consumed);
        target = arrow != NULL ? f2c_identifier(f2c_trim(arrow + 2), &consumed) : NULL;
        if (name == NULL || (arrow != NULL && target == NULL)) {
            f2c_diagnostic(context, line->number, 1, "malformed type-bound PROCEDURE binding '%s'",
                           entities[i]);
            free(name);
            free(target);
            continue;
        }
        binding = append_type_binding(derived);
        if (binding == NULL) {
            f2c_diagnostic(context, line->number, 1,
                           "out of memory recording type-bound procedure");
            free(name);
            free(target);
            continue;
        }
        binding->name = name;
        binding->target_name = target != NULL ? target : f2c_strdup(name);
        binding->interface_name =
            interface_name != NULL ? f2c_strdup(f2c_trim(interface_name)) : NULL;
        binding->owner = derived;
        binding->storage_owner = derived;
        binding->procedure.name = f2c_strdup(name);
        binding->procedure.c_name = f2c_strdup(name);
        binding->procedure.procedure_pointer = 1;
        binding->procedure.type_bound = 1;
        binding->procedure.derived_owner = derived;
        for (size_t attribute = 0U; attribute < attribute_count; ++attribute)
            (void)type_binding_attribute(context, line, binding, attribute_items[attribute]);
        binding->procedure.type_bound_deferred = binding->deferred;
        binding->procedure.type_bound_nopass = binding->nopass;
        if (binding->deferred && arrow != NULL)
            f2c_diagnostic(context, line->number, 1,
                           "DEFERRED binding '%s' cannot specify an implementation", name);
        if (!binding->deferred && binding->target_name == NULL)
            f2c_diagnostic(context, line->number, 1, "binding '%s' has no implementation", name);
    }

cleanup:
    for (i = 0U; i < attribute_count; ++i)
        free(attribute_items[i]);
    for (i = 0U; i < entity_count; ++i)
        free(entities[i]);
    free(attribute_items);
    free(entities);
    free(attributes);
    free(interface_name);
}

static void parse_defined_io_generic(Context *context, F2cDerivedType *derived, const Line *line) {
    const char *double_colon = strstr(line->text, "::");
    char *copy;
    char *arrow;
    char *generic;
    char *binding;
    F2cDefinedIoKind kind;
    if (double_colon == NULL) {
        f2c_diagnostic(context, line->number, 1, "type-bound GENERIC declaration requires '::'");
        return;
    }
    copy = f2c_strdup(double_colon + 2);
    arrow = copy != NULL ? strstr(copy, "=>") : NULL;
    if (arrow == NULL) {
        free(copy);
        return;
    }
    *arrow = '\0';
    generic = f2c_trim(copy);
    binding = f2c_trim(arrow + 2);
    if (strcmp(generic, "read(formatted)") == 0)
        kind = F2C_DEFINED_IO_READ_FORMATTED;
    else if (strcmp(generic, "write(formatted)") == 0)
        kind = F2C_DEFINED_IO_WRITE_FORMATTED;
    else if (strcmp(generic, "read(unformatted)") == 0)
        kind = F2C_DEFINED_IO_READ_UNFORMATTED;
    else if (strcmp(generic, "write(unformatted)") == 0)
        kind = F2C_DEFINED_IO_WRITE_UNFORMATTED;
    else {
        free(copy);
        return;
    }
    if (strchr(binding, ',') != NULL || *binding == '\0') {
        f2c_diagnostic(context, line->number, 1,
                       "defined I/O generic must resolve to one specific binding");
    } else if (derived->defined_io_bindings[kind] != NULL) {
        f2c_diagnostic(context, line->number, 1, "duplicate defined I/O generic '%s'", generic);
    } else {
        derived->defined_io_bindings[kind] = f2c_strdup(binding);
    }
    free(copy);
}

static void parse_derived_type_definitions(Context *context, Unit *unit) {
    size_t line_index;
    size_t type_index;
    for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
        const char *text = f2c_trim(context->lines.items[line_index].text);
        const char *double_colon;
        const char *name_text;
        size_t consumed = 0U;
        char *name;
        size_t end;
        F2cDerivedType *replacement;
        F2cDerivedType *derived;
        if (!f2c_starts_word(text, "type") || *skip_declaration_space(text + strlen("type")) == '(')
            continue;
        double_colon = strstr(text + strlen("type"), "::");
        name_text = double_colon != NULL ? skip_declaration_space(double_colon + 2) : NULL;
        name = name_text != NULL ? f2c_identifier(name_text, &consumed) : NULL;
        if (name == NULL || consumed == 0U) {
            free(name);
            continue;
        }
        for (end = line_index + 1U; end < unit->end; ++end) {
            if (f2c_starts_word(context->lines.items[end].text, "end type"))
                break;
        }
        if (end == unit->end) {
            f2c_diagnostic(context, context->lines.items[line_index].number, 1,
                           "unterminated derived-type definition '%s'", name);
            free(name);
            continue;
        }
        if (f2c_find_derived_type(unit, name) != NULL) {
            f2c_diagnostic(context, context->lines.items[line_index].number, 1,
                           "duplicate derived-type definition '%s'", name);
            free(name);
            line_index = end;
            continue;
        }
        if (unit->derived_type_count == unit->derived_type_capacity) {
            const size_t capacity =
                unit->derived_type_capacity == 0U ? 4U : unit->derived_type_capacity * 2U;
            replacement =
                (F2cDerivedType *)realloc(unit->derived_types, capacity * sizeof(*replacement));
            if (replacement == NULL) {
                free(name);
                return;
            }
            unit->derived_types = replacement;
            unit->derived_type_capacity = capacity;
        }
        derived = &unit->derived_types[unit->derived_type_count++];
        memset(derived, 0, sizeof(*derived));
        derived->name = name;
        {
            Buffer c_name = {0};
            f2c_buffer_printf(&c_name, "f2c_type_%s_%s", unit->name != NULL ? unit->name : "scope",
                              name);
            derived->c_name = f2c_buffer_take(&c_name);
        }
        derived->begin = line_index;
        derived->end = end;
        derived->abstract_type = strstr(text, "abstract") != NULL;
        line_index = end;
    }
    for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
        F2cDerivedType *derived = &unit->derived_types[type_index];
        Unit component_scope;
        const char *header = context->lines.items[derived->begin].text;
        const char *extends = strstr(header, "extends");
        if (extends != NULL) {
            const char *open = strchr(extends + strlen("extends"), '(');
            const char *close = open != NULL ? strchr(open + 1, ')') : NULL;
            if (open != NULL && close != NULL) {
                derived->parent_name = f2c_strdup_n(open + 1, (size_t)(close - open - 1));
                if (derived->parent_name != NULL) {
                    char *clean = f2c_trim(derived->parent_name);
                    if (clean != derived->parent_name)
                        memmove(derived->parent_name, clean, strlen(clean) + 1U);
                    derived->parent = f2c_find_derived_type(unit, derived->parent_name);
                }
            }
            if (derived->parent == NULL) {
                f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                               "EXTENDS parent '%s' is not a visible derived type",
                               derived->parent_name != NULL ? derived->parent_name : "<invalid>");
            } else if (derived->parent == derived) {
                f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                               "derived type '%s' cannot extend itself", derived->name);
                derived->parent = NULL;
            }
        }
        memset(&component_scope, 0, sizeof(component_scope));
        component_scope.name = unit->name;
        component_scope.signature_host = unit;
        component_scope.derived_types = unit->derived_types;
        component_scope.derived_type_count = unit->derived_type_count;
        int in_bindings = 0;
        for (line_index = derived->begin + 1U; line_index < derived->end; ++line_index) {
            Line *line = &context->lines.items[line_index];
            if (f2c_starts_word(line->text, "contains")) {
                in_bindings = 1;
                continue;
            }
            if (f2c_starts_word(line->text, "final")) {
                const char *cursor = line->text + strlen("final");
                char **names;
                size_t count = 0U;
                size_t finalizer;
                cursor = skip_declaration_space(cursor);
                if (cursor[0] == ':' && cursor[1] == ':')
                    cursor = skip_declaration_space(cursor + 2);
                names = f2c_split_comma_list(cursor, &count);
                for (finalizer = 0U; finalizer < count; ++finalizer) {
                    size_t consumed = 0U;
                    char *clean = f2c_trim(names[finalizer]);
                    char *name = f2c_identifier(clean, &consumed);
                    char **replacement;
                    if (name == NULL || consumed != strlen(clean)) {
                        f2c_diagnostic(context, line->number, 1,
                                       "malformed FINAL procedure name '%s'", clean);
                        free(name);
                    } else {
                        replacement =
                            (char **)realloc(derived->finalizers, (derived->finalizer_count + 1U) *
                                                                      sizeof(*replacement));
                        if (replacement == NULL) {
                            free(name);
                        } else {
                            derived->finalizers = replacement;
                            derived->finalizers[derived->finalizer_count++] = name;
                        }
                    }
                    free(names[finalizer]);
                }
                free(names);
                continue;
            }
            if (in_bindings && f2c_starts_word(line->text, "procedure")) {
                parse_type_bound_procedure(context, derived, line);
                continue;
            }
            if (in_bindings && f2c_starts_word(line->text, "generic")) {
                parse_defined_io_generic(context, derived, line);
                continue;
            }
            if (in_bindings)
                continue;
            f2c_parse_procedure_declaration(context, &component_scope, line);
            parse_declaration(context, &component_scope, line);
        }
        derived->components = component_scope.symbols;
        derived->component_count = component_scope.symbol_count;
        derived->component_capacity = component_scope.symbol_capacity;
        for (line_index = 0U; line_index < derived->component_count; ++line_index)
            derived->components[line_index].derived_owner = derived;
    }
}

static void parse_external_declaration(Context *context, Unit *unit, Line *source_line) {
    char *copy;
    char *cursor;
    char **names;
    size_t count = 0U;
    size_t i;
    if (!f2c_starts_word(source_line->text, "external"))
        return;
    copy = f2c_strdup(source_line->text + strlen("external"));
    if (copy == NULL) {
        f2c_diagnostic(context, source_line->number, 1, "out of memory in EXTERNAL declaration");
        return;
    }
    cursor = f2c_trim(copy);
    if (cursor[0] == ':' && cursor[1] == ':')
        cursor = f2c_trim(cursor + 2);
    names = f2c_split_arguments(cursor, &count);
    for (i = 0U; i < count; ++i) {
        Symbol *symbol = ensure_symbol(unit, f2c_trim(names[i]));
        if (symbol == NULL) {
            f2c_diagnostic(context, source_line->number, 1,
                           "out of memory in EXTERNAL declaration");
        } else {
            symbol->external = 1;
            symbol->external_declared = 1;
            if (symbol->type == TYPE_UNKNOWN)
                symbol->external_subroutine = 1;
        }
        free(names[i]);
    }
    free(names);
    free(copy);
}

static void parse_dimension_declaration(Context *context, Unit *unit, Line *source_line) {
    char *copy;
    char *cursor;
    char **items;
    size_t count = 0U;
    size_t i;
    if (!f2c_starts_word(source_line->text, "dimension"))
        return;
    copy = f2c_strdup(source_line->text + strlen("dimension"));
    if (copy == NULL) {
        f2c_diagnostic(context, source_line->number, 1, "out of memory in DIMENSION declaration");
        return;
    }
    cursor = f2c_trim(copy);
    if (cursor[0] == ':' && cursor[1] == ':')
        cursor = f2c_trim(cursor + 2);
    items = f2c_split_arguments(cursor, &count);
    for (i = 0U; i < count; ++i) {
        char *open = strchr(items[i], '(');
        size_t length = 0U;
        char *name = f2c_identifier(f2c_trim(items[i]), &length);
        Symbol *symbol = name != NULL ? ensure_symbol(unit, name) : NULL;
        if (symbol == NULL || open == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "malformed DIMENSION declaration");
        } else {
            (void)parse_dimensions(symbol, open);
        }
        free(name);
        free(items[i]);
    }
    free(items);
    free(copy);
}

static void parse_parameter_declaration(Context *context, Unit *unit, Line *source_line) {
    const char *open;
    char **assignments;
    size_t count = 0U;
    size_t i;
    if (!f2c_starts_word(source_line->text, "parameter"))
        return;
    open = strchr(source_line->text, '(');
    if (open == NULL) {
        f2c_diagnostic(context, source_line->number, 1, "malformed PARAMETER declaration");
        return;
    }
    assignments = f2c_split_arguments(open, &count);
    for (i = 0U; i < count; ++i) {
        char *equals = strchr(assignments[i], '=');
        if (equals == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "malformed PARAMETER initializer");
        } else {
            Symbol *symbol;
            *equals = '\0';
            symbol = ensure_symbol(unit, f2c_trim(assignments[i]));
            if (symbol == NULL) {
                f2c_diagnostic(context, source_line->number, 1,
                               "out of memory in PARAMETER declaration");
            } else {
                symbol->parameter = 1;
                free(symbol->initializer);
                symbol->initializer = f2c_strdup(f2c_trim(equals + 1));
                symbol->kind_type = kind_type_from_initializer(unit, symbol->initializer);
            }
        }
        free(assignments[i]);
    }
    free(assignments);
}

static void parse_save_declaration(Context *context, Unit *unit, Line *source_line) {
    char *copy;
    char *cursor;
    char **names;
    size_t count = 0U;
    size_t i;
    if (!f2c_starts_word(source_line->text, "save"))
        return;
    copy = f2c_strdup(source_line->text + strlen("save"));
    if (copy == NULL) {
        f2c_diagnostic(context, source_line->number, 1, "out of memory in SAVE declaration");
        return;
    }
    cursor = f2c_trim(copy);
    if (*cursor == '\0') {
        unit->save_all = 1;
        free(copy);
        return;
    }
    if (cursor[0] == ':' && cursor[1] == ':')
        cursor = f2c_trim(cursor + 2);
    if (*cursor == '/') {
        /* COMMON storage is already emitted at file scope. */
        free(copy);
        return;
    }
    names = f2c_split_arguments(cursor, &count);
    for (i = 0U; i < count; ++i) {
        Symbol *symbol = ensure_symbol(unit, f2c_trim(names[i]));
        if (symbol == NULL)
            f2c_diagnostic(context, source_line->number, 1, "out of memory in SAVE declaration");
        else
            symbol->saved = 1;
        free(names[i]);
    }
    free(names);
    free(copy);
}

static int equivalence_designator(Unit *unit, char *text, Symbol **symbol_out,
                                  int64_t *offset_out) {
    char *clean = f2c_trim(text);
    size_t consumed = 0U;
    char *name = f2c_identifier(clean, &consumed);
    Symbol *symbol = name != NULL ? ensure_symbol(unit, name) : NULL;
    const char *open = clean + consumed;
    int64_t offset = 0;
    int64_t stride = 1;
    size_t dimension;
    char **indices = NULL;
    size_t index_count = 0U;
    free(name);
    while (isspace((unsigned char)*open))
        ++open;
    if (symbol == NULL)
        return 0;
    if (*open == '(')
        indices = f2c_split_arguments(open, &index_count);
    if ((*open == '(' && index_count != symbol->rank) || (*open != '(' && *open != '\0'))
        goto failed;
    for (dimension = 0U; dimension < symbol->rank && indices != NULL; ++dimension) {
        int64_t index;
        int64_t lower;
        int64_t upper;
        int64_t extent;
        if (!f2c_evaluate_integer_text(unit, f2c_trim(indices[dimension]), &index) ||
            !f2c_evaluate_integer_text(unit, symbol->dimensions[dimension].lower, &lower) ||
            !f2c_evaluate_integer_text(unit, symbol->dimensions[dimension].upper, &upper) ||
            upper < lower || index < lower || index > upper)
            goto failed;
        extent = upper - lower + 1;
        if ((index - lower) != 0 && stride > INT64_MAX / (index - lower))
            goto failed;
        offset += (index - lower) * stride;
        if (dimension + 1U < symbol->rank && extent != 0 && stride > INT64_MAX / extent)
            goto failed;
        stride *= extent;
    }
    while (index_count != 0U)
        free(indices[--index_count]);
    free(indices);
    *symbol_out = symbol;
    *offset_out = offset;
    return 1;

failed:
    while (index_count != 0U)
        free(indices[--index_count]);
    free(indices);
    return 0;
}

static void parse_equivalence_declaration(Context *context, Unit *unit, Line *source_line) {
    const char *cursor;
    if (!f2c_starts_word(source_line->text, "equivalence"))
        return;
    cursor = source_line->text + strlen("equivalence");
    while ((cursor = strchr(cursor, '(')) != NULL) {
        const char *begin = ++cursor;
        int depth = 1;
        char *group;
        char **members;
        size_t count = 0U;
        while (*cursor != '\0' && depth != 0) {
            if (*cursor == '(')
                ++depth;
            else if (*cursor == ')')
                --depth;
            ++cursor;
        }
        if (depth != 0) {
            f2c_diagnostic(context, source_line->number, 1, "unclosed EQUIVALENCE group");
            return;
        }
        group = f2c_strdup_n(begin, (size_t)((cursor - 1) - begin));
        members = f2c_split_arguments(group, &count);
        if (count != 2U) {
            f2c_diagnostic(context, source_line->number, 1,
                           "only pairwise EQUIVALENCE groups are supported");
        } else {
            Symbol *first = NULL;
            Symbol *second = NULL;
            Symbol *root;
            int64_t first_offset = 0;
            int64_t second_offset = 0;
            int64_t alias_offset;
            if (!equivalence_designator(unit, members[0], &first, &first_offset) ||
                !equivalence_designator(unit, members[1], &second, &second_offset)) {
                f2c_diagnostic(context, source_line->number, 1, "malformed EQUIVALENCE group");
            } else {
                Buffer c_name = {0};
                root = first;
                alias_offset = first_offset;
                while (root->alias_to != NULL) {
                    alias_offset += root->alias_offset;
                    root = f2c_find_symbol(unit, root->alias_to);
                    if (root == NULL)
                        break;
                }
                alias_offset -= second_offset;
                if (root == NULL) {
                    f2c_diagnostic(context, source_line->number, 1,
                                   "EQUIVALENCE storage root is invalid");
                } else {
                    free(second->alias_to);
                    second->alias_to = f2c_strdup(root->name);
                    second->alias_offset = alias_offset;
                    free(second->c_name);
                    if (alias_offset == 0)
                        second->c_name = f2c_strdup(root->c_name);
                    else {
                        f2c_buffer_printf(&c_name, "(&%s[%lld])", root->c_name,
                                          (long long)alias_offset);
                        second->c_name = f2c_buffer_take(&c_name);
                    }
                    if (second->alias_to == NULL || second->c_name == NULL)
                        f2c_diagnostic(context, source_line->number, 1,
                                       "out of memory in EQUIVALENCE declaration");
                }
            }
        }
        while (count != 0U)
            free(members[--count]);
        free(members);
        free(group);
    }
}

static void mark_call_targets(Unit *unit, const char *line) {
    const char *match = line;
    while ((match = strstr(match, "call")) != NULL) {
        const char *cursor = match + strlen("call");
        size_t consumed = 0U;
        char *name;
        if ((match != line && (isalnum((unsigned char)match[-1]) || match[-1] == '_')) ||
            (!isspace((unsigned char)*cursor) && *cursor != '(')) {
            match = cursor;
            continue;
        }
        while (isspace((unsigned char)*cursor))
            ++cursor;
        name = f2c_identifier(cursor, &consumed);
        if (name != NULL && cursor[consumed] != '%' && !f2c_is_intrinsic_name(name)) {
            Symbol *symbol = ensure_symbol(unit, name);
            if (symbol != NULL) {
                symbol->external = 1;
                symbol->external_subroutine = 1;
            }
            free(name);
        }
        match = cursor + consumed;
    }
}

static void mark_function_references(Unit *unit, const char *line) {
    size_t i;
    if (f2c_declaration_line(line) || f2c_starts_word(line, "use"))
        return;
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const size_t length = strlen(symbol->name);
        const char *match = line;
        if (length == 0U)
            continue;
        if (symbol->argument || symbol->parameter || symbol->rank != 0U ||
            symbol->type == TYPE_CHARACTER || strcmp(symbol->name, "if") == 0 ||
            f2c_is_intrinsic_name(symbol->name))
            continue;
        while ((match = strstr(match, symbol->name)) != NULL) {
            const char *after = match + length;
            const char *before = match;
            char *assignment;
            const char *quote_cursor;
            int quote = 0;
            for (quote_cursor = line; quote_cursor < match; ++quote_cursor) {
                if ((*quote_cursor == '\'' || *quote_cursor == '"') &&
                    (quote == 0 || quote == (unsigned char)*quote_cursor)) {
                    if (quote != 0 && quote_cursor[1] == *quote_cursor) {
                        ++quote_cursor;
                    } else {
                        quote = quote == 0 ? (unsigned char)*quote_cursor : 0;
                    }
                }
            }
            if (quote != 0) {
                match = after;
                continue;
            }
            if ((match != line && (isalnum((unsigned char)match[-1]) || match[-1] == '_')) ||
                (isalnum((unsigned char)*after) || *after == '_')) {
                match = after;
                continue;
            }
            while (before > line && isspace((unsigned char)before[-1]))
                --before;
            if ((size_t)(before - line) >= strlen("call")) {
                const char *call_begin = before - strlen("call");
                if (strncmp(call_begin, "call", strlen("call")) == 0 &&
                    (call_begin == line ||
                     (!isalnum((unsigned char)call_begin[-1]) && call_begin[-1] != '_'))) {
                    match = after;
                    continue;
                }
            }
            while (isspace((unsigned char)*after))
                ++after;
            if (*after != '(') {
                match = after;
                continue;
            }
            assignment = f2c_find_assignment((char *)line);
            if (match == line && assignment != NULL && match < assignment)
                break;
            symbol->external = 1;
            symbol->external_subroutine = 0;
            break;
        }
    }
}

static void mark_statement_function_symbols(Unit *unit, const char *line) {
    size_t consumed = 0U;
    char *name = f2c_identifier(line, &consumed);
    Symbol *function;
    char *assignment;
    const char *open;
    char **arguments;
    size_t count = 0U;
    size_t i;
    if (name == NULL)
        return;
    function = f2c_find_symbol(unit, name);
    open = line + consumed;
    while (isspace((unsigned char)*open))
        ++open;
    assignment = f2c_find_assignment((char *)line);
    if (function == NULL || function->rank != 0U || !f2c_is_intrinsic_name(name) || *open != '(' ||
        assignment == NULL || open > assignment) {
        free(name);
        return;
    }
    function->statement_function = 1;
    arguments = f2c_split_arguments(open, &count);
    for (i = 0U; i < count; ++i) {
        size_t argument_length = 0U;
        char *argument_name = f2c_identifier(f2c_trim(arguments[i]), &argument_length);
        Symbol *argument = argument_name != NULL ? f2c_find_symbol(unit, argument_name) : NULL;
        if (argument != NULL)
            argument->statement_dummy = 1;
        free(argument_name);
        free(arguments[i]);
    }
    free(arguments);
    free(name);
}

static void infer_external_signature(Unit *unit, Symbol *external, const char *line) {
    const size_t name_length = strlen(external->name);
    const char *match = line;
    while ((match = strstr(match, external->name)) != NULL) {
        const char *cursor = match + name_length;
        char **arguments;
        size_t count = 0U;
        size_t i;
        if ((match != line && (isalnum((unsigned char)match[-1]) || match[-1] == '_'))) {
            match += name_length;
            continue;
        }
        while (isspace((unsigned char)*cursor))
            ++cursor;
        if (*cursor != '(') {
            match += name_length;
            continue;
        }
        arguments = f2c_split_arguments(cursor, &count);
        if (count > 64U)
            count = 64U;
        for (i = 0U; i < count; ++i) {
            char *actual_text = f2c_trim(arguments[i]);
            size_t consumed = 0U;
            char *actual_name = f2c_identifier(actual_text, &consumed);
            Symbol *actual = actual_name != NULL ? f2c_find_symbol(unit, actual_name) : NULL;
            F2cExpr *actual_expression = f2c_parse_expression_ast(unit, actual_text, NULL);
            Type type = f2c_expression_type(unit, actual_text);
            if (type == TYPE_UNKNOWN && actual_text[0] == '\'')
                type = TYPE_CHARACTER;
            else if (type == TYPE_UNKNOWN && actual != NULL)
                type = actual->type;
            else if (type == TYPE_UNKNOWN && strchr(actual_text, '.') == NULL &&
                     strchr(actual_text, 'e') == NULL && strchr(actual_text, 'd') == NULL)
                type = TYPE_INTEGER;
            if (type == TYPE_UNKNOWN)
                type = TYPE_DOUBLE;
            if (external->external_parameter_count == 0U) {
                external->external_parameter_types[i] = type;
                external->external_parameter_kinds[i] = actual_expression != NULL
                                                            ? actual_expression->type_kind
                                                            : f2c_default_kind(type);
                external->external_parameter_ranks[i] =
                    actual_expression != NULL ? actual_expression->rank : 0U;
            }
            if (actual != NULL && actual->external && actual->external_declared &&
                actual_text[consumed] == '\0')
                external->external_parameter_procedures[i] = actual;
            external->external_parameter_const[i] |=
                (actual != NULL && (actual->intent == F2C_INTENT_IN || actual->parameter)) ||
                actual_text[0] == '\'' || actual_text[0] == '"';
            f2c_expr_free(actual_expression);
            free(actual_name);
            free(arguments[i]);
        }
        free(arguments);
        if (!external->external_signature_observed)
            external->external_parameter_count = count;
        external->external_signature_observed = 1;
        return;
    }
}

void f2c_analyze_module(Context *context, Unit *unit) {
    size_t i;
    f2c_prepare_implicit_map(context, unit);
    for (i = unit->begin + 1U; i < unit->end; ++i)
        f2c_import_module(context, unit, &context->lines.items[i]);
    parse_derived_type_definitions(context, unit);
    f2c_parse_explicit_interfaces(context, unit);
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (f2c_interface_start_line(context->lines.items[i].text)) {
            while (i + 1U < unit->end && !f2c_interface_end_line(context->lines.items[i + 1U].text))
                ++i;
            if (i + 1U < unit->end)
                ++i;
            continue;
        }
        if (context->lines.items[i].interface_depth != 0U)
            continue;
        if (line_in_derived_type(unit, i))
            continue;
        parse_declaration(context, unit, &context->lines.items[i]);
        parse_dimension_declaration(context, unit, &context->lines.items[i]);
        parse_parameter_declaration(context, unit, &context->lines.items[i]);
        parse_save_declaration(context, unit, &context->lines.items[i]);
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        Buffer c_name = {0};
        symbol->module_entity = 1;
        symbol->saved = 1;
        f2c_buffer_printf(&c_name, "f2c_module_%s_%s", unit->name, symbol->name);
        free(symbol->c_name);
        symbol->c_name = f2c_buffer_take(&c_name);
        if (symbol->type == TYPE_UNKNOWN) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1, "module entity '%s' requires an explicit type", symbol->name);
        }
    }
}

void f2c_analyze_unit(Context *context, Unit *unit) {
    size_t i;
    Symbol *function_result = NULL;
    if (!unit->interface_body)
        f2c_parse_explicit_interfaces(context, unit);
    f2c_prepare_implicit_map(context, unit);
    f2c_import_host_module(context, unit);
    parse_derived_type_definitions(context, unit);
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = ensure_symbol(unit, unit->arguments[i]);
        if (symbol != NULL) {
            symbol->argument = 1;
            symbol->first_seen_line = context->lines.items[unit->begin].number;
        }
    }
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (!f2c_unit_line_is_active(unit, &context->lines.items[i]))
            continue;
        if (line_in_derived_type(unit, i))
            continue;
        f2c_import_module(context, unit, &context->lines.items[i]);
        parse_declaration(context, unit, &context->lines.items[i]);
        parse_optional_declaration(context, unit, &context->lines.items[i]);
        parse_dimension_declaration(context, unit, &context->lines.items[i]);
        parse_external_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_procedure_declaration(context, unit, &context->lines.items[i]);
        parse_parameter_declaration(context, unit, &context->lines.items[i]);
        parse_save_declaration(context, unit, &context->lines.items[i]);
        parse_equivalence_declaration(context, unit, &context->lines.items[i]);
        parse_common_declaration(context, unit, &context->lines.items[i]);
        parse_namelist_declaration(context, unit, &context->lines.items[i]);
        mark_call_targets(unit, context->lines.items[i].text);
    }
    f2c_discover_implicit_symbols(context, unit);
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (f2c_unit_line_is_active(unit, &context->lines.items[i]))
            mark_function_references(unit, context->lines.items[i].text);
    }
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (f2c_unit_line_is_active(unit, &context->lines.items[i]))
            mark_statement_function_symbols(unit, context->lines.items[i].text);
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        size_t line_index;
        if (!unit->symbols[i].external)
            continue;
        for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
            if (!f2c_unit_line_is_active(unit, &context->lines.items[line_index]))
                continue;
            infer_external_signature(unit, &unit->symbols[i],
                                     context->lines.items[line_index].text);
        }
    }
    if (unit->kind == UNIT_FUNCTION && unit->result_name != NULL) {
        function_result = ensure_symbol(unit, unit->result_name);
        if (function_result == NULL) {
            f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                           "out of memory while declaring function result '%s'", unit->result_name);
        } else if (unit->return_type_explicit && function_result->type == TYPE_UNKNOWN) {
            function_result->type = unit->return_type;
            function_result->kind = unit->return_kind;
        }
        if (function_result != NULL && function_result->first_seen_line == 0U)
            function_result->first_seen_line = context->lines.items[unit->begin].number;
        if (function_result != NULL && unit->return_type == TYPE_CHARACTER &&
            unit->result_character_length != NULL && function_result->character_length == NULL)
            function_result->character_length = f2c_strdup(unit->result_character_length);
        if (function_result != NULL && function_result->allocatable) {
            char *result_c_name = f2c_strdup("f2c_result");
            if (result_c_name == NULL) {
                f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                               "out of memory naming allocatable function result");
            } else {
                free(function_result->c_name);
                function_result->c_name = result_c_name;
            }
        }
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const int is_function_result = unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
                                       strcmp(symbol->name, unit->result_name) == 0;
        if (symbol->optional && !symbol->argument) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1, "OPTIONAL entity '%s' is not a dummy argument", symbol->name);
        }
        if (symbol->intent != F2C_INTENT_UNSPECIFIED && !symbol->argument) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1, "INTENT attribute on '%s' requires a dummy argument", symbol->name);
        }
        if (symbol->saved && symbol->argument) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1, "dummy argument '%s' cannot have the SAVE attribute", symbol->name);
        }
        if (symbol->initializer != NULL && symbol->argument) {
            f2c_diagnostic(
                context,
                symbol->declaration_line != 0U ? symbol->declaration_line
                                               : context->lines.items[unit->begin].number,
                1, "dummy argument '%s' cannot have a declaration initializer", symbol->name);
        }
        if (symbol->parameter && symbol->initializer == NULL) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1, "PARAMETER entity '%s' requires an initializer", symbol->name);
        }
        if (symbol->parameter && symbol->allocatable) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1, "PARAMETER entity '%s' cannot be ALLOCATABLE", symbol->name);
        }
        if (symbol->allocatable && symbol->pointer) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1, "entity '%s' cannot be both ALLOCATABLE and POINTER", symbol->name);
        }
        if (symbol->parameter && (symbol->pointer || symbol->target)) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1, "PARAMETER entity '%s' cannot have POINTER or TARGET", symbol->name);
        }
        if (symbol->deferred_character &&
            (symbol->external || (is_function_result && !symbol->allocatable))) {
            f2c_diagnostic(context,
                           symbol->declaration_line != 0U
                               ? symbol->declaration_line
                               : context->lines.items[unit->begin].number,
                           1,
                           "deferred-length CHARACTER '%s' currently requires local allocatable "
                           "storage",
                           symbol->name);
        }
        if (symbol->type == TYPE_CHARACTER && symbol->character_length != NULL &&
            strcmp(symbol->character_length, "*") == 0 && !symbol->argument && !symbol->external &&
            !symbol->parameter && !is_function_result) {
            f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                           "assumed-length CHARACTER '%s' must be a dummy argument, "
                           "function result, external function, or named constant",
                           symbol->name);
        }
        if (symbol->type == TYPE_CHARACTER && !symbol->argument && !symbol->external &&
            !symbol->parameter && !symbol->allocatable && !is_function_result && !symbol->pointer &&
            symbol->character_length != NULL && strcmp(symbol->character_length, "*") != 0) {
            int64_t constant_length;
            symbol->automatic_character =
                !f2c_evaluate_integer_text(unit, symbol->character_length, &constant_length);
        }
        if (unit->symbols[i].type == TYPE_UNKNOWN) {
            Type implicit_type;
            if (unit->symbols[i].external && unit->symbols[i].external_subroutine) {
                unit->symbols[i].external_subroutine = 1;
                continue;
            }
            implicit_type = f2c_implicit_type_for_name(unit, unit->symbols[i].name);
            if (implicit_type == TYPE_UNKNOWN) {
                f2c_diagnostic(context,
                               symbol->first_seen_line != 0U
                                   ? symbol->first_seen_line
                                   : context->lines.items[unit->begin].number,
                               1, "'%s' has no type under IMPLICIT NONE", unit->symbols[i].name);
            } else {
                unit->symbols[i].type = implicit_type;
                unit->symbols[i].kind = f2c_implicit_kind_for_name(unit, unit->symbols[i].name);
                if (implicit_type == TYPE_CHARACTER && symbol->character_length == NULL) {
                    const char *length = f2c_implicit_character_length_for_name(unit, symbol->name);
                    symbol->character_length = f2c_strdup(length != NULL ? length : "1");
                    if (symbol->character_length == NULL)
                        f2c_diagnostic(context,
                                       symbol->first_seen_line != 0U
                                           ? symbol->first_seen_line
                                           : context->lines.items[unit->begin].number,
                                       1, "out of memory applying implicit CHARACTER length");
                }
                f2c_diagnostic(context,
                               symbol->first_seen_line != 0U
                                   ? symbol->first_seen_line
                                   : context->lines.items[unit->begin].number,
                               0, "'%s' uses implicit Fortran typing", unit->symbols[i].name);
            }
        }
        if (symbol->kind == 0)
            symbol->kind = f2c_default_kind(symbol->type);
        symbol->value_category = symbol->parameter ? F2C_VALUE_CONSTANT : F2C_VALUE_VARIABLE;
        f2c_shape_from_symbol(unit, &symbol->shape, symbol);
    }
    if (function_result != NULL && function_result->type != TYPE_UNKNOWN) {
        unit->return_type = function_result->type;
        unit->return_kind = function_result->kind != 0 ? function_result->kind
                                                       : f2c_default_kind(function_result->type);
    }
}

static int statement_begins_block(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE ||
           statement->kind == F2C_STMT_SELECT_CASE || statement->kind == F2C_STMT_SELECT_TYPE ||
           statement->kind == F2C_STMT_BLOCK_SCOPE ||
           (statement->kind == F2C_STMT_IF && statement->block);
}

static int statement_begins_loop(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE;
}

static void annotate_loop_hints(Unit *unit) {
    size_t *blocks;
    size_t *loops;
    size_t block_count = 0U;
    size_t loop_count = 0U;
    size_t i;
    if (unit->statement_count == 0U)
        return;
    blocks = (size_t *)calloc(unit->statement_count, sizeof(*blocks));
    loops = (size_t *)calloc(unit->statement_count, sizeof(*loops));
    if (blocks == NULL || loops == NULL) {
        free(blocks);
        free(loops);
        return;
    }
    for (i = 0U; i < unit->statement_count; ++i) {
        F2cStatement *statement = &unit->statements[i];
        if (statement_begins_loop(statement)) {
            size_t ancestor;
            for (ancestor = 0U; ancestor + 1U < loop_count; ++ancestor)
                unit->statements[loops[ancestor]].unroll_hint = 0;
            if (loop_count != 0U) {
                F2cStatement *parent = &unit->statements[loops[loop_count - 1U]];
                parent->unroll_hint = parent->kind == F2C_STMT_DO;
                statement->unroll_hint = statement->kind == F2C_STMT_DO;
            }
            loops[loop_count++] = i;
        }
        if (statement_begins_block(statement)) {
            blocks[block_count++] = i;
        } else if ((statement->kind == F2C_STMT_END_BLOCK ||
                    statement->kind == F2C_STMT_END_BLOCK_SCOPE ||
                    statement->kind == F2C_STMT_END_SELECT) &&
                   block_count != 0U) {
            const size_t opener = blocks[--block_count];
            if (statement_begins_loop(&unit->statements[opener]) && loop_count != 0U)
                --loop_count;
        }
    }
    free(blocks);
    free(loops);
}

static void annotate_block_scopes(Context *context, Unit *unit) {
    size_t *stack;
    size_t depth = 0U;
    size_t i;
    if (unit->statement_count == 0U)
        return;
    stack = (size_t *)calloc(unit->statement_count, sizeof(*stack));
    if (stack == NULL)
        return;
    for (i = 0U; i < unit->statement_count; ++i) {
        F2cStatement *statement = &unit->statements[i];
        if (statement->kind == F2C_STMT_BLOCK_SCOPE) {
            stack[depth++] = i;
        } else if (statement->kind == F2C_STMT_END_BLOCK_SCOPE) {
            size_t symbol_index;
            F2cStatement *opener;
            if (depth == 0U) {
                f2c_diagnostic(context, statement->line, 1,
                               "END BLOCK has no matching BLOCK construct");
                continue;
            }
            opener = &unit->statements[stack[--depth]];
            for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
                Symbol *symbol = &unit->symbols[symbol_index];
                if (symbol->declaration_line <= opener->line ||
                    symbol->declaration_line >= statement->line)
                    continue;
                if (symbol->scope_begin_line == 0U || opener->line > symbol->scope_begin_line) {
                    symbol->scope_begin_line = opener->line;
                    symbol->scope_end_line = statement->line;
                }
            }
        }
    }
    while (depth != 0U) {
        F2cStatement *opener = &unit->statements[stack[--depth]];
        f2c_diagnostic(context, opener->line, 1, "BLOCK construct is missing END BLOCK");
    }
    free(stack);
}

void f2c_build_statement_ir(Context *context, Unit *unit) {
    size_t i;
    Symbol **select_symbols = NULL;
    F2cDerivedType **select_declared_types = NULL;
    size_t select_depth = 0U;
    unit->statement_count = unit->end > unit->begin + 1U ? unit->end - unit->begin - 1U : 0U;
    if (unit->statement_count != 0U) {
        unit->statements = (F2cStatement *)calloc(unit->statement_count, sizeof(*unit->statements));
        if (unit->statements == NULL) {
            f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                           "out of memory while building statement IR for '%s'", unit->name);
            unit->statement_count = 0U;
            return;
        }
        select_symbols = (Symbol **)calloc(unit->statement_count, sizeof(*select_symbols));
        select_declared_types =
            (F2cDerivedType **)calloc(unit->statement_count, sizeof(*select_declared_types));
        if (select_symbols == NULL || select_declared_types == NULL) {
            free(select_symbols);
            free(select_declared_types);
            f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                           "out of memory while tracking SELECT TYPE scopes");
            return;
        }
        for (i = 0U; i < unit->statement_count; ++i) {
            Line *line = &context->lines.items[unit->begin + 1U + i];
            const char *text = f2c_unit_line_is_active(unit, line) ? line->text : "";
            if (!f2c_parse_statement(unit, text, line->number, &unit->statements[i])) {
                f2c_diagnostic(context, line->number, 1,
                               "out of memory while parsing statement IR");
                break;
            }
            if (unit->statements[i].kind == F2C_STMT_SELECT_TYPE) {
                F2cExpr *selector = unit->statements[i].expression;
                Symbol *symbol =
                    selector != NULL && selector->kind == F2C_EXPR_NAME ? selector->symbol : NULL;
                if (symbol == NULL || !symbol->polymorphic || symbol->derived_type == NULL) {
                    f2c_diagnostic(context, line->number, 1,
                                   "SELECT TYPE selector must be a named polymorphic object");
                } else {
                    select_symbols[select_depth] = symbol;
                    select_declared_types[select_depth] = symbol->derived_type;
                    ++select_depth;
                }
            } else if (unit->statements[i].kind == F2C_STMT_TYPE_GUARD && select_depth != 0U) {
                Symbol *selector = select_symbols[select_depth - 1U];
                if (unit->statements[i].guard_type != NULL)
                    selector->derived_type = unit->statements[i].guard_type;
                else
                    selector->derived_type = select_declared_types[select_depth - 1U];
            } else if (unit->statements[i].kind == F2C_STMT_END_SELECT && select_depth != 0U) {
                --select_depth;
                select_symbols[select_depth]->derived_type = select_declared_types[select_depth];
            }
        }
        while (select_depth != 0U) {
            --select_depth;
            select_symbols[select_depth]->derived_type = select_declared_types[select_depth];
        }
        free(select_symbols);
        free(select_declared_types);
        annotate_block_scopes(context, unit);
        f2c_validate_unit_expressions(context, unit);
        annotate_loop_hints(unit);
    }
}
