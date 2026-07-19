#include "ast/declaration/use.h"
#include "frontend/module/access.h"
#include "frontend/module_constants.h"
#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

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

static int use_name_is_renamed(const F2cUseStatementSyntax *syntax, const char *name) {
    size_t index;
    if (syntax == NULL)
        return 0;
    for (index = 0U; index < syntax->item_count; ++index) {
        const F2cUseAssociationSyntax *association = &syntax->items[index];
        if (association->renamed && association->remote.kind == F2C_USE_DESIGNATOR_NAME &&
            f2c_token_equals(association->remote.name, name))
            return 1;
    }
    return 0;
}

static void import_la_constant(Context *context, Unit *unit, const char *local_name,
                               const char *module_name, const F2cSourceSpan *span) {
    const F2cModuleConstant *constant = find_la_constant(module_name);
    Symbol *symbol;
    Buffer c_name = {0};
    char *resolved_c_name;
    if (constant == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, span, 1,
                                 "LA_CONSTANTS has no member '%s'", module_name);
        return;
    }
    f2c_buffer_printf(&c_name, "f2c_la_constants_%s", module_name);
    resolved_c_name = f2c_buffer_take(&c_name);
    if (resolved_c_name == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                                 "out of memory importing '%s'", local_name);
        return;
    }
    symbol = f2c_find_symbol(unit, local_name);
    if (symbol != NULL) {
        const int same_entity = symbol->use_associated && symbol->c_name != NULL &&
                                strcmp(symbol->c_name, resolved_c_name) == 0;
        free(resolved_c_name);
        if (!same_entity)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, span, 1,
                                     "USE local name '%s' denotes conflicting entities",
                                     local_name);
        return;
    }
    symbol = f2c_ensure_symbol(unit, local_name);
    if (symbol == NULL) {
        free(resolved_c_name);
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                                 "out of memory importing '%s'", local_name);
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
    symbol->module_entity = 1;
    symbol->use_associated = 1;
    symbol->access = F2C_ACCESS_UNSPECIFIED;
    memset(&symbol->access_span, 0, sizeof(symbol->access_span));
    free(symbol->c_name);
    symbol->c_name = resolved_c_name;
    free(symbol->initializer);
    symbol->initializer = f2c_strdup(constant->initializer);
    f2c_expr_free(symbol->initializer_expression);
    symbol->initializer_expression = parse_compiler_constant(unit, constant->initializer);
    if (symbol->c_name == NULL || symbol->initializer == NULL ||
        symbol->initializer_expression == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, 1,
                                 "unable to build typed intrinsic-module constant '%s'",
                                 local_name);
    }
    if (constant->type == TYPE_CHARACTER) {
        free(symbol->character_length);
        symbol->character_length = f2c_strdup("1");
    }
}

static void import_la_constants(Context *context, Unit *unit, const F2cUseStatementSyntax *syntax) {
    size_t index;
    if (syntax->only_token == NULL) {
        for (index = 0U; index < sizeof(la_constants) / sizeof(la_constants[0]); ++index) {
            const F2cModuleConstant *constant = &la_constants[index];
            if (!use_name_is_renamed(syntax, constant->name))
                import_la_constant(context, unit, constant->name, constant->name,
                                   &syntax->module_name->span);
        }
    }
    for (index = 0U; index < syntax->item_count; ++index) {
        const F2cUseAssociationSyntax *association = &syntax->items[index];
        char *local_name;
        char *module_name;
        if (association->local.kind != F2C_USE_DESIGNATOR_NAME ||
            association->remote.kind != F2C_USE_DESIGNATOR_NAME) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, &association->span, 1,
                                     "LA_CONSTANTS exposes only named entities");
            continue;
        }
        local_name = f2c_token_text(association->local.name);
        module_name = f2c_token_text(association->remote.name);
        if (local_name == NULL || module_name == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &association->span, 1,
                                     "out of memory importing LA_CONSTANTS");
            free(local_name);
            free(module_name);
            return;
        }
        import_la_constant(context, unit, local_name, module_name, &association->span);
        free(local_name);
        free(module_name);
    }
}

static Unit *find_project_module(Context *context, const char *name) {
    size_t i;
    for (i = 0U; i < context->modules.count; ++i) {
        if (strcmp(context->modules.items[i].name, name) == 0)
            return &context->modules.items[i];
    }
    return NULL;
}

static int import_derived_type(Unit *unit, F2cDerivedType *derived, const char *local_name,
                               const F2cSourceSpan *association_span) {
    F2cImportedDerivedType *replacement;
    char *owned_name;
    size_t i;
    for (i = 0U; i < unit->derived_type_count; ++i) {
        if (strcmp(unit->derived_types[i].name, local_name) == 0)
            return &unit->derived_types[i] == derived ? 1 : -1;
    }
    for (i = 0U; i < unit->imported_derived_type_count; ++i) {
        F2cImportedDerivedType *existing = &unit->imported_derived_types[i];
        if (strcmp(existing->local_name, local_name) == 0)
            return existing->type == derived ? 1 : -1;
    }
    owned_name = f2c_strdup(local_name);
    if (owned_name == NULL)
        return 0;
    if (unit->imported_derived_type_count == unit->imported_derived_type_capacity) {
        const size_t capacity = unit->imported_derived_type_capacity == 0U
                                    ? 4U
                                    : unit->imported_derived_type_capacity * 2U;
        if (capacity < unit->imported_derived_type_capacity ||
            capacity > SIZE_MAX / sizeof(*replacement)) {
            free(owned_name);
            return 0;
        }
        replacement = (F2cImportedDerivedType *)realloc(unit->imported_derived_types,
                                                        capacity * sizeof(*replacement));
        if (replacement == NULL) {
            free(owned_name);
            return 0;
        }
        unit->imported_derived_types = replacement;
        unit->imported_derived_type_capacity = capacity;
    }
    memset(&unit->imported_derived_types[unit->imported_derived_type_count], 0,
           sizeof(unit->imported_derived_types[unit->imported_derived_type_count]));
    unit->imported_derived_types[unit->imported_derived_type_count].local_name = owned_name;
    unit->imported_derived_types[unit->imported_derived_type_count].type = derived;
    if (association_span != NULL)
        unit->imported_derived_types[unit->imported_derived_type_count].association_span =
            *association_span;
    else
        memset(&unit->imported_derived_types[unit->imported_derived_type_count].association_span, 0,
               sizeof(F2cSourceSpan));
    ++unit->imported_derived_type_count;
    return 1;
}

static int clone_module_symbol(Unit *unit, const Symbol *source, const char *local_name) {
    Symbol *target = f2c_find_symbol(unit, local_name);
    char *procedure_interface_name = source->procedure_interface_name != NULL
                                         ? f2c_strdup(source->procedure_interface_name)
                                         : NULL;
    size_t dimension;
    size_t parameter;
    if (target != NULL) {
        const int same_entity = target->use_associated && target->c_name != NULL &&
                                source->c_name != NULL &&
                                strcmp(target->c_name, source->c_name) == 0;
        free(procedure_interface_name);
        return same_entity ? 1 : -1;
    }
    target = f2c_ensure_symbol(unit, local_name);
    if (target == NULL ||
        (source->procedure_interface_name != NULL && procedure_interface_name == NULL)) {
        free(procedure_interface_name);
        return 0;
    }
    if (!f2c_symbol_resize_external_parameters(
            target, source->external ? source->external_parameter_count : 0U)) {
        free(procedure_interface_name);
        return 0;
    }
    target->type = source->type;
    target->kind_type = source->kind_type;
    target->kind = source->kind;
    target->value_category = source->value_category;
    target->shape = source->shape;
    target->rank = source->rank;
    target->intent = source->intent;
    target->parameter = source->parameter;
    target->external = source->external;
    target->external_declared = source->external_declared;
    target->external_subroutine = source->external_subroutine;
    target->external_result_allocatable = source->external_result_allocatable;
    target->external_result_rank = source->external_result_rank;
    target->external_signature_observed = source->external_signature_observed;
    target->external_signature_explicit = source->external_signature_explicit;
    target->procedure_interface = source->procedure_interface;
    free(target->procedure_interface_name);
    target->procedure_interface_name = procedure_interface_name;
    target->saved = !source->external;
    target->allocatable = source->allocatable;
    target->pointer = source->pointer;
    target->procedure_pointer = source->procedure_pointer;
    target->polymorphic = source->polymorphic;
    target->target = source->target;
    target->module_entity = !source->external;
    target->use_associated = 1;
    target->access = F2C_ACCESS_UNSPECIFIED;
    memset(&target->access_span, 0, sizeof(target->access_span));
    target->deferred_character = source->deferred_character;
    target->optional = source->optional;
    target->derived_type = source->derived_type;
    target->declaration_line = source->declaration_line;
    target->initializer_syntax = source->initializer_syntax;
    target->character_length_syntax = source->character_length_syntax;
    target->declaration_span = source->declaration_span;
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
        (source->derived_type_name != NULL && target->derived_type_name == NULL) ||
        (source->c_type != NULL && target->c_type == NULL))
        return 0;
    for (dimension = 0U; dimension < source->rank; ++dimension) {
        char *lower = source->dimensions[dimension].lower != NULL
                          ? f2c_strdup(source->dimensions[dimension].lower)
                          : NULL;
        char *upper = source->dimensions[dimension].upper != NULL
                          ? f2c_strdup(source->dimensions[dimension].upper)
                          : NULL;
        if ((source->dimensions[dimension].lower != NULL && lower == NULL) ||
            (source->dimensions[dimension].upper != NULL && upper == NULL)) {
            free(lower);
            free(upper);
            return 0;
        }
        free(target->dimensions[dimension].lower);
        free(target->dimensions[dimension].upper);
        target->dimensions[dimension].kind = source->dimensions[dimension].kind;
        target->dimensions[dimension].lower = lower;
        target->dimensions[dimension].upper = upper;
        target->dimension_lower_syntax[dimension] = source->dimension_lower_syntax[dimension];
        target->dimension_upper_syntax[dimension] = source->dimension_upper_syntax[dimension];
    }
    target->external_parameter_count = source->external ? source->external_parameter_count : 0U;
    for (parameter = 0U; parameter < target->external_parameter_count; ++parameter) {
        target->external_parameter_types[parameter] = source->external_parameter_types[parameter];
        target->external_parameter_kinds[parameter] = source->external_parameter_kinds[parameter];
        target->external_parameter_ranks[parameter] = source->external_parameter_ranks[parameter];
        target->external_parameter_intents[parameter] =
            source->external_parameter_intents[parameter];
        target->external_parameter_optional[parameter] =
            source->external_parameter_optional[parameter];
        target->external_parameter_allocatable[parameter] =
            source->external_parameter_allocatable[parameter];
        target->external_parameter_pointer[parameter] =
            source->external_parameter_pointer[parameter];
        target->external_parameter_descriptor[parameter] =
            source->external_parameter_descriptor[parameter];
        target->external_parameter_derived_types[parameter] =
            source->external_parameter_derived_types[parameter];
        target->external_parameter_polymorphic[parameter] =
            source->external_parameter_polymorphic[parameter];
        target->external_parameter_procedures[parameter] =
            source->external_parameter_procedures[parameter];
        target->external_parameter_const[parameter] = source->external_parameter_const[parameter];
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
    Symbol *symbol = f2c_find_symbol(unit, local_name);
    Symbol *result = procedure->kind == UNIT_FUNCTION && procedure->result_name != NULL
                         ? f2c_find_symbol(procedure, procedure->result_name)
                         : NULL;
    size_t i;
    if (symbol != NULL)
        return symbol->use_associated && symbol->c_name != NULL && procedure->name != NULL &&
                       strcmp(symbol->c_name, procedure->name) == 0
                   ? 1
                   : -1;
    symbol = f2c_ensure_symbol(unit, local_name);
    if (symbol == NULL)
        return 0;
    symbol->external = 1;
    symbol->external_declared = 1;
    symbol->external_signature_observed = 1;
    symbol->external_signature_explicit = 1;
    symbol->use_associated = 1;
    symbol->access = F2C_ACCESS_UNSPECIFIED;
    memset(&symbol->access_span, 0, sizeof(symbol->access_span));
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
                                 const char *module_name,
                                 const F2cUseAssociationSyntax *association) {
    Symbol *symbol = f2c_find_symbol(module, module_name);
    F2cDerivedType *derived = f2c_find_derived_type(module, module_name);
    Unit *procedure = find_module_procedure(context, module, module_name);
    if (symbol != NULL) {
        int imported;
        if (!f2c_module_symbol_is_public(module, symbol)) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &association->remote.span, 1,
                                     "module entity '%s' is PRIVATE in module '%s'", module_name,
                                     module->name);
            return 0;
        }
        imported = clone_module_symbol(unit, symbol, local_name);
        if (imported < 0) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &association->local.span, 1,
                                     "USE local name '%s' denotes conflicting entities",
                                     local_name);
            return 0;
        }
        if (imported == 0) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                     &association->local.span, 1,
                                     "out of memory importing module entity '%s'", local_name);
            return 0;
        } else {
            return 1;
        }
    }
    if (derived != NULL) {
        if (!f2c_module_derived_type_is_public(module, module_name, derived)) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &association->remote.span, 1,
                                     "derived type '%s' is PRIVATE in module '%s'", module_name,
                                     module->name);
            return 0;
        }
        const int imported =
            import_derived_type(unit, derived, local_name, &association->local.span);
        if (imported < 0) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &association->local.span, 1,
                                     "USE local name '%s' denotes conflicting derived types",
                                     local_name);
            return 0;
        }
        if (imported == 0)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                     &association->local.span, 1,
                                     "out of memory importing derived type '%s'", local_name);
        return imported;
    }
    if (procedure != NULL) {
        int imported;
        if (!f2c_module_procedure_is_public(module, procedure)) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &association->remote.span, 1,
                                     "module procedure '%s' is PRIVATE in module '%s'", module_name,
                                     module->name);
            return 0;
        }
        imported = import_module_procedure(unit, procedure, local_name);
        if (imported < 0) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &association->local.span, 1,
                                     "USE local name '%s' denotes conflicting entities",
                                     local_name);
            return 0;
        }
        if (imported == 0) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                     &association->local.span, 1,
                                     "out of memory importing module procedure '%s'", local_name);
            return 0;
        }
        return 1;
    }
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &association->remote.span, 1,
                             "module '%s' has no public entity '%s'", module->name, module_name);
    return 0;
}

static void import_entire_project_module(Context *context, Unit *unit, Unit *module, size_t line,
                                         const F2cUseStatementSyntax *syntax) {
    size_t i;
    for (i = 0U; i < module->derived_type_count; ++i) {
        int imported;
        if (use_name_is_renamed(syntax, module->derived_types[i].name))
            continue;
        if (syntax != NULL && !f2c_module_derived_type_is_public(
                                  module, module->derived_types[i].name, &module->derived_types[i]))
            continue;
        imported = import_derived_type(unit, &module->derived_types[i],
                                       module->derived_types[i].name, NULL);
        if (imported == 0) {
            f2c_diagnostic(context, line, 1, "out of memory importing module type");
        } else if (imported < 0) {
            if (syntax != NULL)
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &syntax->module_name->span, 1,
                    "module association creates conflicting derived type name '%s'",
                    module->derived_types[i].name);
            else
                f2c_diagnostic(context, line, 1,
                               "host association creates conflicting derived type name '%s'",
                               module->derived_types[i].name);
        }
    }
    for (i = 0U; i < module->imported_derived_type_count; ++i) {
        F2cImportedDerivedType *source = &module->imported_derived_types[i];
        int imported;
        if (use_name_is_renamed(syntax, source->local_name))
            continue;
        if (syntax != NULL &&
            !f2c_module_derived_type_is_public(module, source->local_name, source->type))
            continue;
        imported =
            import_derived_type(unit, source->type, source->local_name, &source->association_span);
        if (imported == 0) {
            f2c_diagnostic(context, line, 1, "out of memory importing module type");
        } else if (imported < 0) {
            f2c_diagnostic(context, line, 1,
                           "%s association creates conflicting derived type name '%s'",
                           syntax != NULL ? "module" : "host", source->local_name);
        }
    }
    for (i = 0U; i < module->symbol_count; ++i) {
        int imported;
        if (use_name_is_renamed(syntax, module->symbols[i].name))
            continue;
        if (syntax != NULL && !f2c_module_symbol_is_public(module, &module->symbols[i]))
            continue;
        imported = clone_module_symbol(unit, &module->symbols[i], module->symbols[i].name);
        if (imported == 0) {
            f2c_diagnostic(context, line, 1, "out of memory importing module entity");
        } else if (imported < 0) {
            if (syntax != NULL)
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC,
                                         &syntax->module_name->span, 1,
                                         "module association creates conflicting entity name '%s'",
                                         module->symbols[i].name);
            else
                f2c_diagnostic(context, line, 1,
                               "host association creates conflicting entity name '%s'",
                               module->symbols[i].name);
        }
    }
    for (i = 0U; i < context->units.count; ++i) {
        Unit *procedure = &context->units.items[i];
        const char *visible =
            procedure->fortran_name != NULL ? procedure->fortran_name : procedure->name;
        if (procedure != unit && procedure->begin > module->end &&
            procedure->begin < module->container_end && !use_name_is_renamed(syntax, visible) &&
            (syntax == NULL || f2c_module_procedure_is_public(module, procedure))) {
            const int imported = import_module_procedure(unit, procedure, visible);
            if (imported == 0) {
                f2c_diagnostic(context, line, 1, "out of memory importing module procedure");
            } else if (imported < 0) {
                f2c_diagnostic(context, line, 1,
                               "%s association creates conflicting procedure name '%s'",
                               syntax != NULL ? "module" : "host", visible);
            }
        }
    }
}

static void report_use_error(Context *context, const Line *line,
                             const F2cUseStatementSyntax *syntax) {
    const F2cToken *token = syntax->error_token != NULL ? syntax->error_token : syntax->keyword;
    const char *message = "malformed USE statement";
    switch (syntax->error) {
    case F2C_USE_ERROR_MODULE_NATURE:
        message = "USE module nature must be INTRINSIC or NON_INTRINSIC";
        break;
    case F2C_USE_ERROR_DOUBLE_COLON:
        message = "USE module nature must be followed by ::";
        break;
    case F2C_USE_ERROR_MODULE_NAME:
        message = "USE statement requires a module name";
        break;
    case F2C_USE_ERROR_LIST_SEPARATOR:
        message = "malformed USE association separator";
        break;
    case F2C_USE_ERROR_ONLY_COLON:
        message = "ONLY must be followed by a colon";
        break;
    case F2C_USE_ERROR_ITEM:
        message = "malformed USE association designator";
        break;
    case F2C_USE_ERROR_RENAME_REQUIRED:
        message = "USE rename list item requires =>";
        break;
    case F2C_USE_ERROR_RENAME_TARGET:
        message = "USE rename requires a valid target designator";
        break;
    case F2C_USE_ERROR_RENAME_KIND:
        message = "USE rename designator kinds do not match";
        break;
    case F2C_USE_ERROR_DUPLICATE_LOCAL_NAME:
        message = "duplicate local name in USE association list";
        break;
    case F2C_USE_ERROR_TRAILING_COMMA:
        message = "USE association list cannot end with a comma";
        break;
    case F2C_USE_ERROR_NONE:
        break;
    }
    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1, "%s", message);
}

void f2c_import_module(Context *context, Unit *unit, Line *source_line) {
    F2cUseStatementSyntax syntax;
    const F2cUseStatementStatus status = f2c_parse_use_statement_syntax(source_line, &syntax);
    char *module_name = NULL;
    Unit *module = NULL;
    size_t index;
    if (status == F2C_USE_STATEMENT_NOT_MATCHED)
        return;
    if (status == F2C_USE_STATEMENT_NO_MEMORY) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax.span, 1,
                                 "out of memory parsing USE statement");
        f2c_use_statement_syntax_discard(&syntax);
        return;
    }
    if (status == F2C_USE_STATEMENT_INVALID) {
        report_use_error(context, source_line, &syntax);
        f2c_use_statement_syntax_discard(&syntax);
        return;
    }
    module_name = f2c_token_text(syntax.module_name);
    if (module_name == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax.span, 1,
                                 "out of memory lowering USE statement");
        goto cleanup;
    }
    if (syntax.nature != F2C_USE_NATURE_INTRINSIC && strcmp(module_name, "la_constants") == 0) {
        import_la_constants(context, unit, &syntax);
        goto cleanup;
    }
    if (syntax.nature == F2C_USE_NATURE_INTRINSIC ||
        (syntax.nature == F2C_USE_NATURE_UNSPECIFIED &&
         (strcmp(module_name, "iso_fortran_env") == 0 ||
          strcmp(module_name, "iso_c_binding") == 0 ||
          strncmp(module_name, "ieee_", strlen("ieee_")) == 0))) {
        goto cleanup;
    }
    module = find_project_module(context, module_name);
    if (module == NULL) {
        /* A project translation may intentionally consume one source at a time.  In that mode
         * the provider of a non-intrinsic module is external to this translation request; any
         * names that are not covered by the intrinsic registry retain their ordinary external
         * procedure handling.  When the provider is present in the same request, the branch
         * below imports its typed entities and interfaces. */
        goto cleanup;
    }
    if (syntax.only_token == NULL)
        import_entire_project_module(context, unit, module, source_line->number, &syntax);
    for (index = 0U; index < syntax.item_count; ++index) {
        const F2cUseAssociationSyntax *association = &syntax.items[index];
        char *local_name;
        char *remote_name;
        if (association->local.kind != F2C_USE_DESIGNATOR_NAME ||
            association->remote.kind != F2C_USE_DESIGNATOR_NAME) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, &association->span, 1,
                                     "generic USE association requires module generic binding");
            continue;
        }
        local_name = f2c_token_text(association->local.name);
        remote_name = f2c_token_text(association->remote.name);
        if (local_name == NULL || remote_name == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &association->span, 1,
                                     "out of memory importing module association");
            free(local_name);
            free(remote_name);
            goto cleanup;
        }
        (void)import_project_member(context, unit, module, local_name, remote_name, association);
        free(local_name);
        free(remote_name);
    }

cleanup:
    free(module_name);
    f2c_use_statement_syntax_discard(&syntax);
}

void f2c_import_host_module(Context *context, Unit *unit) {
    size_t i;
    if (unit->signature_host != NULL) {
        Unit *host = unit->signature_host;
        for (i = 0U; i < host->derived_type_count; ++i)
            (void)import_derived_type(unit, &host->derived_types[i], host->derived_types[i].name,
                                      NULL);
        for (i = 0U; i < host->imported_derived_type_count; ++i) {
            F2cImportedDerivedType *imported = &host->imported_derived_types[i];
            (void)import_derived_type(unit, imported->type, imported->local_name,
                                      &imported->association_span);
        }
    }
    if (unit->internal && unit->host_index < context->units.count) {
        Unit *host = &context->units.items[unit->host_index];
        for (i = 0U; i < host->derived_type_count; ++i)
            (void)import_derived_type(unit, &host->derived_types[i], host->derived_types[i].name,
                                      NULL);
    }
    for (i = 0U; i < context->modules.count; ++i) {
        Unit *module = &context->modules.items[i];
        if (unit->begin > module->end && unit->begin < module->container_end) {
            import_entire_project_module(context, unit, module,
                                         context->lines.items[unit->begin].number, NULL);
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
            size_t interface_depth = 0U;
            for (end = line_index + 1U; end < context->lines.count; ++end) {
                Line *candidate = &context->lines.items[end];
                if (f2c_interface_start_tokens(candidate)) {
                    ++interface_depth;
                    candidate->interface_depth = interface_depth;
                    continue;
                }
                if (interface_depth != 0U) {
                    candidate->interface_depth = interface_depth;
                    if (f2c_interface_end_tokens(candidate))
                        --interface_depth;
                    continue;
                }
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
