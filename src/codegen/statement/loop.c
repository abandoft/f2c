#include "codegen/statement/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    while (depth-- > 0)
        f2c_buffer_append(output, "    ");
}

static int integer_literal_value(const F2cExpr *expression, long long *value) {
    char *end = NULL;
    if (expression == NULL || expression->kind != F2C_EXPR_INTEGER_LITERAL ||
        expression->text == NULL)
        return 0;
    *value = strtoll(expression->text, &end, 10);
    return end != expression->text && *end == '\0';
}

static int same_scalar_designator(const F2cExpr *left, const F2cExpr *right) {
    if (left == NULL || right == NULL || left->rank != 0U || right->rank != 0U ||
        left->kind != F2C_EXPR_NAME || right->kind != F2C_EXPR_NAME)
        return 0;
    if (left->symbol != NULL || right->symbol != NULL)
        return left->symbol != NULL && left->symbol == right->symbol;
    return left->text != NULL && right->text != NULL && strcmp(left->text, right->text) == 0;
}

static int relative_do_count_fits_default_integer(const F2cExpr *start, const F2cExpr *finish,
                                                  long long step) {
    const F2cExpr *offset = NULL;
    long long distance;
    if (finish == NULL || finish->kind != F2C_EXPR_BINARY || finish->text == NULL ||
        finish->child_count != 2U)
        return 0;
    if (step == 1 && strcmp(finish->text, "+") == 0) {
        if (same_scalar_designator(start, finish->children[0]))
            offset = finish->children[1];
        else if (same_scalar_designator(start, finish->children[1]))
            offset = finish->children[0];
    } else if (step == -1 && strcmp(finish->text, "-") == 0 &&
               same_scalar_designator(start, finish->children[0])) {
        offset = finish->children[1];
    }
    return integer_literal_value(offset, &distance) && distance >= 0 && distance < INT32_MAX;
}

static int do_count_fits_default_integer(const F2cStatement *statement) {
    long long start;
    long long step;
    if (statement == NULL || statement->right == NULL || statement->step == NULL ||
        statement->left == NULL || statement->left->type_kind != f2c_default_kind(TYPE_INTEGER) ||
        !integer_literal_value(statement->step, &step))
        return 0;
    if (step == 1 && statement->right->kind == F2C_EXPR_INTEGER_LITERAL &&
        integer_literal_value(statement->right, &start) && start >= 1 && start <= INT32_MAX)
        return 1;
    if (relative_do_count_fits_default_integer(statement->right, statement->limit, step))
        return 1;
    return step <= -3 || step >= 3;
}

static int is_canonical_positive_unit_do(const F2cStatement *statement) {
    long long start;
    long long step;
    return statement != NULL && statement->left != NULL && statement->right != NULL &&
           statement->step != NULL && statement->left->type == TYPE_INTEGER &&
           statement->left->type_kind == f2c_default_kind(TYPE_INTEGER) &&
           integer_literal_value(statement->right, &start) && start >= 1 && start <= INT32_MAX &&
           integer_literal_value(statement->step, &step) && step == 1;
}

static void emit_condition(Buffer *output, const char *condition) {
    const size_t length = strlen(condition);
    if (length >= 2U && condition[0] == '(' && condition[length - 1U] == ')')
        f2c_buffer_append(output, condition);
    else
        f2c_buffer_printf(output, "(%s)", condition);
}

static int resolve_loop_id(Context *context, const Unit *unit, const F2cStatement *statement,
                           size_t source_line, size_t *loop_id) {
    *loop_id = f2c_statement_unit_index(unit, statement);
    if (*loop_id != SIZE_MAX)
        return 1;
    f2c_diagnostic(context, source_line, 1,
                   "internal compiler error: DO statement is not owned by its program unit");
    return 0;
}

static int emit_counted_do_begin(Context *context, Unit *unit, const F2cStatement *statement,
                                 size_t source_line, int *depth) {
    char *variable = f2c_emit_statement_expression(context, unit, statement->left, source_line);
    F2cPreparedStatementExpression start_expression;
    F2cPreparedStatementExpression finish_expression;
    F2cPreparedStatementExpression step_expression;
    const int canonical_positive_unit = is_canonical_positive_unit_do(statement);
    size_t loop_id;
    memset(&start_expression, 0, sizeof(start_expression));
    memset(&finish_expression, 0, sizeof(finish_expression));
    memset(&step_expression, 0, sizeof(step_expression));
    if (!resolve_loop_id(context, unit, statement, source_line, &loop_id)) {
        free(variable);
        return 0;
    }
    if (!f2c_prepare_statement_expression(context, unit, statement, statement->right, "do_start",
                                          source_line, *depth, &start_expression) ||
        !f2c_prepare_statement_expression(context, unit, statement, statement->limit, "do_limit",
                                          source_line, *depth, &finish_expression) ||
        !f2c_prepare_statement_expression(context, unit, statement, statement->step, "do_step",
                                          source_line, *depth, &step_expression)) {
        free(variable);
        f2c_release_statement_expression(&start_expression);
        f2c_release_statement_expression(&finish_expression);
        f2c_release_statement_expression(&step_expression);
        return 0;
    }
    if (statement->left->type == TYPE_INTEGER) {
        const int narrow_count = do_count_fits_default_integer(statement);
        f2c_buffer_append(&context->output, start_expression.prelude.data != NULL
                                                ? start_expression.prelude.data
                                                : "");
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "const int32_t f2c_do_start_%zu = (int32_t)(%s);\n",
                          loop_id, start_expression.code);
        f2c_buffer_append(&context->output, start_expression.cleanup.data != NULL
                                                ? start_expression.cleanup.data
                                                : "");
        f2c_buffer_append(&context->output, finish_expression.prelude.data != NULL
                                                ? finish_expression.prelude.data
                                                : "");
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "const int32_t f2c_do_limit_%zu = (int32_t)(%s);\n",
                          loop_id, finish_expression.code);
        f2c_buffer_append(&context->output, finish_expression.cleanup.data != NULL
                                                ? finish_expression.cleanup.data
                                                : "");
        if (!canonical_positive_unit) {
            f2c_buffer_append(&context->output, step_expression.prelude.data != NULL
                                                    ? step_expression.prelude.data
                                                    : "");
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "const int32_t f2c_do_step_%zu = (int32_t)(%s);\n",
                              loop_id, step_expression.code);
            f2c_buffer_append(&context->output, step_expression.cleanup.data != NULL
                                                    ? step_expression.cleanup.data
                                                    : "");
        }
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "%s = f2c_do_start_%zu;\n", variable, loop_id);
        if (canonical_positive_unit) {
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output,
                              "int64_t f2c_do_index_%zu = (int64_t)f2c_do_start_%zu;\n", loop_id,
                              loop_id);
        } else {
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "%s f2c_do_count_%zu = 0;\n",
                              narrow_count ? "int32_t" : "int64_t", loop_id);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output,
                              "if (f2c_do_step_%zu > 0 && %s <= f2c_do_limit_%zu) "
                              "f2c_do_count_%zu = %s((int64_t)f2c_do_limit_%zu - (int64_t)%s) / "
                              "(int64_t)f2c_do_step_%zu + 1%s;\n",
                              loop_id, variable, loop_id, loop_id, narrow_count ? "(int32_t)(" : "",
                              loop_id, variable, loop_id, narrow_count ? ")" : "");
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output,
                              "else if (f2c_do_step_%zu < 0 && %s >= f2c_do_limit_%zu) "
                              "f2c_do_count_%zu = %s((int64_t)%s - (int64_t)f2c_do_limit_%zu) / "
                              "-(int64_t)f2c_do_step_%zu + 1%s;\n",
                              loop_id, variable, loop_id, loop_id, narrow_count ? "(int32_t)(" : "",
                              variable, loop_id, loop_id, narrow_count ? ")" : "");
        }
    } else {
        const char *c_type = f2c_expression_c_type(statement->left);
        f2c_buffer_append(&context->output, start_expression.prelude.data != NULL
                                                ? start_expression.prelude.data
                                                : "");
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "const %s f2c_do_start_%zu = (%s)(%s);\n", c_type,
                          loop_id, c_type, start_expression.code);
        f2c_buffer_append(&context->output, start_expression.cleanup.data != NULL
                                                ? start_expression.cleanup.data
                                                : "");
        f2c_buffer_append(&context->output, finish_expression.prelude.data != NULL
                                                ? finish_expression.prelude.data
                                                : "");
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "const %s f2c_do_limit_%zu = (%s)(%s);\n", c_type,
                          loop_id, c_type, finish_expression.code);
        f2c_buffer_append(&context->output, finish_expression.cleanup.data != NULL
                                                ? finish_expression.cleanup.data
                                                : "");
        f2c_buffer_append(&context->output,
                          step_expression.prelude.data != NULL ? step_expression.prelude.data : "");
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "const %s f2c_do_step_%zu = (%s)(%s);\n", c_type,
                          loop_id, c_type, step_expression.code);
        f2c_buffer_append(&context->output,
                          step_expression.cleanup.data != NULL ? step_expression.cleanup.data : "");
    }
    if (statement->unroll_hint) {
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "F2C_LOOP_UNROLL\n");
    }
    indent(&context->output, *depth);
    if (statement->left->type == TYPE_INTEGER) {
        if (canonical_positive_unit)
            f2c_buffer_printf(&context->output,
                              "for (; f2c_do_index_%zu <= (int64_t)f2c_do_limit_%zu; "
                              "++f2c_do_index_%zu) {\n",
                              loop_id, loop_id, loop_id);
        else
            f2c_buffer_printf(&context->output,
                              "for (; f2c_do_count_%zu > 0; --f2c_do_count_%zu, %s += "
                              "f2c_do_step_%zu) {\n",
                              loop_id, loop_id, variable, loop_id);
    } else {
        f2c_buffer_printf(&context->output,
                          "for (%s = f2c_do_start_%zu; "
                          "(f2c_do_step_%zu >= 0 ? %s <= f2c_do_limit_%zu "
                          ": %s >= f2c_do_limit_%zu); %s += f2c_do_step_%zu) {\n",
                          variable, loop_id, loop_id, variable, loop_id, variable, loop_id,
                          variable, loop_id);
    }
    ++*depth;
    if (canonical_positive_unit) {
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "%s = (int32_t)f2c_do_index_%zu;\n", variable, loop_id);
    }
    free(variable);
    f2c_release_statement_expression(&start_expression);
    f2c_release_statement_expression(&finish_expression);
    f2c_release_statement_expression(&step_expression);
    return 1;
}

int f2c_emit_do_begin(Context *context, Unit *unit, const F2cStatement *statement,
                      size_t source_line, int *depth) {
    if (statement->kind == F2C_STMT_DO_WHILE && statement->expression != NULL) {
        F2cPreparedStatementExpression condition;
        const size_t identifier = f2c_statement_unit_index(unit, statement);
        if (!f2c_prepare_statement_expression(context, unit, statement, statement->expression,
                                              "condition", source_line, *depth + 1, &condition))
            return 0;
        indent(&context->output, *depth);
        if (condition.materialized) {
            f2c_buffer_append(&context->output, "for (;;) {\n");
            ++*depth;
            f2c_buffer_append(&context->output,
                              condition.prelude.data != NULL ? condition.prelude.data : "");
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "const bool f2c_condition_%zu = (bool)(%s);\n",
                              identifier, condition.code);
            f2c_buffer_append(&context->output,
                              condition.cleanup.data != NULL ? condition.cleanup.data : "");
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "if (!f2c_condition_%zu) break;\n", identifier);
        } else {
            f2c_buffer_append(&context->output, "while ");
            emit_condition(&context->output, condition.code);
            f2c_buffer_append(&context->output, " {\n");
            ++*depth;
        }
        f2c_release_statement_expression(&condition);
        return 1;
    }
    if (statement->kind != F2C_STMT_DO) {
        f2c_diagnostic(context, source_line, 1, "malformed DO statement");
        return 0;
    }
    if (statement->left != NULL && statement->right != NULL && statement->limit != NULL &&
        statement->step != NULL)
        return emit_counted_do_begin(context, unit, statement, source_line, depth);
    if (statement->left == NULL && statement->right == NULL && statement->limit == NULL &&
        statement->step == NULL) {
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "for (;;) {\n");
        ++*depth;
        return 1;
    }
    f2c_diagnostic(context, source_line, 1, "malformed DO statement");
    return 0;
}

int f2c_emit_do_end(Context *context, Unit *unit, const F2cStatement *opener, size_t source_line,
                    int *depth) {
    size_t loop_id;
    if (opener == NULL || (opener->kind != F2C_STMT_DO && opener->kind != F2C_STMT_DO_WHILE)) {
        f2c_diagnostic(context, source_line, 1,
                       "internal compiler error: DO termination has no typed loop owner");
        return 0;
    }
    if (!resolve_loop_id(context, unit, opener, source_line, &loop_id))
        return 0;
    if (f2c_statement_unit_targets_construct(unit, opener, F2C_STMT_CYCLE)) {
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "f2c_cycle_%zu: ;\n", loop_id);
    }
    if (*depth > 1)
        --*depth;
    indent(&context->output, *depth);
    f2c_buffer_append(&context->output, "}\n");
    if (is_canonical_positive_unit_do(opener)) {
        char *variable = f2c_emit_statement_expression(context, unit, opener->left, source_line);
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output,
                          "%s = f2c_do_index_%zu <= (int64_t)INT32_MAX "
                          "? (int32_t)f2c_do_index_%zu : INT32_MIN;\n",
                          variable, loop_id, loop_id);
        free(variable);
    }
    if (f2c_statement_unit_targets_construct(unit, opener, F2C_STMT_EXIT)) {
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "f2c_exit_%zu: ;\n", loop_id);
    }
    return 1;
}
