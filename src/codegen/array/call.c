#include "codegen/array/private.h"

#include <stdio.h>
#include <stdlib.h>

static const F2cExpr *actual_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static void free_arguments(F2cExpr **arguments, size_t count) {
    size_t argument;
    if (arguments == NULL)
        return;
    for (argument = 0U; argument < count; ++argument)
        f2c_expr_free(arguments[argument]);
    free(arguments);
}

int f2c_array_emit_elemental_call(Context *context, Unit *unit, const F2cStatement *statement,
                                  int depth) {
    const Unit *procedure;
    const F2cExpr *shape_source = NULL;
    F2cExpr **arguments = NULL;
    F2cExpr **prepared_arguments = NULL;
    char **actual_extents = NULL;
    char *extents[F2C_MAX_RANK] = {0};
    char ordinal_storage[F2C_MAX_RANK][48];
    const char *ordinals[F2C_MAX_RANK] = {0};
    size_t argument;
    size_t dimension;
    size_t rank = 0U;
    size_t shape_argument = SIZE_MAX;
    size_t temporary = 0U;
    Buffer prelude = {0};
    Buffer cleanup = {0};
    int emitted_depth;
    if (context == NULL || unit == NULL || statement == NULL || statement->kind != F2C_STMT_CALL)
        return 0;
    procedure = statement->resolved_procedure;
    if (procedure == NULL || !procedure->elemental || procedure->kind != UNIT_SUBROUTINE)
        return 0;
    for (argument = 0U; argument < statement->item_count; ++argument) {
        const F2cExpr *value = actual_value(statement->arguments[argument]);
        if (value != NULL && value->kind != F2C_EXPR_ABSENT_ARGUMENT && value->rank != 0U) {
            shape_source = value;
            rank = value->rank;
            shape_argument = argument;
            break;
        }
    }
    if (shape_source == NULL)
        return 0;
    prepared_arguments =
        statement->item_count != 0U
            ? (F2cExpr **)calloc(statement->item_count, sizeof(*prepared_arguments))
            : NULL;
    if (statement->item_count != 0U && prepared_arguments == NULL)
        goto unsupported;
    for (argument = 0U; argument < statement->item_count; ++argument) {
        prepared_arguments[argument] = f2c_array_clone_expression(statement->arguments[argument]);
        if (prepared_arguments[argument] == NULL ||
            !f2c_array_materialize_constructors(context, unit, prepared_arguments[argument],
                                                statement->line, "call", &temporary, &prelude,
                                                &cleanup, depth + 1))
            goto unsupported;
    }
    shape_source = actual_value(prepared_arguments[shape_argument]);
    for (dimension = 0U; dimension < rank; ++dimension) {
        extents[dimension] = f2c_array_expression_extent(unit, shape_source, dimension);
        (void)snprintf(ordinal_storage[dimension], sizeof(ordinal_storage[dimension]),
                       "f2c_elemental_call_ordinal_%zu", dimension);
        ordinals[dimension] = ordinal_storage[dimension];
        if (extents[dimension] == NULL)
            goto unsupported;
    }
    arguments = statement->item_count != 0U
                    ? (F2cExpr **)calloc(statement->item_count, sizeof(*arguments))
                    : NULL;
    if (statement->item_count != 0U && arguments == NULL)
        goto unsupported;
    for (argument = 0U; argument < statement->item_count; ++argument) {
        const F2cExpr *source = prepared_arguments[argument];
        arguments[argument] = source != NULL && source->rank != 0U
                                  ? f2c_array_element_expression(unit, source, rank, ordinals)
                                  : f2c_array_clone_expression(source);
        if (arguments[argument] == NULL)
            goto unsupported;
    }
    if (statement->item_count != 0U && rank != 0U) {
        actual_extents = (char **)calloc(statement->item_count * rank, sizeof(*actual_extents));
        if (actual_extents == NULL)
            goto unsupported;
    }
    for (argument = 0U; argument < statement->item_count; ++argument) {
        const F2cExpr *value = actual_value(prepared_arguments[argument]);
        if (value == NULL || value->kind == F2C_EXPR_ABSENT_ARGUMENT || value->rank == 0U ||
            value == shape_source)
            continue;
        for (dimension = 0U; dimension < rank; ++dimension) {
            actual_extents[argument * rank + dimension] =
                f2c_array_expression_extent(unit, value, dimension);
            if (actual_extents[argument * rank + dimension] == NULL)
                goto unsupported;
        }
    }

    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    emitted_depth = depth + 1;
    f2c_buffer_append(&context->output, prelude.data != NULL ? prelude.data : "");
    for (dimension = 0U; dimension < rank; ++dimension) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_elemental_call_extent_%zu = (size_t)(%s);\n", dimension,
                          extents[dimension]);
    }
    for (argument = 0U; argument < statement->item_count; ++argument) {
        const F2cExpr *value = actual_value(prepared_arguments[argument]);
        if (value == NULL || value->kind == F2C_EXPR_ABSENT_ARGUMENT || value->rank == 0U ||
            value == shape_source)
            continue;
        for (dimension = 0U; dimension < rank; ++dimension) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_printf(&context->output,
                              "if ((size_t)(%s) != f2c_elemental_call_extent_%zu) abort();\n",
                              actual_extents[argument * rank + dimension], dimension);
        }
    }
    for (dimension = rank; dimension != 0U; --dimension) {
        const size_t current = dimension - 1U;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_elemental_call_ordinal_%zu = 0U; "
                          "f2c_elemental_call_ordinal_%zu < f2c_elemental_call_extent_%zu; "
                          "++f2c_elemental_call_ordinal_%zu) {\n",
                          current, current, current, current);
        ++emitted_depth;
    }
    f2c_emit_call(&context->output, unit, statement->name, arguments, statement->item_count,
                  emitted_depth);
    for (dimension = 0U; dimension < rank; ++dimension) {
        --emitted_depth;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_buffer_append(&context->output, cleanup.data != NULL ? cleanup.data : "");
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free_arguments(arguments, statement->item_count);
    free_arguments(prepared_arguments, statement->item_count);
    free(prelude.data);
    free(cleanup.data);
    for (argument = 0U; argument < statement->item_count * rank; ++argument)
        free(actual_extents[argument]);
    free(actual_extents);
    for (dimension = 0U; dimension < rank; ++dimension)
        free(extents[dimension]);
    return 1;

unsupported:
    free_arguments(arguments, statement->item_count);
    free_arguments(prepared_arguments, statement->item_count);
    free(prelude.data);
    free(cleanup.data);
    for (argument = 0U; argument < statement->item_count * rank; ++argument)
        free(actual_extents != NULL ? actual_extents[argument] : NULL);
    free(actual_extents);
    for (dimension = 0U; dimension < rank; ++dimension)
        free(extents[dimension]);
    f2c_diagnostic(context, statement->line, 1,
                   "ELEMENTAL subroutine call cannot derive a scalar element expression");
    return 1;
}
