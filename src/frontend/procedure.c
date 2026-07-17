#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static int parse_attributes(Context *context, const Line *source_line, size_t begin, size_t end,
                            int *optional, int *pointer, F2cIntent *intent) {
    size_t index = begin;
    int valid = 1;
    while (index < end) {
        const F2cToken *attribute;
        if (source_line->tokens[index].kind == F2C_TOKEN_COMMA) {
            ++index;
            continue;
        }
        attribute = &source_line->tokens[index];
        if (attribute->kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line, attribute, 1,
                                      "malformed PROCEDURE attribute");
            valid = 0;
            ++index;
            continue;
        }
        if (f2c_token_equals(attribute, "optional")) {
            if (*optional) {
                f2c_diagnostic(context, source_line->number, 1,
                               "duplicate OPTIONAL attribute in PROCEDURE declaration");
                valid = 0;
            }
            *optional = 1;
            ++index;
        } else if (f2c_token_equals(attribute, "intent")) {
            size_t close;
            F2cIntent parsed_intent = F2C_INTENT_UNSPECIFIED;
            if (index + 1U >= end || source_line->tokens[index + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
                !f2c_token_matching_delimiter(source_line->tokens, end, index + 1U, &close) ||
                close != index + 3U ||
                source_line->tokens[index + 2U].kind != F2C_TOKEN_IDENTIFIER) {
                f2c_diagnostic(context, source_line->number, 1,
                               "malformed PROCEDURE INTENT attribute");
                valid = 0;
                ++index;
                continue;
            }
            if (f2c_token_equals(&source_line->tokens[index + 2U], "in"))
                parsed_intent = F2C_INTENT_IN;
            else if (f2c_token_equals(&source_line->tokens[index + 2U], "out"))
                parsed_intent = F2C_INTENT_OUT;
            else if (f2c_token_equals(&source_line->tokens[index + 2U], "inout"))
                parsed_intent = F2C_INTENT_INOUT;
            else
                valid = 0;
            if (*intent != F2C_INTENT_UNSPECIFIED) {
                f2c_diagnostic(context, source_line->number, 1,
                               "duplicate INTENT attribute in PROCEDURE declaration");
                valid = 0;
            }
            *intent = parsed_intent;
            index = close + 1U;
        } else if (f2c_token_equals(attribute, "pointer")) {
            if (*pointer) {
                f2c_diagnostic(context, source_line->number, 1,
                               "duplicate POINTER attribute in PROCEDURE declaration");
                valid = 0;
            }
            *pointer = 1;
            ++index;
        } else if (f2c_token_equals(attribute, "nopass")) {
            /* NOPASS changes no C ABI state for an explicit procedure-pointer interface. */
            ++index;
        } else if (f2c_token_equals(attribute, "pass")) {
            f2c_diagnostic(context, source_line->number, 1,
                           "PASS requires a type-bound procedure binding");
            valid = 0;
            ++index;
        } else {
            char *text = f2c_token_text(attribute);
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line, attribute, 1,
                                      "unsupported or malformed PROCEDURE attribute '%s'",
                                      text != NULL ? text : "<invalid>");
            free(text);
            valid = 0;
            ++index;
        }
        if (index < end && source_line->tokens[index].kind != F2C_TOKEN_COMMA) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      &source_line->tokens[index], 1,
                                      "malformed PROCEDURE attribute list");
            valid = 0;
            while (index < end && source_line->tokens[index].kind != F2C_TOKEN_COMMA)
                ++index;
        }
    }
    return valid;
}

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
        symbol->external_parameter_descriptor[i] = f2c_symbol_uses_descriptor(dummy);
        symbol->external_parameter_derived_types[i] = dummy != NULL ? dummy->derived_type : NULL;
        symbol->external_parameter_polymorphic[i] = dummy != NULL && dummy->polymorphic;
        symbol->external_parameter_const[i] = dummy != NULL && dummy->intent == F2C_INTENT_IN;
        symbol->external_parameter_procedures[i] = dummy != NULL && dummy->external ? dummy : NULL;
    }
    return 1;
}

void f2c_parse_procedure_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t start = source_line != NULL && source_line->token_count > 1U &&
                           source_line->tokens[0].kind == F2C_TOKEN_NUMBER
                       ? 1U
                       : 0U;
    size_t open;
    size_t close;
    size_t double_colon;
    size_t entities_begin;
    char *interface_name;
    Unit *signature;
    size_t index;
    int optional = 0;
    int pointer = 0;
    F2cIntent intent = F2C_INTENT_UNSPECIFIED;
    if (!f2c_line_token_equals(source_line, start, "procedure"))
        return;
    open = start + 1U;
    if (open >= source_line->token_count ||
        source_line->tokens[open].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(source_line->tokens, source_line->token_count, open,
                                      &close) ||
        close != open + 2U || source_line->tokens[open + 1U].kind != F2C_TOKEN_IDENTIFIER) {
        f2c_diagnostic(context, source_line->number, 1,
                       "PROCEDURE declaration requires a named interface in parentheses");
        return;
    }
    interface_name = f2c_token_text(&source_line->tokens[open + 1U]);
    if (interface_name == NULL) {
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                            "out of memory parsing PROCEDURE declaration");
        return;
    }
    signature = f2c_find_interface_signature(context, unit, interface_name, 1);
    if (signature == NULL || strcmp(signature->name, interface_name) != 0) {
        f2c_diagnostic(context, source_line->number, 1,
                       "PROCEDURE interface '%s' is not a visible specific or abstract interface",
                       interface_name);
        free(interface_name);
        return;
    }
    double_colon = f2c_line_find_token(source_line, close + 1U, F2C_TOKEN_DOUBLE_COLON, NULL);
    if (double_colon != SIZE_MAX) {
        (void)parse_attributes(context, source_line, close + 1U, double_colon, &optional, &pointer,
                               &intent);
        entities_begin = double_colon + 1U;
    } else {
        entities_begin = close + 1U;
        if (entities_begin < source_line->token_count &&
            source_line->tokens[entities_begin].kind == F2C_TOKEN_COMMA) {
            f2c_diagnostic(context, source_line->number, 1,
                           "PROCEDURE attributes require '::' before the entity list");
            free(interface_name);
            return;
        }
    }
    if (intent != F2C_INTENT_UNSPECIFIED && !pointer)
        f2c_diagnostic(context, source_line->number, 1,
                       "INTENT on a PROCEDURE entity requires the POINTER attribute");
    if (entities_begin == source_line->token_count)
        f2c_diagnostic(context, source_line->number, 1, "PROCEDURE declaration has no entities");
    index = entities_begin;
    while (index < source_line->token_count) {
        char *name;
        Symbol *symbol;
        if (source_line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      &source_line->tokens[index], 1,
                                      "malformed PROCEDURE declaration entity");
            break;
        }
        name = f2c_token_text(&source_line->tokens[index++]);
        symbol = name != NULL ? f2c_ensure_symbol(unit, name) : NULL;
        if (symbol == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory binding PROCEDURE entity");
        } else if (!f2c_copy_procedure_signature(symbol, signature)) {
            f2c_diagnostic(context, source_line->number, 1,
                           "out of memory binding PROCEDURE entity '%s'", name);
        } else {
            symbol->optional |= optional;
            symbol->procedure_pointer |= pointer;
            symbol->intent = intent;
            symbol->declaration_line = source_line->number;
        }
        free(name);
        if (index == source_line->token_count)
            break;
        if (source_line->tokens[index].kind != F2C_TOKEN_COMMA ||
            ++index == source_line->token_count) {
            f2c_diagnostic(context, source_line->number, 1,
                           "malformed PROCEDURE declaration entity list");
            break;
        }
    }
    free(interface_name);
}
