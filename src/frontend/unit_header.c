#include "frontend/declaration/private.h"

#include "ast/unit.h"

#include <stdlib.h>
#include <string.h>

static void report_header_error(Context *context, const Line *line,
                                const F2cUnitHeaderSyntax *syntax) {
    const F2cToken *token;
    if (context == NULL || line == NULL || syntax == NULL)
        return;
    token = syntax->error_token;
    switch (syntax->error) {
    case F2C_UNIT_HEADER_ERROR_DUPLICATE_PREFIX: {
        char *name = f2c_token_text(token);
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, token, 1,
                                  "duplicate procedure prefix '%s'",
                                  name != NULL ? name : "<invalid>");
        free(name);
        break;
    }
    case F2C_UNIT_HEADER_ERROR_MALFORMED_PREFIX:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "malformed program-unit prefix");
        break;
    case F2C_UNIT_HEADER_ERROR_MISSING_NAME:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "program unit requires a valid name");
        break;
    case F2C_UNIT_HEADER_ERROR_MALFORMED_ARGUMENT_LIST:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "malformed dummy argument list");
        break;
    case F2C_UNIT_HEADER_ERROR_MALFORMED_ARGUMENT:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "dummy argument must be a name");
        break;
    case F2C_UNIT_HEADER_ERROR_DUPLICATE_ARGUMENT: {
        char *name = f2c_token_text(token);
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, token, 1,
                                  "duplicate dummy argument '%s'",
                                  name != NULL ? name : "<invalid>");
        free(name);
        break;
    }
    case F2C_UNIT_HEADER_ERROR_ALTERNATE_RETURN:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "alternate return is valid only in a SUBROUTINE header");
        break;
    case F2C_UNIT_HEADER_ERROR_MALFORMED_RESULT:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "malformed RESULT clause");
        break;
    case F2C_UNIT_HEADER_ERROR_TRAILING_TOKENS:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "unexpected tokens after program-unit header");
        break;
    case F2C_UNIT_HEADER_ERROR_NONE:
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &syntax->span, 1,
                                 "malformed program-unit header");
        break;
    }
}

static void copy_prefixes(const F2cUnitHeaderSyntax *syntax, Unit *unit) {
    unit->recursive = syntax->recursive_prefix != NULL;
    unit->pure = syntax->pure_prefix != NULL;
    unit->elemental = syntax->elemental_prefix != NULL;
    unit->impure = syntax->impure_prefix != NULL;
    unit->module_procedure = syntax->module_prefix != NULL;
    if (syntax->recursive_prefix != NULL)
        unit->recursive_span = syntax->recursive_prefix->span;
    if (syntax->pure_prefix != NULL)
        unit->pure_span = syntax->pure_prefix->span;
    if (syntax->elemental_prefix != NULL)
        unit->elemental_span = syntax->elemental_prefix->span;
    if (syntax->impure_prefix != NULL)
        unit->impure_span = syntax->impure_prefix->span;
    if (syntax->module_prefix != NULL)
        unit->module_procedure_span = syntax->module_prefix->span;
}

static int lower_result_type(Context *context, const Line *line, const F2cUnitHeaderSyntax *syntax,
                             Unit *unit) {
    F2cDeclarationTypeSpec type_spec;
    size_t begin;
    size_t end;
    size_t errors_before = context != NULL ? context->result.error_count : 0U;
    int parsed;
    if (syntax->type_spec.count == 0U) {
        unit->return_type_explicit = 0;
        return 1;
    }
    if (syntax->kind != F2C_UNIT_SYNTAX_FUNCTION) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                  &syntax->type_spec.tokens[0], 1,
                                  "only a FUNCTION may have an explicit result type prefix");
        return 0;
    }
    begin = (size_t)(syntax->type_spec.tokens - line->tokens);
    end = begin + syntax->type_spec.count;
    parsed = f2c_parse_type_spec_tokens(context, NULL, line, begin, &type_spec);
    if (!parsed || type_spec.end != end) {
        if (context != NULL && context->result.error_count == errors_before) {
            const F2cToken *token = !parsed || type_spec.end >= end ? &line->tokens[begin]
                                                                    : &line->tokens[type_spec.end];
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                      "malformed FUNCTION result type prefix");
        }
        f2c_release_type_spec(&type_spec);
        return 0;
    }
    unit->return_type = type_spec.type;
    unit->return_kind = type_spec.kind;
    unit->return_type_explicit = 1;
    unit->return_type_span =
        f2c_source_span_cover(&line->tokens[begin].span, &line->tokens[end - 1U].span);
    unit->result_character_length = type_spec.character_length;
    unit->result_character_length_syntax = type_spec.character_length_syntax;
    unit->result_derived_type_name = type_spec.derived_type_name;
    type_spec.character_length = NULL;
    type_spec.derived_type_name = NULL;
    f2c_release_type_spec(&type_spec);
    return 1;
}

static int lower_arguments(Context *context, const F2cUnitHeaderSyntax *syntax, Unit *unit) {
    size_t index;
    if (syntax->argument_count == 0U)
        return 1;
    if (syntax->argument_count > SIZE_MAX / sizeof(*unit->arguments) ||
        syntax->argument_count > SIZE_MAX / sizeof(*unit->argument_spans) ||
        syntax->argument_count > SIZE_MAX / sizeof(*unit->dummy_argument_indices))
        goto no_memory;
    unit->arguments = (char **)calloc(syntax->argument_count, sizeof(*unit->arguments));
    unit->argument_spans =
        (F2cSourceSpan *)calloc(syntax->argument_count, sizeof(*unit->argument_spans));
    unit->dummy_argument_indices =
        (size_t *)calloc(syntax->argument_count, sizeof(*unit->dummy_argument_indices));
    if (unit->arguments == NULL || unit->argument_spans == NULL ||
        unit->dummy_argument_indices == NULL)
        goto no_memory;
    unit->dummy_count = syntax->argument_count;
    for (index = 0U; index < syntax->argument_count; ++index) {
        const F2cUnitDummySyntax *argument = &syntax->arguments[index];
        if (argument->alternate_return) {
            unit->dummy_argument_indices[index] = SIZE_MAX;
            ++unit->alternate_return_count;
            continue;
        }
        unit->dummy_argument_indices[index] = unit->argument_count;
        unit->arguments[unit->argument_count] = f2c_token_text(argument->token);
        if (unit->arguments[unit->argument_count] == NULL)
            goto no_memory;
        unit->argument_spans[unit->argument_count] = argument->token->span;
        ++unit->argument_count;
    }
    return 1;

no_memory:
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax->span, 1,
                             "out of memory lowering program-unit arguments");
    return 0;
}

static int result_name_conflicts(const F2cUnitHeaderSyntax *syntax) {
    size_t index;
    char *name;
    int equal;
    if (syntax->result_name == NULL)
        return 0;
    name = f2c_token_text(syntax->name);
    equal = name != NULL && f2c_token_equals(syntax->result_name, name);
    free(name);
    if (equal)
        return 1;
    for (index = 0U; index < syntax->argument_count; ++index) {
        const F2cToken *argument = syntax->arguments[index].token;
        if (syntax->arguments[index].alternate_return)
            continue;
        name = f2c_token_text(argument);
        equal = name != NULL && f2c_token_equals(syntax->result_name, name);
        free(name);
        if (equal)
            return 1;
    }
    return 0;
}

static int lower_header(Context *context, const Line *line, const F2cUnitHeaderSyntax *syntax,
                        Unit *unit) {
    memset(unit, 0, sizeof(*unit));
    if (syntax->kind == F2C_UNIT_SYNTAX_PROGRAM)
        unit->kind = UNIT_PROGRAM;
    else if (syntax->kind == F2C_UNIT_SYNTAX_SUBROUTINE)
        unit->kind = UNIT_SUBROUTINE;
    else if (syntax->kind == F2C_UNIT_SYNTAX_BLOCK_DATA)
        unit->kind = UNIT_BLOCK_DATA;
    else
        unit->kind = UNIT_FUNCTION;
    unit->header_span = syntax->span;
    if (syntax->name != NULL)
        unit->name_span = syntax->name->span;
    unit->return_type = TYPE_REAL;
    unit->return_kind = f2c_default_kind(TYPE_REAL);
    copy_prefixes(syntax, unit);
    if (syntax->name != NULL) {
        unit->name = f2c_token_text(syntax->name);
    } else if (unit->kind == UNIT_BLOCK_DATA) {
        Buffer generated = {0};
        f2c_buffer_printf(&generated, "f2c_block_data_%zu", syntax->span.begin.line);
        unit->name = f2c_buffer_take(&generated);
        unit->fortran_name = f2c_strdup("");
    }
    if (unit->name == NULL)
        goto no_memory;
    if (!lower_result_type(context, line, syntax, unit) || !lower_arguments(context, syntax, unit))
        return 0;
    if (unit->kind != UNIT_FUNCTION)
        return 1;
    if (result_name_conflicts(syntax)) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax->result_name, 1,
                                  "FUNCTION result name must differ from the function and its "
                                  "dummy arguments");
        return 0;
    }
    if (syntax->result_name != NULL) {
        unit->result_name = f2c_token_text(syntax->result_name);
        unit->result_name_span = syntax->result_name->span;
    } else {
        unit->result_name = f2c_strdup(unit->name);
        unit->result_name_span = syntax->name->span;
    }
    if (unit->result_name == NULL)
        goto no_memory;
    return 1;

no_memory:
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax->span, 1,
                             "out of memory lowering program-unit header");
    return 0;
}

F2cUnitHeaderParseStatus f2c_parse_unit_header(Context *context, const Line *line, Unit *unit) {
    F2cUnitHeaderSyntax syntax;
    F2cUnitHeaderParseStatus status;
    if (unit == NULL)
        return F2C_UNIT_HEADER_INVALID;
    memset(unit, 0, sizeof(*unit));
    status = f2c_parse_unit_header_syntax(line, &syntax);
    if (status == F2C_UNIT_HEADER_INVALID) {
        report_header_error(context, line, &syntax);
    } else if (status == F2C_UNIT_HEADER_NO_MEMORY) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax.span, 1,
                                 "out of memory parsing program-unit header");
    } else if (status == F2C_UNIT_HEADER_PARSED && !lower_header(context, line, &syntax, unit)) {
        f2c_free_unit(unit);
        status = F2C_UNIT_HEADER_INVALID;
    }
    f2c_unit_header_syntax_discard(&syntax);
    return status;
}

static F2cUnitSyntaxKind syntax_kind(UnitKind kind) {
    switch (kind) {
    case UNIT_PROGRAM:
        return F2C_UNIT_SYNTAX_PROGRAM;
    case UNIT_SUBROUTINE:
        return F2C_UNIT_SYNTAX_SUBROUTINE;
    case UNIT_FUNCTION:
        return F2C_UNIT_SYNTAX_FUNCTION;
    case UNIT_MODULE:
        return F2C_UNIT_SYNTAX_MODULE;
    case UNIT_BLOCK_DATA:
        return F2C_UNIT_SYNTAX_BLOCK_DATA;
    }
    return F2C_UNIT_SYNTAX_PROGRAM;
}

F2cUnitEndMatchStatus f2c_match_program_unit_end(Context *context, const Line *line,
                                                 const Unit *unit) {
    F2cUnitEndSyntax syntax;
    const F2cUnitEndParseStatus status = f2c_parse_unit_end_syntax(line, &syntax);
    const char *expected_name;
    if (status == F2C_UNIT_END_NOT_MATCHED)
        return F2C_UNIT_END_NO_MATCH;
    if (status == F2C_UNIT_END_INVALID) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, syntax.error_token, 1,
                                  "unexpected tokens after program-unit END statement");
        return F2C_UNIT_END_MISMATCHED;
    }
    if (unit == NULL)
        return F2C_UNIT_END_MATCHED;
    if (syntax.has_kind && syntax.kind != syntax_kind(unit->kind)) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax.kind_token, 1,
                                  "program-unit END kind does not match its opening statement");
        return F2C_UNIT_END_MISMATCHED;
    }
    expected_name = unit->fortran_name != NULL ? unit->fortran_name : unit->name;
    if (syntax.name != NULL &&
        (expected_name == NULL || !f2c_token_equals(syntax.name, expected_name))) {
        char *actual_name = f2c_token_text(syntax.name);
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax.name, 1,
                                  "program-unit END name '%s' does not match '%s'",
                                  actual_name != NULL ? actual_name : "<invalid>",
                                  expected_name != NULL ? expected_name : "<unnamed>");
        free(actual_name);
        return F2C_UNIT_END_MISMATCHED;
    }
    return F2C_UNIT_END_MATCHED;
}
