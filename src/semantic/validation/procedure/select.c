#include "semantic/validation/private.h"

#include <stdlib.h>
#include <string.h>

static int array_storage_sequence_actual(const F2cExpr *actual) {
    return actual != NULL &&
           (actual->kind == F2C_EXPR_NAME || actual->kind == F2C_EXPR_ARRAY_REFERENCE) &&
           actual->symbol != NULL && actual->symbol->rank != 0U;
}

static int assumed_shape_dummy(const Symbol *dummy) {
    size_t dimension;
    if (dummy == NULL || !dummy->argument)
        return 0;
    for (dimension = 0U; dimension < dummy->rank; ++dimension)
        if (dummy->dimensions[dimension].kind == F2C_DIMENSION_ASSUMED_SHAPE)
            return 1;
    return 0;
}

static int interface_candidate_matches(Unit *candidate, F2cExpr *const *arguments,
                                       size_t argument_count, int subroutine_call) {
    unsigned char *assigned;
    const size_t dummy_count = f2c_procedure_dummy_count(candidate);
    size_t next_positional = 0U;
    size_t i;
    int saw_keyword = 0;
    int matches = 0;
    if (argument_count > dummy_count || (subroutine_call && candidate->kind != UNIT_SUBROUTINE) ||
        (!subroutine_call && candidate->kind != UNIT_FUNCTION))
        return 0;
    assigned = dummy_count != 0U ? (unsigned char *)calloc(dummy_count, sizeof(*assigned)) : NULL;
    if (dummy_count != 0U && assigned == NULL)
        return 0;
    for (i = 0U; i < argument_count; ++i) {
        const F2cExpr *actual = arguments != NULL ? arguments[i] : NULL;
        const F2cExpr *value = f2c_validation_actual_value(actual);
        size_t target = SIZE_MAX;
        size_t argument_index;
        Symbol *dummy;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            size_t d;
            saw_keyword = 1;
            for (d = 0U; d < candidate->argument_count; ++d) {
                if (actual->text != NULL && strcmp(actual->text, candidate->arguments[d]) == 0) {
                    target = f2c_procedure_dummy_slot(candidate, d);
                    break;
                }
            }
            if (target == SIZE_MAX)
                goto done;
        } else {
            if (saw_keyword)
                goto done;
            while (next_positional < dummy_count && assigned[next_positional])
                ++next_positional;
            target = next_positional++;
        }
        if (target >= dummy_count || assigned[target])
            goto done;
        assigned[target] = 1U;
        argument_index = f2c_procedure_dummy_argument_index(candidate, target);
        if (argument_index == SIZE_MAX) {
            if (actual == NULL || actual->kind != F2C_EXPR_ALTERNATE_RETURN || actual->text == NULL)
                goto done;
            continue;
        }
        if (actual != NULL && actual->kind == F2C_EXPR_ALTERNATE_RETURN)
            goto done;
        dummy = f2c_find_symbol(candidate, candidate->arguments[argument_index]);
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
                   (assumed_shape_dummy(dummy) && f2c_expression_is_whole_assumed_size(value)) ||
                   (dummy->rank != value->rank && !(candidate->elemental && dummy->rank == 0U) &&
                    !(dummy->rank != 0U && array_storage_sequence_actual(value))) ||
                   (dummy->allocatable && (value->kind != F2C_EXPR_NAME || value->symbol == NULL ||
                                           !value->symbol->allocatable))) {
            goto done;
        }
    }
    for (i = 0U; i < dummy_count; ++i) {
        const size_t argument_index = f2c_procedure_dummy_argument_index(candidate, i);
        Symbol *dummy;
        if (assigned[i])
            continue;
        if (argument_index == SIZE_MAX)
            goto done;
        dummy = f2c_find_symbol(candidate, candidate->arguments[argument_index]);
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
        const char *visible_name;
        if (candidate->interface_abstract)
            continue;
        visible_name = candidate->interface_generic_name != NULL ? candidate->interface_generic_name
                                                                 : candidate->name;
        if (strcmp(visible_name, name) == 0) {
            has_visible_name = 1;
            break;
        }
    }
    for (i = 0U; i < scope->interface_count; ++i) {
        Unit *candidate = &scope->interfaces[i];
        const char *visible_name;
        int named;
        if (candidate->interface_abstract)
            continue;
        visible_name = candidate->interface_generic_name != NULL ? candidate->interface_generic_name
                                                                 : candidate->name;
        named = has_visible_name ? strcmp(visible_name, name) == 0
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
            const char *visible_name;
            int named;
            if (candidate->interface_abstract)
                continue;
            visible_name = candidate->interface_generic_name != NULL
                               ? candidate->interface_generic_name
                               : candidate->name;
            named = has_visible_name ? strcmp(visible_name, name) == 0
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

size_t f2c_procedure_select_explicit_interface(Context *context, Unit *caller, const char *name,
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
    if (count == 0U && caller->internal && caller->host_index < context->units.count)
        count = select_scope_interface(&context->units.items[caller->host_index], name,
                                       resolved_name, arguments, argument_count, subroutine_call,
                                       selection, matching_count);
    return count;
}

static F2cSourceSpan operation_span(const F2cSourceSpan *span, size_t line) {
    F2cSourceSpan result = {0};
    if (span != NULL && span->begin.line != 0U)
        return *span;
    result.begin.line = line;
    result.begin.column = 1U;
    result.end = result.begin;
    return result;
}

Unit *f2c_validation_generic_specific(Context *context, Unit *caller, size_t line,
                                      const char *generic_name, const F2cSourceSpan *span,
                                      F2cExpr *const *arguments, size_t argument_count,
                                      int subroutine_call, int required, int *handled) {
    Unit *definition = NULL;
    size_t matching_interfaces = 0U;
    const size_t interface_count = f2c_procedure_select_explicit_interface(
        context, caller, generic_name, arguments, argument_count, subroutine_call, &definition,
        &matching_interfaces);
    const F2cSourceSpan diagnostic = operation_span(span, line);
    if (handled != NULL)
        *handled = 0;
    if (matching_interfaces == 1U) {
        if (handled != NULL)
            *handled = 1;
        return definition;
    }
    if (matching_interfaces > 1U) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &diagnostic, 1,
                                 "generic interface '%s' is ambiguous for this operand list",
                                 generic_name);
        if (handled != NULL)
            *handled = 1;
        return NULL;
    }
    if (required) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &diagnostic, 1,
            interface_count == 0U
                ? "generic interface '%s' is not visible for this operation"
                : "generic interface '%s' has no specific procedure matching this operand list",
            generic_name);
        if (handled != NULL)
            *handled = 1;
    }
    return NULL;
}
