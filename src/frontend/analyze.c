#include "frontend/private.h"

#include "ast/declaration/use.h"
#include "frontend/module/access.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void header_diagnostic(Context *context, const F2cSourceSpan *span, size_t fallback_line,
                              const char *message) {
    if (span != NULL && span->begin.line != 0U)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, span, 1, "%s", message);
    else
        f2c_diagnostic(context, fallback_line, 1, "%s", message);
}

void f2c_analyze_module(Context *context, Unit *unit) {
    size_t i;
    f2c_prepare_implicit_map(context, unit);
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (f2c_interface_start_tokens(&context->lines.items[i])) {
            while (i + 1U < unit->end && !f2c_interface_end_tokens(&context->lines.items[i + 1U]))
                ++i;
            if (i + 1U < unit->end)
                ++i;
            continue;
        }
        f2c_import_module(context, unit, &context->lines.items[i]);
    }
    f2c_parse_derived_type_definitions(context, unit);
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (f2c_interface_start_tokens(&context->lines.items[i])) {
            while (i + 1U < unit->end && !f2c_interface_end_tokens(&context->lines.items[i + 1U]))
                ++i;
            if (i + 1U < unit->end)
                ++i;
            continue;
        }
        if (context->lines.items[i].interface_depth != 0U)
            continue;
        if (f2c_line_in_derived_type(unit, i))
            continue;
        f2c_parse_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_dimension_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_parameter_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_save_declaration(context, unit, &context->lines.items[i]);
    }
    f2c_parse_explicit_interfaces(context, unit);
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (context->lines.items[i].interface_depth == 0U && !f2c_line_in_derived_type(unit, i))
            f2c_parse_procedure_declaration(context, unit, &context->lines.items[i]);
    }
    f2c_parse_access_statements(context, unit);
    f2c_finalize_module_accessibility(context, unit);
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        Buffer c_name = {0};
        if (symbol->external)
            continue;
        if (symbol->use_associated)
            continue;
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
    unit->phase = F2C_UNIT_SYMBOLS_RESOLVED;
}

void f2c_analyze_unit(Context *context, Unit *unit) {
    size_t i;
    Symbol *function_result = NULL;
    const size_t header_line = context->lines.items[unit->begin].number;
    if (unit->pure && unit->impure)
        header_diagnostic(context, &unit->impure_span, header_line,
                          "a procedure cannot have both PURE and IMPURE prefixes");
    if (unit->kind == UNIT_PROGRAM) {
        if (unit->pure)
            header_diagnostic(context, &unit->pure_span, header_line,
                              "PURE is not valid on a PROGRAM");
        if (unit->elemental)
            header_diagnostic(context, &unit->elemental_span, header_line,
                              "ELEMENTAL is not valid on a PROGRAM");
        if (unit->impure)
            header_diagnostic(context, &unit->impure_span, header_line,
                              "IMPURE is not valid on a PROGRAM");
        if (unit->recursive)
            header_diagnostic(context, &unit->recursive_span, header_line,
                              "RECURSIVE is not valid on a PROGRAM");
        if (unit->module_procedure)
            header_diagnostic(context, &unit->module_procedure_span, header_line,
                              "MODULE is not valid on a PROGRAM");
    }
    if (unit->elemental && unit->recursive)
        header_diagnostic(context, &unit->recursive_span, header_line,
                          "an ELEMENTAL procedure cannot also be RECURSIVE");
    if (unit->elemental && !unit->impure)
        unit->pure = 1;
    if (!unit->interface_body)
        f2c_parse_explicit_interfaces(context, unit);
    f2c_prepare_implicit_map(context, unit);
    f2c_import_host_module(context, unit);
    f2c_parse_derived_type_definitions(context, unit);
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_ensure_symbol_impl(unit, unit->arguments[i]);
        if (symbol != NULL) {
            symbol->argument = 1;
            symbol->first_seen_line = context->lines.items[unit->begin].number;
        }
    }
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (!f2c_unit_line_is_active(unit, &context->lines.items[i]))
            continue;
        if (f2c_line_in_derived_type(unit, i))
            continue;
        f2c_import_module(context, unit, &context->lines.items[i]);
        f2c_parse_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_optional_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_dimension_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_external_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_procedure_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_parameter_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_save_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_equivalence_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_common_declaration(context, unit, &context->lines.items[i]);
        f2c_parse_namelist_declaration(context, unit, &context->lines.items[i]);
        f2c_mark_call_targets(unit, &context->lines.items[i]);
    }
    f2c_parse_access_statements(context, unit);
    f2c_discover_implicit_symbols(context, unit);
    {
        int in_specification_part = 1;
        for (i = unit->begin + 1U; i < unit->end; ++i) {
            Line *line = &context->lines.items[i];
            if (!f2c_unit_line_is_active(unit, line) || f2c_line_in_derived_type(unit, i))
                continue;
            if (in_specification_part && f2c_mark_statement_function_symbols(unit, line))
                continue;
            if (!f2c_declaration_tokens(line) && !f2c_use_statement_candidate(line))
                in_specification_part = 0;
        }
    }
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        if (f2c_unit_line_is_active(unit, &context->lines.items[i]))
            f2c_mark_function_references(unit, &context->lines.items[i]);
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        size_t line_index;
        if (!unit->symbols[i].external)
            continue;
        for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
            if (!f2c_unit_line_is_active(unit, &context->lines.items[line_index]))
                continue;
            f2c_infer_external_signature(unit, &unit->symbols[i],
                                         &context->lines.items[line_index]);
        }
    }
    if (unit->kind == UNIT_FUNCTION && unit->result_name != NULL) {
        function_result = f2c_ensure_symbol_impl(unit, unit->result_name);
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
            unit->result_character_length != NULL && function_result->character_length == NULL) {
            function_result->character_length = f2c_strdup(unit->result_character_length);
            function_result->character_length_syntax = unit->result_character_length_syntax;
        }
        if (function_result != NULL && unit->return_type == TYPE_DERIVED &&
            unit->result_derived_type_name != NULL) {
            F2cDerivedType *derived = f2c_find_derived_type(unit, unit->result_derived_type_name);
            char *derived_name = f2c_strdup(unit->result_derived_type_name);
            char *c_type =
                derived != NULL && derived->c_name != NULL ? f2c_strdup(derived->c_name) : NULL;
            if (derived == NULL || derived_name == NULL || c_type == NULL) {
                free(derived_name);
                free(c_type);
                f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                               "unknown or unavailable derived function result type '%s'",
                               unit->result_derived_type_name);
            } else {
                function_result->derived_type = derived;
                free(function_result->derived_type_name);
                function_result->derived_type_name = derived_name;
                free(function_result->c_type);
                function_result->c_type = c_type;
            }
        }
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
                symbol->character_length_syntax.count != 0U
                    ? !f2c_evaluate_integer_syntax(unit, symbol->character_length_syntax,
                                                   &constant_length)
                    : strcmp(symbol->character_length, "1") != 0;
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
                    symbol->character_length_syntax =
                        f2c_implicit_character_length_syntax_for_name(unit, symbol->name);
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
    f2c_resolve_equivalence_storage(context, unit);
    if (function_result != NULL && function_result->type != TYPE_UNKNOWN) {
        unit->return_type = function_result->type;
        unit->return_kind = function_result->kind != 0 ? function_result->kind
                                                       : f2c_default_kind(function_result->type);
    }
    if (unit->elemental) {
        for (i = 0U; i < unit->argument_count; ++i) {
            Symbol *dummy = f2c_find_symbol(unit, unit->arguments[i]);
            const size_t declaration_line = dummy != NULL && dummy->declaration_line != 0U
                                                ? dummy->declaration_line
                                                : header_line;
            if (dummy == NULL)
                continue;
            if (dummy->rank != 0U)
                f2c_diagnostic(context, declaration_line, 1,
                               "dummy argument '%s' of an ELEMENTAL procedure must be scalar",
                               dummy->name);
            if (dummy->allocatable || dummy->pointer)
                f2c_diagnostic(context, declaration_line, 1,
                               "dummy argument '%s' of an ELEMENTAL procedure cannot be "
                               "ALLOCATABLE or POINTER",
                               dummy->name);
            if (dummy->intent == F2C_INTENT_UNSPECIFIED)
                f2c_diagnostic(context, declaration_line, 1,
                               "dummy argument '%s' of an ELEMENTAL procedure requires INTENT",
                               dummy->name);
            if (unit->kind == UNIT_FUNCTION && dummy->intent != F2C_INTENT_IN)
                f2c_diagnostic(context, declaration_line, 1,
                               "dummy argument '%s' of an ELEMENTAL function requires "
                               "INTENT(IN)",
                               dummy->name);
        }
        if (function_result != NULL && (function_result->rank != 0U ||
                                        function_result->allocatable || function_result->pointer))
            f2c_diagnostic(context,
                           function_result->declaration_line != 0U
                               ? function_result->declaration_line
                               : header_line,
                           1,
                           "result of an ELEMENTAL function must be scalar and cannot be "
                           "ALLOCATABLE or POINTER");
    }
    unit->phase = F2C_UNIT_SYMBOLS_RESOLVED;
}
