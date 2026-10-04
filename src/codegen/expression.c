#include "codegen/expression/private.h"

#include "codegen/descriptor/private.h"
#include "codegen/lowering/private.h"
#include "codegen/operator.h"
#include "codegen/type/initialization.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int emit_array_indices(Unit *unit, const F2cExpr *expression, char ***indices_out,
                              Type **types_out, int *supported) {
    char **indices = expression->child_count != 0U
                         ? (char **)calloc(expression->child_count, sizeof(*indices))
                         : NULL;
    Type *types = expression->child_count != 0U
                      ? (Type *)malloc(expression->child_count * sizeof(*types))
                      : NULL;
    size_t i;
    if (expression->symbol == NULL ||
        (expression->child_count != 0U && (indices == NULL || types == NULL))) {
        free(indices);
        free(types);
        *supported = 0;
        return 0;
    }
    for (i = 0U; i < expression->child_count; ++i) {
        if (expression->children[i]->kind == F2C_EXPR_ARRAY_SECTION) {
            const F2cExpr *section = expression->children[i];
            if (section->child_count != 3U) {
                f2c_expression_free_arguments(indices, types, expression->child_count);
                *supported = 0;
                return 0;
            }
            if (section->children[0]->kind == F2C_EXPR_INVALID)
                indices[i] = i < expression->symbol->rank
                                 ? f2c_symbol_dimension_lower(unit, expression->symbol, i)
                                 : f2c_strdup("1");
            else
                indices[i] = f2c_expression_emit(unit, section->children[0], supported);
            types[i] = TYPE_INTEGER;
        } else {
            indices[i] = f2c_expression_emit(unit, expression->children[i], supported);
            types[i] = expression->children[i]->type;
        }
        if (!*supported || indices[i] == NULL) {
            f2c_expression_free_arguments(indices, types, expression->child_count);
            *supported = 0;
            return 0;
        }
    }
    *indices_out = indices;
    *types_out = types;
    return 1;
}

char *f2c_expression_emit_array_reference(Unit *unit, const F2cExpr *expression, int *supported) {
    char **indices = NULL;
    Type *types = NULL;
    char *result;
    if (!emit_array_indices(unit, expression, &indices, &types, supported))
        return NULL;
    result = f2c_emit_array_reference(unit, expression->symbol, indices, expression->child_count);
    f2c_expression_free_arguments(indices, types, expression->child_count);
    if (result == NULL)
        *supported = 0;
    return result;
}

char *f2c_expression_array_storage(Unit *unit, const F2cExpr *expression, int *supported) {
    char **indices = NULL;
    Type *types = NULL;
    char *result;
    if (!emit_array_indices(unit, expression, &indices, &types, supported))
        return NULL;
    result = f2c_emit_array_storage_reference(unit, expression->symbol, indices,
                                              expression->child_count);
    f2c_expression_free_arguments(indices, types, expression->child_count);
    if (result == NULL)
        *supported = 0;
    return result;
}

char *f2c_emit_unaligned_designator_address(Unit *unit, const F2cExpr *expression, int *supported) {
    char **indices = NULL;
    Type *types = NULL;
    char *result;
    if (supported == NULL)
        return NULL;
    *supported = 1;
    if (expression == NULL || expression->symbol == NULL ||
        !expression->symbol->equivalence_unaligned ||
        (expression->kind != F2C_EXPR_NAME && expression->kind != F2C_EXPR_ARRAY_REFERENCE)) {
        *supported = 0;
        return NULL;
    }
    if (expression->kind == F2C_EXPR_NAME)
        return f2c_emit_unaligned_address(unit, expression->symbol, NULL, 0U);
    if (!emit_array_indices(unit, expression, &indices, &types, supported))
        return NULL;
    result = f2c_emit_unaligned_address(unit, expression->symbol, indices, expression->child_count);
    f2c_expression_free_arguments(indices, types, expression->child_count);
    if (result == NULL)
        *supported = 0;
    return result;
}

static char *emit_array_constructor(Unit *unit, const F2cExpr *expression, int *supported) {
    char **elements = NULL;
    Type *types = NULL;
    Buffer result = {0};
    size_t i;
    if (!f2c_expression_children(unit, expression, &elements, &types)) {
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "(%s[%zu]){", f2c_expression_c_type(expression),
                      expression->child_count);
    for (i = 0U; i < expression->child_count; ++i)
        f2c_buffer_printf(&result, "%s%s", i == 0U ? "" : ", ", elements[i]);
    f2c_buffer_append(&result, "}");
    f2c_expression_free_arguments(elements, types, expression->child_count);
    return f2c_buffer_take(&result);
}

static char *emit_structure_constructor(Unit *unit, const F2cExpr *expression, int *supported) {
    Buffer result = {0};
    char *initializer = f2c_derived_constructor_initializer(unit, expression);
    if (initializer == NULL) {
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "((%s)%s)", expression->derived_type->c_name, initializer);
    free(initializer);
    return f2c_buffer_take(&result);
}

char *f2c_expression_emit(Unit *unit, const F2cExpr *expression, int *supported) {
    Buffer result = {0};
    const char *lowered_code;
    char *left;
    char *right;
    if (expression == NULL) {
        *supported = 0;
        return NULL;
    }
    lowered_code = f2c_lowering_code(unit, expression);
    if (lowered_code != NULL)
        return f2c_expression_apply_access(unit, expression, f2c_strdup(lowered_code));
    if (expression->resolved_procedure != NULL &&
        (expression->kind == F2C_EXPR_UNARY || expression->kind == F2C_EXPR_BINARY))
        return f2c_expression_call(unit, expression, supported);
    switch (expression->kind) {
    case F2C_EXPR_INTEGER_LITERAL:
        if (expression->text != NULL &&
            ((tolower((unsigned char)expression->text[0]) == 'b' ||
              tolower((unsigned char)expression->text[0]) == 'o' ||
              tolower((unsigned char)expression->text[0]) == 'z' ||
              tolower((unsigned char)expression->text[0]) == 'x') &&
             (expression->text[1] == '\'' || expression->text[1] == '"'))) {
            char *boz = f2c_expression_boz_literal(expression->text);
            if (boz == NULL)
                *supported = 0;
            return boz;
        }
        return f2c_expression_integer_literal(expression);
    case F2C_EXPR_REAL_LITERAL:
        return f2c_expression_real_literal(expression);
    case F2C_EXPR_STRING_LITERAL:
        return f2c_expression_string_literal(expression->text);
    case F2C_EXPR_LOGICAL_LITERAL:
        return f2c_strdup(strcmp(expression->text, ".true.") == 0 ? "true" : "false");
    case F2C_EXPR_NAME:
        return f2c_expression_apply_access(unit, expression,
                                           f2c_expression_name(unit, expression, supported));
    case F2C_EXPR_PARENTHESIZED:
        return f2c_expression_parenthesized(unit, expression, supported);
    case F2C_EXPR_COMPONENT:
        return f2c_expression_emit_component(unit, expression, supported);
    case F2C_EXPR_UNARY:
        left = f2c_expression_emit(unit, expression->children[0], supported);
        if (!*supported || left == NULL)
            return NULL;
        {
            const F2cScalarOperand operand = {left,
                                              f2c_expression_scalar_type(expression->children[0])};
            const F2cScalarOperand absent = {NULL, {TYPE_UNKNOWN, 0}};
            char *unary = f2c_emit_scalar_operator(expression->operator_kind, 1, operand, absent,
                                                   f2c_expression_scalar_type(expression));
            free(left);
            if (unary == NULL)
                *supported = 0;
            return unary;
        }
    case F2C_EXPR_BINARY:
        left = f2c_expression_emit(unit, expression->children[0], supported);
        right = *supported ? f2c_expression_emit(unit, expression->children[1], supported) : NULL;
        if (!*supported || left == NULL || right == NULL) {
            free(left);
            free(right);
            return NULL;
        }
        {
            const int order_left = expression->children[0]->has_order_sensitive_call;
            const int order_right = expression->children[1]->has_order_sensitive_call;
            const int ordered =
                expression->ordered_temporary_index != SIZE_MAX && (order_left || order_right);
            Buffer temporary = {0};
            Buffer sequenced = {0};
            const char *left_value = left;
            const char *right_value = right;
            const char *ordered_code = order_left ? left : right;
            char *binary;
            if (ordered) {
                f2c_buffer_printf(&temporary, "f2c_ordered_value_%zu",
                                  expression->ordered_temporary_index);
                if (temporary.data == NULL) {
                    free(left);
                    free(right);
                    *supported = 0;
                    return NULL;
                }
                if (order_left)
                    left_value = temporary.data;
                else
                    right_value = temporary.data;
            }
            binary = f2c_emit_character_concatenation(unit, expression, left_value, right_value);
            if (binary == NULL)
                binary = f2c_emit_character_comparison(unit, expression->children[0], left_value,
                                                       expression->text, expression->children[1],
                                                       right_value);
            if (binary == NULL) {
                const F2cScalarOperand left_operand = {
                    left_value, f2c_expression_scalar_type(expression->children[0])};
                const F2cScalarOperand right_operand = {
                    right_value, f2c_expression_scalar_type(expression->children[1])};
                binary =
                    f2c_emit_scalar_operator(expression->operator_kind, 0, left_operand,
                                             right_operand, f2c_expression_scalar_type(expression));
            }
            if (ordered && binary != NULL) {
                f2c_buffer_printf(&sequenced, "(%s = (%s), %s)", temporary.data, ordered_code,
                                  binary);
                free(binary);
                binary = f2c_buffer_take(&sequenced);
            }
            if (binary == NULL)
                *supported = 0;
            free(left);
            free(right);
            free(temporary.data);
            free(sequenced.data);
            return binary;
        }
    case F2C_EXPR_CALL:
        return f2c_expression_call(unit, expression, supported);
    case F2C_EXPR_ARRAY_REFERENCE:
        return f2c_expression_emit_array_reference(unit, expression, supported);
    case F2C_EXPR_SUBSTRING:
        return f2c_expression_emit_substring(unit, expression, supported);
    case F2C_EXPR_COMPLEX_LITERAL:
        left = f2c_expression_emit(unit, expression->children[0], supported);
        right = *supported ? f2c_expression_emit(unit, expression->children[1], supported) : NULL;
        if (!*supported || left == NULL || right == NULL) {
            free(left);
            free(right);
            return NULL;
        }
        f2c_buffer_printf(&result, "%s((%s)(%s), (%s)(%s))",
                          expression->type_kind == 4   ? "f2c_make_c"
                          : expression->type_kind == 8 ? "f2c_make_z"
                                                       : "f2c_make_q",
                          f2c_c_type_kind(TYPE_REAL, expression->type_kind), left,
                          f2c_c_type_kind(TYPE_REAL, expression->type_kind), right);
        free(left);
        free(right);
        return f2c_buffer_take(&result);
    case F2C_EXPR_ARRAY_CONSTRUCTOR:
        return emit_array_constructor(unit, expression, supported);
    case F2C_EXPR_STRUCTURE_CONSTRUCTOR:
        return emit_structure_constructor(unit, expression, supported);
    case F2C_EXPR_IMPLIED_DO:
        *supported = 0;
        return NULL;
    case F2C_EXPR_KEYWORD_ARGUMENT:
        if (expression->child_count != 1U) {
            *supported = 0;
            return NULL;
        }
        return f2c_expression_emit(unit, expression->children[0], supported);
    case F2C_EXPR_ABSENT_ARGUMENT:
        return f2c_strdup("NULL");
    case F2C_EXPR_ARRAY_SECTION:
    case F2C_EXPR_INVALID:
    default:
        *supported = 0;
        return NULL;
    }
}

char *f2c_emit_pointer_designator(Unit *unit, const F2cExpr *expression, int *supported) {
    Buffer result = {0};
    char *base;
    if (supported != NULL)
        *supported = 1;
    if (expression == NULL || expression->symbol == NULL || !expression->symbol->pointer) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    if (expression->kind == F2C_EXPR_NAME)
        return f2c_strdup(f2c_symbol_c_name(unit, expression->symbol));
    if (expression->kind != F2C_EXPR_COMPONENT || expression->child_count != 1U) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    base = f2c_expression_emit(unit, expression->children[0], supported);
    if (base == NULL || (supported != NULL && !*supported)) {
        free(base);
        return NULL;
    }
    f2c_expression_append_component(&result, base, expression->children[0]->derived_type,
                                    expression->symbol);
    free(base);
    return f2c_buffer_take(&result);
}

char *f2c_emit_expression_ast(Unit *unit, const F2cExpr *expression, int *supported) {
    int local_supported = 1;
    char *result = f2c_expression_emit(unit, expression, &local_supported);
    if (supported != NULL)
        *supported = local_supported;
    if (!local_supported) {
        free(result);
        return NULL;
    }
    return result;
}

char *f2c_emit_typed_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 1;
    char *result;
    if (expression == NULL || expression->parse_error_offset != SIZE_MAX)
        return NULL;
    result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}
