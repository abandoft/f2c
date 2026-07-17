#include "codegen/transform/private.h"

#include <stdlib.h>
#include <string.h>

static int matmul_types_compatible(const Symbol *target, const F2cExpr *call,
                                   const TransformArray *left, const TransformArray *right) {
    if (target == NULL || call == NULL || left == NULL || right == NULL)
        return 0;
    if (left->type == TYPE_LOGICAL || right->type == TYPE_LOGICAL)
        return left->type == TYPE_LOGICAL && right->type == TYPE_LOGICAL &&
               target->type == TYPE_LOGICAL;
    return f2c_type_is_numeric(left->type) && f2c_type_is_numeric(right->type) &&
           target->type == call->type && target->kind == call->type_kind;
}

static char *matmul_initial_value(Type type) {
    if (type == TYPE_COMPLEX)
        return f2c_strdup("f2c_make_c(0.0f, 0.0f)");
    if (type == TYPE_DOUBLE_COMPLEX)
        return f2c_strdup("f2c_make_z(0.0, 0.0)");
    return f2c_strdup(type == TYPE_LOGICAL ? "false" : "0");
}

static char *matmul_update(Unit *unit, const Symbol *target, const TransformArray *left,
                           const TransformArray *right, const char *left_index,
                           const char *right_index) {
    Buffer left_value = {0};
    Buffer right_value = {0};
    Type product_type = TYPE_UNKNOWN;
    Type result_type = TYPE_UNKNOWN;
    char *product;
    char *update;
    if (target->type == TYPE_LOGICAL) {
        Buffer logical = {0};
        f2c_buffer_printf(&logical, "(f2c_transform_value || (%s[%s] && %s[%s]))", left->pointer,
                          left_index, right->pointer, right_index);
        return f2c_buffer_take(&logical);
    }
    f2c_buffer_printf(&left_value, "%s[%s]", left->pointer, left_index);
    f2c_buffer_printf(&right_value, "%s[%s]", right->pointer, right_index);
    product = f2c_emit_binary(unit, left_value.data, left->type, "*", right_value.data, right->type,
                              &product_type);
    free(left_value.data);
    free(right_value.data);
    if (product == NULL)
        return NULL;
    update = f2c_emit_binary(unit, "f2c_transform_value", target->type, "+", product, product_type,
                             &result_type);
    free(product);
    if (result_type != target->type) {
        free(update);
        return NULL;
    }
    return update;
}

static int emit_transpose(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                          size_t line, int depth) {
    const F2cExpr *source_expression = f2c_transform_argument(call, "matrix", 0U);
    TransformArray source = {0};
    if (target->rank != 2U || !f2c_transform_array_view(unit, source_expression, &source) ||
        source.rank != 2U || !f2c_transform_compatible_array(target, &source) ||
        target->kind != source_expression->type_kind) {
        f2c_diagnostic(context, line, 1,
                       "TRANSPOSE requires a conforming rank-two array and result target");
        f2c_transform_free_array(&source);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_transform_materialize_array(context, unit, &source, "transpose_source", depth + 1)) {
        f2c_diagnostic(context, line, 1, "TRANSPOSE array expression could not be materialized");
        f2c_transform_free_array(&source);
        return 1;
    }
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const size_t f2c_transform_result_extent_1 = (size_t)(%s);\n",
                      source.extents[1]);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const size_t f2c_transform_result_extent_2 = (size_t)(%s);\n",
                      source.extents[0]);
    f2c_transform_emit_result_count(context, 2U, depth + 1);
    f2c_transform_emit_result_allocation(context, unit, target, source_expression, depth + 1);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "for (size_t f2c_transform_output = 0U; "
                      "f2c_transform_output < f2c_transform_result_count; "
                      "++f2c_transform_output) { size_t f2c_transform_row = "
                      "f2c_transform_output % f2c_transform_result_extent_1; size_t "
                      "f2c_transform_column = f2c_transform_output / "
                      "f2c_transform_result_extent_1; size_t f2c_transform_source_index = "
                      "f2c_transform_column + f2c_transform_result_extent_2 * "
                      "f2c_transform_row; ");
    f2c_transform_append_array_store(&context->output, target, "f2c_transform_output", &source,
                                     "f2c_transform_source_index");
    f2c_buffer_append(&context->output, "}\n");
    f2c_transform_emit_array_cleanup(context, &source, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, 2U, depth + 1);
    f2c_transform_free_array(&source);
    return 1;
}

static int prepare_matmul(Context *context, Unit *unit, const Symbol *target, const F2cExpr *call,
                          size_t line, TransformArray *left, TransformArray *right) {
    const F2cExpr *left_expression = f2c_transform_argument(call, "matrix_a", 0U);
    const F2cExpr *right_expression = f2c_transform_argument(call, "matrix_b", 1U);
    if (!f2c_transform_array_view(unit, left_expression, left) ||
        !f2c_transform_array_view(unit, right_expression, right) || left->rank < 1U ||
        left->rank > 2U || right->rank < 1U || right->rank > 2U ||
        !matmul_types_compatible(target, call, left, right)) {
        f2c_diagnostic(context, line, 1,
                       "MATMUL requires compatible rank-one or rank-two numeric/LOGICAL arrays");
        f2c_transform_free_array(left);
        f2c_transform_free_array(right);
        return 0;
    }
    return 1;
}

static void emit_matmul_extents(Context *context, const TransformArray *left,
                                const TransformArray *right, int depth) {
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_left_rows = (size_t)(%s);\n",
                      left->rank == 2U ? left->extents[0] : "1U");
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_left_inner = (size_t)(%s);\n",
                      left->extents[left->rank - 1U]);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_right_inner = (size_t)(%s);\n",
                      right->extents[0]);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "const size_t f2c_transform_right_columns = (size_t)(%s);\n",
                      right->rank == 2U ? right->extents[1] : "1U");
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "(void)f2c_transform_left_rows; "
                                        "(void)f2c_transform_right_columns;\n");
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "if (f2c_transform_left_inner != f2c_transform_right_inner) abort();\n");
}

static int emit_matmul_loop(Context *context, Unit *unit, const Symbol *target,
                            const TransformArray *left, const TransformArray *right, int depth) {
    const char *left_index = left->rank == 2U ? "f2c_transform_row + f2c_transform_left_rows * "
                                                "f2c_transform_inner_index"
                                              : "f2c_transform_inner_index";
    const char *right_index = right->rank == 2U
                                  ? "f2c_transform_inner_index + f2c_transform_right_inner * "
                                    "f2c_transform_column"
                                  : "f2c_transform_inner_index";
    char *initial = matmul_initial_value(target->type);
    char *update = matmul_update(unit, target, left, right, left_index, right_index);
    if (initial == NULL || update == NULL) {
        free(initial);
        free(update);
        return 0;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "for (size_t f2c_transform_column = 0U; f2c_transform_column < "
                      "f2c_transform_right_columns; ++f2c_transform_column) {\n");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "for (size_t f2c_transform_row = 0U; f2c_transform_row < "
                                        "f2c_transform_left_rows; ++f2c_transform_row) {\n");
    depth += 2;
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s f2c_transform_value = %s;\n", f2c_symbol_c_type(target),
                      initial);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "for (size_t f2c_transform_inner_index = 0U; "
                                        "f2c_transform_inner_index < f2c_transform_left_inner; "
                                        "++f2c_transform_inner_index)\n");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "f2c_transform_value = %s;\n", update);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "f2c_transform_result[f2c_transform_row + "
                                        "f2c_transform_left_rows * f2c_transform_column] = "
                                        "f2c_transform_value;\n");
    f2c_transform_indent(&context->output, depth - 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_transform_indent(&context->output, depth - 2);
    f2c_buffer_append(&context->output, "}\n");
    free(initial);
    free(update);
    return 1;
}

static int emit_matmul_array(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                             size_t line, int depth) {
    TransformArray left = {0};
    TransformArray right = {0};
    const F2cExpr *element_source = f2c_transform_argument(call, "matrix_a", 0U);
    if (!prepare_matmul(context, unit, target, call, line, &left, &right))
        return 1;
    if (call->rank == 0U || call->rank != target->rank) {
        f2c_diagnostic(context, line, 1, "MATMUL result rank does not conform to its target");
        f2c_transform_free_array(&left);
        f2c_transform_free_array(&right);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_transform_materialize_array(context, unit, &left, "matmul_left", depth + 1) ||
        !f2c_transform_materialize_array(context, unit, &right, "matmul_right", depth + 1)) {
        f2c_diagnostic(context, line, 1, "MATMUL array expression could not be materialized");
        f2c_transform_free_array(&left);
        f2c_transform_free_array(&right);
        return 1;
    }
    emit_matmul_extents(context, &left, &right, depth + 1);
    f2c_transform_indent(&context->output, depth + 1);
    if (call->rank == 2U) {
        f2c_buffer_append(&context->output, "const size_t f2c_transform_result_extent_1 = "
                                            "f2c_transform_left_rows;\n");
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "const size_t f2c_transform_result_extent_2 = "
                                            "f2c_transform_right_columns;\n");
    } else {
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_result_extent_1 = %s;\n",
                          left.rank == 2U ? "f2c_transform_left_rows"
                                          : "f2c_transform_right_columns");
    }
    f2c_transform_emit_result_count(context, call->rank, depth + 1);
    f2c_transform_emit_result_allocation(context, unit, target, element_source, depth + 1);
    if (!emit_matmul_loop(context, unit, target, &left, &right, depth + 1)) {
        f2c_diagnostic(context, line, 1, "MATMUL element operation could not be lowered");
        f2c_transform_free_array(&left);
        f2c_transform_free_array(&right);
        return 1;
    }
    f2c_transform_emit_array_cleanup(context, &left, depth + 1);
    f2c_transform_emit_array_cleanup(context, &right, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, call->rank, depth + 1);
    f2c_transform_free_array(&left);
    f2c_transform_free_array(&right);
    return 1;
}

int f2c_transform_emit_matrix(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                              size_t line, int depth) {
    if (call == NULL || call->text == NULL)
        return 0;
    if (strcmp(call->text, "transpose") == 0)
        return emit_transpose(context, unit, target, call, line, depth);
    if (strcmp(call->text, "matmul") == 0)
        return emit_matmul_array(context, unit, target, call, line, depth);
    return 0;
}
