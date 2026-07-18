#include "semantic/validation/private.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int ordered_numeric_scalar(const F2cExpr *expression) {
    return expression != NULL && expression->rank == 0U &&
           (expression->type == TYPE_INTEGER || expression->type == TYPE_REAL ||
            expression->type == TYPE_DOUBLE);
}

static void validate_control_flow_statement(Context *context, Unit *unit,
                                            const F2cStatement *statement) {
    const F2cExpr *expression = statement->expression;
    if (statement->kind == F2C_STMT_IF || statement->kind == F2C_STMT_ELSE_IF ||
        statement->kind == F2C_STMT_DO_WHILE) {
        if (expression != NULL && (expression->type != TYPE_LOGICAL || expression->rank != 0U)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, expression),
                              1, "%s condition must be a scalar LOGICAL expression",
                              statement->kind == F2C_STMT_DO_WHILE ? "DO WHILE" : "IF");
        }
    } else if (statement->kind == F2C_STMT_ARITHMETIC_IF) {
        if (expression != NULL && !ordered_numeric_scalar(expression)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, expression),
                              1,
                              "arithmetic IF selector must be a scalar INTEGER or REAL "
                              "expression");
        }
    } else if (statement->kind == F2C_STMT_SELECT_CASE) {
        if (expression == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                     "SELECT CASE requires a selector expression");
        } else if (expression->rank != 0U ||
                   (expression->type != TYPE_INTEGER && expression->type != TYPE_CHARACTER &&
                    expression->type != TYPE_LOGICAL)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, expression),
                              1,
                              "SELECT CASE selector must be a scalar INTEGER, CHARACTER, or "
                              "LOGICAL expression");
        } else if (expression->type == TYPE_CHARACTER &&
                   expression->type_kind != f2c_default_kind(TYPE_CHARACTER)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, expression),
                              1, "SELECT CASE currently supports default CHARACTER kind only");
        }
    } else if (statement->kind == F2C_STMT_DO) {
        int64_t step;
        if (statement->left != NULL &&
            (!ordered_numeric_scalar(statement->left) || !statement->left->definable)) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, statement->left), 1,
                "counted DO variable must be a definable scalar INTEGER or REAL "
                "object");
        }
        if (statement->right != NULL && statement->limit != NULL && statement->step != NULL &&
            (!ordered_numeric_scalar(statement->right) ||
             !ordered_numeric_scalar(statement->limit) ||
             !ordered_numeric_scalar(statement->step))) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, statement->right), 1,
                "counted DO initial value, limit, and step must be scalar INTEGER "
                "or REAL expressions");
        }
        if (statement->step != NULL && statement->step->type == TYPE_INTEGER &&
            f2c_evaluate_integer_constant(unit, statement->step, &step) && step == 0) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, statement->step), 1,
                "counted DO step cannot be zero");
        }
    } else if (statement->kind == F2C_STMT_GOTO && expression != NULL) {
        if (expression->type != TYPE_INTEGER || expression->rank != 0U) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, expression),
                              1, "computed GOTO selector must be a scalar INTEGER");
        }
    } else if (statement->kind == F2C_STMT_ASSIGNED_GOTO ||
               statement->kind == F2C_STMT_ASSIGN_LABEL) {
        if (expression != NULL && (expression->type != TYPE_INTEGER || expression->rank != 0U ||
                                   !expression->definable)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, expression),
                              1, "%s target must be a definable scalar INTEGER object",
                              statement->kind == F2C_STMT_ASSIGN_LABEL ? "ASSIGN"
                                                                       : "assigned GOTO");
        }
    }
}

static void validate_pointer_statement(Context *context, const F2cStatement *statement) {
    size_t i;
    if (statement->kind == F2C_STMT_NULLIFY) {
        if (statement->item_count == 0U) {
            f2c_diagnostic_at(context, statement->line, 1U, 1,
                              "NULLIFY requires at least one POINTER object");
        }
        for (i = 0U; i < statement->item_count; ++i) {
            F2cExpr *argument = statement->arguments != NULL ? statement->arguments[i] : NULL;
            Symbol *symbol = argument != NULL && (argument->kind == F2C_EXPR_NAME ||
                                                  argument->kind == F2C_EXPR_COMPONENT)
                                 ? argument->symbol
                                 : NULL;
            if (symbol == NULL || (!symbol->pointer && !symbol->procedure_pointer)) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, argument),
                                  1, "NULLIFY object must be a whole POINTER object");
            } else if (argument->kind == F2C_EXPR_COMPONENT && symbol->rank != 0U) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, argument),
                                  1,
                                  "array POINTER components require a component descriptor and "
                                  "are not yet supported");
            }
        }
        return;
    }
    if (statement->kind == F2C_STMT_POINTER_ASSIGNMENT) {
        const F2cExpr *left = statement->left;
        const F2cExpr *right = statement->right;
        Symbol *pointer =
            left != NULL && (left->kind == F2C_EXPR_NAME || left->kind == F2C_EXPR_COMPONENT)
                ? left->symbol
                : NULL;
        Symbol *target = right != NULL && right->kind == F2C_EXPR_NAME ? right->symbol : NULL;
        const int null_target = statement->item_count == 2U && statement->items != NULL &&
                                f2c_starts_word(statement->items[1], "null");
        if (pointer == NULL || (!pointer->pointer && !pointer->procedure_pointer)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, left), 1,
                              "pointer-assignment target must be a whole POINTER object");
            return;
        }
        if (pointer->procedure_pointer) {
            if (left->kind != F2C_EXPR_NAME && left->kind != F2C_EXPR_COMPONENT) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, left), 1,
                                  "procedure pointer assignment requires a whole pointer object");
            } else if (!null_target &&
                       (target == NULL || !target->external ||
                        !f2c_validation_procedure_signatures_compatible(pointer, target, 0U))) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, right), 1,
                                  "procedure pointer target must have a compatible explicit "
                                  "interface");
            }
            return;
        }
        if (left->kind == F2C_EXPR_COMPONENT && pointer->rank != 0U) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, left), 1,
                              "array POINTER components require a component descriptor and are "
                              "not yet supported");
            return;
        }
        if (!null_target &&
            (target == NULL || (!target->target && !target->pointer && !target->allocatable))) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, right), 1,
                              "pointer-assignment value must be a whole TARGET, POINTER, "
                              "ALLOCATABLE object, or NULL()");
        } else if (!null_target && target != NULL &&
                   (target->type != pointer->type || target->kind != pointer->kind ||
                    target->rank != pointer->rank)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, right), 1,
                              "pointer-assignment objects have incompatible type, kind, or rank");
        }
    }
}

static void validate_statement(Context *context, Unit *unit, F2cStatement *statement) {
    size_t i;
    if (statement->kind == F2C_STMT_INVALID) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, &statement->span, 1,
                                 "unsupported Fortran statement: %s", statement->text);
    }
    if (!statement->construct_syntax_valid) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "malformed construct name or control target syntax");
    }
    f2c_validation_report_parse_error(context, statement->line, statement->text,
                                      statement->expression, "statement");
    f2c_validation_constructor(context, unit, statement->line, statement->text,
                               statement->expression);
    f2c_validation_expression_calls(context, unit, statement->line, statement->text,
                                    statement->expression);
    f2c_validation_report_parse_error(context, statement->line, statement->text, statement->left,
                                      "left-hand side");
    f2c_validation_constructor(context, unit, statement->line, statement->text, statement->left);
    f2c_validation_expression_calls(context, unit, statement->line, statement->text,
                                    statement->left);
    f2c_validation_report_parse_error(context, statement->line, statement->text, statement->right,
                                      "right-hand side");
    f2c_validation_constructor(context, unit, statement->line, statement->text, statement->right);
    f2c_validation_expression_calls(context, unit, statement->line, statement->text,
                                    statement->right);
    f2c_validation_report_parse_error(context, statement->line, statement->text, statement->limit,
                                      "limit");
    f2c_validation_report_parse_error(context, statement->line, statement->text, statement->step,
                                      "step");
    f2c_validation_expression_calls(context, unit, statement->line, statement->text,
                                    statement->limit);
    f2c_validation_expression_calls(context, unit, statement->line, statement->text,
                                    statement->step);
    validate_control_flow_statement(context, unit, statement);
    f2c_validation_case_statement(context, unit, statement);
    f2c_validation_data_statement(context, unit, statement);
    f2c_validation_where_statement(context, statement);
    validate_pointer_statement(context, statement);
    f2c_validation_intrinsic_assignment(context, statement);
    f2c_validation_constructor_assignment(context, unit, statement);
    if (statement->kind == F2C_STMT_ALLOCATE || statement->kind == F2C_STMT_DEALLOCATE)
        f2c_validation_allocation(context, unit, statement);
    if (statement->kind != F2C_STMT_READ && statement->kind != F2C_STMT_WRITE &&
        statement->kind != F2C_STMT_PRINT) {
        for (i = 0U; i < statement->item_count; ++i)
            f2c_validation_report_parse_error(
                context, statement->line, statement->text,
                statement->arguments != NULL ? statement->arguments[i] : NULL, "argument");
        for (i = 0U; i < statement->item_count; ++i)
            f2c_validation_constructor(context, unit, statement->line, statement->text,
                                       statement->arguments != NULL ? statement->arguments[i]
                                                                    : NULL);
        for (i = 0U; i < statement->item_count; ++i)
            f2c_validation_expression_calls(context, unit, statement->line, statement->text,
                                            statement->arguments != NULL ? statement->arguments[i]
                                                                         : NULL);
    }
    if (statement->kind == F2C_STMT_MOVE_ALLOC) {
        f2c_validation_move_alloc(context, unit, statement);
    } else if (statement->kind == F2C_STMT_CALL && statement->expression == NULL) {
        Unit *definition = f2c_validation_procedure_call(
            context, unit, statement->line, statement->text, statement->name, &statement->arguments,
            &statement->items, &statement->item_count, 1);
        statement->resolved_procedure = definition;
        if (definition != NULL && definition->name != NULL && !definition->interface_abstract &&
            strcmp(statement->name, definition->name) != 0) {
            char *resolved = f2c_strdup(definition->name);
            if (resolved != NULL) {
                free(statement->name);
                statement->name = resolved;
            }
        }
    } else if (statement->kind == F2C_STMT_CALL && statement->expression != NULL &&
               statement->expression->symbol != NULL && statement->expression->symbol->type_bound) {
        const Symbol *binding = statement->expression->symbol;
        const size_t expected =
            binding->external_parameter_count -
            ((!binding->type_bound_nopass && binding->external_parameter_count != 0U) ? 1U : 0U);
        if (statement->item_count != expected)
            f2c_diagnostic(context, statement->line, 1,
                           "type-bound subroutine '%s' expects %zu explicit arguments but has "
                           "%zu",
                           statement->expression->text, expected, statement->item_count);
    }
    for (i = 0U; i < statement->control_count; ++i)
        f2c_validation_report_parse_error(context, statement->line, statement->text,
                                          statement->io_controls[i].value, "I/O control");
    for (i = 0U; i < statement->control_count; ++i)
        f2c_validation_expression_calls(context, unit, statement->line, statement->text,
                                        statement->io_controls[i].value);
    for (i = 0U; i < statement->io_item_count; ++i) {
        f2c_validation_io_item(context, statement->line, statement->text, &statement->io_items[i]);
        f2c_validation_io_item_calls(context, unit, statement->line, statement->text,
                                     &statement->io_items[i]);
    }
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_OPEN || statement->kind == F2C_STMT_REWIND ||
        statement->kind == F2C_STMT_CLOSE)
        f2c_validation_io_statement(context, unit, statement);
    if (statement->nested != NULL)
        validate_statement(context, unit, statement->nested);
    statement->state = F2C_IR_TYPED;
}

static F2cExpr *parse_specification_expression(Context *context, Unit *unit, size_t line,
                                               const char *text, F2cTokenRange syntax,
                                               const char *role) {
    const char *error_at = NULL;
    const char *source_line;
    F2cExpr *expression;
    if (text == NULL || strcmp(text, "*") == 0 || strcmp(text, ":") == 0)
        return NULL;
    expression = syntax.count != 0U ? f2c_parse_expression_tokens(unit, syntax.tokens, syntax.count,
                                                                  syntax.source, &error_at)
                                    : f2c_parse_expression_ast(unit, text, &error_at);
    source_line = f2c_validation_unit_line(context, unit, line);
    if (expression == NULL) {
        f2c_diagnostic_at(context, line, 1U, 1, "out of memory while validating %s expression",
                          role);
    } else if (error_at != NULL) {
        f2c_validation_report_parse_error(context, line, source_line, expression, role);
    }
    return expression;
}

static void validate_integer_specification(Context *context, size_t line, const char *source_line,
                                           const F2cExpr *expression, const char *role) {
    if (expression != NULL && expression->parse_error_offset == SIZE_MAX &&
        ((expression->type != TYPE_UNKNOWN && expression->type != TYPE_INTEGER) ||
         expression->rank != 0U)) {
        f2c_diagnostic_at(context, line, f2c_validation_expression_column(source_line, expression),
                          1, "%s must be a scalar INTEGER expression", role);
    }
}

static void validate_declaration_initializer(Context *context, size_t line, const char *source_line,
                                             const Symbol *symbol) {
    const F2cExpr *initializer = symbol->initializer_expression;
    if (initializer == NULL || initializer->parse_error_offset != SIZE_MAX)
        return;
    if (symbol->type != TYPE_UNKNOWN && initializer->type != TYPE_UNKNOWN &&
        !f2c_validation_type_compatible(symbol->type, initializer->type)) {
        f2c_diagnostic_at(context, line, f2c_validation_expression_column(source_line, initializer),
                          1, "declaration initializer type is incompatible with '%s'",
                          symbol->name);
    }
    if (initializer->rank != 0U && initializer->rank != symbol->rank) {
        f2c_diagnostic_at(context, line, f2c_validation_expression_column(source_line, initializer),
                          1, "declaration initializer rank %zu does not match rank-%zu entity '%s'",
                          initializer->rank, symbol->rank, symbol->name);
    }
}

static void validate_symbol_expressions(Context *context, Unit *unit, Symbol *symbol) {
    size_t dimension;
    const size_t line = symbol->declaration_line != 0U ? symbol->declaration_line
                                                       : context->lines.items[unit->begin].number;
    const char *source_line = f2c_validation_unit_line(context, unit, line);
    f2c_expr_free(symbol->initializer_expression);
    f2c_expr_free(symbol->character_length_expression);
    symbol->initializer_expression =
        parse_specification_expression(context, unit, line, symbol->initializer,
                                       symbol->initializer_syntax, "declaration initializer");
    symbol->character_length_expression =
        parse_specification_expression(context, unit, line, symbol->character_length,
                                       symbol->character_length_syntax, "character length");
    f2c_expr_free(symbol->statement_function_expression);
    symbol->statement_function_expression =
        symbol->statement_function
            ? parse_specification_expression(context, unit, symbol->statement_function_line,
                                             symbol->statement_function_text, (F2cTokenRange){0},
                                             "statement-function result")
            : NULL;
    f2c_validation_expression_calls(context, unit, line, source_line,
                                    symbol->initializer_expression);
    f2c_validation_expression_calls(context, unit, line, source_line,
                                    symbol->character_length_expression);
    if (symbol->statement_function_expression != NULL) {
        const char *statement_source =
            f2c_validation_unit_line(context, unit, symbol->statement_function_line);
        f2c_validation_expression_calls(context, unit, symbol->statement_function_line,
                                        statement_source, symbol->statement_function_expression);
        if (symbol->statement_function_expression->rank != 0U)
            f2c_diagnostic_at(context, symbol->statement_function_line, 1U, 1,
                              "statement function '%s' must produce a scalar value", symbol->name);
        if (symbol->type != TYPE_UNKNOWN &&
            symbol->statement_function_expression->type != TYPE_UNKNOWN &&
            !f2c_validation_type_compatible(symbol->type,
                                            symbol->statement_function_expression->type))
            f2c_diagnostic_at(context, symbol->statement_function_line, 1U, 1,
                              "statement function '%s' result type is incompatible with its "
                              "declaration",
                              symbol->name);
    }
    validate_integer_specification(context, line, source_line, symbol->character_length_expression,
                                   "character length");
    validate_declaration_initializer(context, line, source_line, symbol);
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        Dimension *shape = &symbol->dimensions[dimension];
        f2c_expr_free(shape->lower_expression);
        f2c_expr_free(shape->upper_expression);
        shape->lower_expression = parse_specification_expression(
            context, unit, line, shape->lower, symbol->dimension_lower_syntax[dimension],
            "array lower bound");
        shape->upper_expression =
            shape->kind == F2C_DIMENSION_EXPLICIT
                ? parse_specification_expression(context, unit, line, shape->upper,
                                                 symbol->dimension_upper_syntax[dimension],
                                                 "array upper bound")
                : NULL;
        f2c_validation_expression_calls(context, unit, line, source_line, shape->lower_expression);
        f2c_validation_expression_calls(context, unit, line, source_line, shape->upper_expression);
        validate_integer_specification(context, line, source_line, shape->lower_expression,
                                       "array lower bound");
        validate_integer_specification(context, line, source_line, shape->upper_expression,
                                       "array upper bound");
    }
    f2c_shape_from_symbol(unit, &symbol->shape, symbol);
}

void f2c_validate_unit_expressions(Context *context, Unit *unit) {
    size_t i;
    size_t type_index;
    for (i = 0U; i < unit->symbol_count; ++i)
        validate_symbol_expressions(context, unit, &unit->symbols[i]);
    for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
        F2cDerivedType *derived = &unit->derived_types[type_index];
        for (i = 0U; i < derived->component_count; ++i)
            validate_symbol_expressions(context, unit, &derived->components[i]);
    }
    f2c_validation_bind_constructs(context, unit);
    for (i = 0U; i < unit->statement_count; ++i)
        validate_statement(context, unit, &unit->statements[i]);
    f2c_validation_select_case_constructs(context, unit);
}
