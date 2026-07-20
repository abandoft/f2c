#include "internal/f2c.h"
#include "semantic/constant/private.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
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

static int logical_literal_value(const char *text, int64_t *value) {
    const char *cursor = text;
    const char *word;
    size_t length;
    size_t index;
    while (cursor != NULL && isspace((unsigned char)*cursor))
        ++cursor;
    if (cursor == NULL || *cursor != '.')
        return 0;
    ++cursor;
    if (tolower((unsigned char)*cursor) == 't') {
        word = "true";
        length = 4U;
        *value = 1;
    } else {
        word = "false";
        length = 5U;
        *value = 0;
    }
    for (index = 0U; index < length; ++index)
        if (cursor[index] == '\0' || tolower((unsigned char)cursor[index]) != word[index])
            return 0;
    cursor += length;
    if (*cursor++ != '.')
        return 0;
    return *cursor == '\0' || *cursor == '_' || isspace((unsigned char)*cursor);
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

static size_t evaluation_line(const F2cConstantEvaluation *evaluation) {
    if (evaluation->context != NULL && evaluation->unit != NULL &&
        evaluation->unit->begin < evaluation->context->lines.count)
        return evaluation->context->lines.items[evaluation->unit->begin].number;
    return 1U;
}

int f2c_constant_consume_step(F2cConstantEvaluation *evaluation, size_t depth) {
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

static int evaluate_bit_intrinsic(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  int64_t *value, size_t depth) {
    static const char *const unary[] = {"i"};
    static const char *const binary[] = {"i", "j"};
    static const char *const position[] = {"i", "pos"};
    static const char *const bits[] = {"i", "pos", "len"};
    static const char *const shift[] = {"i", "shift"};
    static const char *const circular[] = {"i", "shift", "size"};
    const char *const *names = unary;
    size_t count = 1U;
    size_t minimum = 1U;
    const F2cExpr *model;
    int64_t arguments[3] = {0, 0, 0};
    int integer_kind;
    size_t index;
    if (expression == NULL || expression->rank != 0U)
        return 0;
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_IAND:
    case F2C_INTRINSIC_IEOR:
    case F2C_INTRINSIC_IOR:
        names = binary;
        count = 2U;
        minimum = 2U;
        break;
    case F2C_INTRINSIC_BTEST:
    case F2C_INTRINSIC_IBCLR:
    case F2C_INTRINSIC_IBSET:
        names = position;
        count = 2U;
        minimum = 2U;
        break;
    case F2C_INTRINSIC_IBITS:
        names = bits;
        count = 3U;
        minimum = 3U;
        break;
    case F2C_INTRINSIC_ISHFT:
        names = shift;
        count = 2U;
        minimum = 2U;
        break;
    case F2C_INTRINSIC_ISHFTC:
        names = circular;
        count = 3U;
        minimum = 2U;
        break;
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_NOT:
        break;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_MVBITS:
    default:
        return 0;
    }
    model = f2c_intrinsic_argument(expression->children, expression->child_count, "i", 0U);
    if (model == NULL || model->type != TYPE_INTEGER)
        return 0;
    integer_kind = model->type_kind != 0 ? model->type_kind : f2c_default_kind(TYPE_INTEGER);
    if (expression->intrinsic == F2C_INTRINSIC_BIT_SIZE)
        return f2c_constant_fold_bit_intrinsic(expression->intrinsic, integer_kind, NULL, 0U,
                                               value);
    for (index = 0U; index < count; ++index) {
        const F2cExpr *argument = f2c_intrinsic_argument(
            expression->children, expression->child_count, names[index], index);
        if (argument == NULL) {
            if (index < minimum)
                return 0;
            count = index;
            break;
        }
        if (argument->type != TYPE_INTEGER || argument->rank != 0U ||
            !f2c_constant_evaluate_integer(evaluation, argument, &arguments[index], depth + 1U))
            return 0;
    }
    return f2c_constant_fold_bit_intrinsic(expression->intrinsic, integer_kind, arguments, count,
                                           value);
}

static int evaluate_numeric_model_intrinsic(F2cConstantEvaluation *evaluation,
                                            const F2cExpr *expression, int64_t *value,
                                            size_t depth) {
    static const char *const selected_real_arguments[] = {"p", "r", "radix"};
    const F2cExpr *model;
    int64_t arguments[3] = {0, 0, 0};
    unsigned int present = 0U;
    int model_kind;
    size_t argument;
    if (expression == NULL || expression->rank != 0U ||
        !f2c_intrinsic_is_numeric_model(expression->intrinsic))
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_SELECTED_INT_KIND) {
        model = f2c_intrinsic_argument(expression->children, expression->child_count, "r", 0U);
        if (model == NULL ||
            !f2c_constant_evaluate_integer(evaluation, model, &arguments[0], depth + 1U))
            return 0;
        present = 1U;
        return f2c_constant_fold_numeric_model(expression->intrinsic, TYPE_UNKNOWN, 0, arguments,
                                               present, value);
    }
    if (expression->intrinsic == F2C_INTRINSIC_SELECTED_REAL_KIND) {
        for (argument = 0U; argument < 3U; ++argument) {
            model = f2c_intrinsic_argument(expression->children, expression->child_count,
                                           selected_real_arguments[argument], argument);
            if (model == NULL)
                continue;
            if (!f2c_constant_evaluate_integer(evaluation, model, &arguments[argument], depth + 1U))
                return 0;
            present |= 1U << argument;
        }
        return f2c_constant_fold_numeric_model(expression->intrinsic, TYPE_UNKNOWN, 0, arguments,
                                               present, value);
    }
    model = f2c_intrinsic_argument(expression->children, expression->child_count, "x", 0U);
    if (model == NULL)
        return 0;
    model_kind = model->type_kind != 0 ? model->type_kind : f2c_default_kind(model->type);
    return f2c_constant_fold_numeric_model(expression->intrinsic, model->type, model_kind, NULL, 0U,
                                           value);
}

static int evaluate_exponent_intrinsic(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                       int64_t *value, size_t depth) {
    const F2cExpr *argument;
    double real_value;
    int exponent = 0;
    if (expression == NULL || expression->intrinsic != F2C_INTRINSIC_EXPONENT ||
        expression->rank != 0U)
        return 0;
    argument = f2c_intrinsic_argument(expression->children, expression->child_count, "x", 0U);
    if (argument == NULL ||
        !f2c_constant_evaluate_real(evaluation, argument, &real_value, depth + 1U))
        return 0;
    if (!isfinite(real_value)) {
        *value = INT32_MAX;
        return 1;
    }
    (void)frexp(real_value, &exponent);
    *value = exponent;
    return 1;
}

static int character_integer_result(const F2cExpr *expression, uint64_t result, int64_t *value) {
    const int kind =
        expression->type_kind != 0 ? expression->type_kind : f2c_default_kind(TYPE_INTEGER);
    const uint64_t maximum = kind == 1   ? UINT64_C(127)
                             : kind == 2 ? UINT64_C(32767)
                             : kind == 4 ? UINT64_C(2147483647)
                             : kind == 8 ? (uint64_t)INT64_MAX
                                         : UINT64_C(0);
    if (maximum == 0U || result > maximum)
        return 0;
    *value = (int64_t)result;
    return 1;
}

static int evaluate_character_intrinsic(F2cConstantEvaluation *evaluation,
                                        const F2cExpr *expression, int64_t *value, size_t depth) {
    const F2cExpr *first;
    const F2cExpr *second;
    const F2cExpr *back;
    char *left = NULL;
    char *right = NULL;
    size_t left_length = 0U;
    size_t right_length = 0U;
    size_t position = 0U;
    int64_t backwards = 0;
    int found = 0;
    if (expression == NULL || expression->rank != 0U ||
        !f2c_intrinsic_is_character(expression->intrinsic))
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_IACHAR ||
        expression->intrinsic == F2C_INTRINSIC_ICHAR) {
        first = f2c_intrinsic_argument(expression->children, expression->child_count, "c", 0U);
        if (!f2c_evaluate_character_constant(evaluation->unit, first, &left, &left_length) ||
            left_length != 1U) {
            free(left);
            return 0;
        }
        position = (unsigned char)left[0];
        free(left);
        return character_integer_result(expression, position, value);
    }
    if (expression->intrinsic == F2C_INTRINSIC_LEN ||
        expression->intrinsic == F2C_INTRINSIC_LEN_TRIM) {
        first = f2c_intrinsic_argument(expression->children, expression->child_count, "string", 0U);
        if (!f2c_evaluate_character_constant(evaluation->unit, first, &left, &left_length))
            return 0;
        if (expression->intrinsic == F2C_INTRINSIC_LEN_TRIM)
            while (left_length != 0U && left[left_length - 1U] == ' ')
                --left_length;
        free(left);
        return character_integer_result(expression, left_length, value);
    }
    if (expression->intrinsic != F2C_INTRINSIC_INDEX &&
        expression->intrinsic != F2C_INTRINSIC_SCAN &&
        expression->intrinsic != F2C_INTRINSIC_VERIFY)
        return 0;
    first = f2c_intrinsic_argument(expression->children, expression->child_count, "string", 0U);
    second = f2c_intrinsic_argument(
        expression->children, expression->child_count,
        expression->intrinsic == F2C_INTRINSIC_INDEX ? "substring" : "set", 1U);
    back = f2c_intrinsic_argument(expression->children, expression->child_count, "back", 2U);
    if (!f2c_evaluate_character_constant(evaluation->unit, first, &left, &left_length) ||
        !f2c_evaluate_character_constant(evaluation->unit, second, &right, &right_length) ||
        (back != NULL && !f2c_constant_evaluate_integer(evaluation, back, &backwards, depth + 1U)))
        goto cleanup;
    if (expression->intrinsic == F2C_INTRINSIC_INDEX) {
        if (right_length == 0U) {
            position = backwards != 0 ? left_length + 1U : 1U;
            found = 1;
        } else if (right_length <= left_length) {
            if (backwards != 0) {
                position = left_length - right_length;
                for (;;) {
                    if (memcmp(left + position, right, right_length) == 0) {
                        ++position;
                        found = 1;
                        break;
                    }
                    if (position == 0U)
                        break;
                    --position;
                }
            } else {
                for (position = 0U; position <= left_length - right_length; ++position)
                    if (memcmp(left + position, right, right_length) == 0) {
                        ++position;
                        found = 1;
                        break;
                    }
            }
        }
    } else if (backwards != 0) {
        position = left_length;
        while (position != 0U) {
            const int member =
                memchr(right, (unsigned char)left[position - 1U], right_length) != NULL;
            if ((expression->intrinsic == F2C_INTRINSIC_SCAN && member) ||
                (expression->intrinsic == F2C_INTRINSIC_VERIFY && !member)) {
                found = 1;
                break;
            }
            --position;
        }
    } else {
        for (position = 0U; position < left_length; ++position) {
            const int member = memchr(right, (unsigned char)left[position], right_length) != NULL;
            if ((expression->intrinsic == F2C_INTRINSIC_SCAN && member) ||
                (expression->intrinsic == F2C_INTRINSIC_VERIFY && !member)) {
                ++position;
                found = 1;
                break;
            }
        }
    }
    free(left);
    free(right);
    return character_integer_result(expression, found ? position : 0U, value);

cleanup:
    free(left);
    free(right);
    return 0;
}

int f2c_constant_evaluate_integer(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  int64_t *value, size_t depth) {
    int64_t left;
    int64_t right;
    Unit *unit = evaluation->unit;
    if (expression == NULL || value == NULL || !f2c_constant_consume_step(evaluation, depth))
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
    if (expression->kind == F2C_EXPR_LOGICAL_LITERAL && expression->text != NULL) {
        return logical_literal_value(expression->text, value);
    }
    if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
        expression->symbol->parameter && expression->symbol->initializer != NULL) {
        F2cExpr *temporary = NULL;
        const F2cExpr *initializer = expression->symbol->initializer_expression;
        if (initializer == NULL && expression->symbol->initializer_syntax.count != 0U) {
            temporary =
                f2c_parse_expression_tokens(unit, expression->symbol->initializer_syntax.tokens,
                                            expression->symbol->initializer_syntax.count,
                                            expression->symbol->initializer_syntax.source, NULL);
            initializer = temporary;
        }
        const int result =
            f2c_constant_evaluate_integer(evaluation, initializer, value, depth + 1U);
        f2c_expr_free(temporary);
        return result;
    }
    if (expression->kind == F2C_EXPR_UNARY && expression->child_count == 1U &&
        f2c_constant_evaluate_integer(evaluation, expression->children[0], &left, depth + 1U)) {
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
        if (evaluate_exponent_intrinsic(evaluation, expression, value, depth))
            return 1;
        if (evaluate_numeric_model_intrinsic(evaluation, expression, value, depth))
            return 1;
        if (expression->intrinsic != F2C_INTRINSIC_NONE &&
            evaluate_bit_intrinsic(evaluation, expression, value, depth))
            return 1;
        if (evaluate_character_intrinsic(evaluation, expression, value, depth))
            return 1;
        if (strcmp(expression->text, "len") == 0 && expression->child_count == 1U) {
            const F2cExpr *argument = expression->children[0];
            if (argument->kind == F2C_EXPR_STRING_LITERAL) {
                left = literal_character_length(argument->text);
                if (left >= 0) {
                    *value = left;
                    return 1;
                }
            }
            if (argument->symbol != NULL) {
                F2cExpr *temporary = NULL;
                const F2cExpr *length = argument->symbol->character_length_expression;
                int result;
                if (length == NULL && argument->symbol->character_length_syntax.count != 0U) {
                    temporary = f2c_parse_expression_tokens(
                        unit, argument->symbol->character_length_syntax.tokens,
                        argument->symbol->character_length_syntax.count,
                        argument->symbol->character_length_syntax.source, NULL);
                    length = temporary;
                }
                if (length == NULL && argument->symbol->character_length != NULL &&
                    strcmp(argument->symbol->character_length, "1") == 0) {
                    *value = 1;
                    return 1;
                }
                result = f2c_constant_evaluate_integer(evaluation, length, value, depth + 1U);
                f2c_expr_free(temporary);
                return result;
            }
            return 0;
        }
        if ((strcmp(expression->text, "max") == 0 || strcmp(expression->text, "min") == 0) &&
            f2c_constant_evaluate_integer(evaluation, expression->children[0], value, depth + 1U)) {
            for (i = 1U; i < expression->child_count; ++i) {
                if (!f2c_constant_evaluate_integer(evaluation, expression->children[i], &right,
                                                   depth + 1U))
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
        !f2c_constant_evaluate_integer(evaluation, expression->children[0], &left, depth + 1U) ||
        !f2c_constant_evaluate_integer(evaluation, expression->children[1], &right, depth + 1U))
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
    return f2c_constant_evaluate_integer(&evaluation, expression, value, 0U);
}

int f2c_evaluate_integer_syntax(Unit *unit, F2cTokenRange syntax, int64_t *value) {
    const char *error_at = NULL;
    F2cExpr *expression;
    int result;
    if (syntax.count == 0U || syntax.tokens == NULL)
        return 0;
    expression =
        f2c_parse_expression_tokens(unit, syntax.tokens, syntax.count, syntax.source, &error_at);
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
        if (f2c_intrinsic_is_numeric_model(expression->intrinsic) &&
            expression->intrinsic != F2C_INTRINSIC_SELECTED_INT_KIND &&
            expression->intrinsic != F2C_INTRINSIC_SELECTED_REAL_KIND)
            return 1;
        if (expression->intrinsic == F2C_INTRINSIC_BIT_SIZE) {
            const F2cExpr *argument =
                f2c_intrinsic_argument(expression->children, expression->child_count, "i", 0U);
            return argument != NULL && argument->type == TYPE_INTEGER;
        }
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
