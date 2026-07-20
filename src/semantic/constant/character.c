#include "internal/f2c.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct F2cCharacterConstantEvaluation {
    Unit *unit;
    Context *context;
    size_t steps;
} F2cCharacterConstantEvaluation;

static int evaluate(F2cCharacterConstantEvaluation *evaluation, const F2cExpr *expression,
                    char **value, size_t *length, size_t depth);

static int consume_step(F2cCharacterConstantEvaluation *evaluation, size_t depth) {
    const size_t depth_limit = evaluation->context != NULL
                                   ? evaluation->context->limits.max_parse_depth
                                   : F2C_DEFAULT_MAX_PARSE_DEPTH;
    const size_t step_limit = evaluation->context != NULL
                                  ? evaluation->context->limits.max_constant_steps
                                  : F2C_DEFAULT_MAX_CONSTANT_STEPS;
    const size_t used = evaluation->context != NULL ? evaluation->context->constant_evaluation_steps
                                                    : evaluation->steps;
    if ((depth_limit != 0U && depth >= depth_limit) || (step_limit != 0U && used >= step_limit))
        return 0;
    ++evaluation->steps;
    if (evaluation->context != NULL)
        ++evaluation->context->constant_evaluation_steps;
    return 1;
}

static int allocate_result(size_t length, char **value) {
    if (value == NULL || length == SIZE_MAX)
        return 0;
    *value = (char *)malloc(length + 1U);
    if (*value == NULL)
        return 0;
    (*value)[length] = '\0';
    return 1;
}

static int evaluate_adjustment(F2cCharacterConstantEvaluation *evaluation,
                               const F2cExpr *expression, char **value, size_t *length,
                               size_t depth, int right) {
    const F2cExpr *argument =
        f2c_intrinsic_argument(expression->children, expression->child_count, "string", 0U);
    char *source = NULL;
    size_t source_length = 0U;
    size_t blanks = 0U;
    if (!evaluate(evaluation, argument, &source, &source_length, depth + 1U) ||
        !allocate_result(source_length, value)) {
        free(source);
        return 0;
    }
    if (right) {
        while (blanks < source_length && source[source_length - blanks - 1U] == ' ')
            ++blanks;
        if (source_length > blanks)
            memcpy(*value + blanks, source, source_length - blanks);
        if (blanks != 0U)
            memset(*value, ' ', blanks);
    } else {
        while (blanks < source_length && source[blanks] == ' ')
            ++blanks;
        if (source_length > blanks)
            memcpy(*value, source + blanks, source_length - blanks);
        if (blanks != 0U)
            memset(*value + source_length - blanks, ' ', blanks);
    }
    free(source);
    *length = source_length;
    return 1;
}

static int evaluate_repeat(F2cCharacterConstantEvaluation *evaluation, const F2cExpr *expression,
                           char **value, size_t *length, size_t depth) {
    const F2cExpr *string =
        f2c_intrinsic_argument(expression->children, expression->child_count, "string", 0U);
    const F2cExpr *ncopies =
        f2c_intrinsic_argument(expression->children, expression->child_count, "ncopies", 1U);
    char *source = NULL;
    size_t source_length = 0U;
    int64_t count;
    size_t result_length;
    size_t copy;
    if (!evaluate(evaluation, string, &source, &source_length, depth + 1U) ||
        !f2c_evaluate_integer_constant(evaluation->unit, ncopies, &count) || count < 0 ||
        (source_length != 0U && (uint64_t)count > (uint64_t)SIZE_MAX / source_length)) {
        free(source);
        return 0;
    }
    result_length = source_length * (size_t)count;
    if (!allocate_result(result_length, value)) {
        free(source);
        return 0;
    }
    for (copy = 0U; copy < (size_t)count; ++copy)
        if (source_length != 0U)
            memcpy(*value + copy * source_length, source, source_length);
    free(source);
    *length = result_length;
    return 1;
}

static int evaluate_trim(F2cCharacterConstantEvaluation *evaluation, const F2cExpr *expression,
                         char **value, size_t *length, size_t depth) {
    const F2cExpr *argument =
        f2c_intrinsic_argument(expression->children, expression->child_count, "string", 0U);
    char *source = NULL;
    size_t source_length = 0U;
    if (!evaluate(evaluation, argument, &source, &source_length, depth + 1U))
        return 0;
    while (source_length != 0U && source[source_length - 1U] == ' ')
        --source_length;
    if (!allocate_result(source_length, value)) {
        free(source);
        return 0;
    }
    if (source_length != 0U)
        memcpy(*value, source, source_length);
    free(source);
    *length = source_length;
    return 1;
}

static int evaluate(F2cCharacterConstantEvaluation *evaluation, const F2cExpr *expression,
                    char **value, size_t *length, size_t depth) {
    if (expression == NULL || value == NULL || length == NULL || !consume_step(evaluation, depth))
        return 0;
    if (expression->kind == F2C_EXPR_KEYWORD_ARGUMENT && expression->child_count == 1U)
        return evaluate(evaluation, expression->children[0], value, length, depth + 1U);
    if (expression->kind == F2C_EXPR_STRING_LITERAL) {
        *value = f2c_character_literal_bytes(expression->text, length);
        return *value != NULL;
    }
    if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
        expression->symbol->parameter && expression->symbol->initializer_expression != NULL)
        return evaluate(evaluation, expression->symbol->initializer_expression, value, length,
                        depth + 1U);
    if (expression->kind == F2C_EXPR_BINARY && expression->type == TYPE_CHARACTER &&
        expression->text != NULL && strcmp(expression->text, "//") == 0 &&
        expression->child_count == 2U) {
        char *left = NULL;
        char *right = NULL;
        size_t left_length = 0U;
        size_t right_length = 0U;
        if (!evaluate(evaluation, expression->children[0], &left, &left_length, depth + 1U) ||
            !evaluate(evaluation, expression->children[1], &right, &right_length, depth + 1U) ||
            left_length > SIZE_MAX - right_length ||
            !allocate_result(left_length + right_length, value)) {
            free(left);
            free(right);
            return 0;
        }
        if (left_length != 0U)
            memcpy(*value, left, left_length);
        if (right_length != 0U)
            memcpy(*value + left_length, right, right_length);
        free(left);
        free(right);
        *length = left_length + right_length;
        return 1;
    }
    if (expression->kind != F2C_EXPR_CALL)
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_CHAR ||
        expression->intrinsic == F2C_INTRINSIC_ACHAR) {
        const F2cExpr *argument =
            f2c_intrinsic_argument(expression->children, expression->child_count, "i", 0U);
        int64_t code;
        if (!f2c_evaluate_integer_constant(evaluation->unit, argument, &code) || code < 0 ||
            code > 255 || !allocate_result(1U, value))
            return 0;
        (*value)[0] = (char)(unsigned char)code;
        *length = 1U;
        return 1;
    }
    if (expression->intrinsic == F2C_INTRINSIC_ADJUSTL)
        return evaluate_adjustment(evaluation, expression, value, length, depth, 0);
    if (expression->intrinsic == F2C_INTRINSIC_ADJUSTR)
        return evaluate_adjustment(evaluation, expression, value, length, depth, 1);
    if (expression->intrinsic == F2C_INTRINSIC_REPEAT)
        return evaluate_repeat(evaluation, expression, value, length, depth);
    if (expression->intrinsic == F2C_INTRINSIC_TRIM)
        return evaluate_trim(evaluation, expression, value, length, depth);
    return 0;
}

int f2c_evaluate_character_constant(Unit *unit, const F2cExpr *expression, char **value,
                                    size_t *length) {
    F2cCharacterConstantEvaluation evaluation = {unit, unit != NULL ? unit->context : NULL, 0U};
    if (value == NULL || length == NULL)
        return 0;
    *value = NULL;
    *length = 0U;
    return evaluate(&evaluation, expression, value, length, 0U);
}
