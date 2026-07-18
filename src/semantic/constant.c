#include "internal/f2c.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int checked_add(int64_t left, int64_t right, int64_t *result) {
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right))
        return 0;
    *result = left + right;
    return 1;
}

static int checked_subtract(int64_t left, int64_t right, int64_t *result) {
    if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right))
        return 0;
    *result = left - right;
    return 1;
}

static int checked_multiply(int64_t left, int64_t right, int64_t *result) {
    if (left == 0 || right == 0) {
        *result = 0;
        return 1;
    }
    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN))
        return 0;
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) || (right < 0 && right < INT64_MIN / left))
            return 0;
    } else if ((right > 0 && left < INT64_MIN / right) || (right < 0 && left < INT64_MAX / right)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int64_t literal_character_length(const char *text) {
    const char *quote_begin;
    char quote;
    int64_t length = 0;
    size_t i;
    size_t source_length;
    const char *payload;
    size_t payload_length;
    if (f2c_hollerith_payload(text, &payload, &payload_length))
        return payload_length <= (size_t)INT64_MAX ? (int64_t)payload_length : -1;
    quote_begin = f2c_character_literal_quote(text);
    if (quote_begin == NULL)
        return -1;
    quote = *quote_begin;
    source_length = strlen(quote_begin);
    if ((quote != '\'' && quote != '"') || source_length < 2U)
        return -1;
    for (i = 1U; i + 1U < source_length; ++i) {
        if (quote_begin[i] == quote && i + 1U < source_length - 1U && quote_begin[i + 1U] == quote)
            ++i;
        ++length;
    }
    return length;
}

typedef struct F2cConstantEvaluation {
    Unit *unit;
    Context *context;
    size_t steps;
} F2cConstantEvaluation;

static size_t evaluation_line(const F2cConstantEvaluation *evaluation) {
    if (evaluation->context != NULL && evaluation->unit != NULL &&
        evaluation->unit->begin < evaluation->context->lines.count)
        return evaluation->context->lines.items[evaluation->unit->begin].number;
    return 1U;
}

static int consume_evaluation_step(F2cConstantEvaluation *evaluation, size_t depth) {
    Context *context = evaluation->context;
    const size_t depth_limit =
        context != NULL ? context->limits.max_parse_depth : F2C_DEFAULT_MAX_PARSE_DEPTH;
    const size_t step_limit =
        context != NULL ? context->limits.max_constant_steps : F2C_DEFAULT_MAX_CONSTANT_STEPS;
    if (depth_limit != 0U && depth >= depth_limit) {
        if (context != NULL && !context->constant_depth_limit_reported) {
            context->constant_depth_limit_reported = 1;
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, evaluation_line(evaluation),
                                1, "constant-evaluation depth limit of %zu exceeded", depth_limit);
        }
        return 0;
    }
    if (step_limit != 0U &&
        (context != NULL ? context->constant_evaluation_steps : evaluation->steps) >= step_limit) {
        if (context != NULL && !context->constant_step_limit_reported) {
            context->constant_step_limit_reported = 1;
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, evaluation_line(evaluation),
                                1, "constant-evaluation step limit of %zu exceeded", step_limit);
        }
        return 0;
    }
    ++evaluation->steps;
    if (context != NULL)
        ++context->constant_evaluation_steps;
    return 1;
}

static int evaluate(F2cConstantEvaluation *evaluation, const F2cExpr *expression, int64_t *value,
                    size_t depth) {
    int64_t left;
    int64_t right;
    Unit *unit = evaluation->unit;
    if (expression == NULL || value == NULL || !consume_evaluation_step(evaluation, depth))
        return 0;
    if (expression->kind == F2C_EXPR_INTEGER_LITERAL && expression->text != NULL) {
        char *end = NULL;
        long long parsed;
        errno = 0;
        parsed = strtoll(expression->text, &end, 10);
        if (errno != 0 || end == expression->text || (*end != '\0' && *end != '_'))
            return 0;
        *value = (int64_t)parsed;
        return 1;
    }
    if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
        expression->symbol->parameter && expression->symbol->initializer != NULL) {
        F2cExpr *initializer =
            f2c_parse_expression_ast(unit, expression->symbol->initializer, NULL);
        const int result = evaluate(evaluation, initializer, value, depth + 1U);
        f2c_expr_free(initializer);
        return result;
    }
    if (expression->kind == F2C_EXPR_UNARY && expression->child_count == 1U &&
        evaluate(evaluation, expression->children[0], &left, depth + 1U)) {
        if (strcmp(expression->text, "+") == 0) {
            *value = left;
            return 1;
        }
        if (strcmp(expression->text, "-") == 0 && left != INT64_MIN) {
            *value = -left;
            return 1;
        }
        return 0;
    }
    if (expression->kind == F2C_EXPR_CALL && expression->text != NULL &&
        expression->child_count != 0U) {
        size_t i;
        if (strcmp(expression->text, "len") == 0 && expression->child_count == 1U) {
            const F2cExpr *argument = expression->children[0];
            if (argument->kind == F2C_EXPR_STRING_LITERAL) {
                left = literal_character_length(argument->text);
                if (left >= 0) {
                    *value = left;
                    return 1;
                }
            }
            if (argument->symbol != NULL && argument->symbol->character_length != NULL)
                return f2c_evaluate_integer_text(unit, argument->symbol->character_length, value);
            return 0;
        }
        if ((strcmp(expression->text, "max") == 0 || strcmp(expression->text, "min") == 0) &&
            evaluate(evaluation, expression->children[0], value, depth + 1U)) {
            for (i = 1U; i < expression->child_count; ++i) {
                if (!evaluate(evaluation, expression->children[i], &right, depth + 1U))
                    return 0;
                if ((strcmp(expression->text, "max") == 0 && right > *value) ||
                    (strcmp(expression->text, "min") == 0 && right < *value))
                    *value = right;
            }
            return 1;
        }
        return 0;
    }
    if (expression->kind != F2C_EXPR_BINARY || expression->child_count != 2U ||
        !evaluate(evaluation, expression->children[0], &left, depth + 1U) ||
        !evaluate(evaluation, expression->children[1], &right, depth + 1U))
        return 0;
    if (strcmp(expression->text, "+") == 0)
        return checked_add(left, right, value);
    if (strcmp(expression->text, "-") == 0)
        return checked_subtract(left, right, value);
    if (strcmp(expression->text, "*") == 0)
        return checked_multiply(left, right, value);
    if (strcmp(expression->text, "/") == 0) {
        if (right == 0 || (left == INT64_MIN && right == -1))
            return 0;
        *value = left / right;
        return 1;
    }
    if (strcmp(expression->text, "**") == 0 && right >= 0) {
        int64_t base = left;
        int64_t exponent = right;
        int64_t result = 1;
        while (exponent != 0) {
            if ((exponent & 1) != 0 && !checked_multiply(result, base, &result))
                return 0;
            exponent >>= 1;
            if (exponent != 0 && !checked_multiply(base, base, &base))
                return 0;
        }
        *value = result;
        return 1;
    }
    return 0;
}

int f2c_evaluate_integer_constant(Unit *unit, const F2cExpr *expression, int64_t *value) {
    F2cConstantEvaluation evaluation = {unit, unit != NULL ? unit->context : NULL, 0U};
    return evaluate(&evaluation, expression, value, 0U);
}

int f2c_evaluate_integer_text(Unit *unit, const char *text, int64_t *value) {
    const char *error_at = NULL;
    F2cExpr *expression;
    int result;
    if (text == NULL)
        return 0;
    expression = f2c_parse_expression_ast(unit, text, &error_at);
    result = expression != NULL && error_at == NULL &&
             f2c_evaluate_integer_constant(unit, expression, value);
    f2c_expr_free(expression);
    return result;
}

int f2c_expression_is_initialization_constant(const F2cExpr *expression) {
    size_t index;
    if (expression == NULL)
        return 0;
    switch (expression->kind) {
    case F2C_EXPR_INTEGER_LITERAL:
    case F2C_EXPR_REAL_LITERAL:
    case F2C_EXPR_STRING_LITERAL:
    case F2C_EXPR_LOGICAL_LITERAL:
        return 1;
    case F2C_EXPR_NAME:
        return expression->symbol != NULL && expression->symbol->parameter;
    case F2C_EXPR_CALL:
        if (expression->text == NULL || !f2c_is_intrinsic_name(expression->text))
            return 0;
        break;
    case F2C_EXPR_ARRAY_REFERENCE:
    case F2C_EXPR_SUBSTRING:
    case F2C_EXPR_COMPONENT:
        if (expression->symbol == NULL || !expression->symbol->parameter)
            return 0;
        break;
    case F2C_EXPR_UNARY:
    case F2C_EXPR_BINARY:
    case F2C_EXPR_COMPLEX_LITERAL:
    case F2C_EXPR_ARRAY_CONSTRUCTOR:
    case F2C_EXPR_KEYWORD_ARGUMENT:
    case F2C_EXPR_STRUCTURE_CONSTRUCTOR:
        break;
    default:
        return 0;
    }
    for (index = 0U; index < expression->child_count; ++index) {
        if (!f2c_expression_is_initialization_constant(expression->children[index]))
            return 0;
    }
    return 1;
}

int f2c_integer_iteration_count(int64_t first, int64_t last, int64_t step, uint64_t *count) {
    uint64_t distance;
    uint64_t magnitude;
    uint64_t quotient;
    if (count == NULL || step == 0)
        return 0;
    if ((step > 0 && first > last) || (step < 0 && first < last)) {
        *count = 0U;
        return 1;
    }
    if (step > 0) {
        distance = (uint64_t)last - (uint64_t)first;
        magnitude = (uint64_t)step;
    } else {
        distance = (uint64_t)first - (uint64_t)last;
        magnitude = 0U - (uint64_t)step;
    }
    quotient = distance / magnitude;
    if (quotient == UINT64_MAX)
        return 0;
    *count = quotient + 1U;
    return 1;
}
