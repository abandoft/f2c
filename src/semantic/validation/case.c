#include "semantic/validation/private.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct CharacterConstant {
    char *data;
    size_t length;
} CharacterConstant;

static int initialization_constant(const F2cExpr *expression) {
    size_t i;
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
    case F2C_EXPR_UNARY:
    case F2C_EXPR_BINARY:
        break;
    case F2C_EXPR_CALL:
        if (expression->text == NULL || !f2c_is_intrinsic_name(expression->text))
            return 0;
        break;
    case F2C_EXPR_ARRAY_REFERENCE:
    case F2C_EXPR_SUBSTRING:
        if (expression->symbol == NULL || !expression->symbol->parameter)
            return 0;
        break;
    default:
        return 0;
    }
    for (i = 0U; i < expression->child_count; ++i) {
        if (!initialization_constant(expression->children[i]))
            return 0;
    }
    return 1;
}

static int valid_case_type(Type type) {
    return type == TYPE_INTEGER || type == TYPE_CHARACTER || type == TYPE_LOGICAL;
}

static void validate_case_endpoint(Context *context, Unit *unit, F2cStatement *statement,
                                   const F2cExpr *selector, F2cExpr *endpoint, const char *role) {
    int64_t integer_value;
    if (endpoint == NULL)
        return;
    f2c_validation_report_parse_error(context, statement->line, statement->text, endpoint, role);
    f2c_validation_constructor(context, unit, statement->line, statement->text, endpoint);
    f2c_validation_expression_calls(context, unit, statement->line, statement->text, endpoint);
    if (endpoint->rank != 0U || !valid_case_type(endpoint->type)) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &endpoint->span, 1,
            "%s must be a scalar INTEGER, CHARACTER, or LOGICAL initialization expression", role);
    } else if (selector != NULL && valid_case_type(selector->type) &&
               endpoint->type != selector->type) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &endpoint->span, 1,
                                 "%s type %s does not match SELECT CASE selector type %s", role,
                                 f2c_validation_type_name(endpoint->type),
                                 f2c_validation_type_name(selector->type));
    } else if (selector != NULL && selector->type == TYPE_CHARACTER &&
               endpoint->type_kind != selector->type_kind) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &endpoint->span, 1,
                                 "%s CHARACTER kind does not match SELECT CASE selector kind",
                                 role);
    } else if (!initialization_constant(endpoint) ||
               (endpoint->type == TYPE_INTEGER &&
                !f2c_evaluate_integer_constant(unit, endpoint, &integer_value))) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &endpoint->span, 1,
                                 "%s must be an initialization expression", role);
    }
}

void f2c_validation_case_statement(Context *context, Unit *unit, F2cStatement *statement) {
    const F2cStatement *select;
    const F2cExpr *selector;
    size_t i;
    if (statement == NULL || statement->kind != F2C_STMT_CASE)
        return;
    select = statement->construct_owner;
    selector = select != NULL ? select->expression : NULL;
    if (select == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                 "CASE statement must be directly enclosed by SELECT CASE");
        return;
    }
    if (!statement->case_syntax_valid) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "malformed CASE value range list");
        return;
    }
    if (statement->case_default) {
        if (statement->case_range_count != 0U)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                     "CASE DEFAULT cannot contain value ranges");
        return;
    }
    if (statement->case_range_count == 0U) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "CASE requires at least one value range");
        return;
    }
    for (i = 0U; i < statement->case_range_count; ++i) {
        F2cCaseRange *range = &statement->case_ranges[i];
        validate_case_endpoint(context, unit, statement, selector, range->lower,
                               "CASE lower value");
        validate_case_endpoint(context, unit, statement, selector, range->upper,
                               "CASE upper value");
        if (range->has_colon && selector != NULL && selector->type == TYPE_LOGICAL)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &range->span, 1,
                                     "LOGICAL CASE values cannot use ranges");
    }
}

static int integer_bounds(Unit *unit, const F2cCaseRange *range, int64_t *lower, int64_t *upper) {
    if (range == NULL || lower == NULL || upper == NULL)
        return 0;
    if (!range->has_colon) {
        if (!f2c_evaluate_integer_constant(unit, range->lower, lower))
            return 0;
        *upper = *lower;
        return 1;
    }
    *lower = INT64_MIN;
    *upper = INT64_MAX;
    return (range->lower == NULL || f2c_evaluate_integer_constant(unit, range->lower, lower)) &&
           (range->upper == NULL || f2c_evaluate_integer_constant(unit, range->upper, upper));
}

static int case_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL)
        return 0;
    while (*left != '\0' && *right != '\0' &&
           tolower((unsigned char)*left) == tolower((unsigned char)*right)) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int logical_constant(const F2cExpr *expression, int *value) {
    int left;
    int right;
    if (expression == NULL || value == NULL)
        return 0;
    if (expression->kind == F2C_EXPR_LOGICAL_LITERAL && expression->text != NULL) {
        if (case_equal(expression->text, ".true.")) {
            *value = 1;
            return 1;
        }
        if (!case_equal(expression->text, ".false."))
            return 0;
        *value = 0;
        return 1;
    }
    if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
        expression->symbol->parameter)
        return logical_constant(expression->symbol->initializer_expression, value);
    if (expression->kind == F2C_EXPR_UNARY && case_equal(expression->text, ".not.") &&
        expression->child_count == 1U) {
        if (!logical_constant(expression->children[0], value))
            return 0;
        *value = !*value;
        return 1;
    }
    if (expression->kind == F2C_EXPR_BINARY && expression->child_count == 2U &&
        logical_constant(expression->children[0], &left) &&
        logical_constant(expression->children[1], &right)) {
        if (case_equal(expression->text, ".and."))
            *value = left && right;
        else if (case_equal(expression->text, ".or."))
            *value = left || right;
        else if (case_equal(expression->text, ".eqv."))
            *value = left == right;
        else if (case_equal(expression->text, ".neqv."))
            *value = left != right;
        else
            return 0;
        return 1;
    }
    return 0;
}

static int character_literal(const char *text, CharacterConstant *constant) {
    const char *cursor;
    const char *quote;
    const char *single_quote;
    const char *double_quote;
    char delimiter;
    size_t capacity;
    if (text == NULL || constant == NULL)
        return 0;
    single_quote = strchr(text, '\'');
    double_quote = strchr(text, '"');
    quote = single_quote == NULL || (double_quote != NULL && double_quote < single_quote)
                ? double_quote
                : single_quote;
    if (quote == NULL)
        return 0;
    delimiter = *quote;
    capacity = strlen(quote + 1U) + 1U;
    constant->data = (char *)malloc(capacity);
    if (constant->data == NULL)
        return 0;
    cursor = quote + 1U;
    while (*cursor != '\0') {
        if (*cursor == delimiter) {
            if (cursor[1] == delimiter) {
                constant->data[constant->length++] = delimiter;
                cursor += 2;
                continue;
            }
            constant->data[constant->length] = '\0';
            return 1;
        }
        constant->data[constant->length++] = *cursor++;
    }
    free(constant->data);
    memset(constant, 0, sizeof(*constant));
    return 0;
}

static int character_constant(const F2cExpr *expression, CharacterConstant *constant) {
    CharacterConstant left = {0};
    CharacterConstant right = {0};
    if (expression == NULL || constant == NULL)
        return 0;
    if (expression->kind == F2C_EXPR_STRING_LITERAL)
        return character_literal(expression->text, constant);
    if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
        expression->symbol->parameter)
        return character_constant(expression->symbol->initializer_expression, constant);
    if (expression->kind == F2C_EXPR_BINARY && expression->text != NULL &&
        strcmp(expression->text, "//") == 0 && expression->child_count == 2U &&
        character_constant(expression->children[0], &left) &&
        character_constant(expression->children[1], &right)) {
        if (left.length < SIZE_MAX - right.length) {
            constant->data = (char *)malloc(left.length + right.length + 1U);
            if (constant->data != NULL) {
                memcpy(constant->data, left.data, left.length);
                memcpy(constant->data + left.length, right.data, right.length);
                constant->length = left.length + right.length;
                constant->data[constant->length] = '\0';
            }
        }
    }
    free(left.data);
    free(right.data);
    return constant->data != NULL;
}

static int compare_character(const CharacterConstant *left, const CharacterConstant *right) {
    size_t i;
    const size_t length = left->length > right->length ? left->length : right->length;
    for (i = 0U; i < length; ++i) {
        const unsigned char l =
            (unsigned char)(i < left->length ? left->data[i] : (unsigned char)' ');
        const unsigned char r =
            (unsigned char)(i < right->length ? right->data[i] : (unsigned char)' ');
        if (l != r)
            return l < r ? -1 : 1;
    }
    return 0;
}

static const F2cExpr *range_lower(const F2cCaseRange *range) { return range->lower; }

static const F2cExpr *range_upper(const F2cCaseRange *range) {
    return range->has_colon ? range->upper : range->lower;
}

static int character_ranges_overlap(const F2cCaseRange *left, const F2cCaseRange *right) {
    CharacterConstant left_lower = {0};
    CharacterConstant left_upper = {0};
    CharacterConstant right_lower = {0};
    CharacterConstant right_upper = {0};
    int overlap = 0;
    const int left_lower_known =
        range_lower(left) == NULL || character_constant(range_lower(left), &left_lower);
    const int left_upper_known =
        range_upper(left) == NULL || character_constant(range_upper(left), &left_upper);
    const int right_lower_known =
        range_lower(right) == NULL || character_constant(range_lower(right), &right_lower);
    const int right_upper_known =
        range_upper(right) == NULL || character_constant(range_upper(right), &right_upper);
    if (left_lower_known && left_upper_known && right_lower_known && right_upper_known) {
        const int left_before_right = range_upper(left) != NULL && range_lower(right) != NULL &&
                                      compare_character(&left_upper, &right_lower) < 0;
        const int right_before_left = range_upper(right) != NULL && range_lower(left) != NULL &&
                                      compare_character(&right_upper, &left_lower) < 0;
        overlap = !left_before_right && !right_before_left;
    }
    free(left_lower.data);
    free(left_upper.data);
    free(right_lower.data);
    free(right_upper.data);
    return overlap;
}

static int ranges_overlap(Unit *unit, Type type, const F2cCaseRange *left,
                          const F2cCaseRange *right) {
    if (type == TYPE_INTEGER) {
        int64_t left_lower;
        int64_t left_upper;
        int64_t right_lower;
        int64_t right_upper;
        if (!integer_bounds(unit, left, &left_lower, &left_upper) ||
            !integer_bounds(unit, right, &right_lower, &right_upper) || left_lower > left_upper ||
            right_lower > right_upper)
            return 0;
        return left_lower <= right_upper && right_lower <= left_upper;
    }
    if (type == TYPE_LOGICAL && !left->has_colon && !right->has_colon) {
        int left_value;
        int right_value;
        return logical_constant(left->lower, &left_value) &&
               logical_constant(right->lower, &right_value) && left_value == right_value;
    }
    return type == TYPE_CHARACTER && character_ranges_overlap(left, right);
}

static size_t collect_direct_cases(const Unit *unit, const F2cStatement *select,
                                   const F2cStatement **cases) {
    size_t count = 0U;
    size_t index;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *statement = &unit->statements[index];
        if (statement->kind == F2C_STMT_CASE && statement->construct_owner == select)
            cases[count++] = statement;
    }
    return count;
}

void f2c_validation_select_case_constructs(Context *context, Unit *unit) {
    const F2cStatement **cases;
    size_t select_index;
    if (unit == NULL || unit->statement_count == 0U)
        return;
    if (unit->statement_count > SIZE_MAX / sizeof(*cases)) {
        f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                       "out of memory while validating SELECT CASE constructs");
        return;
    }
    cases = (const F2cStatement **)malloc(unit->statement_count * sizeof(*cases));
    if (cases == NULL) {
        f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                       "out of memory while validating SELECT CASE constructs");
        return;
    }
    for (select_index = 0U; select_index < unit->statement_count; ++select_index) {
        const F2cStatement *select = &unit->statements[select_index];
        size_t case_count;
        size_t i;
        if (select->kind != F2C_STMT_SELECT_CASE || select->expression == NULL ||
            !valid_case_type(select->expression->type))
            continue;
        case_count = collect_direct_cases(unit, select, cases);
        for (i = 0U; i < case_count; ++i) {
            size_t j;
            size_t left_range;
            if (!cases[i]->case_syntax_valid)
                continue;
            for (j = 0U; j < i; ++j) {
                if (cases[i]->case_default && cases[j]->case_default) {
                    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &cases[i]->span, 1,
                                             "SELECT CASE cannot contain more than one CASE "
                                             "DEFAULT");
                }
            }
            if (cases[i]->case_default)
                continue;
            for (left_range = 0U; left_range < cases[i]->case_range_count; ++left_range) {
                size_t prior_case;
                for (prior_case = 0U; prior_case <= i; ++prior_case) {
                    size_t right_limit =
                        prior_case == i ? left_range : cases[prior_case]->case_range_count;
                    size_t right_range;
                    if (cases[prior_case]->case_default)
                        continue;
                    for (right_range = 0U; right_range < right_limit; ++right_range) {
                        if (ranges_overlap(unit, select->expression->type,
                                           &cases[i]->case_ranges[left_range],
                                           &cases[prior_case]->case_ranges[right_range])) {
                            f2c_diagnostic_span_code(
                                context, F2C_DIAGNOSTIC_SEMANTIC,
                                &cases[i]->case_ranges[left_range].span, 1,
                                "CASE value range overlaps a previous range in this SELECT CASE");
                        }
                    }
                }
            }
        }
    }
    free(cases);
}
