#include "frontend/private.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static size_t statement_start(const Line *line) {
    if (line != NULL && line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER)
        return 1U;
    return 0U;
}

static char *identifier_text(const F2cToken *token) {
    return token != NULL && token->kind == F2C_TOKEN_IDENTIFIER ? f2c_token_text(token) : NULL;
}

static const F2cToken *derived_type_name_token(const Line *line) {
    const size_t start = statement_start(line);
    const size_t double_colon =
        f2c_line_find_token(line, start + 1U, F2C_TOKEN_DOUBLE_COLON, NULL);
    const size_t index = double_colon != SIZE_MAX ? double_colon + 1U : start + 1U;
    return line != NULL && index < line->token_count &&
                   line->tokens[index].kind == F2C_TOKEN_IDENTIFIER
               ? &line->tokens[index]
               : NULL;
}

int f2c_line_in_derived_type(const Unit *unit, size_t line_index) {
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
        if (derived->binding_capacity > SIZE_MAX / 2U)
            return NULL;
        capacity = derived->binding_capacity == 0U ? 4U : derived->binding_capacity * 2U;
        if (capacity > SIZE_MAX / sizeof(*replacement))
            return NULL;
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

static void diagnose_attribute(Context *context, const Line *line, const F2cToken *token,
                               const char *message) {
    char *text = f2c_token_text(token);
    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_GENERAL, line, token, 1, message,
                              text != NULL ? text : "<invalid>");
    free(text);
}

static int apply_binding_attributes(Context *context, const Line *line, size_t begin, size_t end,
                                    F2cTypeBinding *binding) {
    size_t index = begin;
    int valid = 1;
    while (index < end) {
        const F2cToken *attribute;
        if (line->tokens[index].kind == F2C_TOKEN_COMMA) {
            ++index;
            continue;
        }
        attribute = &line->tokens[index];
        if (attribute->kind != F2C_TOKEN_IDENTIFIER) {
            diagnose_attribute(context, line, attribute,
                               "malformed type-bound PROCEDURE attribute '%s'");
            valid = 0;
            ++index;
            continue;
        }
        if (f2c_token_equals(attribute, "deferred")) {
            binding->deferred = 1;
            ++index;
        } else if (f2c_token_equals(attribute, "nopass")) {
            binding->nopass = 1;
            ++index;
        } else if (f2c_token_equals(attribute, "pass")) {
            binding->nopass = 0;
            ++index;
            if (index < end && line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN) {
                size_t close;
                if (!f2c_token_matching_delimiter(line->tokens, end, index, &close) ||
                    close != index + 2U ||
                    line->tokens[index + 1U].kind != F2C_TOKEN_IDENTIFIER) {
                    diagnose_attribute(context, line, attribute, "malformed PASS attribute '%s'");
                    valid = 0;
                    while (index < end && line->tokens[index].kind != F2C_TOKEN_COMMA)
                        ++index;
                    continue;
                }
                free(binding->pass_name);
                binding->pass_name = identifier_text(&line->tokens[index + 1U]);
                if (binding->pass_name == NULL)
                    valid = 0;
                index = close + 1U;
            }
        } else if (f2c_token_equals(attribute, "non_overridable")) {
            binding->non_overridable = 1;
            ++index;
        } else if (f2c_token_equals(attribute, "public") ||
                   f2c_token_equals(attribute, "private")) {
            ++index;
        } else {
            diagnose_attribute(context, line, attribute,
                               "unsupported type-bound PROCEDURE attribute '%s'");
            valid = 0;
            ++index;
        }
        if (index < end && line->tokens[index].kind != F2C_TOKEN_COMMA) {
            diagnose_attribute(context, line, &line->tokens[index],
                               "malformed type-bound PROCEDURE attribute '%s'");
            valid = 0;
            while (index < end && line->tokens[index].kind != F2C_TOKEN_COMMA)
                ++index;
        }
    }
    return valid;
}

static void initialize_binding(Context *context, F2cDerivedType *derived, const Line *line,
                               F2cTypeBinding *binding, const F2cToken *name_token,
                               const F2cToken *target_token, const F2cToken *interface_token,
                               size_t attribute_begin, size_t attribute_end) {
    binding->name = identifier_text(name_token);
    binding->target_name =
        target_token != NULL ? identifier_text(target_token) : identifier_text(name_token);
    binding->interface_name = identifier_text(interface_token);
    binding->owner = derived;
    binding->storage_owner = derived;
    binding->procedure.name = identifier_text(name_token);
    binding->procedure.c_name = identifier_text(name_token);
    binding->procedure.procedure_pointer = 1;
    binding->procedure.type_bound = 1;
    binding->procedure.derived_owner = derived;
    (void)apply_binding_attributes(context, line, attribute_begin, attribute_end, binding);
    binding->procedure.type_bound_deferred = binding->deferred;
    binding->procedure.type_bound_nopass = binding->nopass;
    if (binding->name == NULL || binding->target_name == NULL || binding->procedure.name == NULL ||
        binding->procedure.c_name == NULL)
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                            "out of memory recording type-bound procedure");
    if (binding->deferred && target_token != NULL)
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, target_token, 1,
                                  "DEFERRED binding '%s' cannot specify an implementation",
                                  binding->name != NULL ? binding->name : "<invalid>");
}

static void parse_type_bound_procedure(Context *context, F2cDerivedType *derived,
                                       const Line *line) {
    const size_t start = statement_start(line);
    const size_t double_colon =
        f2c_line_find_token(line, start + 1U, F2C_TOKEN_DOUBLE_COLON, NULL);
    const F2cToken *interface_token = NULL;
    size_t attributes_begin = start + 1U;
    size_t index;
    if (double_colon == SIZE_MAX) {
        f2c_diagnostic(context, line->number, 1, "type-bound PROCEDURE declaration requires '::'");
        return;
    }
    if (attributes_begin < double_colon &&
        line->tokens[attributes_begin].kind == F2C_TOKEN_LEFT_PAREN) {
        size_t close;
        if (!f2c_token_matching_delimiter(line->tokens, double_colon, attributes_begin, &close) ||
            close != attributes_begin + 2U ||
            line->tokens[attributes_begin + 1U].kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                      &line->tokens[attributes_begin], 1,
                                      "malformed type-bound PROCEDURE interface");
            return;
        }
        interface_token = &line->tokens[attributes_begin + 1U];
        attributes_begin = close + 1U;
    }
    index = double_colon + 1U;
    if (index >= line->token_count) {
        f2c_diagnostic(context, line->number, 1,
                       "type-bound PROCEDURE declaration has no bindings");
        return;
    }
    while (index < line->token_count) {
        const F2cToken *name_token;
        const F2cToken *target_token = NULL;
        F2cTypeBinding *binding;
        if (line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                      &line->tokens[index], 1,
                                      "malformed type-bound PROCEDURE binding");
            while (index < line->token_count && line->tokens[index].kind != F2C_TOKEN_COMMA)
                ++index;
            if (index < line->token_count)
                ++index;
            continue;
        }
        name_token = &line->tokens[index++];
        if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
            f2c_token_equals(&line->tokens[index], "=>")) {
            ++index;
            if (index >= line->token_count ||
                line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, name_token, 1,
                                          "binding implementation is missing after '=>'");
                while (index < line->token_count && line->tokens[index].kind != F2C_TOKEN_COMMA)
                    ++index;
            } else {
                target_token = &line->tokens[index++];
            }
        }
        if (index < line->token_count && line->tokens[index].kind != F2C_TOKEN_COMMA) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                      &line->tokens[index], 1,
                                      "malformed type-bound PROCEDURE binding");
            while (index < line->token_count && line->tokens[index].kind != F2C_TOKEN_COMMA)
                ++index;
        }
        binding = append_type_binding(derived);
        if (binding == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                                "out of memory recording type-bound procedure");
            return;
        }
        initialize_binding(context, derived, line, binding, name_token, target_token,
                           interface_token, attributes_begin, double_colon);
        if (index < line->token_count)
            ++index;
    }
}

static int defined_io_kind(const F2cToken *operation, const F2cToken *form,
                           F2cDefinedIoKind *kind) {
    if (f2c_token_equals(operation, "read") && f2c_token_equals(form, "formatted"))
        *kind = F2C_DEFINED_IO_READ_FORMATTED;
    else if (f2c_token_equals(operation, "write") && f2c_token_equals(form, "formatted"))
        *kind = F2C_DEFINED_IO_WRITE_FORMATTED;
    else if (f2c_token_equals(operation, "read") && f2c_token_equals(form, "unformatted"))
        *kind = F2C_DEFINED_IO_READ_UNFORMATTED;
    else if (f2c_token_equals(operation, "write") && f2c_token_equals(form, "unformatted"))
        *kind = F2C_DEFINED_IO_WRITE_UNFORMATTED;
    else
        return 0;
    return 1;
}

static void parse_defined_io_generic(Context *context, F2cDerivedType *derived, const Line *line) {
    const size_t start = statement_start(line);
    const size_t double_colon =
        f2c_line_find_token(line, start + 1U, F2C_TOKEN_DOUBLE_COLON, NULL);
    const size_t generic = double_colon != SIZE_MAX ? double_colon + 1U : SIZE_MAX;
    size_t arrow;
    F2cDefinedIoKind kind;
    char *binding;
    if (double_colon == SIZE_MAX) {
        f2c_diagnostic(context, line->number, 1, "type-bound GENERIC declaration requires '::'");
        return;
    }
    arrow = f2c_line_find_token(line, generic, F2C_TOKEN_OPERATOR, "=>");
    if (generic > SIZE_MAX - 4U || generic + 4U != arrow || arrow == SIZE_MAX ||
        line->tokens[generic].kind != F2C_TOKEN_IDENTIFIER ||
        line->tokens[generic + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
        line->tokens[generic + 2U].kind != F2C_TOKEN_IDENTIFIER ||
        line->tokens[generic + 3U].kind != F2C_TOKEN_RIGHT_PAREN ||
        !defined_io_kind(&line->tokens[generic], &line->tokens[generic + 2U], &kind))
        return;
    if (arrow + 2U != line->token_count ||
        line->tokens[arrow + 1U].kind != F2C_TOKEN_IDENTIFIER) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[arrow], 1,
                                  "defined I/O generic must resolve to one specific binding");
        return;
    }
    if (derived->defined_io_bindings[kind] != NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                  &line->tokens[generic], 1,
                                  "duplicate defined I/O generic");
        return;
    }
    binding = identifier_text(&line->tokens[arrow + 1U]);
    if (binding == NULL) {
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                            "out of memory recording defined I/O binding");
        return;
    }
    derived->defined_io_bindings[kind] = binding;
}

static int header_has_attribute(const Line *line, const char *attribute) {
    const size_t start = statement_start(line);
    const size_t double_colon =
        f2c_line_find_token(line, start + 1U, F2C_TOKEN_DOUBLE_COLON, NULL);
    const size_t end = double_colon != SIZE_MAX ? double_colon : line->token_count;
    size_t index;
    for (index = start + 1U; index < end; ++index) {
        if (line->tokens[index].kind == F2C_TOKEN_IDENTIFIER &&
            f2c_token_equals(&line->tokens[index], attribute))
            return 1;
    }
    return 0;
}

static void parse_parent_type(Context *context, Unit *unit, F2cDerivedType *derived) {
    const Line *line = &context->lines.items[derived->begin];
    const size_t start = statement_start(line);
    const size_t double_colon =
        f2c_line_find_token(line, start + 1U, F2C_TOKEN_DOUBLE_COLON, NULL);
    const size_t end = double_colon != SIZE_MAX ? double_colon : line->token_count;
    size_t index;
    for (index = start + 1U; index < end; ++index) {
        size_t close;
        if (!f2c_line_token_equals(line, index, "extends"))
            continue;
        if (index + 1U >= end || line->tokens[index + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
            !f2c_token_matching_delimiter(line->tokens, end, index + 1U, &close) ||
            close != index + 3U || line->tokens[index + 2U].kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                      &line->tokens[index], 1, "malformed EXTENDS attribute");
            return;
        }
        derived->parent_name = identifier_text(&line->tokens[index + 2U]);
        derived->parent =
            derived->parent_name != NULL ? f2c_find_derived_type(unit, derived->parent_name) : NULL;
        if (derived->parent == NULL) {
            f2c_diagnostic_token_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[index + 2U], 1,
                "EXTENDS parent '%s' is not a visible derived type",
                derived->parent_name != NULL ? derived->parent_name : "<invalid>");
        } else if (derived->parent == derived) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                      &line->tokens[index + 2U], 1,
                                      "derived type '%s' cannot extend itself", derived->name);
            derived->parent = NULL;
        }
        return;
    }
}

static void append_finalizer(Context *context, F2cDerivedType *derived, const Line *line,
                             const F2cToken *name_token) {
    char **replacement;
    char *name;
    if (derived->finalizer_count == SIZE_MAX ||
        derived->finalizer_count + 1U > SIZE_MAX / sizeof(*replacement)) {
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                            "too many FINAL procedures");
        return;
    }
    name = identifier_text(name_token);
    replacement = (char **)realloc(derived->finalizers,
                                   (derived->finalizer_count + 1U) * sizeof(*replacement));
    if (name == NULL || replacement == NULL) {
        free(name);
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                            "out of memory recording FINAL procedure");
        return;
    }
    derived->finalizers = replacement;
    derived->finalizers[derived->finalizer_count++] = name;
}

static void parse_finalizers(Context *context, F2cDerivedType *derived, const Line *line) {
    size_t index = statement_start(line) + 1U;
    int expect_name = 1;
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON)
        ++index;
    for (; index < line->token_count; ++index) {
        if (expect_name && line->tokens[index].kind == F2C_TOKEN_IDENTIFIER) {
            append_finalizer(context, derived, line, &line->tokens[index]);
            expect_name = 0;
        } else if (!expect_name && line->tokens[index].kind == F2C_TOKEN_COMMA) {
            expect_name = 1;
        } else {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                      &line->tokens[index], 1,
                                      "malformed FINAL procedure list");
            return;
        }
    }
    if (expect_name)
        f2c_diagnostic(context, line->number, 1, "FINAL declaration requires a procedure name");
}

static F2cDerivedType *append_derived_type(Context *context, Unit *unit, const Line *line,
                                           char *name, size_t begin, size_t end) {
    F2cDerivedType *replacement;
    F2cDerivedType *derived;
    size_t capacity;
    if (unit->derived_type_count == unit->derived_type_capacity) {
        if (unit->derived_type_capacity > SIZE_MAX / 2U)
            return NULL;
        capacity = unit->derived_type_capacity == 0U ? 4U : unit->derived_type_capacity * 2U;
        if (capacity > SIZE_MAX / sizeof(*replacement))
            return NULL;
        replacement =
            (F2cDerivedType *)realloc(unit->derived_types, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return NULL;
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
    if (derived->c_name == NULL)
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                            "out of memory naming derived type '%s'", name);
    derived->begin = begin;
    derived->end = end;
    derived->abstract_type = header_has_attribute(line, "abstract");
    return derived;
}

void f2c_parse_derived_type_definitions(Context *context, Unit *unit) {
    size_t line_index;
    size_t type_index;
    for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
        Line *line = &context->lines.items[line_index];
        const F2cToken *name_token;
        char *name;
        size_t end;
        if (!f2c_derived_type_start_tokens(line))
            continue;
        name_token = derived_type_name_token(line);
        name = identifier_text(name_token);
        if (name == NULL)
            continue;
        for (end = line_index + 1U; end < unit->end; ++end) {
            if (f2c_derived_type_end_tokens(&context->lines.items[end]))
                break;
        }
        if (end == unit->end) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, name_token, 1,
                                      "unterminated derived-type definition '%s'", name);
            free(name);
            continue;
        }
        if (f2c_find_derived_type(unit, name) != NULL) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, name_token, 1,
                                      "duplicate derived-type definition '%s'", name);
            free(name);
            line_index = end;
            continue;
        }
        if (append_derived_type(context, unit, line, name, line_index, end) == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                                "out of memory recording derived type '%s'", name);
            free(name);
            return;
        }
        line_index = end;
    }
    for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
        F2cDerivedType *derived = &unit->derived_types[type_index];
        Unit component_scope;
        int in_bindings = 0;
        parse_parent_type(context, unit, derived);
        memset(&component_scope, 0, sizeof(component_scope));
        component_scope.context = context;
        component_scope.name = unit->name;
        component_scope.signature_host = unit;
        component_scope.derived_types = unit->derived_types;
        component_scope.derived_type_count = unit->derived_type_count;
        component_scope.options = unit->options;
        for (line_index = derived->begin + 1U; line_index < derived->end; ++line_index) {
            Line *line = &context->lines.items[line_index];
            const size_t start = statement_start(line);
            if (f2c_contains_tokens(line)) {
                in_bindings = 1;
                continue;
            }
            if (f2c_line_token_equals(line, start, "final")) {
                parse_finalizers(context, derived, line);
                continue;
            }
            if (in_bindings && f2c_line_token_equals(line, start, "procedure")) {
                parse_type_bound_procedure(context, derived, line);
                continue;
            }
            if (in_bindings && f2c_line_token_equals(line, start, "generic")) {
                parse_defined_io_generic(context, derived, line);
                continue;
            }
            if (in_bindings)
                continue;
            f2c_parse_procedure_declaration(context, &component_scope, line);
            f2c_parse_declaration(context, &component_scope, line);
        }
        derived->components = component_scope.symbols;
        derived->component_count = component_scope.symbol_count;
        derived->component_capacity = component_scope.symbol_capacity;
        for (line_index = 0U; line_index < derived->component_count; ++line_index)
            derived->components[line_index].derived_owner = derived;
    }
}
