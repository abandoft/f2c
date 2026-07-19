#include "codegen/expression/private.h"

#include <stdlib.h>

static const char *kind_suffix(int kind) {
    switch (kind) {
    case 1:
        return "i8";
    case 2:
        return "i16";
    case 4:
        return "i32";
    case 8:
        return "i64";
    default:
        return NULL;
    }
}

static const char *operation_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_BTEST:
        return "btest";
    case F2C_INTRINSIC_IAND:
        return "iand";
    case F2C_INTRINSIC_IBCLR:
        return "ibclr";
    case F2C_INTRINSIC_IBITS:
        return "ibits";
    case F2C_INTRINSIC_IBSET:
        return "ibset";
    case F2C_INTRINSIC_IEOR:
        return "ieor";
    case F2C_INTRINSIC_IOR:
        return "ior";
    case F2C_INTRINSIC_ISHFT:
        return "ishft";
    case F2C_INTRINSIC_ISHFTC:
        return "ishftc";
    case F2C_INTRINSIC_NOT:
        return "not";
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_MVBITS:
    default:
        return NULL;
    }
}

static size_t argument_names(F2cIntrinsicId intrinsic, const char *const **names) {
    static const char *const unary[] = {"i"};
    static const char *const binary[] = {"i", "j"};
    static const char *const position[] = {"i", "pos"};
    static const char *const bits[] = {"i", "pos", "len"};
    static const char *const shift[] = {"i", "shift"};
    static const char *const circular[] = {"i", "shift", "size"};
    switch (intrinsic) {
    case F2C_INTRINSIC_IAND:
    case F2C_INTRINSIC_IEOR:
    case F2C_INTRINSIC_IOR:
        *names = binary;
        return 2U;
    case F2C_INTRINSIC_BTEST:
    case F2C_INTRINSIC_IBCLR:
    case F2C_INTRINSIC_IBSET:
        *names = position;
        return 2U;
    case F2C_INTRINSIC_IBITS:
        *names = bits;
        return 3U;
    case F2C_INTRINSIC_ISHFT:
        *names = shift;
        return 2U;
    case F2C_INTRINSIC_ISHFTC:
        *names = circular;
        return 3U;
    case F2C_INTRINSIC_NOT:
        *names = unary;
        return 1U;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_MVBITS:
    default:
        *names = NULL;
        return 0U;
    }
}

static void free_arguments(char **arguments, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index)
        free(arguments[index]);
}

char *f2c_expression_bit_intrinsic(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *model;
    const char *suffix;
    const char *integer_type;
    const char *operation;
    const char *const *names = NULL;
    char *arguments[3] = {NULL, NULL, NULL};
    size_t count;
    size_t index;
    int kind;
    Buffer result = {0};
    if (expression == NULL || expression->intrinsic == F2C_INTRINSIC_NONE ||
        expression->intrinsic == F2C_INTRINSIC_MVBITS) {
        *supported = 0;
        return NULL;
    }
    model = f2c_intrinsic_argument(expression->children, expression->child_count, "i", 0U);
    if (model == NULL || model->type != TYPE_INTEGER) {
        *supported = 0;
        return NULL;
    }
    kind = model->type_kind != 0 ? model->type_kind : f2c_default_kind(TYPE_INTEGER);
    suffix = kind_suffix(kind);
    integer_type = f2c_c_type_kind(TYPE_INTEGER, kind);
    if (suffix == NULL) {
        *supported = 0;
        return NULL;
    }
    if (expression->intrinsic == F2C_INTRINSIC_BIT_SIZE) {
        f2c_buffer_printf(&result, "((%s)%d)", integer_type, kind * 8);
        return f2c_buffer_take(&result);
    }
    operation = operation_name(expression->intrinsic);
    count = argument_names(expression->intrinsic, &names);
    if (operation == NULL || count == 0U) {
        *supported = 0;
        return NULL;
    }
    for (index = 0U; index < count; ++index) {
        const F2cExpr *argument = f2c_intrinsic_argument(
            expression->children, expression->child_count, names[index], index);
        if (argument == NULL && expression->intrinsic == F2C_INTRINSIC_ISHFTC && index == 2U) {
            f2c_buffer_printf(&result, "INT64_C(%d)", kind * 8);
            arguments[index] = f2c_buffer_take(&result);
            continue;
        }
        if (argument == NULL || argument->rank != 0U) {
            free_arguments(arguments, count);
            *supported = 0;
            return NULL;
        }
        arguments[index] = f2c_expression_emit(unit, argument, supported);
        if (!*supported || arguments[index] == NULL) {
            free_arguments(arguments, count);
            return NULL;
        }
    }
    f2c_buffer_printf(&result, "f2c_%s_%s(", operation, suffix);
    for (index = 0U; index < count; ++index) {
        const int integer_value = index == 0U || ((expression->intrinsic == F2C_INTRINSIC_IAND ||
                                                   expression->intrinsic == F2C_INTRINSIC_IEOR ||
                                                   expression->intrinsic == F2C_INTRINSIC_IOR) &&
                                                  index == 1U);
        f2c_buffer_printf(&result, "%s((%s)(%s))", index == 0U ? "" : ", ",
                          integer_value ? integer_type : "int64_t", arguments[index]);
    }
    f2c_buffer_append(&result, ")");
    free_arguments(arguments, count);
    return f2c_buffer_take(&result);
}
