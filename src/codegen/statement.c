#include "codegen/statement/private.h"

#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
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
            f2c_emit_scope_cleanup_plan(&context->output, unit, &statement->label_cleanups[0],
                                        *depth + 2);
            indent(&context->output, *depth + 2);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n", statement->labels[0]);
            indent(&context->output, *depth + 1);
            f2c_buffer_append(&context->output, "if (f2c_arithmetic_if_value == 0) {\n");
            f2c_emit_scope_cleanup_plan(&context->output, unit, &statement->label_cleanups[1],
                                        *depth + 2);
            indent(&context->output, *depth + 2);
            f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n", statement->labels[1]);
            f2c_emit_scope_cleanup_plan(&context->output, unit, &statement->label_cleanups[2],
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
    } else if (statement->kind == F2C_STMT_CALL && statement->intrinsic == F2C_INTRINSIC_MVBITS) {
        if (!f2c_emit_mvbits_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1, "cannot lower MVBITS invocation");
    } else if (statement->kind == F2C_STMT_CALL &&
               (statement->intrinsic == F2C_INTRINSIC_RANDOM_NUMBER ||
                statement->intrinsic == F2C_INTRINSIC_RANDOM_SEED)) {
        if (!f2c_emit_random_statement(context, unit, statement, *depth))
            f2c_diagnostic(context, source_line->number, 1,
                           "cannot lower random intrinsic invocation");
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
        char *character_pointer = NULL;
        char *character_length = NULL;
        if (statement->expression != NULL && (!supported || code == NULL)) {
            free(code);
            f2c_diagnostic(context, source_line->number, 1,
                           "internal compiler error: typed STOP code cannot be emitted");
            return 0;
        }
        if (statement->expression != NULL && statement->expression->type == TYPE_CHARACTER) {
            character_pointer = f2c_character_source_pointer(unit, statement->expression, code);
            character_length = f2c_character_length_expression(unit, statement->expression);
            if (character_pointer == NULL || character_length == NULL) {
                free(code);
                free(character_pointer);
                free(character_length);
                f2c_diagnostic(context, source_line->number, 1,
                               "internal compiler error: CHARACTER STOP code cannot be emitted");
                return 0;
            }
            indent(&context->output, *depth);
            f2c_buffer_printf(&context->output,
                              "{ const char *f2c_stop_text = %s; "
                              "const size_t f2c_stop_length = (size_t)(%s);\n",
                              character_pointer, character_length);
            indent(&context->output, *depth + 1);
            f2c_buffer_printf(&context->output,
                              "fputs(\"%s \", stderr); "
                              "if (f2c_stop_length != 0U) "
                              "(void)fwrite(f2c_stop_text, 1U, f2c_stop_length, stderr); "
                              "fputc('\\n', stderr);\n",
                              statement->error_stop ? "ERROR STOP" : "STOP");
            indent(&context->output, *depth + 1);
            f2c_buffer_append(
                &context->output,
                unit->kind == UNIT_PROGRAM
                    ? (statement->error_stop ? "return EXIT_FAILURE;\n" : "return EXIT_SUCCESS;\n")
                    : (statement->error_stop ? "exit(EXIT_FAILURE);\n" : "exit(EXIT_SUCCESS);\n"));
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "}\n");
            free(code);
            free(character_pointer);
            free(character_length);
            return 1;
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
        f2c_emit_scope_cleanup_plan(&context->output, unit, &statement->transfer_cleanup, *depth);
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
        f2c_emit_scope_cleanup_plan(&context->output, unit, &statement->transfer_cleanup, *depth);
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
                f2c_emit_scope_cleanup_plan(&context->output, unit, &statement->label_cleanups[i],
                                            *depth + 2);
                indent(&context->output, *depth + 2);
                f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n",
                                  f2c_trim(statement->labels[i]));
            }
            indent(&context->output, *depth);
            f2c_buffer_append(&context->output, "}\n");
            free(selector);
        } else if (statement->name != NULL && statement->name[0] != '\0') {
            f2c_emit_scope_cleanup_plan(&context->output, unit, &statement->transfer_cleanup,
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
                    f2c_emit_scope_cleanup_plan(&context->output, unit,
                                                &statement->label_cleanups[i], *depth + 2);
                    indent(&context->output, *depth + 2);
                    f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n",
                                      statement->labels[i]);
                }
                emitted = statement->label_count;
            } else {
                for (i = 0U; i < statement->resolved_branch_count; ++i) {
                    const F2cResolvedBranch *branch = &statement->resolved_branches[i];
                    indent(&context->output, *depth + 1);
                    f2c_buffer_printf(&context->output, "case %s: {\n", branch->label);
                    f2c_emit_scope_cleanup_plan(&context->output, unit, &branch->cleanup,
                                                *depth + 2);
                    indent(&context->output, *depth + 2);
                    f2c_buffer_printf(&context->output, "goto f2c_label_%s; }\n", branch->label);
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
        if (!f2c_emit_nullify_statement(context, unit, statement, source_line->number, *depth))
            return 0;
    } else if (statement->kind == F2C_STMT_POINTER_ASSIGNMENT && statement->item_count == 2U) {
        if (!f2c_emit_pointer_assignment_statement(context, unit, statement, source_line->number,
                                                   *depth))
            return 0;
    } else if (statement->kind == F2C_STMT_ASSIGNMENT && statement->construct_owner != NULL &&
               statement->construct_owner->kind == F2C_STMT_WHERE) {
        if (!f2c_emit_where_assignment(context, unit, statement, *depth))
            return 0;
    } else if (statement->kind == F2C_STMT_ASSIGNMENT && statement->item_count == 2U) {
        if (!f2c_emit_assignment_statement(context, unit, statement, source_line->number, *depth))
            return 0;
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
