#include "codegen/statement/private.h"

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

static int null_pointer_value(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_CALL && expression->text != NULL &&
           strcmp(expression->text, "null") == 0;
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
                           : f2c_emit_pointer_designator(unit, expression, &supported);
        if (symbol->procedure_pointer)
            supported = pointer_name != NULL;
        if (!supported || pointer_name == NULL) {
            free(pointer_name);
            return 0;
        }
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = NULL;\n", pointer_name);
        for (dimension = 0U; !symbol->procedure_pointer && expression->kind == F2C_EXPR_NAME &&
                             dimension < symbol->rank;
             ++dimension) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s_extent_%zu = 0;\n",
                              f2c_symbol_c_name(unit, symbol), dimension + 1U);
        }
        free(pointer_name);
    }
    return 1;
}

int f2c_emit_pointer_assignment_statement(Context *context, Unit *unit,
                                          const F2cStatement *statement, size_t line, int depth) {
    Symbol *pointer = statement->left != NULL ? statement->left->symbol : NULL;
    Symbol *target = statement->right != NULL ? statement->right->symbol : NULL;
    const int null_target = null_pointer_value(statement->right);
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
        char *pointer_name = f2c_emit_pointer_designator(unit, statement->left, &supported);
        size_t dimension;
        if (!supported || pointer_name == NULL) {
            free(pointer_name);
            return 0;
        }
        indent(&context->output, depth);
        if (null_target) {
            f2c_buffer_printf(&context->output, "%s = NULL;\n", pointer_name);
        } else if (target != NULL) {
            f2c_buffer_printf(&context->output, "%s = %s%s;\n", pointer_name,
                              target->pointer || target->allocatable || target->argument ||
                                      target->rank != 0U || target->type == TYPE_CHARACTER
                                  ? ""
                                  : "&",
                              f2c_symbol_c_name(unit, target));
            if (pointer->deferred_character && statement->left->kind == F2C_EXPR_NAME) {
                char *length = f2c_symbol_character_length(unit, target);
                indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "f2c_char_len_%s = (size_t)(%s);\n",
                                  pointer_name, length != NULL ? length : "1U");
                free(length);
            }
        }
        for (dimension = 0U; statement->left->kind == F2C_EXPR_NAME && dimension < pointer->rank;
             ++dimension) {
            indent(&context->output, depth);
            if (null_target || target == NULL) {
                f2c_buffer_printf(&context->output, "%s_lower_%zu = 1; %s_extent_%zu = 0;\n",
                                  pointer_name, dimension + 1U, pointer_name, dimension + 1U);
            } else if (target->pointer || target->allocatable) {
                f2c_buffer_printf(&context->output,
                                  "%s_lower_%zu = %s_lower_%zu; %s_extent_%zu = "
                                  "%s_extent_%zu;\n",
                                  pointer_name, dimension + 1U, f2c_symbol_c_name(unit, target),
                                  dimension + 1U, pointer_name, dimension + 1U,
                                  f2c_symbol_c_name(unit, target), dimension + 1U);
            } else {
                char *lower = f2c_symbol_dimension_lower(unit, target, dimension);
                char *upper = f2c_symbol_dimension_upper(unit, target, dimension);
                f2c_buffer_printf(&context->output,
                                  "%s_lower_%zu = (int32_t)(%s); %s_extent_%zu = "
                                  "(int32_t)((%s) - (%s) + 1);\n",
                                  pointer_name, dimension + 1U, lower != NULL ? lower : "1",
                                  pointer_name, dimension + 1U, upper != NULL ? upper : "0",
                                  lower != NULL ? lower : "1");
                free(lower);
                free(upper);
            }
        }
        free(pointer_name);
        return 1;
    }
    return 0;
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
    } else if (statement->right->kind == F2C_EXPR_CALL) {
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

int f2c_emit_assignment_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  size_t line, int depth) {
    Symbol *left_symbol = statement->left != NULL ? statement->left->symbol : NULL;
    Symbol *right_symbol = statement->right != NULL ? statement->right->symbol : NULL;
    Type right_type = statement->right != NULL ? statement->right->type : TYPE_UNKNOWN;
    char *left;
    char *right;
    int left_supported = 0;
    int right_supported = 0;
    if (f2c_emit_array_section_assignment(context, unit, statement->left, statement->right,
                                          depth) ||
        f2c_emit_rank2_section_assignment(context, unit, statement->left, statement->right,
                                          depth) ||
        f2c_emit_whole_array_assignment(context, unit, statement->left, statement->right, line,
                                        depth))
        return 1;
    if (left_symbol != NULL && left_symbol->rank == 0U && !left_symbol->argument &&
        !left_symbol->external && left_symbol->type != TYPE_CHARACTER &&
        (statement->left->kind == F2C_EXPR_CALL ||
         statement->left->kind == F2C_EXPR_ARRAY_REFERENCE))
        return 1;
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
