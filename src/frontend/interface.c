#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

int f2c_interface_start_line(const char *line) {
    return f2c_starts_word(line, "interface") || f2c_starts_word(line, "abstract interface");
}

int f2c_interface_end_line(const char *line) {
    return f2c_starts_word(line, "end interface") || f2c_starts_word(line, "endinterface");
}

static int interface_units_push(Unit *host, Unit procedure) {
    Unit *replacement;
    size_t capacity;
    if (host->interface_count == host->interface_capacity) {
        capacity = host->interface_capacity == 0U ? 4U : host->interface_capacity * 2U;
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
    if (!copy_signature_to_symbol(context, host, procedure, procedure->name, 0))
        return 0;
    if (procedure->interface_generic_name != NULL && first_generic_specific &&
        strcmp(procedure->interface_generic_name, procedure->name) != 0 &&
        !copy_signature_to_symbol(context, host, procedure, procedure->interface_generic_name, 1))
        return 0;
    return 1;
}

static char *parse_generic_name(Context *context, const Line *line) {
    const size_t start = line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U
                                                                                           : 0U;
    if (f2c_abstract_interface_tokens(line))
        return f2c_strdup("");
    if (start + 1U == line->token_count)
        return f2c_strdup("");
    if (start + 2U != line->token_count ||
        line->tokens[start + 1U].kind != F2C_TOKEN_IDENTIFIER) {
        f2c_diagnostic(context, line->number, 1,
                       "only a plain name is supported on a generic INTERFACE statement");
        return NULL;
    }
    return f2c_token_text(&line->tokens[start + 1U]);
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

static size_t find_procedure_end(const Context *context, size_t begin, size_t block_end,
                                 UnitKind kind) {
    size_t i;
    for (i = begin + 1U; i < block_end; ++i) {
        if (f2c_program_unit_end_tokens(&context->lines.items[i], kind))
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

void f2c_parse_explicit_interfaces(Context *context, Unit *host) {
    size_t i;
    for (i = host->begin + 1U; i < host->end; ++i) {
        Line *start = &context->lines.items[i];
        size_t block_end;
        size_t j;
        size_t procedure_count = 0U;
        size_t module_procedure_count = 0U;
        const int abstract_block = f2c_abstract_interface_tokens(start);
        char *generic_name;
        if (!f2c_interface_start_tokens(start))
            continue;
        block_end = i + 1U;
        while (block_end < host->end &&
               !(f2c_interface_end_tokens(&context->lines.items[block_end]) &&
                 context->lines.items[block_end].interface_depth == start->interface_depth))
            ++block_end;
        if (block_end == host->end) {
            f2c_diagnostic(context, start->number, 1, "unterminated INTERFACE block");
            return;
        }
        generic_name = parse_generic_name(context, start);
        for (j = i + 1U; j < block_end;) {
            Unit procedure;
            Unit *previous_specific;
            size_t procedure_end;
            int first_generic_specific;
            if (f2c_module_procedure_tokens(&context->lines.items[j])) {
                ++module_procedure_count;
                ++j;
                continue;
            }
            if (!f2c_parse_unit_header_tokens(&context->lines.items[j], &procedure)) {
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
            procedure_end = find_procedure_end(context, j, block_end, procedure.kind);
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
            previous_specific = generic_name != NULL && !abstract_block
                                    ? find_local_interface(host, interface_visible_name(&procedure))
                                    : NULL;
            first_generic_specific = previous_specific == NULL;
            if (previous_specific != NULL &&
                (previous_specific->kind != procedure.kind ||
                 (procedure.kind == UNIT_FUNCTION &&
                  previous_specific->return_type != procedure.return_type))) {
                f2c_diagnostic(context, context->lines.items[j].number, 1,
                               "generic interface '%s' mixes procedure kinds or function result "
                               "types that the current C17 type system cannot represent",
                               interface_visible_name(&procedure));
            }
            if (!interface_units_push(host, procedure)) {
                f2c_free_unit(&procedure);
                f2c_diagnostic(context, context->lines.items[j].number, 1,
                               "out of memory while storing explicit interface");
                free(generic_name);
                return;
            }
            if (generic_name != NULL && !abstract_block &&
                !copy_interface_signatures(context, host,
                                           &host->interfaces[host->interface_count - 1U],
                                           first_generic_specific)) {
                f2c_diagnostic(context, context->lines.items[j].number, 1,
                               "out of memory while binding explicit interface");
                free(generic_name);
                return;
            }
            ++procedure_count;
            j = procedure_end + 1U;
        }
        if (procedure_count == 0U && module_procedure_count == 0U && generic_name != NULL)
            f2c_diagnostic(context, start->number, 1,
                           "INTERFACE block contains no explicit procedure interface");
        free(generic_name);
        i = block_end;
    }
    rebind_procedure_interfaces(context, host);
}
