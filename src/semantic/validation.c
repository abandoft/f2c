#include "semantic/validation/private.h"

#include <ctype.h>
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
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR && expression->type == TYPE_DERIVED &&
        expression->derived_type != NULL) {
        for (i = 0U; i < expression->child_count; ++i) {
            const F2cExpr *value = expression->children[i];
            if (value->type == TYPE_DERIVED && value->derived_type != NULL &&
                value->derived_type != expression->derived_type) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_column(statement_text, value), 1,
                                  "array-constructor derived values must have the same type");
            }
        }
    }
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
    case F2C_STMT_BACKSPACE:
        return "BACKSPACE";
    case F2C_STMT_ENDFILE:
        return "ENDFILE";
    case F2C_STMT_INQUIRE:
        return "INQUIRE";
    case F2C_STMT_CLOSE:
        return "CLOSE";
    default:
        return "I/O statement";
    }
}

static const char *io_control_name(F2cIoControlKind kind) {
    static const char *const names[F2C_IO_CONTROL_READWRITE + 1U] = {
        [F2C_IO_CONTROL_POSITIONAL] = "positional",
        [F2C_IO_CONTROL_UNKNOWN] = "unknown",
        [F2C_IO_CONTROL_UNIT] = "UNIT",
        [F2C_IO_CONTROL_FMT] = "FMT",
        [F2C_IO_CONTROL_NML] = "NML",
        [F2C_IO_CONTROL_END] = "END",
        [F2C_IO_CONTROL_EOR] = "EOR",
        [F2C_IO_CONTROL_ERR] = "ERR",
        [F2C_IO_CONTROL_IOSTAT] = "IOSTAT",
        [F2C_IO_CONTROL_IOMSG] = "IOMSG",
        [F2C_IO_CONTROL_SIZE] = "SIZE",
        [F2C_IO_CONTROL_ADVANCE] = "ADVANCE",
        [F2C_IO_CONTROL_REC] = "REC",
        [F2C_IO_CONTROL_POS] = "POS",
        [F2C_IO_CONTROL_FILE] = "FILE",
        [F2C_IO_CONTROL_STATUS] = "STATUS",
        [F2C_IO_CONTROL_ACCESS] = "ACCESS",
        [F2C_IO_CONTROL_ACTION] = "ACTION",
        [F2C_IO_CONTROL_FORM] = "FORM",
        [F2C_IO_CONTROL_RECL] = "RECL",
        [F2C_IO_CONTROL_BLANK] = "BLANK",
        [F2C_IO_CONTROL_DECIMAL] = "DECIMAL",
        [F2C_IO_CONTROL_DELIM] = "DELIM",
        [F2C_IO_CONTROL_ENCODING] = "ENCODING",
        [F2C_IO_CONTROL_PAD] = "PAD",
        [F2C_IO_CONTROL_ROUND] = "ROUND",
        [F2C_IO_CONTROL_SIGN] = "SIGN",
        [F2C_IO_CONTROL_ASYNCHRONOUS] = "ASYNCHRONOUS",
        [F2C_IO_CONTROL_ID] = "ID",
        [F2C_IO_CONTROL_NEWUNIT] = "NEWUNIT",
        [F2C_IO_CONTROL_EXIST] = "EXIST",
        [F2C_IO_CONTROL_OPENED] = "OPENED",
        [F2C_IO_CONTROL_NUMBER] = "NUMBER",
        [F2C_IO_CONTROL_NAMED] = "NAMED",
        [F2C_IO_CONTROL_NAME] = "NAME",
        [F2C_IO_CONTROL_SEQUENTIAL] = "SEQUENTIAL",
        [F2C_IO_CONTROL_DIRECT] = "DIRECT",
        [F2C_IO_CONTROL_FORMATTED] = "FORMATTED",
        [F2C_IO_CONTROL_UNFORMATTED] = "UNFORMATTED",
        [F2C_IO_CONTROL_NEXTREC] = "NEXTREC",
        [F2C_IO_CONTROL_POSITION] = "POSITION",
        [F2C_IO_CONTROL_READ] = "READ",
        [F2C_IO_CONTROL_WRITE] = "WRITE",
        [F2C_IO_CONTROL_READWRITE] = "READWRITE",
    };
    return (size_t)kind < sizeof(names) / sizeof(names[0]) && names[kind] != NULL ? names[kind]
                                                                                  : "unknown";
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
               control_kind == F2C_IO_CONTROL_IOSTAT || control_kind == F2C_IO_CONTROL_IOMSG ||
               control_kind == F2C_IO_CONTROL_ACCESS || control_kind == F2C_IO_CONTROL_ACTION ||
               control_kind == F2C_IO_CONTROL_FORM || control_kind == F2C_IO_CONTROL_RECL ||
               control_kind == F2C_IO_CONTROL_BLANK || control_kind == F2C_IO_CONTROL_POSITION ||
               control_kind == F2C_IO_CONTROL_DELIM || control_kind == F2C_IO_CONTROL_PAD;
    if (statement_kind == F2C_STMT_CLOSE)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_STATUS ||
               control_kind == F2C_IO_CONTROL_ERR || control_kind == F2C_IO_CONTROL_IOSTAT ||
               control_kind == F2C_IO_CONTROL_IOMSG;
    if (statement_kind == F2C_STMT_REWIND || statement_kind == F2C_STMT_BACKSPACE ||
        statement_kind == F2C_STMT_ENDFILE)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_ERR ||
               control_kind == F2C_IO_CONTROL_IOSTAT || control_kind == F2C_IO_CONTROL_IOMSG;
    if (statement_kind == F2C_STMT_INQUIRE)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_FILE ||
               control_kind == F2C_IO_CONTROL_ERR || control_kind == F2C_IO_CONTROL_IOSTAT ||
               control_kind == F2C_IO_CONTROL_IOMSG || control_kind == F2C_IO_CONTROL_EXIST ||
               control_kind == F2C_IO_CONTROL_OPENED || control_kind == F2C_IO_CONTROL_NUMBER ||
               control_kind == F2C_IO_CONTROL_NAMED || control_kind == F2C_IO_CONTROL_NAME ||
               control_kind == F2C_IO_CONTROL_ACCESS ||
               control_kind == F2C_IO_CONTROL_SEQUENTIAL ||
               control_kind == F2C_IO_CONTROL_DIRECT || control_kind == F2C_IO_CONTROL_FORM ||
               control_kind == F2C_IO_CONTROL_FORMATTED ||
               control_kind == F2C_IO_CONTROL_UNFORMATTED ||
               control_kind == F2C_IO_CONTROL_RECL || control_kind == F2C_IO_CONTROL_NEXTREC ||
               control_kind == F2C_IO_CONTROL_BLANK ||
               control_kind == F2C_IO_CONTROL_POSITION || control_kind == F2C_IO_CONTROL_ACTION ||
               control_kind == F2C_IO_CONTROL_READ || control_kind == F2C_IO_CONTROL_WRITE ||
               control_kind == F2C_IO_CONTROL_READWRITE ||
               control_kind == F2C_IO_CONTROL_DELIM || control_kind == F2C_IO_CONTROL_PAD;
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
    if ((statement_kind == F2C_STMT_CLOSE || statement_kind == F2C_STMT_REWIND ||
         statement_kind == F2C_STMT_BACKSPACE || statement_kind == F2C_STMT_ENDFILE) &&
        position == 0U)
        return F2C_IO_CONTROL_UNIT;
    return F2C_IO_CONTROL_UNKNOWN;
}

static int scalar_type(const F2cExpr *expression, Type type) {
    return expression != NULL && expression->type == type && expression->rank == 0U;
}

static int inquiry_logical_result(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_EXIST || kind == F2C_IO_CONTROL_OPENED ||
           kind == F2C_IO_CONTROL_NAMED;
}

static int inquiry_integer_result(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_NUMBER || kind == F2C_IO_CONTROL_RECL ||
           kind == F2C_IO_CONTROL_NEXTREC;
}

static int inquiry_character_result(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_NAME || kind == F2C_IO_CONTROL_ACCESS ||
           kind == F2C_IO_CONTROL_SEQUENTIAL || kind == F2C_IO_CONTROL_DIRECT ||
           kind == F2C_IO_CONTROL_FORM || kind == F2C_IO_CONTROL_FORMATTED ||
           kind == F2C_IO_CONTROL_UNFORMATTED || kind == F2C_IO_CONTROL_BLANK ||
           kind == F2C_IO_CONTROL_POSITION || kind == F2C_IO_CONTROL_ACTION ||
           kind == F2C_IO_CONTROL_READ || kind == F2C_IO_CONTROL_WRITE ||
           kind == F2C_IO_CONTROL_READWRITE || kind == F2C_IO_CONTROL_DELIM ||
           kind == F2C_IO_CONTROL_PAD;
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
            if (statement->kind != F2C_STMT_READ && statement->kind != F2C_STMT_WRITE) {
                f2c_diagnostic_at(context, statement->line, column, 1,
                                  "%s UNIT= must be a scalar INTEGER", statement_name);
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
                              "%s UNIT= must be %sa scalar INTEGER", statement_name,
                              statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE
                                  ? "an asterisk or "
                                  : "");
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
    } else if (statement->kind == F2C_STMT_INQUIRE &&
               inquiry_logical_result(semantic_kind)) {
        if (control->asterisk || !scalar_type(value, TYPE_LOGICAL) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "INQUIRE %s= must be a definable scalar LOGICAL variable",
                              io_control_name(semantic_kind));
        }
    } else if (statement->kind == F2C_STMT_INQUIRE &&
               inquiry_integer_result(semantic_kind)) {
        if (control->asterisk || !scalar_type(value, TYPE_INTEGER) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "INQUIRE %s= must be a definable scalar INTEGER variable",
                              io_control_name(semantic_kind));
        }
    } else if (statement->kind == F2C_STMT_INQUIRE &&
               inquiry_character_result(semantic_kind)) {
        if (control->asterisk || !scalar_type(value, TYPE_CHARACTER) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "INQUIRE %s= must be a definable scalar CHARACTER variable",
                              io_control_name(semantic_kind));
        }
    } else if (semantic_kind == F2C_IO_CONTROL_RECL) {
        if (control->asterisk || !scalar_type(value, TYPE_INTEGER)) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s RECL= must be a scalar INTEGER expression", statement_name);
        }
    } else if (semantic_kind == F2C_IO_CONTROL_ADVANCE || semantic_kind == F2C_IO_CONTROL_FILE ||
               semantic_kind == F2C_IO_CONTROL_STATUS || semantic_kind == F2C_IO_CONTROL_FORM ||
               semantic_kind == F2C_IO_CONTROL_ACCESS || semantic_kind == F2C_IO_CONTROL_ACTION ||
               semantic_kind == F2C_IO_CONTROL_BLANK ||
               semantic_kind == F2C_IO_CONTROL_POSITION || semantic_kind == F2C_IO_CONTROL_DELIM ||
               semantic_kind == F2C_IO_CONTROL_PAD) {
        if (control->asterisk || !scalar_type(value, TYPE_CHARACTER)) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s %s= must be a scalar CHARACTER expression", statement_name,
                              io_control_name(semantic_kind));
        }
    }
}

static const F2cIoControl *find_io_control(const F2cStatement *statement,
                                          F2cIoControlKind kind) {
    size_t index;
    for (index = 0U; index < statement->control_count; ++index)
        if (statement->io_controls[index].kind == kind)
            return &statement->io_controls[index];
    return NULL;
}

static int character_control_value(const F2cIoControl *control, char *value, size_t capacity) {
    const char *text = control != NULL && control->value != NULL ? control->value->text : NULL;
    const char *quote = text != NULL ? f2c_character_literal_quote(text) : NULL;
    char delimiter;
    size_t length = 0U;
    size_t begin = 0U;
    size_t end;
    if (quote == NULL || control->value->kind != F2C_EXPR_STRING_LITERAL || capacity == 0U)
        return 0;
    delimiter = *quote;
    ++quote;
    while (*quote != '\0') {
        unsigned char character;
        if (*quote == delimiter) {
            if (quote[1] != delimiter)
                break;
            character = (unsigned char)delimiter;
            quote += 2;
        } else {
            character = (unsigned char)*quote++;
        }
        if (length + 1U >= capacity)
            return 0;
        value[length++] = (char)tolower(character);
    }
    while (begin < length && value[begin] == ' ')
        ++begin;
    end = length;
    while (end > begin && value[end - 1U] == ' ')
        --end;
    if (begin != 0U && end > begin)
        memmove(value, value + begin, end - begin);
    length = end - begin;
    value[length] = '\0';
    return 1;
}

static int value_in_choices(const char *value, const char *const *choices, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index)
        if (strcmp(value, choices[index]) == 0)
            return 1;
    return 0;
}

static void validate_character_choices(Context *context, const F2cStatement *statement,
                                       F2cIoControlKind kind, const char *const *choices,
                                       size_t choice_count) {
    const F2cIoControl *control = find_io_control(statement, kind);
    char value[32];
    if (control != NULL && character_control_value(control, value, sizeof(value)) &&
        !value_in_choices(value, choices, choice_count)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &control->span, 1,
                                 "%s %s= has invalid value '%s'", io_statement_name(statement->kind),
                                 io_control_name(kind), value);
    }
}

static void validate_file_control_relations(Context *context, Unit *unit,
                                            const F2cStatement *statement,
                                            const unsigned char *seen) {
    static const char *const open_status[] = {"old", "new", "scratch", "replace", "unknown"};
    static const char *const close_status[] = {"keep", "delete"};
    static const char *const access[] = {"sequential", "direct"};
    static const char *const action[] = {"read", "write", "readwrite"};
    static const char *const form[] = {"formatted", "unformatted"};
    static const char *const blank[] = {"null", "zero"};
    static const char *const position[] = {"asis", "rewind", "append"};
    static const char *const delim[] = {"apostrophe", "quote", "none"};
    static const char *const yes_no[] = {"yes", "no"};
    const F2cIoControl *status = find_io_control(statement, F2C_IO_CONTROL_STATUS);
    const F2cIoControl *access_control = find_io_control(statement, F2C_IO_CONTROL_ACCESS);
    const F2cIoControl *recl = find_io_control(statement, F2C_IO_CONTROL_RECL);
    char status_value[32];
    char access_value[32];
    int64_t recl_value;
    if (statement->kind == F2C_STMT_CLOSE) {
        validate_character_choices(context, statement, F2C_IO_CONTROL_STATUS, close_status,
                                   sizeof(close_status) / sizeof(close_status[0]));
        return;
    }
    if (statement->kind != F2C_STMT_OPEN)
        return;
    validate_character_choices(context, statement, F2C_IO_CONTROL_STATUS, open_status,
                               sizeof(open_status) / sizeof(open_status[0]));
    validate_character_choices(context, statement, F2C_IO_CONTROL_ACCESS, access,
                               sizeof(access) / sizeof(access[0]));
    validate_character_choices(context, statement, F2C_IO_CONTROL_ACTION, action,
                               sizeof(action) / sizeof(action[0]));
    validate_character_choices(context, statement, F2C_IO_CONTROL_FORM, form,
                               sizeof(form) / sizeof(form[0]));
    validate_character_choices(context, statement, F2C_IO_CONTROL_BLANK, blank,
                               sizeof(blank) / sizeof(blank[0]));
    validate_character_choices(context, statement, F2C_IO_CONTROL_POSITION, position,
                               sizeof(position) / sizeof(position[0]));
    validate_character_choices(context, statement, F2C_IO_CONTROL_DELIM, delim,
                               sizeof(delim) / sizeof(delim[0]));
    validate_character_choices(context, statement, F2C_IO_CONTROL_PAD, yes_no,
                               sizeof(yes_no) / sizeof(yes_no[0]));
    if (recl != NULL && recl->value != NULL &&
        f2c_evaluate_integer_constant(unit, recl->value, &recl_value) && recl_value <= 0) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &recl->span, 1,
                                 "OPEN RECL= must be positive");
    }
    if (status != NULL && character_control_value(status, status_value, sizeof(status_value)) &&
        strcmp(status_value, "scratch") == 0 && seen[F2C_IO_CONTROL_FILE]) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &status->span, 1,
                                 "OPEN STATUS='SCRATCH' cannot specify FILE=");
    }
    if (access_control != NULL &&
        character_control_value(access_control, access_value, sizeof(access_value))) {
        if (strcmp(access_value, "direct") == 0 && !seen[F2C_IO_CONTROL_RECL])
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &access_control->span, 1,
                                     "OPEN ACCESS='DIRECT' requires RECL=");
        if (strcmp(access_value, "sequential") == 0 && seen[F2C_IO_CONTROL_RECL])
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &access_control->span, 1,
                                     "OPEN RECL= is valid only with ACCESS='DIRECT'");
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
    unsigned char seen[F2C_IO_CONTROL_READWRITE + 1U] = {0};
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
    if (statement->kind == F2C_STMT_INQUIRE &&
        seen[F2C_IO_CONTROL_UNIT] == seen[F2C_IO_CONTROL_FILE]) {
        f2c_diagnostic_at(context, statement->line, 1U, 1,
                          "INQUIRE requires exactly one of UNIT= or FILE=");
    } else if (statement->kind != F2C_STMT_INQUIRE && !seen[F2C_IO_CONTROL_UNIT]) {
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
    validate_file_control_relations(context, unit, statement, seen);
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
