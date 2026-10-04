#include "semantic/validation/private.h"

#include <math.h>

void f2c_validation_integer_loop_parameter(Context *context, Unit *unit, int kind,
                                           const F2cExpr *expression, const char *role) {
    int64_t integer;
    double real;
    int outside = 0;
    const int resolved = kind != 0 ? kind : f2c_default_kind(TYPE_INTEGER);
    if (expression == NULL || expression->rank != 0U ||
        (resolved != 1 && resolved != 2 && resolved != 4 && resolved != 8))
        return;
    if (expression->type == TYPE_INTEGER &&
        f2c_evaluate_integer_constant(unit, expression, &integer)) {
        outside = !f2c_integer_loop_parameters_fit(resolved, integer, integer, 1);
    } else if ((expression->type == TYPE_REAL || expression->type == TYPE_DOUBLE) &&
               (expression->type_kind == 0 || expression->type_kind <= 8) &&
               f2c_evaluate_real_constant(unit, expression, &real)) {
        const double limit = ldexp(1.0, resolved * 8 - 1);
        real = trunc(real);
        outside = !isfinite(real) || real < -limit || real >= limit;
    }
    if (outside)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &expression->span, 1,
                                 "%s exceeds the iteration variable kind %d", role, resolved);
}

static int ordered_numeric_scalar(const F2cExpr *expression) {
    return expression != NULL && expression->rank == 0U &&
           (expression->type == TYPE_INTEGER || expression->type == TYPE_REAL ||
            expression->type == TYPE_DOUBLE);
}

static int constant_step_is_zero(Unit *unit, const F2cStatement *statement) {
    const F2cExpr *step = statement->step;
    const F2cExpr *variable = statement->left;
    int64_t integer;
    double real;
    if (step == NULL || variable == NULL)
        return 0;
    if (step->type == TYPE_INTEGER)
        return f2c_evaluate_integer_constant(unit, step, &integer) && integer == 0;
    if (!ordered_numeric_scalar(step) || !f2c_evaluate_real_constant(unit, step, &real))
        return 0;
    if (variable->type == TYPE_INTEGER)
        return real > -1.0 && real < 1.0;
    const int kind =
        variable->type_kind != 0 ? variable->type_kind : f2c_default_kind(variable->type);
    /* The existing real constant evaluator stores double. Do not infer that
     * a wider REAL model's tiny, nonzero control parameter is zero. */
    if (kind > 8)
        return 0;
    return kind == 4 ? (float)real == 0.0f : real == 0.0;
}

void f2c_validation_do(Context *context, Unit *unit, const F2cStatement *statement) {
    const F2cExpr *variable = statement->left;
    if (variable != NULL && (variable->kind != F2C_EXPR_NAME || !ordered_numeric_scalar(variable) ||
                             !variable->definable))
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &variable->span, 1,
                                 "counted DO variable must be a definable scalar INTEGER or REAL "
                                 "name");
    const F2cExpr *controls[] = {statement->right, statement->limit, statement->step};
    for (size_t index = 0U; index < sizeof(controls) / sizeof(controls[0]); ++index) {
        if (controls[index] != NULL && !ordered_numeric_scalar(controls[index]))
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &controls[index]->span, 1,
                                     "counted DO initial value, limit, and step must be scalar "
                                     "INTEGER or REAL expressions");
        if (variable != NULL && variable->type == TYPE_INTEGER)
            f2c_validation_integer_loop_parameter(context, unit, variable->type_kind,
                                                  controls[index], "counted DO control");
    }
    if (constant_step_is_zero(unit, statement))
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->step->span, 1,
                                 "counted DO step cannot be zero after conversion to the "
                                 "iteration variable type and kind");
}
