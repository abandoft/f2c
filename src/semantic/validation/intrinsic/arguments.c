#include "semantic/validation/intrinsic/arguments.h"

#include "semantic/validation/private.h"

#include <stdint.h>
#include <string.h>

static size_t argument_index(const char *const *names, size_t count, const char *name) {
    size_t index;
    for (index = 0U; index < count; ++index)
        if (strcmp(names[index], name) == 0)
            return index;
    return SIZE_MAX;
}

F2cBoundIntrinsicArguments f2c_validation_bind_intrinsic_arguments(
    Context *context, size_t line, const char *statement_text, const char *intrinsic_name,
    F2cExpr *const *arguments, size_t argument_count, const char *const *names,
    size_t name_count, size_t required_count) {
    F2cBoundIntrinsicArguments bound = {{0}};
    size_t positional = 0U;
    size_t argument;
    int saw_keyword = 0;
    if (name_count > F2C_INTRINSIC_ARGUMENT_LIMIT || required_count > name_count)
        return bound;
    for (argument = 0U; argument < argument_count; ++argument) {
        const F2cExpr *actual = arguments[argument];
        size_t index;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            saw_keyword = 1;
            index =
                actual->text != NULL ? argument_index(names, name_count, actual->text) : SIZE_MAX;
            if (index == SIZE_MAX) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "%s has no argument named '%s'", intrinsic_name,
                                  actual->text != NULL ? actual->text : "");
                continue;
            }
        } else {
            if (saw_keyword)
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "positional argument in %s cannot follow a keyword argument",
                                  intrinsic_name);
            index = positional++;
            if (index >= name_count)
                continue;
        }
        if (bound.values[index] != NULL) {
            f2c_diagnostic_at(
                context, line, f2c_validation_expression_start_column(statement_text, actual), 1,
                "%s argument '%s' is specified more than once", intrinsic_name, names[index]);
            continue;
        }
        bound.values[index] = f2c_validation_actual_value(actual);
    }
    for (argument = 0U; argument < required_count; ++argument)
        if (bound.values[argument] == NULL)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, NULL), 1,
                              "%s requires argument %s", intrinsic_name, names[argument]);
    return bound;
}
