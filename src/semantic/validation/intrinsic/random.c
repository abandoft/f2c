#include "semantic/validation/private.h"

#include "semantic/validation/intrinsic/arguments.h"

#include <stdio.h>
#include <string.h>

static int default_integer(const F2cExpr *expression) {
    const int kind = expression != NULL && expression->type_kind != 0
                         ? expression->type_kind
                         : f2c_default_kind(TYPE_INTEGER);
    return expression != NULL && expression->type == TYPE_INTEGER &&
           kind == f2c_default_kind(TYPE_INTEGER);
}

static void diagnose_argument(Context *context, const F2cStatement *statement,
                              const F2cExpr *argument, const char *message) {
    f2c_diagnostic_at(context, statement->line,
                      f2c_validation_expression_start_column(statement->text, argument), 1, "%s",
                      message);
}

static void validate_random_number(Context *context, F2cStatement *statement) {
    static const char *const names[] = {"harvest"};
    F2cBoundIntrinsicArguments bound;
    const F2cExpr *harvest;
    statement->intrinsic = F2C_INTRINSIC_RANDOM_NUMBER;
    if (statement->item_count != 1U)
        f2c_diagnostic_at(context, statement->line, statement->name_span.begin.column, 1,
                          "RANDOM_NUMBER requires exactly one argument");
    bound = f2c_validation_bind_intrinsic_arguments(context, statement->line, statement->text,
                                                    "RANDOM_NUMBER", statement->arguments,
                                                    statement->item_count, names, 1U, 1U);
    harvest = bound.values[0];
    if (harvest == NULL)
        return;
    if ((harvest->type != TYPE_REAL && harvest->type != TYPE_DOUBLE) ||
        (harvest->type_kind != 0 && harvest->type_kind != 4 && harvest->type_kind != 8))
        diagnose_argument(context, statement, harvest,
                          "RANDOM_NUMBER argument HARVEST must have supported REAL kind");
    if (!harvest->definable)
        diagnose_argument(context, statement, harvest,
                          "RANDOM_NUMBER argument HARVEST must be definable");
}

static void validate_seed_size(Context *context, const F2cStatement *statement,
                               const F2cExpr *size) {
    if (size == NULL)
        return;
    if (!default_integer(size) || size->rank != 0U)
        diagnose_argument(context, statement, size,
                          "RANDOM_SEED argument SIZE must be a scalar default INTEGER");
    if (!size->definable)
        diagnose_argument(context, statement, size, "RANDOM_SEED argument SIZE must be definable");
}

static void validate_seed_vector(Context *context, const F2cStatement *statement,
                                 const F2cExpr *vector, const char *name, int definable) {
    if (vector == NULL)
        return;
    if (!default_integer(vector) || vector->rank != 1U) {
        char message[128];
        (void)snprintf(message, sizeof(message),
                       "RANDOM_SEED argument %s must be a rank-one default INTEGER array", name);
        diagnose_argument(context, statement, vector, message);
    }
    if (definable && !vector->definable) {
        char message[96];
        (void)snprintf(message, sizeof(message), "RANDOM_SEED argument %s must be definable", name);
        diagnose_argument(context, statement, vector, message);
    }
    if (vector->rank == 1U && vector->shape.dimensions[0].extent_known &&
        vector->shape.dimensions[0].extent < UINT64_C(2)) {
        char message[128];
        (void)snprintf(message, sizeof(message),
                       "RANDOM_SEED argument %s must contain at least 2 elements", name);
        diagnose_argument(context, statement, vector, message);
    }
}

static void validate_random_seed(Context *context, F2cStatement *statement) {
    static const char *const names[] = {"size", "put", "get"};
    F2cBoundIntrinsicArguments bound;
    size_t present = 0U;
    size_t argument;
    statement->intrinsic = F2C_INTRINSIC_RANDOM_SEED;
    if (statement->item_count > 3U)
        f2c_diagnostic_at(context, statement->line, statement->name_span.begin.column, 1,
                          "RANDOM_SEED accepts at most three argument positions");
    bound = f2c_validation_bind_intrinsic_arguments(context, statement->line, statement->text,
                                                    "RANDOM_SEED", statement->arguments,
                                                    statement->item_count, names, 3U, 0U);
    for (argument = 0U; argument < 3U; ++argument)
        present += bound.values[argument] != NULL;
    if (present > 1U)
        f2c_diagnostic_at(context, statement->line, statement->name_span.begin.column, 1,
                          "RANDOM_SEED permits at most one of SIZE, PUT, and GET");
    validate_seed_size(context, statement, bound.values[0]);
    validate_seed_vector(context, statement, bound.values[1], "PUT", 0);
    validate_seed_vector(context, statement, bound.values[2], "GET", 1);
}

void f2c_validation_random_intrinsic(Context *context, F2cStatement *statement) {
    if (statement == NULL || statement->kind != F2C_STMT_CALL || statement->name == NULL)
        return;
    if (strcmp(statement->name, "random_number") == 0)
        validate_random_number(context, statement);
    else if (strcmp(statement->name, "random_seed") == 0)
        validate_random_seed(context, statement);
}
