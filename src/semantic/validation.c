#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static const char *unit_line_text(const Context *context, const Unit *unit, size_t line) {
    size_t i;
    for (i = unit->begin; i < unit->end && i < context->lines.count; ++i) {
        if (context->lines.items[i].number == line)
            return context->lines.items[i].text;
    }
    return NULL;
}

static size_t expression_column(const char *statement_text, const F2cExpr *expression) {
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

static size_t expression_start_column(const char *statement_text, const F2cExpr *expression) {
    const char *match = expression != NULL && expression->source != NULL && statement_text != NULL
                            ? strstr(statement_text, expression->source)
                            : NULL;
    return match != NULL ? (size_t)(match - statement_text) + 1U : 1U;
}

static void report_parse_error(Context *context, size_t line, const char *statement_text,
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
        f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                          "malformed %s expression: expression is empty", role);
    } else if (remaining == 0U) {
        f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                          "malformed %s expression: unexpected end of '%.*s'", role,
                          (int)(length > 80U ? 80U : length), expression->source);
    } else {
        f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
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
            f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                              "implied DO is valid only inside an array constructor");
        }
        if (value_count == 0U) {
            f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                              "array-constructor implied DO has no values");
        }
        if (expression->symbol == NULL || expression->symbol->type != TYPE_INTEGER ||
            expression->symbol->rank != 0U) {
            f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                              "array-constructor implied-DO iterator '%s' must be a scalar "
                              "INTEGER",
                              expression->text != NULL ? expression->text : "<unknown>");
        }
        if (expression->child_count < 3U) {
            f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                              "array-constructor implied DO has incomplete bounds");
        } else {
            const F2cExpr *initial = expression->children[value_count];
            const F2cExpr *limit = expression->children[value_count + 1U];
            const F2cExpr *step_expression = expression->children[value_count + 2U];
            if (initial->type != TYPE_INTEGER || initial->rank != 0U ||
                limit->type != TYPE_INTEGER || limit->rank != 0U ||
                step_expression->type != TYPE_INTEGER || step_expression->rank != 0U) {
                f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                                  "array-constructor implied-DO bounds must be scalar INTEGER "
                                  "expressions");
            }
            if (f2c_evaluate_integer_constant(unit, step_expression, &step) && step == 0) {
                f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
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

static void validate_constructor_semantics(Context *context, Unit *unit, size_t line,
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

static int constructor_type_compatible(Type target, Type source) {
    if (target == TYPE_DERIVED || source == TYPE_DERIVED)
        return target == TYPE_DERIVED && source == TYPE_DERIVED;
    if (target == TYPE_CHARACTER || source == TYPE_CHARACTER)
        return target == TYPE_CHARACTER && source == TYPE_CHARACTER;
    if (target == TYPE_LOGICAL || source == TYPE_LOGICAL)
        return target == TYPE_LOGICAL && source == TYPE_LOGICAL;
    return f2c_type_is_numeric(target) && f2c_type_is_numeric(source);
}

static int shapes_have_known_mismatch(const F2cExpr *left, const F2cExpr *right,
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

static int symbol_expression_shapes_have_known_mismatch(const Symbol *left, const F2cExpr *right,
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

static void validate_intrinsic_assignment(Context *context, const F2cStatement *statement) {
    const F2cExpr *left = statement->left;
    const F2cExpr *right = statement->right;
    if (statement->kind != F2C_STMT_ASSIGNMENT || left == NULL || right == NULL)
        return;
    if (left->parse_error_offset != SIZE_MAX || right->parse_error_offset != SIZE_MAX)
        return;
    if (!left->definable && left->kind != F2C_EXPR_CALL) {
        f2c_diagnostic_at(context, statement->line, expression_column(statement->text, left), 1,
                          "assignment target is not definable");
    }
    if (left->type != TYPE_UNKNOWN && right->type != TYPE_UNKNOWN &&
        !constructor_type_compatible(left->type, right->type)) {
        f2c_diagnostic_at(context, statement->line, expression_column(statement->text, right), 1,
                          "assignment types are incompatible");
    } else if (left->type == TYPE_DERIVED && right->type == TYPE_DERIVED &&
               left->derived_type != right->derived_type) {
        f2c_diagnostic_at(context, statement->line, expression_column(statement->text, right), 1,
                          "assignment derived types are incompatible");
    }
    if (left->rank == 0U && right->rank != 0U) {
        f2c_diagnostic_at(context, statement->line, expression_column(statement->text, right), 1,
                          "rank-%zu array cannot be assigned to a scalar", right->rank);
    } else if (left->rank != 0U && right->rank != 0U && left->rank != right->rank) {
        f2c_diagnostic_at(context, statement->line, expression_column(statement->text, right), 1,
                          "assignment rank mismatch: target rank %zu, value rank %zu", left->rank,
                          right->rank);
    } else if (right->kind != F2C_EXPR_ARRAY_CONSTRUCTOR &&
               !(left->symbol != NULL && left->kind == F2C_EXPR_NAME &&
                 left->symbol->allocatable)) {
        size_t dimension;
        if (shapes_have_known_mismatch(left, right, &dimension)) {
            f2c_diagnostic_at(
                context, statement->line, expression_column(statement->text, right), 1,
                "assignment shape mismatch in dimension %zu: target extent %llu, value extent "
                "%llu",
                dimension + 1U, (unsigned long long)left->shape.dimensions[dimension].extent,
                (unsigned long long)right->shape.dimensions[dimension].extent);
        }
    }
}

static void validate_constructor_assignment(Context *context, Unit *unit,
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
    if (!constructor_type_compatible(left->type, right->type)) {
        f2c_diagnostic_at(context, statement->line, expression_column(statement->text, right), 1,
                          "array-constructor element type is incompatible with target '%s'",
                          left->text != NULL ? left->text : "<unknown>");
    }
    left_known = symbol_constant_extent(unit, left->symbol, &left_extent);
    right_known = constructor_constant_extent(unit, right, &right_extent);
    if (left_known < 0 || right_known < 0) {
        f2c_diagnostic_at(context, statement->line, expression_column(statement->text, right), 1,
                          "array-constructor extent overflows the supported size range");
    } else if (left_known > 0 && right_known > 0 && left_extent != right_extent) {
        f2c_diagnostic_at(context, statement->line, expression_column(statement->text, right), 1,
                          "array-constructor extent %llu does not match target extent %llu",
                          (unsigned long long)right_extent, (unsigned long long)left_extent);
    }
}

static void validate_io_item(Context *context, size_t line, const char *statement_text,
                             const F2cIoItem *item) {
    size_t i;
    report_parse_error(context, line, statement_text, item->expression, "I/O item");
    report_parse_error(context, line, statement_text, item->iterator, "implied-DO iterator");
    report_parse_error(context, line, statement_text, item->initial, "implied-DO initial");
    report_parse_error(context, line, statement_text, item->limit, "implied-DO limit");
    report_parse_error(context, line, statement_text, item->step, "implied-DO step");
    for (i = 0U; i < item->child_count; ++i)
        validate_io_item(context, line, statement_text, &item->children[i]);
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
    const size_t column = expression_start_column(statement->text, value);
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
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, item->iterator), 1,
                              "I/O implied-DO iterator must be a definable scalar INTEGER");
        }
        if (!scalar_type(item->initial, TYPE_INTEGER) || !scalar_type(item->limit, TYPE_INTEGER) ||
            !scalar_type(item->step, TYPE_INTEGER)) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, item->initial), 1,
                              "I/O implied-DO bounds and step must be scalar INTEGER "
                              "expressions");
        } else if (f2c_evaluate_integer_constant(unit, item->step, &step) && step == 0) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, item->step), 1,
                              "I/O implied-DO step cannot be zero");
        }
        for (i = 0U; i < item->child_count; ++i)
            validate_io_item_semantics(context, unit, statement, &item->children[i], input);
        return;
    }
    if (input && item->expression != NULL && !item->expression->definable) {
        f2c_diagnostic_at(context, statement->line,
                          expression_start_column(statement->text, item->expression), 1,
                          "READ item must be definable");
    }
}

static void validate_io_statement_semantics(Context *context, Unit *unit, F2cStatement *statement) {
    unsigned char seen[F2C_IO_CONTROL_NEWUNIT + 1U] = {0};
    size_t positional_count = 0U;
    size_t i;
    int saw_keyword = 0;
    for (i = 0U; i < statement->control_count; ++i) {
        F2cIoControl *control = &statement->io_controls[i];
        F2cIoControlKind semantic_kind = control->kind;
        if (control->kind == F2C_IO_CONTROL_POSITIONAL) {
            if (saw_keyword) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, control->value), 1,
                                  "positional I/O control follows a keyword control in %s",
                                  io_statement_name(statement->kind));
            }
            semantic_kind = positional_io_control_kind(statement->kind, positional_count++);
            if (semantic_kind == F2C_IO_CONTROL_UNKNOWN) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, control->value), 1,
                                  "too many positional I/O controls in %s",
                                  io_statement_name(statement->kind));
                continue;
            }
        } else {
            saw_keyword = 1;
            if (control->kind == F2C_IO_CONTROL_UNKNOWN) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, control->value), 1,
                                  "unknown I/O control '%s' in %s",
                                  control->keyword != NULL ? control->keyword : "<unknown>",
                                  io_statement_name(statement->kind));
                continue;
            }
        }
        if (seen[semantic_kind]) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, control->value), 1,
                              "duplicate %s= control in %s", io_control_name(semantic_kind),
                              io_statement_name(statement->kind));
            continue;
        }
        seen[semantic_kind] = 1U;
        if (!io_control_supported(statement->kind, semantic_kind)) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, control->value), 1,
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

static const char *semantic_type_name(Type type) {
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

static Unit *find_procedure_definition(Context *context, Unit *caller, const char *name) {
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
    }
    return NULL;
}

static const F2cExpr *actual_value(const F2cExpr *actual) {
    if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U)
        return actual->children[0];
    return actual;
}

static int array_storage_sequence_actual(const F2cExpr *actual) {
    return actual != NULL &&
           (actual->kind == F2C_EXPR_NAME || actual->kind == F2C_EXPR_ARRAY_REFERENCE) &&
           actual->symbol != NULL && actual->symbol->rank != 0U;
}

static int interface_candidate_matches(Unit *candidate, F2cExpr *const *arguments,
                                       size_t argument_count, int subroutine_call) {
    unsigned char assigned[64] = {0};
    size_t next_positional = 0U;
    size_t i;
    int saw_keyword = 0;
    if (candidate->argument_count > 64U || argument_count > candidate->argument_count ||
        (subroutine_call && candidate->kind != UNIT_SUBROUTINE) ||
        (!subroutine_call && candidate->kind != UNIT_FUNCTION))
        return 0;
    for (i = 0U; i < argument_count; ++i) {
        const F2cExpr *actual = arguments != NULL ? arguments[i] : NULL;
        const F2cExpr *value = actual_value(actual);
        size_t target = SIZE_MAX;
        Symbol *dummy;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            size_t d;
            saw_keyword = 1;
            for (d = 0U; d < candidate->argument_count; ++d) {
                if (actual->text != NULL && strcmp(actual->text, candidate->arguments[d]) == 0) {
                    target = d;
                    break;
                }
            }
            if (target == SIZE_MAX)
                return 0;
        } else {
            if (saw_keyword)
                return 0;
            target = next_positional++;
        }
        if (target >= candidate->argument_count || assigned[target])
            return 0;
        assigned[target] = 1U;
        dummy = f2c_find_symbol(candidate, candidate->arguments[target]);
        if (dummy == NULL || value == NULL)
            continue;
        if (value->kind == F2C_EXPR_ABSENT_ARGUMENT) {
            if (!dummy->optional)
                return 0;
            continue;
        }
        if (dummy->external) {
            if (value->kind != F2C_EXPR_NAME || value->symbol == NULL || !value->symbol->external ||
                value->symbol->external_subroutine != dummy->external_subroutine)
                return 0;
        } else if ((dummy->type != TYPE_UNKNOWN && value->type != TYPE_UNKNOWN &&
                    (dummy->type != value->type || (dummy->kind != 0 && value->type_kind != 0 &&
                                                    dummy->kind != value->type_kind))) ||
                   (dummy->rank != value->rank &&
                    !(dummy->rank != 0U && array_storage_sequence_actual(value))) ||
                   (dummy->allocatable && (value->kind != F2C_EXPR_NAME || value->symbol == NULL ||
                                           !value->symbol->allocatable))) {
            return 0;
        }
    }
    for (i = 0U; i < candidate->argument_count; ++i) {
        Symbol *dummy;
        if (assigned[i])
            continue;
        dummy = f2c_find_symbol(candidate, candidate->arguments[i]);
        if (dummy == NULL || !dummy->optional)
            return 0;
    }
    return 1;
}

static size_t select_scope_interface(Unit *scope, const char *name, const char *resolved_name,
                                     F2cExpr *const *arguments, size_t argument_count,
                                     int subroutine_call, Unit **selection,
                                     size_t *matching_count) {
    size_t candidate_count = 0U;
    size_t i;
    int has_visible_name = 0;
    *selection = NULL;
    *matching_count = 0U;
    for (i = 0U; i < scope->interface_count; ++i) {
        Unit *candidate = &scope->interfaces[i];
        if (candidate->interface_abstract)
            continue;
        const char *visible_name = candidate->interface_generic_name != NULL
                                       ? candidate->interface_generic_name
                                       : candidate->name;
        if (strcmp(visible_name, name) == 0) {
            has_visible_name = 1;
            break;
        }
    }
    for (i = 0U; i < scope->interface_count; ++i) {
        Unit *candidate = &scope->interfaces[i];
        if (candidate->interface_abstract)
            continue;
        const char *visible_name = candidate->interface_generic_name != NULL
                                       ? candidate->interface_generic_name
                                       : candidate->name;
        const int named = has_visible_name ? strcmp(visible_name, name) == 0
                                           : strcmp(candidate->name, resolved_name) == 0;
        if (!named)
            continue;
        ++candidate_count;
        if (interface_candidate_matches(candidate, arguments, argument_count, subroutine_call)) {
            *selection = candidate;
            ++*matching_count;
        }
    }
    if (candidate_count == 1U && *matching_count == 0U) {
        for (i = 0U; i < scope->interface_count; ++i) {
            Unit *candidate = &scope->interfaces[i];
            if (candidate->interface_abstract)
                continue;
            const char *visible_name = candidate->interface_generic_name != NULL
                                           ? candidate->interface_generic_name
                                           : candidate->name;
            const int named = has_visible_name ? strcmp(visible_name, name) == 0
                                               : strcmp(candidate->name, resolved_name) == 0;
            if (named) {
                *selection = candidate;
                break;
            }
        }
    }
    return candidate_count;
}

static size_t select_explicit_interface(Context *context, Unit *caller, const char *name,
                                        F2cExpr *const *arguments, size_t argument_count,
                                        int subroutine_call, Unit **selection,
                                        size_t *matching_count) {
    Symbol *symbol = f2c_find_symbol(caller, name);
    const char *resolved_name =
        symbol != NULL && symbol->external && symbol->c_name != NULL ? symbol->c_name : name;
    size_t count;
    if (symbol != NULL && symbol->procedure_interface != NULL) {
        *selection = symbol->procedure_interface;
        *matching_count =
            interface_candidate_matches(*selection, arguments, argument_count, subroutine_call)
                ? 1U
                : 0U;
        return 1U;
    }
    count = select_scope_interface(caller, name, resolved_name, arguments, argument_count,
                                   subroutine_call, selection, matching_count);
    if (count == 0U && caller->internal && caller->host_index < context->units.count) {
        count = select_scope_interface(&context->units.items[caller->host_index], name,
                                       resolved_name, arguments, argument_count, subroutine_call,
                                       selection, matching_count);
    }
    return count;
}

static size_t call_column(const char *statement_text, const char *name) {
    const char *match =
        statement_text != NULL && name != NULL ? strstr(statement_text, name) : NULL;
    return match != NULL ? (size_t)(match - statement_text) + 1U : 1U;
}

static int has_explicit_argument_association(F2cExpr *const *arguments, size_t argument_count) {
    size_t i;
    for (i = 0U; i < argument_count; ++i) {
        if (arguments != NULL && arguments[i] != NULL &&
            (arguments[i]->kind == F2C_EXPR_KEYWORD_ARGUMENT ||
             arguments[i]->kind == F2C_EXPR_ABSENT_ARGUMENT))
            return 1;
    }
    return 0;
}

static size_t keyword_column(const char *statement_text, const F2cExpr *argument) {
    const char *match = statement_text != NULL && argument != NULL && argument->text != NULL
                            ? strstr(statement_text, argument->text)
                            : NULL;
    return match != NULL ? (size_t)(match - statement_text) + 1U : 1U;
}

static int bind_procedure_arguments(Context *context, Unit *definition, size_t line,
                                    const char *statement_text, const char *name,
                                    F2cExpr ***arguments_io, char ***items_io,
                                    size_t *argument_count_io) {
    F2cExpr *ordered_arguments[64] = {0};
    char *ordered_items[64] = {0};
    unsigned char assigned[64] = {0};
    unsigned char created[64] = {0};
    F2cExpr **arguments = arguments_io != NULL ? *arguments_io : NULL;
    char **items = items_io != NULL ? *items_io : NULL;
    const size_t argument_count = argument_count_io != NULL ? *argument_count_io : 0U;
    size_t next_positional = 0U;
    size_t i;
    int saw_keyword = 0;
    int valid = 1;
    if (argument_count > 64U || definition->argument_count > 64U) {
        f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                          "procedure '%s' exceeds the supported 64-argument interface limit", name);
        return 0;
    }
    for (i = 0U; i < argument_count; ++i) {
        F2cExpr *actual = arguments != NULL ? arguments[i] : NULL;
        size_t target = SIZE_MAX;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            size_t dummy_index;
            saw_keyword = 1;
            for (dummy_index = 0U; dummy_index < definition->argument_count; ++dummy_index) {
                if (actual->text != NULL &&
                    strcmp(actual->text, definition->arguments[dummy_index]) == 0) {
                    target = dummy_index;
                    break;
                }
            }
            if (target == SIZE_MAX) {
                f2c_diagnostic_at(context, line, keyword_column(statement_text, actual), 1,
                                  "procedure '%s' has no dummy argument named '%s'", name,
                                  actual->text != NULL ? actual->text : "<unknown>");
                valid = 0;
                continue;
            }
        } else {
            if (saw_keyword) {
                f2c_diagnostic_at(context, line, expression_start_column(statement_text, actual), 1,
                                  "positional argument follows a keyword argument in call to "
                                  "procedure '%s'",
                                  name);
                valid = 0;
                continue;
            }
            while (next_positional < definition->argument_count && assigned[next_positional])
                ++next_positional;
            target = next_positional++;
        }
        if (target >= definition->argument_count)
            continue;
        if (assigned[target]) {
            f2c_diagnostic_at(context, line, keyword_column(statement_text, actual), 1,
                              "dummy argument '%s' is associated more than once in call to "
                              "procedure '%s'",
                              definition->arguments[target], name);
            valid = 0;
            continue;
        }
        assigned[target] = 1U;
        ordered_arguments[target] = actual;
        if (items != NULL)
            ordered_items[target] = items[i];
        if (actual != NULL && actual->kind == F2C_EXPR_ABSENT_ARGUMENT) {
            Symbol *dummy = f2c_find_symbol(definition, definition->arguments[target]);
            if (actual->source != NULL) {
                f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                                  "an actual argument cannot be omitted with an empty positional "
                                  "slot in call to procedure '%s'; use a keyword for later "
                                  "arguments",
                                  name);
                valid = 0;
            } else if (dummy == NULL || !dummy->optional) {
                f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                                  "dummy argument '%s' of procedure '%s' is not OPTIONAL and "
                                  "cannot be omitted",
                                  definition->arguments[target], name);
                valid = 0;
            } else {
                actual->type = dummy->type;
                actual->rank = dummy->rank;
            }
        }
    }
    for (i = 0U; i < definition->argument_count; ++i) {
        Symbol *dummy;
        if (assigned[i])
            continue;
        dummy = f2c_find_symbol(definition, definition->arguments[i]);
        if (dummy == NULL || !dummy->optional) {
            f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                              "required dummy argument '%s' of procedure '%s' has no actual "
                              "argument",
                              definition->arguments[i], name);
            valid = 0;
            continue;
        }
        ordered_arguments[i] = f2c_expr_new_absent(dummy->type, dummy->rank);
        if (ordered_arguments[i] == NULL) {
            f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                              "out of memory while binding call to procedure '%s'", name);
            valid = 0;
            continue;
        }
        created[i] = 1U;
        if (items != NULL) {
            ordered_items[i] = f2c_strdup("");
            if (ordered_items[i] == NULL) {
                f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                                  "out of memory while binding call to procedure '%s'", name);
                valid = 0;
            }
        }
    }
    if (!valid) {
        for (i = 0U; i < definition->argument_count; ++i) {
            if (created[i]) {
                f2c_expr_free(ordered_arguments[i]);
                free(ordered_items[i]);
            }
        }
        return 0;
    }
    if (definition->argument_count != argument_count) {
        F2cExpr **replacement =
            definition->argument_count != 0U
                ? (F2cExpr **)realloc(arguments, definition->argument_count * sizeof(*arguments))
                : NULL;
        if (definition->argument_count != 0U && replacement == NULL) {
            for (i = 0U; i < definition->argument_count; ++i) {
                if (created[i]) {
                    f2c_expr_free(ordered_arguments[i]);
                    free(ordered_items[i]);
                }
            }
            f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                              "out of memory while expanding call to procedure '%s'", name);
            return 0;
        }
        arguments = replacement;
        *arguments_io = arguments;
        if (items_io != NULL) {
            char **item_replacement =
                definition->argument_count != 0U
                    ? (char **)realloc(items, definition->argument_count * sizeof(*items))
                    : NULL;
            if (definition->argument_count != 0U && item_replacement == NULL) {
                for (i = 0U; i < definition->argument_count; ++i) {
                    if (created[i]) {
                        f2c_expr_free(ordered_arguments[i]);
                        free(ordered_items[i]);
                    }
                }
                f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                                  "out of memory while expanding call to procedure '%s'", name);
                return 0;
            }
            items = item_replacement;
            *items_io = items;
        }
    }
    for (i = 0U; i < definition->argument_count; ++i) {
        arguments[i] = ordered_arguments[i];
        if (items != NULL)
            items[i] = ordered_items[i];
    }
    *argument_count_io = definition->argument_count;
    return 1;
}

static int procedure_signatures_compatible(const Symbol *expected, const Symbol *actual,
                                           unsigned int depth) {
    size_t i;
    if (expected == NULL || actual == NULL || depth > 16U ||
        expected->external_subroutine != actual->external_subroutine ||
        (!expected->external_subroutine && expected->type != TYPE_UNKNOWN &&
         actual->type != TYPE_UNKNOWN && expected->type != actual->type) ||
        expected->external_parameter_count != actual->external_parameter_count)
        return 0;
    for (i = 0U; i < expected->external_parameter_count; ++i) {
        Symbol *expected_procedure = expected->external_parameter_procedures[i];
        Symbol *actual_procedure = actual->external_parameter_procedures[i];
        if (expected->external_parameter_types[i] != actual->external_parameter_types[i] ||
            expected->external_parameter_ranks[i] != actual->external_parameter_ranks[i] ||
            expected->external_parameter_intents[i] != actual->external_parameter_intents[i] ||
            expected->external_parameter_optional[i] != actual->external_parameter_optional[i] ||
            expected->external_parameter_allocatable[i] !=
                actual->external_parameter_allocatable[i] ||
            expected->external_parameter_pointer[i] != actual->external_parameter_pointer[i] ||
            expected->external_parameter_derived_types[i] !=
                actual->external_parameter_derived_types[i] ||
            expected->external_parameter_polymorphic[i] !=
                actual->external_parameter_polymorphic[i] ||
            (expected_procedure == NULL) != (actual_procedure == NULL))
            return 0;
        if (expected_procedure != NULL &&
            !procedure_signatures_compatible(expected_procedure, actual_procedure, depth + 1U))
            return 0;
    }
    return 1;
}

static int derived_extends(const F2cDerivedType *candidate, const F2cDerivedType *ancestor) {
    while (candidate != NULL) {
        if (candidate == ancestor)
            return 1;
        candidate = candidate->parent;
    }
    return 0;
}

static F2cTypeBinding *find_type_binding(F2cDerivedType *derived, const char *name) {
    size_t index;
    if (derived == NULL || name == NULL)
        return NULL;
    for (index = 0U; index < derived->binding_count; ++index)
        if (strcmp(derived->bindings[index].name, name) == 0)
            return &derived->bindings[index];
    return find_type_binding(derived->parent, name);
}

static void validate_defined_io_binding(Context *context, F2cDerivedType *derived,
                                        F2cTypeBinding *binding, F2cDefinedIoKind kind) {
    const Symbol *procedure = binding != NULL ? &binding->procedure : NULL;
    const int formatted =
        kind == F2C_DEFINED_IO_READ_FORMATTED || kind == F2C_DEFINED_IO_WRITE_FORMATTED;
    const int input =
        kind == F2C_DEFINED_IO_READ_FORMATTED || kind == F2C_DEFINED_IO_READ_UNFORMATTED;
    const size_t expected_count = formatted ? 6U : 4U;
    const size_t line = context->lines.items[derived->begin].number;
    size_t parameter;
    if (procedure == NULL || !procedure->external_subroutine || binding->nopass ||
        procedure->type_bound_pass_index != 0U ||
        procedure->external_parameter_count != expected_count) {
        f2c_diagnostic(context, line, 1,
                       "defined I/O binding '%s' must be a PASS subroutine with %zu dummy "
                       "arguments",
                       binding != NULL ? binding->name : "<missing>", expected_count);
        return;
    }
    for (parameter = 0U; parameter < expected_count; ++parameter) {
        if (procedure->external_parameter_optional[parameter] ||
            procedure->external_parameter_allocatable[parameter] ||
            procedure->external_parameter_pointer[parameter])
            f2c_diagnostic(context, line, 1,
                           "defined I/O binding '%s' dummy %zu may not be OPTIONAL, "
                           "ALLOCATABLE, or POINTER",
                           binding->name, parameter + 1U);
    }
#define F2C_DTIO_REQUIRE(index, expected_type, expected_rank, expected_intent, description)        \
    do {                                                                                           \
        if (procedure->external_parameter_types[index] != (expected_type) ||                       \
            procedure->external_parameter_ranks[index] != (expected_rank) ||                       \
            procedure->external_parameter_intents[index] != (expected_intent))                     \
            f2c_diagnostic(context, line, 1, "defined I/O binding '%s' requires %s as dummy %zu",  \
                           binding->name, description, (size_t)(index) + 1U);                      \
    } while (0)
    F2C_DTIO_REQUIRE(0U, TYPE_DERIVED, 0U, input ? F2C_INTENT_INOUT : F2C_INTENT_IN,
                     input ? "scalar TYPE(dtv) INTENT(INOUT)" : "scalar TYPE(dtv) INTENT(IN)");
    if (procedure->external_parameter_derived_types[0] != derived)
        f2c_diagnostic(context, line, 1,
                       "defined I/O binding '%s' passed-object dummy must have declared type '%s'",
                       binding->name, derived->name);
    F2C_DTIO_REQUIRE(1U, TYPE_INTEGER, 0U, F2C_INTENT_IN,
                     "scalar default INTEGER unit with INTENT(IN)");
    if (formatted) {
        F2C_DTIO_REQUIRE(2U, TYPE_CHARACTER, 0U, F2C_INTENT_IN,
                         "scalar CHARACTER iotype with INTENT(IN)");
        F2C_DTIO_REQUIRE(3U, TYPE_INTEGER, 1U, F2C_INTENT_IN,
                         "rank-one default INTEGER v_list with INTENT(IN)");
        F2C_DTIO_REQUIRE(4U, TYPE_INTEGER, 0U, F2C_INTENT_OUT,
                         "scalar default INTEGER iostat with INTENT(OUT)");
        F2C_DTIO_REQUIRE(5U, TYPE_CHARACTER, 0U, F2C_INTENT_INOUT,
                         "scalar CHARACTER iomsg with INTENT(INOUT)");
    } else {
        F2C_DTIO_REQUIRE(2U, TYPE_INTEGER, 0U, F2C_INTENT_OUT,
                         "scalar default INTEGER iostat with INTENT(OUT)");
        F2C_DTIO_REQUIRE(3U, TYPE_CHARACTER, 0U, F2C_INTENT_INOUT,
                         "scalar CHARACTER iomsg with INTENT(INOUT)");
    }
#undef F2C_DTIO_REQUIRE
}

static int overriding_signatures_compatible(const F2cTypeBinding *parent,
                                            const F2cTypeBinding *child) {
    const Symbol *expected = &parent->procedure;
    const Symbol *actual = &child->procedure;
    size_t expected_argument = 0U;
    size_t actual_argument = 0U;
    if (expected->external_subroutine != actual->external_subroutine ||
        expected->external_parameter_count != actual->external_parameter_count ||
        parent->nopass != child->nopass ||
        (!expected->external_subroutine &&
         (expected->type != actual->type || expected->kind != actual->kind)))
        return 0;
    while (expected_argument < expected->external_parameter_count &&
           actual_argument < actual->external_parameter_count) {
        if (!parent->nopass && expected_argument == expected->type_bound_pass_index) {
            ++expected_argument;
            ++actual_argument;
            continue;
        }
        if (expected->external_parameter_types[expected_argument] !=
                actual->external_parameter_types[actual_argument] ||
            expected->external_parameter_kinds[expected_argument] !=
                actual->external_parameter_kinds[actual_argument] ||
            expected->external_parameter_ranks[expected_argument] !=
                actual->external_parameter_ranks[actual_argument] ||
            expected->external_parameter_intents[expected_argument] !=
                actual->external_parameter_intents[actual_argument] ||
            expected->external_parameter_optional[expected_argument] !=
                actual->external_parameter_optional[actual_argument] ||
            expected->external_parameter_allocatable[expected_argument] !=
                actual->external_parameter_allocatable[actual_argument] ||
            expected->external_parameter_pointer[expected_argument] !=
                actual->external_parameter_pointer[actual_argument])
            return 0;
        ++expected_argument;
        ++actual_argument;
    }
    return 1;
}

static Unit *binding_interface(Context *context, Unit *scope, F2cTypeBinding *binding) {
    if (binding->interface_name != NULL && binding->interface_name[0] != '\0')
        return f2c_find_interface_signature(context, scope, binding->interface_name, 1);
    return NULL;
}

static void resolve_type_binding(Context *context, Unit *scope, F2cDerivedType *derived,
                                 F2cTypeBinding *binding) {
    Unit *target =
        binding->deferred ? NULL : find_procedure_definition(context, scope, binding->target_name);
    Unit *interface = binding_interface(context, scope, binding);
    Unit *signature = interface != NULL ? interface : target;
    F2cTypeBinding *overridden = find_type_binding(derived->parent, binding->name);
    Symbol *procedure = &binding->procedure;
    Symbol *passed_dummy = NULL;
    size_t pass_index = 0U;
    size_t index;
    if (binding->deferred && !derived->abstract_type)
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "DEFERRED binding '%s' requires an ABSTRACT derived type", binding->name);
    if (binding->deferred && interface == NULL)
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "DEFERRED binding '%s' requires a visible abstract interface",
                       binding->name);
    if (!binding->deferred && target == NULL)
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "implementation '%s' for type-bound procedure '%s' is not visible",
                       binding->target_name != NULL ? binding->target_name : "<missing>",
                       binding->name);
    if (signature == NULL)
        return;
    if (interface != NULL && target != NULL) {
        Symbol expected = {0};
        Symbol actual = {0};
        if (!f2c_copy_procedure_signature(&expected, interface) ||
            !f2c_copy_procedure_signature(&actual, target) ||
            !procedure_signatures_compatible(&expected, &actual, 0U))
            f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                           "implementation '%s' does not match interface '%s' for binding '%s'",
                           binding->target_name, binding->interface_name, binding->name);
        free(expected.procedure_interface_name);
        free(actual.procedure_interface_name);
    }
    if (!f2c_copy_procedure_signature(procedure, signature)) {
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "out of memory resolving type-bound procedure '%s'", binding->name);
        return;
    }
    procedure->procedure_pointer = 1;
    procedure->type_bound = 1;
    procedure->type_bound_deferred = binding->deferred;
    procedure->type_bound_nopass = binding->nopass;
    procedure->derived_owner = overridden != NULL ? overridden->storage_owner : derived;
    binding->overridden = overridden;
    binding->storage_owner = overridden != NULL ? overridden->storage_owner : derived;
    if (overridden != NULL && overridden->non_overridable)
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "binding '%s' overrides a NON_OVERRIDABLE parent binding", binding->name);
    if (!binding->nopass) {
        if (binding->pass_name != NULL) {
            for (index = 0U; index < signature->argument_count; ++index)
                if (strcmp(signature->arguments[index], binding->pass_name) == 0) {
                    pass_index = index;
                    break;
                }
            if (index == signature->argument_count)
                f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                               "PASS dummy '%s' is not present in binding '%s'", binding->pass_name,
                               binding->name);
        }
        if (pass_index < signature->argument_count)
            passed_dummy = f2c_find_symbol(signature, signature->arguments[pass_index]);
        if (passed_dummy == NULL || passed_dummy->type != TYPE_DERIVED ||
            passed_dummy->rank != 0U || passed_dummy->derived_type == NULL ||
            !derived_extends(derived, passed_dummy->derived_type))
            f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                           "passed-object dummy for binding '%s' must be a scalar object of type "
                           "'%s' or an ancestor",
                           binding->name, derived->name);
    }
    procedure->type_bound_pass_index = pass_index;
    if (overridden != NULL && !overriding_signatures_compatible(overridden, binding))
        f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                       "binding '%s' has an incompatible overriding interface", binding->name);
}

static void resolve_derived_unit(Context *context, Unit *unit) {
    size_t type_index;
    for (type_index = 0U; type_index < unit->derived_type_count; ++type_index) {
        F2cDerivedType *derived = &unit->derived_types[type_index];
        size_t finalizer;
        size_t binding;
        free(derived->finalizer_procedures);
        free(derived->finalizer_ranks);
        derived->finalizer_procedures =
            derived->finalizer_count != 0U
                ? (Unit **)calloc(derived->finalizer_count, sizeof(*derived->finalizer_procedures))
                : NULL;
        derived->finalizer_ranks =
            derived->finalizer_count != 0U
                ? (size_t *)calloc(derived->finalizer_count, sizeof(*derived->finalizer_ranks))
                : NULL;
        for (finalizer = 0U; finalizer < derived->finalizer_count; ++finalizer) {
            Unit *procedure =
                find_procedure_definition(context, unit, derived->finalizers[finalizer]);
            Symbol *dummy = procedure != NULL && procedure->argument_count == 1U
                                ? f2c_find_symbol(procedure, procedure->arguments[0])
                                : NULL;
            if (procedure == NULL || procedure->kind != UNIT_SUBROUTINE ||
                procedure->argument_count != 1U || dummy == NULL || dummy->type != TYPE_DERIVED ||
                dummy->derived_type != derived) {
                f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                               "FINAL procedure '%s' must be a visible one-argument subroutine "
                               "whose dummy has type '%s'",
                               derived->finalizers[finalizer], derived->name);
                continue;
            }
            for (size_t previous = 0U; previous < finalizer; ++previous)
                if (derived->finalizer_procedures[previous] != NULL &&
                    derived->finalizer_ranks[previous] == dummy->rank)
                    f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                                   "FINAL procedures for type '%s' have duplicate rank %zu",
                                   derived->name, dummy->rank);
            derived->finalizer_procedures[finalizer] = procedure;
            derived->finalizer_ranks[finalizer] = dummy->rank;
        }
        for (binding = 0U; binding < derived->binding_count; ++binding)
            resolve_type_binding(context, unit, derived, &derived->bindings[binding]);
        for (binding = 0U; binding < F2C_DEFINED_IO_COUNT; ++binding) {
            const char *name = derived->defined_io_bindings[binding];
            F2cTypeBinding *io_binding = name != NULL ? find_type_binding(derived, name) : NULL;
            if (name != NULL && io_binding == NULL)
                f2c_diagnostic(context, context->lines.items[derived->begin].number, 1,
                               "defined I/O generic references unknown binding '%s'", name);
            else if (io_binding != NULL)
                validate_defined_io_binding(context, derived, io_binding,
                                            (F2cDefinedIoKind)binding);
        }
    }
}

void f2c_resolve_derived_semantics(Context *context) {
    size_t unit;
    for (unit = 0U; unit < context->modules.count; ++unit)
        resolve_derived_unit(context, &context->modules.items[unit]);
    for (unit = 0U; unit < context->units.count; ++unit)
        resolve_derived_unit(context, &context->units.items[unit]);
}

static void validate_procedure_actual(Context *context, Unit *caller, const Unit *definition,
                                      const Symbol *dummy, const F2cExpr *actual, size_t index,
                                      size_t line, const char *statement_text) {
    const size_t column = expression_start_column(statement_text, actual);
    const F2cExpr *value = actual_value(actual);
    if (dummy == NULL || value == NULL)
        return;
    if (value->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return;
    if (dummy->external) {
        const Symbol *procedure = value->kind == F2C_EXPR_NAME ? value->symbol : NULL;
        if (procedure == NULL || !procedure->external) {
            f2c_diagnostic_at(context, line, column, 1,
                              "argument %zu of procedure '%s' must be a procedure", index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name);
            return;
        }
        if (procedure->external_subroutine != dummy->external_subroutine) {
            f2c_diagnostic_at(context, line, column, 1,
                              "procedure actual argument %zu of '%s' has an incompatible "
                              "procedure kind",
                              index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name);
        } else if (!dummy->external_subroutine && dummy->type != TYPE_UNKNOWN &&
                   procedure->type != TYPE_UNKNOWN && dummy->type != procedure->type) {
            f2c_diagnostic_at(context, line, column, 1,
                              "procedure actual argument %zu of '%s' returns %s but the dummy "
                              "procedure returns %s",
                              index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name,
                              semantic_type_name(procedure->type), semantic_type_name(dummy->type));
        } else if (!procedure_signatures_compatible(dummy, procedure, 0U)) {
            f2c_diagnostic_at(context, line, column, 1,
                              "procedure actual argument %zu of '%s' has an incompatible "
                              "explicit interface",
                              index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name);
        }
        return;
    }
    if (dummy->type != TYPE_UNKNOWN && value->type != TYPE_UNKNOWN && dummy->type != value->type) {
        f2c_diagnostic_at(
            context, line, column, 1,
            "argument %zu of procedure '%s' has type %s but dummy '%s' has type %s", index + 1U,
            definition->fortran_name != NULL ? definition->fortran_name : definition->name,
            semantic_type_name(value->type), dummy->name, semantic_type_name(dummy->type));
    }
    if (dummy->type == TYPE_DERIVED && value->type == TYPE_DERIVED &&
        dummy->derived_type != value->derived_type &&
        (!dummy->polymorphic || !derived_extends(value->derived_type, dummy->derived_type))) {
        f2c_diagnostic_at(context, line, column, 1,
                          "argument %zu of procedure '%s' has incompatible dynamic derived type "
                          "for dummy '%s'",
                          index + 1U,
                          definition->fortran_name != NULL ? definition->fortran_name
                                                           : definition->name,
                          dummy->name);
    }
    if (dummy->kind != 0 && value->type_kind != 0 && dummy->kind != value->type_kind) {
        f2c_diagnostic_at(context, line, value->source_offset + 1U, 1,
                          "argument %zu of procedure '%s' has kind %d but dummy '%s' has kind %d",
                          index + 1U, definition->name, value->type_kind, dummy->name, dummy->kind);
    }
    if (dummy->allocatable &&
        (value->kind != F2C_EXPR_NAME || value->symbol == NULL || !value->symbol->allocatable)) {
        f2c_diagnostic_at(context, line, value->source_offset + 1U, 1,
                          "argument %zu of procedure '%s' must be an ALLOCATABLE whole object for "
                          "dummy '%s'",
                          index + 1U, definition->name, dummy->name);
    }
    if (dummy->pointer &&
        (value->kind != F2C_EXPR_NAME || value->symbol == NULL || !value->symbol->pointer)) {
        f2c_diagnostic_at(context, line, value->source_offset + 1U, 1,
                          "argument %zu of procedure '%s' must be a POINTER whole object for "
                          "dummy '%s'",
                          index + 1U, definition->name, dummy->name);
    }
    if (value->rank != dummy->rank &&
        !(dummy->rank != 0U && array_storage_sequence_actual(value))) {
        f2c_diagnostic_at(
            context, line, column, 1,
            "argument %zu of procedure '%s' has rank %zu but dummy '%s' has rank %zu", index + 1U,
            definition->fortran_name != NULL ? definition->fortran_name : definition->name,
            value->rank, dummy->name, dummy->rank);
    } else if (value->rank == dummy->rank && value->rank != 0U &&
               value->kind != F2C_EXPR_ARRAY_REFERENCE) {
        size_t dimension;
        if (symbol_expression_shapes_have_known_mismatch(dummy, value, &dimension)) {
            f2c_diagnostic_at(context, line, column, 1,
                              "argument %zu of procedure '%s' is nonconformable with dummy '%s' in "
                              "dimension %zu",
                              index + 1U,
                              definition->fortran_name != NULL ? definition->fortran_name
                                                               : definition->name,
                              dummy->name, dimension + 1U);
        }
    }
    if ((dummy->intent == F2C_INTENT_OUT || dummy->intent == F2C_INTENT_INOUT) &&
        !value->definable) {
        f2c_diagnostic_at(context, line, column, 1,
                          "argument %zu of procedure '%s' is not definable but dummy '%s' has "
                          "INTENT(%s)",
                          index + 1U,
                          definition->fortran_name != NULL ? definition->fortran_name
                                                           : definition->name,
                          dummy->name, dummy->intent == F2C_INTENT_OUT ? "OUT" : "INOUT");
    }
    (void)caller;
}

static Unit *validate_procedure_call(Context *context, Unit *caller, size_t line,
                                     const char *statement_text, const char *name,
                                     F2cExpr ***arguments, char ***items, size_t *argument_count,
                                     int subroutine_call) {
    Unit *definition = NULL;
    size_t matching_interfaces = 0U;
    const size_t interface_count =
        select_explicit_interface(context, caller, name, arguments != NULL ? *arguments : NULL,
                                  argument_count != NULL ? *argument_count : 0U, subroutine_call,
                                  &definition, &matching_interfaces);
    size_t i;
    if (interface_count > 1U && matching_interfaces != 1U) {
        f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                          matching_interfaces == 0U
                              ? "generic interface '%s' has no specific procedure matching this "
                                "actual argument list"
                              : "generic interface '%s' is ambiguous for this actual argument "
                                "list",
                          name);
        return NULL;
    }
    if (interface_count == 0U)
        definition = find_procedure_definition(context, caller, name);
    if (definition == NULL) {
        if (interface_count == 0U &&
            has_explicit_argument_association(arguments != NULL ? *arguments : NULL,
                                              argument_count != NULL ? *argument_count : 0U)) {
            f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                              "keyword arguments in call to procedure '%s' require a visible "
                              "explicit interface",
                              name);
        }
        return NULL;
    }
    if ((subroutine_call && definition->kind != UNIT_SUBROUTINE) ||
        (!subroutine_call && definition->kind != UNIT_FUNCTION)) {
        f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                          "procedure '%s' is called as a %s but is defined as a %s", name,
                          subroutine_call ? "SUBROUTINE" : "FUNCTION",
                          definition->kind == UNIT_SUBROUTINE ? "SUBROUTINE" : "FUNCTION");
        return NULL;
    }
    if (argument_count != NULL && *argument_count > definition->argument_count) {
        f2c_diagnostic_at(context, line, call_column(statement_text, name), 1,
                          "procedure '%s' is called with %zu arguments but is defined with %zu",
                          name, *argument_count, definition->argument_count);
        return NULL;
    }
    if (!bind_procedure_arguments(context, definition, line, statement_text, name, arguments, items,
                                  argument_count))
        return NULL;
    for (i = 0U; i < *argument_count; ++i) {
        Symbol *dummy = f2c_find_symbol(definition, definition->arguments[i]);
        validate_procedure_actual(context, caller, definition, dummy,
                                  arguments != NULL && *arguments != NULL ? (*arguments)[i] : NULL,
                                  i, line, statement_text);
    }
    return definition;
}

static void validate_present_intrinsic(Context *context, Unit *unit, size_t line,
                                       const char *statement_text, F2cExpr *expression) {
    F2cExpr *argument;
    Symbol *symbol;
    if (expression->child_count != 1U) {
        f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                          "PRESENT requires exactly one OPTIONAL dummy argument");
        return;
    }
    argument = expression->children[0];
    symbol = argument != NULL && argument->kind == F2C_EXPR_NAME ? argument->symbol : NULL;
    if (symbol == NULL || !symbol->argument || !symbol->optional) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, argument), 1,
                          "PRESENT argument must be an OPTIONAL dummy argument");
    }
    (void)unit;
}

static void validate_allocated_intrinsic(Context *context, size_t line, const char *statement_text,
                                         F2cExpr *expression) {
    F2cExpr *argument = expression->child_count == 1U ? expression->children[0] : NULL;
    Symbol *symbol = argument != NULL && (argument->kind == F2C_EXPR_NAME ||
                                          argument->kind == F2C_EXPR_COMPONENT)
                         ? argument->symbol
                         : NULL;
    if (symbol == NULL || !symbol->allocatable) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, argument), 1,
                          "ALLOCATED requires a whole ALLOCATABLE object");
    }
}

static void validate_associated_intrinsic(Context *context, size_t line, const char *statement_text,
                                          F2cExpr *expression) {
    F2cExpr *pointer = expression->child_count >= 1U ? expression->children[0] : NULL;
    F2cExpr *target = expression->child_count >= 2U ? expression->children[1] : NULL;
    Symbol *pointer_symbol =
        pointer != NULL && (pointer->kind == F2C_EXPR_NAME || pointer->kind == F2C_EXPR_COMPONENT)
            ? pointer->symbol
            : NULL;
    Symbol *target_symbol = target != NULL && target->kind == F2C_EXPR_NAME ? target->symbol : NULL;
    if (pointer_symbol == NULL ||
        (!pointer_symbol->pointer && !pointer_symbol->procedure_pointer)) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, pointer), 1,
                          "ASSOCIATED first argument must be a whole POINTER object");
        return;
    }
    if (pointer_symbol->procedure_pointer) {
        if (pointer == NULL ||
            (pointer->kind != F2C_EXPR_NAME && pointer->kind != F2C_EXPR_COMPONENT)) {
            f2c_diagnostic_at(context, line, expression_start_column(statement_text, pointer), 1,
                              "ASSOCIATED procedure pointer must be a whole object");
        } else if (target != NULL &&
                   (target_symbol == NULL || !target_symbol->external ||
                    !procedure_signatures_compatible(pointer_symbol, target_symbol, 0U))) {
            f2c_diagnostic_at(context, line, expression_start_column(statement_text, target), 1,
                              "ASSOCIATED target must be a procedure with a compatible explicit "
                              "interface");
        }
        return;
    }
    if (target != NULL &&
        (target_symbol == NULL || (!target_symbol->target && !target_symbol->pointer) ||
         target_symbol->type != pointer_symbol->type ||
         target_symbol->kind != pointer_symbol->kind ||
         target_symbol->rank != pointer_symbol->rank)) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, target), 1,
                          "ASSOCIATED target must be a compatible TARGET or POINTER object");
    }
}

static void validate_intrinsic_arity(Context *context, size_t line, const char *statement_text,
                                     const F2cExpr *expression) {
    const F2cIntrinsicSignature *signature =
        expression != NULL ? f2c_find_intrinsic(expression->text) : NULL;
    if (signature == NULL || (expression->child_count >= signature->minimum_arguments &&
                              expression->child_count <= signature->maximum_arguments))
        return;
    if (signature->minimum_arguments == signature->maximum_arguments) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, expression), 1,
                          "%s requires exactly %zu argument%s", signature->name,
                          signature->minimum_arguments,
                          signature->minimum_arguments == 1U ? "" : "s");
    } else {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, expression), 1,
                          "%s requires between %zu and %zu arguments", signature->name,
                          signature->minimum_arguments, signature->maximum_arguments);
    }
}

static void validate_substring_semantics(Context *context, Unit *unit, size_t line,
                                         const char *statement_text, const F2cExpr *expression) {
    const F2cExpr *selector;
    const F2cExpr *lower = NULL;
    const F2cExpr *upper = NULL;
    const F2cExpr *stride = NULL;
    int64_t lower_value = 1;
    int64_t upper_value = 0;
    int64_t length_value = 0;
    int lower_known = 1;
    int upper_known = 0;
    int length_known = 0;
    if (expression == NULL || expression->kind != F2C_EXPR_SUBSTRING)
        return;
    if (expression->symbol == NULL || expression->child_count != 1U) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, expression), 1,
                          "malformed CHARACTER substring designator");
        return;
    }
    selector = expression->children[0];
    if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
        if (selector->child_count != 3U) {
            f2c_diagnostic_at(context, line, expression_start_column(statement_text, selector), 1,
                              "malformed CHARACTER substring range");
            return;
        }
        if (selector->children[0]->kind != F2C_EXPR_INVALID)
            lower = selector->children[0];
        if (selector->children[1]->kind != F2C_EXPR_INVALID)
            upper = selector->children[1];
        if (selector->children[2]->kind != F2C_EXPR_INVALID)
            stride = selector->children[2];
    } else {
        lower = selector;
        upper = selector;
    }
    if (stride != NULL) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, stride), 1,
                          "CHARACTER substring range cannot have a stride");
    }
    if (lower != NULL && (lower->type != TYPE_INTEGER || lower->rank != 0U)) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, lower), 1,
                          "CHARACTER substring lower bound must be a scalar INTEGER");
    }
    if (upper != NULL && (upper->type != TYPE_INTEGER || upper->rank != 0U)) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, upper), 1,
                          "CHARACTER substring upper bound must be a scalar INTEGER");
    }
    length_known =
        expression->symbol->character_length_expression != NULL
            ? f2c_evaluate_integer_constant(unit, expression->symbol->character_length_expression,
                                            &length_value)
            : f2c_evaluate_integer_text(unit, expression->symbol->character_length, &length_value);
    if (lower != NULL)
        lower_known = f2c_evaluate_integer_constant(unit, lower, &lower_value);
    if (upper != NULL) {
        upper_known = f2c_evaluate_integer_constant(unit, upper, &upper_value);
    } else if (length_known) {
        upper_value = length_value;
        upper_known = 1;
    }
    if (lower_known && lower_value < 1) {
        f2c_diagnostic_at(context, line,
                          expression_start_column(statement_text, lower != NULL ? lower : selector),
                          1, "CHARACTER substring lower bound must be at least one");
    }
    if (upper_known && length_known && upper_value > length_value) {
        f2c_diagnostic_at(context, line,
                          expression_start_column(statement_text, upper != NULL ? upper : selector),
                          1, "CHARACTER substring upper bound exceeds declared length %lld",
                          (long long)length_value);
    }
    if (lower_known && upper_known &&
        (upper_value == INT64_MAX ? lower_value > upper_value : lower_value > upper_value + 1)) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, selector), 1,
                          "CHARACTER substring lower bound may exceed the upper bound by at most "
                          "one");
    }
}

static void validate_structure_constructor(Context *context, size_t line,
                                           const char *statement_text, F2cExpr *expression) {
    unsigned char *assigned;
    size_t next_positional = 0U;
    size_t argument;
    int saw_keyword = 0;
    if (expression == NULL || expression->derived_type == NULL)
        return;
    assigned = expression->derived_type->component_count != 0U
                   ? (unsigned char *)calloc(expression->derived_type->component_count, 1U)
                   : NULL;
    if (expression->derived_type->component_count != 0U && assigned == NULL) {
        f2c_diagnostic_at(context, line, expression_start_column(statement_text, expression), 1,
                          "out of memory validating structure constructor");
        return;
    }
    for (argument = 0U; argument < expression->child_count; ++argument) {
        F2cExpr *actual = expression->children[argument];
        F2cExpr *value = actual;
        size_t component = SIZE_MAX;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            actual->child_count == 1U) {
            size_t index;
            saw_keyword = 1;
            value = actual->children[0];
            for (index = 0U; index < expression->derived_type->component_count; ++index) {
                if (actual->text != NULL &&
                    strcmp(actual->text, expression->derived_type->components[index].name) == 0) {
                    component = index;
                    break;
                }
            }
            if (component == SIZE_MAX) {
                f2c_diagnostic_at(context, line, expression_start_column(statement_text, actual), 1,
                                  "unknown component '%s' in constructor for type '%s'",
                                  actual->text != NULL ? actual->text : "<unknown>",
                                  expression->derived_type->name);
                continue;
            }
        } else {
            if (saw_keyword) {
                f2c_diagnostic_at(context, line, expression_start_column(statement_text, actual), 1,
                                  "positional component follows a keyword component in "
                                  "constructor for type '%s'",
                                  expression->derived_type->name);
            }
            while (next_positional < expression->derived_type->component_count &&
                   assigned[next_positional])
                ++next_positional;
            component = next_positional++;
            if (component >= expression->derived_type->component_count) {
                f2c_diagnostic_at(context, line, expression_start_column(statement_text, actual), 1,
                                  "too many components in constructor for type '%s'",
                                  expression->derived_type->name);
                continue;
            }
        }
        if (assigned[component]) {
            f2c_diagnostic_at(context, line, expression_start_column(statement_text, actual), 1,
                              "component '%s' is specified more than once in constructor for "
                              "type '%s'",
                              expression->derived_type->components[component].name,
                              expression->derived_type->name);
            continue;
        }
        assigned[component] = 1U;
        {
            const Symbol *declared = &expression->derived_type->components[component];
            if (declared->pointer || declared->allocatable || declared->rank != 0U ||
                declared->type == TYPE_CHARACTER) {
                f2c_diagnostic_at(
                    context, line, expression_start_column(statement_text, actual), 1,
                    "constructor component '%s' uses pointer, allocatable, array, or CHARACTER "
                    "semantics that are not yet supported",
                    declared->name);
            } else if (value != NULL && !constructor_type_compatible(declared->type, value->type)) {
                f2c_diagnostic_at(context, line, expression_start_column(statement_text, value), 1,
                                  "constructor value for component '%s' has an incompatible type",
                                  declared->name);
            } else if (declared->type == TYPE_DERIVED && value != NULL &&
                       declared->derived_type != value->derived_type) {
                f2c_diagnostic_at(context, line, expression_start_column(statement_text, value), 1,
                                  "constructor value for component '%s' has an incompatible "
                                  "derived type",
                                  declared->name);
            }
        }
    }
    for (argument = 0U; argument < expression->derived_type->component_count; ++argument) {
        if (!assigned[argument] &&
            expression->derived_type->components[argument].initializer == NULL) {
            f2c_diagnostic_at(context, line, expression_start_column(statement_text, expression), 1,
                              "constructor for type '%s' does not initialize component '%s'",
                              expression->derived_type->name,
                              expression->derived_type->components[argument].name);
        }
    }
    free(assigned);
}

static void validate_expression_calls(Context *context, Unit *unit, size_t line,
                                      const char *statement_text, F2cExpr *expression) {
    size_t i;
    if (expression == NULL)
        return;
    validate_substring_semantics(context, unit, line, statement_text, expression);
    if (expression->kind == F2C_EXPR_ARRAY_REFERENCE && expression->symbol != NULL) {
        for (i = 0U; i < expression->child_count; ++i) {
            F2cExpr *selector = expression->children[i];
            size_t part;
            if (selector == NULL)
                continue;
            if (selector->kind != F2C_EXPR_ARRAY_SECTION) {
                if (selector->type != TYPE_INTEGER || selector->rank > 1U) {
                    f2c_diagnostic_at(context, line,
                                      expression_start_column(statement_text, selector), 1,
                                      "array subscript must be a scalar INTEGER or rank-one "
                                      "INTEGER vector");
                }
                continue;
            }
            for (part = 0U; part < selector->child_count; ++part) {
                F2cExpr *bound = selector->children[part];
                if (bound != NULL && bound->kind != F2C_EXPR_INVALID &&
                    (bound->type != TYPE_INTEGER || bound->rank != 0U)) {
                    f2c_diagnostic_at(context, line, expression_start_column(statement_text, bound),
                                      1,
                                      "array section bound and stride must be scalar INTEGER "
                                      "expressions");
                }
            }
        }
    }
    if (expression->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR)
        validate_structure_constructor(context, line, statement_text, expression);
    if (expression->kind == F2C_EXPR_BINARY && expression->child_count == 2U) {
        size_t dimension;
        if (shapes_have_known_mismatch(expression->children[0], expression->children[1],
                                       &dimension)) {
            f2c_diagnostic_at(
                context, line, expression_start_column(statement_text, expression), 1,
                "nonconformable array operands in dimension %zu: extent %llu and %llu",
                dimension + 1U,
                (unsigned long long)expression->children[0]->shape.dimensions[dimension].extent,
                (unsigned long long)expression->children[1]->shape.dimensions[dimension].extent);
        }
    }
    if (expression->kind == F2C_EXPR_CALL && expression->text != NULL &&
        f2c_is_intrinsic_name(expression->text)) {
        const F2cIntrinsicSignature *signature = f2c_find_intrinsic(expression->text);
        validate_intrinsic_arity(context, line, statement_text, expression);
        if (signature != NULL && signature->rank_rule == F2C_INTRINSIC_RANK_ELEMENTAL) {
            const F2cExpr *array_argument = NULL;
            for (i = 0U; i < expression->child_count; ++i) {
                F2cExpr *argument = expression->children[i];
                size_t dimension;
                if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                    argument->child_count == 1U)
                    argument = argument->children[0];
                if (argument == NULL || argument->rank == 0U)
                    continue;
                if (array_argument == NULL) {
                    array_argument = argument;
                } else if (array_argument->rank != argument->rank) {
                    f2c_diagnostic_at(context, line,
                                      expression_start_column(statement_text, argument), 1,
                                      "elemental intrinsic '%s' has nonconformable argument ranks "
                                      "%zu and %zu",
                                      expression->text, array_argument->rank, argument->rank);
                } else if (shapes_have_known_mismatch(array_argument, argument, &dimension)) {
                    f2c_diagnostic_at(
                        context, line, expression_start_column(statement_text, argument), 1,
                        "elemental intrinsic '%s' has nonconformable extent in dimension %zu",
                        expression->text, dimension + 1U);
                }
            }
        }
        if (strcmp(expression->text, "present") == 0)
            validate_present_intrinsic(context, unit, line, statement_text, expression);
        else if (strcmp(expression->text, "allocated") == 0)
            validate_allocated_intrinsic(context, line, statement_text, expression);
        else if (strcmp(expression->text, "associated") == 0)
            validate_associated_intrinsic(context, line, statement_text, expression);
    } else if (expression->kind == F2C_EXPR_CALL && expression->symbol != NULL &&
               expression->symbol->type_bound) {
        const Symbol *binding = expression->symbol;
        const size_t explicit_count =
            expression->child_count != 0U ? expression->child_count - 1U : 0U;
        const size_t expected_count =
            binding->external_parameter_count -
            ((!binding->type_bound_nopass && binding->external_parameter_count != 0U) ? 1U : 0U);
        if (explicit_count != expected_count)
            f2c_diagnostic_at(context, line, expression_column(statement_text, expression), 1,
                              "type-bound function '%s' expects %zu explicit arguments but has "
                              "%zu",
                              expression->text, expected_count, explicit_count);
    } else if (expression->kind == F2C_EXPR_CALL && !f2c_is_intrinsic_name(expression->text)) {
        Unit *definition =
            validate_procedure_call(context, unit, line, statement_text, expression->text,
                                    &expression->children, NULL, &expression->child_count, 0);
        expression->child_capacity = expression->child_count;
        if (definition != NULL && definition->kind == UNIT_FUNCTION) {
            Symbol *result = definition->result_name != NULL
                                 ? f2c_find_symbol(definition, definition->result_name)
                                 : NULL;
            expression->type = definition->return_type;
            expression->type_kind = definition->return_kind;
            expression->derived_type = result != NULL ? result->derived_type : NULL;
            expression->rank = result != NULL ? result->rank : 0U;
            if (result != NULL)
                expression->shape = result->shape;
        }
        if (definition != NULL && definition->name != NULL && !definition->interface_abstract &&
            strcmp(expression->text, definition->name) != 0) {
            char *resolved = f2c_strdup(definition->name);
            if (resolved != NULL) {
                Symbol *resolved_symbol = f2c_find_symbol(unit, definition->name);
                free(expression->text);
                expression->text = resolved;
                if (resolved_symbol != NULL) {
                    expression->symbol = resolved_symbol;
                } else if (expression->symbol != NULL &&
                           strcmp(f2c_symbol_c_name(unit, expression->symbol), definition->name) !=
                               0) {
                    expression->symbol = NULL;
                }
            }
        }
    }
    for (i = 0U; i < expression->child_count; ++i)
        validate_expression_calls(context, unit, line, statement_text, expression->children[i]);
}

static void validate_io_item_calls(Context *context, Unit *unit, size_t line,
                                   const char *statement_text, F2cIoItem *item) {
    size_t i;
    validate_expression_calls(context, unit, line, statement_text, item->expression);
    validate_expression_calls(context, unit, line, statement_text, item->iterator);
    validate_expression_calls(context, unit, line, statement_text, item->initial);
    validate_expression_calls(context, unit, line, statement_text, item->limit);
    validate_expression_calls(context, unit, line, statement_text, item->step);
    for (i = 0U; i < item->child_count; ++i)
        validate_io_item_calls(context, unit, line, statement_text, &item->children[i]);
}

static void validate_allocation_bound(Context *context, const F2cStatement *statement,
                                      const Symbol *symbol, const F2cExpr *bound,
                                      size_t dimension) {
    const F2cExpr *lower = NULL;
    const F2cExpr *upper = bound;
    if (bound != NULL && bound->kind == F2C_EXPR_ARRAY_SECTION && bound->child_count == 3U) {
        lower = bound->children[0];
        upper = bound->children[1];
        if (bound->children[2]->kind != F2C_EXPR_INVALID) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, bound->children[2]), 1,
                              "ALLOCATE bound %zu for '%s' cannot have a stride", dimension + 1U,
                              symbol->name);
        }
    }
    if (upper == NULL || upper->kind == F2C_EXPR_INVALID) {
        f2c_diagnostic_at(context, statement->line, expression_start_column(statement->text, bound),
                          1, "ALLOCATE bound %zu for '%s' requires an upper bound", dimension + 1U,
                          symbol->name);
        return;
    }
    if (lower != NULL && lower->kind != F2C_EXPR_INVALID &&
        (lower->type != TYPE_INTEGER || lower->rank != 0U)) {
        f2c_diagnostic_at(context, statement->line, expression_start_column(statement->text, lower),
                          1, "ALLOCATE lower bound %zu for '%s' must be a scalar INTEGER",
                          dimension + 1U, symbol->name);
    }
    if (upper->type != TYPE_INTEGER || upper->rank != 0U) {
        f2c_diagnostic_at(context, statement->line, expression_start_column(statement->text, upper),
                          1, "ALLOCATE upper bound %zu for '%s' must be a scalar INTEGER",
                          dimension + 1U, symbol->name);
    }
}

static F2cExpr *allocation_keyword_value(F2cStatement *statement, const char *name) {
    size_t i;
    for (i = 0U; i < statement->item_count; ++i) {
        F2cExpr *argument = statement->arguments != NULL ? statement->arguments[i] : NULL;
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            argument->text != NULL && strcmp(argument->text, name) == 0 &&
            argument->child_count == 1U)
            return argument->children[0];
    }
    return NULL;
}

static int allocation_type_compatible(const Symbol *target, const F2cExpr *value) {
    if (target == NULL || value == NULL)
        return 0;
    if (target->type == TYPE_CHARACTER || value->type == TYPE_CHARACTER)
        return target->type == TYPE_CHARACTER && value->type == TYPE_CHARACTER;
    return target->type == value->type;
}

static void validate_allocation_statement(Context *context, Unit *unit, F2cStatement *statement) {
    const int allocating = statement->kind == F2C_STMT_ALLOCATE;
    F2cExpr *source = allocating ? allocation_keyword_value(statement, "source") : NULL;
    F2cExpr *mold = allocating ? allocation_keyword_value(statement, "mold") : NULL;
    F2cExpr *model = source != NULL ? source : mold;
    size_t target_count = 0U;
    size_t stat_count = 0U;
    size_t source_count = 0U;
    size_t mold_count = 0U;
    size_t i;
    if (source != NULL && mold != NULL) {
        f2c_diagnostic_at(context, statement->line, 1U, 1,
                          "ALLOCATE cannot specify both SOURCE= and MOLD=");
    }
    if (model != NULL && statement->allocation_character_length != NULL) {
        f2c_diagnostic_at(context, statement->line, 1U, 1,
                          "ALLOCATE type specification cannot be combined with SOURCE=/MOLD=");
    }
    if (allocating && statement->tail != NULL) {
        if (!f2c_starts_word(statement->tail, "character") ||
            statement->allocation_character_length == NULL) {
            f2c_diagnostic_at(context, statement->line, 1U, 1,
                              "unsupported or malformed ALLOCATE type specification '%s'",
                              statement->tail);
        } else {
            report_parse_error(context, statement->line, statement->text,
                               statement->allocation_character_length, "ALLOCATE CHARACTER length");
            if (statement->allocation_character_length->type != TYPE_INTEGER ||
                statement->allocation_character_length->rank != 0U) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text,
                                                          statement->allocation_character_length),
                                  1, "ALLOCATE CHARACTER length must be a scalar INTEGER");
            }
            validate_constructor_semantics(context, unit, statement->line, statement->text,
                                           statement->allocation_character_length);
            validate_expression_calls(context, unit, statement->line, statement->text,
                                      statement->allocation_character_length);
        }
    }
    for (i = 0U; i < statement->item_count; ++i) {
        F2cExpr *argument = statement->arguments != NULL ? statement->arguments[i] : NULL;
        F2cExpr *target = argument;
        Symbol *symbol;
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            const char *keyword = argument->text != NULL ? argument->text : "";
            F2cExpr *value = argument->child_count == 1U ? argument->children[0] : NULL;
            if (strcmp(keyword, "stat") == 0) {
                ++stat_count;
                if (stat_count > 1U) {
                    f2c_diagnostic_at(context, statement->line,
                                      expression_start_column(statement->text, argument), 1,
                                      "duplicate STAT= in %s",
                                      allocating ? "ALLOCATE" : "DEALLOCATE");
                }
                if (value == NULL || value->type != TYPE_INTEGER || value->rank != 0U ||
                    !value->definable) {
                    f2c_diagnostic_at(context, statement->line,
                                      expression_start_column(statement->text, argument), 1,
                                      "STAT= in %s must be a definable scalar INTEGER",
                                      allocating ? "ALLOCATE" : "DEALLOCATE");
                }
            } else if (allocating &&
                       (strcmp(keyword, "source") == 0 || strcmp(keyword, "mold") == 0)) {
                size_t *keyword_count =
                    strcmp(keyword, "source") == 0 ? &source_count : &mold_count;
                ++*keyword_count;
                if (*keyword_count > 1U) {
                    f2c_diagnostic_at(context, statement->line,
                                      expression_start_column(statement->text, argument), 1,
                                      "duplicate %s= in ALLOCATE", keyword);
                }
                if (value == NULL) {
                    f2c_diagnostic_at(context, statement->line,
                                      expression_start_column(statement->text, argument), 1,
                                      "%s= in ALLOCATE requires an expression", keyword);
                } else if (value->rank != 0U && value->kind != F2C_EXPR_NAME) {
                    f2c_diagnostic_at(context, statement->line,
                                      expression_start_column(statement->text, value), 1,
                                      "array %s= currently requires a whole named array", keyword);
                }
            } else {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, argument), 1,
                                  "%s= is not yet supported in %s", keyword,
                                  allocating ? "ALLOCATE" : "DEALLOCATE");
            }
            continue;
        }
        ++target_count;
        symbol = target != NULL ? target->symbol : NULL;
        if (target == NULL || symbol == NULL ||
            (target->kind != F2C_EXPR_NAME && target->kind != F2C_EXPR_ARRAY_REFERENCE &&
             target->kind != F2C_EXPR_COMPONENT)) {
            f2c_diagnostic_at(
                context, statement->line, expression_start_column(statement->text, target), 1,
                "%s target must be an allocatable object", allocating ? "ALLOCATE" : "DEALLOCATE");
            continue;
        }
        if (!allocating && target->kind != F2C_EXPR_NAME && target->kind != F2C_EXPR_COMPONENT) {
            f2c_diagnostic_at(
                context, statement->line, expression_start_column(statement->text, target), 1,
                "DEALLOCATE target '%s' must be a whole allocatable object", symbol->name);
            continue;
        }
        if (!symbol->allocatable) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, target), 1,
                              "%s target '%s' is not ALLOCATABLE",
                              allocating ? "ALLOCATE" : "DEALLOCATE", symbol->name);
            continue;
        }
        if (allocating && symbol->rank != 0U && target->kind != F2C_EXPR_ARRAY_REFERENCE &&
            !(target->kind == F2C_EXPR_COMPONENT && target->child_count > 1U) && model == NULL) {
            f2c_diagnostic_at(
                context, statement->line, expression_start_column(statement->text, target), 1,
                "ALLOCATE target '%s' requires %zu explicit bounds", symbol->name, symbol->rank);
        }
        if (allocating && target->kind == F2C_EXPR_ARRAY_REFERENCE &&
            target->child_count != symbol->rank) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, target), 1,
                              "ALLOCATE target '%s' has %zu bounds but rank %zu", symbol->name,
                              target->child_count, symbol->rank);
        }
        if (allocating && target->kind == F2C_EXPR_COMPONENT && symbol->rank != 0U &&
            target->child_count != symbol->rank + 1U) {
            f2c_diagnostic_at(
                context, statement->line, expression_start_column(statement->text, target), 1,
                "ALLOCATE component '%s' has %zu bounds but rank %zu", symbol->name,
                target->child_count > 0U ? target->child_count - 1U : 0U, symbol->rank);
        }
        if (allocating && target->kind == F2C_EXPR_COMPONENT &&
            target->child_count == symbol->rank + 1U) {
            size_t dimension;
            for (dimension = 0U; dimension < symbol->rank; ++dimension)
                validate_allocation_bound(context, statement, symbol,
                                          target->children[dimension + 1U], dimension);
        }
        if (allocating && target->kind == F2C_EXPR_ARRAY_REFERENCE &&
            target->child_count == symbol->rank) {
            size_t dimension;
            for (dimension = 0U; dimension < target->child_count; ++dimension)
                validate_allocation_bound(context, statement, symbol, target->children[dimension],
                                          dimension);
        }
        if (allocating && model != NULL) {
            if (!allocation_type_compatible(symbol, model)) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, model), 1,
                                  "%s= type is incompatible with ALLOCATE target '%s'",
                                  source != NULL ? "SOURCE" : "MOLD", symbol->name);
            }
            if (symbol->rank == 0U && model->rank != 0U) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, model), 1,
                                  "%s= rank %zu is incompatible with scalar target '%s'",
                                  source != NULL ? "SOURCE" : "MOLD", model->rank, symbol->name);
            } else if (symbol->rank != 0U && target->kind == F2C_EXPR_NAME &&
                       model->rank != symbol->rank) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, model), 1,
                                  "%s= must provide rank-%zu shape for target '%s' without "
                                  "explicit bounds",
                                  source != NULL ? "SOURCE" : "MOLD", symbol->rank, symbol->name);
            } else if (symbol->rank != 0U && target->kind == F2C_EXPR_ARRAY_REFERENCE &&
                       model->rank != 0U && model->rank != symbol->rank) {
                f2c_diagnostic_at(
                    context, statement->line, expression_start_column(statement->text, model), 1,
                    "%s= rank %zu does not conform to rank-%zu target '%s'",
                    source != NULL ? "SOURCE" : "MOLD", model->rank, symbol->rank, symbol->name);
            }
        }
        if (allocating && symbol->deferred_character &&
            statement->allocation_character_length == NULL && model == NULL) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, target), 1,
                              "deferred-length CHARACTER target '%s' requires an explicit "
                              "CHARACTER length or SOURCE=/MOLD=",
                              symbol->name);
        }
        if (allocating && statement->allocation_character_length != NULL &&
            !symbol->deferred_character) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, target), 1,
                              "ALLOCATE CHARACTER type specification requires a deferred-length "
                              "CHARACTER target");
        }
    }
    if (target_count == 0U) {
        f2c_diagnostic_at(context, statement->line, 1U, 1, "%s requires at least one target",
                          allocating ? "ALLOCATE" : "DEALLOCATE");
    }
    if (model != NULL && target_count != 1U) {
        f2c_diagnostic_at(context, statement->line, 1U, 1,
                          "ALLOCATE with SOURCE=/MOLD= currently requires exactly one target");
    }
}

static size_t move_alloc_argument_slot(const char *keyword) {
    static const char *const names[] = {"from", "to", "stat", "errmsg"};
    size_t i;
    if (keyword == NULL)
        return SIZE_MAX;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(keyword, names[i]) == 0)
            return i;
    }
    return SIZE_MAX;
}

static int bind_move_alloc_arguments(Context *context, F2cStatement *statement) {
    static const char *const names[] = {"from", "to", "stat", "errmsg"};
    F2cExpr *ordered_arguments[4] = {0};
    char *ordered_items[4] = {0};
    unsigned char assigned[4] = {0};
    size_t next_positional = 0U;
    size_t i;
    int saw_keyword = 0;
    int valid = 1;
    for (i = 0U; i < statement->item_count; ++i) {
        F2cExpr *actual = statement->arguments != NULL ? statement->arguments[i] : NULL;
        size_t slot;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            saw_keyword = 1;
            slot = move_alloc_argument_slot(actual->text);
            if (slot == SIZE_MAX) {
                f2c_diagnostic_at(context, statement->line, keyword_column(statement->text, actual),
                                  1, "MOVE_ALLOC has no argument named '%s'",
                                  actual->text != NULL ? actual->text : "<unknown>");
                valid = 0;
                continue;
            }
        } else {
            if (saw_keyword) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, actual), 1,
                                  "positional argument follows a keyword argument in "
                                  "MOVE_ALLOC");
                valid = 0;
                continue;
            }
            while (next_positional < 4U && assigned[next_positional])
                ++next_positional;
            slot = next_positional++;
            if (slot >= 4U) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, actual), 1,
                                  "MOVE_ALLOC accepts at most four arguments");
                valid = 0;
                continue;
            }
        }
        if (assigned[slot]) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, actual), 1,
                              "MOVE_ALLOC argument '%s' is associated more than once", names[slot]);
            valid = 0;
            continue;
        }
        assigned[slot] = 1U;
        ordered_arguments[slot] = actual;
        ordered_items[slot] = statement->items != NULL ? statement->items[i] : NULL;
        if (actual_value(actual) == NULL ||
            actual_value(actual)->kind == F2C_EXPR_ABSENT_ARGUMENT) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, actual), 1,
                              "MOVE_ALLOC argument '%s' cannot be omitted", names[slot]);
            valid = 0;
        }
    }
    for (i = 0U; i < 2U; ++i) {
        if (!assigned[i]) {
            f2c_diagnostic_at(context, statement->line, call_column(statement->text, "move_alloc"),
                              1, "required MOVE_ALLOC argument '%s' has no actual argument",
                              names[i]);
            valid = 0;
        }
    }
    if (valid) {
        F2cExpr **arguments = (F2cExpr **)calloc(4U, sizeof(*arguments));
        char **items = (char **)calloc(4U, sizeof(*items));
        if (arguments == NULL || items == NULL) {
            free(arguments);
            free(items);
            f2c_diagnostic_at(context, statement->line, call_column(statement->text, "move_alloc"),
                              1, "out of memory while binding MOVE_ALLOC arguments");
            return 0;
        }
        for (i = 0U; i < 4U; ++i) {
            arguments[i] = ordered_arguments[i];
            items[i] = ordered_items[i];
        }
        free(statement->arguments);
        free(statement->items);
        statement->arguments = arguments;
        statement->items = items;
        statement->item_count = 4U;
    }
    return valid;
}

static Symbol *validate_move_alloc_object(Context *context, const F2cStatement *statement,
                                          const F2cExpr *expression, const char *role) {
    Symbol *symbol = expression != NULL ? expression->symbol : NULL;
    if (expression == NULL || expression->kind != F2C_EXPR_NAME || symbol == NULL) {
        f2c_diagnostic_at(context, statement->line,
                          expression_start_column(statement->text, expression), 1,
                          "MOVE_ALLOC %s= must be a whole named allocatable object", role);
        return NULL;
    }
    if (!symbol->allocatable) {
        f2c_diagnostic_at(context, statement->line,
                          expression_start_column(statement->text, expression), 1,
                          "MOVE_ALLOC %s= object '%s' is not ALLOCATABLE", role, symbol->name);
        return NULL;
    }
    return symbol;
}

static int same_move_alloc_character_length(Unit *unit, const Symbol *from, const Symbol *to) {
    int64_t from_length;
    int64_t to_length;
    if (from->deferred_character || to->deferred_character)
        return from->deferred_character == to->deferred_character;
    if (from->character_length_expression != NULL && to->character_length_expression != NULL &&
        f2c_evaluate_integer_constant(unit, from->character_length_expression, &from_length) &&
        f2c_evaluate_integer_constant(unit, to->character_length_expression, &to_length))
        return from_length == to_length;
    if (from->character_length == NULL || to->character_length == NULL)
        return from->character_length == to->character_length;
    return strcmp(from->character_length, to->character_length) == 0;
}

static void validate_move_alloc_statement(Context *context, Unit *unit, F2cStatement *statement) {
    const F2cExpr *from_expression;
    const F2cExpr *to_expression;
    const F2cExpr *status_expression;
    const F2cExpr *message_expression;
    Symbol *from;
    Symbol *to;
    if (!bind_move_alloc_arguments(context, statement))
        return;
    from_expression = actual_value(statement->arguments[0]);
    to_expression = actual_value(statement->arguments[1]);
    status_expression = actual_value(statement->arguments[2]);
    message_expression = actual_value(statement->arguments[3]);
    from = validate_move_alloc_object(context, statement, from_expression, "FROM");
    to = validate_move_alloc_object(context, statement, to_expression, "TO");
    if (from != NULL && to != NULL) {
        if (from == to) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, to_expression), 1,
                              "MOVE_ALLOC FROM= and TO= must designate distinct objects");
        }
        if (from->type != to->type ||
            (from->kind_type != TYPE_UNKNOWN && to->kind_type != TYPE_UNKNOWN &&
             from->kind_type != to->kind_type)) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, to_expression), 1,
                              "MOVE_ALLOC FROM= and TO= must have the same declared type and "
                              "kind");
        }
        if (from->rank != to->rank) {
            f2c_diagnostic_at(
                context, statement->line, expression_start_column(statement->text, to_expression),
                1, "MOVE_ALLOC FROM= rank %zu does not match TO= rank %zu", from->rank, to->rank);
        }
        if (from->type == TYPE_CHARACTER && to->type == TYPE_CHARACTER &&
            !same_move_alloc_character_length(unit, from, to)) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, to_expression), 1,
                              "MOVE_ALLOC CHARACTER objects must have compatible length type "
                              "parameters");
        }
    }
    if (status_expression != NULL &&
        (status_expression->type != TYPE_INTEGER || status_expression->rank != 0U ||
         !status_expression->definable)) {
        f2c_diagnostic_at(context, statement->line,
                          expression_start_column(statement->text, status_expression), 1,
                          "MOVE_ALLOC STAT= must be a definable scalar INTEGER");
    }
    if (message_expression != NULL &&
        (message_expression->type != TYPE_CHARACTER || message_expression->rank != 0U ||
         !message_expression->definable)) {
        f2c_diagnostic_at(context, statement->line,
                          expression_start_column(statement->text, message_expression), 1,
                          "MOVE_ALLOC ERRMSG= must be a definable scalar CHARACTER");
    }
}

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
                              expression_start_column(statement->text, expression), 1,
                              "%s condition must be a scalar LOGICAL expression",
                              statement->kind == F2C_STMT_DO_WHILE ? "DO WHILE" : "IF");
        }
    } else if (statement->kind == F2C_STMT_ARITHMETIC_IF) {
        if (expression != NULL && !ordered_numeric_scalar(expression)) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, expression), 1,
                              "arithmetic IF selector must be a scalar INTEGER or REAL "
                              "expression");
        }
    } else if (statement->kind == F2C_STMT_SELECT_CASE) {
        if (expression != NULL && (expression->type != TYPE_INTEGER || expression->rank != 0U)) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, expression), 1,
                              "SELECT CASE currently requires a scalar INTEGER selector");
        }
    } else if (statement->kind == F2C_STMT_CASE && expression != NULL) {
        if (expression->kind == F2C_EXPR_ARRAY_SECTION) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, expression), 1,
                              "CASE value ranges are not yet supported");
        } else if (expression->type != TYPE_INTEGER || expression->rank != 0U) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, expression), 1,
                              "CASE value must be a scalar INTEGER constant");
        }
    } else if (statement->kind == F2C_STMT_DO) {
        int64_t step;
        if (statement->left != NULL &&
            (!ordered_numeric_scalar(statement->left) || !statement->left->definable)) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, statement->left), 1,
                              "counted DO variable must be a definable scalar INTEGER or REAL "
                              "object");
        }
        if (statement->right != NULL && statement->limit != NULL && statement->step != NULL &&
            (!ordered_numeric_scalar(statement->right) ||
             !ordered_numeric_scalar(statement->limit) ||
             !ordered_numeric_scalar(statement->step))) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, statement->right), 1,
                              "counted DO initial value, limit, and step must be scalar INTEGER "
                              "or REAL expressions");
        }
        if (statement->step != NULL && statement->step->type == TYPE_INTEGER &&
            f2c_evaluate_integer_constant(unit, statement->step, &step) && step == 0) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, statement->step), 1,
                              "counted DO step cannot be zero");
        }
    } else if (statement->kind == F2C_STMT_GOTO && expression != NULL) {
        if (expression->type != TYPE_INTEGER || expression->rank != 0U) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, expression), 1,
                              "computed GOTO selector must be a scalar INTEGER");
        }
    } else if (statement->kind == F2C_STMT_ASSIGNED_GOTO ||
               statement->kind == F2C_STMT_ASSIGN_LABEL) {
        if (expression != NULL && (expression->type != TYPE_INTEGER || expression->rank != 0U ||
                                   !expression->definable)) {
            f2c_diagnostic_at(
                context, statement->line, expression_start_column(statement->text, expression), 1,
                "%s target must be a definable scalar INTEGER object",
                statement->kind == F2C_STMT_ASSIGN_LABEL ? "ASSIGN" : "assigned GOTO");
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
                                  expression_start_column(statement->text, argument), 1,
                                  "NULLIFY object must be a whole POINTER object");
            } else if (argument->kind == F2C_EXPR_COMPONENT && symbol->rank != 0U) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, argument), 1,
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
                              expression_start_column(statement->text, left), 1,
                              "pointer-assignment target must be a whole POINTER object");
            return;
        }
        if (pointer->procedure_pointer) {
            if (left->kind != F2C_EXPR_NAME && left->kind != F2C_EXPR_COMPONENT) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, left), 1,
                                  "procedure pointer assignment requires a whole pointer object");
            } else if (!null_target && (target == NULL || !target->external ||
                                        !procedure_signatures_compatible(pointer, target, 0U))) {
                f2c_diagnostic_at(context, statement->line,
                                  expression_start_column(statement->text, right), 1,
                                  "procedure pointer target must have a compatible explicit "
                                  "interface");
            }
            return;
        }
        if (left->kind == F2C_EXPR_COMPONENT && pointer->rank != 0U) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, left), 1,
                              "array POINTER components require a component descriptor and are "
                              "not yet supported");
            return;
        }
        if (!null_target &&
            (target == NULL || (!target->target && !target->pointer && !target->allocatable))) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, right), 1,
                              "pointer-assignment value must be a whole TARGET, POINTER, "
                              "ALLOCATABLE object, or NULL()");
        } else if (!null_target && target != NULL &&
                   (target->type != pointer->type || target->kind != pointer->kind ||
                    target->rank != pointer->rank)) {
            f2c_diagnostic_at(context, statement->line,
                              expression_start_column(statement->text, right), 1,
                              "pointer-assignment objects have incompatible type, kind, or rank");
        }
    }
}

static void validate_statement(Context *context, Unit *unit, F2cStatement *statement) {
    size_t i;
    size_t j;
    report_parse_error(context, statement->line, statement->text, statement->expression,
                       "statement");
    validate_constructor_semantics(context, unit, statement->line, statement->text,
                                   statement->expression);
    validate_expression_calls(context, unit, statement->line, statement->text,
                              statement->expression);
    report_parse_error(context, statement->line, statement->text, statement->left,
                       "left-hand side");
    validate_constructor_semantics(context, unit, statement->line, statement->text,
                                   statement->left);
    validate_expression_calls(context, unit, statement->line, statement->text, statement->left);
    report_parse_error(context, statement->line, statement->text, statement->right,
                       "right-hand side");
    validate_constructor_semantics(context, unit, statement->line, statement->text,
                                   statement->right);
    validate_expression_calls(context, unit, statement->line, statement->text, statement->right);
    report_parse_error(context, statement->line, statement->text, statement->limit, "limit");
    report_parse_error(context, statement->line, statement->text, statement->step, "step");
    validate_expression_calls(context, unit, statement->line, statement->text, statement->limit);
    validate_expression_calls(context, unit, statement->line, statement->text, statement->step);
    validate_control_flow_statement(context, unit, statement);
    validate_pointer_statement(context, statement);
    validate_intrinsic_assignment(context, statement);
    validate_constructor_assignment(context, unit, statement);
    if (statement->kind == F2C_STMT_ALLOCATE || statement->kind == F2C_STMT_DEALLOCATE)
        validate_allocation_statement(context, unit, statement);
    if (statement->kind != F2C_STMT_READ && statement->kind != F2C_STMT_WRITE &&
        statement->kind != F2C_STMT_PRINT) {
        for (i = 0U; i < statement->item_count; ++i)
            report_parse_error(context, statement->line, statement->text,
                               statement->arguments != NULL ? statement->arguments[i] : NULL,
                               "argument");
        for (i = 0U; i < statement->item_count; ++i)
            validate_constructor_semantics(context, unit, statement->line, statement->text,
                                           statement->arguments != NULL ? statement->arguments[i]
                                                                        : NULL);
        for (i = 0U; i < statement->item_count; ++i)
            validate_expression_calls(context, unit, statement->line, statement->text,
                                      statement->arguments != NULL ? statement->arguments[i]
                                                                   : NULL);
    }
    if (statement->kind == F2C_STMT_MOVE_ALLOC) {
        validate_move_alloc_statement(context, unit, statement);
    } else if (statement->kind == F2C_STMT_CALL && statement->expression == NULL) {
        Unit *definition = validate_procedure_call(context, unit, statement->line, statement->text,
                                                   statement->name, &statement->arguments,
                                                   &statement->items, &statement->item_count, 1);
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
        report_parse_error(context, statement->line, statement->text,
                           statement->io_controls[i].value, "I/O control");
    for (i = 0U; i < statement->control_count; ++i)
        validate_expression_calls(context, unit, statement->line, statement->text,
                                  statement->io_controls[i].value);
    for (i = 0U; i < statement->io_item_count; ++i) {
        validate_io_item(context, statement->line, statement->text, &statement->io_items[i]);
        validate_io_item_calls(context, unit, statement->line, statement->text,
                               &statement->io_items[i]);
    }
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_OPEN || statement->kind == F2C_STMT_REWIND ||
        statement->kind == F2C_STMT_CLOSE)
        validate_io_statement_semantics(context, unit, statement);
    for (i = 0U; i < statement->data_group_count; ++i) {
        F2cDataGroup *group = &statement->data_groups[i];
        for (j = 0U; j < group->target_count; ++j)
            validate_io_item(context, statement->line, statement->text, &group->targets[j]);
        for (j = 0U; j < group->target_count; ++j)
            validate_io_item_calls(context, unit, statement->line, statement->text,
                                   &group->targets[j]);
        for (j = 0U; j < group->value_count; ++j) {
            report_parse_error(context, statement->line, statement->text, group->values[j].repeat,
                               "DATA repeat");
            report_parse_error(context, statement->line, statement->text,
                               group->values[j].expression, "DATA value");
            validate_expression_calls(context, unit, statement->line, statement->text,
                                      group->values[j].repeat);
            validate_expression_calls(context, unit, statement->line, statement->text,
                                      group->values[j].expression);
        }
    }
    if (statement->nested != NULL)
        validate_statement(context, unit, statement->nested);
}

static F2cExpr *parse_specification_expression(Context *context, Unit *unit, size_t line,
                                               const char *text, const char *role) {
    const char *error_at = NULL;
    const char *source_line;
    F2cExpr *expression;
    if (text == NULL || strcmp(text, "*") == 0 || strcmp(text, ":") == 0)
        return NULL;
    expression = f2c_parse_expression_ast(unit, text, &error_at);
    source_line = unit_line_text(context, unit, line);
    if (expression == NULL) {
        f2c_diagnostic_at(context, line, 1U, 1, "out of memory while validating %s expression",
                          role);
    } else if (error_at != NULL) {
        report_parse_error(context, line, source_line, expression, role);
    }
    return expression;
}

static void validate_integer_specification(Context *context, size_t line, const char *source_line,
                                           const F2cExpr *expression, const char *role) {
    if (expression != NULL && expression->parse_error_offset == SIZE_MAX &&
        ((expression->type != TYPE_UNKNOWN && expression->type != TYPE_INTEGER) ||
         expression->rank != 0U)) {
        f2c_diagnostic_at(context, line, expression_column(source_line, expression), 1,
                          "%s must be a scalar INTEGER expression", role);
    }
}

static void validate_declaration_initializer(Context *context, size_t line, const char *source_line,
                                             const Symbol *symbol) {
    const F2cExpr *initializer = symbol->initializer_expression;
    if (initializer == NULL || initializer->parse_error_offset != SIZE_MAX)
        return;
    if (symbol->type != TYPE_UNKNOWN && initializer->type != TYPE_UNKNOWN &&
        !constructor_type_compatible(symbol->type, initializer->type)) {
        f2c_diagnostic_at(context, line, expression_column(source_line, initializer), 1,
                          "declaration initializer type is incompatible with '%s'", symbol->name);
    }
    if (initializer->rank != 0U && initializer->rank != symbol->rank) {
        f2c_diagnostic_at(context, line, expression_column(source_line, initializer), 1,
                          "declaration initializer rank %zu does not match rank-%zu entity '%s'",
                          initializer->rank, symbol->rank, symbol->name);
    }
}

void f2c_validate_unit_expressions(Context *context, Unit *unit) {
    size_t i;
    size_t dimension;
    for (i = 0U; i < unit->statement_count; ++i)
        validate_statement(context, unit, &unit->statements[i]);
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const size_t line = symbol->declaration_line != 0U
                                ? symbol->declaration_line
                                : context->lines.items[unit->begin].number;
        const char *source_line = unit_line_text(context, unit, line);
        symbol->initializer_expression = parse_specification_expression(
            context, unit, line, symbol->initializer, "declaration initializer");
        symbol->character_length_expression = parse_specification_expression(
            context, unit, line, symbol->character_length, "character length");
        validate_expression_calls(context, unit, line, source_line, symbol->initializer_expression);
        validate_expression_calls(context, unit, line, source_line,
                                  symbol->character_length_expression);
        validate_integer_specification(context, line, source_line,
                                       symbol->character_length_expression, "character length");
        validate_declaration_initializer(context, line, source_line, symbol);
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            Dimension *shape = &symbol->dimensions[dimension];
            shape->lower_expression = parse_specification_expression(
                context, unit, line, shape->lower, "array lower bound");
            shape->upper_expression =
                shape->kind == F2C_DIMENSION_EXPLICIT
                    ? parse_specification_expression(context, unit, line, shape->upper,
                                                     "array upper bound")
                    : NULL;
            validate_expression_calls(context, unit, line, source_line, shape->lower_expression);
            validate_expression_calls(context, unit, line, source_line, shape->upper_expression);
            validate_integer_specification(context, line, source_line, shape->lower_expression,
                                           "array lower bound");
            validate_integer_specification(context, line, source_line, shape->upper_expression,
                                           "array upper bound");
        }
        f2c_shape_from_symbol(unit, &symbol->shape, symbol);
    }
    (void)context;
}
