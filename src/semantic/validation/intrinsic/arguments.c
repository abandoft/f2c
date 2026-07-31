#include "semantic/validation/intrinsic/arguments.h"

#include "semantic/validation/private.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#define F2C_INTRINSIC_DIAGNOSTIC_NAME_LIMIT 64U

static void diagnostic_name(const F2cIntrinsicDescriptor *descriptor,
                            char output[F2C_INTRINSIC_DIAGNOSTIC_NAME_LIMIT]) {
    size_t index;
    if (descriptor == NULL || descriptor->canonical_name == NULL) {
        output[0] = '\0';
        return;
    }
    for (index = 0U;
         index + 1U < F2C_INTRINSIC_DIAGNOSTIC_NAME_LIMIT &&
         descriptor->canonical_name[index] != '\0';
         ++index)
        output[index] = (char)toupper((unsigned char)descriptor->canonical_name[index]);
    output[index] = '\0';
}

static size_t argument_index(const char *const *names, size_t count, const char *name) {
    size_t index;
    for (index = 0U; index < count; ++index)
        if (strcmp(names[index], name) == 0)
            return index;
    return SIZE_MAX;
}

static F2cBoundIntrinsicArguments bind_intrinsic_arguments(
    Context *context, size_t line, const char *statement_text, const char *intrinsic_name,
    F2cExpr *const *arguments, size_t argument_count, const char *const *names,
    size_t name_count, unsigned int required_mask) {
    F2cBoundIntrinsicArguments bound = {{0}};
    size_t positional = 0U;
    size_t argument;
    int saw_keyword = 0;
    if (name_count > F2C_INTRINSIC_ARGUMENT_LIMIT ||
        (required_mask >> (unsigned int)name_count) != 0U)
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
    for (argument = 0U; argument < name_count; ++argument)
        if ((required_mask & (1U << (unsigned int)argument)) != 0U &&
            bound.values[argument] == NULL)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, NULL), 1,
                              "%s requires argument %s", intrinsic_name, names[argument]);
    return bound;
}

F2cBoundIntrinsicArguments f2c_validation_bind_registered_intrinsic_arguments(
    Context *context, size_t line, const char *statement_text, const char *intrinsic_name,
    F2cIntrinsicId intrinsic, size_t maximum_arguments, F2cExpr *const *arguments,
    size_t argument_count) {
    const F2cIntrinsicArgumentSchema *schema = f2c_intrinsic_argument_schema(intrinsic);
    size_t name_count;
    unsigned int required_mask;
    if (schema == NULL)
        return (F2cBoundIntrinsicArguments){{0}};
    name_count = schema->count;
    if (!schema->variadic && maximum_arguments < name_count)
        name_count = maximum_arguments;
    required_mask = schema->required_mask;
    if (name_count < sizeof(required_mask) * 8U)
        required_mask &= (1U << (unsigned int)name_count) - 1U;
    return bind_intrinsic_arguments(context, line, statement_text, intrinsic_name, arguments,
                                    argument_count, schema->names, name_count, required_mask);
}

F2cBoundIntrinsicArguments f2c_validation_bind_intrinsic_expression(
    Context *context, size_t line, const char *statement_text, F2cExpr *expression) {
    const F2cIntrinsicSignature *signature =
        expression != NULL ? f2c_find_intrinsic(expression->text) : NULL;
    const F2cIntrinsicDescriptor *descriptor =
        expression != NULL ? f2c_intrinsic_descriptor(expression->intrinsic) : NULL;
    char name[F2C_INTRINSIC_DIAGNOSTIC_NAME_LIMIT];
    if (expression == NULL || signature == NULL || descriptor == NULL)
        return (F2cBoundIntrinsicArguments){{0}};
    diagnostic_name(descriptor, name);
    return f2c_validation_bind_registered_intrinsic_arguments(
        context, line, statement_text, name, expression->intrinsic, signature->maximum_arguments,
        expression->children, expression->child_count);
}

F2cBoundIntrinsicArguments f2c_validation_bind_intrinsic_statement(
    Context *context, F2cStatement *statement) {
    const F2cIntrinsicDescriptor *descriptor =
        statement != NULL ? f2c_intrinsic_descriptor(statement->intrinsic) : NULL;
    const F2cIntrinsicArgumentSchema *schema =
        statement != NULL ? f2c_intrinsic_argument_schema(statement->intrinsic) : NULL;
    char name[F2C_INTRINSIC_DIAGNOSTIC_NAME_LIMIT];
    if (statement == NULL || descriptor == NULL || schema == NULL)
        return (F2cBoundIntrinsicArguments){{0}};
    diagnostic_name(descriptor, name);
    return f2c_validation_bind_registered_intrinsic_arguments(
        context, statement->line, statement->text, name, statement->intrinsic, schema->count,
        statement->arguments, statement->item_count);
}

#undef F2C_INTRINSIC_DIAGNOSTIC_NAME_LIMIT
