#include "internal/f2c.h"

#include <ctype.h>
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

static int parse_intent(const char *attribute, F2cIntent *intent) {
    const char *cursor;
    const char *close;
    char *value;
    char *clean;
    if (strncmp(attribute, "intent", strlen("intent")) != 0)
        return 0;
    cursor = skip_space(attribute + strlen("intent"));
    if (*cursor != '(')
        return 0;
    close = matching_parenthesis(cursor);
    if (close == NULL || *skip_space(close + 1) != '\0')
        return 0;
    value = f2c_strdup_n(cursor + 1, (size_t)(close - cursor - 1));
    if (value == NULL)
        return -1;
    clean = f2c_trim(value);
    if (strcmp(clean, "in") == 0)
        *intent = F2C_INTENT_IN;
    else if (strcmp(clean, "out") == 0)
        *intent = F2C_INTENT_OUT;
    else if (strcmp(clean, "inout") == 0)
        *intent = F2C_INTENT_INOUT;
    else {
        free(value);
        return 0;
    }
    free(value);
    return 1;
}

static int parse_attributes(Context *context, const Line *source_line, const char *attributes,
                            int *optional, int *pointer, F2cIntent *intent) {
    char **items;
    size_t count = 0U;
    size_t i;
    int valid = 1;
    items = f2c_split_arguments(attributes, &count);
    if (items == NULL && *skip_space(attributes) != '\0') {
        f2c_diagnostic(context, source_line->number, 1,
                       "out of memory parsing PROCEDURE attributes");
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        char *attribute = f2c_trim(items[i]);
        F2cIntent parsed_intent = F2C_INTENT_UNSPECIFIED;
        const int intent_status = parse_intent(attribute, &parsed_intent);
        if (strcmp(attribute, "optional") == 0) {
            if (*optional) {
                f2c_diagnostic(context, source_line->number, 1,
                               "duplicate OPTIONAL attribute in PROCEDURE declaration");
                valid = 0;
            }
            *optional = 1;
        } else if (intent_status > 0) {
            if (*intent != F2C_INTENT_UNSPECIFIED) {
                f2c_diagnostic(context, source_line->number, 1,
                               "duplicate INTENT attribute in PROCEDURE declaration");
                valid = 0;
            }
            *intent = parsed_intent;
        } else if (intent_status < 0) {
            f2c_diagnostic(context, source_line->number, 1,
                           "out of memory parsing PROCEDURE INTENT attribute");
            valid = 0;
        } else if (strcmp(attribute, "pointer") == 0) {
            if (*pointer) {
                f2c_diagnostic(context, source_line->number, 1,
                               "duplicate POINTER attribute in PROCEDURE declaration");
                valid = 0;
            }
            *pointer = 1;
        } else if (strcmp(attribute, "nopass") == 0) {
            /* NOPASS changes no C ABI state for an explicit procedure-pointer interface. */
        } else if (strcmp(attribute, "pass") == 0) {
            f2c_diagnostic(context, source_line->number, 1,
                           "PASS requires a type-bound procedure binding");
            valid = 0;
        } else {
            f2c_diagnostic(context, source_line->number, 1,
                           "unsupported or malformed PROCEDURE attribute '%s'", attribute);
            valid = 0;
        }
        free(items[i]);
    }
    free(items);
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
    symbol->external_parameter_count = signature->argument_count;
    if (symbol->external_parameter_count > 64U)
        symbol->external_parameter_count = 64U;
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
        symbol->external_parameter_derived_types[i] = dummy != NULL ? dummy->derived_type : NULL;
        symbol->external_parameter_polymorphic[i] = dummy != NULL && dummy->polymorphic;
        symbol->external_parameter_const[i] = dummy != NULL && dummy->intent == F2C_INTENT_IN;
        symbol->external_parameter_procedures[i] = dummy != NULL && dummy->external ? dummy : NULL;
    }
    return 1;
}

void f2c_parse_procedure_declaration(Context *context, Unit *unit, Line *source_line) {
    const char *cursor;
    const char *open;
    const char *close;
    const char *double_colon;
    char *interface_name;
    char *attributes;
    char *entities_text;
    char **entities;
    Unit *signature;
    size_t count = 0U;
    size_t i;
    int optional = 0;
    int pointer = 0;
    F2cIntent intent = F2C_INTENT_UNSPECIFIED;
    if (!f2c_starts_word(source_line->text, "procedure"))
        return;
    cursor = skip_space(source_line->text + strlen("procedure"));
    open = *cursor == '(' ? cursor : NULL;
    close = matching_parenthesis(open);
    if (open == NULL || close == NULL) {
        f2c_diagnostic(context, source_line->number, 1,
                       "PROCEDURE declaration requires a named interface in parentheses");
        return;
    }
    interface_name = f2c_strdup_n(open + 1, (size_t)(close - open - 1));
    if (interface_name == NULL) {
        f2c_diagnostic(context, source_line->number, 1,
                       "out of memory parsing PROCEDURE declaration");
        return;
    }
    {
        char *clean = f2c_trim(interface_name);
        size_t consumed = 0U;
        char *identifier = f2c_identifier(clean, &consumed);
        if (clean != interface_name)
            memmove(interface_name, clean, strlen(clean) + 1U);
        if (identifier == NULL || consumed != strlen(interface_name)) {
            f2c_diagnostic(context, source_line->number, 1,
                           "PROCEDURE interface name '%s' is invalid", interface_name);
            free(identifier);
            free(interface_name);
            return;
        }
        free(identifier);
    }
    signature = f2c_find_interface_signature(context, unit, interface_name, 1);
    if (signature == NULL || strcmp(signature->name, interface_name) != 0) {
        f2c_diagnostic(context, source_line->number, 1,
                       "PROCEDURE interface '%s' is not a visible specific or abstract interface",
                       interface_name);
        free(interface_name);
        return;
    }
    if (signature->argument_count > 64U) {
        f2c_diagnostic(context, source_line->number, 1,
                       "PROCEDURE interface '%s' exceeds the supported 64-argument limit",
                       interface_name);
        free(interface_name);
        return;
    }
    double_colon = strstr(close + 1, "::");
    if (double_colon != NULL) {
        attributes = f2c_strdup_n(close + 1, (size_t)(double_colon - close - 1));
        entities_text = f2c_strdup(double_colon + 2);
    } else {
        attributes = f2c_strdup("");
        entities_text = f2c_strdup(close + 1);
    }
    if (attributes == NULL || entities_text == NULL) {
        f2c_diagnostic(context, source_line->number, 1,
                       "out of memory parsing PROCEDURE declaration");
        free(attributes);
        free(entities_text);
        free(interface_name);
        return;
    }
    (void)parse_attributes(context, source_line, attributes, &optional, &pointer, &intent);
    if (intent != F2C_INTENT_UNSPECIFIED && !pointer)
        f2c_diagnostic(context, source_line->number, 1,
                       "INTENT on a PROCEDURE entity requires the POINTER attribute");
    entities = f2c_split_arguments(entities_text, &count);
    if (count == 0U)
        f2c_diagnostic(context, source_line->number, 1, "PROCEDURE declaration has no entities");
    for (i = 0U; i < count; ++i) {
        char *clean = f2c_trim(entities[i]);
        size_t consumed = 0U;
        char *name = f2c_identifier(clean, &consumed);
        Symbol *symbol =
            name != NULL && consumed == strlen(clean) ? f2c_ensure_symbol(unit, name) : NULL;
        if (symbol == NULL) {
            f2c_diagnostic(context, source_line->number, 1,
                           "malformed PROCEDURE declaration entity '%s'", clean);
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
        free(entities[i]);
    }
    free(entities);
    free(attributes);
    free(entities_text);
    free(interface_name);
}
