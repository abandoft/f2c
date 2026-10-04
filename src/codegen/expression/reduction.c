#include "codegen/expression/private.h"

#include "codegen/array/private.h"
#include "codegen/array/view.h"
#include "codegen/lowering/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const F2cExpr *reduction_argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static char *reduction_zero_value(const F2cExpr *expression) {
    Buffer result = {0};
    if (expression == NULL)
        return NULL;
    if (expression->type == TYPE_COMPLEX || expression->type == TYPE_DOUBLE_COMPLEX)
        return f2c_strdup(expression->type_kind == 4   ? "f2c_make_c(0.0f, 0.0f)"
                          : expression->type_kind == 8 ? "f2c_make_z(0.0, 0.0)"
                                                       : "f2c_make_q(0.0L, 0.0L)");
    if (expression->type == TYPE_LOGICAL)
        return f2c_strdup("false");
    f2c_buffer_printf(&result, "((%s)0)", f2c_expression_c_type(expression));
    return f2c_buffer_take(&result);
}

static int relation_code(F2cOperator operator_kind) {
    switch (operator_kind) {
    case F2C_OPERATOR_EQUAL:
        return 0;
    case F2C_OPERATOR_NOT_EQUAL:
        return 1;
    case F2C_OPERATOR_LESS:
        return 2;
    case F2C_OPERATOR_LESS_EQUAL:
        return 3;
    case F2C_OPERATOR_GREATER:
        return 4;
    case F2C_OPERATOR_GREATER_EQUAL:
        return 5;
    case F2C_OPERATOR_EQUIVALENT:
        return 6;
    case F2C_OPERATOR_NOT_EQUIVALENT:
        return 7;
    default:
        return -1;
    }
}

static int reduction_code(F2cIntrinsicId intrinsic) {
    if (intrinsic == F2C_INTRINSIC_ANY)
        return 0;
    if (intrinsic == F2C_INTRINSIC_ALL)
        return 1;
    if (intrinsic == F2C_INTRINSIC_COUNT)
        return 2;
    return -1;
}

static const char *kernel_type_suffix(const F2cExpr *expression) {
    const int kind =
        expression->type_kind != 0 ? expression->type_kind : f2c_default_kind(expression->type);
    switch (expression->type) {
    case TYPE_LOGICAL:
        if (kind == 1)
            return "l";
        /* Other logical models have the corresponding integer storage. */
        return kind == 2 ? "i16" : kind == 4 ? "i32" : kind == 8 ? "i64" : NULL;
    case TYPE_INTEGER:
        return kind == 1 ? "i8" : kind == 2 ? "i16" : kind == 4 ? "i32" : kind == 8 ? "i64" : NULL;
    case TYPE_REAL:
    case TYPE_DOUBLE:
        return kind == 4 ? "f" : kind == 8 ? "d" : NULL;
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        return kind == 4 ? "c" : kind == 8 ? "z" : NULL;
    default:
        return NULL;
    }
}

static int direct_relation_operand(const F2cExpr *operand) {
    if (operand == NULL)
        return 0;
    if (operand->rank != 0U)
        return operand->kind == F2C_EXPR_NAME && operand->symbol != NULL &&
               (operand->rank == 1U ||
                (!operand->symbol->pointer && !f2c_symbol_uses_descriptor(operand->symbol)));
    return operand->kind == F2C_EXPR_NAME || operand->kind == F2C_EXPR_INTEGER_LITERAL ||
           operand->kind == F2C_EXPR_REAL_LITERAL || operand->kind == F2C_EXPR_STRING_LITERAL ||
           operand->kind == F2C_EXPR_LOGICAL_LITERAL;
}

int f2c_expression_direct_relation_reduction(const F2cExpr *expression) {
    const F2cExpr *mask;
    const F2cExpr *left;
    const F2cExpr *right;
    int relation;
    if (expression == NULL || expression->kind != F2C_EXPR_CALL ||
        reduction_code(expression->intrinsic) < 0 || expression->child_count != 1U)
        return 0;
    mask = reduction_argument_value(expression->children[0]);
    if (mask == NULL || mask->kind != F2C_EXPR_BINARY || mask->rank == 0U ||
        mask->child_count != 2U)
        return 0;
    relation = relation_code(mask->operator_kind);
    left = mask->children[0];
    right = mask->children[1];
    return relation >= 0 && direct_relation_operand(left) && direct_relation_operand(right) &&
           left->type == right->type && left->type_kind == right->type_kind &&
           left->type != TYPE_DERIVED &&
           ((left->type != TYPE_COMPLEX && left->type != TYPE_DOUBLE_COMPLEX) || relation <= 1);
}

static int scalar_view(Unit *unit, const F2cExpr *expression, F2cArrayView *view, int *supported) {
    char *value = f2c_expression_emit(unit, expression, supported);
    Buffer address = {0};
    if (!*supported || value == NULL) {
        free(value);
        return 0;
    }
    f2c_buffer_printf(&address, "(&(%s){(%s)})", f2c_expression_c_type(expression), value);
    free(value);
    view->pointer = f2c_buffer_take(&address);
    view->count = f2c_strdup("SIZE_MAX");
    view->stride = f2c_strdup("0");
    view->storage_qualifiers = F2C_STORAGE_UNQUALIFIED;
    return view->pointer != NULL && view->count != NULL && view->stride != NULL;
}

static int operand_view(Unit *unit, const F2cExpr *expression, F2cArrayView *view, int *supported) {
    return expression->rank == 0U ? scalar_view(unit, expression, view, supported)
                                  : f2c_array_view(unit, expression, view, supported);
}

typedef struct CharacterOperand {
    char *pointer;
    char *count;
    char *stride;
    char *length;
    int pointer_vector;
    unsigned int storage_qualifiers;
} CharacterOperand;

static void free_character_operand(CharacterOperand *operand) {
    free(operand->pointer);
    free(operand->count);
    free(operand->stride);
    free(operand->length);
    memset(operand, 0, sizeof(*operand));
}

static int character_constructor_operand(Unit *unit, const F2cExpr *expression,
                                         CharacterOperand *operand, int *supported) {
    Buffer pointers = {0};
    size_t index;
    if (expression->child_count == 0U)
        return 0;
    operand->length = f2c_character_length_expression(unit, expression->children[0]);
    for (index = 0U; index < expression->child_count; ++index)
        operand->storage_qualifiers |= expression->children[index]->storage_qualifiers;
    f2c_buffer_printf(&pointers, "(const %schar *[%zu]){",
                      (operand->storage_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "",
                      expression->child_count);
    for (index = 0U; index < expression->child_count; ++index) {
        const F2cExpr *element = expression->children[index];
        char *value;
        char *pointer;
        if (element == NULL || element->rank != 0U)
            goto unsupported;
        value = f2c_expression_emit(unit, element, supported);
        pointer =
            *supported && value != NULL ? f2c_character_source_pointer(unit, element, value) : NULL;
        free(value);
        if (pointer == NULL)
            goto unsupported;
        f2c_buffer_printf(&pointers, "%s%s", index == 0U ? "" : ", ", pointer);
        free(pointer);
    }
    f2c_buffer_append(&pointers, "}");
    operand->pointer = f2c_buffer_take(&pointers);
    {
        Buffer count = {0};
        f2c_buffer_printf(&count, "%zuU", expression->child_count);
        operand->count = f2c_buffer_take(&count);
    }
    operand->stride = f2c_strdup("1");
    operand->pointer_vector = (operand->storage_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? 2 : 1;
    return operand->pointer != NULL && operand->count != NULL && operand->stride != NULL &&
           operand->length != NULL;

unsupported:
    free(pointers.data);
    *supported = 0;
    return 0;
}

static int character_operand(Unit *unit, const F2cExpr *expression, CharacterOperand *operand,
                             int *supported) {
    if (expression->rank == 0U) {
        operand->storage_qualifiers = expression->storage_qualifiers;
        char *value = f2c_expression_emit(unit, expression, supported);
        operand->pointer = *supported && value != NULL
                               ? f2c_character_source_pointer(unit, expression, value)
                               : NULL;
        operand->length = f2c_character_length_expression(unit, expression);
        operand->count = f2c_strdup("SIZE_MAX");
        operand->stride = f2c_strdup("0");
        free(value);
    } else if (f2c_lowering_code(unit, expression) == NULL &&
               expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR && expression->child_count == 1U &&
               expression->children[0]->rank != 0U) {
        return character_operand(unit, expression->children[0], operand, supported);
    } else if (f2c_lowering_code(unit, expression) == NULL &&
               expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        return character_constructor_operand(unit, expression, operand, supported);
    } else {
        operand->length = f2c_character_length_expression(unit, expression);
        F2cArrayView view = {0};
        if (!f2c_array_view(unit, expression, &view, supported))
            return 0;
        operand->pointer = view.pointer;
        operand->count = view.count;
        operand->stride = view.stride;
        operand->storage_qualifiers = view.storage_qualifiers;
    }
    return *supported && operand->pointer != NULL && operand->count != NULL &&
           operand->stride != NULL && operand->length != NULL;
}

static char *character_relation_reduction(Unit *unit, const F2cExpr *left, const F2cExpr *right,
                                          int relation, int reduction, int *supported) {
    CharacterOperand left_operand = {0};
    CharacterOperand right_operand = {0};
    Buffer result = {0};
    if (!character_operand(unit, left, &left_operand, supported) ||
        !character_operand(unit, right, &right_operand, supported)) {
        free_character_operand(&left_operand);
        free_character_operand(&right_operand);
        *supported = 0;
        return NULL;
    }
    const int qualified = ((left_operand.storage_qualifiers | right_operand.storage_qualifiers) &
                           F2C_STORAGE_VOLATILE) != 0U;
    f2c_buffer_printf(
        &result,
        "f2c_character_relation_reduce%s((const %svoid *)(%s), %s, %s, (size_t)(%s), %d, "
        "(const %svoid *)(%s), %s, %s, (size_t)(%s), %d, %d, %d)",
        qualified ? "_volatile" : "", qualified ? "volatile " : "", left_operand.pointer,
        left_operand.stride, left_operand.count, left_operand.length, left_operand.pointer_vector,
        qualified ? "volatile " : "", right_operand.pointer, right_operand.stride,
        right_operand.count, right_operand.length, right_operand.pointer_vector, relation,
        reduction);
    free_character_operand(&left_operand);
    free_character_operand(&right_operand);
    return f2c_buffer_take(&result);
}

char *f2c_expression_relation_reduction(Unit *unit, const F2cExpr *expression, int *supported,
                                        int *matched) {
    const F2cExpr *array;
    const F2cExpr *left;
    const F2cExpr *right;
    F2cArrayView left_view = {0};
    F2cArrayView right_view = {0};
    Buffer result = {0};
    int relation;
    int reduction;
    *matched = 0;
    reduction = expression != NULL ? reduction_code(expression->intrinsic) : -1;
    if (reduction < 0 || expression->child_count != 1U)
        return NULL;
    array = reduction_argument_value(expression->children[0]);
    if (f2c_lowering_code(unit, array) != NULL)
        return NULL;
    if (array == NULL || array->kind != F2C_EXPR_BINARY || array->child_count != 2U ||
        array->rank == 0U)
        return NULL;
    relation = relation_code(array->operator_kind);
    if (relation < 0)
        return NULL;
    *matched = 1;
    left = array->children[0];
    right = array->children[1];
    if (left == NULL || right == NULL || (left->rank == 0U && right->rank == 0U) ||
        left->type != right->type || left->type_kind != right->type_kind)
        goto unsupported;
    if (left->type == TYPE_CHARACTER)
        return character_relation_reduction(unit, left, right, relation, reduction, supported);
    if (left->type == TYPE_DERIVED ||
        ((left->type == TYPE_COMPLEX || left->type == TYPE_DOUBLE_COMPLEX) && relation > 1) ||
        !operand_view(unit, left, &left_view, supported) ||
        !operand_view(unit, right, &right_view, supported)) {
        goto unsupported;
    }
    if (((left_view.storage_qualifiers | right_view.storage_qualifiers) & F2C_STORAGE_VOLATILE) !=
        0U) {
        const char *suffix = kernel_type_suffix(left);
        if (suffix == NULL)
            goto unsupported;
        f2c_buffer_printf(&result, "f2c_relation_reduce_%s_volatile", suffix);
    } else {
        f2c_buffer_append(&result, "F2C_RELATION_REDUCE");
    }
    f2c_buffer_printf(&result, "(%s, %s, %s, %s, %s, %s, %d, %d)", left_view.pointer,
                      left_view.stride, left_view.count, right_view.pointer, right_view.stride,
                      right_view.count, relation, reduction);
    f2c_array_view_discard(&left_view);
    f2c_array_view_discard(&right_view);
    return f2c_buffer_take(&result);

unsupported:
    f2c_array_view_discard(&left_view);
    f2c_array_view_discard(&right_view);
    free(result.data);
    *supported = 0;
    return NULL;
}

static const char *reduction_macro(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_ALL:
        return "f2c_all_l";
    case F2C_INTRINSIC_ANY:
        return "f2c_any_l";
    case F2C_INTRINSIC_COUNT:
        return "f2c_count_l";
    case F2C_INTRINSIC_MAXLOC:
        return "F2C_MAXIMUM_LOCATION";
    case F2C_INTRINSIC_MAXVAL:
        return "F2C_MAXIMUM";
    case F2C_INTRINSIC_MINLOC:
        return "F2C_MINIMUM_LOCATION";
    case F2C_INTRINSIC_MINVAL:
        return "F2C_MINIMUM";
    case F2C_INTRINSIC_PRODUCT:
        return "F2C_PRODUCT";
    case F2C_INTRINSIC_SUM:
        return "F2C_SUM";
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_DOT_PRODUCT:
    default:
        return NULL;
    }
}

static const char *masked_reduction_macro(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_MAXLOC:
        return "F2C_MAXIMUM_LOCATION_MASK";
    case F2C_INTRINSIC_MAXVAL:
        return "F2C_MAXIMUM_MASK";
    case F2C_INTRINSIC_MINLOC:
        return "F2C_MINIMUM_LOCATION_MASK";
    case F2C_INTRINSIC_MINVAL:
        return "F2C_MINIMUM_MASK";
    case F2C_INTRINSIC_PRODUCT:
        return "F2C_PRODUCT_MASK";
    case F2C_INTRINSIC_SUM:
        return "F2C_SUM_MASK";
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_ALL:
    case F2C_INTRINSIC_ANY:
    case F2C_INTRINSIC_COUNT:
    case F2C_INTRINSIC_DOT_PRODUCT:
    default:
        return NULL;
    }
}

static char *reduction_conformance(Unit *unit, const F2cExpr *array, const F2cExpr *mask,
                                   const char *array_count, const char *mask_count) {
    Buffer result = {0};
    size_t dimension;
    f2c_buffer_printf(&result, "((%s) == (%s)", array_count, mask_count);
    for (dimension = 0U; dimension < array->rank; ++dimension) {
        char *array_extent = f2c_array_expression_extent(unit, array, dimension);
        char *mask_extent = f2c_array_expression_extent(unit, mask, dimension);
        if (array_extent == NULL || mask_extent == NULL) {
            free(array_extent);
            free(mask_extent);
            free(result.data);
            return NULL;
        }
        f2c_buffer_printf(&result, " && ((size_t)(%s) == (size_t)(%s))", array_extent, mask_extent);
        free(array_extent);
        free(mask_extent);
    }
    f2c_buffer_append(&result, ")");
    return f2c_buffer_take(&result);
}

static const char *reduction_type_code(const F2cExpr *expression) {
    const int kind = expression != NULL && expression->type_kind != 0
                         ? expression->type_kind
                         : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
    if (expression == NULL)
        return NULL;
    switch (expression->type) {
    case TYPE_INTEGER:
        return kind == 1   ? "F2C_REDUCTION_I8"
               : kind == 2 ? "F2C_REDUCTION_I16"
               : kind == 4 ? "F2C_REDUCTION_I32"
               : kind == 8 ? "F2C_REDUCTION_I64"
                           : NULL;
    case TYPE_REAL:
        return "F2C_REDUCTION_F";
    case TYPE_DOUBLE:
        return "F2C_REDUCTION_D";
    case TYPE_COMPLEX:
        return "F2C_REDUCTION_C";
    case TYPE_DOUBLE_COMPLEX:
        return "F2C_REDUCTION_Z";
    case TYPE_UNKNOWN:
    case TYPE_LOGICAL:
    case TYPE_CHARACTER:
    case TYPE_DERIVED:
    default:
        return NULL;
    }
}

static const char *dot_product_helper(const F2cExpr *expression) {
    const int kind = expression != NULL && expression->type_kind != 0
                         ? expression->type_kind
                         : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
    if (expression == NULL)
        return NULL;
    switch (expression->type) {
    case TYPE_INTEGER:
        return kind == 1   ? "f2c_dot_i8"
               : kind == 2 ? "f2c_dot_i16"
               : kind == 4 ? "f2c_dot_i32"
               : kind == 8 ? "f2c_dot_i64"
                           : NULL;
    case TYPE_REAL:
        return "f2c_dot_f";
    case TYPE_DOUBLE:
        return "f2c_dot_d";
    case TYPE_COMPLEX:
        return "f2c_dot_c";
    case TYPE_DOUBLE_COMPLEX:
        return "f2c_dot_z";
    case TYPE_LOGICAL:
        return "f2c_dot_l";
    case TYPE_UNKNOWN:
    case TYPE_CHARACTER:
    case TYPE_DERIVED:
    default:
        return NULL;
    }
}

static char *dot_product(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *left_array =
        f2c_intrinsic_argument(expression->children, expression->child_count, "vector_a", 0U);
    const F2cExpr *right_array =
        f2c_intrinsic_argument(expression->children, expression->child_count, "vector_b", 1U);
    F2cArrayView left_view = {0};
    F2cArrayView right_view = {0};
    char *zero = NULL;
    const char *helper = dot_product_helper(expression);
    int qualified;
    Buffer result = {0};
    if (helper == NULL || !f2c_array_view(unit, left_array, &left_view, supported) ||
        !f2c_array_view(unit, right_array, &right_view, supported)) {
        goto unsupported;
    }
    qualified = ((left_view.storage_qualifiers | right_view.storage_qualifiers) &
                 F2C_STORAGE_VOLATILE) != 0U;
    if (expression->type == TYPE_LOGICAL) {
        f2c_buffer_printf(&result,
                          "((%s) == (%s) ? %s%s((const %svoid *)(%s), sizeof(*(%s)), %s, "
                          "(const %svoid *)(%s), sizeof(*(%s)), %s, %s) : "
                          "(abort(), false))",
                          left_view.count, right_view.count, helper, qualified ? "_volatile" : "",
                          qualified ? "volatile " : "", left_view.pointer, left_view.pointer,
                          left_view.stride, qualified ? "volatile " : "", right_view.pointer,
                          right_view.pointer, right_view.stride, left_view.count);
    } else {
        const char *left_type = reduction_type_code(left_array);
        const char *right_type = reduction_type_code(right_array);
        if (left_type == NULL || right_type == NULL)
            goto unsupported;
        zero = reduction_zero_value(expression);
        if (zero == NULL)
            goto unsupported;
        f2c_buffer_printf(&result,
                          "((%s) == (%s) ? %s%s((const %svoid *)(%s), %s, %s, "
                          "(const %svoid *)(%s), %s, %s, %s) : "
                          "(abort(), %s))",
                          left_view.count, right_view.count, helper, qualified ? "_volatile" : "",
                          qualified ? "volatile " : "", left_view.pointer, left_type,
                          left_view.stride, qualified ? "volatile " : "", right_view.pointer,
                          right_type, right_view.stride, left_view.count, zero);
    }
    f2c_array_view_discard(&left_view);
    f2c_array_view_discard(&right_view);
    free(zero);
    return f2c_buffer_take(&result);

unsupported:
    f2c_array_view_discard(&left_view);
    f2c_array_view_discard(&right_view);
    free(zero);
    free(result.data);
    *supported = 0;
    return NULL;
}

char *f2c_expression_reduction_intrinsic(Unit *unit, const F2cExpr *expression, int *supported) {
    const int logical = expression != NULL && (expression->intrinsic == F2C_INTRINSIC_ALL ||
                                               expression->intrinsic == F2C_INTRINSIC_ANY ||
                                               expression->intrinsic == F2C_INTRINSIC_COUNT);
    const F2cExpr *array;
    const F2cExpr *dimension;
    const F2cExpr *mask;
    const F2cExpr *kind;
    const F2cExpr *back;
    const char *macro;
    const int integer_result =
        expression != NULL && (expression->intrinsic == F2C_INTRINSIC_COUNT ||
                               expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
                               expression->intrinsic == F2C_INTRINSIC_MINLOC);
    F2cArrayView array_view = {0};
    char *dimension_code = NULL;
    F2cArrayView mask_view = {0};
    char *mask_size = NULL;
    char *mask_scalar = NULL;
    char *conformance = NULL;
    char *back_code = NULL;
    char *zero = NULL;
    Buffer result = {0};
    Buffer qualified_kernel = {0};
    int qualified;
    if (expression == NULL || !f2c_intrinsic_is_reduction(expression->intrinsic)) {
        *supported = 0;
        return NULL;
    }
    if (expression->intrinsic == F2C_INTRINSIC_DOT_PRODUCT)
        return dot_product(unit, expression, supported);
    if (expression->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    array = f2c_intrinsic_argument(expression->children, expression->child_count,
                                   logical ? "mask" : "array", 0U);
    dimension = f2c_intrinsic_argument(expression->children, expression->child_count, "dim", 1U);
    mask = logical
               ? NULL
               : f2c_intrinsic_argument(expression->children, expression->child_count, "mask", 2U);
    kind = expression->intrinsic == F2C_INTRINSIC_COUNT
               ? f2c_intrinsic_argument(expression->children, expression->child_count, "kind", 2U)
               : NULL;
    back = expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
                   expression->intrinsic == F2C_INTRINSIC_MINLOC
               ? f2c_intrinsic_argument(expression->children, expression->child_count, "back", 4U)
               : NULL;
    (void)kind;
    macro = logical ? reduction_macro(expression->intrinsic)
                    : masked_reduction_macro(expression->intrinsic);
    if (macro == NULL || !f2c_array_view(unit, array, &array_view, supported))
        goto unsupported;
    if (mask == NULL || mask->rank == 0U) {
        mask_view.pointer = f2c_strdup("NULL");
        mask_view.count = f2c_strdup(array_view.count);
        mask_view.stride = f2c_strdup("0");
        mask_size = f2c_strdup("1U");
        mask_scalar =
            mask != NULL ? f2c_expression_emit(unit, mask, supported) : f2c_strdup("true");
    } else {
        if (!f2c_array_view(unit, mask, &mask_view, supported))
            goto unsupported;
        {
            Buffer size = {0};
            f2c_buffer_printf(&size, "sizeof(*(%s))", mask_view.pointer);
            mask_size = f2c_buffer_take(&size);
        }
        mask_scalar = f2c_strdup("true");
        conformance = reduction_conformance(unit, array, mask, array_view.count, mask_view.count);
    }
    back_code = back != NULL ? f2c_expression_emit(unit, back, supported) : f2c_strdup("false");
    if (dimension != NULL)
        dimension_code = f2c_expression_emit(unit, dimension, supported);
    if (!*supported || mask_view.pointer == NULL || mask_view.count == NULL ||
        mask_view.stride == NULL || mask_size == NULL || mask_scalar == NULL || back_code == NULL ||
        (mask != NULL && mask->rank != 0U && conformance == NULL) ||
        (dimension != NULL && dimension_code == NULL))
        goto unsupported;
    qualified = ((array_view.storage_qualifiers | mask_view.storage_qualifiers) &
                 F2C_STORAGE_VOLATILE) != 0U;
    if (qualified) {
        const char *suffix = logical ? "l" : kernel_type_suffix(array);
        if (suffix == NULL || expression->text == NULL)
            goto unsupported;
        f2c_buffer_printf(&qualified_kernel, "f2c_%s%s_%s_volatile", expression->text,
                          logical ? "" : "_mask", suffix);
        macro = qualified_kernel.data;
    }
    if (dimension_code != NULL || conformance != NULL) {
        f2c_buffer_append(&result, "((");
        if (dimension_code != NULL)
            f2c_buffer_printf(&result, "(%s) == 1", dimension_code);
        if (dimension_code != NULL && conformance != NULL)
            f2c_buffer_append(&result, " && ");
        if (conformance != NULL)
            f2c_buffer_append(&result, conformance);
        f2c_buffer_append(&result, ") ? ");
    }
    if (integer_result)
        f2c_buffer_printf(&result, "((%s)f2c_reduction_integer_result((int64_t)(",
                          f2c_expression_c_type(expression));
    if (logical) {
        f2c_buffer_printf(&result, "%s((const %svoid *)(%s), sizeof(*(%s)), %s, %s)", macro,
                          qualified ? "volatile " : "", array_view.pointer, array_view.pointer,
                          array_view.count, array_view.stride);
    } else {
        f2c_buffer_printf(&result, "%s(%s, %s, %s, (const %svoid *)(%s), %s, %s, (%s)", macro,
                          array_view.pointer, array_view.count, array_view.stride,
                          qualified ? "volatile " : "", mask_view.pointer, mask_size,
                          mask_view.stride, mask_scalar);
        if (expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
            expression->intrinsic == F2C_INTRINSIC_MINLOC)
            f2c_buffer_printf(&result, ", (%s)", back_code);
        f2c_buffer_append(&result, ")");
    }
    if (integer_result)
        f2c_buffer_printf(&result, "), %d))",
                          expression->type_kind != 0 ? expression->type_kind
                                                     : f2c_default_kind(TYPE_INTEGER));
    if (dimension_code != NULL || conformance != NULL) {
        zero = reduction_zero_value(expression);
        if (zero == NULL)
            goto unsupported;
        f2c_buffer_printf(&result, " : (abort(), %s))", zero);
    }
    f2c_array_view_discard(&array_view);
    free(dimension_code);
    f2c_array_view_discard(&mask_view);
    free(mask_size);
    free(mask_scalar);
    free(conformance);
    free(back_code);
    free(zero);
    free(qualified_kernel.data);
    return f2c_buffer_take(&result);

unsupported:
    f2c_array_view_discard(&array_view);
    free(dimension_code);
    f2c_array_view_discard(&mask_view);
    free(mask_size);
    free(mask_scalar);
    free(conformance);
    free(back_code);
    free(zero);
    free(qualified_kernel.data);
    free(result.data);
    *supported = 0;
    return NULL;
}
