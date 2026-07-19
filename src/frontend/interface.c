#include "internal/f2c.h"

#include "ast/interface/header.h"
#include "ast/interface/specific.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int interface_units_push(Unit *host, Unit procedure) {
    Unit *replacement;
    size_t capacity;
    if (host->interface_count == host->interface_capacity) {
        if (host->interface_capacity > SIZE_MAX / 2U)
            return 0;
        capacity = host->interface_capacity == 0U ? 4U : host->interface_capacity * 2U;
        if (capacity > SIZE_MAX / sizeof(*host->interfaces))
            return 0;
        replacement = (Unit *)realloc(host->interfaces, capacity * sizeof(*host->interfaces));
        if (replacement == NULL)
            return 0;
        host->interfaces = replacement;
        host->interface_capacity = capacity;
    }
    host->interfaces[host->interface_count++] = procedure;
    return 1;
}

static const char *interface_visible_name(const Unit *procedure) {
    return procedure->interface_generic_name != NULL ? procedure->interface_generic_name
                                                     : procedure->name;
}

static const char *interface_specific_name(const Unit *procedure) {
    return procedure->fortran_name != NULL ? procedure->fortran_name : procedure->name;
}

static Unit *find_local_interface(Unit *host, const char *visible_name) {
    size_t i;
    for (i = 0U; i < host->interface_count; ++i) {
        Unit *procedure = &host->interfaces[i];
        if (strcmp(interface_visible_name(procedure), visible_name) == 0)
            return procedure;
    }
    return NULL;
}

static int copy_signature_to_symbol(Context *context, Unit *host, Unit *procedure,
                                    const char *visible_name, int alias) {
    Symbol *external = f2c_ensure_symbol(host, visible_name);
    Symbol *result_symbol = procedure->kind == UNIT_FUNCTION && procedure->result_name != NULL
                                ? f2c_find_symbol(procedure, procedure->result_name)
                                : NULL;
    size_t i;
    if (external == NULL)
        return 0;
    if (external->external_signature_explicit) {
        f2c_diagnostic(context, context->lines.items[procedure->begin].number, 1,
                       "duplicate explicit interface for procedure '%s'", visible_name);
        return 1;
    }
    if (alias) {
        char *c_name = f2c_strdup(procedure->name);
        if (c_name == NULL)
            return 0;
        free(external->c_name);
        external->c_name = c_name;
    }
    external->external = 1;
    external->external_declared = 1;
    external->external_subroutine = procedure->kind == UNIT_SUBROUTINE;
    external->external_signature_observed = 1;
    external->external_signature_explicit = 1;
    if (!f2c_copy_function_result_metadata(external, procedure))
        return 0;
    if (!f2c_symbol_resize_external_parameters(external, procedure->argument_count))
        return 0;
    external->external_parameter_count = procedure->argument_count;
    if (external->type == TYPE_CHARACTER && result_symbol != NULL &&
        result_symbol->character_length != NULL) {
        free(external->character_length);
        external->character_length = f2c_strdup(result_symbol->character_length);
        external->character_length_syntax = result_symbol->character_length_syntax;
        if (external->character_length == NULL)
            return 0;
    }
    for (i = 0U; i < external->external_parameter_count; ++i) {
        Symbol *dummy = f2c_find_symbol(procedure, procedure->arguments[i]);
        external->external_parameter_types[i] = dummy != NULL ? dummy->type : TYPE_UNKNOWN;
        external->external_parameter_kinds[i] =
            dummy != NULL ? dummy->kind : f2c_default_kind(TYPE_REAL);
        external->external_parameter_ranks[i] = dummy != NULL ? dummy->rank : 0U;
        external->external_parameter_intents[i] =
            dummy != NULL ? dummy->intent : F2C_INTENT_UNSPECIFIED;
        external->external_parameter_optional[i] = dummy != NULL && dummy->optional;
        external->external_parameter_allocatable[i] = dummy != NULL && dummy->allocatable;
        external->external_parameter_pointer[i] = dummy != NULL && dummy->pointer;
        external->external_parameter_descriptor[i] = f2c_symbol_uses_descriptor(dummy);
        external->external_parameter_derived_types[i] = dummy != NULL ? dummy->derived_type : NULL;
        external->external_parameter_polymorphic[i] = dummy != NULL && dummy->polymorphic;
        external->external_parameter_const[i] = dummy != NULL && dummy->intent == F2C_INTENT_IN;
        external->external_parameter_procedures[i] =
            dummy != NULL && dummy->external ? dummy : NULL;
    }
    return 1;
}

static int copy_interface_signatures(Context *context, Unit *host, Unit *procedure,
                                     int first_generic_specific) {
    const char *specific_name = interface_specific_name(procedure);
    const int specific_alias = strcmp(specific_name, procedure->name) != 0;
    Symbol *specific = f2c_find_symbol(host, specific_name);
    const int same_specific = specific != NULL && specific->external_signature_explicit &&
                              specific->c_name != NULL &&
                              strcmp(specific->c_name, procedure->name) == 0;
    if (!same_specific &&
        !copy_signature_to_symbol(context, host, procedure, specific_name, specific_alias))
        return 0;
    if (procedure->interface_generic_name != NULL && first_generic_specific &&
        strcmp(procedure->interface_generic_name, procedure->name) != 0 &&
        !copy_signature_to_symbol(context, host, procedure, procedure->interface_generic_name, 1))
        return 0;
    return 1;
}

static int append_generic_candidate(Context *context, Unit *host, Unit *procedure) {
    Symbol *generic;
    Unit **replacement;
    char *origin_name = NULL;
    size_t index;
    size_t count;
    if (procedure->interface_generic_name == NULL || procedure->interface_generic_name[0] == '\0')
        return 1;
    generic = f2c_find_symbol(host, procedure->interface_generic_name);
    if (generic == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &procedure->name_span, 1,
                                 "generic interface '%s' has no binding symbol",
                                 procedure->interface_generic_name);
        return 1;
    }
    if (generic->generic_candidate_count != 0U &&
        (generic->generic_origin_scope != host || generic->generic_origin_name == NULL ||
         strcmp(generic->generic_origin_name, procedure->interface_generic_name) != 0)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &procedure->name_span, 1,
                                 "generic interface '%s' conflicts with an associated generic",
                                 procedure->interface_generic_name);
        return 1;
    }
    for (index = 0U; index < generic->generic_candidate_count; ++index) {
        if (generic->generic_candidates[index] == procedure)
            return 1;
    }
    if (generic->generic_origin_scope == NULL) {
        origin_name = f2c_strdup(procedure->interface_generic_name);
        if (origin_name == NULL)
            return 0;
    }
    if (generic->generic_candidate_count >= SIZE_MAX / sizeof(*replacement)) {
        free(origin_name);
        return 0;
    }
    count = generic->generic_candidate_count + 1U;
    replacement = (Unit **)realloc(generic->generic_candidates, count * sizeof(*replacement));
    if (replacement == NULL) {
        free(origin_name);
        return 0;
    }
    generic->generic_candidates = replacement;
    generic->generic_candidates[generic->generic_candidate_count++] = procedure;
    if (generic->generic_origin_scope == NULL) {
        generic->generic_origin_name = origin_name;
        generic->generic_origin_scope = host;
    }
    return 1;
}

static int rebuild_generic_candidates(Context *context, Unit *host) {
    size_t index;
    for (index = 0U; index < host->symbol_count; ++index) {
        Symbol *symbol = &host->symbols[index];
        if (symbol->generic_origin_scope != host)
            continue;
        free(symbol->generic_candidates);
        symbol->generic_candidates = NULL;
        symbol->generic_candidate_count = 0U;
        free(symbol->generic_origin_name);
        symbol->generic_origin_name = NULL;
        symbol->generic_origin_scope = NULL;
    }
    for (index = 0U; index < host->interface_count; ++index) {
        if (!append_generic_candidate(context, host, &host->interfaces[index])) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                context->lines.items[host->interfaces[index].begin].number, 1,
                                "out of memory building generic interface candidates");
            return 0;
        }
    }
    return 1;
}

static Unit *find_signature_in_scope(Unit *scope, const char *name, int include_abstract) {
    size_t i;
    if (scope == NULL || name == NULL)
        return NULL;
    for (i = 0U; i < scope->interface_count; ++i) {
        Unit *candidate = &scope->interfaces[i];
        if ((!candidate->interface_abstract || include_abstract) &&
            (strcmp(candidate->name, name) == 0 ||
             (candidate->interface_generic_name != NULL &&
              strcmp(candidate->interface_generic_name, name) == 0)))
            return candidate;
    }
    return NULL;
}

Unit *f2c_find_interface_signature(Context *context, Unit *scope, const char *name,
                                   int include_abstract) {
    Unit *result = find_signature_in_scope(scope, name, include_abstract);
    if (result != NULL)
        return result;
    if (scope != NULL && scope->signature_host != NULL) {
        result = find_signature_in_scope(scope->signature_host, name, include_abstract);
        if (result != NULL)
            return result;
    }
    if (scope != NULL && scope->internal && scope->host_index < context->units.count)
        return find_signature_in_scope(&context->units.items[scope->host_index], name,
                                       include_abstract);
    return NULL;
}

static size_t find_procedure_end(Context *context, size_t begin, size_t block_end,
                                 const Unit *procedure) {
    size_t i;
    for (i = begin + 1U; i < block_end; ++i) {
        if (f2c_match_program_unit_end(context, &context->lines.items[i], procedure) !=
            F2C_UNIT_END_NO_MATCH)
            return i;
    }
    return block_end;
}

static void rebind_procedure_interfaces(Context *context, Unit *host) {
    size_t interface_index;
    for (interface_index = 0U; interface_index < host->interface_count; ++interface_index) {
        Unit *procedure = &host->interfaces[interface_index];
        size_t symbol_index;
        for (symbol_index = 0U; symbol_index < procedure->symbol_count; ++symbol_index) {
            Symbol *symbol = &procedure->symbols[symbol_index];
            if (symbol->procedure_interface_name != NULL)
                symbol->procedure_interface = f2c_find_interface_signature(
                    context, procedure, symbol->procedure_interface_name, 1);
        }
    }
}

static Unit *find_specific_procedure(Context *context, const Unit *host, const F2cToken *name_token,
                                     int require_module_procedure) {
    size_t index;
    for (index = 0U; index < context->units.count; ++index) {
        Unit *candidate = &context->units.items[index];
        const char *visible = interface_specific_name(candidate);
        const int contained = host->kind == UNIT_MODULE && !candidate->internal &&
                              candidate->begin > host->end &&
                              candidate->begin < host->container_end;
        if (candidate->kind != UNIT_PROGRAM && f2c_token_equals(name_token, visible) &&
            (contained || (!require_module_procedure && !candidate->internal)))
            return candidate;
    }
    return NULL;
}

static Unit *find_generic_specific(Unit *host, const char *generic_name,
                                   const char *specific_name) {
    size_t index;
    for (index = 0U; index < host->interface_count; ++index) {
        Unit *candidate = &host->interfaces[index];
        if (candidate->interface_generic_name != NULL &&
            strcmp(candidate->interface_generic_name, generic_name) == 0 &&
            strcmp(interface_specific_name(candidate), specific_name) == 0)
            return candidate;
    }
    return NULL;
}

static int store_interface_signature(Context *context, Unit *host, Unit *procedure,
                                     size_t diagnostic_line) {
    Unit *previous = find_local_interface(host, interface_visible_name(procedure));
    const int first_generic_specific = previous == NULL;
    if (procedure->interface_generic_name != NULL &&
        find_generic_specific(host, procedure->interface_generic_name,
                              interface_specific_name(procedure)) != NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &procedure->name_span, 1,
                                 "specific procedure '%s' appears more than once in generic "
                                 "interface '%s'",
                                 interface_specific_name(procedure),
                                 procedure->interface_generic_name);
        f2c_free_unit(procedure);
        return 1;
    }
    if (previous != NULL && previous->kind != procedure->kind) {
        f2c_diagnostic(context, diagnostic_line, 1,
                       "generic interface '%s' cannot mix functions and subroutines",
                       interface_visible_name(procedure));
    }
    if (!interface_units_push(host, *procedure)) {
        f2c_free_unit(procedure);
        f2c_diagnostic(context, diagnostic_line, 1,
                       "out of memory while storing explicit interface");
        return 0;
    }
    memset(procedure, 0, sizeof(*procedure));
    if (!host->interfaces[host->interface_count - 1U].interface_abstract &&
        !copy_interface_signatures(context, host, &host->interfaces[host->interface_count - 1U],
                                   first_generic_specific)) {
        f2c_diagnostic(context, diagnostic_line, 1,
                       "out of memory while binding explicit interface");
        return 0;
    }
    return 1;
}

static int build_procedure_signature(Context *context, Unit *host, Unit *actual,
                                     const char *generic_name, const F2cToken *binding_name,
                                     Unit *procedure) {
    const F2cOptions *previous_options = context->options;
    char *specific_name;
    if (f2c_parse_unit_header(context, &context->lines.items[actual->begin], procedure) !=
        F2C_UNIT_HEADER_PARSED)
        return 0;
    specific_name = procedure->name;
    procedure->name = f2c_strdup(actual->name);
    procedure->fortran_name = specific_name;
    procedure->interface_generic_name = f2c_strdup(generic_name);
    procedure->begin = actual->begin;
    procedure->end = actual->end;
    procedure->context = context;
    procedure->interface_body = 1;
    procedure->host_index = (size_t)-1;
    procedure->signature_host = host;
    procedure->options.source_name = f2c_strdup(context->lines.items[actual->begin].source_name);
    procedure->options.source_form = F2C_SOURCE_AUTO;
    procedure->options.emit_source_comments = 0;
    if (procedure->name == NULL || procedure->interface_generic_name == NULL ||
        procedure->options.source_name == NULL) {
        f2c_free_unit(procedure);
        memset(procedure, 0, sizeof(*procedure));
        return 0;
    }
    context->options = &procedure->options;
    f2c_analyze_unit(context, procedure);
    context->options = previous_options;
    procedure->name_span = binding_name->span;
    return 1;
}

static void report_specific_syntax_error(Context *context, const Line *line,
                                         const F2cInterfaceSpecificSyntax *syntax) {
    const F2cToken *token =
        syntax->error_token != NULL ? syntax->error_token : syntax->procedure_keyword;
    const char *prefix = syntax->module_keyword != NULL ? "MODULE PROCEDURE" : "PROCEDURE";
    const char *message = "malformed PROCEDURE specific list";
    switch (syntax->error) {
    case F2C_INTERFACE_SPECIFIC_ERROR_EMPTY_LIST:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "%s requires at least one specific procedure name", prefix);
        return;
    case F2C_INTERFACE_SPECIFIC_ERROR_NAME:
        message = "PROCEDURE specific list requires a procedure name";
        break;
    case F2C_INTERFACE_SPECIFIC_ERROR_SEPARATOR:
        message = "PROCEDURE specific names must be separated by commas";
        break;
    case F2C_INTERFACE_SPECIFIC_ERROR_DUPLICATE_NAME:
        message = "duplicate name in PROCEDURE specific list";
        break;
    case F2C_INTERFACE_SPECIFIC_ERROR_TRAILING_COMMA:
        message = "PROCEDURE specific list cannot end with a comma";
        break;
    case F2C_INTERFACE_SPECIFIC_ERROR_NONE:
        break;
    }
    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1, "%s", message);
}

void f2c_parse_explicit_interfaces(Context *context, Unit *host) {
    size_t i;
    for (i = host->begin + 1U; i < host->end; ++i) {
        Line *start = &context->lines.items[i];
        size_t block_end;
        size_t j;
        size_t procedure_count = 0U;
        F2cInterfaceHeaderSyntax header_syntax;
        F2cEndInterfaceSyntax end_syntax;
        F2cInterfaceHeaderStatus interface_status;
        int abstract_block;
        char *generic_name = NULL;
        if (!f2c_interface_start_tokens(start))
            continue;
        interface_status = f2c_parse_interface_header_syntax(start, &header_syntax);
        if (interface_status != F2C_INTERFACE_HEADER_PARSED) {
            const F2cToken *token = header_syntax.error_token != NULL
                                        ? header_syntax.error_token
                                        : header_syntax.interface_keyword;
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, start, token, 1,
                                      "malformed INTERFACE statement");
            continue;
        }
        abstract_block = header_syntax.abstract_keyword != NULL;
        generic_name = header_syntax.has_generic
                           ? f2c_generic_designator_key(&header_syntax.generic)
                           : f2c_strdup("");
        if (generic_name == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &header_syntax.span, 1,
                                     "out of memory parsing INTERFACE generic spec");
            return;
        }
        block_end = i + 1U;
        while (block_end < host->end &&
               !(f2c_interface_end_tokens(&context->lines.items[block_end]) &&
                 context->lines.items[block_end].interface_depth == start->interface_depth))
            ++block_end;
        if (block_end == host->end) {
            f2c_diagnostic(context, start->number, 1, "unterminated INTERFACE block");
            free(generic_name);
            return;
        }
        interface_status =
            f2c_parse_end_interface_syntax(&context->lines.items[block_end], &end_syntax);
        if (interface_status != F2C_INTERFACE_HEADER_PARSED) {
            const F2cToken *token = end_syntax.error_token != NULL ? end_syntax.error_token
                                                                   : end_syntax.interface_keyword;
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX,
                                      &context->lines.items[block_end], token, 1,
                                      "malformed END INTERFACE statement");
        } else if (end_syntax.has_generic &&
                   (!header_syntax.has_generic ||
                    !f2c_generic_designators_equal(&header_syntax.generic, &end_syntax.generic))) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &end_syntax.generic.span, 1,
                                     "END INTERFACE generic spec does not match its INTERFACE");
        }
        for (j = i + 1U; j < block_end;) {
            Unit procedure;
            F2cUnitHeaderParseStatus header_status;
            size_t procedure_end;
            F2cInterfaceSpecificSyntax specific_syntax;
            const F2cInterfaceSpecificStatus specific_status =
                f2c_parse_interface_specific_syntax(&context->lines.items[j], &specific_syntax);
            if (specific_status != F2C_INTERFACE_SPECIFIC_NOT_MATCHED) {
                size_t specific_index;
                const F2cToken *keyword = specific_syntax.module_keyword != NULL
                                              ? specific_syntax.module_keyword
                                              : specific_syntax.procedure_keyword;
                if (specific_status == F2C_INTERFACE_SPECIFIC_NO_MEMORY) {
                    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                             &specific_syntax.span, 1,
                                             "out of memory parsing PROCEDURE specific list");
                } else if (specific_status == F2C_INTERFACE_SPECIFIC_INVALID) {
                    report_specific_syntax_error(context, &context->lines.items[j],
                                                 &specific_syntax);
                } else if (abstract_block) {
                    f2c_diagnostic_token_code(
                        context, F2C_DIAGNOSTIC_SEMANTIC, &context->lines.items[j], keyword, 1,
                        "PROCEDURE specific lists are not valid in an ABSTRACT INTERFACE");
                } else if (specific_syntax.module_keyword != NULL && host->kind != UNIT_MODULE) {
                    f2c_diagnostic_token_code(
                        context, F2C_DIAGNOSTIC_SEMANTIC, &context->lines.items[j],
                        specific_syntax.module_keyword, 1,
                        "MODULE PROCEDURE generic bindings require a module scope");
                } else if (generic_name[0] == '\0') {
                    f2c_diagnostic_token_code(
                        context, F2C_DIAGNOSTIC_SEMANTIC, &context->lines.items[j], keyword, 1,
                        "PROCEDURE specific list requires a generic INTERFACE");
                } else {
                    for (specific_index = 0U; specific_index < specific_syntax.name_count;
                         ++specific_index) {
                        const F2cToken *specific_token = specific_syntax.names[specific_index];
                        Unit *actual = find_specific_procedure(
                            context, host, specific_token, specific_syntax.module_keyword != NULL);
                        memset(&procedure, 0, sizeof(procedure));
                        if (actual == NULL) {
                            if (specific_syntax.module_keyword != NULL)
                                f2c_diagnostic_token_code(
                                    context, F2C_DIAGNOSTIC_SEMANTIC, &context->lines.items[j],
                                    specific_token, 1,
                                    "MODULE PROCEDURE specific '%.*s' is not defined in module "
                                    "'%s'",
                                    (int)specific_token->length, specific_token->begin, host->name);
                            else
                                f2c_diagnostic_token_code(
                                    context, F2C_DIAGNOSTIC_SEMANTIC, &context->lines.items[j],
                                    specific_token, 1,
                                    "PROCEDURE specific '%.*s' is not a visible procedure",
                                    (int)specific_token->length, specific_token->begin);
                            continue;
                        }
                        if (!build_procedure_signature(context, host, actual, generic_name,
                                                       specific_token, &procedure)) {
                            f2c_diagnostic_token_code(
                                context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &context->lines.items[j],
                                specific_token, 1,
                                "out of memory binding PROCEDURE specific '%.*s'",
                                (int)specific_token->length, specific_token->begin);
                            continue;
                        }
                        if (!store_interface_signature(context, host, &procedure,
                                                       context->lines.items[j].number)) {
                            f2c_interface_specific_syntax_discard(&specific_syntax);
                            free(generic_name);
                            return;
                        }
                        ++procedure_count;
                    }
                }
                f2c_interface_specific_syntax_discard(&specific_syntax);
                ++j;
                continue;
            }
            header_status = f2c_parse_unit_header(context, &context->lines.items[j], &procedure);
            if (header_status != F2C_UNIT_HEADER_PARSED) {
                ++j;
                continue;
            }
            if (procedure.kind == UNIT_PROGRAM) {
                f2c_diagnostic(context, context->lines.items[j].number, 1,
                               "PROGRAM is not valid inside an INTERFACE block");
                f2c_free_unit(&procedure);
                ++j;
                continue;
            }
            procedure_end = find_procedure_end(context, j, block_end, &procedure);
            if (procedure_end == block_end) {
                f2c_diagnostic(context, context->lines.items[j].number, 1,
                               "unterminated procedure interface '%s'", procedure.name);
                f2c_free_unit(&procedure);
                break;
            }
            procedure.begin = j;
            procedure.context = context;
            procedure.end = procedure_end;
            procedure.interface_body = 1;
            procedure.interface_abstract = abstract_block;
            procedure.host_index = (size_t)-1;
            procedure.signature_host = host;
            procedure.options.source_name = f2c_strdup(context->lines.items[j].source_name);
            procedure.options.source_form = F2C_SOURCE_AUTO;
            procedure.options.emit_source_comments = 0;
            if (!abstract_block && generic_name != NULL && generic_name[0] != '\0')
                procedure.interface_generic_name = f2c_strdup(generic_name);
            if (procedure.options.source_name == NULL ||
                (!abstract_block && generic_name != NULL && generic_name[0] != '\0' &&
                 procedure.interface_generic_name == NULL)) {
                f2c_free_unit(&procedure);
                f2c_diagnostic(context, context->lines.items[j].number, 1,
                               "out of memory while parsing explicit interface");
                break;
            }
            context->options = &procedure.options;
            f2c_analyze_unit(context, &procedure);
            context->options = &host->options;
            if (!store_interface_signature(context, host, &procedure,
                                           context->lines.items[j].number)) {
                free(generic_name);
                return;
            }
            ++procedure_count;
            j = procedure_end + 1U;
        }
        if (procedure_count == 0U && generic_name != NULL)
            f2c_diagnostic(context, start->number, 1,
                           "INTERFACE block contains no explicit procedure interface");
        free(generic_name);
        i = block_end;
    }
    if (rebuild_generic_candidates(context, host))
        rebind_procedure_interfaces(context, host);
}
