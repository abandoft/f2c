#include "codegen/expression/private.h"

#include <stdlib.h>

static const char *operation_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_EXPONENT:
        return "exponent";
    case F2C_INTRINSIC_FRACTION:
        return "fraction";
    case F2C_INTRINSIC_NEAREST:
        return "nearest";
    case F2C_INTRINSIC_RRSPACING:
        return "rrspacing";
    case F2C_INTRINSIC_SCALE:
        return "scale";
    case F2C_INTRINSIC_SET_EXPONENT:
        return "set_exponent";
    case F2C_INTRINSIC_SPACING:
        return "spacing";
    case F2C_INTRINSIC_NONE:
    default:
        return NULL;
    }
}

char *f2c_expression_real_representation_intrinsic(Unit *unit, const F2cExpr *expression,
                                                   int *supported) {
    const F2cExpr *primary;
    const F2cExpr *secondary = NULL;
    const char *operation;
    char *primary_code;
    char *secondary_code = NULL;
    Buffer result = {0};
    int kind;
    if (expression == NULL || !f2c_intrinsic_is_real_representation(expression->intrinsic)) {
        *supported = 0;
        return NULL;
    }
    operation = operation_name(expression->intrinsic);
    primary = f2c_intrinsic_argument(expression->children, expression->child_count, "x", 0U);
    if (operation == NULL || primary == NULL || primary->rank != 0U ||
        (primary->type != TYPE_REAL && primary->type != TYPE_DOUBLE)) {
        *supported = 0;
        return NULL;
    }
    kind = primary->type_kind != 0 ? primary->type_kind : f2c_default_kind(primary->type);
    if (kind != 4 && kind != 8) {
        *supported = 0;
        return NULL;
    }
    if (expression->intrinsic == F2C_INTRINSIC_NEAREST)
        secondary = f2c_intrinsic_argument(expression->children, expression->child_count, "s", 1U);
    else if (expression->intrinsic == F2C_INTRINSIC_SCALE ||
             expression->intrinsic == F2C_INTRINSIC_SET_EXPONENT)
        secondary = f2c_intrinsic_argument(expression->children, expression->child_count, "i", 1U);
    if ((expression->intrinsic == F2C_INTRINSIC_NEAREST ||
         expression->intrinsic == F2C_INTRINSIC_SCALE ||
         expression->intrinsic == F2C_INTRINSIC_SET_EXPONENT) &&
        (secondary == NULL || secondary->rank != 0U)) {
        *supported = 0;
        return NULL;
    }
    primary_code = f2c_expression_emit(unit, primary, supported);
    if (secondary != NULL)
        secondary_code = *supported ? f2c_expression_emit(unit, secondary, supported) : NULL;
    if (!*supported || primary_code == NULL || (secondary != NULL && secondary_code == NULL)) {
        free(primary_code);
        free(secondary_code);
        return NULL;
    }
    f2c_buffer_printf(&result, "f2c_%s_r%d(%s", operation, kind, primary_code);
    if (expression->intrinsic == F2C_INTRINSIC_NEAREST)
        f2c_buffer_printf(&result, ", (double)(%s)", secondary_code);
    else if (expression->intrinsic == F2C_INTRINSIC_SCALE ||
             expression->intrinsic == F2C_INTRINSIC_SET_EXPONENT)
        f2c_buffer_printf(&result, ", (int64_t)(%s)", secondary_code);
    f2c_buffer_append(&result, ")");
    free(primary_code);
    free(secondary_code);
    return f2c_buffer_take(&result);
}
