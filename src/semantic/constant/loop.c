#include "core/numeric/loop.h"
#include "internal/f2c.h"
#include "semantic/numeric_model.h"

int f2c_integer_loop_parameters_fit(int kind, int64_t first, int64_t last, int64_t step) {
    const F2cNumericModel *model =
        f2c_numeric_model(TYPE_INTEGER, kind != 0 ? kind : f2c_default_kind(TYPE_INTEGER));
    if (model == NULL)
        return 0;
    const int64_t maximum = model->integer_huge;
    const int64_t minimum = -maximum - 1;
    return first >= minimum && first <= maximum && last >= minimum && last <= maximum &&
           step >= minimum && step <= maximum;
}

int f2c_evaluate_integer_loop_parameters(Unit *unit, int kind, const F2cExpr *initial,
                                         const F2cExpr *limit, const F2cExpr *increment,
                                         int64_t *first, int64_t *last, int64_t *step) {
    return first != NULL && last != NULL && step != NULL &&
           f2c_evaluate_integer_constant(unit, initial, first) &&
           f2c_evaluate_integer_constant(unit, limit, last) &&
           f2c_evaluate_integer_constant(unit, increment, step) &&
           f2c_integer_loop_parameters_fit(kind, *first, *last, *step);
}

int f2c_integer_iteration_count(int64_t first, int64_t last, int64_t step, uint64_t *count) {
    uint64_t remaining;
    int active;
    if (count == NULL)
        return 0;
    active = f2c_integer_loop_begin(first, last, step, &remaining);
    /* Expansion needs a materializable cardinality. Execution can represent a
     * complete 2^64 interval and leave it through an early EXIT. */
    if (active < 0 || (active != 0 && remaining == UINT64_MAX))
        return 0;
    *count = active != 0 ? remaining + 1U : 0U;
    return 1;
}
