#include "semantic/validation/private.h"

#include <stdlib.h>
#include <string.h>

static F2cSourceSpan diagnostic_span(const F2cSourceSpan *span, size_t line) {
    F2cSourceSpan result = {0};
    if (span != NULL && span->begin.line != 0U)
        return *span;
    result.begin.line = line;
    result.begin.column = 1U;
    result.end = result.begin;
    return result;
}

int f2c_validation_bind_procedure_arguments(Context *context, Unit *definition, size_t line,
                                            const char *statement_text, const char *name,
                                            const F2cSourceSpan *call_span, F2cExpr ***arguments_io,
                                            char ***items_io, size_t *argument_count_io,
                                            F2cStatement *call_statement, size_t implicit_parameter,
                                            F2cExpr *passed_object) {
    F2cExpr **ordered_arguments = NULL;
    char **ordered_items = NULL;
    unsigned char *assigned = NULL;
    unsigned char *created = NULL;
    size_t *alternate_actuals = NULL;
    F2cExpr **arguments = arguments_io != NULL ? *arguments_io : NULL;
    char **items = items_io != NULL ? *items_io : NULL;
    const size_t argument_count = argument_count_io != NULL ? *argument_count_io : 0U;
    const size_t dummy_count = f2c_procedure_dummy_count(definition);
    size_t next_positional = 0U;
    size_t i;
    int saw_keyword = 0;
    int valid = 1;
    const F2cSourceSpan call_diagnostic_span = diagnostic_span(call_span, line);
    if (definition->argument_count != 0U) {
        ordered_arguments =
            (F2cExpr **)calloc(definition->argument_count, sizeof(*ordered_arguments));
        created = (unsigned char *)calloc(definition->argument_count, sizeof(*created));
        if (items_io != NULL)
            ordered_items = (char **)calloc(definition->argument_count, sizeof(*ordered_items));
    }
    if (dummy_count != 0U)
        assigned = (unsigned char *)calloc(dummy_count, sizeof(*assigned));
    if (definition->alternate_return_count != 0U) {
        alternate_actuals =
            (size_t *)malloc(definition->alternate_return_count * sizeof(*alternate_actuals));
        if (alternate_actuals != NULL)
            for (i = 0U; i < definition->alternate_return_count; ++i)
                alternate_actuals[i] = SIZE_MAX;
    }
    if ((definition->argument_count != 0U && (ordered_arguments == NULL || created == NULL ||
                                              (items_io != NULL && ordered_items == NULL))) ||
        (dummy_count != 0U && assigned == NULL) ||
        (definition->alternate_return_count != 0U && alternate_actuals == NULL)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span, 1,
                                 "out of memory binding %zu dummy arguments to procedure '%s'",
                                 dummy_count, name);
        goto failed;
    }
    if (implicit_parameter != SIZE_MAX) {
        const size_t slot = f2c_procedure_dummy_slot(definition, implicit_parameter);
        if (passed_object == NULL || implicit_parameter >= definition->argument_count ||
            slot >= dummy_count) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, &call_diagnostic_span, 1,
                                     "invalid implicit passed-object argument");
            goto failed;
        }
        assigned[slot] = 1U;
        ordered_arguments[implicit_parameter] = passed_object;
    }
    for (i = 0U; i < argument_count; ++i) {
        F2cExpr *actual = arguments != NULL ? arguments[i] : NULL;
        const F2cSourceSpan actual_diagnostic_span =
            diagnostic_span(actual != NULL ? &actual->span : NULL, line);
        size_t target = SIZE_MAX;
        size_t argument_index;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            size_t dummy_index;
            saw_keyword = 1;
            for (dummy_index = 0U; dummy_index < definition->argument_count; ++dummy_index) {
                if (actual->text != NULL &&
                    strcmp(actual->text, definition->arguments[dummy_index]) == 0) {
                    target = f2c_procedure_dummy_slot(definition, dummy_index);
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
            while (next_positional < dummy_count && assigned[next_positional])
                ++next_positional;
            target = next_positional++;
        }
        if (target >= dummy_count) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &actual_diagnostic_span, 1,
                                     "procedure '%s' has more actual than dummy arguments", name);
            valid = 0;
            continue;
        }
        argument_index = f2c_procedure_dummy_argument_index(definition, target);
        if (assigned[target]) {
            if (argument_index == SIZE_MAX)
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &actual_diagnostic_span,
                                         1,
                                         "alternate-return dummy argument %zu is associated more "
                                         "than once in call to procedure '%s'",
                                         target + 1U, name);
            else
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &actual_diagnostic_span, 1,
                    "dummy argument '%s' is associated more than once in call to procedure '%s'",
                    definition->arguments[argument_index], name);
            valid = 0;
            continue;
        }
        assigned[target] = 1U;
        if (argument_index == SIZE_MAX) {
            const size_t alternate = f2c_procedure_alternate_return_index(definition, target);
            if (actual == NULL || actual->kind != F2C_EXPR_ALTERNATE_RETURN) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &actual_diagnostic_span, 1,
                    "alternate-return dummy argument %zu of procedure '%s' requires a *label "
                    "actual specifier",
                    target + 1U, name);
                valid = 0;
            } else if (actual->text == NULL) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SYNTAX, &actual_diagnostic_span, 1,
                    "alternate return target must be a statement label of one to five digits");
                valid = 0;
            } else if (alternate >= definition->alternate_return_count) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_INTERNAL, &actual_diagnostic_span, 1,
                    "invalid alternate-return dummy layout for procedure '%s'", name);
                valid = 0;
            } else {
                alternate_actuals[alternate] = i;
            }
            continue;
        }
        if (actual != NULL && actual->kind == F2C_EXPR_ALTERNATE_RETURN) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &actual_diagnostic_span, 1,
                "ordinary dummy argument '%s' of procedure '%s' cannot be associated with an "
                "alternate-return specifier",
                definition->arguments[argument_index], name);
            valid = 0;
            continue;
        }
        ordered_arguments[argument_index] = actual;
        if (items != NULL)
            ordered_items[argument_index] = items[i];
        if (actual != NULL && actual->kind == F2C_EXPR_ABSENT_ARGUMENT) {
            Symbol *dummy = f2c_find_symbol(definition, definition->arguments[argument_index]);
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
                    definition->arguments[argument_index], name);
                valid = 0;
            } else {
                actual->type = dummy->type;
                actual->rank = dummy->rank;
            }
        }
    }
    for (i = 0U; i < dummy_count; ++i) {
        const size_t argument_index = f2c_procedure_dummy_argument_index(definition, i);
        Symbol *dummy;
        if (assigned[i])
            continue;
        if (argument_index == SIZE_MAX) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
                "alternate-return dummy argument %zu of procedure '%s' has no *label actual "
                "specifier",
                i + 1U, name);
            valid = 0;
            continue;
        }
        dummy = f2c_find_symbol(definition, definition->arguments[argument_index]);
        if (dummy == NULL || !dummy->optional) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &call_diagnostic_span, 1,
                "required dummy argument '%s' of procedure '%s' has no actual argument",
                definition->arguments[argument_index], name);
            valid = 0;
            continue;
        }
        ordered_arguments[argument_index] = f2c_expr_new_absent(dummy->type, dummy->rank);
        if (ordered_arguments[argument_index] == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span,
                                     1, "out of memory while binding call to procedure '%s'", name);
            valid = 0;
            continue;
        }
        created[argument_index] = 1U;
        if (items != NULL) {
            ordered_items[argument_index] = f2c_strdup("");
            if (ordered_items[argument_index] == NULL) {
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
    if (definition->alternate_return_count == 0U && argument_count == definition->argument_count) {
        int identity_order = 1;
        for (i = 0U; i < definition->argument_count; ++i) {
            if (ordered_arguments[i] != arguments[i] ||
                (items_io != NULL && ordered_items[i] != items[i]) || created[i]) {
                identity_order = 0;
                break;
            }
        }
        if (identity_order) {
            free(ordered_arguments);
            free(ordered_items);
            free(assigned);
            free(created);
            free(alternate_actuals);
            return 1;
        }
    }
    if (definition->alternate_return_count != 0U) {
        if (call_statement == NULL || call_statement->labels != NULL ||
            call_statement->label_count != 0U) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, &call_diagnostic_span, 1,
                                     "alternate-return binding requires an unbound CALL statement");
            goto failed;
        }
        call_statement->labels =
            (char **)calloc(definition->alternate_return_count, sizeof(*call_statement->labels));
        call_statement->label_spans = (F2cSourceSpan *)calloc(definition->alternate_return_count,
                                                              sizeof(*call_statement->label_spans));
        if (call_statement->labels == NULL || call_statement->label_spans == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span,
                                     1, "out of memory binding alternate-return targets for '%s'",
                                     name);
            goto failed;
        }
        for (i = 0U; i < definition->alternate_return_count; ++i) {
            F2cExpr *actual =
                alternate_actuals[i] != SIZE_MAX ? arguments[alternate_actuals[i]] : NULL;
            call_statement->labels[i] =
                actual != NULL && actual->text != NULL ? f2c_strdup(actual->text) : NULL;
            if (call_statement->labels[i] == NULL) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &call_diagnostic_span, 1,
                    "out of memory copying alternate-return target for '%s'", name);
                goto failed;
            }
            call_statement->label_spans[i] = actual->span;
            ++call_statement->label_count;
        }
    }
    for (i = 0U; i < argument_count; ++i) {
        if (arguments[i] == NULL || arguments[i]->kind != F2C_EXPR_ALTERNATE_RETURN)
            continue;
        f2c_expr_free(arguments[i]);
        if (items != NULL)
            free(items[i]);
    }
    free(arguments);
    free(items);
    *arguments_io = ordered_arguments;
    if (items_io != NULL)
        *items_io = ordered_items;
    *argument_count_io = definition->argument_count;
    ordered_arguments = NULL;
    ordered_items = NULL;
    free(assigned);
    free(created);
    free(alternate_actuals);
    return 1;

failed:
    if (created != NULL) {
        for (i = 0U; i < definition->argument_count; ++i) {
            if (created[i]) {
                f2c_expr_free(ordered_arguments[i]);
                if (ordered_items != NULL)
                    free(ordered_items[i]);
            }
        }
    }
    free(ordered_arguments);
    free(ordered_items);
    free(assigned);
    free(created);
    free(alternate_actuals);
    return 0;
}
