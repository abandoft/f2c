#include "codegen/array/private.h"
#include "codegen/expression/private.h"
#include "codegen/lowering/private.h"

#include <stdlib.h>

char *f2c_expression_etime(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *values =
        f2c_intrinsic_argument(expression->children, expression->child_count, "values", 0U);
    const char *const first_ordinal[] = {"0"};
    const char *const second_ordinal[] = {"1"};
    F2cExpr *first;
    F2cExpr *second;
    char *first_code;
    char *second_code;
    Buffer result = {0};
    if (values == NULL || values->rank != 1U) {
        *supported = 0;
        return NULL;
    }
    first = f2c_array_element_expression(unit, values, 1U, first_ordinal);
    second = f2c_array_element_expression(unit, values, 1U, second_ordinal);
    first_code = first != NULL ? f2c_expression_emit(unit, first, supported) : NULL;
    second_code =
        second != NULL && *supported ? f2c_expression_emit(unit, second, supported) : NULL;
    if (first_code != NULL && second_code != NULL && *supported)
        f2c_buffer_printf(&result, "f2c_etime(&(%s), &(%s))", first_code, second_code);
    else
        *supported = 0;
    free(first_code);
    free(second_code);
    f2c_codegen_expression_free(unit, first);
    f2c_codegen_expression_free(unit, second);
    return f2c_buffer_take(&result);
}
