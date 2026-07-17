#include "semantic/validation/private.h"

#include <stdlib.h>
#include <string.h>

const char *f2c_validation_unit_line(const Context *context, const Unit *unit, size_t line) {
    size_t i;
    for (i = unit->begin; i < unit->end && i < context->lines.count; ++i) {
        if (context->lines.items[i].number == line)
            return context->lines.items[i].text;
    }
    return NULL;
}

size_t f2c_validation_expression_column(const char *statement_text, const F2cExpr *expression) {
    const char *match;
    size_t source_length;
    size_t offset;
    if (expression == NULL || expression->source == NULL)
        return 1U;
    source_length = strlen(expression->source);
    offset = expression->parse_error_offset < source_length ? expression->parse_error_offset
                                                            : source_length;
    match = statement_text != NULL ? strstr(statement_text, expression->source) : NULL;
    return match != NULL ? (size_t)(match - statement_text) + offset + 1U : offset + 1U;
}

size_t f2c_validation_expression_start_column(const char *statement_text,
                                              const F2cExpr *expression) {
    const char *match = expression != NULL && expression->source != NULL && statement_text != NULL
                            ? strstr(statement_text, expression->source)
                            : NULL;
    return match != NULL ? (size_t)(match - statement_text) + 1U : 1U;
}

void f2c_validation_report_parse_error(Context *context, size_t line, const char *statement_text,
                                       const F2cExpr *expression, const char *role) {
    size_t length;
    size_t offset;
    size_t remaining;
    if (expression == NULL || expression->source == NULL ||
        expression->parse_error_offset == SIZE_MAX)
        return;
    length = strlen(expression->source);
    offset = expression->parse_error_offset < length ? expression->parse_error_offset : length;
    remaining = length - offset;
    if (length == 0U) {
        if (expression->parse_error_span.begin.line != 0U)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &expression->parse_error_span,
                                     1, "malformed %s expression: expression is empty", role);
        else
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_column(statement_text, expression), 1,
                              "malformed %s expression: expression is empty", role);
    } else if (remaining == 0U) {
        if (expression->parse_error_span.begin.line != 0U)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &expression->parse_error_span,
                                     1, "malformed %s expression: unexpected end of '%.*s'", role,
                                     (int)(length > 80U ? 80U : length), expression->source);
        else
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_column(statement_text, expression), 1,
                              "malformed %s expression: unexpected end of '%.*s'", role,
                              (int)(length > 80U ? 80U : length), expression->source);
    } else {
        if (expression->parse_error_span.begin.line != 0U)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &expression->parse_error_span,
                                     1, "malformed %s expression: unexpected token near '%.*s'",
                                     role, (int)(remaining > 32U ? 32U : remaining),
                                     expression->source + offset);
        else
            f2c_diagnostic_at(
                context, line, f2c_validation_expression_column(statement_text, expression), 1,
                "malformed %s expression: unexpected token near '%.*s'", role,
                (int)(remaining > 32U ? 32U : remaining), expression->source + offset);
    }
}

static void validate_constructor_semantics_impl(Context *context, Unit *unit, size_t line,
                                                const char *statement_text,
                                                const F2cExpr *expression, int inside_constructor) {
    size_t i;
    if (expression == NULL)
        return;
    if (expression->kind == F2C_EXPR_IMPLIED_DO) {
        int64_t step;
        const size_t value_count =
            expression->child_count >= 3U ? expression->child_count - 3U : 0U;
        if (!inside_constructor) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_column(statement_text, expression), 1,
                              "implied DO is valid only inside an array constructor");
        }
        if (value_count == 0U) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_column(statement_text, expression), 1,
                              "array-constructor implied DO has no values");
        }
        if (expression->symbol == NULL || expression->symbol->type != TYPE_INTEGER ||
            expression->symbol->rank != 0U) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_column(statement_text, expression), 1,
                              "array-constructor implied-DO iterator '%s' must be a scalar "
                              "INTEGER",
                              expression->text != NULL ? expression->text : "<unknown>");
        }
        if (expression->child_count < 3U) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_column(statement_text, expression), 1,
                              "array-constructor implied DO has incomplete bounds");
        } else {
            const F2cExpr *initial = expression->children[value_count];
            const F2cExpr *limit = expression->children[value_count + 1U];
            const F2cExpr *step_expression = expression->children[value_count + 2U];
            if (initial->type != TYPE_INTEGER || initial->rank != 0U ||
                limit->type != TYPE_INTEGER || limit->rank != 0U ||
                step_expression->type != TYPE_INTEGER || step_expression->rank != 0U) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_column(statement_text, expression), 1,
                                  "array-constructor implied-DO bounds must be scalar INTEGER "
                                  "expressions");
            }
            if (f2c_evaluate_integer_constant(unit, step_expression, &step) && step == 0) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_column(statement_text, expression), 1,
                                  "array-constructor implied-DO step must not be zero");
            }
        }
    }
    for (i = 0U; i < expression->child_count; ++i)
        validate_constructor_semantics_impl(
            context, unit, line, statement_text, expression->children[i],
            inside_constructor || expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR ||
                expression->kind == F2C_EXPR_IMPLIED_DO);
}

void f2c_validation_constructor(Context *context, Unit *unit, size_t line,
                                const char *statement_text, const F2cExpr *expression) {
    validate_constructor_semantics_impl(context, unit, line, statement_text, expression, 0);
}

static uint64_t unsigned_distance(int64_t lower, int64_t upper) {
    if (lower >= 0 || upper < 0)
        return (uint64_t)(upper - lower);
    return (uint64_t)upper + (uint64_t)(-(lower + 1)) + UINT64_C(1);
}

static uint64_t unsigned_magnitude(int64_t value) {
    return value >= 0 ? (uint64_t)value : (uint64_t)(-(value + 1)) + UINT64_C(1);
}

static int checked_extent_add(uint64_t left, uint64_t right, uint64_t *result) {
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static int checked_extent_multiply(uint64_t left, uint64_t right, uint64_t *result) {
    if (left != 0U && right > UINT64_MAX / left)
        return 0;
    *result = left * right;
    return 1;
}

static int symbol_constant_extent(Unit *unit, const Symbol *symbol, uint64_t *extent) {
    uint64_t total = UINT64_C(1);
    size_t dimension;
    if (symbol == NULL || symbol->rank == 0U || symbol->allocatable)
        return 0;
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        int64_t lower;
        int64_t upper;
        uint64_t dimension_extent;
        if (symbol->dimensions[dimension].lower == NULL ||
            symbol->dimensions[dimension].upper == NULL ||
            strcmp(symbol->dimensions[dimension].upper, "*") == 0 ||
            !f2c_evaluate_integer_text(unit, symbol->dimensions[dimension].lower, &lower) ||
            !f2c_evaluate_integer_text(unit, symbol->dimensions[dimension].upper, &upper))
            return 0;
        if (upper >= lower) {
            dimension_extent = unsigned_distance(lower, upper);
            if (dimension_extent == UINT64_MAX)
                return -1;
            ++dimension_extent;
        } else {
            dimension_extent = UINT64_C(0);
        }
        if (!checked_extent_multiply(total, dimension_extent, &total))
            return -1;
    }
    *extent = total;
    return 1;
}

static int constructor_constant_extent(Unit *unit, const F2cExpr *expression, uint64_t *extent) {
    uint64_t total = UINT64_C(0);
    size_t i;
    if (expression == NULL)
        return 0;
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        for (i = 0U; i < expression->child_count; ++i) {
            uint64_t child_extent;
            const int known =
                constructor_constant_extent(unit, expression->children[i], &child_extent);
            if (known <= 0)
                return known;
            if (!checked_extent_add(total, child_extent, &total))
                return -1;
        }
        *extent = total;
        return 1;
    }
    if (expression->kind == F2C_EXPR_IMPLIED_DO) {
        const size_t value_count =
            expression->child_count >= 3U ? expression->child_count - 3U : 0U;
        int64_t first;
        int64_t last;
        int64_t step;
        uint64_t iterations;
        if (value_count == 0U ||
            !f2c_evaluate_integer_constant(unit, expression->children[value_count], &first) ||
            !f2c_evaluate_integer_constant(unit, expression->children[value_count + 1U], &last) ||
            !f2c_evaluate_integer_constant(unit, expression->children[value_count + 2U], &step) ||
            step == 0)
            return 0;
        if ((step > 0 && first > last) || (step < 0 && first < last)) {
            *extent = UINT64_C(0);
            return 1;
        }
        iterations = unsigned_distance(step > 0 ? first : last, step > 0 ? last : first) /
                     unsigned_magnitude(step);
        if (iterations == UINT64_MAX)
            return -1;
        ++iterations;
        for (i = 0U; i < value_count; ++i) {
            uint64_t child_extent;
            const int known =
                constructor_constant_extent(unit, expression->children[i], &child_extent);
            if (known <= 0)
                return known;
            if (!checked_extent_add(total, child_extent, &total))
                return -1;
        }
        if (!checked_extent_multiply(total, iterations, &total))
            return -1;
        *extent = total;
        return 1;
    }
    if (expression->kind == F2C_EXPR_NAME && expression->rank != 0U)
        return symbol_constant_extent(unit, expression->symbol, extent);
    if (expression->rank != 0U)
        return 0;
    *extent = UINT64_C(1);
    return 1;
}

int f2c_validation_type_compatible(Type target, Type source) {
    if (target == TYPE_DERIVED || source == TYPE_DERIVED)
        return target == TYPE_DERIVED && source == TYPE_DERIVED;
    if (target == TYPE_CHARACTER || source == TYPE_CHARACTER)
        return target == TYPE_CHARACTER && source == TYPE_CHARACTER;
    if (target == TYPE_LOGICAL || source == TYPE_LOGICAL)
        return target == TYPE_LOGICAL && source == TYPE_LOGICAL;
    return f2c_type_is_numeric(target) && f2c_type_is_numeric(source);
}

int f2c_validation_shapes_mismatch(const F2cExpr *left, const F2cExpr *right,
                                   size_t *dimension_out) {
    size_t dimension;
    if (left == NULL || right == NULL || left->rank == 0U || right->rank == 0U ||
        left->rank != right->rank)
        return 0;
    for (dimension = 0U; dimension < left->rank && dimension < F2C_MAX_RANK; ++dimension) {
        if (left->shape.dimensions[dimension].extent_known &&
            right->shape.dimensions[dimension].extent_known &&
            left->shape.dimensions[dimension].extent != right->shape.dimensions[dimension].extent) {
            if (dimension_out != NULL)
                *dimension_out = dimension;
            return 1;
        }
    }
    return 0;
}

int f2c_validation_symbol_shape_mismatch(const Symbol *left, const F2cExpr *right,
                                         size_t *dimension_out) {
    size_t dimension;
    if (left == NULL || right == NULL || left->rank == 0U || right->rank == 0U ||
        left->rank != right->rank)
        return 0;
    for (dimension = 0U; dimension < left->rank && dimension < F2C_MAX_RANK; ++dimension) {
        if (left->shape.dimensions[dimension].extent_known &&
            right->shape.dimensions[dimension].extent_known &&
            left->shape.dimensions[dimension].extent != right->shape.dimensions[dimension].extent) {
            if (dimension_out != NULL)
                *dimension_out = dimension;
            return 1;
        }
    }
    return 0;
}

void f2c_validation_intrinsic_assignment(Context *context, const F2cStatement *statement) {
    const F2cExpr *left = statement->left;
    const F2cExpr *right = statement->right;
    if (statement->kind != F2C_STMT_ASSIGNMENT || left == NULL || right == NULL)
        return;
    if (left->parse_error_offset != SIZE_MAX || right->parse_error_offset != SIZE_MAX)
        return;
    if (!left->definable && left->kind != F2C_EXPR_CALL) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, left), 1,
                          "assignment target is not definable");
    }
    if (left->type != TYPE_UNKNOWN && right->type != TYPE_UNKNOWN &&
        !f2c_validation_type_compatible(left->type, right->type)) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, right), 1,
                          "assignment types are incompatible");
    } else if (left->type == TYPE_DERIVED && right->type == TYPE_DERIVED &&
               left->derived_type != right->derived_type) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, right), 1,
                          "assignment derived types are incompatible");
    }
    if (left->rank == 0U && right->rank != 0U) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, right), 1,
                          "rank-%zu array cannot be assigned to a scalar", right->rank);
    } else if (left->rank != 0U && right->rank != 0U && left->rank != right->rank) {
        f2c_diagnostic_at(
            context, statement->line, f2c_validation_expression_column(statement->text, right), 1,
            "assignment rank mismatch: target rank %zu, value rank %zu", left->rank, right->rank);
    } else if (right->kind != F2C_EXPR_ARRAY_CONSTRUCTOR &&
               !(left->symbol != NULL && left->kind == F2C_EXPR_NAME &&
                 left->symbol->allocatable)) {
        size_t dimension;
        if (f2c_validation_shapes_mismatch(left, right, &dimension)) {
            f2c_diagnostic_at(
                context, statement->line, f2c_validation_expression_column(statement->text, right),
                1,
                "assignment shape mismatch in dimension %zu: target extent %llu, value extent "
                "%llu",
                dimension + 1U, (unsigned long long)left->shape.dimensions[dimension].extent,
                (unsigned long long)right->shape.dimensions[dimension].extent);
        }
    }
}

void f2c_validation_constructor_assignment(Context *context, Unit *unit,
                                           const F2cStatement *statement) {
    const F2cExpr *left = statement->left;
    const F2cExpr *right = statement->right;
    uint64_t left_extent;
    uint64_t right_extent;
    int left_known;
    int right_known;
    if (statement->kind != F2C_STMT_ASSIGNMENT || left == NULL || right == NULL ||
        left->kind != F2C_EXPR_NAME || left->symbol == NULL || left->rank == 0U ||
        right->kind != F2C_EXPR_ARRAY_CONSTRUCTOR)
        return;
    if (!f2c_validation_type_compatible(left->type, right->type)) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, right), 1,
                          "array-constructor element type is incompatible with target '%s'",
                          left->text != NULL ? left->text : "<unknown>");
    }
    left_known = symbol_constant_extent(unit, left->symbol, &left_extent);
    right_known = constructor_constant_extent(unit, right, &right_extent);
    if (left_known < 0 || right_known < 0) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, right), 1,
                          "array-constructor extent overflows the supported size range");
    } else if (left_known > 0 && right_known > 0 && left_extent != right_extent) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, right), 1,
                          "array-constructor extent %llu does not match target extent %llu",
                          (unsigned long long)right_extent, (unsigned long long)left_extent);
    }
}

void f2c_validation_io_item(Context *context, size_t line, const char *statement_text,
                            const F2cIoItem *item) {
    size_t i;
    f2c_validation_report_parse_error(context, line, statement_text, item->expression, "I/O item");
    f2c_validation_report_parse_error(context, line, statement_text, item->iterator,
                                      "implied-DO iterator");
    f2c_validation_report_parse_error(context, line, statement_text, item->initial,
                                      "implied-DO initial");
    f2c_validation_report_parse_error(context, line, statement_text, item->limit,
                                      "implied-DO limit");
    f2c_validation_report_parse_error(context, line, statement_text, item->step, "implied-DO step");
    for (i = 0U; i < item->child_count; ++i)
        f2c_validation_io_item(context, line, statement_text, &item->children[i]);
}

static const char *io_statement_name(F2cStatementKind kind) {
    switch (kind) {
    case F2C_STMT_READ:
        return "READ";
    case F2C_STMT_WRITE:
        return "WRITE";
    case F2C_STMT_OPEN:
        return "OPEN";
    case F2C_STMT_REWIND:
        return "REWIND";
    case F2C_STMT_CLOSE:
        return "CLOSE";
    default:
        return "I/O statement";
    }
}

static const char *io_control_name(F2cIoControlKind kind) {
    static const char *const names[] = {
        "positional", "unknown", "UNIT", "FMT",          "NML",   "END",     "EOR",   "ERR",
        "IOSTAT",     "IOMSG",   "SIZE", "ADVANCE",      "REC",   "POS",     "FILE",  "STATUS",
        "ACCESS",     "ACTION",  "FORM", "RECL",         "BLANK", "DECIMAL", "DELIM", "ENCODING",
        "PAD",        "ROUND",   "SIGN", "ASYNCHRONOUS", "ID",    "NEWUNIT"};
    return (size_t)kind < sizeof(names) / sizeof(names[0]) ? names[kind] : "unknown";
}

static int io_control_supported(F2cStatementKind statement_kind, F2cIoControlKind control_kind) {
    if (statement_kind == F2C_STMT_READ)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_FMT ||
               control_kind == F2C_IO_CONTROL_NML || control_kind == F2C_IO_CONTROL_END ||
               control_kind == F2C_IO_CONTROL_EOR || control_kind == F2C_IO_CONTROL_ERR ||
               control_kind == F2C_IO_CONTROL_IOSTAT || control_kind == F2C_IO_CONTROL_IOMSG ||
               control_kind == F2C_IO_CONTROL_SIZE || control_kind == F2C_IO_CONTROL_ADVANCE;
    if (statement_kind == F2C_STMT_WRITE)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_FMT ||
               control_kind == F2C_IO_CONTROL_NML || control_kind == F2C_IO_CONTROL_ERR ||
               control_kind == F2C_IO_CONTROL_IOSTAT || control_kind == F2C_IO_CONTROL_IOMSG ||
               control_kind == F2C_IO_CONTROL_ADVANCE;
    if (statement_kind == F2C_STMT_OPEN)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_FILE ||
               control_kind == F2C_IO_CONTROL_STATUS || control_kind == F2C_IO_CONTROL_ERR ||
               control_kind == F2C_IO_CONTROL_IOSTAT || control_kind == F2C_IO_CONTROL_FORM;
    if (statement_kind == F2C_STMT_CLOSE || statement_kind == F2C_STMT_REWIND)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_ERR ||
               control_kind == F2C_IO_CONTROL_IOSTAT;
    return 0;
}

static F2cIoControlKind positional_io_control_kind(F2cStatementKind statement_kind,
                                                   size_t position) {
    if ((statement_kind == F2C_STMT_READ || statement_kind == F2C_STMT_WRITE) && position == 0U)
        return F2C_IO_CONTROL_UNIT;
    if ((statement_kind == F2C_STMT_READ || statement_kind == F2C_STMT_WRITE) && position == 1U)
        return F2C_IO_CONTROL_FMT;
    if (statement_kind == F2C_STMT_OPEN && position == 0U)
        return F2C_IO_CONTROL_UNIT;
    if ((statement_kind == F2C_STMT_CLOSE || statement_kind == F2C_STMT_REWIND) && position == 0U)
        return F2C_IO_CONTROL_UNIT;
    return F2C_IO_CONTROL_UNKNOWN;
}

static int scalar_type(const F2cExpr *expression, Type type) {
    return expression != NULL && expression->type == type && expression->rank == 0U;
}

static void validate_io_control_type(Context *context, Unit *unit, const F2cStatement *statement,
                                     const F2cIoControl *control, F2cIoControlKind semantic_kind) {
    const F2cExpr *value = control->value;
    const size_t column = f2c_validation_expression_start_column(statement->text, value);
    const char *statement_name = io_statement_name(statement->kind);
    if (semantic_kind == F2C_IO_CONTROL_UNIT) {
        if (control->asterisk) {
            if (statement->kind != F2C_STMT_READ && statement->kind != F2C_STMT_WRITE) {
                f2c_diagnostic_at(context, statement->line, column, 1,
                                  "%s UNIT= cannot be an asterisk", statement_name);
            }
        } else if (value != NULL && value->type == TYPE_CHARACTER) {
            if (statement->kind == F2C_STMT_OPEN) {
                f2c_diagnostic_at(context, statement->line, column, 1,
                                  "OPEN UNIT= must be an asterisk or a scalar INTEGER");
            } else if (value->rank > 1U || (value->rank == 1U && value->kind != F2C_EXPR_NAME) ||
                       value->value_category != F2C_VALUE_VARIABLE ||
                       (statement->kind == F2C_STMT_WRITE && !value->definable)) {
                f2c_diagnostic_at(context, statement->line, column, 1,
                                  "internal-file %s UNIT= must be a CHARACTER scalar or "
                                  "rank-one array variable%s",
                                  statement_name,
                                  statement->kind == F2C_STMT_WRITE ? " that is definable" : "");
            }
        } else if (!scalar_type(value, TYPE_INTEGER)) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s UNIT= must be an asterisk or a scalar INTEGER", statement_name);
        }
    } else if (semantic_kind == F2C_IO_CONTROL_FMT) {
        if (!control->asterisk && !scalar_type(value, TYPE_CHARACTER) &&
            !scalar_type(value, TYPE_INTEGER)) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s FMT= must be an asterisk, scalar CHARACTER format, or "
                              "statement label",
                              statement_name);
        }
    } else if (semantic_kind == F2C_IO_CONTROL_NML) {
        const char *name = value != NULL ? value->text : NULL;
        if (control->asterisk || value == NULL || value->kind != F2C_EXPR_NAME || name == NULL ||
            f2c_find_namelist(unit, name) == NULL) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s NML= must name a declared NAMELIST group", statement_name);
        }
    } else if (semantic_kind == F2C_IO_CONTROL_END || semantic_kind == F2C_IO_CONTROL_EOR ||
               semantic_kind == F2C_IO_CONTROL_ERR) {
        if (control->asterisk || value == NULL || value->kind != F2C_EXPR_INTEGER_LITERAL ||
            value->rank != 0U) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s %s= must be a statement label", statement_name,
                              io_control_name(semantic_kind));
        }
    } else if (semantic_kind == F2C_IO_CONTROL_IOSTAT) {
        if (control->asterisk || !scalar_type(value, TYPE_INTEGER) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s IOSTAT= must be a definable scalar INTEGER", statement_name);
        }
    } else if (semantic_kind == F2C_IO_CONTROL_SIZE) {
        if (control->asterisk || !scalar_type(value, TYPE_INTEGER) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s SIZE= must be a definable scalar INTEGER", statement_name);
        }
    } else if (semantic_kind == F2C_IO_CONTROL_IOMSG) {
        if (control->asterisk || !scalar_type(value, TYPE_CHARACTER) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s IOMSG= must be a definable scalar CHARACTER variable",
                              statement_name);
        }
    } else if (semantic_kind == F2C_IO_CONTROL_ADVANCE || semantic_kind == F2C_IO_CONTROL_FILE ||
               semantic_kind == F2C_IO_CONTROL_STATUS || semantic_kind == F2C_IO_CONTROL_FORM) {
        if (control->asterisk || !scalar_type(value, TYPE_CHARACTER)) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s %s= must be a scalar CHARACTER expression", statement_name,
                              io_control_name(semantic_kind));
        }
    }
}

static void validate_io_item_semantics(Context *context, Unit *unit, const F2cStatement *statement,
                                       const F2cIoItem *item, int input) {
    size_t i;
    if (item == NULL)
        return;
    if (item->implied_do) {
        int64_t step;
        if (!scalar_type(item->iterator, TYPE_INTEGER) || !item->iterator->definable) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, item->iterator), 1,
                "I/O implied-DO iterator must be a definable scalar INTEGER");
        }
        if (!scalar_type(item->initial, TYPE_INTEGER) || !scalar_type(item->limit, TYPE_INTEGER) ||
            !scalar_type(item->step, TYPE_INTEGER)) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, item->initial), 1,
                "I/O implied-DO bounds and step must be scalar INTEGER "
                "expressions");
        } else if (f2c_evaluate_integer_constant(unit, item->step, &step) && step == 0) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, item->step),
                              1, "I/O implied-DO step cannot be zero");
        }
        for (i = 0U; i < item->child_count; ++i)
            validate_io_item_semantics(context, unit, statement, &item->children[i], input);
        return;
    }
    if (input && item->expression != NULL && !item->expression->definable) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, item->expression),
                          1, "READ item must be definable");
    }
}

void f2c_validation_io_statement(Context *context, Unit *unit, F2cStatement *statement) {
    unsigned char seen[F2C_IO_CONTROL_NEWUNIT + 1U] = {0};
    size_t positional_count = 0U;
    size_t i;
    int saw_keyword = 0;
    for (i = 0U; i < statement->control_count; ++i) {
        F2cIoControl *control = &statement->io_controls[i];
        F2cIoControlKind semantic_kind = control->kind;
        if (control->kind == F2C_IO_CONTROL_POSITIONAL) {
            if (saw_keyword) {
                f2c_diagnostic_at(
                    context, statement->line,
                    f2c_validation_expression_start_column(statement->text, control->value), 1,
                    "positional I/O control follows a keyword control in %s",
                    io_statement_name(statement->kind));
            }
            semantic_kind = positional_io_control_kind(statement->kind, positional_count++);
            if (semantic_kind == F2C_IO_CONTROL_UNKNOWN) {
                f2c_diagnostic_at(
                    context, statement->line,
                    f2c_validation_expression_start_column(statement->text, control->value), 1,
                    "too many positional I/O controls in %s", io_statement_name(statement->kind));
                continue;
            }
        } else {
            saw_keyword = 1;
            if (control->kind == F2C_IO_CONTROL_UNKNOWN) {
                f2c_diagnostic_at(
                    context, statement->line,
                    f2c_validation_expression_start_column(statement->text, control->value), 1,
                    "unknown I/O control '%s' in %s",
                    control->keyword != NULL ? control->keyword : "<unknown>",
                    io_statement_name(statement->kind));
                continue;
            }
        }
        if (seen[semantic_kind]) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, control->value), 1,
                "duplicate %s= control in %s", io_control_name(semantic_kind),
                io_statement_name(statement->kind));
            continue;
        }
        seen[semantic_kind] = 1U;
        if (!io_control_supported(statement->kind, semantic_kind)) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, control->value), 1,
                "%s= is not yet supported in %s", io_control_name(semantic_kind),
                io_statement_name(statement->kind));
            continue;
        }
        validate_io_control_type(context, unit, statement, control, semantic_kind);
    }
    if (!seen[F2C_IO_CONTROL_UNIT]) {
        f2c_diagnostic_at(context, statement->line, 1U, 1,
                          "%s requires UNIT=", io_statement_name(statement->kind));
    }
    if (seen[F2C_IO_CONTROL_FMT] && seen[F2C_IO_CONTROL_NML]) {
        f2c_diagnostic_at(context, statement->line, 1U, 1, "%s cannot specify both FMT= and NML=",
                          io_statement_name(statement->kind));
    }
    if (seen[F2C_IO_CONTROL_NML] && statement->io_item_count != 0U) {
        f2c_diagnostic_at(context, statement->line, 1U, 1,
                          "%s with NML= cannot have an explicit I/O item list",
                          io_statement_name(statement->kind));
    }
    if ((seen[F2C_IO_CONTROL_EOR] || seen[F2C_IO_CONTROL_SIZE]) && !seen[F2C_IO_CONTROL_ADVANCE]) {
        f2c_diagnostic_at(context, statement->line, 1U, 1, "EOR= and SIZE= require ADVANCE='NO'");
    }
    for (i = 0U; i < statement->io_item_count; ++i)
        validate_io_item_semantics(context, unit, statement, &statement->io_items[i],
                                   statement->kind == F2C_STMT_READ);
}

const char *f2c_validation_type_name(Type type) {
    switch (type) {
    case TYPE_INTEGER:
        return "INTEGER";
    case TYPE_REAL:
        return "REAL";
    case TYPE_DOUBLE:
        return "DOUBLE PRECISION";
    case TYPE_COMPLEX:
        return "COMPLEX";
    case TYPE_DOUBLE_COMPLEX:
        return "DOUBLE COMPLEX";
    case TYPE_LOGICAL:
        return "LOGICAL";
    case TYPE_CHARACTER:
        return "CHARACTER";
    case TYPE_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

Unit *f2c_validation_find_procedure(Context *context, Unit *caller, const char *name) {
    Symbol *symbol;
    const char *resolved_name = name;
    size_t i;
    if (name == NULL)
        return NULL;
    symbol = f2c_find_symbol(caller, name);
    if (symbol != NULL && symbol->external && symbol->c_name != NULL)
        resolved_name = symbol->c_name;
    for (i = 0U; i < context->procedures.count; ++i) {
        Procedure *procedure = &context->procedures.items[i];
        if (strcmp(procedure->name, resolved_name) == 0 || strcmp(procedure->name, name) == 0)
            return procedure->definition;
        if (procedure->definition != NULL && procedure->definition->internal &&
            procedure->definition->fortran_name != NULL &&
            strcmp(procedure->definition->fortran_name, name) == 0 &&
            ((caller->internal && caller->host_index == procedure->definition->host_index) ||
             (!caller->internal && procedure->definition->host_index < context->units.count &&
              &context->units.items[procedure->definition->host_index] == caller)))
            return procedure->definition;
        if (procedure->definition != NULL && procedure->definition->fortran_name != NULL &&
            strcmp(procedure->definition->fortran_name, name) == 0) {
            size_t module_index;
            for (module_index = 0U; module_index < context->modules.count; ++module_index) {
                Unit *module = &context->modules.items[module_index];
                const int owns_definition = procedure->definition->begin > module->end &&
                                            procedure->definition->begin < module->container_end;
                const int owns_caller = caller == module || (caller->begin > module->end &&
                                                             caller->begin < module->container_end);
                if (owns_definition && owns_caller)
                    return procedure->definition;
            }
        }
    }
    return NULL;
}

const F2cExpr *f2c_validation_actual_value(const F2cExpr *actual) {
    if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        return actual->children[0];
    return actual;
}
