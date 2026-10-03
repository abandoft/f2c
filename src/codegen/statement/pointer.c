#include "codegen/statement/private.h"

#include "codegen/descriptor/private.h"
#include "codegen/lowering/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int index;
    for (index = 0; index < depth; ++index)
        f2c_buffer_append(output, "    ");
}

static int null_pointer_value(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL &&
           expression->intrinsic == F2C_INTRINSIC_NULL;
}

static char *pointer_assignment_designator(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cStorageReference reference = f2c_ir_storage_reference(expression);
    char *designator = f2c_storage_write_property(unit, &reference, F2C_OBJECT_DATA, 0U);
    *supported = designator != NULL;
    return designator;
}

static char *pointer_property_designator(Unit *unit, const F2cExpr *expression,
                                         F2cObjectStateProperty property) {
    const F2cStorageReference reference = f2c_ir_storage_reference(expression);
    return f2c_storage_write_property(unit, &reference, property, 0U);
}

static char *pointer_target_deallocatable(Unit *unit, const F2cExpr *target) {
    const Symbol *symbol;
    const char *descriptor = f2c_lowering_result_descriptor(unit, target);
    if (f2c_expression_has_pointer_result(target) && descriptor != NULL) {
        Buffer property = {0};
        f2c_buffer_printf(&property, "%s.deallocatable", descriptor);
        return f2c_buffer_take(&property);
    }
    if (target == NULL || target->symbol == NULL ||
        (target->kind != F2C_EXPR_NAME && target->kind != F2C_EXPR_COMPONENT))
        return f2c_strdup("false");
    if (target->kind == F2C_EXPR_COMPONENT && target->child_count != 1U)
        return f2c_strdup("false");
    symbol = target->symbol;
    if (!symbol->pointer)
        return f2c_strdup("false");
    const F2cStorageReference reference = f2c_ir_storage_reference(target);
    return f2c_storage_read_property(unit, &reference, F2C_OBJECT_DEALLOCATABLE, 0U);
}

static int emit_character_length_check(Context *context, Unit *unit, const Symbol *pointer,
                                       const F2cExpr *target, int depth) {
    char *pointer_length;
    char *target_length;
    if (pointer == NULL || target == NULL || pointer->type != TYPE_CHARACTER ||
        pointer->deferred_character)
        return 1;
    pointer_length = f2c_symbol_character_length(unit, pointer);
    target_length = f2c_character_length_expression(unit, target);
    if (pointer_length == NULL || target_length == NULL) {
        free(pointer_length);
        free(target_length);
        return 0;
    }
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if ((size_t)(%s) != (size_t)(%s)) abort();\n",
                      pointer_length, target_length);
    free(pointer_length);
    free(target_length);
    return 1;
}

static int emit_pointer_bounds(Context *context, Unit *unit, const F2cStatement *statement,
                               int depth) {
    size_t dimension;
    if (statement->pointer_bounds == F2C_POINTER_BOUNDS_NONE)
        return 1;
    if (statement->left == NULL || (statement->left->kind != F2C_EXPR_ARRAY_REFERENCE &&
                                    statement->left->kind != F2C_EXPR_COMPONENT))
        return 0;
    const size_t selector_offset = statement->left->kind == F2C_EXPR_COMPONENT ? 1U : 0U;
    for (dimension = 0U; dimension < statement->left->symbol->rank; ++dimension) {
        const F2cExpr *selector = statement->left->children[selector_offset + dimension];
        char *lower;
        char *upper = NULL;
        if (selector == NULL || selector->kind != F2C_EXPR_ARRAY_SECTION ||
            selector->child_count != 3U)
            return 0;
        lower = f2c_emit_typed_expression(unit, selector->children[0]);
        if (statement->pointer_bounds == F2C_POINTER_BOUNDS_REMAPPING)
            upper = f2c_emit_typed_expression(unit, selector->children[1]);
        if (lower == NULL ||
            (statement->pointer_bounds == F2C_POINTER_BOUNDS_REMAPPING && upper == NULL)) {
            free(lower);
            free(upper);
            return 0;
        }
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "const int64_t f2c_pointer_bound_lower_%zu = (int64_t)(%s);\n",
                          dimension + 1U, lower);
        if (upper != NULL) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "const int64_t f2c_pointer_bound_upper_%zu = (int64_t)(%s);\n",
                              dimension + 1U, upper);
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_pointer_bound_extent_%zu = "
                              "f2c_section_extent(f2c_pointer_bound_lower_%zu, "
                              "f2c_pointer_bound_upper_%zu, INT64_C(1));\n",
                              dimension + 1U, dimension + 1U, dimension + 1U);
        }
        free(lower);
        free(upper);
    }
    return 1;
}

static void emit_descriptor_count(Context *context, const F2cDescriptorView *view, int depth) {
    size_t dimension;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_pointer_target_count = 1U;\n");
    for (dimension = 0U; dimension < view->rank; ++dimension) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (!f2c_size_multiply(f2c_pointer_target_count, (size_t)(%s), "
                          "&f2c_pointer_target_count)) abort();\n",
                          view->extent[dimension]);
    }
}

static void emit_remapping_validation(Context *context, const F2cStatement *statement,
                                      const F2cDescriptorView *view, int depth) {
    size_t dimension;
    emit_descriptor_count(context, view, depth);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_pointer_remap_count = 1U;\n");
    for (dimension = 0U; dimension < statement->left->symbol->rank; ++dimension) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (!f2c_size_multiply(f2c_pointer_remap_count, "
                          "f2c_pointer_bound_extent_%zu, &f2c_pointer_remap_count)) abort();\n",
                          dimension + 1U);
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "if (f2c_pointer_target_count < f2c_pointer_remap_count) abort();\n");
}

static void emit_contiguous_view_validation(Context *context, const F2cDescriptorView *view,
                                            int depth) {
    size_t dimension;
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (!f2c_descriptor_is_contiguous(%zuU, (const size_t[]){",
                      view->rank);
    for (dimension = 0U; dimension < view->rank; ++dimension)
        f2c_buffer_printf(&context->output, "%s(size_t)(%s)", dimension == 0U ? "" : ", ",
                          view->extent[dimension]);
    f2c_buffer_append(&context->output, "}, (const ptrdiff_t[]){");
    for (dimension = 0U; dimension < view->rank; ++dimension)
        f2c_buffer_printf(&context->output, "%s(ptrdiff_t)(%s)", dimension == 0U ? "" : ", ",
                          view->stride[dimension]);
    f2c_buffer_append(&context->output, "})) abort();\n");
}

static int emit_array_pointer_metadata(Context *context, Unit *unit, const F2cStatement *statement,
                                       const F2cDescriptorView *view, int null_target, int depth) {
    const size_t rank = statement->left->symbol->rank;
    const F2cStorageReference reference = f2c_ir_storage_reference(statement->left);
    size_t dimension;
    if (statement->pointer_bounds == F2C_POINTER_BOUNDS_REMAPPING) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "ptrdiff_t f2c_pointer_remap_stride = (ptrdiff_t)(%s);\n",
                          view->rank == 1U ? view->stride[0] : "1");
    }
    for (dimension = 0U; dimension < rank; ++dimension) {
        Buffer lower = {0};
        Buffer extent = {0};
        const char *stride;
        if (null_target) {
            f2c_buffer_append(&lower, "1");
            f2c_buffer_append(&extent, "0");
            stride = "0";
        } else if (statement->pointer_bounds == F2C_POINTER_BOUNDS_REMAPPING) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (!f2c_default_integer_bounds(f2c_pointer_bound_lower_%zu, "
                              "f2c_pointer_bound_extent_%zu)) abort();\n",
                              dimension + 1U, dimension + 1U);
            f2c_buffer_printf(&lower, "(int32_t)f2c_pointer_bound_lower_%zu", dimension + 1U);
            f2c_buffer_printf(&extent, "(int32_t)f2c_pointer_bound_extent_%zu", dimension + 1U);
            stride = "f2c_pointer_remap_stride";
        } else {
            Buffer checked_lower = {0};
            if (statement->pointer_bounds == F2C_POINTER_BOUNDS_SPECIFICATION) {
                f2c_buffer_printf(&lower, "(int32_t)f2c_pointer_bound_lower_%zu", dimension + 1U);
                f2c_buffer_printf(&checked_lower, "f2c_pointer_bound_lower_%zu", dimension + 1U);
            } else {
                f2c_buffer_printf(&lower, "(int32_t)(%s)", view->lower[dimension]);
                f2c_buffer_append(&checked_lower, view->lower[dimension]);
            }
            f2c_buffer_printf(&extent, "(int32_t)(%s)", view->extent[dimension]);
            stride = view->stride[dimension];
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (!f2c_default_integer_bounds((int64_t)(%s), "
                              "(size_t)(%s))) abort();\n",
                              checked_lower.data, view->extent[dimension]);
            const int failed = checked_lower.failed;
            free(checked_lower.data);
            if (failed) {
                free(lower.data);
                free(extent.data);
                return 0;
            }
        }
        const int emitted =
            !lower.failed && !extent.failed &&
            f2c_storage_emit_store(&context->output, unit, &reference, F2C_OBJECT_LOWER, dimension,
                                   NULL, lower.data, depth) &&
            f2c_storage_emit_store(&context->output, unit, &reference, F2C_OBJECT_EXTENT, dimension,
                                   NULL, extent.data, depth) &&
            f2c_storage_emit_store(&context->output, unit, &reference, F2C_OBJECT_STRIDE, dimension,
                                   NULL, stride, depth);
        free(lower.data);
        free(extent.data);
        if (!emitted)
            return 0;
        if (!null_target && statement->pointer_bounds == F2C_POINTER_BOUNDS_REMAPPING) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "f2c_pointer_remap_stride = f2c_descriptor_stride_extent("
                              "f2c_pointer_remap_stride, f2c_pointer_bound_extent_%zu);\n",
                              dimension + 1U);
        }
    }
    return 1;
}

static int emit_array_pointer_assignment(Context *context, Unit *unit,
                                         const F2cStatement *statement, const char *pointer_name,
                                         int depth) {
    const F2cExpr *target = statement->right;
    const int null_target = null_pointer_value(target);
    F2cDescriptorView view = {0};
    char *character_length = NULL;
    char *deallocatable_name =
        pointer_property_designator(unit, statement->left, F2C_OBJECT_DEALLOCATABLE);
    char *target_deallocatable =
        null_target ? f2c_strdup("false") : pointer_target_deallocatable(unit, target);
    int success = 0;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (deallocatable_name == NULL || target_deallocatable == NULL)
        goto cleanup;
    if (!emit_pointer_bounds(context, unit, statement, depth + 1))
        goto cleanup;
    if (!null_target &&
        !f2c_descriptor_association_view(&context->output, unit, target, depth + 1, &view))
        goto cleanup;
    if (!null_target &&
        !emit_character_length_check(context, unit, statement->left->symbol, target, depth + 1))
        goto cleanup;
    if (!null_target && statement->left->symbol->deferred_character) {
        character_length = f2c_character_length_expression(unit, target);
        if (character_length == NULL)
            goto cleanup;
    }
    if (!null_target && statement->pointer_bounds == F2C_POINTER_BOUNDS_REMAPPING)
        emit_remapping_validation(context, statement, &view, depth + 1);
    if (!null_target &&
        (statement->left->symbol->contiguous ||
         (statement->pointer_bounds == F2C_POINTER_BOUNDS_REMAPPING && view.rank > 1U)))
        emit_contiguous_view_validation(context, &view, depth + 1);
    if (!emit_array_pointer_metadata(context, unit, statement, &view, null_target, depth + 1))
        goto cleanup;
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "%s = %s;\n", pointer_name,
                      null_target ? "NULL" : view.data);
    indent(&context->output, depth + 1);
    if (statement->pointer_bounds == F2C_POINTER_BOUNDS_REMAPPING) {
        f2c_buffer_printf(&context->output,
                          "%s = (%s) && f2c_pointer_target_count == "
                          "f2c_pointer_remap_count;\n",
                          deallocatable_name, target_deallocatable);
    } else {
        f2c_buffer_printf(&context->output, "%s = %s;\n", deallocatable_name, target_deallocatable);
    }
    if (character_length != NULL) {
        char *length_name =
            pointer_property_designator(unit, statement->left, F2C_OBJECT_CHARACTER_LENGTH);
        if (length_name == NULL)
            goto cleanup;
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = (size_t)(%s);\n", length_name, character_length);
        free(length_name);
    } else if (null_target && statement->left->symbol->deferred_character) {
        char *length_name =
            pointer_property_designator(unit, statement->left, F2C_OBJECT_CHARACTER_LENGTH);
        if (length_name == NULL)
            goto cleanup;
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = 0U;\n", length_name);
        free(length_name);
    }
    success = 1;

cleanup:
    free(character_length);
    free(deallocatable_name);
    free(target_deallocatable);
    f2c_descriptor_view_free(&view);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    return success;
}

static void emit_scalar_pointer_store(Context *context, const F2cStatement *statement,
                                      const char *pointer_name, const char *target_address,
                                      int depth) {
    const Symbol *pointer = statement->left != NULL ? statement->left->symbol : NULL;
    indent(&context->output, depth);
    if (pointer != NULL && pointer->polymorphic && pointer->type == TYPE_DERIVED)
        f2c_buffer_printf(&context->output, "%s = (%s *)(void *)(%s);\n", pointer_name,
                          f2c_symbol_c_type(pointer), target_address);
    else
        f2c_buffer_printf(&context->output, "%s = %s;\n", pointer_name, target_address);
}

static int emit_scalar_pointer_assignment(Context *context, Unit *unit,
                                          const F2cStatement *statement, const char *pointer_name,
                                          size_t line, int depth) {
    const F2cExpr *target_expression = statement->right;
    Symbol *target = target_expression != NULL ? target_expression->symbol : NULL;
    const int null_target = null_pointer_value(target_expression);
    char *target_code = NULL;
    char *character_length = NULL;
    char *deallocatable_name =
        pointer_property_designator(unit, statement->left, F2C_OBJECT_DEALLOCATABLE);
    char *target_deallocatable =
        null_target ? f2c_strdup("false") : pointer_target_deallocatable(unit, target_expression);
    if (deallocatable_name == NULL || target_deallocatable == NULL) {
        free(deallocatable_name);
        free(target_deallocatable);
        return 0;
    }
    if (!null_target && target_expression != NULL && target_expression->rank == 0U &&
        (target_expression->kind == F2C_EXPR_ARRAY_REFERENCE ||
         target_expression->kind == F2C_EXPR_SUBSTRING ||
         (target_expression->kind == F2C_EXPR_COMPONENT && target_expression->child_count > 1U)))
        target_code = f2c_emit_statement_expression(context, unit, target_expression, line);
    if (!null_target && !emit_character_length_check(context, unit, statement->left->symbol,
                                                     target_expression, depth)) {
        free(target_code);
        free(deallocatable_name);
        free(target_deallocatable);
        return 0;
    }
    if (null_target) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = NULL;\n", pointer_name);
    } else if (f2c_expression_has_pointer_result(target_expression)) {
        const char *descriptor = f2c_lowering_result_descriptor(unit, target_expression);
        Buffer target_address = {0};
        if (descriptor == NULL) {
            free(target_code);
            free(deallocatable_name);
            free(target_deallocatable);
            return 0;
        }
        f2c_buffer_printf(&target_address, "(%s *)%s.data",
                          f2c_expression_c_type(target_expression), descriptor);
        emit_scalar_pointer_store(context, statement, pointer_name, target_address.data, depth);
        free(target_address.data);
    } else if (target_code != NULL) {
        Buffer target_address = {0};
        f2c_buffer_printf(&target_address, "%s%s%s",
                          target_expression->kind == F2C_EXPR_SUBSTRING ? "" : "&(", target_code,
                          target_expression->kind == F2C_EXPR_SUBSTRING ? "" : ")");
        emit_scalar_pointer_store(context, statement, pointer_name, target_address.data, depth);
        free(target_address.data);
    } else if (target_expression != NULL && target_expression->kind == F2C_EXPR_COMPONENT) {
        const F2cStorageReference reference = f2c_ir_storage_reference(target_expression);
        char *target_designator = f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
        Buffer target_address = {0};
        if (target_designator == NULL) {
            free(target_code);
            free(deallocatable_name);
            free(target_deallocatable);
            return 0;
        }
        f2c_buffer_printf(
            &target_address, "%s%s",
            target->pointer || target->allocatable || target->type == TYPE_CHARACTER ? "" : "&",
            target_designator);
        emit_scalar_pointer_store(context, statement, pointer_name, target_address.data, depth);
        free(target_address.data);
        free(target_designator);
    } else if (target != NULL) {
        Buffer target_address = {0};
        char *data = f2c_storage_symbol_data(unit, target);
        if (data == NULL) {
            free(target_code);
            free(deallocatable_name);
            free(target_deallocatable);
            return 0;
        }
        f2c_buffer_printf(&target_address, "%s%s",
                          target->pointer || target->allocatable || target->argument ||
                                  target->type == TYPE_CHARACTER
                              ? ""
                              : "&",
                          data);
        emit_scalar_pointer_store(context, statement, pointer_name, target_address.data, depth);
        free(target_address.data);
        free(data);
    } else {
        free(target_code);
        free(deallocatable_name);
        free(target_deallocatable);
        return 0;
    }
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s = %s;\n", deallocatable_name, target_deallocatable);
    if (!null_target && statement->left->symbol->deferred_character) {
        char *length_name;
        character_length = f2c_character_length_expression(unit, target_expression);
        if (character_length == NULL) {
            free(target_code);
            free(deallocatable_name);
            free(target_deallocatable);
            return 0;
        }
        length_name =
            pointer_property_designator(unit, statement->left, F2C_OBJECT_CHARACTER_LENGTH);
        if (length_name == NULL) {
            free(character_length);
            free(target_code);
            free(deallocatable_name);
            free(target_deallocatable);
            return 0;
        }
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = (size_t)(%s);\n", length_name, character_length);
        free(length_name);
    } else if (null_target && statement->left->symbol->deferred_character) {
        char *length_name =
            pointer_property_designator(unit, statement->left, F2C_OBJECT_CHARACTER_LENGTH);
        if (length_name == NULL) {
            free(target_code);
            free(deallocatable_name);
            free(target_deallocatable);
            return 0;
        }
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = 0U;\n", length_name);
        free(length_name);
    }
    free(character_length);
    free(target_code);
    free(deallocatable_name);
    free(target_deallocatable);
    return 1;
}

int f2c_emit_nullify_statement(Context *context, Unit *unit, const F2cStatement *statement,
                               size_t line, int depth) {
    size_t item;
    for (item = 0U; item < statement->item_count; ++item) {
        F2cExpr *expression = statement->arguments != NULL ? statement->arguments[item] : NULL;
        Symbol *symbol = expression != NULL ? expression->symbol : NULL;
        size_t dimension;
        int supported = 0;
        char *pointer_name;
        if (symbol == NULL || (!symbol->pointer && !symbol->procedure_pointer))
            continue;
        pointer_name = symbol->procedure_pointer
                           ? f2c_emit_statement_expression(context, unit, expression, line)
                           : pointer_assignment_designator(unit, expression, &supported);
        if (symbol->procedure_pointer)
            supported = pointer_name != NULL;
        if (!supported || pointer_name == NULL) {
            free(pointer_name);
            return 0;
        }
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = NULL;\n", pointer_name);
        if (!symbol->procedure_pointer) {
            char *deallocatable_name =
                pointer_property_designator(unit, expression, F2C_OBJECT_DEALLOCATABLE);
            if (deallocatable_name == NULL) {
                free(pointer_name);
                return 0;
            }
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = false;\n", deallocatable_name);
            free(deallocatable_name);
        }
        if (!symbol->procedure_pointer && symbol->deferred_character) {
            char *length_name =
                pointer_property_designator(unit, expression, F2C_OBJECT_CHARACTER_LENGTH);
            if (length_name == NULL) {
                free(pointer_name);
                return 0;
            }
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = 0U;\n", length_name);
            free(length_name);
        }
        for (dimension = 0U; !symbol->procedure_pointer && dimension < symbol->rank; ++dimension) {
            const F2cStorageReference reference = f2c_ir_storage_reference(expression);
            if (!f2c_storage_emit_store(&context->output, unit, &reference, F2C_OBJECT_LOWER,
                                        dimension, NULL, "1", depth) ||
                !f2c_storage_emit_store(&context->output, unit, &reference, F2C_OBJECT_EXTENT,
                                        dimension, NULL, "0", depth) ||
                !f2c_storage_emit_store(&context->output, unit, &reference, F2C_OBJECT_STRIDE,
                                        dimension, NULL, "0", depth)) {
                free(pointer_name);
                return 0;
            }
        }
        free(pointer_name);
    }
    return 1;
}

static int emit_prepared_pointer_assignment(Context *context, Unit *unit,
                                            const F2cStatement *statement, size_t line, int depth) {
    const size_t output_start = context->output.length;
    F2cPreparedStatementExpression left = {0};
    F2cPreparedStatementExpression right = {0};
    F2cStatement prepared = *statement;
    int emitted = 0;
    if (!f2c_prepare_statement_expression(context, unit, statement, statement->left, "pointer_left",
                                          line, depth + 1, &left) ||
        !f2c_prepare_statement_expression(context, unit, statement, statement->right,
                                          "pointer_right", line, depth + 1, &right))
        goto cleanup;
    prepared.left = (F2cExpr *)left.expression;
    prepared.right = (F2cExpr *)right.expression;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_buffer_append(&context->output, left.prelude.data != NULL ? left.prelude.data : "");
    f2c_buffer_append(&context->output, right.prelude.data != NULL ? right.prelude.data : "");
    if (!f2c_emit_pointer_assignment_statement(context, unit, &prepared, line, depth + 1)) {
        context->output.length = output_start;
        if (context->output.data != NULL)
            context->output.data[output_start] = '\0';
        goto cleanup;
    }
    (void)f2c_array_cleanup_emit(&context->output, unit, &right.cleanup);
    (void)f2c_array_cleanup_emit(&context->output, unit, &left.cleanup);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    emitted = 1;
cleanup:
    f2c_release_statement_expression(&right);
    f2c_release_statement_expression(&left);
    return emitted;
}

int f2c_emit_pointer_assignment_statement(Context *context, Unit *unit,
                                          const F2cStatement *statement, size_t line, int depth) {
    Symbol *pointer = statement->left != NULL ? statement->left->symbol : NULL;
    Symbol *target = statement->right != NULL ? statement->right->symbol : NULL;
    const int null_target = null_pointer_value(statement->right);
    if (pointer != NULL && pointer->pointer &&
        (f2c_array_contains_unmaterialized_value(unit, statement->left) ||
         f2c_array_contains_unmaterialized_value(unit, statement->right)))
        return emit_prepared_pointer_assignment(context, unit, statement, line, depth);
    if (pointer != NULL && pointer->procedure_pointer) {
        char *pointer_name = f2c_emit_statement_expression(context, unit, statement->left, line);
        if (pointer_name == NULL)
            return 0;
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = %s;\n", pointer_name,
                          null_target || target == NULL ? "NULL" : f2c_symbol_c_name(unit, target));
        free(pointer_name);
        return 1;
    }
    if (pointer != NULL && pointer->pointer) {
        int supported = 0;
        char *pointer_name = pointer_assignment_designator(unit, statement->left, &supported);
        if (!supported || pointer_name == NULL) {
            free(pointer_name);
            return 0;
        }
        supported = pointer->rank != 0U ? emit_array_pointer_assignment(context, unit, statement,
                                                                        pointer_name, depth)
                                        : emit_scalar_pointer_assignment(context, unit, statement,
                                                                         pointer_name, line, depth);
        free(pointer_name);
        return supported;
    }
    return 0;
}
