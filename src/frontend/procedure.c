#include "internal/f2c.h"

#include "ast/declaration/procedure.h"

#include <stdlib.h>
#include <string.h>

int f2c_copy_function_result_metadata(Symbol *symbol, Unit *signature) {
    Symbol *result = signature->kind == UNIT_FUNCTION && signature->result_name != NULL
                         ? f2c_find_symbol(signature, signature->result_name)
                         : NULL;
    char *c_type = NULL;
    char *derived_type_name = NULL;
    if (signature->kind == UNIT_FUNCTION && result != NULL && result->type == TYPE_DERIVED) {
        c_type = result->c_type != NULL ? f2c_strdup(result->c_type) : NULL;
        derived_type_name =
            result->derived_type_name != NULL ? f2c_strdup(result->derived_type_name) : NULL;
        if ((result->c_type != NULL && c_type == NULL) ||
            (result->derived_type_name != NULL && derived_type_name == NULL)) {
            free(c_type);
            free(derived_type_name);
            return 0;
        }
    }
    symbol->type = signature->kind == UNIT_FUNCTION ? signature->return_type : TYPE_UNKNOWN;
    symbol->kind = signature->kind == UNIT_FUNCTION ? signature->return_kind : 0;
    symbol->external_result_allocatable = result != NULL && result->allocatable;
    symbol->external_result_rank = result != NULL ? result->rank : 0U;
    symbol->derived_type = result != NULL ? result->derived_type : NULL;
    if (signature->kind == UNIT_FUNCTION && symbol->type == TYPE_DERIVED) {
        free(symbol->c_type);
        symbol->c_type = c_type;
        free(symbol->derived_type_name);
        symbol->derived_type_name = derived_type_name;
    } else {
        free(c_type);
        free(derived_type_name);
    }
    return 1;
}

int f2c_copy_procedure_signature(Symbol *symbol, Unit *signature) {
    Symbol *result = signature->kind == UNIT_FUNCTION && signature->result_name != NULL
                         ? f2c_find_symbol(signature, signature->result_name)
                         : NULL;
    size_t i;
    symbol->external = 1;
    symbol->external_declared = 1;
    symbol->external_signature_observed = 1;
    symbol->external_signature_explicit = 1;
    symbol->external_subroutine = signature->kind == UNIT_SUBROUTINE;
    symbol->external_alternate_return_count = signature->alternate_return_count;
    symbol->procedure_interface = signature;
    free(symbol->procedure_interface_name);
    symbol->procedure_interface_name = f2c_strdup(signature->name);
    if (symbol->procedure_interface_name == NULL)
        return 0;
    if (!f2c_copy_function_result_metadata(symbol, signature))
        return 0;
    if (!f2c_symbol_resize_external_parameters(symbol, signature->argument_count))
        return 0;
    symbol->external_parameter_count = signature->argument_count;
    if (symbol->type == TYPE_CHARACTER && result != NULL && result->character_length != NULL) {
        free(symbol->character_length);
        symbol->character_length = f2c_strdup(result->character_length);
        symbol->character_length_syntax = result->character_length_syntax;
        if (symbol->character_length == NULL)
            return 0;
    }
    for (i = 0U; i < symbol->external_parameter_count; ++i) {
        Symbol *dummy = f2c_find_symbol(signature, signature->arguments[i]);
        symbol->external_parameter_types[i] = dummy != NULL ? dummy->type : TYPE_UNKNOWN;
        symbol->external_parameter_kinds[i] =
            dummy != NULL ? dummy->kind : f2c_default_kind(TYPE_REAL);
        symbol->external_parameter_ranks[i] = dummy != NULL ? dummy->rank : 0U;
        symbol->external_parameter_intents[i] =
            dummy != NULL ? dummy->intent : F2C_INTENT_UNSPECIFIED;
        symbol->external_parameter_optional[i] = dummy != NULL && dummy->optional;
        symbol->external_parameter_allocatable[i] = dummy != NULL && dummy->allocatable;
        symbol->external_parameter_pointer[i] = dummy != NULL && dummy->pointer;
        symbol->external_parameter_contiguous[i] = dummy != NULL && dummy->contiguous;
        symbol->external_parameter_descriptor[i] = f2c_symbol_uses_descriptor(dummy);
        symbol->external_parameter_derived_types[i] = dummy != NULL ? dummy->derived_type : NULL;
        symbol->external_parameter_polymorphic[i] = dummy != NULL && dummy->polymorphic;
        symbol->external_parameter_const[i] = dummy != NULL && dummy->intent == F2C_INTENT_IN;
        symbol->external_parameter_procedures[i] = dummy != NULL && dummy->external ? dummy : NULL;
    }
    return 1;
}

static void report_declaration_error(Context *context, const Line *line,
                                     const F2cProcedureDeclarationSyntax *syntax) {
    const F2cToken *token = syntax->error_token;
    switch (syntax->error) {
    case F2C_PROCEDURE_DECLARATION_ERROR_INTERFACE:
        f2c_diagnostic_token_code(
            context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
            "PROCEDURE declaration requires a named interface in parentheses");
        break;
    case F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE_SEPARATOR:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "PROCEDURE attributes require comma separators and '::'");
        break;
    case F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "malformed PROCEDURE attribute list");
        break;
    case F2C_PROCEDURE_DECLARATION_ERROR_UNKNOWN_ATTRIBUTE: {
        char *name = f2c_token_text(token);
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "unsupported or malformed PROCEDURE attribute '%s'",
                                  name != NULL ? name : "<invalid>");
        free(name);
        break;
    }
    case F2C_PROCEDURE_DECLARATION_ERROR_DUPLICATE_ATTRIBUTE: {
        char *name = f2c_token_text(token);
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, token, 1,
                                  "duplicate %s attribute in PROCEDURE declaration",
                                  name != NULL ? name : "<invalid>");
        free(name);
        break;
    }
    case F2C_PROCEDURE_DECLARATION_ERROR_CONFLICTING_ACCESS:
        f2c_diagnostic_token_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, line, token, 1,
            "PROCEDURE declaration cannot have both PUBLIC and PRIVATE attributes");
        break;
    case F2C_PROCEDURE_DECLARATION_ERROR_INTENT:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "malformed PROCEDURE INTENT attribute");
        break;
    case F2C_PROCEDURE_DECLARATION_ERROR_ENTITY_LIST:
        if (syntax->entity_count == 0U)
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                      "PROCEDURE declaration has no entities");
        else
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                      "malformed PROCEDURE declaration entity list");
        break;
    case F2C_PROCEDURE_DECLARATION_ERROR_ENTITY:
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "malformed PROCEDURE declaration entity");
        break;
    case F2C_PROCEDURE_DECLARATION_ERROR_DUPLICATE_ENTITY: {
        char *name = f2c_token_text(token);
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, token, 1,
                                  "duplicate PROCEDURE entity '%s'",
                                  name != NULL ? name : "<invalid>");
        free(name);
        break;
    }
    case F2C_PROCEDURE_DECLARATION_ERROR_NONE:
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &syntax->span, 1,
                                 "malformed PROCEDURE declaration");
        break;
    }
}

static F2cIntent lower_intent(F2cProcedureIntentSyntax intent) {
    switch (intent) {
    case F2C_PROCEDURE_INTENT_IN:
        return F2C_INTENT_IN;
    case F2C_PROCEDURE_INTENT_OUT:
        return F2C_INTENT_OUT;
    case F2C_PROCEDURE_INTENT_INOUT:
        return F2C_INTENT_INOUT;
    case F2C_PROCEDURE_INTENT_NONE:
        break;
    }
    return F2C_INTENT_UNSPECIFIED;
}

static void lower_declaration(Context *context, Unit *unit, const Line *line,
                              const F2cProcedureDeclarationSyntax *syntax) {
    char *interface_name = f2c_token_text(syntax->interface_name);
    Unit *signature;
    F2cIntent intent = lower_intent(syntax->intent);
    const F2cToken *access_token =
        syntax->public_attribute != NULL ? syntax->public_attribute : syntax->private_attribute;
    size_t index;
    if (interface_name == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax->span, 1,
                                 "out of memory parsing PROCEDURE declaration");
        return;
    }
    signature = f2c_find_interface_signature(context, unit, interface_name, 1);
    if (signature == NULL || strcmp(signature->name, interface_name) != 0) {
        f2c_diagnostic_token_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax->interface_name, 1,
            "PROCEDURE interface '%s' is not a visible specific or abstract interface",
            interface_name);
        free(interface_name);
        return;
    }
    if (syntax->pass_attribute != NULL)
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax->pass_attribute, 1,
                                  "PASS requires a type-bound procedure binding");
    if (intent != F2C_INTENT_UNSPECIFIED && syntax->pointer_attribute == NULL)
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax->intent_attribute,
                                  1, "INTENT on a PROCEDURE entity requires the POINTER attribute");
    if (access_token != NULL && unit->kind != UNIT_MODULE)
        f2c_diagnostic_token_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, line, access_token, 1,
            "PUBLIC and PRIVATE PROCEDURE attributes are valid only in a module");
    for (index = 0U; index < syntax->entity_count; ++index) {
        const F2cToken *entity = syntax->entities[index];
        char *name = f2c_token_text(entity);
        Symbol *symbol = name != NULL ? f2c_ensure_symbol(unit, name) : NULL;
        if (symbol == NULL) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, entity, 1,
                                      "out of memory binding PROCEDURE entity");
        } else if (!f2c_copy_procedure_signature(symbol, signature)) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, entity, 1,
                                      "out of memory binding PROCEDURE entity '%s'", name);
        } else {
            symbol->optional |= syntax->optional_attribute != NULL;
            symbol->procedure_pointer |= syntax->pointer_attribute != NULL;
            symbol->intent = intent;
            symbol->declaration_line = entity->span.begin.line;
            symbol->declaration_span = entity->span;
            if (access_token != NULL && unit->kind == UNIT_MODULE) {
                symbol->access = syntax->public_attribute != NULL ? F2C_ACCESSIBILITY_PUBLIC
                                                                  : F2C_ACCESSIBILITY_PRIVATE;
                symbol->access_span = access_token->span;
            }
        }
        free(name);
    }
    free(interface_name);
}

void f2c_parse_procedure_declaration(Context *context, Unit *unit, Line *source_line) {
    F2cProcedureDeclarationSyntax syntax;
    F2cProcedureDeclarationStatus status =
        f2c_parse_procedure_declaration_syntax(source_line, &syntax);
    if (status == F2C_PROCEDURE_DECLARATION_INVALID)
        report_declaration_error(context, source_line, &syntax);
    else if (status == F2C_PROCEDURE_DECLARATION_NO_MEMORY)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax.span, 1,
                                 "out of memory parsing PROCEDURE declaration");
    else if (status == F2C_PROCEDURE_DECLARATION_PARSED)
        lower_declaration(context, unit, source_line, &syntax);
    f2c_procedure_declaration_syntax_discard(&syntax);
}
