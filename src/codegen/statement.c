#include "internal/f2c.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static int is_numeric_type(Type type) {
    return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE ||
           type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX;
}

static int do_count_fits_default_integer(const F2cStatement *statement) {
    char *end = NULL;
    long long start;
    long long step;
    if (statement == NULL || statement->right == NULL || statement->step == NULL ||
        statement->left == NULL || statement->left->type_kind != f2c_default_kind(TYPE_INTEGER) ||
        statement->step->kind != F2C_EXPR_INTEGER_LITERAL || statement->step->text == NULL)
        return 0;
    if (statement->right->kind == F2C_EXPR_INTEGER_LITERAL && statement->right->text != NULL &&
        statement->limit != NULL && statement->limit->kind == F2C_EXPR_INTEGER_LITERAL &&
        statement->limit->text != NULL && strcmp(statement->right->text, "1") == 0 &&
        strcmp(statement->step->text, "1") == 0)
        return 1;
    step = strtoll(statement->step->text, &end, 10);
    if (end == statement->step->text || *end != '\0')
        return 0;
    if (step == 1 && statement->right->kind == F2C_EXPR_INTEGER_LITERAL &&
        statement->right->text != NULL) {
        start = strtoll(statement->right->text, &end, 10);
        if (end != statement->right->text && *end == '\0' && start >= 2 &&
            start <= INT32_MAX)
            return 1;
    }
    return step <= -3 || step >= 3;
}

static void emit_condition(Buffer *output, const char *condition) {
    const size_t length = strlen(condition);
    if (length >= 2U && condition[0] == '(' && condition[length - 1U] == ')') {
        f2c_buffer_append(output, condition);
    } else {
        f2c_buffer_printf(output, "(%s)", condition);
    }
}

static int emit_inline_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                 Line *source_line, int depth);

static int statement_targets_label(const F2cStatement *statement, const char *label) {
    size_t i;
    if (statement->kind == F2C_STMT_GOTO || statement->kind == F2C_STMT_ASSIGNED_GOTO ||
        statement->kind == F2C_STMT_ASSIGN_LABEL || statement->kind == F2C_STMT_ARITHMETIC_IF) {
        if (statement->name != NULL && strcmp(statement->name, label) == 0)
            return 1;
        for (i = 0U; i < statement->label_count; ++i) {
            if (strcmp(statement->labels[i], label) == 0)
                return 1;
        }
    }
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_OPEN || statement->kind == F2C_STMT_REWIND ||
        statement->kind == F2C_STMT_CLOSE) {
        for (i = 0U; i < statement->control_count; ++i) {
            const F2cIoControl *control = &statement->io_controls[i];
            const F2cExpr *value = control->value;
            if ((control->kind != F2C_IO_CONTROL_END && control->kind != F2C_IO_CONTROL_EOR &&
                 control->kind != F2C_IO_CONTROL_ERR) ||
                value == NULL)
                continue;
            if (value->text != NULL && strcmp(value->text, label) == 0)
                return 1;
        }
    }
    return statement->nested != NULL && statement_targets_label(statement->nested, label);
}

static int unit_has_goto_label(Unit *unit, const char *label) {
    size_t i;
    for (i = 0U; i < unit->statement_count; ++i) {
        if (statement_targets_label(&unit->statements[i], label))
            return 1;
    }
    return 0;
}

static size_t label_line(const Unit *unit, const char *label) {
    size_t i;
    if (label == NULL)
        return 0U;
    for (i = 0U; i < unit->statement_count; ++i) {
        const F2cStatement *statement = &unit->statements[i];
        if (statement->kind == F2C_STMT_LABEL && statement->name != NULL &&
            strcmp(statement->name, label) == 0)
            return statement->line;
    }
    return 0U;
}

static int opens_control_scope(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE ||
           statement->kind == F2C_STMT_SELECT_CASE || statement->kind == F2C_STMT_SELECT_TYPE ||
           statement->kind == F2C_STMT_BLOCK_SCOPE ||
           (statement->kind == F2C_STMT_IF && statement->block);
}

static size_t enclosing_loop_line(const Unit *unit, const F2cStatement *current) {
    size_t *stack;
    size_t depth = 0U;
    size_t i;
    size_t result = 0U;
    stack = unit->statement_count != 0U ? (size_t *)calloc(unit->statement_count, sizeof(*stack))
                                        : NULL;
    if (stack == NULL)
        return 0U;
    for (i = 0U; i < unit->statement_count && &unit->statements[i] != current; ++i) {
        const F2cStatement *statement = &unit->statements[i];
        if (opens_control_scope(statement))
            stack[depth++] = i;
        else if ((statement->kind == F2C_STMT_END_BLOCK ||
                  statement->kind == F2C_STMT_END_BLOCK_SCOPE ||
                  statement->kind == F2C_STMT_END_SELECT) &&
                 depth != 0U)
            --depth;
    }
    while (depth != 0U) {
        const F2cStatement *statement = &unit->statements[stack[--depth]];
        if (statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE) {
            result = statement->line;
            break;
        }
    }
    free(stack);
    return result;
}

static int select_has_prior_case(Context *context, Unit *unit, Line *source_line) {
    size_t index = (size_t)(source_line - context->lines.items);
    int nested = 0;
    while (index > unit->begin + 1U) {
        const size_t statement_index = --index - unit->begin - 1U;
        const F2cStatementKind kind = unit->statements[statement_index].kind;
        if (kind == F2C_STMT_END_SELECT) {
            ++nested;
        } else if (kind == F2C_STMT_SELECT_CASE) {
            if (nested == 0)
                return 0;
            --nested;
        } else if (nested == 0 && kind == F2C_STMT_CASE) {
            return 1;
        }
    }
    return 0;
}

static const F2cStatement *enclosing_select_type(Context *context, Unit *unit, Line *source_line,
                                                 int *has_prior_guard) {
    size_t index = (size_t)(source_line - context->lines.items);
    int nested = 0;
    if (has_prior_guard != NULL)
        *has_prior_guard = 0;
    while (index > unit->begin + 1U) {
        const size_t statement_index = --index - unit->begin - 1U;
        const F2cStatement *candidate = &unit->statements[statement_index];
        if (candidate->kind == F2C_STMT_END_SELECT) {
            ++nested;
        } else if (candidate->kind == F2C_STMT_SELECT_TYPE) {
            if (nested == 0)
                return candidate;
            --nested;
        } else if (nested == 0 && candidate->kind == F2C_STMT_TYPE_GUARD &&
                   has_prior_guard != NULL) {
            *has_prior_guard = 1;
        }
    }
    return NULL;
}

static int derived_extends(const F2cDerivedType *candidate, const F2cDerivedType *ancestor) {
    while (candidate != NULL) {
        if (candidate == ancestor)
            return 1;
        candidate = candidate->parent;
    }
    return 0;
}

static void emit_class_guard_ids(Buffer *output, Unit *unit, const F2cDerivedType *guard,
                                 const char *tag) {
    size_t i;
    int emitted = 0;
    for (i = 0U; i < unit->derived_type_count; ++i) {
        F2cDerivedType *candidate = &unit->derived_types[i];
        if (!derived_extends(candidate, guard))
            continue;
        f2c_buffer_printf(output, "%s%s == F2C_TYPE_ID_%s", emitted ? " || " : "", tag,
                          candidate->c_name);
        emitted = 1;
    }
    for (i = 0U; i < unit->imported_derived_type_count; ++i) {
        F2cDerivedType *candidate = unit->imported_derived_types[i];
        if (!derived_extends(candidate, guard))
            continue;
        f2c_buffer_printf(output, "%s%s == F2C_TYPE_ID_%s", emitted ? " || " : "", tag,
                          candidate->c_name);
        emitted = 1;
    }
    if (!emitted)
        f2c_buffer_append(output, "false");
}

static char *emit_statement_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported || result == NULL) {
        free(result);
        return f2c_strdup("0 /* invalid statement expression */");
    }
    return result;
}

int f2c_emit_statement(Context *context, Unit *unit, const F2cStatement *statement,
                       Line *source_line, int *depth) {
    const char *line = statement->text;
    if (statement->kind == F2C_STMT_EMPTY || statement->kind == F2C_STMT_DECLARATION) {
        return 1;
    }
    if (statement->kind == F2C_STMT_END_SELECT) {
        const F2cStatement *type_select = enclosing_select_type(context, unit, source_line, NULL);
        if (type_select == NULL) {
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "break;\n");
        }
        if (*depth > 1)
            --*depth;
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "}\n");
    } else if (statement->kind == F2C_STMT_TYPE_GUARD) {
        int prior = 0;
        const F2cStatement *select = enclosing_select_type(context, unit, source_line, &prior);
        if (select == NULL || select->expression == NULL) {
            f2c_diagnostic(context, source_line->number, 1,
                           "TYPE/CLASS guard must be enclosed by SELECT TYPE");
        } else {
            char *selector = emit_statement_expression(unit, select->expression);
            Buffer tag = {0};
            if (prior && *depth > 1)
                --*depth;
            indent(&context->output, *depth);
            if (prior)
                f2c_buffer_append(&context->output, "} else ");
            if (statement->name != NULL && strcmp(statement->name, "default") == 0) {
                f2c_buffer_append(&context->output, "{\n");
            } else if (statement->guard_type != NULL) {
                f2c_buffer_printf(&tag, "(%s).f2c_type_tag", selector);
                f2c_buffer_append(&context->output, "if (");
                if (strcmp(statement->name, "class") == 0)
                    emit_class_guard_ids(&context->output, unit, statement->guard_type, tag.data);
                else
                    f2c_buffer_printf(&context->output, "%s == F2C_TYPE_ID_%s", tag.data,
                                      statement->guard_type->c_name);
                f2c_buffer_append(&context->output, ") {\n");
            } else {
                f2c_diagnostic(context, source_line->number, 1,
                               "TYPE/CLASS guard names an unknown derived type");
            }
            ++*depth;
            free(tag.data);
            free(selector);
        }
    } else if (statement->kind == F2C_STMT_SELECT_TYPE) {
        if (statement->expression == NULL || statement->expression->symbol == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "malformed SELECT TYPE selector");
        }
    } else if (statement->kind == F2C_STMT_BLOCK_SCOPE) {
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "{\n");
        ++*depth;
        f2c_emit_block_scope_begin(&context->output, unit, statement->line, *depth);
    } else if (statement->kind == F2C_STMT_END_BLOCK_SCOPE) {
        f2c_emit_block_scope_end(&context->output, unit, statement->line, *depth);
        if (*depth > 1)
            --*depth;
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "}\n");
    } else if (statement->kind == F2C_STMT_CASE) {
        if (select_has_prior_case(context, unit, source_line)) {
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "break;\n");
        }
        indent(&context->output, *depth);
        if (f2c_starts_word(line, "case default")) {
            f2c_buffer_append(&context->output, "default: ;\n");
        } else if (statement->expression != NULL) {
            char *value = emit_statement_expression(unit, statement->expression);
            f2c_buffer_printf(&context->output, "case %s: ;\n", value);
            free(value);
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed CASE statement");
        }
    } else if (statement->kind == F2C_STMT_SELECT_CASE) {
        if (statement->expression != NULL) {
            char *selector = emit_statement_expression(unit, statement->expression);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "switch ((int32_t)(%s)) {\n", selector);
            ++*depth;
            free(selector);
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed SELECT CASE statement");
        }
    } else if (statement->kind == F2C_STMT_END_BLOCK) {
        if (statement->name != NULL && unit_has_goto_label(unit, statement->name)) {
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "f2c_label_%s: ;\n", statement->name);
        }
        if (*depth > 1)
            --*depth;
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "}\n");
    } else if (statement->kind == F2C_STMT_ELSE_IF) {
        char *condition;
        if (statement->expression == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "malformed ELSE IF statement");
        } else {
            condition = emit_statement_expression(unit, statement->expression);
            if (*depth > 1)
                --*depth;
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "} else if ");
            emit_condition(&context->output, condition);
            f2c_buffer_append(&context->output, " {\n");
            ++*depth;
            free(condition);
        }
    } else if (statement->kind == F2C_STMT_ELSE) {
        if (*depth > 1)
            --*depth;
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "} else {\n");
        ++*depth;
    } else if (statement->kind == F2C_STMT_ARITHMETIC_IF) {
        if (statement->expression == NULL || statement->label_count != 3U ||
            (statement->expression->type != TYPE_INTEGER &&
             statement->expression->type != TYPE_REAL &&
             statement->expression->type != TYPE_DOUBLE)) {
            f2c_diagnostic(context, source_line->number, 1, "malformed arithmetic IF statement");
        } else {
            char *value = emit_statement_expression(unit, statement->expression);
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "{\n");
            indent(&context->output, *depth + 1);
            f2c_buffer_printf(&context->output, "%s f2c_arithmetic_if_value = %s;\n",
                              f2c_expression_c_type(statement->expression), value);
            indent(&context->output, *depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_arithmetic_if_value < 0) {\n");
            f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                            label_line(unit, statement->labels[0]), *depth + 2);
            indent(&context->output, *depth + 2);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n", statement->labels[0]);
            indent(&context->output, *depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_arithmetic_if_value == 0) {\n");
            f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                            label_line(unit, statement->labels[1]), *depth + 2);
            indent(&context->output, *depth + 2);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n", statement->labels[1]);
            f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                            label_line(unit, statement->labels[2]), *depth + 1);
            indent(&context->output, *depth + 1);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s;\n", statement->labels[2]);
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "}\n");
            free(value);
        }
    } else if (statement->kind == F2C_STMT_IF) {
        if (statement->expression == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "malformed IF statement");
        } else {
            char *condition = emit_statement_expression(unit, statement->expression);
            indent(&context->output, *depth);
            if (statement->block) {
                f2c_buffer_append(&context->output, "if ");
                emit_condition(&context->output, condition);
                f2c_buffer_append(&context->output, " {\n");
                ++*depth;
            } else {
                f2c_buffer_append(&context->output, "if ");
                emit_condition(&context->output, condition);
                f2c_buffer_append(&context->output, " {\n");
                if (statement->nested != NULL)
                    (void)emit_inline_statement(context, unit, statement->nested, source_line,
                                                *depth + 1);
                indent(&context->output, *depth);
                f2c_buffer_append(&context->output, "}\n");
            }
            free(condition);
        }
    } else if (statement->kind == F2C_STMT_DO_WHILE) {
        if (statement->expression != NULL) {
            char *condition = emit_statement_expression(unit, statement->expression);
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "while ");
            emit_condition(&context->output, condition);
            f2c_buffer_append(&context->output, " {\n");
            ++*depth;
            free(condition);
        }
    } else if (statement->kind == F2C_STMT_DO) {
        if (statement->left != NULL && statement->right != NULL && statement->limit != NULL &&
            statement->step != NULL) {
            char *variable = emit_statement_expression(unit, statement->left);
            char *start = emit_statement_expression(unit, statement->right);
            char *finish = emit_statement_expression(unit, statement->limit);
            char *step = emit_statement_expression(unit, statement->step);
            const size_t loop_id = (size_t)(statement - unit->statements);
            if (statement->left->type == TYPE_INTEGER) {
                const int narrow_count = do_count_fits_default_integer(statement);
                indent(&context->output, *depth);
                f2c_buffer_printf(&context->output,
                                  "const int32_t f2c_do_start_%zu = (int32_t)(%s);\n", loop_id,
                                  start);
                indent(&context->output, *depth);
                f2c_buffer_printf(&context->output,
                                  "const int32_t f2c_do_limit_%zu = (int32_t)(%s);\n", loop_id,
                                  finish);
                indent(&context->output, *depth);
                f2c_buffer_printf(&context->output,
                                  "const int32_t f2c_do_step_%zu = (int32_t)(%s);\n", loop_id,
                                  step);
                indent(&context->output, *depth);
                f2c_buffer_printf(&context->output, "%s f2c_do_count_%zu = 0;\n",
                                  narrow_count ? "int32_t" : "int64_t", loop_id);
                indent(&context->output, *depth);
                f2c_buffer_printf(&context->output, "%s = f2c_do_start_%zu;\n", variable, loop_id);
                indent(&context->output, *depth);
                f2c_buffer_printf(
                    &context->output,
                    "if (f2c_do_step_%zu > 0 && %s <= f2c_do_limit_%zu) "
                    "f2c_do_count_%zu = %s((int64_t)f2c_do_limit_%zu - (int64_t)%s) / "
                    "(int64_t)f2c_do_step_%zu + 1%s;\n",
                    loop_id, variable, loop_id, loop_id, narrow_count ? "(int32_t)(" : "", loop_id,
                    variable, loop_id, narrow_count ? ")" : "");
                indent(&context->output, *depth);
                f2c_buffer_printf(
                    &context->output,
                    "else if (f2c_do_step_%zu < 0 && %s >= f2c_do_limit_%zu) "
                    "f2c_do_count_%zu = %s((int64_t)%s - (int64_t)f2c_do_limit_%zu) / "
                    "-(int64_t)f2c_do_step_%zu + 1%s;\n",
                    loop_id, variable, loop_id, loop_id, narrow_count ? "(int32_t)(" : "", variable,
                    loop_id, loop_id, narrow_count ? ")" : "");
            }
            if (statement->unroll_hint) {
                indent(&context->output, *depth);
                f2c_buffer_append(&context->output, "F2C_LOOP_UNROLL\n");
            }
            indent(&context->output, *depth);
            if (statement->left->type == TYPE_INTEGER) {
                f2c_buffer_printf(&context->output,
                                  "for (; f2c_do_count_%zu > 0; --f2c_do_count_%zu, %s += "
                                  "f2c_do_step_%zu) {\n",
                                  loop_id, loop_id, variable, loop_id);
            } else {
                f2c_buffer_printf(&context->output,
                                  "for (%s = %s; ((%s) >= 0 ? %s <= %s : %s >= %s); %s += "
                                  "%s) {\n",
                                  variable, start, step, variable, finish, variable, finish,
                                  variable, step);
            }
            ++*depth;
            free(variable);
            free(start);
            free(finish);
            free(step);
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed DO statement");
        }
    } else if (statement->kind == F2C_STMT_WRITE) {
        if (!f2c_emit_read_write_statement(context, unit, statement, 0, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed WRITE statement");
    } else if (statement->kind == F2C_STMT_READ) {
        if (!f2c_emit_read_write_statement(context, unit, statement, 1, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed READ statement");
    } else if (statement->kind == F2C_STMT_PRINT) {
        if (!f2c_emit_print_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed PRINT statement");
    } else if (statement->kind == F2C_STMT_OPEN) {
        if (!f2c_emit_open_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed OPEN statement");
    } else if (statement->kind == F2C_STMT_REWIND) {
        if (!f2c_emit_rewind_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed REWIND statement");
    } else if (statement->kind == F2C_STMT_CLOSE) {
        if (!f2c_emit_close_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed CLOSE statement");
    } else if (statement->kind == F2C_STMT_ALLOCATE) {
        if (!f2c_emit_allocate_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed ALLOCATE statement");
    } else if (statement->kind == F2C_STMT_DEALLOCATE) {
        if (!f2c_emit_deallocate_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed DEALLOCATE statement");
    } else if (statement->kind == F2C_STMT_DATA) {
        f2c_emit_data_statement(context, unit, statement, *depth);
    } else if (statement->kind == F2C_STMT_CALL) {
        if (statement->expression != NULL && statement->expression->symbol != NULL &&
            statement->expression->symbol->procedure_pointer) {
            const Symbol *procedure = statement->expression->symbol;
            char *callee = emit_statement_expression(unit, statement->expression);
            if (procedure->type_bound && !procedure->type_bound_nopass &&
                statement->expression->kind == F2C_EXPR_COMPONENT &&
                statement->expression->child_count != 0U &&
                procedure->external_parameter_count == statement->item_count + 1U) {
                char **items = (char **)calloc(procedure->external_parameter_count, sizeof(*items));
                F2cExpr **arguments =
                    (F2cExpr **)calloc(procedure->external_parameter_count, sizeof(*arguments));
                if (items != NULL && arguments != NULL) {
                    size_t source = 0U;
                    for (size_t parameter = 0U; parameter < procedure->external_parameter_count;
                         ++parameter) {
                        if (parameter == procedure->type_bound_pass_index) {
                            items[parameter] = (char *)"f2c_passed_object";
                            arguments[parameter] = statement->expression->children[0];
                        } else {
                            items[parameter] = statement->items[source];
                            arguments[parameter] = statement->arguments[source++];
                        }
                    }
                    f2c_emit_call_with_signature(&context->output, unit, callee, procedure, items,
                                                 arguments, procedure->external_parameter_count,
                                                 *depth);
                } else {
                    f2c_diagnostic(context, source_line->number, 1,
                                   "out of memory lowering type-bound call");
                }
                free(items);
                free(arguments);
            } else {
                f2c_emit_call_with_signature(&context->output, unit, callee, procedure,
                                             statement->items, statement->arguments,
                                             statement->item_count, *depth);
            }
            free(callee);
        } else {
            f2c_emit_call(&context->output, unit, statement->name, statement->items,
                          statement->arguments, statement->item_count, *depth);
        }
    } else if (statement->kind == F2C_STMT_MOVE_ALLOC) {
        if (!f2c_emit_move_alloc_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed MOVE_ALLOC statement");
    } else if (statement->kind == F2C_STMT_RETURN) {
        f2c_emit_unit_cleanup(&context->output, unit, *depth);
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output,
                          f2c_unit_has_allocatable_result(unit) ? "return f2c_result_descriptor;\n"
                          : unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER
                              ? "return;\n"
                          : unit->kind == UNIT_FUNCTION
                              ? "return f2c_result;\n"
                              : (unit->kind == UNIT_PROGRAM ? "return 0;\n" : "return;\n"));
    } else if (statement->kind == F2C_STMT_STOP) {
        const int error_stop = f2c_starts_word(statement->text, "error stop");
        const char *code_text = f2c_trim((char *)statement->text +
                                         (error_stop ? strlen("error stop") : strlen("stop")));
        char *code = *code_text != '\0' && *code_text != '\'' && *code_text != '"'
                         ? f2c_translate_expression(unit, code_text)
                         : NULL;
        indent(&context->output, *depth);
        if (code != NULL)
            f2c_buffer_printf(
                &context->output,
                unit->kind == UNIT_PROGRAM ? "return (int)(%s);\n" : "exit((int)(%s));\n", code);
        else if (error_stop)
            f2c_buffer_append(&context->output, unit->kind == UNIT_PROGRAM
                                                    ? "return EXIT_FAILURE;\n"
                                                    : "exit(EXIT_FAILURE);\n");
        else
            f2c_buffer_append(&context->output, unit->kind == UNIT_PROGRAM
                                                    ? "return EXIT_SUCCESS;\n"
                                                    : "exit(EXIT_SUCCESS);\n");
        free(code);
    } else if (statement->kind == F2C_STMT_CYCLE) {
        f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                        enclosing_loop_line(unit, statement), *depth);
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "continue;\n");
    } else if (statement->kind == F2C_STMT_EXIT) {
        f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                        enclosing_loop_line(unit, statement), *depth);
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "break;\n");
    } else if (statement->kind == F2C_STMT_CONTINUE) {
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, ";\n");
    } else if (statement->kind == F2C_STMT_ASSIGN_LABEL) {
        if (statement->expression != NULL && statement->expression->definable &&
            statement->expression->type == TYPE_INTEGER && statement->label_count == 1U) {
            char *target = emit_statement_expression(unit, statement->expression);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "%s = %s;\n", target, statement->labels[0]);
            free(target);
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed ASSIGN label statement");
        }
    } else if (statement->kind == F2C_STMT_GOTO) {
        if (statement->label_count != 0U && statement->expression != NULL) {
            size_t i;
            char *selector = emit_statement_expression(unit, statement->expression);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "switch ((int32_t)(%s)) {\n", selector);
            for (i = 0U; i < statement->label_count; ++i) {
                indent(&context->output, *depth + 1);
                f2c_buffer_printf(&context->output, "case %zu: {\n", i + 1U);
                f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                                label_line(unit, f2c_trim(statement->labels[i])),
                                                *depth + 2);
                indent(&context->output, *depth + 2);
                f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n",
                                  f2c_trim(statement->labels[i]));
            }
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "}\n");
            free(selector);
        } else if (statement->name != NULL && statement->name[0] != '\0') {
            f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                            label_line(unit, statement->name), *depth);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s;\n", statement->name);
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed GOTO statement");
        }
    } else if (statement->kind == F2C_STMT_ASSIGNED_GOTO) {
        if (statement->expression != NULL && statement->expression->type == TYPE_INTEGER) {
            char *selector = emit_statement_expression(unit, statement->expression);
            size_t i;
            size_t emitted = 0U;
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "switch ((int32_t)(%s)) {\n", selector);
            if (statement->label_count != 0U) {
                for (i = 0U; i < statement->label_count; ++i) {
                    indent(&context->output, *depth + 1);
                    f2c_buffer_printf(&context->output, "case %s: {\n", statement->labels[i]);
                    f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                                    label_line(unit, statement->labels[i]),
                                                    *depth + 2);
                    indent(&context->output, *depth + 2);
                    f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n",
                                      statement->labels[i]);
                }
                emitted = statement->label_count;
            } else {
                for (i = 0U; i < unit->statement_count; ++i) {
                    const F2cStatement *assignment = &unit->statements[i];
                    size_t previous;
                    int duplicate = 0;
                    if (assignment->kind != F2C_STMT_ASSIGN_LABEL || assignment->name == NULL ||
                        statement->name == NULL || strcmp(assignment->name, statement->name) != 0 ||
                        assignment->label_count != 1U)
                        continue;
                    for (previous = 0U; previous < i; ++previous) {
                        const F2cStatement *candidate = &unit->statements[previous];
                        if (candidate->kind == F2C_STMT_ASSIGN_LABEL && candidate->name != NULL &&
                            strcmp(candidate->name, statement->name) == 0 &&
                            candidate->label_count == 1U &&
                            strcmp(candidate->labels[0], assignment->labels[0]) == 0) {
                            duplicate = 1;
                            break;
                        }
                    }
                    if (duplicate)
                        continue;
                    indent(&context->output, *depth + 1);
                    f2c_buffer_printf(&context->output, "case %s: {\n", assignment->labels[0]);
                    f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                                    label_line(unit, assignment->labels[0]),
                                                    *depth + 2);
                    indent(&context->output, *depth + 2);
                    f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n",
                                      assignment->labels[0]);
                    ++emitted;
                }
            }
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "}\n");
            if (emitted == 0U)
                f2c_diagnostic(context, source_line->number, 1,
                               "assigned GOTO has no allowed target labels");
            free(selector);
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed assigned GOTO statement");
        }
    } else if (statement->kind == F2C_STMT_LABEL) {
        if (statement->name != NULL) {
            if (statement->nested != NULL && unit_has_goto_label(unit, statement->name)) {
                indent(&context->output, *depth);
                f2c_buffer_printf(&context->output, "f2c_label_%s: ;\n", statement->name);
            }
            if (statement->nested != NULL)
                (void)emit_inline_statement(context, unit, statement->nested, source_line, *depth);
        }
    } else if (statement->kind == F2C_STMT_NULLIFY) {
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
                               ? emit_statement_expression(unit, expression)
                               : f2c_emit_pointer_designator(unit, expression, &supported);
            if (symbol->procedure_pointer)
                supported = pointer_name != NULL;
            if (!supported || pointer_name == NULL) {
                free(pointer_name);
                continue;
            }
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "%s = NULL;\n", pointer_name);
            for (dimension = 0U; !symbol->procedure_pointer && expression->kind == F2C_EXPR_NAME &&
                                 dimension < symbol->rank;
                 ++dimension) {
                indent(&context->output, *depth);
                f2c_buffer_printf(&context->output, "%s_extent_%zu = 0;\n",
                                  f2c_symbol_c_name(unit, symbol), dimension + 1U);
            }
            free(pointer_name);
        }
    } else if (statement->kind == F2C_STMT_POINTER_ASSIGNMENT && statement->item_count == 2U) {
        Symbol *pointer = statement->left != NULL ? statement->left->symbol : NULL;
        Symbol *target = statement->right != NULL ? statement->right->symbol : NULL;
        const int null_target =
            statement->items != NULL && f2c_starts_word(statement->items[1], "null");
        if (pointer != NULL && pointer->procedure_pointer) {
            char *pointer_name = emit_statement_expression(unit, statement->left);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "%s = %s;\n", pointer_name,
                              null_target || target == NULL ? "NULL"
                                                            : f2c_symbol_c_name(unit, target));
            free(pointer_name);
        } else if (pointer != NULL && pointer->pointer) {
            int supported = 0;
            char *pointer_name = f2c_emit_pointer_designator(unit, statement->left, &supported);
            size_t dimension;
            if (!supported || pointer_name == NULL) {
                free(pointer_name);
                return 0;
            }
            indent(&context->output, *depth);
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
                    indent(&context->output, *depth);
                    f2c_buffer_printf(&context->output, "f2c_char_len_%s = (size_t)(%s);\n",
                                      pointer_name, length != NULL ? length : "1U");
                    free(length);
                }
            }
            for (dimension = 0U;
                 statement->left->kind == F2C_EXPR_NAME && dimension < pointer->rank; ++dimension) {
                indent(&context->output, *depth);
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
                    char *lower =
                        f2c_translate_expression(unit, target->dimensions[dimension].lower != NULL
                                                           ? target->dimensions[dimension].lower
                                                           : "1");
                    char *upper =
                        f2c_translate_expression(unit, target->dimensions[dimension].upper != NULL
                                                           ? target->dimensions[dimension].upper
                                                           : "0");
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
        }
    } else if (statement->kind == F2C_STMT_ASSIGNMENT && statement->item_count == 2U) {
        const char *left_text = statement->items[0];
        const char *right_text = statement->items[1];
        char *left;
        char *right;
        size_t name_length = 0U;
        char *left_name;
        char *right_name;
        Symbol *left_symbol;
        Symbol *right_symbol;
        Type right_type;
        size_t right_name_length = 0U;
        left_name = f2c_identifier(left_text, &name_length);
        left_symbol = left_name != NULL ? f2c_find_symbol(unit, left_name) : NULL;
        if (f2c_emit_array_section_assignment(context, unit, statement->left, statement->right,
                                              *depth)) {
            free(left_name);
            return 1;
        }
        if (f2c_emit_rank2_section_assignment(context, unit, left_symbol, left_text, right_text,
                                              *depth)) {
            free(left_name);
            return 1;
        }
        if (f2c_emit_whole_array_assignment(context, unit, statement->left, statement->right,
                                            source_line->number, *depth)) {
            free(left_name);
            return 1;
        }
        if (left_symbol != NULL && left_symbol->rank == 0U && !left_symbol->argument &&
            !left_symbol->external && left_symbol->type != TYPE_CHARACTER) {
            const char *after_name = left_text + name_length;
            while (*after_name != '\0' && isspace((unsigned char)*after_name))
                ++after_name;
            if (*after_name == '(') {
                free(left_name);
                return 1;
            }
        }
        {
            int left_supported = 0;
            int right_supported = 0;
            left = f2c_emit_expression_ast(unit, statement->left, &left_supported);
            right = f2c_emit_expression_ast(unit, statement->right, &right_supported);
            if (!left_supported || left == NULL) {
                free(left);
                left = f2c_translate_expression(unit, left_text);
            }
            if (!right_supported || right == NULL) {
                free(right);
                right = f2c_translate_expression(unit, right_text);
            }
        }
        right_type = statement->right != NULL ? statement->right->type : TYPE_UNKNOWN;
        right_name = f2c_identifier(right_text, &right_name_length);
        right_symbol = right_name != NULL ? f2c_find_symbol(unit, right_name) : NULL;
        if (statement->left != NULL && statement->right != NULL &&
            statement->left->type == TYPE_DERIVED && statement->right->type == TYPE_DERIVED &&
            statement->left->rank == 0U && statement->right->rank == 0U &&
            statement->left->derived_type != NULL &&
            statement->left->derived_type == statement->right->derived_type) {
            indent(&context->output, *depth);
            if (statement->right->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR) {
                f2c_buffer_printf(&context->output, "{ %s f2c_assignment_temporary = %s;\n",
                                  statement->right->derived_type->c_name, right);
                indent(&context->output, *depth + 1);
                f2c_buffer_printf(&context->output,
                                  "f2c_initialize_%s(&f2c_assignment_temporary);\n",
                                  statement->right->derived_type->c_name);
                indent(&context->output, *depth + 1);
                f2c_buffer_printf(&context->output,
                                  "f2c_copy_%s(&(%s), &f2c_assignment_temporary);\n",
                                  statement->left->derived_type->c_name, left);
                indent(&context->output, *depth + 1);
                f2c_buffer_printf(&context->output, "f2c_destroy_%s(&f2c_assignment_temporary);\n",
                                  statement->right->derived_type->c_name);
                indent(&context->output, *depth);
                f2c_buffer_append(&context->output, "}\n");
            } else if (statement->right->kind == F2C_EXPR_CALL) {
                f2c_buffer_printf(&context->output, "{ %s f2c_assignment_result = %s;\n",
                                  statement->right->derived_type->c_name, right);
                indent(&context->output, *depth + 1);
                f2c_buffer_printf(&context->output, "f2c_copy_%s(&(%s), &f2c_assignment_result);\n",
                                  statement->left->derived_type->c_name, left);
                indent(&context->output, *depth + 1);
                f2c_buffer_printf(&context->output, "f2c_destroy_%s(&f2c_assignment_result);\n",
                                  statement->right->derived_type->c_name);
                indent(&context->output, *depth);
                f2c_buffer_append(&context->output, "}\n");
            } else {
                f2c_buffer_printf(&context->output, "f2c_copy_%s(&(%s), &(%s));\n",
                                  statement->left->derived_type->c_name, left, right);
            }
            free(right_name);
            free(left_name);
            free(left);
            free(right);
            return 1;
        }
        if (f2c_emit_character_assignment(context, unit,
                                          statement->left != NULL && statement->left->symbol != NULL
                                              ? statement->left->symbol
                                              : left_symbol,
                                          statement->left, statement->right, left, right, *depth)) {
            free(right_name);
            free(left_name);
            free(left);
            free(right);
            return 1;
        }
        indent(&context->output, *depth);
        if (unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER &&
            unit->result_name != NULL && left_name != NULL &&
            strcmp(left_name, unit->result_name) == 0 && right[0] == '"') {
            f2c_buffer_printf(&context->output, "f2c_result = %s[0];\n", right);
        } else if (left_symbol != NULL && left_symbol->type == TYPE_CHARACTER &&
                   left_symbol->argument && strchr(left_text, '(') == NULL) {
            const int right_pointer =
                right[0] == '"' || (right_symbol != NULL && right_symbol->type == TYPE_CHARACTER &&
                                    !right_symbol->argument && strchr(right_text, '(') == NULL);
            f2c_buffer_printf(&context->output, "%s[0] = %s%s;\n",
                              f2c_symbol_c_name(unit, left_symbol), right,
                              right_pointer ? "[0]" : "");
        } else if (left_symbol != NULL && left_symbol->type == TYPE_CHARACTER &&
                   left_symbol->character_length != NULL &&
                   atoi(left_symbol->character_length) == 1) {
            const int right_pointer =
                right[0] == '"' || (right_symbol != NULL && right_symbol->type == TYPE_CHARACTER &&
                                    !right_symbol->argument && strchr(right_text, '(') == NULL);
            f2c_buffer_printf(&context->output, "%s[0] = %s%s; %s[1] = '\\0';\n",
                              f2c_symbol_c_name(unit, left_symbol), right,
                              right_pointer ? "[0]" : "", f2c_symbol_c_name(unit, left_symbol));
        } else if (left_symbol != NULL && left_symbol->type == TYPE_CHARACTER &&
                   strchr(left_text, '(') == NULL && left_symbol->character_length != NULL) {
            if (left_symbol->argument) {
                f2c_buffer_printf(&context->output, "*%s = %s%s;\n", left_name, right,
                                  right[0] == '"' ? "[0]" : "");
            } else {
                const char *copy_source =
                    right_symbol != NULL && right_symbol->type == TYPE_CHARACTER &&
                            right_symbol->argument && strchr(right_text, '(') == NULL
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
        } else if (left_symbol != NULL && is_numeric_type(left_symbol->type) &&
                   is_numeric_type(right_type) && left_symbol->type != right_type) {
            char *converted = f2c_emit_numeric_conversion(right, right_type, left_symbol->type);
            f2c_buffer_printf(&context->output, "%s = %s;\n", left, converted);
            free(converted);
        } else {
            f2c_buffer_printf(&context->output, "%s = %s;\n", left, right);
        }
        free(right_name);
        free(left_name);
        free(left);
        free(right);
    } else {
        f2c_diagnostic(context, source_line->number, 1, "statement is not yet translated: %s",
                       line);
        indent(&context->output, *depth);
        f2c_buffer_printf(&context->output, "/* unsupported Fortran: %s */\n", line);
    }
    return 1;
}

static int emit_inline_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                 Line *source_line, int depth) {
    int mutable_depth = depth;
    return f2c_emit_statement(context, unit, statement, source_line, &mutable_depth);
}
