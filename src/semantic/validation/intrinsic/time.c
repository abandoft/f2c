#include "semantic/validation/private.h"

#include "semantic/validation/intrinsic/arguments.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int resolved_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static int supported_integer_kind(const F2cExpr *expression) {
    const int kind = resolved_kind(expression);
    return expression != NULL && expression->type == TYPE_INTEGER &&
           (kind == 1 || kind == 2 || kind == 4 || kind == 8);
}

static int supported_real_kind(const F2cExpr *expression) {
    const int kind = resolved_kind(expression);
    return expression != NULL &&
           (expression->type == TYPE_REAL || expression->type == TYPE_DOUBLE) &&
           (kind == 4 || kind == 8);
}

static void diagnose_argument(Context *context, const F2cStatement *statement,
                              const F2cExpr *argument, const char *message) {
    f2c_diagnostic_at(context, statement->line,
                      f2c_validation_expression_start_column(statement->text, argument), 1, "%s",
                      message);
}

static void validate_scalar_output(Context *context, const F2cStatement *statement,
                                   const F2cExpr *argument, Type type, const char *name) {
    char message[160];
    if (argument == NULL)
        return;
    if (argument->type != type || argument->rank != 0U) {
        (void)snprintf(message, sizeof(message), "%s argument %s must be a scalar %s",
                       statement->name, name,
                       type == TYPE_INTEGER     ? "INTEGER"
                       : type == TYPE_CHARACTER ? "CHARACTER"
                                                : "REAL");
        diagnose_argument(context, statement, argument, message);
    }
    if (!argument->definable) {
        (void)snprintf(message, sizeof(message), "%s argument %s must be definable",
                       statement->name, name);
        diagnose_argument(context, statement, argument, message);
    }
}

static void validate_date_and_time(Context *context, F2cStatement *statement) {
    static const char *const names[] = {"date", "time", "zone", "values"};
    F2cBoundIntrinsicArguments bound;
    size_t argument;
    statement->intrinsic = F2C_INTRINSIC_DATE_AND_TIME;
    if (statement->item_count > 4U)
        f2c_diagnostic_at(context, statement->line, statement->name_span.begin.column, 1,
                          "DATE_AND_TIME accepts at most four arguments");
    bound = f2c_validation_bind_intrinsic_arguments(context, statement->line, statement->text,
                                                    "DATE_AND_TIME", statement->arguments,
                                                    statement->item_count, names, 4U, 0U);
    for (argument = 0U; argument < 3U; ++argument) {
        const F2cExpr *value = bound.values[argument];
        validate_scalar_output(context, statement, value, TYPE_CHARACTER,
                               argument == 0U ? "DATE" : (argument == 1U ? "TIME" : "ZONE"));
        if (value != NULL && resolved_kind(value) != f2c_default_kind(TYPE_CHARACTER))
            diagnose_argument(context, statement, value,
                              "DATE_AND_TIME character arguments must use the supported default "
                              "CHARACTER kind");
    }
    if (bound.values[3] != NULL) {
        const F2cExpr *values = bound.values[3];
        const int kind = resolved_kind(values);
        if (!supported_integer_kind(values) || kind == 1 || values->rank != 1U)
            diagnose_argument(context, statement, values,
                              "DATE_AND_TIME argument VALUES must be a rank-one INTEGER array "
                              "with decimal range of at least four");
        if (!values->definable)
            diagnose_argument(context, statement, values,
                              "DATE_AND_TIME argument VALUES must be definable");
        if (values->rank == 1U && values->shape.dimensions[0].extent_known &&
            values->shape.dimensions[0].extent < UINT64_C(8))
            diagnose_argument(context, statement, values,
                              "DATE_AND_TIME argument VALUES must contain at least 8 elements");
    }
}

static void validate_system_clock(Context *context, F2cStatement *statement) {
    static const char *const names[] = {"count", "count_rate", "count_max"};
    F2cBoundIntrinsicArguments bound;
    statement->intrinsic = F2C_INTRINSIC_SYSTEM_CLOCK;
    if (statement->item_count > 3U)
        f2c_diagnostic_at(context, statement->line, statement->name_span.begin.column, 1,
                          "SYSTEM_CLOCK accepts at most three arguments");
    bound = f2c_validation_bind_intrinsic_arguments(context, statement->line, statement->text,
                                                    "SYSTEM_CLOCK", statement->arguments,
                                                    statement->item_count, names, 3U, 0U);
    validate_scalar_output(context, statement, bound.values[0], TYPE_INTEGER, "COUNT");
    if (bound.values[0] != NULL && !supported_integer_kind(bound.values[0]))
        diagnose_argument(context, statement, bound.values[0],
                          "SYSTEM_CLOCK argument COUNT uses an unsupported INTEGER kind");
    if (bound.values[1] != NULL) {
        const F2cExpr *rate = bound.values[1];
        if (rate->rank != 0U || (!supported_integer_kind(rate) && !supported_real_kind(rate)))
            diagnose_argument(context, statement, rate,
                              "SYSTEM_CLOCK argument COUNT_RATE must be a scalar INTEGER or REAL "
                              "with a supported kind");
        if (!rate->definable)
            diagnose_argument(context, statement, rate,
                              "SYSTEM_CLOCK argument COUNT_RATE must be definable");
    }
    validate_scalar_output(context, statement, bound.values[2], TYPE_INTEGER, "COUNT_MAX");
    if (bound.values[2] != NULL && !supported_integer_kind(bound.values[2]))
        diagnose_argument(context, statement, bound.values[2],
                          "SYSTEM_CLOCK argument COUNT_MAX uses an unsupported INTEGER kind");
}

static void validate_cpu_time(Context *context, F2cStatement *statement) {
    static const char *const names[] = {"time"};
    F2cBoundIntrinsicArguments bound;
    statement->intrinsic = F2C_INTRINSIC_CPU_TIME;
    if (statement->item_count != 1U)
        f2c_diagnostic_at(context, statement->line, statement->name_span.begin.column, 1,
                          "CPU_TIME requires exactly one argument");
    bound = f2c_validation_bind_intrinsic_arguments(context, statement->line, statement->text,
                                                    "CPU_TIME", statement->arguments,
                                                    statement->item_count, names, 1U, 1U);
    if (bound.values[0] == NULL)
        return;
    if ((bound.values[0]->type != TYPE_REAL && bound.values[0]->type != TYPE_DOUBLE) ||
        bound.values[0]->rank != 0U)
        diagnose_argument(context, statement, bound.values[0],
                          "CPU_TIME argument TIME must be a scalar REAL");
    if (!bound.values[0]->definable)
        diagnose_argument(context, statement, bound.values[0],
                          "CPU_TIME argument TIME must be definable");
    if (!supported_real_kind(bound.values[0]))
        diagnose_argument(context, statement, bound.values[0],
                          "CPU_TIME argument TIME must have a supported REAL kind");
}

void f2c_validation_time_intrinsic(Context *context, F2cStatement *statement) {
    if (statement == NULL || statement->kind != F2C_STMT_CALL || statement->name == NULL)
        return;
    if (strcmp(statement->name, "date_and_time") == 0)
        validate_date_and_time(context, statement);
    else if (strcmp(statement->name, "system_clock") == 0)
        validate_system_clock(context, statement);
    else if (strcmp(statement->name, "cpu_time") == 0)
        validate_cpu_time(context, statement);
}
