#include "semantic/constant/array.h"
#include "semantic/constant/private.h"
#include "semantic/constant/transform/private.h"

#include "internal/f2c.h"

#include <string.h>

static int specification_integer(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                 F2cTokenRange syntax, int64_t *value, size_t depth) {
    F2cExpr *temporary = NULL;
    if (expression == NULL && syntax.count != 0U) {
        temporary = f2c_parse_expression_tokens(evaluation->unit, syntax.tokens, syntax.count,
                                                syntax.source, NULL);
        expression = temporary;
    }
    const int success = expression != NULL && expression->rank == 0U &&
                        expression->type == TYPE_INTEGER &&
                        f2c_constant_evaluate_integer(evaluation, expression, value, depth);
    f2c_expr_free(temporary);
    return success;
}

/* Specification expressions must retain this evaluation's parameter cache and
 * recursion depth. Starting a fresh public evaluation here hides cycles through
 * a parameter array's bounds or character length. */
int f2c_constant_storage_shape(F2cConstantEvaluation *evaluation, const Symbol *symbol,
                               F2cShape *shape, size_t *character_length, size_t depth) {
    memset(shape, 0, sizeof(*shape));
    *character_length = 0U;
    if (symbol->rank > F2C_MAX_RANK)
        return 0;
    shape->kind = symbol->rank == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_EXPLICIT;
    shape->rank = symbol->rank;
    for (size_t axis = 0U; axis < symbol->rank; ++axis) {
        const Dimension *dimension = &symbol->dimensions[axis];
        int64_t lower = 1, upper;
        if (dimension->kind != F2C_DIMENSION_EXPLICIT ||
            ((dimension->lower_expression != NULL ||
              symbol->dimension_lower_syntax[axis].count != 0U) &&
             !specification_integer(evaluation, dimension->lower_expression,
                                    symbol->dimension_lower_syntax[axis], &lower, depth + 1U)) ||
            !specification_integer(evaluation, dimension->upper_expression,
                                   symbol->dimension_upper_syntax[axis], &upper, depth + 1U))
            return 0;
        const uint64_t distance = upper >= lower ? (uint64_t)upper - (uint64_t)lower : 0U;
        if (upper >= lower && distance == UINT64_MAX)
            return 0;
        shape->dimensions[axis] = (F2cShapeDimension){F2C_DIMENSION_EXPLICIT, 1, 1, lower,
                                                      upper >= lower ? distance + 1U : 0U};
    }
    if (symbol->type == TYPE_CHARACTER) {
        int64_t length = 1;
        if (symbol->character_length_expression != NULL ||
            symbol->character_length_syntax.count != 0U) {
            if (!specification_integer(evaluation, symbol->character_length_expression,
                                       symbol->character_length_syntax, &length, depth + 1U))
                return 0;
        } else if (symbol->character_length != NULL && strcmp(symbol->character_length, "1") != 0) {
            return 0;
        }
        if (length < 0)
            length = 0;
        if ((uint64_t)length >= SIZE_MAX)
            return 0;
        *character_length = (size_t)length;
    }
    return 1;
}

int f2c_evaluate_constant_storage_layout(Unit *unit, const Symbol *symbol, F2cShape *shape,
                                         size_t *character_length) {
    F2cConstantEvaluation evaluation = {.unit = unit,
                                        .context = unit != NULL ? unit->context : NULL};
    const int success =
        symbol != NULL && shape != NULL && character_length != NULL &&
        f2c_constant_storage_shape(&evaluation, symbol, shape, character_length, 0U);
    f2c_constant_evaluation_finish(&evaluation);
    return success;
}
