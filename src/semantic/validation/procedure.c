#include "semantic/validation/private.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int array_storage_sequence_actual(const F2cExpr *actual) {
    return actual != NULL &&
           (actual->kind == F2C_EXPR_NAME || actual->kind == F2C_EXPR_ARRAY_REFERENCE) &&
           actual->symbol != NULL && actual->symbol->rank != 0U;
}

static int interface_candidate_matches(Unit *candidate, F2cExpr *const *arguments,
                                       size_t argument_count, int subroutine_call) {
    unsigned char *assigned;
    size_t next_positional = 0U;
    size_t i;
    int saw_keyword = 0;
    int matches = 0;
    if (argument_count > candidate->argument_count ||
        (subroutine_call && candidate->kind != UNIT_SUBROUTINE) ||
        (!subroutine_call && candidate->kind != UNIT_FUNCTION))
        return 0;
    assigned = candidate->argument_count != 0U
                   ? (unsigned char *)calloc(candidate->argument_count, sizeof(*assigned))
                   : NULL;
    if (candidate->argument_count != 0U && assigned == NULL)
        return 0;
    for (i = 0U; i < argument_count; ++i) {
        const F2cExpr *actual = arguments != NULL ? arguments[i] : NULL;
        const F2cExpr *value = f2c_validation_actual_value(actual);
        size_t target = SIZE_MAX;
        Symbol *dummy;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            size_t d;
            saw_keyword = 1;
            for (d = 0U; d < candidate->argument_count; ++d) {
                if (actual->text != NULL && strcmp(actual->text, candidate->arguments[d]) == 0) {
                    target = d;
                    break;
                }
            }
            if (target == SIZE_MAX)
                goto done;
        } else {
            if (saw_keyword)
                goto done;
            target = next_positional++;
        }
        if (target >= candidate->argument_count || assigned[target])
            goto done;
        assigned[target] = 1U;
        dummy = f2c_find_symbol(candidate, candidate->arguments[target]);
        if (dummy == NULL || value == NULL)
            continue;
        if (value->kind == F2C_EXPR_ABSENT_ARGUMENT) {
            if (!dummy->optional)
                goto done;
            continue;
        }
        if (dummy->external) {
            if (value->kind != F2C_EXPR_NAME || value->symbol == NULL || !value->symbol->external ||
                value->symbol->external_subroutine != dummy->external_subroutine)
                goto done;
        } else if ((dummy->type != TYPE_UNKNOWN && value->type != TYPE_UNKNOWN &&
                    (dummy->type != value->type || (dummy->kind != 0 && value->type_kind != 0 &&
                                                    dummy->kind != value->type_kind))) ||
                   (dummy->rank != value->rank && !(candidate->elemental && dummy->rank == 0U) &&
                    !(dummy->rank != 0U && array_storage_sequence_actual(value))) ||
                   (dummy->allocatable && (value->kind != F2C_EXPR_NAME || value->symbol == NULL ||
                                           !value->symbol->allocatable))) {
            goto done;
        }
    }
    for (i = 0U; i < candidate->argument_count; ++i) {
        Symbol *dummy;
        if (assigned[i])
            continue;
        dummy = f2c_find_symbol(candidate, candidate->arguments[i]);
        if (dummy == NULL || !dummy->optional)
            goto done;
    }
    matches = 1;
done:
    free(assigned);
    return matches;
}

static size_t select_scope_interface(Unit *scope, const char *name, const char *resolved_name,
                                     F2cExpr *const *arguments, size_t argument_count,
                                     int subroutine_call, Unit **selection,
                                     size_t *matching_count) {
    size_t candidate_count = 0U;
    size_t i;
    int has_visible_name = 0;
    *selection = NULL;
    *matching_count = 0U;
    for (i = 0U; i < scope->interface_count; ++i) {
        Unit *candidate = &scope->interfaces[i];
        if (candidate->interface_abstract)
            continue;
        const char *visible_name = candidate->interface_generic_name != NULL
                                       ? candidate->interface_generic_name
                                       : candidate->name;
        if (strcmp(visible_name, name) == 0) {
            has_visible_name = 1;
            break;
        }
    }
    for (i = 0U; i < scope->interface_count; ++i) {
        Unit *candidate = &scope->interfaces[i];
        if (candidate->interface_abstract)
            continue;
        const char *visible_name = candidate->interface_generic_name != NULL
                                       ? candidate->interface_generic_name
                                       : candidate->name;
        const int named = has_visible_name ? strcmp(visible_name, name) == 0
                                           : strcmp(candidate->name, resolved_name) == 0;
        if (!named)
            continue;
        ++candidate_count;
        if (interface_candidate_matches(candidate, arguments, argument_count, subroutine_call)) {
            *selection = candidate;
            ++*matching_count;
        }
    }
    if (candidate_count == 1U && *matching_count == 0U) {
        for (i = 0U; i < scope->interface_count; ++i) {
            Unit *candidate = &scope->interfaces[i];
            if (candidate->interface_abstract)
                continue;
            const char *visible_name = candidate->interface_generic_name != NULL
                                           ? candidate->interface_generic_name
                                           : candidate->name;
            const int named = has_visible_name ? strcmp(visible_name, name) == 0
                                               : strcmp(candidate->name, resolved_name) == 0;
            if (named) {
                *selection = candidate;
                break;
            }
        }
    }
    return candidate_count;
}

static size_t select_generic_symbol(Symbol *generic, F2cExpr *const *arguments,
                                    size_t argument_count, int subroutine_call, Unit **selection,
                                    size_t *matching_count) {
    size_t index;
    *selection = NULL;
    *matching_count = 0U;
    for (index = 0U; index < generic->generic_candidate_count; ++index) {
        Unit *candidate = generic->generic_candidates[index];
        if (candidate != NULL &&
            interface_candidate_matches(candidate, arguments, argument_count, subroutine_call)) {
            *selection = candidate;
            ++*matching_count;
        }
    }
    if (generic->generic_candidate_count == 1U && *matching_count == 0U)
        *selection = generic->generic_candidates[0];
    return generic->generic_candidate_count;
}

static size_t select_explicit_interface(Context *context, Unit *caller, const char *name,
                                        F2cExpr *const *arguments, size_t argument_count,
                                        int subroutine_call, Unit **selection,
                                        size_t *matching_count) {
    Symbol *symbol = f2c_find_symbol(caller, name);
    const char *resolved_name =
        symbol != NULL && symbol->external && symbol->c_name != NULL ? symbol->c_name : name;
    size_t count;
    if (symbol != NULL && symbol->procedure_interface != NULL) {
        *selection = symbol->procedure_interface;
        *matching_count =
            interface_candidate_matches(*selection, arguments, argument_count, subroutine_call)
                ? 1U
                : 0U;
        return 1U;
    }
    if (symbol != NULL && symbol->generic_candidate_count != 0U)
        return select_generic_symbol(symbol, arguments, argument_count, subroutine_call, selection,
                                     matching_count);
    count = select_scope_interface(caller, name, resolved_name, arguments, argument_count,
                                   subroutine_call, selection, matching_count);
    if (count == 0U && caller->internal && caller->host_index < context->units.count) {
        count = select_scope_interface(&context->units.items[caller->host_index], name,
                                       resolved_name, arguments, argument_count, subroutine_call,
                                       selection, matching_count);
    }
    return count;
}

static F2cSourceSpan diagnostic_span(const F2cSourceSpan *span, size_t line) {
    F2cSourceSpan result = {0};
    if (span != NULL && span->begin.line != 0U)
        return *span;
    result.begin.line = line;
    result.begin.column = 1U;
    result.end = result.begin;
    return result;
}

static int has_explicit_argument_association(F2cExpr *const *arguments, size_t argument_count) {
    size_t i;
    for (i = 0U; i < argument_count; ++i) {
        if (arguments != NULL && arguments[i] != NULL &&
            (arguments[i]->kind == F2C_EXPR_KEYWORD_ARGUMENT ||
             arguments[i]->kind == F2C_EXPR_ABSENT_ARGUMENT))
            return 1;
    }
    return 0;
}

static int bind_procedure_arguments(Context *context, Unit *definition, size_t line,
                                    const char *statement_text, const char *name,
                                    const F2cSourceSpan *call_span, F2cExpr ***arguments_io,
                                    char ***items_io, size_t *argument_count_io) {
    F2cExpr **ordered_arguments = NULL;
    char **ordered_items = NULL;
    unsigned char *assigned = NULL;
    unsigned char *created = NULL;
    F2cExpr **arguments = arguments_io != NULL ? *arguments_io : NULL;
    char **items = items_io != NULL ? *items_io : NULL;
    const size_t argument_count = argument_count_io != NULL ? *argument_count_io : 0U;
    size_t next_positional = 0U;
    size_t i;
    int saw_keyword = 0;
    int valid = 1;
    const F2cSourceSpan call_diagnostic_span = diagnostic_span(call_span, line);
    if (definition->argument_count != 0U) {
        ordered_arguments =
            (F2cExpr **)calloc(definition->argument_count, sizeof(*ordered_arguments));
        ordered_items = (char **)calloc(definition->argument_count, sizeof(*ordered_items));
        assigned = (unsigned char *)calloc(definition->argument_count, sizeof(*assigned));
        created = (unsigned char *)calloc(definition->argument_count, sizeof(*created));
    }
    if (definition->argument_count != 0U && (ordered_arguments == NULL || ordered_items == NULL ||
                                             assigned == NULL || created == NULL)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span, 1,
                                 "out of memory binding %zu arguments to procedure '%s'",
                                 definition->argument_count, name);
        goto failed;
    }
    for (i = 0U; i < argument_count; ++i) {
        F2cExpr *actual = arguments != NULL ? arguments[i] : NULL;
        const F2cSourceSpan actual_diagnostic_span =
            diagnostic_span(actual != NULL ? &actual->span : NULL, line);
        size_t target = SIZE_MAX;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            size_t dummy_index;
            saw_keyword = 1;
            for (dummy_index = 0U; dummy_index < definition->argument_count; ++dummy_index) {
                if (actual->text != NULL &&
                    strcmp(actual->text, definition->arguments[dummy_index]) == 0) {
                    target = dummy_index;
                    break;
                }
            }
            if (target == SIZE_MAX) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &actual_diagnostic_span,
                                         1, "procedure '%s' has no dummy argument named '%s'", name,
                                         actual->text != NULL ? actual->text : "<unknown>");
                valid = 0;
                continue;
            }
        } else {
            if (saw_keyword) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "positional argument follows a keyword argument in call to "
                                  "procedure '%s'",
                                  name);
                valid = 0;
                continue;
            }
            while (next_positional < definition->argument_count && assigned[next_positional])
                ++next_positional;
            target = next_positional++;
        }
        if (target >= definition->argument_count) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, actual), 1,
                              "procedure '%s' has more actual than dummy arguments", name);
            valid = 0;
            continue;
        }
        if (assigned[target]) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &actual_diagnostic_span, 1,
                "dummy argument '%s' is associated more than once in call to procedure '%s'",
                definition->arguments[target], name);
            valid = 0;
            continue;
        }
        assigned[target] = 1U;
        ordered_arguments[target] = actual;
        if (items != NULL)
            ordered_items[target] = items[i];
        if (actual != NULL && actual->kind == F2C_EXPR_ABSENT_ARGUMENT) {
            Symbol *dummy = f2c_find_symbol(definition, definition->arguments[target]);
            if (actual->source != NULL) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
                    "an actual argument cannot be omitted with an empty positional slot in call "
                    "to procedure '%s'; use a keyword for later arguments",
                    name);
                valid = 0;
            } else if (dummy == NULL || !dummy->optional) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
                    "dummy argument '%s' of procedure '%s' is not OPTIONAL and cannot be omitted",
                    definition->arguments[target], name);
                valid = 0;
            } else {
                actual->type = dummy->type;
                actual->rank = dummy->rank;
            }
        }
    }
    for (i = 0U; i < definition->argument_count; ++i) {
        Symbol *dummy;
        if (assigned[i])
            continue;
        dummy = f2c_find_symbol(definition, definition->arguments[i]);
        if (dummy == NULL || !dummy->optional) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
                "required dummy argument '%s' of procedure '%s' has no actual argument",
                definition->arguments[i], name);
            valid = 0;
            continue;
        }
        ordered_arguments[i] = f2c_expr_new_absent(dummy->type, dummy->rank);
        if (ordered_arguments[i] == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span,
                                     1, "out of memory while binding call to procedure '%s'", name);
            valid = 0;
            continue;
        }
        created[i] = 1U;
        if (items != NULL) {
            ordered_items[i] = f2c_strdup("");
            if (ordered_items[i] == NULL) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span, 1,
                    "out of memory while binding call to procedure '%s'", name);
                valid = 0;
            }
        }
    }
    if (!valid) {
        goto failed;
    }
    if (definition->argument_count != argument_count) {
        F2cExpr **replacement =
            definition->argument_count != 0U
                ? (F2cExpr **)realloc(arguments, definition->argument_count * sizeof(*arguments))
                : NULL;
        if (definition->argument_count != 0U && replacement == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span,
                                     1, "out of memory while expanding call to procedure '%s'",
                                     name);
            goto failed;
        }
        arguments = replacement;
        *arguments_io = arguments;
        if (items_io != NULL) {
            char **item_replacement =
                definition->argument_count != 0U
                    ? (char **)realloc(items, definition->argument_count * sizeof(*items))
                    : NULL;
            if (definition->argument_count != 0U && item_replacement == NULL) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span, 1,
                    "out of memory while expanding call to procedure '%s'", name);
                goto failed;
            }
            items = item_replacement;
            *items_io = items;
        }
    }
    for (i = 0U; i < definition->argument_count; ++i) {
        arguments[i] = ordered_arguments[i];
        if (items != NULL)
            items[i] = ordered_items[i];
    }
    *argument_count_io = definition->argument_count;
    free(ordered_arguments);
    free(ordered_items);
    free(assigned);
    free(created);
    return 1;

failed:
    if (created != NULL) {
        for (i = 0U; i < definition->argument_count; ++i) {
            if (created[i]) {
                f2c_expr_free(ordered_arguments[i]);
                free(ordered_items[i]);
            }
        }
    }
    free(ordered_arguments);
    free(ordered_items);
    free(assigned);
    free(created);
    return 0;
}

int f2c_validation_procedure_signatures_compatible(const Symbol *expected, const Symbol *actual,
                                                   unsigned int depth) {
    size_t i;
    if (expected == NULL || actual == NULL || depth > 16U ||
        expected->external_subroutine != actual->external_subroutine ||
        (!expected->external_subroutine && expected->type != TYPE_UNKNOWN &&
         actual->type != TYPE_UNKNOWN && expected->type != actual->type) ||
        expected->external_parameter_count != actual->external_parameter_count)
        return 0;
    for (i = 0U; i < expected->external_parameter_count; ++i) {
        Symbol *expected_procedure = expected->external_parameter_procedures[i];
        Symbol *actual_procedure = actual->external_parameter_procedures[i];
        if (expected->external_parameter_types[i] != actual->external_parameter_types[i] ||
            expected->external_parameter_ranks[i] != actual->external_parameter_ranks[i] ||
            expected->external_parameter_intents[i] != actual->external_parameter_intents[i] ||
            expected->external_parameter_optional[i] != actual->external_parameter_optional[i] ||
            expected->external_parameter_allocatable[i] !=
                actual->external_parameter_allocatable[i] ||
            expected->external_parameter_pointer[i] != actual->external_parameter_pointer[i] ||
            expected->external_parameter_descriptor[i] !=
                actual->external_parameter_descriptor[i] ||
            expected->external_parameter_derived_types[i] !=
                actual->external_parameter_derived_types[i] ||
            expected->external_parameter_polymorphic[i] !=
                actual->external_parameter_polymorphic[i] ||
            (expected_procedure == NULL) != (actual_procedure == NULL))
            return 0;
        if (expected_procedure != NULL && !f2c_validation_procedure_signatures_compatible(
                                              expected_procedure, actual_procedure, depth + 1U))
            return 0;
    }
    return 1;
}

static int derived_extends(const F2cDerivedType *candidate, const F2cDerivedType *ancestor) {
    while (candidate != NULL) {
        if (candidate == ancestor)
            return 1;
        candidate = candidate->parent;
    }
    return 0;
}

static F2cTypeBinding *find_type_binding(F2cDerivedType *derived, const char *name) {
    size_t index;
    if (derived == NULL || name == NULL)
        return NULL;
    for (index = 0U; index < derived->binding_count; ++index)
        if (strcmp(derived->bindings[index].name, name) == 0)
            return &derived->bindings[index];
    return find_type_binding(derived->parent, name);
}

static void validate_defined_io_binding(Context *context, F2cDerivedType *derived,
                                        F2cTypeBinding *binding, F2cDefinedIoKind kind) {
    const Symbol *procedure = binding != NULL ? &binding->procedure : NULL;
    const int formatted =
        kind == F2C_DEFINED_IO_READ_FORMATTED || kind == F2C_DEFINED_IO_WRITE_FORMATTED;
    const int input =
        kind == F2C_DEFINED_IO_READ_FORMATTED || kind == F2C_DEFINED_IO_READ_UNFORMATTED;
    const size_t expected_count = formatted ? 6U : 4U;
    const size_t line = context->lines.items[derived->begin].number;
    size_t parameter;
    if (procedure == NULL || !procedure->external_subroutine || binding->nopass ||
        procedure->type_bound_pass_index != 0U ||
        procedure->external_parameter_count != expected_count) {
        f2c_diagnostic(context, line, 1,
                       "defined I/O binding '%s' must be a PASS subroutine with %zu dummy "
                       "arguments",
                       binding != NULL ? binding->name : "<missing>", expected_count);
        return;
    }
    for (parameter = 0U; parameter < expected_count; ++parameter) {
        if (procedure->external_parameter_optional[parameter] ||
            procedure->external_parameter_allocatable[parameter] ||
            procedure->external_parameter_pointer[parameter])
            f2c_diagnostic(context, line, 1,
                           "defined I/O binding '%s' dummy %zu may not be OPTIONAL, "
                           "ALLOCATABLE, or POINTER",
                           binding->name, parameter + 1U);
    }
#define F2C_DTIO_REQUIRE(index, expected_type, expected_rank, expected_intent, description)        \
    do {                                                                                           \
        if (procedure->external_parameter_types[index] != (expected_type) ||                       \
            procedure->external_parameter_ranks[index] != (expected_rank) ||                       \
            procedure->external_parameter_intents[index] != (expected_intent))                     \
            f2c_diagnostic(context, line, 1, "defined I/O binding '%s' requires %s as dummy %zu",  \
                           binding->name, description, (size_t)(index) + 1U);                      \
    } while (0)
    F2C_DTIO_REQUIRE(0U, TYPE_DERIVED, 0U, input ? F2C_INTENT_INOUT : F2C_INTENT_IN,
                     input ? "scalar TYPE(dtv) INTENT(INOUT)" : "scalar TYPE(dtv) INTENT(IN)");
    if (procedure->external_parameter_derived_types[0] != derived)
        f2c_diagnostic(context, line, 1,
                       "defined I/O binding '%s' passed-object dummy must have declared type '%s'",
                       binding->name, derived->name);
    F2C_DTIO_REQUIRE(1U, TYPE_INTEGER, 0U, F2C_INTENT_IN,
                     "scalar default INTEGER unit with INTENT(IN)");
    if (formatted) {
        F2C_DTIO_REQUIRE(2U, TYPE_CHARACTER, 0U, F2C_INTENT_IN,
                         "scalar CHARACTER iotype with INTENT(IN)");
        F2C_DTIO_REQUIRE(3U, TYPE_INTEGER, 1U, F2C_INTENT_IN,
                         "rank-one default INTEGER v_list with INTENT(IN)");
        F2C_DTIO_REQUIRE(4U, TYPE_INTEGER, 0U, F2C_INTENT_OUT,
                         "scalar default INTEGER iostat with INTENT(OUT)");
        F2C_DTIO_REQUIRE(5U, TYPE_CHARACTER, 0U, F2C_INTENT_INOUT,
                         "scalar CHARACTER iomsg with INTENT(INOUT)");
    } else {
        F2C_DTIO_REQUIRE(2U, TYPE_INTEGER, 0U, F2C_INTENT_OUT,
                         "scalar default INTEGER iostat with INTENT(OUT)");
        F2C_DTIO_REQUIRE(3U, TYPE_CHARACTER, 0U, F2C_INTENT_INOUT,
                         "scalar CHARACTER iomsg with INTENT(INOUT)");
    }
#undef F2C_DTIO_REQUIRE
}

static int overriding_signatures_compatible(const F2cTypeBinding *parent,
                                            const F2cTypeBinding *child) {
    const Symbol *expected = &parent->procedure;
    const Symbol *actual = &child->procedure;
    size_t expected_argument = 0U;
    size_t actual_argument = 0U;
    if (expected->external_subroutine != actual->external_subroutine ||
        expected->external_parameter_count != actual->external_parameter_count ||
        parent->nopass != child->nopass ||
        (!expected->external_subroutine &&
         (expected->type != actual->type || expected->kind != actual->kind)))
        return 0;
    while (expected_argument < expected->external_parameter_count &&
           actual_argument < actual->external_parameter_count) {
        if (!parent->nopass && expected_argument == expected->type_bound_pass_index) {
            ++expected_argument;
            ++actual_argument;
            continue;
        }
        if (expected->external_parameter_types[expected_argument] !=
                actual->external_parameter_types[actual_argument] ||
            expected->external_parameter_kinds[expected_argument] !=
                actual->external_parameter_kinds[actual_argument] ||
            expected->external_parameter_ranks[expected_argument] !=
                actual->external_parameter_ranks[actual_argument] ||
            expected->external_parameter_intents[expected_argument] !=
                actual->external_parameter_intents[actual_argument] ||
            expected->external_parameter_optional[expected_argument] !=
                actual->external_parameter_optional[actual_argument] ||
            expected->external_parameter_allocatable[expected_argument] !=
                actual->external_parameter_allocatable[actual_argument] ||
            expected->external_parameter_pointer[expected_argument] !=
                actual->external_parameter_pointer[actual_argument] ||
            expected->external_parameter_descriptor[expected_argument] !=
                actual->external_parameter_descriptor[actual_argument])
            return 0;
        ++expected_argument;
        ++actual_argument;
    }
    return 1;
}

static Unit *binding_interface(Context *context, Unit *scope, F2cTypeBinding *binding) {
    if (binding->interface_name != NULL && binding->interface_name[0] != '\0')
        return f2c_find_interface_signature(context, scope, binding->interface_name, 1);
    return NULL;
}

static void resolve_type_binding(Context *context, Unit *scope, F2cDerivedType *derived,
                                 F2cTypeBinding *binding) {
    Unit *target = binding->deferred
                       ? NULL
                       : f2c_validation_find_procedure(context, scope, binding->target_name);
    Unit *interface = binding_interface(context, scope, binding);
    Unit *signature = interface != NULL ? interface : target;
    F2cTypeBinding *overridden = find_type_binding(derived->parent, binding->name);
    Symbol *procedure = &binding->procedure;
    Symbol *passed_dummy = NULL;
    size_t pass_index = 0U;
    size_t index;
    if (binding->deferred && !derived->abstract_type)
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "DEFERRED binding '%s' requires an ABSTRACT derived type", binding->name);
    if (binding->deferred && interface == NULL)
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "DEFERRED binding '%s' requires a visible abstract interface",
                       binding->name);
    if (!binding->deferred && target == NULL)
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "implementation '%s' for type-bound procedure '%s' is not visible",
                       binding->target_name != NULL ? binding->target_name : "<missing>",
                       binding->name);
    if (signature == NULL)
        return;
    if (interface != NULL && target != NULL) {
        Symbol expected = {0};
        Symbol actual = {0};
        if (!f2c_copy_procedure_signature(&expected, interface) ||
            !f2c_copy_procedure_signature(&actual, target) ||
            !f2c_validation_procedure_signatures_compatible(&expected, &actual, 0U))
            f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                           "implementation '%s' does not match interface '%s' for binding '%s'",
                           binding->target_name, binding->interface_name, binding->name);
        free(expected.procedure_interface_name);
        free(actual.procedure_interface_name);
    }
    if (!f2c_copy_procedure_signature(procedure, signature)) {
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "out of memory resolving type-bound procedure '%s'", binding->name);
        return;
    }
    procedure->procedure_pointer = 1;
    procedure->type_bound = 1;
    procedure->type_bound_deferred = binding->deferred;
    procedure->type_bound_nopass = binding->nopass;
    procedure->derived_owner = overridden != NULL ? overridden->storage_owner : derived;
    binding->overridden = overridden;
    binding->storage_owner = overridden != NULL ? overridden->storage_owner : derived;
    if (overridden != NULL && overridden->non_overridable)
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "binding '%s' overrides a NON_OVERRIDABLE parent binding", binding->name);
    if (!binding->nopass) {
        if (binding->pass_name != NULL) {
            for (index = 0U; index < signature->argument_count; ++index)
                if (strcmp(signature->arguments[index], binding->pass_name) == 0) {
                    pass_index = index;
                    break;
                }
            if (index == signature->argument_count)
                f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                               "PASS dummy '%s' is not present in binding '%s'", binding->pass_name,
                               binding->name);
        }
        if (pass_index < signature->argument_count)
            passed_dummy = f2c_find_symbol(signature, signature->arguments[pass_index]);
        if (passed_dummy == NULL || passed_dummy->type != TYPE_DERIVED ||
            passed_dummy->rank != 0U || passed_dummy->derived_type == NULL ||
            !derived_extends(derived, passed_dummy->derived_type))
            f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                           "passed-object dummy for binding '%s' must be a scalar object of type "
                           "'%s' or an ancestor",
                           binding->name, derived->name);
    }
    procedure->type_bound_pass_index = pass_index;
    if (overridden != NULL && !overriding_signatures_compatible(overridden, binding))
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "binding '%s' has an incompatible overriding interface", binding->name);
}

static void resolve_derived_unit(Context *context, Unit *unit) {
    size_t type_index;
    for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
        F2cDerivedType *derived = &unit->derived_types[type_index];
        size_t finalizer;
        size_t binding;
        free(derived->finalizer_procedures);
        free(derived->finalizer_ranks);
        derived->finalizer_procedures =
            derived->finalizer_count != 0U
                ? (Unit **)calloc(derived->finalizer_count, sizeof(*derived->finalizer_procedures))
                : NULL;
        derived->finalizer_ranks =
            derived->finalizer_count != 0U
                ? (size_t *)calloc(derived->finalizer_count, sizeof(*derived->finalizer_ranks))
                : NULL;
        for (finalizer = 0U; finalizer < derived->finalizer_count; ++finalizer) {
            Unit *procedure =
                f2c_validation_find_procedure(context, unit, derived->finalizers[finalizer]);
            Symbol *dummy = procedure != NULL && procedure->argument_count == 1U
                                ? f2c_find_symbol(procedure, procedure->arguments[0])
                                : NULL;
            if (procedure == NULL || procedure->kind != UNIT_SUBROUTINE ||
                procedure->argument_count != 1U || dummy == NULL || dummy->type != TYPE_DERIVED ||
                dummy->derived_type != derived) {
                f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                               "FINAL procedure '%s' must be a visible one-argument subroutine "
                               "whose dummy has type '%s'",
                               derived->finalizers[finalizer], derived->name);
                continue;
            }
            for (size_t previous = 0U; previous < finalizer; ++previous)
                if (derived->finalizer_procedures[previous] != NULL &&
                    derived->finalizer_ranks[previous] == dummy->rank)
                    f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                                   "FINAL procedures for type '%s' have duplicate rank %zu",
                                   derived->name, dummy->rank);
            derived->finalizer_procedures[finalizer] = procedure;
            derived->finalizer_ranks[finalizer] = dummy->rank;
        }
        for (binding = 0U; binding < derived->binding_count; ++binding)
            resolve_type_binding(context, unit, derived, &derived->bindings[binding]);
        for (binding = 0U; binding < F2C_DEFINED_IO_COUNT; ++binding) {
            const char *name = derived->defined_io_bindings[binding];
            F2cTypeBinding *io_binding = name != NULL ? find_type_binding(derived, name) : NULL;
            if (name != NULL && io_binding == NULL)
                f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                               "defined I/O generic references unknown binding '%s'", name);
            else if (io_binding != NULL)
                validate_defined_io_binding(context, derived, io_binding,
                                            (F2cDefinedIoKind)binding);
        }
    }
}

void f2c_resolve_derived_semantics(Context *context) {
    size_t unit;
    for (unit = 0U; unit < context->modules.count; ++unit)
        resolve_derived_unit(context, &context->modules.items[unit]);
    for (unit = 0U; unit < context->units.count; ++unit)
        resolve_derived_unit(context, &context->units.items[unit]);
}

static int has_vector_subscript(const F2cExpr *expression) {
    size_t selector;
    if (expression == NULL || expression->kind != F2C_EXPR_ARRAY_REFERENCE)
        return 0;
    for (selector = 0U; selector < expression->child_count; ++selector)
        if (expression->children[selector]->kind != F2C_EXPR_ARRAY_SECTION &&
            expression->children[selector]->rank != 0U)
            return 1;
    return 0;
}

static void validate_procedure_actual(Context *context, Unit *caller, const Unit *definition,
                                      const Symbol *dummy, const F2cExpr *actual, size_t index,
                                      size_t line, const char *statement_text) {
    const size_t column = f2c_validation_expression_start_column(statement_text, actual);
    const F2cExpr *value = f2c_validation_actual_value(actual);
    if (dummy == NULL || value == NULL)
        return;
    if (value->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return;
    if (dummy->external) {
        const Symbol *procedure = value->kind == F2C_EXPR_NAME ? value->symbol : NULL;
        if (procedure == NULL || !procedure->external) {
            f2c_diagnostic_at(context, line, column, 1,
                              "argument %zu of procedure '%s' must be a procedure", index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name);
            return;
        }
        if (procedure->external_subroutine != dummy->external_subroutine) {
            f2c_diagnostic_at(context, line, column, 1,
                              "procedure actual argument %zu of '%s' has an incompatible "
                              "procedure kind",
                              index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name);
        } else if (!dummy->external_subroutine && dummy->type != TYPE_UNKNOWN &&
                   procedure->type != TYPE_UNKNOWN && dummy->type != procedure->type) {
            f2c_diagnostic_at(
                context, line, column, 1,
                "procedure actual argument %zu of '%s' returns %s but the dummy "
                "procedure returns %s",
                index + 1U,
                definition->fortran_name != NULL ? definition->fortran_name : definition->name,
                f2c_validation_type_name(procedure->type), f2c_validation_type_name(dummy->type));
        } else if (!f2c_validation_procedure_signatures_compatible(dummy, procedure, 0U)) {
            f2c_diagnostic_at(context, line, column, 1,
                              "procedure actual argument %zu of '%s' has an incompatible "
                              "explicit interface",
                              index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name);
        }
        return;
    }
    if (dummy->type != TYPE_UNKNOWN && value->type != TYPE_UNKNOWN && dummy->type != value->type) {
        f2c_diagnostic_at(
            context, line, column, 1,
            "argument %zu of procedure '%s' has type %s but dummy '%s' has type %s", index + 1U,
            definition->fortran_name != NULL ? definition->fortran_name : definition->name,
            f2c_validation_type_name(value->type), dummy->name,
            f2c_validation_type_name(dummy->type));
    }
    if (dummy->type == TYPE_DERIVED && value->type == TYPE_DERIVED &&
        dummy->derived_type != value->derived_type &&
        (!dummy->polymorphic || !derived_extends(value->derived_type, dummy->derived_type))) {
        f2c_diagnostic_at(context, line, column, 1,
                          "argument %zu of procedure '%s' has incompatible dynamic derived type "
                          "for dummy '%s'",
                          index + 1U,
                          definition->fortran_name != NULL ? definition->fortran_name
                                                           : definition->name,
                          dummy->name);
    }
    if (dummy->kind != 0 && value->type_kind != 0 && dummy->kind != value->type_kind) {
        f2c_diagnostic_at(context, line, value->source_offset + 1U, 1,
                          "argument %zu of procedure '%s' has kind %d but dummy '%s' has kind %d",
                          index + 1U, definition->name, value->type_kind, dummy->name, dummy->kind);
    }
    if (dummy->allocatable &&
        (value->kind != F2C_EXPR_NAME || value->symbol == NULL || !value->symbol->allocatable)) {
        f2c_diagnostic_at(context, line, value->source_offset + 1U, 1,
                          "argument %zu of procedure '%s' must be an ALLOCATABLE whole object for "
                          "dummy '%s'",
                          index + 1U, definition->name, dummy->name);
    }
    if (dummy->pointer &&
        (value->kind != F2C_EXPR_NAME || value->symbol == NULL || !value->symbol->pointer)) {
        f2c_diagnostic_at(context, line, value->source_offset + 1U, 1,
                          "argument %zu of procedure '%s' must be a POINTER whole object for "
                          "dummy '%s'",
                          index + 1U, definition->name, dummy->name);
    }
    if (value->rank != dummy->rank && !(definition->elemental && dummy->rank == 0U) &&
        !(dummy->rank != 0U && array_storage_sequence_actual(value))) {
        f2c_diagnostic_at(
            context, line, column, 1,
            "argument %zu of procedure '%s' has rank %zu but dummy '%s' has rank %zu", index + 1U,
            definition->fortran_name != NULL ? definition->fortran_name : definition->name,
            value->rank, dummy->name, dummy->rank);
    } else if (value->rank == dummy->rank && value->rank != 0U &&
               value->kind != F2C_EXPR_ARRAY_REFERENCE) {
        size_t dimension;
        if (f2c_validation_symbol_shape_mismatch(dummy, value, &dimension)) {
            f2c_diagnostic_at(context, line, column, 1,
                              "argument %zu of procedure '%s' is nonconformable with dummy '%s' in "
                              "dimension %zu",
                              index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name,
                              dummy->name, dimension + 1U);
        }
    }
    if ((dummy->intent == F2C_INTENT_OUT || dummy->intent == F2C_INTENT_INOUT) &&
        has_vector_subscript(value)) {
        f2c_diagnostic_at(context, line, column, 1,
                          "argument %zu of procedure '%s' uses a vector subscript but dummy '%s' "
                          "has INTENT(%s)",
                          index + 1U,
                          definition->fortran_name != NULL ? definition->fortran_name
                                                           : definition->name,
                          dummy->name, dummy->intent == F2C_INTENT_OUT ? "OUT" : "INOUT");
    } else if ((dummy->intent == F2C_INTENT_OUT || dummy->intent == F2C_INTENT_INOUT) &&
               !value->definable) {
        f2c_diagnostic_at(context, line, column, 1,
                          "argument %zu of procedure '%s' is not definable but dummy '%s' has "
                          "INTENT(%s)",
                          index + 1U,
                          definition->fortran_name != NULL ? definition->fortran_name
                                                           : definition->name,
                          dummy->name, dummy->intent == F2C_INTENT_OUT ? "OUT" : "INOUT");
    }
    (void)caller;
}

static void validate_elemental_conformance(Context *context, const Unit *definition, size_t line,
                                           const char *statement_text, F2cExpr *const *arguments,
                                           size_t argument_count) {
    const F2cExpr *array_argument = NULL;
    size_t i;
    if (definition == NULL || !definition->elemental)
        return;
    for (i = 0U; i < argument_count; ++i) {
        const F2cExpr *argument = f2c_validation_actual_value(arguments[i]);
        size_t dimension;
        if (argument == NULL || argument->kind == F2C_EXPR_ABSENT_ARGUMENT || argument->rank == 0U)
            continue;
        if (array_argument == NULL) {
            array_argument = argument;
        } else if (array_argument->rank != argument->rank) {
            f2c_diagnostic_at(
                context, line, f2c_validation_expression_start_column(statement_text, argument), 1,
                "ELEMENTAL procedure '%s' has nonconformable actual argument ranks %zu and %zu",
                definition->fortran_name != NULL ? definition->fortran_name : definition->name,
                array_argument->rank, argument->rank);
        } else if (f2c_validation_shapes_mismatch(array_argument, argument, &dimension)) {
            f2c_diagnostic_at(
                context, line, f2c_validation_expression_start_column(statement_text, argument), 1,
                "ELEMENTAL procedure '%s' has nonconformable actual argument extent in "
                "dimension %zu",
                definition->fortran_name != NULL ? definition->fortran_name : definition->name,
                dimension + 1U);
        }
    }
}

Unit *f2c_validation_procedure_call(Context *context, Unit *caller, size_t line,
                                    const char *statement_text, const char *name,
                                    const F2cSourceSpan *call_span, F2cExpr ***arguments,
                                    char ***items, size_t *argument_count, int subroutine_call) {
    Unit *definition = NULL;
    size_t matching_interfaces = 0U;
    const size_t interface_count =
        select_explicit_interface(context, caller, name, arguments != NULL ? *arguments : NULL,
                                  argument_count != NULL ? *argument_count : 0U, subroutine_call,
                                  &definition, &matching_interfaces);
    const F2cSourceSpan call_diagnostic_span = diagnostic_span(call_span, line);
    size_t i;
    if (interface_count > 1U && matching_interfaces != 1U) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
            matching_interfaces == 0U
                ? "generic interface '%s' has no specific procedure matching this actual argument "
                  "list"
                : "generic interface '%s' is ambiguous for this actual argument list",
            name);
        return NULL;
    }
    if (interface_count == 0U)
        definition = f2c_validation_find_procedure(context, caller, name);
    if (definition == NULL) {
        if (interface_count == 0U &&
            has_explicit_argument_association(arguments != NULL ? *arguments : NULL,
                                              argument_count != NULL ? *argument_count : 0U)) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
                "keyword arguments in call to procedure '%s' require a visible explicit interface",
                name);
        }
        return NULL;
    }
    if ((subroutine_call && definition->kind != UNIT_SUBROUTINE) ||
        (!subroutine_call && definition->kind != UNIT_FUNCTION)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
                                 "procedure '%s' is called as a %s but is defined as a %s", name,
                                 subroutine_call ? "SUBROUTINE" : "FUNCTION",
                                 definition->kind == UNIT_SUBROUTINE ? "SUBROUTINE" : "FUNCTION");
        return NULL;
    }
    if (argument_count != NULL && *argument_count > definition->argument_count) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
            "procedure '%s' is called with %zu arguments but is defined with %zu", name,
            *argument_count, definition->argument_count);
        return NULL;
    }
    if (!bind_procedure_arguments(context, definition, line, statement_text, name, call_span,
                                  arguments, items, argument_count))
        return NULL;
    for (i = 0U; i < *argument_count; ++i) {
        Symbol *dummy = f2c_find_symbol(definition, definition->arguments[i]);
        validate_procedure_actual(context, caller, definition, dummy,
                                  arguments != NULL && *arguments != NULL ? (*arguments)[i] : NULL,
                                  i, line, statement_text);
    }
    validate_elemental_conformance(context, definition, line, statement_text,
                                   arguments != NULL ? *arguments : NULL, *argument_count);
    return definition;
}
