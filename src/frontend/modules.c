#include "frontend/module_constants.h"
#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

typedef struct UseSyntax {
    const F2cToken *module_name;
    size_t association_begin;
    int intrinsic;
    int non_intrinsic;
    int only;
    int valid;
} UseSyntax;

static size_t statement_start(const Line *line) {
    if (line != NULL && line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER)
        return 1U;
    return 0U;
}

static int parse_use_syntax(const Line *line, UseSyntax *syntax) {
    const size_t start = statement_start(line);
    size_t index = start + 1U;
    memset(syntax, 0, sizeof(*syntax));
    syntax->association_begin = line != NULL ? line->token_count : 0U;
    if (line == NULL || !f2c_line_token_equals(line, start, "use"))
        return 0;
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_COMMA) {
        ++index;
        if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_IDENTIFIER)
            return 1;
        syntax->intrinsic = f2c_token_equals(&line->tokens[index], "intrinsic");
        syntax->non_intrinsic = f2c_token_equals(&line->tokens[index], "non_intrinsic");
        if (!syntax->intrinsic && !syntax->non_intrinsic)
            return 1;
        ++index;
        if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_DOUBLE_COLON)
            return 1;
        ++index;
    } else if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON) {
        ++index;
    }
    if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_IDENTIFIER)
        return 1;
    syntax->module_name = &line->tokens[index++];
    if (index == line->token_count) {
        syntax->valid = 1;
        return 1;
    }
    if (line->tokens[index].kind != F2C_TOKEN_COMMA)
        return 1;
    ++index;
    if (index < line->token_count && f2c_line_token_equals(line, index, "only")) {
        syntax->only = 1;
        ++index;
        if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_COLON)
            return 1;
        ++index;
    }
    syntax->association_begin = index;
    syntax->valid = 1;
    return 1;
}

static int next_use_association(const Line *line, size_t *cursor, const F2cToken **local,
                                const F2cToken **remote, int *renamed) {
    size_t index = *cursor;
    *local = NULL;
    *remote = NULL;
    *renamed = 0;
    if (index >= line->token_count)
        return 0;
    if (line->tokens[index].kind != F2C_TOKEN_IDENTIFIER)
        return -1;
    *local = &line->tokens[index++];
    *remote = *local;
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
        f2c_token_equals(&line->tokens[index], "=>")) {
        *renamed = 1;
        ++index;
        if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_IDENTIFIER)
            return -1;
        *remote = &line->tokens[index++];
    }
    if (index < line->token_count) {
        if (line->tokens[index].kind != F2C_TOKEN_COMMA)
            return -1;
        ++index;
        if (index == line->token_count)
            return -1;
    }
    *cursor = index;
    return 1;
}

static const F2cModuleConstant la_constants[] = {
    {"sp", TYPE_INTEGER, "4"},
    {"szero", TYPE_REAL, "0.0"},
    {"shalf", TYPE_REAL, "0.5"},
    {"sone", TYPE_REAL, "1.0"},
    {"stwo", TYPE_REAL, "2.0"},
    {"sthree", TYPE_REAL, "3.0"},
    {"sfour", TYPE_REAL, "4.0"},
    {"seight", TYPE_REAL, "8.0"},
    {"sten", TYPE_REAL, "10.0"},
    {"czero", TYPE_COMPLEX, "(0.0, 0.0)"},
    {"chalf", TYPE_COMPLEX, "(0.5, 0.0)"},
    {"cone", TYPE_COMPLEX, "(1.0, 0.0)"},
    {"sprefix", TYPE_CHARACTER, "'S'"},
    {"cprefix", TYPE_CHARACTER, "'C'"},
    {"sulp", TYPE_REAL, "1.1920928955078125e-7"},
    {"seps", TYPE_REAL, "5.9604644775390625e-8"},
    {"ssafmin", TYPE_REAL, "1.1754943508222875e-38"},
    {"ssafmax", TYPE_REAL, "8.507059173023462e37"},
    {"ssmlnum", TYPE_REAL, "9.860761315262648e-32"},
    {"sbignum", TYPE_REAL, "1.0141204801825835e31"},
    {"srtmin", TYPE_REAL, "3.1401849173675503e-16"},
    {"srtmax", TYPE_REAL, "3.184525836262886e15"},
    {"stsml", TYPE_REAL, "1.0842021724855044e-19"},
    {"stbig", TYPE_REAL, "4.503599627370496e15"},
    {"sssml", TYPE_REAL, "3.777893186295716e22"},
    {"ssbig", TYPE_REAL, "1.3234889800848443e-23"},
    {"dp", TYPE_INTEGER, "8"},
    {"dzero", TYPE_DOUBLE, "0.0d0"},
    {"dhalf", TYPE_DOUBLE, "0.5d0"},
    {"done", TYPE_DOUBLE, "1.0d0"},
    {"dtwo", TYPE_DOUBLE, "2.0d0"},
    {"dthree", TYPE_DOUBLE, "3.0d0"},
    {"dfour", TYPE_DOUBLE, "4.0d0"},
    {"deight", TYPE_DOUBLE, "8.0d0"},
    {"dten", TYPE_DOUBLE, "10.0d0"},
    {"zzero", TYPE_DOUBLE_COMPLEX, "(0.0d0, 0.0d0)"},
    {"zhalf", TYPE_DOUBLE_COMPLEX, "(0.5d0, 0.0d0)"},
    {"zone", TYPE_DOUBLE_COMPLEX, "(1.0d0, 0.0d0)"},
    {"dprefix", TYPE_CHARACTER, "'D'"},
    {"zprefix", TYPE_CHARACTER, "'Z'"},
    {"dulp", TYPE_DOUBLE, "2.2204460492503131d-16"},
    {"deps", TYPE_DOUBLE, "1.1102230246251565d-16"},
    {"dsafmin", TYPE_DOUBLE, "2.2250738585072014d-308"},
    {"dsafmax", TYPE_DOUBLE, "4.4942328371557898d307"},
    {"dsmlnum", TYPE_DOUBLE, "1.0020841800044864d-292"},
    {"dbignum", TYPE_DOUBLE, "9.9792015476735991d291"},
    {"drtmin", TYPE_DOUBLE, "1.0010415475915505d-146"},
    {"drtmax", TYPE_DOUBLE, "9.9895953610111750d145"},
    {"dtsml", TYPE_DOUBLE, "1.4916681462400413d-154"},
    {"dtbig", TYPE_DOUBLE, "1.9979190722022350d146"},
    {"dssml", TYPE_DOUBLE, "4.4989137945431964d161"},
    {"dsbig", TYPE_DOUBLE, "1.1113793747425387d-162"},
};

const F2cModuleConstant *f2c_la_constants(size_t *count) {
    if (count != NULL)
        *count = sizeof(la_constants) / sizeof(la_constants[0]);
    return la_constants;
}

static const F2cModuleConstant *find_la_constant(const char *name) {
    size_t i;
    for (i = 0U; i < sizeof(la_constants) / sizeof(la_constants[0]); ++i) {
        if (strcmp(la_constants[i].name, name) == 0)
            return &la_constants[i];
    }
    return NULL;
}

static F2cExpr *parse_compiler_constant(Unit *unit, const char *source) {
    F2cTokenStream stream;
    F2cToken *tokens = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    F2cExpr *expression = NULL;
    const char *error_at = NULL;
    f2c_token_stream_init(&stream, source, 1U, 1U);
    for (;;) {
        F2cToken *replacement;
        size_t next;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (stream.token.kind == F2C_TOKEN_INVALID)
            goto cleanup;
        if (count == capacity) {
            next = capacity == 0U ? 8U : capacity * 2U;
            if (next < capacity || next > SIZE_MAX / sizeof(*tokens))
                goto cleanup;
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

static void import_la_constant(Context *context, Unit *unit, Line *line, const char *local_name,
                               const char *module_name) {
    const F2cModuleConstant *constant = find_la_constant(module_name);
    Symbol *symbol;
    if (constant == NULL) {
        f2c_diagnostic(context, line->number, 1, "LA_CONSTANTS has no member '%s'", module_name);
        return;
    }
    symbol = f2c_ensure_symbol(unit, local_name);
    if (symbol == NULL) {
        f2c_diagnostic(context, line->number, 1, "out of memory importing '%s'", local_name);
        return;
    }
    symbol->type = constant->type;
    symbol->kind = f2c_default_kind(constant->type);
    symbol->value_category = F2C_VALUE_CONSTANT;
    if (strcmp(module_name, "sp") == 0)
        symbol->kind_type = TYPE_REAL;
    else if (strcmp(module_name, "dp") == 0)
        symbol->kind_type = TYPE_DOUBLE;
    symbol->parameter = 1;
    free(symbol->initializer);
    symbol->initializer = f2c_strdup(constant->initializer);
    f2c_expr_free(symbol->initializer_expression);
    symbol->initializer_expression = parse_compiler_constant(unit, constant->initializer);
    if (symbol->initializer == NULL || symbol->initializer_expression == NULL) {
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                            "unable to build typed intrinsic-module constant '%s'", local_name);
    }
    if (constant->type == TYPE_CHARACTER) {
        free(symbol->character_length);
        symbol->character_length = f2c_strdup("1");
    }
}

static void import_la_constants(Context *context, Unit *unit, Line *source_line,
                                const UseSyntax *syntax) {
    size_t cursor = syntax->association_begin;
    int status;
    if (!syntax->only) {
        f2c_diagnostic(context, source_line->number, 1,
                       "USE LA_CONSTANTS without ONLY is not supported");
        return;
    }
    do {
        const F2cToken *local_token;
        const F2cToken *module_token;
        int renamed;
        char *local_name;
        char *module_name;
        status = next_use_association(source_line, &cursor, &local_token, &module_token, &renamed);
        if (status <= 0)
            break;
        local_name = f2c_token_text(local_token);
        module_name = f2c_token_text(module_token);
        if (local_name == NULL || module_name == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory importing LA_CONSTANTS");
            free(local_name);
            free(module_name);
            return;
        }
        import_la_constant(context, unit, source_line, local_name, module_name);
        free(local_name);
        free(module_name);
        (void)renamed;
    } while (status > 0);
    if (status < 0)
        f2c_diagnostic(context, source_line->number, 1, "malformed USE LA_CONSTANTS association");
}

static Unit *find_project_module(Context *context, const char *name) {
    size_t i;
    for (i = 0U; i < context->modules.count; ++i) {
        if (strcmp(context->modules.items[i].name, name) == 0)
            return &context->modules.items[i];
    }
    return NULL;
}

static int import_derived_type(Unit *unit, F2cDerivedType *derived) {
    F2cDerivedType **replacement;
    size_t i;
    for (i = 0U; i < unit->imported_derived_type_count; ++i) {
        if (unit->imported_derived_types[i] == derived ||
            strcmp(unit->imported_derived_types[i]->name, derived->name) == 0)
            return 1;
    }
    if (unit->imported_derived_type_count == unit->imported_derived_type_capacity) {
        const size_t capacity = unit->imported_derived_type_capacity == 0U
                                    ? 4U
                                    : unit->imported_derived_type_capacity * 2U;
        replacement = (F2cDerivedType **)realloc(unit->imported_derived_types,
                                                 capacity * sizeof(*replacement));
        if (replacement == NULL)
            return 0;
        unit->imported_derived_types = replacement;
        unit->imported_derived_type_capacity = capacity;
    }
    unit->imported_derived_types[unit->imported_derived_type_count++] = derived;
    return 1;
}

static int clone_module_symbol(Unit *unit, const Symbol *source, const char *local_name) {
    Symbol *target = f2c_ensure_symbol(unit, local_name);
    size_t dimension;
    if (target == NULL)
        return 0;
    target->type = source->type;
    target->kind_type = source->kind_type;
    target->kind = source->kind;
    target->value_category = source->value_category;
    target->shape = source->shape;
    target->rank = source->rank;
    target->parameter = source->parameter;
    target->saved = 1;
    target->allocatable = source->allocatable;
    target->pointer = source->pointer;
    target->procedure_pointer = source->procedure_pointer;
    target->polymorphic = source->polymorphic;
    target->target = source->target;
    target->module_entity = 1;
    target->deferred_character = source->deferred_character;
    target->derived_type = source->derived_type;
    target->declaration_line = source->declaration_line;
    target->initializer_syntax = source->initializer_syntax;
    target->character_length_syntax = source->character_length_syntax;
    free(target->c_name);
    target->c_name = f2c_strdup(source->c_name);
    free(target->initializer);
    target->initializer = source->initializer != NULL ? f2c_strdup(source->initializer) : NULL;
    free(target->character_length);
    target->character_length =
        source->character_length != NULL ? f2c_strdup(source->character_length) : NULL;
    free(target->derived_type_name);
    target->derived_type_name =
        source->derived_type_name != NULL ? f2c_strdup(source->derived_type_name) : NULL;
    free(target->c_type);
    target->c_type = source->c_type != NULL ? f2c_strdup(source->c_type) : NULL;
    if (target->c_name == NULL || (source->initializer != NULL && target->initializer == NULL) ||
        (source->character_length != NULL && target->character_length == NULL) ||
        (source->c_type != NULL && target->c_type == NULL))
        return 0;
    for (dimension = 0U; dimension < source->rank; ++dimension) {
        target->dimensions[dimension].kind = source->dimensions[dimension].kind;
        target->dimensions[dimension].lower = source->dimensions[dimension].lower != NULL
                                                  ? f2c_strdup(source->dimensions[dimension].lower)
                                                  : NULL;
        target->dimensions[dimension].upper = source->dimensions[dimension].upper != NULL
                                                  ? f2c_strdup(source->dimensions[dimension].upper)
                                                  : NULL;
        target->dimension_lower_syntax[dimension] = source->dimension_lower_syntax[dimension];
        target->dimension_upper_syntax[dimension] = source->dimension_upper_syntax[dimension];
    }
    return 1;
}

static Unit *find_module_procedure(Context *context, const Unit *module, const char *name) {
    size_t i;
    for (i = 0U; i < context->units.count; ++i) {
        Unit *candidate = &context->units.items[i];
        const char *visible =
            candidate->fortran_name != NULL ? candidate->fortran_name : candidate->name;
        if (candidate->begin > module->end && candidate->begin < module->container_end &&
            strcmp(visible, name) == 0)
            return candidate;
    }
    return NULL;
}

static int import_module_procedure(Unit *unit, Unit *procedure, const char *local_name) {
    Symbol *symbol = f2c_ensure_symbol(unit, local_name);
    Symbol *result = procedure->kind == UNIT_FUNCTION && procedure->result_name != NULL
                         ? f2c_find_symbol(procedure, procedure->result_name)
                         : NULL;
    size_t i;
    if (symbol == NULL)
        return 0;
    symbol->external = 1;
    symbol->external_declared = 1;
    symbol->external_signature_observed = 1;
    symbol->external_signature_explicit = 1;
    symbol->external_subroutine = procedure->kind == UNIT_SUBROUTINE;
    if (!f2c_copy_function_result_metadata(symbol, procedure))
        return 0;
    if (symbol->type == TYPE_CHARACTER && result != NULL && result->character_length != NULL) {
        char *length = f2c_strdup(result->character_length);
        if (length == NULL)
            return 0;
        free(symbol->character_length);
        symbol->character_length = length;
    }
    if (!f2c_symbol_resize_external_parameters(symbol, procedure->argument_count))
        return 0;
    symbol->external_parameter_count = procedure->argument_count;
    free(symbol->c_name);
    symbol->c_name = f2c_strdup(procedure->name);
    if (symbol->c_name == NULL)
        return 0;
    for (i = 0U; i < symbol->external_parameter_count; ++i) {
        Symbol *dummy = f2c_find_symbol(procedure, procedure->arguments[i]);
        symbol->external_parameter_types[i] = dummy != NULL ? dummy->type : TYPE_UNKNOWN;
        symbol->external_parameter_kinds[i] = dummy != NULL ? dummy->kind : 0;
        symbol->external_parameter_ranks[i] = dummy != NULL ? dummy->rank : 0U;
        symbol->external_parameter_intents[i] =
            dummy != NULL ? dummy->intent : F2C_INTENT_UNSPECIFIED;
        symbol->external_parameter_optional[i] = dummy != NULL && dummy->optional;
        symbol->external_parameter_allocatable[i] = dummy != NULL && dummy->allocatable;
        symbol->external_parameter_pointer[i] = dummy != NULL && dummy->pointer;
        symbol->external_parameter_descriptor[i] = f2c_symbol_uses_descriptor(dummy);
        symbol->external_parameter_derived_types[i] = dummy != NULL ? dummy->derived_type : NULL;
        symbol->external_parameter_polymorphic[i] = dummy != NULL && dummy->polymorphic;
        symbol->external_parameter_const[i] = dummy != NULL && dummy->intent == F2C_INTENT_IN;
    }
    return 1;
}

static int import_project_member(Context *context, Unit *unit, Unit *module, const char *local_name,
                                 const char *module_name, size_t line) {
    Symbol *symbol = f2c_find_symbol(module, module_name);
    F2cDerivedType *derived = f2c_find_derived_type(module, module_name);
    Unit *procedure = find_module_procedure(context, module, module_name);
    if (symbol != NULL)
        return clone_module_symbol(unit, symbol, local_name);
    if (derived != NULL) {
        if (strcmp(local_name, module_name) != 0) {
            f2c_diagnostic(context, line, 1,
                           "renaming a derived type in USE association is not yet supported");
            return 0;
        }
        return import_derived_type(unit, derived);
    }
    if (procedure != NULL)
        return import_module_procedure(unit, procedure, local_name);
    f2c_diagnostic(context, line, 1, "module '%s' has no public entity '%s'", module->name,
                   module_name);
    return 0;
}

static void import_entire_project_module(Context *context, Unit *unit, Unit *module, size_t line) {
    size_t i;
    for (i = 0U; i < module->derived_type_count; ++i) {
        if (!import_derived_type(unit, &module->derived_types[i]))
            f2c_diagnostic(context, line, 1, "out of memory importing module type");
    }
    for (i = 0U; i < module->symbol_count; ++i) {
        if (!clone_module_symbol(unit, &module->symbols[i], module->symbols[i].name))
            f2c_diagnostic(context, line, 1, "out of memory importing module entity");
    }
    for (i = 0U; i < context->units.count; ++i) {
        Unit *procedure = &context->units.items[i];
        const char *visible =
            procedure->fortran_name != NULL ? procedure->fortran_name : procedure->name;
        if (procedure != unit && procedure->begin > module->end &&
            procedure->begin < module->container_end &&
            !import_module_procedure(unit, procedure, visible))
            f2c_diagnostic(context, line, 1, "out of memory importing module procedure");
    }
}

void f2c_import_module(Context *context, Unit *unit, Line *source_line) {
    UseSyntax syntax;
    char *module_name;
    Unit *module;
    if (!parse_use_syntax(source_line, &syntax))
        return;
    if (!syntax.valid || syntax.module_name == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                  source_line->token_count != 0U ? &source_line->tokens[0] : NULL,
                                  1, "malformed USE statement");
        return;
    }
    module_name = f2c_token_text(syntax.module_name);
    if (module_name == NULL) {
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                            "out of memory parsing USE statement");
        return;
    }
    if (strcmp(module_name, "la_constants") == 0) {
        free(module_name);
        import_la_constants(context, unit, source_line, &syntax);
        return;
    }
    module = find_project_module(context, module_name);
    if (module == NULL &&
        (strcmp(module_name, "iso_fortran_env") == 0 || strcmp(module_name, "iso_c_binding") == 0 ||
         strncmp(module_name, "ieee_", strlen("ieee_")) == 0 || syntax.intrinsic)) {
        free(module_name);
        return;
    }
    free(module_name);
    if (module == NULL) {
        /* A project translation may intentionally consume one source at a time.  In that mode
         * the provider of a non-intrinsic module is external to this translation request; any
         * names that are not covered by the intrinsic registry retain their ordinary external
         * procedure handling.  When the provider is present in the same request, the branch
         * below imports its typed entities and interfaces. */
        return;
    }
    if (syntax.association_begin == source_line->token_count) {
        import_entire_project_module(context, unit, module, source_line->number);
    } else {
        size_t cursor = syntax.association_begin;
        int status;
        if (!syntax.only)
            import_entire_project_module(context, unit, module, source_line->number);
        do {
            const F2cToken *local_token;
            const F2cToken *remote_token;
            int renamed;
            char *local_name;
            char *remote_name;
            status =
                next_use_association(source_line, &cursor, &local_token, &remote_token, &renamed);
            if (status <= 0)
                break;
            local_name = f2c_token_text(local_token);
            remote_name = f2c_token_text(remote_token);
            if (local_name == NULL || remote_name == NULL) {
                f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                    "out of memory importing module association");
                free(local_name);
                free(remote_name);
                return;
            }
            (void)import_project_member(context, unit, module, local_name, remote_name,
                                        source_line->number);
            free(local_name);
            free(remote_name);
            (void)renamed;
        } while (status > 0);
        if (status < 0)
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      source_line->token_count != 0U ? &source_line->tokens[0]
                                                                     : NULL,
                                      1, "malformed USE association list");
    }
}

void f2c_import_host_module(Context *context, Unit *unit) {
    size_t i;
    if (unit->signature_host != NULL) {
        Unit *host = unit->signature_host;
        for (i = 0U; i < host->derived_type_count; ++i)
            (void)import_derived_type(unit, &host->derived_types[i]);
        for (i = 0U; i < host->imported_derived_type_count; ++i)
            (void)import_derived_type(unit, host->imported_derived_types[i]);
    }
    if (unit->internal && unit->host_index < context->units.count) {
        Unit *host = &context->units.items[unit->host_index];
        for (i = 0U; i < host->derived_type_count; ++i)
            (void)import_derived_type(unit, &host->derived_types[i]);
    }
    for (i = 0U; i < context->modules.count; ++i) {
        Unit *module = &context->modules.items[i];
        if (unit->begin > module->end && unit->begin < module->container_end) {
            import_entire_project_module(context, unit, module,
                                         context->lines.items[unit->begin].number);
            return;
        }
    }
}

int f2c_discover_modules(Context *context) {
    size_t line_index;
    for (line_index = 0U; line_index < context->lines.count; ++line_index) {
        F2cModuleHeaderSyntax header;
        F2cModuleHeaderParseStatus header_status;
        const F2cToken *name_token;
        char *name;
        size_t end;
        size_t contains;
        Unit *replacement;
        Unit module;
        Unit opening;
        header_status = f2c_parse_module_header_syntax(&context->lines.items[line_index], &header);
        if (header_status == F2C_MODULE_HEADER_NOT_MATCHED)
            continue;
        if (header_status == F2C_MODULE_HEADER_INVALID) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX,
                                      &context->lines.items[line_index], header.error_token, 1,
                                      "MODULE statement requires exactly one valid name");
            continue;
        }
        name_token = header.name;
        name = f2c_token_text(name_token);
        if (name == NULL)
            continue;
        if (strcmp(name, "la_constants") == 0) {
            free(name);
            continue;
        }
        memset(&opening, 0, sizeof(opening));
        opening.kind = UNIT_MODULE;
        opening.name = name;
        contains = context->lines.count;
        {
            size_t derived_type_depth = 0U;
            for (end = line_index + 1U; end < context->lines.count; ++end) {
                Line *candidate = &context->lines.items[end];
                if (derived_type_depth != 0U && f2c_derived_type_end_tokens(candidate)) {
                    --derived_type_depth;
                    continue;
                }
                if (f2c_derived_type_start_tokens(candidate)) {
                    ++derived_type_depth;
                    continue;
                }
                if (derived_type_depth != 0U)
                    continue;
                if (contains == context->lines.count && f2c_contains_tokens(candidate))
                    contains = end;
                {
                    F2cUnitEndSyntax end_syntax;
                    const F2cUnitEndParseStatus end_status =
                        f2c_parse_unit_end_syntax(candidate, &end_syntax);
                    if (end_status != F2C_UNIT_END_NOT_MATCHED && end_syntax.has_kind &&
                        end_syntax.kind == F2C_UNIT_SYNTAX_MODULE) {
                        (void)f2c_match_program_unit_end(context, candidate, &opening);
                        break;
                    }
                }
            }
        }
        if (end == context->lines.count) {
            f2c_diagnostic(context, context->lines.items[line_index].number, 1,
                           "unterminated module '%s'", name);
            free(name);
            continue;
        }
        if (context->modules.count == context->modules.capacity) {
            const size_t capacity =
                context->modules.capacity == 0U ? 4U : context->modules.capacity * 2U;
            replacement = (Unit *)realloc(context->modules.items, capacity * sizeof(*replacement));
            if (replacement == NULL) {
                free(name);
                return 0;
            }
            context->modules.items = replacement;
            context->modules.capacity = capacity;
        }
        memset(&module, 0, sizeof(module));
        module.context = context;
        module.kind = UNIT_MODULE;
        module.header_span = header.span;
        module.name_span = name_token->span;
        module.name = name;
        module.fortran_name = f2c_strdup(name);
        module.begin = line_index;
        module.end = contains != context->lines.count ? contains : end;
        module.container_end = end;
        module.options.source_name = f2c_strdup(context->lines.items[line_index].source_name);
        if (module.fortran_name == NULL || module.options.source_name == NULL) {
            free(module.name);
            free(module.fortran_name);
            free((char *)module.options.source_name);
            return 0;
        }
        context->modules.items[context->modules.count++] = module;
        line_index = end;
    }
    return 1;
}

int f2c_has_la_constants_module(const Context *context) {
    size_t i;
    for (i = 0U; i < context->lines.count; ++i) {
        F2cModuleHeaderSyntax header;
        if (f2c_parse_module_header_syntax(&context->lines.items[i], &header) ==
                F2C_MODULE_HEADER_PARSED &&
            f2c_token_equals(header.name, "la_constants"))
            return 1;
    }
    return 0;
}

int f2c_has_supported_module(const Context *context) {
    return f2c_has_la_constants_module(context) || context->modules.count != 0U;
}

int f2c_supported_module_needs_complex(const Context *context) {
    return f2c_has_la_constants_module(context);
}
