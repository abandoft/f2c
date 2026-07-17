#include "codegen/expression/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const F2cExpr *reduction_argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static int relation_code(const char *operator_text) {
    if (operator_text == NULL)
        return -1;
    if (strcmp(operator_text, "==") == 0 || strcmp(operator_text, ".eq.") == 0 ||
        strcmp(operator_text, ".eqv.") == 0)
        return 0;
    if (strcmp(operator_text, "/=") == 0 || strcmp(operator_text, ".ne.") == 0 ||
        strcmp(operator_text, ".neqv.") == 0)
        return 1;
    if (strcmp(operator_text, "<") == 0 || strcmp(operator_text, ".lt.") == 0)
        return 2;
    if (strcmp(operator_text, "<=") == 0 || strcmp(operator_text, ".le.") == 0)
        return 3;
    if (strcmp(operator_text, ">") == 0 || strcmp(operator_text, ".gt.") == 0)
        return 4;
    if (strcmp(operator_text, ">=") == 0 || strcmp(operator_text, ".ge.") == 0)
        return 5;
    return -1;
}

static int reduction_code(const char *name) {
    if (name != NULL && strcmp(name, "any") == 0)
        return 0;
    if (name != NULL && strcmp(name, "all") == 0)
        return 1;
    if (name != NULL && strcmp(name, "count") == 0)
        return 2;
    return -1;
}

static int scalar_view(Unit *unit, const F2cExpr *expression, char **pointer, char **count,
                       char **stride, int *supported) {
    char *value = f2c_expression_emit(unit, expression, supported);
    Buffer address = {0};
    if (!*supported || value == NULL) {
        free(value);
        return 0;
    }
    f2c_buffer_printf(&address, "(&(%s){(%s)})", f2c_expression_c_type(expression), value);
    free(value);
    *pointer = f2c_buffer_take(&address);
    *count = f2c_strdup("SIZE_MAX");
    *stride = f2c_strdup("0");
    return *pointer != NULL && *count != NULL && *stride != NULL;
}

static int operand_view(Unit *unit, const F2cExpr *expression, char **pointer, char **count,
                        char **stride, int *supported) {
    return expression->rank == 0U
               ? scalar_view(unit, expression, pointer, count, stride, supported)
               : f2c_expression_array_view(unit, expression, pointer, count, stride, supported);
}

typedef struct CharacterOperand {
    char *pointer;
    char *count;
    char *stride;
    char *length;
    int pointer_vector;
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
    f2c_buffer_printf(&pointers, "(const char *[%zu]){", expression->child_count);
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
    operand->pointer_vector = 1;
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
        char *value = f2c_expression_emit(unit, expression, supported);
        operand->pointer = *supported && value != NULL
                               ? f2c_character_source_pointer(unit, expression, value)
                               : NULL;
        operand->length = f2c_character_length_expression(unit, expression);
        operand->count = f2c_strdup("SIZE_MAX");
        operand->stride = f2c_strdup("0");
        free(value);
    } else if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR && expression->child_count == 1U &&
               expression->children[0]->rank != 0U) {
        return character_operand(unit, expression->children[0], operand, supported);
    } else if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        return character_constructor_operand(unit, expression, operand, supported);
    } else {
        operand->length = f2c_character_length_expression(unit, expression);
        if (!f2c_expression_array_view(unit, expression, &operand->pointer, &operand->count,
                                       &operand->stride, supported))
            return 0;
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
    f2c_buffer_printf(&result,
                      "f2c_character_relation_reduce((const void *)(%s), %s, %s, (size_t)(%s), %d, "
                      "(const void *)(%s), %s, %s, (size_t)(%s), %d, %d, %d)",
                      left_operand.pointer, left_operand.stride, left_operand.count,
                      left_operand.length, left_operand.pointer_vector, right_operand.pointer,
                      right_operand.stride, right_operand.count, right_operand.length,
                      right_operand.pointer_vector, relation, reduction);
    free_character_operand(&left_operand);
    free_character_operand(&right_operand);
    return f2c_buffer_take(&result);
}

char *f2c_expression_relation_reduction(Unit *unit, const F2cExpr *expression, int *supported,
                                        int *matched) {
    const F2cExpr *array;
    const F2cExpr *left;
    const F2cExpr *right;
    char *left_pointer = NULL;
    char *left_count = NULL;
    char *left_stride = NULL;
    char *right_pointer = NULL;
    char *right_count = NULL;
    char *right_stride = NULL;
    Buffer result = {0};
    int relation;
    int reduction;
    *matched = 0;
    reduction = expression != NULL ? reduction_code(expression->text) : -1;
    if (reduction < 0 || expression->child_count != 1U)
        return NULL;
    array = reduction_argument_value(expression->children[0]);
    if (array == NULL || array->kind != F2C_EXPR_BINARY || array->child_count != 2U ||
        array->rank == 0U)
        return NULL;
    relation = relation_code(array->text);
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
        !operand_view(unit, left, &left_pointer, &left_count, &left_stride, supported) ||
        !operand_view(unit, right, &right_pointer, &right_count, &right_stride, supported)) {
        goto unsupported;
    }
    f2c_buffer_printf(&result, "F2C_RELATION_REDUCE(%s, %s, %s, %s, %s, %s, %d, %d)", left_pointer,
                      left_stride, left_count, right_pointer, right_stride, right_count, relation,
                      reduction);
    free(left_pointer);
    free(left_count);
    free(left_stride);
    free(right_pointer);
    free(right_count);
    free(right_stride);
    return f2c_buffer_take(&result);

unsupported:
    free(left_pointer);
    free(left_count);
    free(left_stride);
    free(right_pointer);
    free(right_count);
    free(right_stride);
    free(result.data);
    *supported = 0;
    return NULL;
}
