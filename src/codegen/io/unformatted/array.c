#include "codegen/io/unformatted/private.h"

#include "codegen/array/private.h"

#include <stdio.h>
#include <stdlib.h>

static void free_extents(char **extents, size_t rank) {
    size_t dimension;
    for (dimension = 0U; dimension < rank; ++dimension)
        free(extents[dimension]);
}

int f2c_io_emit_unformatted_array(Context *context, Unit *unit, const F2cIoItem *item, int input,
                                  const char *stream, const char *unit_number, const char *status,
                                  int depth) {
    char *extents[F2C_MAX_RANK] = {0};
    char ordinal_storage[F2C_MAX_RANK][64];
    const char *ordinals[F2C_MAX_RANK] = {0};
    F2cExpr *prepared = NULL;
    F2cExpr *element = NULL;
    char *value = NULL;
    Buffer prelude = {0};
    Buffer cleanup = {0};
    size_t temporary = 0U;
    size_t dimension;
    int emitted_depth;
    int result = 0;
    if (context == NULL || unit == NULL || item == NULL || item->expression == NULL ||
        item->expression->rank == 0U || item->expression->rank > F2C_MAX_RANK || stream == NULL ||
        unit_number == NULL || status == NULL)
        return 0;
    prepared = f2c_array_clone_expression(item->expression);
    if (prepared == NULL ||
        !f2c_array_hoist_scalar_subexpressions(unit, prepared, item->expression->span.begin.line,
                                               "unformatted", &temporary, &prelude, depth + 1, 1) ||
        !f2c_array_materialize_constructors(context, unit, prepared,
                                            item->expression->span.begin.line, "unformatted",
                                            &temporary, &prelude, &cleanup, depth + 1))
        goto cleanup;
    for (dimension = 0U; dimension < prepared->rank; ++dimension) {
        extents[dimension] = f2c_array_expression_extent(unit, prepared, dimension);
        (void)snprintf(ordinal_storage[dimension], sizeof(ordinal_storage[dimension]),
                       "f2c_unformatted_ordinal_%zu", dimension + 1U);
        ordinals[dimension] = ordinal_storage[dimension];
        if (extents[dimension] == NULL)
            goto cleanup;
    }
    element = f2c_array_element_expression(unit, prepared, prepared->rank, ordinals);
    value = element != NULL ? f2c_array_emit_expression(unit, element) : NULL;
    if (element == NULL || value == NULL || (input && !element->definable))
        goto cleanup;

    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (prelude.data != NULL)
        f2c_buffer_append(&context->output, prelude.data);
    emitted_depth = depth + 1;
    for (dimension = 0U; dimension < prepared->rank; ++dimension) {
        f2c_io_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_unformatted_extent_%zu = (size_t)(%s);\n",
                          dimension + 1U, extents[dimension]);
    }
    for (dimension = prepared->rank; dimension != 0U; --dimension) {
        const size_t current = dimension - 1U;
        f2c_io_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "for (size_t %s = 0U; %s < f2c_unformatted_extent_%zu; ++%s) {\n",
                          ordinals[current], ordinals[current], current + 1U, ordinals[current]);
        ++emitted_depth;
    }
    if (input && element->symbol != NULL && element->symbol->equivalence_unaligned) {
        F2cIoItem element_item = {0};
        F2cIoItem lowered_item;
        F2cExpr lowered_expression;
        element_item.expression = element;
        if (!f2c_io_begin_unaligned_input(context, unit, &element_item, emitted_depth,
                                          &lowered_item, &lowered_expression))
            goto cleanup;
        f2c_io_emit_unformatted_scalar(context, unit, &lowered_expression, "f2c_unaligned_io_value",
                                       input, stream, status, emitted_depth + 1);
        f2c_io_end_unaligned_input(context, element->symbol, emitted_depth);
    } else if (element->type == TYPE_DERIVED && element->derived_type != NULL) {
        f2c_io_emit_unformatted_derived_scalar(context, unit, element->derived_type, value, input,
                                               stream, unit_number, status, emitted_depth);
    } else {
        f2c_io_emit_unformatted_scalar(context, unit, element, value, input, stream, status,
                                       emitted_depth);
    }
    for (dimension = 0U; dimension < prepared->rank; ++dimension) {
        --emitted_depth;
        f2c_io_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    if (cleanup.data != NULL)
        f2c_buffer_append(&context->output, cleanup.data);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    if (!result)
        f2c_diagnostic(context, item->expression->span.begin.line, 1,
                       "unformatted array item could not be lowered to scalar element order");
    free_extents(extents, item->expression->rank);
    f2c_expr_free(prepared);
    f2c_expr_free(element);
    free(value);
    free(prelude.data);
    free(cleanup.data);
    return result;
}
