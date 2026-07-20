#include "codegen/statement/private.h"

#include "codegen/array/private.h"

#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int index;
    for (index = 0; index < depth; ++index)
        f2c_buffer_append(output, "    ");
}

static int numeric_type(Type type) {
    return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE ||
           type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX;
}

static int emit_derived_assignment(Context *context, const F2cStatement *statement,
                                   const char *left, const char *right, int depth) {
    if (statement->left == NULL || statement->right == NULL ||
        statement->left->type != TYPE_DERIVED || statement->right->type != TYPE_DERIVED ||
        statement->left->rank != 0U || statement->right->rank != 0U ||
        statement->left->derived_type == NULL ||
        statement->left->derived_type != statement->right->derived_type)
        return 0;
    indent(&context->output, depth);
    if (statement->right->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR) {
        f2c_buffer_printf(&context->output, "{ %s f2c_assignment_temporary = %s;\n",
                          statement->right->derived_type->c_name, right);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_initialize_%s(&f2c_assignment_temporary);\n",
                          statement->right->derived_type->c_name);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_copy_%s(&(%s), &f2c_assignment_temporary);\n",
                          statement->left->derived_type->c_name, left);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_destroy_%s(&f2c_assignment_temporary);\n",
                          statement->right->derived_type->c_name);
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    } else if ((statement->right->kind == F2C_EXPR_CALL &&
                statement->right->intrinsic != F2C_INTRINSIC_MERGE) ||
               statement->right->resolved_procedure != NULL) {
        f2c_buffer_printf(&context->output, "{ %s f2c_assignment_result = %s;\n",
                          statement->right->derived_type->c_name, right);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_copy_%s(&(%s), &f2c_assignment_result);\n",
                          statement->left->derived_type->c_name, left);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_destroy_%s(&f2c_assignment_result);\n",
                          statement->right->derived_type->c_name);
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    } else {
        f2c_buffer_printf(&context->output, "f2c_copy_%s(&(%s), &(%s));\n",
                          statement->left->derived_type->c_name, left, right);
    }
    return 1;
}

static int emit_derived_merge_assignment(Context *context, Unit *unit,
                                         const F2cStatement *statement, int depth,
                                         size_t merge_depth) {
    const size_t output_start = context != NULL ? context->output.length : 0U;
    const F2cExpr *merge = statement != NULL ? statement->right : NULL;
    const F2cExpr *true_source;
    const F2cExpr *false_source;
    const F2cExpr *mask;
    F2cStatement branch;
    char *left;
    char *true_code;
    char *false_code;
    char *mask_code;
    int supported = 1;
    if (context == NULL || unit == NULL || statement == NULL || statement->left == NULL ||
        merge == NULL || merge->kind != F2C_EXPR_CALL || merge->intrinsic != F2C_INTRINSIC_MERGE ||
        statement->left->type != TYPE_DERIVED || merge->type != TYPE_DERIVED ||
        statement->left->rank != 0U || merge->rank != 0U || statement->left->derived_type == NULL ||
        statement->left->derived_type != merge->derived_type)
        return 0;
    true_source = f2c_intrinsic_argument(merge->children, merge->child_count, "tsource", 0U);
    false_source = f2c_intrinsic_argument(merge->children, merge->child_count, "fsource", 1U);
    mask = f2c_intrinsic_argument(merge->children, merge->child_count, "mask", 2U);
    if (true_source == NULL || false_source == NULL || mask == NULL)
        return 0;
    left = f2c_emit_expression_ast(unit, statement->left, &supported);
    true_code = supported ? f2c_emit_expression_ast(unit, true_source, &supported) : NULL;
    false_code = supported ? f2c_emit_expression_ast(unit, false_source, &supported) : NULL;
    mask_code = supported ? f2c_emit_expression_ast(unit, mask, &supported) : NULL;
    if (!supported || left == NULL || true_code == NULL || false_code == NULL ||
        mask_code == NULL) {
        free(left);
        free(true_code);
        free(false_code);
        free(mask_code);
        return 0;
    }
    branch = *statement;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_merge_mask_%zu = (bool)(%s);\n",
                      merge_depth, mask_code);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "if (f2c_merge_mask_%zu) {\n", merge_depth);
    branch.right = (F2cExpr *)true_source;
    if (true_source->kind == F2C_EXPR_CALL && true_source->intrinsic == F2C_INTRINSIC_MERGE) {
        if (!emit_derived_merge_assignment(context, unit, &branch, depth + 2, merge_depth + 1U))
            goto failed;
    } else if (!emit_derived_assignment(context, &branch, left, true_code, depth + 2)) {
        goto failed;
    }
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "} else {\n");
    branch.right = (F2cExpr *)false_source;
    if (false_source->kind == F2C_EXPR_CALL && false_source->intrinsic == F2C_INTRINSIC_MERGE) {
        if (!emit_derived_merge_assignment(context, unit, &branch, depth + 2, merge_depth + 1U))
            goto failed;
    } else if (!emit_derived_assignment(context, &branch, left, false_code, depth + 2)) {
        goto failed;
    }
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(left);
    free(true_code);
    free(false_code);
    free(mask_code);
    return 1;

failed:
    if (context != NULL && context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    free(left);
    free(true_code);
    free(false_code);
    free(mask_code);
    return 0;
}

int f2c_emit_assignment_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  size_t line, int depth) {
    Symbol *left_symbol = statement->left != NULL ? statement->left->symbol : NULL;
    Symbol *right_symbol = statement->right != NULL ? statement->right->symbol : NULL;
    Type right_type = statement->right != NULL ? statement->right->type : TYPE_UNKNOWN;
    char *left;
    char *right;
    int left_supported = 0;
    int right_supported = 0;
    if (statement->resolved_procedure != NULL) {
        F2cExpr *operands[2] = {statement->left, statement->right};
        if (statement->resolved_procedure->elemental &&
            ((statement->left != NULL && statement->left->rank != 0U) ||
             (statement->right != NULL && statement->right->rank != 0U))) {
            F2cStatement call = *statement;
            call.kind = F2C_STMT_CALL;
            call.name = statement->resolved_procedure->name;
            call.arguments = operands;
            call.item_count = 2U;
            if (f2c_array_emit_elemental_call(context, unit, &call, depth))
                return 1;
        }
        f2c_emit_call_with_procedure(&context->output, unit, statement->resolved_procedure,
                                     operands, 2U, depth);
        return 1;
    }
    if (f2c_emit_array_section_assignment(context, unit, statement->left, statement->right,
                                          depth) ||
        f2c_emit_rank2_section_assignment(context, unit, statement->left, statement->right,
                                          depth) ||
        f2c_emit_whole_array_assignment(context, unit, statement->left, statement->right, line,
                                        depth))
        return 1;
    if (statement->left != NULL && statement->right != NULL &&
        statement->left->kind == F2C_EXPR_COMPONENT && statement->left->symbol != NULL &&
        statement->left->symbol->allocatable && statement->left->rank == 1U &&
        statement->right->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        if (!f2c_array_emit_allocatable_component_constructor(context, unit, statement->left,
                                                              statement->right, depth))
            f2c_diagnostic(context, line, 1,
                           "allocatable component array-constructor assignment requires "
                           "compatible rank-one values");
        return 1;
    }
    if (emit_derived_merge_assignment(context, unit, statement, depth, 0U))
        return 1;
    if (left_symbol != NULL && left_symbol->rank == 0U && !left_symbol->argument &&
        !left_symbol->external && left_symbol->type != TYPE_CHARACTER &&
        (statement->left->kind == F2C_EXPR_CALL ||
         statement->left->kind == F2C_EXPR_ARRAY_REFERENCE))
        return 1;
    if (left_symbol != NULL && left_symbol->equivalence_unaligned && statement->left != NULL &&
        statement->left->rank == 0U) {
        const char *suffix = f2c_unaligned_access_suffix(left_symbol);
        char *address =
            f2c_emit_unaligned_designator_address(unit, statement->left, &left_supported);
        right = f2c_emit_expression_ast(unit, statement->right, &right_supported);
        if (suffix == NULL || !left_supported || address == NULL || !right_supported ||
            right == NULL) {
            free(address);
            free(right);
            f2c_diagnostic(context, line, 1,
                           "unaligned EQUIVALENCE assignment requires a supported intrinsic "
                           "scalar designator");
            return 0;
        }
        if (numeric_type(left_symbol->type) && numeric_type(right_type) &&
            left_symbol->type != right_type) {
            char *converted = f2c_emit_numeric_conversion(right, right_type, left_symbol->type);
            free(right);
            right = converted;
        }
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_unaligned_store_%s(%s, %s);\n", suffix, address,
                          right);
        free(address);
        free(right);
        return 1;
    }
    left = f2c_emit_expression_ast(unit, statement->left, &left_supported);
    right = f2c_emit_expression_ast(unit, statement->right, &right_supported);
    if (!left_supported || left == NULL || !right_supported || right == NULL) {
        free(left);
        free(right);
        f2c_diagnostic(context, line, 1,
                       "internal compiler error: typed assignment cannot be emitted");
        return 0;
    }
    if (emit_derived_assignment(context, statement, left, right, depth)) {
        free(left);
        free(right);
        return 1;
    }
    if (f2c_emit_character_assignment(context, unit, left_symbol, statement->left, statement->right,
                                      left, right, depth)) {
        free(left);
        free(right);
        return 1;
    }
    indent(&context->output, depth);
    if (unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER &&
        unit->result_name != NULL && left_symbol != NULL && left_symbol->name != NULL &&
        strcmp(left_symbol->name, unit->result_name) == 0 && right[0] == '"') {
        f2c_buffer_printf(&context->output, "f2c_result = %s[0];\n", right);
    } else if (left_symbol != NULL && left_symbol->type == TYPE_CHARACTER &&
               left_symbol->argument && statement->left->kind == F2C_EXPR_NAME) {
        const int right_pointer =
            right[0] == '"' || (right_symbol != NULL && right_symbol->type == TYPE_CHARACTER &&
                                !right_symbol->argument && statement->right->kind == F2C_EXPR_NAME);
        f2c_buffer_printf(&context->output, "%s[0] = %s%s;\n", f2c_symbol_c_name(unit, left_symbol),
                          right, right_pointer ? "[0]" : "");
    } else if (left_symbol != NULL && left_symbol->type == TYPE_CHARACTER &&
               left_symbol->character_length != NULL && atoi(left_symbol->character_length) == 1) {
        const int right_pointer =
            right[0] == '"' || (right_symbol != NULL && right_symbol->type == TYPE_CHARACTER &&
                                !right_symbol->argument && statement->right->kind == F2C_EXPR_NAME);
        f2c_buffer_printf(&context->output, "%s[0] = %s%s; %s[1] = '\\0';\n",
                          f2c_symbol_c_name(unit, left_symbol), right, right_pointer ? "[0]" : "",
                          f2c_symbol_c_name(unit, left_symbol));
    } else if (left_symbol != NULL && left_symbol->type == TYPE_CHARACTER &&
               statement->left->kind == F2C_EXPR_NAME && left_symbol->character_length != NULL) {
        if (left_symbol->argument) {
            f2c_buffer_printf(&context->output, "*%s = %s%s;\n",
                              f2c_symbol_c_name(unit, left_symbol), right,
                              right[0] == '"' ? "[0]" : "");
        } else {
            const char *copy_source =
                right_symbol != NULL && right_symbol->type == TYPE_CHARACTER &&
                        right_symbol->argument && statement->right->kind == F2C_EXPR_NAME
                    ? f2c_symbol_c_name(unit, right_symbol)
                    : right;
            const char *left_c_name = f2c_symbol_c_name(unit, left_symbol);
            f2c_buffer_printf(&context->output,
                              "memset(%s, ' ', sizeof(%s) - 1U); "
                              "memcpy(%s, %s, F2C_MIN(sizeof(%s) - 1U, strlen(%s))); "
                              "%s[sizeof(%s) - 1U] = '\\0';\n",
                              left_c_name, left_c_name, left_c_name, copy_source, left_c_name,
                              copy_source, left_c_name, left_c_name);
        }
    } else if (left_symbol != NULL && left_symbol->type == TYPE_CHARACTER && right[0] == '"') {
        f2c_buffer_printf(&context->output, "%s = %s[0];\n", left, right);
    } else if (left_symbol != NULL && numeric_type(left_symbol->type) && numeric_type(right_type) &&
               left_symbol->type != right_type) {
        char *converted = f2c_emit_numeric_conversion(right, right_type, left_symbol->type);
        f2c_buffer_printf(&context->output, "%s = %s;\n", left, converted);
        free(converted);
    } else {
        f2c_buffer_printf(&context->output, "%s = %s;\n", left, right);
    }
    free(left);
    free(right);
    return 1;
}
