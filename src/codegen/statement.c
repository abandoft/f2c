#include "codegen/statement/private.h"

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

char *f2c_emit_statement_expression(Context *context, Unit *unit, const F2cExpr *expression,
                                    size_t line) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported || result == NULL) {
        free(result);
        f2c_diagnostic(context, line, 1,
                       "code generation does not support this typed statement expression");
        return f2c_strdup("0 /* unsupported typed statement expression */");
    }
    return result;
}

int f2c_emit_statement(Context *context, Unit *unit, const F2cStatement *statement,
                       Line *source_line, int *depth) {
    const char *line = statement->text;
    if (statement->state != F2C_IR_TYPED) {
        f2c_diagnostic(context, source_line != NULL ? source_line->number : statement->line, 1,
                       "internal compiler error: code generation received untyped statement IR");
        return 0;
    }
    if (statement->kind == F2C_STMT_EMPTY || statement->kind == F2C_STMT_DECLARATION ||
        statement->kind == F2C_STMT_FORMAT) {
        return 1;
    }
    if (statement->kind == F2C_STMT_END_WHERE) {
        if (!f2c_emit_where_end(context, unit, statement, depth)) {
            f2c_diagnostic(context, source_line->number, 1,
                           "internal compiler error: END WHERE has no WHERE owner");
            return 0;
        }
    } else if (statement->kind == F2C_STMT_ELSEWHERE) {
        if (!f2c_emit_elsewhere(context, unit, statement, *depth)) {
            f2c_diagnostic(context, source_line->number, 1,
                           "internal compiler error: ELSEWHERE has no WHERE owner");
            return 0;
        }
    } else if (statement->kind == F2C_STMT_WHERE) {
        if (!f2c_emit_where_begin(context, unit, statement, depth))
            return 0;
        if (!statement->block) {
            if (statement->nested == NULL ||
                !f2c_emit_where_assignment(context, unit, statement->nested, *depth) ||
                !f2c_emit_where_end(context, unit, statement, depth))
                return 0;
        }
    } else if (statement->kind == F2C_STMT_END_SELECT) {
        if (!f2c_emit_select_case_end(context, statement, depth)) {
            if (*depth > 1)
                --*depth;
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "}\n");
        }
    } else if (statement->kind == F2C_STMT_TYPE_GUARD) {
        if (!f2c_emit_type_guard(context, unit, statement, source_line->number, depth))
            return 0;
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
        if (!f2c_emit_case_begin(context, unit, statement, depth)) {
            f2c_diagnostic(context, source_line->number, 1,
                           "internal compiler error: CASE has no SELECT CASE owner");
            return 0;
        }
    } else if (statement->kind == F2C_STMT_SELECT_CASE) {
        if (statement->expression != NULL) {
            if (!f2c_emit_select_case_begin(context, unit, statement, depth)) {
                f2c_diagnostic(context, source_line->number, 1,
                               "internal compiler error: failed to lower SELECT CASE selector");
                return 0;
            }
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed SELECT CASE statement");
            return 0;
        }
    } else if (statement->kind == F2C_STMT_END_IF) {
        if (*depth > 1)
            --*depth;
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, "}\n");
    } else if (statement->kind == F2C_STMT_END_DO) {
        if (!f2c_emit_do_end(context, unit, statement->construct_owner, source_line->number, depth))
            return 0;
    } else if (statement->kind == F2C_STMT_ELSE_IF) {
        char *condition;
        if (statement->expression == NULL) {
            f2c_diagnostic(context, source_line->number, 1, "malformed ELSE IF statement");
        } else {
            condition = f2c_emit_statement_expression(context, unit, statement->expression,
                                                      source_line->number);
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
            char *value = f2c_emit_statement_expression(context, unit, statement->expression,
                                                        source_line->number);
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "{\n");
            indent(&context->output, *depth + 1);
            f2c_buffer_printf(&context->output, "%s f2c_arithmetic_if_value = %s;\n",
                              f2c_expression_c_type(statement->expression), value);
            indent(&context->output, *depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_arithmetic_if_value < 0) {\n");
            f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                            f2c_statement_label_line(unit, statement->labels[0]),
                                            *depth + 2);
            indent(&context->output, *depth + 2);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n", statement->labels[0]);
            indent(&context->output, *depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_arithmetic_if_value == 0) {\n");
            f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                            f2c_statement_label_line(unit, statement->labels[1]),
                                            *depth + 2);
            indent(&context->output, *depth + 2);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n", statement->labels[1]);
            f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                            f2c_statement_label_line(unit, statement->labels[2]),
                                            *depth + 1);
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
            char *condition = f2c_emit_statement_expression(context, unit, statement->expression,
                                                            source_line->number);
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
    } else if (statement->kind == F2C_STMT_DO_WHILE || statement->kind == F2C_STMT_DO) {
        if (!f2c_emit_do_begin(context, unit, statement, source_line->number, depth))
            return 0;
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
    } else if (statement->kind == F2C_STMT_REWIND || statement->kind == F2C_STMT_BACKSPACE ||
               statement->kind == F2C_STMT_ENDFILE) {
        if (!f2c_emit_position_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed file positioning statement");
    } else if (statement->kind == F2C_STMT_INQUIRE) {
        if (!f2c_emit_inquire_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "malformed INQUIRE statement");
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
        /* DATA initialization is emitted once in the procedure prologue. */
    } else if (statement->kind == F2C_STMT_CALL) {
        if (f2c_array_emit_elemental_call(context, unit, statement, *depth)) {
            return 1;
        }
        if (statement->expression != NULL && statement->expression->symbol != NULL &&
            statement->expression->symbol->procedure_pointer) {
            const Symbol *procedure = statement->expression->symbol;
            char *callee = f2c_emit_statement_expression(context, unit, statement->expression,
                                                         source_line->number);
            if (procedure->type_bound && !procedure->type_bound_nopass &&
                statement->expression->kind == F2C_EXPR_COMPONENT &&
                statement->expression->child_count != 0U &&
                procedure->external_parameter_count == statement->item_count + 1U) {
                F2cExpr **arguments =
                    (F2cExpr **)calloc(procedure->external_parameter_count, sizeof(*arguments));
                if (arguments != NULL) {
                    size_t source = 0U;
                    for (size_t parameter = 0U; parameter < procedure->external_parameter_count;
                         ++parameter) {
                        if (parameter == procedure->type_bound_pass_index) {
                            arguments[parameter] = statement->expression->children[0];
                        } else {
                            arguments[parameter] = statement->arguments[source++];
                        }
                    }
                    f2c_emit_call_with_signature(&context->output, unit, callee, procedure,
                                                 arguments, procedure->external_parameter_count,
                                                 *depth);
                } else {
                    f2c_diagnostic(context, source_line->number, 1,
                                   "out of memory lowering type-bound call");
                }
                free(arguments);
            } else {
                f2c_emit_call_with_signature(&context->output, unit, callee, procedure,
                                             statement->arguments, statement->item_count, *depth);
            }
            free(callee);
        } else {
            f2c_emit_call(&context->output, unit, statement->name, statement->arguments,
                          statement->item_count, *depth);
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
        int supported = 1;
        char *code = statement->expression != NULL
                         ? f2c_emit_expression_ast(unit, statement->expression, &supported)
                         : NULL;
        if (statement->expression != NULL && (!supported || code == NULL)) {
            free(code);
            f2c_diagnostic(context, source_line->number, 1,
                           "internal compiler error: typed STOP code cannot be emitted");
            return 0;
        }
        indent(&context->output, *depth);
        if (supported && code != NULL)
            f2c_buffer_printf(
                &context->output,
                unit->kind == UNIT_PROGRAM ? "return (int)(%s);\n" : "exit((int)(%s));\n", code);
        else if (statement->error_stop)
            f2c_buffer_append(&context->output, unit->kind == UNIT_PROGRAM
                                                    ? "return EXIT_FAILURE;\n"
                                                    : "exit(EXIT_FAILURE);\n");
        else
            f2c_buffer_append(&context->output, unit->kind == UNIT_PROGRAM
                                                    ? "return EXIT_SUCCESS;\n"
                                                    : "exit(EXIT_SUCCESS);\n");
        free(code);
    } else if (statement->kind == F2C_STMT_CYCLE) {
        const F2cStatement *target = statement->control_target;
        if (target == NULL) {
            f2c_diagnostic(context, source_line->number, 1,
                           "internal compiler error: CYCLE has no typed DO target");
            return 0;
        }
        f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line, target->line,
                                        *depth);
        indent(&context->output, *depth);
        if (statement->control_name != NULL)
            f2c_buffer_printf(&context->output, "goto f2c_cycle_%zu;\n",
                              f2c_statement_unit_index(unit, target));
        else
            f2c_buffer_append(&context->output, "continue;\n");
    } else if (statement->kind == F2C_STMT_EXIT) {
        const F2cStatement *target = statement->control_target;
        if (target == NULL) {
            f2c_diagnostic(context, source_line->number, 1,
                           "internal compiler error: EXIT has no typed DO target");
            return 0;
        }
        f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line, target->line,
                                        *depth);
        indent(&context->output, *depth);
        if (statement->control_name != NULL)
            f2c_buffer_printf(&context->output, "goto f2c_exit_%zu;\n",
                              f2c_statement_unit_index(unit, target));
        else
            f2c_buffer_append(&context->output, "break;\n");
    } else if (statement->kind == F2C_STMT_CONTINUE) {
        indent(&context->output, *depth);
        f2c_buffer_append(&context->output, ";\n");
    } else if (statement->kind == F2C_STMT_ASSIGN_LABEL) {
        if (statement->expression != NULL && statement->expression->definable &&
            statement->expression->type == TYPE_INTEGER && statement->label_count == 1U) {
            char *target = f2c_emit_statement_expression(context, unit, statement->expression,
                                                         source_line->number);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "%s = %s;\n", target, statement->labels[0]);
            free(target);
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed ASSIGN label statement");
        }
    } else if (statement->kind == F2C_STMT_GOTO) {
        if (statement->label_count != 0U && statement->expression != NULL) {
            size_t i;
            char *selector = f2c_emit_statement_expression(context, unit, statement->expression,
                                                           source_line->number);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "switch ((int32_t)(%s)) {\n", selector);
            for (i = 0U; i < statement->label_count; ++i) {
                indent(&context->output, *depth + 1);
                f2c_buffer_printf(&context->output, "case %zu: {\n", i + 1U);
                f2c_emit_scope_transfer_cleanup(
                    &context->output, unit, statement->line,
                    f2c_statement_label_line(unit, f2c_trim(statement->labels[i])), *depth + 2);
                indent(&context->output, *depth + 2);
                f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n",
                                  f2c_trim(statement->labels[i]));
            }
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "}\n");
            free(selector);
        } else if (statement->name != NULL && statement->name[0] != '\0') {
            f2c_emit_scope_transfer_cleanup(&context->output, unit, statement->line,
                                            f2c_statement_label_line(unit, statement->name),
                                            *depth);
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s;\n", statement->name);
        } else {
            f2c_diagnostic(context, source_line->number, 1, "malformed GOTO statement");
        }
    } else if (statement->kind == F2C_STMT_ASSIGNED_GOTO) {
        if (statement->expression != NULL && statement->expression->type == TYPE_INTEGER) {
            char *selector = f2c_emit_statement_expression(context, unit, statement->expression,
                                                           source_line->number);
            size_t i;
            size_t emitted = 0U;
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output, "switch ((int32_t)(%s)) {\n", selector);
            if (statement->label_count != 0U) {
                for (i = 0U; i < statement->label_count; ++i) {
                    indent(&context->output, *depth + 1);
                    f2c_buffer_printf(&context->output, "case %s: {\n", statement->labels[i]);
                    f2c_emit_scope_transfer_cleanup(
                        &context->output, unit, statement->line,
                        f2c_statement_label_line(unit, statement->labels[i]), *depth + 2);
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
                            f2c_statement_labels_equal(candidate->labels[0],
                                                       assignment->labels[0])) {
                            duplicate = 1;
                            break;
                        }
                    }
                    if (duplicate)
                        continue;
                    indent(&context->output, *depth + 1);
                    f2c_buffer_printf(&context->output, "case %s: {\n", assignment->labels[0]);
                    f2c_emit_scope_transfer_cleanup(
                        &context->output, unit, statement->line,
                        f2c_statement_label_line(unit, assignment->labels[0]), *depth + 2);
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
            if (statement->nested != NULL &&
                f2c_statement_unit_has_label_target(unit, statement->name)) {
                indent(&context->output, *depth);
                f2c_buffer_printf(&context->output, "f2c_label_%s: ;\n", statement->name);
            }
            if (statement->nested != NULL && statement->nested->kind != F2C_STMT_END_DO)
                (void)f2c_emit_statement(context, unit, statement->nested, source_line, depth);
            for (size_t loop = 0U; loop < statement->terminal_loop_count; ++loop)
                if (!f2c_emit_do_end(context, unit, statement->terminal_loops[loop],
                                     source_line->number, depth))
                    return 0;
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
            pointer_name =
                symbol->procedure_pointer
                    ? f2c_emit_statement_expression(context, unit, expression, source_line->number)
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
            char *pointer_name =
                f2c_emit_statement_expression(context, unit, statement->left, source_line->number);
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
        }
    } else if (statement->kind == F2C_STMT_ASSIGNMENT && statement->construct_owner != NULL &&
               statement->construct_owner->kind == F2C_STMT_WHERE) {
        if (!f2c_emit_where_assignment(context, unit, statement, *depth))
            return 0;
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
        if (f2c_emit_rank2_section_assignment(context, unit, statement->left, statement->right,
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
            if (!left_supported || left == NULL || !right_supported || right == NULL) {
                free(left);
                free(right);
                f2c_diagnostic(context, source_line->number, 1,
                               "internal compiler error: typed assignment cannot be emitted");
                free(left_name);
                return 0;
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
        f2c_diagnostic(context, source_line->number, 1,
                       "internal compiler error: typed statement has no C11 emitter: %s", line);
        return 0;
    }
    return 1;
}

static int emit_inline_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                 Line *source_line, int depth) {
    int mutable_depth = depth;
    return f2c_emit_statement(context, unit, statement, source_line, &mutable_depth);
}
