#include "semantic/validation/private.h"

#include <stdlib.h>

int f2c_validation_bind_unresolved_alternate_call(Context *context, Unit *caller, const char *name,
                                                  const F2cSourceSpan *call_span,
                                                  F2cStatement *statement) {
    Symbol *external;
    F2cExpr **ordinary_arguments;
    char **ordinary_items;
    char **labels;
    F2cSourceSpan *label_spans;
    size_t ordinary_count = 0U;
    size_t alternate_count = 0U;
    size_t argument;
    size_t ordinary = 0U;
    size_t alternate = 0U;
    int valid = 1;
    if (statement == NULL)
        return 1;
    for (argument = 0U; argument < statement->item_count; ++argument) {
        F2cExpr *actual = statement->arguments != NULL ? statement->arguments[argument] : NULL;
        if (actual != NULL && actual->kind == F2C_EXPR_ALTERNATE_RETURN)
            ++alternate_count;
        else
            ++ordinary_count;
    }
    if (alternate_count == 0U)
        return 1;
    external = f2c_find_symbol(caller, name);
    if (external == NULL || !external->external || !external->external_subroutine) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, call_span, 1,
                                 "alternate-return call target '%s' is not a subroutine", name);
        return 0;
    }
    if (external->external_alternate_return_count != 0U &&
        external->external_alternate_return_count != alternate_count) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, call_span, 1,
            "external subroutine '%s' is called with inconsistent alternate-return counts (%zu "
            "and %zu)",
            name, external->external_alternate_return_count, alternate_count);
        valid = 0;
    }
    if (external->external_signature_observed &&
        external->external_parameter_count != ordinary_count) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, call_span, 1,
            "external subroutine '%s' is called with %zu ordinary arguments but its inferred "
            "interface has %zu",
            name, ordinary_count, external->external_parameter_count);
        valid = 0;
    }
    for (argument = 0U; argument < statement->item_count; ++argument) {
        F2cExpr *actual = statement->arguments != NULL ? statement->arguments[argument] : NULL;
        if (actual == NULL || actual->kind != F2C_EXPR_ALTERNATE_RETURN)
            continue;
        if (actual->text == NULL) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SYNTAX, &actual->span, 1,
                "alternate return target must be a statement label of one to five digits");
            valid = 0;
        }
    }
    if (!valid)
        return 0;
    ordinary_arguments = ordinary_count != 0U
                             ? (F2cExpr **)calloc(ordinary_count, sizeof(*ordinary_arguments))
                             : NULL;
    ordinary_items =
        ordinary_count != 0U ? (char **)calloc(ordinary_count, sizeof(*ordinary_items)) : NULL;
    labels = (char **)calloc(alternate_count, sizeof(*labels));
    label_spans = (F2cSourceSpan *)calloc(alternate_count, sizeof(*label_spans));
    if ((ordinary_count != 0U && (ordinary_arguments == NULL || ordinary_items == NULL)) ||
        labels == NULL || label_spans == NULL) {
        free(ordinary_arguments);
        free(ordinary_items);
        free(labels);
        free(label_spans);
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, call_span, 1,
                                 "out of memory binding alternate-return call to '%s'", name);
        return 0;
    }
    for (argument = 0U; argument < statement->item_count; ++argument) {
        F2cExpr *actual = statement->arguments[argument];
        if (actual->kind == F2C_EXPR_ALTERNATE_RETURN) {
            labels[alternate] = f2c_strdup(actual->text);
            if (labels[alternate] == NULL) {
                size_t copied;
                for (copied = 0U; copied < alternate; ++copied)
                    free(labels[copied]);
                free(ordinary_arguments);
                free(ordinary_items);
                free(labels);
                free(label_spans);
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, call_span, 1,
                                         "out of memory copying alternate-return target for '%s'",
                                         name);
                return 0;
            }
            label_spans[alternate++] = actual->span;
        } else {
            ordinary_arguments[ordinary] = actual;
            ordinary_items[ordinary++] = statement->items[argument];
        }
    }
    for (argument = 0U; argument < statement->item_count; ++argument) {
        F2cExpr *actual = statement->arguments[argument];
        if (actual->kind == F2C_EXPR_ALTERNATE_RETURN) {
            f2c_expr_free(actual);
            free(statement->items[argument]);
        }
    }
    free(statement->arguments);
    free(statement->items);
    statement->arguments = ordinary_arguments;
    statement->items = ordinary_items;
    statement->item_count = ordinary_count;
    statement->labels = labels;
    statement->label_spans = label_spans;
    statement->label_count = alternate_count;
    external->external_alternate_return_count = alternate_count;
    return 1;
}
