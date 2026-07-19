#include "semantic/validation/private.h"

#include "ast/format.h"

#include <ctype.h>
#include <string.h>

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
    case F2C_STMT_PRINT:
        return "PRINT";
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
        [F2C_IO_CONTROL_IOLENGTH] = "IOLENGTH",
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
    if (statement_kind == F2C_STMT_PRINT)
        return control_kind == F2C_IO_CONTROL_FMT;
    if (statement_kind == F2C_STMT_READ)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_FMT ||
               control_kind == F2C_IO_CONTROL_NML || control_kind == F2C_IO_CONTROL_END ||
               control_kind == F2C_IO_CONTROL_EOR || control_kind == F2C_IO_CONTROL_ERR ||
               control_kind == F2C_IO_CONTROL_IOSTAT || control_kind == F2C_IO_CONTROL_IOMSG ||
               control_kind == F2C_IO_CONTROL_SIZE || control_kind == F2C_IO_CONTROL_ADVANCE ||
               control_kind == F2C_IO_CONTROL_REC;
    if (statement_kind == F2C_STMT_WRITE)
        return control_kind == F2C_IO_CONTROL_UNIT || control_kind == F2C_IO_CONTROL_FMT ||
               control_kind == F2C_IO_CONTROL_NML || control_kind == F2C_IO_CONTROL_ERR ||
               control_kind == F2C_IO_CONTROL_IOSTAT || control_kind == F2C_IO_CONTROL_IOMSG ||
               control_kind == F2C_IO_CONTROL_ADVANCE || control_kind == F2C_IO_CONTROL_REC;
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
        return control_kind == F2C_IO_CONTROL_IOLENGTH || control_kind == F2C_IO_CONTROL_UNIT ||
               control_kind == F2C_IO_CONTROL_FILE || control_kind == F2C_IO_CONTROL_ERR ||
               control_kind == F2C_IO_CONTROL_IOSTAT || control_kind == F2C_IO_CONTROL_IOMSG ||
               control_kind == F2C_IO_CONTROL_EXIST || control_kind == F2C_IO_CONTROL_OPENED ||
               control_kind == F2C_IO_CONTROL_NUMBER || control_kind == F2C_IO_CONTROL_NAMED ||
               control_kind == F2C_IO_CONTROL_NAME || control_kind == F2C_IO_CONTROL_ACCESS ||
               control_kind == F2C_IO_CONTROL_SEQUENTIAL || control_kind == F2C_IO_CONTROL_DIRECT ||
               control_kind == F2C_IO_CONTROL_FORM || control_kind == F2C_IO_CONTROL_FORMATTED ||
               control_kind == F2C_IO_CONTROL_UNFORMATTED || control_kind == F2C_IO_CONTROL_RECL ||
               control_kind == F2C_IO_CONTROL_NEXTREC || control_kind == F2C_IO_CONTROL_BLANK ||
               control_kind == F2C_IO_CONTROL_POSITION || control_kind == F2C_IO_CONTROL_ACTION ||
               control_kind == F2C_IO_CONTROL_READ || control_kind == F2C_IO_CONTROL_WRITE ||
               control_kind == F2C_IO_CONTROL_READWRITE || control_kind == F2C_IO_CONTROL_DELIM ||
               control_kind == F2C_IO_CONTROL_PAD;
    return 0;
}

static F2cIoControlKind positional_io_control_kind(F2cStatementKind statement_kind,
                                                   size_t position) {
    if ((statement_kind == F2C_STMT_READ || statement_kind == F2C_STMT_WRITE) && position == 0U)
        return F2C_IO_CONTROL_UNIT;
    if ((statement_kind == F2C_STMT_READ || statement_kind == F2C_STMT_WRITE) && position == 1U)
        return F2C_IO_CONTROL_FMT;
    if (statement_kind == F2C_STMT_PRINT && position == 0U)
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

static const F2cStatement *find_format_statement(const Unit *unit, const char *label) {
    size_t index;
    if (unit == NULL || label == NULL)
        return NULL;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *statement = &unit->statements[index];
        if (statement->kind == F2C_STMT_FORMAT && statement->name != NULL &&
            strcmp(statement->name, label) == 0)
            return statement;
    }
    return NULL;
}

static int statement_assigns_format_label(const Unit *unit, const F2cStatement *statement,
                                          const char *name) {
    if (statement == NULL || name == NULL)
        return 0;
    if (statement->kind == F2C_STMT_ASSIGN_LABEL && statement->name != NULL &&
        strcmp(statement->name, name) == 0 && statement->label_count == 1U &&
        find_format_statement(unit, statement->labels[0]) != NULL)
        return 1;
    return statement_assigns_format_label(unit, statement->nested, name);
}

static int unit_assigns_format_label(const Unit *unit, const char *name) {
    size_t index;
    if (unit == NULL || name == NULL)
        return 0;
    for (index = 0U; index < unit->statement_count; ++index)
        if (statement_assigns_format_label(unit, &unit->statements[index], name))
            return 1;
    return 0;
}

static F2cSourceSpan format_error_span(const F2cIoControl *control) {
    F2cSourceSpan span = control->format_span;
    if (span.begin.line != 0U && span.begin.line == span.end.line) {
        span.begin.column += control->format_error.offset;
        span.end = span.begin;
        ++span.end.column;
    }
    return span;
}

static int bind_constant_format(Context *context, Unit *unit, const F2cStatement *statement,
                                F2cIoControl *control) {
    const F2cExpr *value = control->value;
    const char *statement_name = io_statement_name(statement->kind);
    const F2cStatement *definition;
    f2c_format_free(control->format);
    control->format = NULL;
    memset(&control->format_error, 0, sizeof(control->format_error));
    if (value == NULL)
        return 0;
    if (value->type == TYPE_CHARACTER && value->kind != F2C_EXPR_STRING_LITERAL)
        return 1;
    if (value->type == TYPE_CHARACTER) {
        control->format_span = value->span;
        control->format =
            f2c_format_parse_character_literal(value->text, &value->span, &control->format_error);
        if (control->format == NULL) {
            const F2cSourceSpan span = format_error_span(control);
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &span, 1,
                                     "invalid constant %s FORMAT: %s", statement_name,
                                     f2c_format_error_message(control->format_error.code));
            return 0;
        }
        control->format->validated = 1;
        return 1;
    }
    if (value->type != TYPE_INTEGER || value->kind != F2C_EXPR_INTEGER_LITERAL)
        return 1;
    definition = find_format_statement(unit, value->text);
    if (definition == NULL || definition->format == NULL || !definition->format_syntax_valid) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &value->span, 1,
                                 "%s FORMAT label %s does not identify a valid FORMAT statement",
                                 statement_name, value->text != NULL ? value->text : "<unknown>");
        return 0;
    }
    control->format = f2c_format_clone(definition->format);
    if (control->format == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, &value->span, 1,
                                 "out of memory while binding %s FORMAT label %s", statement_name,
                                 value->text);
        return 0;
    }
    control->format->validated = 1;
    control->format_span = definition->format_span;
    return 1;
}

static int inquiry_logical_result(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_EXIST || kind == F2C_IO_CONTROL_OPENED ||
           kind == F2C_IO_CONTROL_NAMED;
}

static int inquiry_integer_result(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_IOLENGTH || kind == F2C_IO_CONTROL_NUMBER ||
           kind == F2C_IO_CONTROL_RECL || kind == F2C_IO_CONTROL_NEXTREC;
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
                                     F2cIoControl *control, F2cIoControlKind semantic_kind) {
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
        if (control->asterisk) {
            return;
        }
        if (scalar_type(value, TYPE_CHARACTER)) {
            (void)bind_constant_format(context, unit, statement, control);
            return;
        }
        if (scalar_type(value, TYPE_INTEGER) && value->kind == F2C_EXPR_INTEGER_LITERAL) {
            (void)bind_constant_format(context, unit, statement, control);
            return;
        }
        if (scalar_type(value, TYPE_INTEGER) && value->kind == F2C_EXPR_NAME && value->definable &&
            unit_assigns_format_label(unit, value->text)) {
            return;
        }
        if (scalar_type(value, TYPE_INTEGER)) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s FMT= INTEGER value must be a statement label or a variable "
                              "defined by ASSIGN",
                              statement_name);
        } else {
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
    } else if (statement->kind == F2C_STMT_INQUIRE && inquiry_logical_result(semantic_kind)) {
        if (control->asterisk || !scalar_type(value, TYPE_LOGICAL) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "INQUIRE %s= must be a definable scalar LOGICAL variable",
                              io_control_name(semantic_kind));
        }
    } else if (statement->kind == F2C_STMT_INQUIRE && inquiry_integer_result(semantic_kind)) {
        if (control->asterisk || !scalar_type(value, TYPE_INTEGER) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "INQUIRE %s= must be a definable scalar INTEGER variable",
                              io_control_name(semantic_kind));
        }
    } else if (statement->kind == F2C_STMT_INQUIRE && inquiry_character_result(semantic_kind)) {
        if (control->asterisk || !scalar_type(value, TYPE_CHARACTER) || !value->definable) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "INQUIRE %s= must be a definable scalar CHARACTER variable",
                              io_control_name(semantic_kind));
        }
    } else if (semantic_kind == F2C_IO_CONTROL_RECL || semantic_kind == F2C_IO_CONTROL_REC) {
        if (control->asterisk || !scalar_type(value, TYPE_INTEGER)) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s %s= must be a scalar INTEGER expression", statement_name,
                              io_control_name(semantic_kind));
        }
    } else if (semantic_kind == F2C_IO_CONTROL_ADVANCE || semantic_kind == F2C_IO_CONTROL_FILE ||
               semantic_kind == F2C_IO_CONTROL_STATUS || semantic_kind == F2C_IO_CONTROL_FORM ||
               semantic_kind == F2C_IO_CONTROL_ACCESS || semantic_kind == F2C_IO_CONTROL_ACTION ||
               semantic_kind == F2C_IO_CONTROL_BLANK || semantic_kind == F2C_IO_CONTROL_POSITION ||
               semantic_kind == F2C_IO_CONTROL_DELIM || semantic_kind == F2C_IO_CONTROL_PAD) {
        if (control->asterisk || !scalar_type(value, TYPE_CHARACTER)) {
            f2c_diagnostic_at(context, statement->line, column, 1,
                              "%s %s= must be a scalar CHARACTER expression", statement_name,
                              io_control_name(semantic_kind));
        }
    }
}

static const F2cIoControl *find_io_control(const F2cStatement *statement, F2cIoControlKind kind) {
    size_t index;
    for (index = 0U; index < statement->control_count; ++index)
        if (statement->io_controls[index].kind == kind)
            return &statement->io_controls[index];
    return NULL;
}

static const F2cIoControl *find_semantic_io_control(const F2cStatement *statement,
                                                    F2cIoControlKind kind) {
    size_t index;
    size_t positional = 0U;
    for (index = 0U; index < statement->control_count; ++index) {
        const F2cIoControl *control = &statement->io_controls[index];
        F2cIoControlKind semantic_kind = control->kind;
        if (semantic_kind == F2C_IO_CONTROL_POSITIONAL)
            semantic_kind = positional_io_control_kind(statement->kind, positional++);
        if (semantic_kind == kind)
            return control;
    }
    return NULL;
}

static const F2cIoControl *find_unit_control(const F2cStatement *statement) {
    size_t index;
    size_t positional = 0U;
    for (index = 0U; index < statement->control_count; ++index) {
        const F2cIoControl *control = &statement->io_controls[index];
        F2cIoControlKind kind = control->kind;
        if (kind == F2C_IO_CONTROL_POSITIONAL)
            kind = positional_io_control_kind(statement->kind, positional++);
        if (kind == F2C_IO_CONTROL_UNIT)
            return control;
    }
    return NULL;
}

static void validate_internal_file_relations(Context *context, const F2cStatement *statement,
                                             const unsigned char *seen) {
    const F2cIoControl *unit = find_unit_control(statement);
    if ((statement->kind != F2C_STMT_READ && statement->kind != F2C_STMT_WRITE) || unit == NULL ||
        unit->value == NULL || unit->value->type != TYPE_CHARACTER)
        return;
    if (seen[F2C_IO_CONTROL_ADVANCE] || seen[F2C_IO_CONTROL_EOR] || seen[F2C_IO_CONTROL_SIZE]) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &unit->span, 1,
                                 "internal-file %s cannot use ADVANCE=, EOR=, or SIZE=",
                                 io_statement_name(statement->kind));
    }
    if (!seen[F2C_IO_CONTROL_FMT] && !seen[F2C_IO_CONTROL_NML]) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &unit->span, 1,
                                 "internal-file %s must be formatted",
                                 io_statement_name(statement->kind));
    }
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
                                 "%s %s= has invalid value '%s'",
                                 io_statement_name(statement->kind), io_control_name(kind), value);
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

static void validate_transfer_relations(Context *context, Unit *unit, const F2cStatement *statement,
                                        const unsigned char *seen) {
    static const char *const yes_no[] = {"yes", "no"};
    const F2cIoControl *unit_control;
    const F2cIoControl *record;
    const F2cIoControl *format;
    const F2cIoControl *advance;
    char advance_value[16];
    int64_t record_value;
    int internal;
    int formatted;
    if (statement->kind != F2C_STMT_READ && statement->kind != F2C_STMT_WRITE)
        return;
    unit_control = find_unit_control(statement);
    record = find_semantic_io_control(statement, F2C_IO_CONTROL_REC);
    format = find_semantic_io_control(statement, F2C_IO_CONTROL_FMT);
    advance = find_semantic_io_control(statement, F2C_IO_CONTROL_ADVANCE);
    internal = unit_control != NULL && unit_control->value != NULL &&
               unit_control->value->type == TYPE_CHARACTER;
    formatted = seen[F2C_IO_CONTROL_FMT] || seen[F2C_IO_CONTROL_NML];
    if (advance != NULL) {
        validate_character_choices(context, statement, F2C_IO_CONTROL_ADVANCE, yes_no,
                                   sizeof(yes_no) / sizeof(yes_no[0]));
        if (character_control_value(advance, advance_value, sizeof(advance_value)) &&
            strcmp(advance_value, "yes") == 0 &&
            (seen[F2C_IO_CONTROL_EOR] || seen[F2C_IO_CONTROL_SIZE])) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &advance->span, 1,
                                     "EOR= and SIZE= require ADVANCE='NO'");
        }
    }
    if (!formatted &&
        (seen[F2C_IO_CONTROL_ADVANCE] || seen[F2C_IO_CONTROL_EOR] || seen[F2C_IO_CONTROL_SIZE])) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                 "unformatted %s cannot use ADVANCE=, EOR=, or SIZE=",
                                 io_statement_name(statement->kind));
    }
    if (record == NULL)
        return;
    if (record->value != NULL &&
        f2c_evaluate_integer_constant(unit, record->value, &record_value) && record_value <= 0) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &record->span, 1,
                                 "%s REC= must be positive", io_statement_name(statement->kind));
    }
    if (internal) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &record->span, 1,
            "internal-file %s cannot specify REC=", io_statement_name(statement->kind));
    }
    if (format != NULL && format->asterisk) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &format->span, 1,
                                 "direct-access %s cannot use list-directed formatting",
                                 io_statement_name(statement->kind));
    }
    if (seen[F2C_IO_CONTROL_NML]) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &record->span, 1,
                                 "direct-access %s cannot use NAMELIST",
                                 io_statement_name(statement->kind));
    }
    if (seen[F2C_IO_CONTROL_END] || seen[F2C_IO_CONTROL_ADVANCE] || seen[F2C_IO_CONTROL_EOR] ||
        seen[F2C_IO_CONTROL_SIZE]) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &record->span, 1,
                                 "direct-access %s cannot use END=, ADVANCE=, EOR=, or SIZE=",
                                 io_statement_name(statement->kind));
    }
}

static int derived_has_io_binding(const F2cDerivedType *derived, int input, int unformatted) {
    const F2cDefinedIoKind kind =
        unformatted ? (input ? F2C_DEFINED_IO_READ_UNFORMATTED : F2C_DEFINED_IO_WRITE_UNFORMATTED)
                    : (input ? F2C_DEFINED_IO_READ_FORMATTED : F2C_DEFINED_IO_WRITE_FORMATTED);
    while (derived != NULL) {
        if (derived->defined_io_bindings[kind] != NULL)
            return 1;
        derived = derived->parent;
    }
    return 0;
}

static int derived_requires_defined_io(const F2cDerivedType *derived) {
    size_t index;
    if (derived == NULL)
        return 0;
    if (derived_requires_defined_io(derived->parent))
        return 1;
    for (index = 0U; index < derived->component_count; ++index) {
        const Symbol *component = &derived->components[index];
        if (component->allocatable || component->pointer || component->procedure_pointer ||
            (component->type == TYPE_DERIVED &&
             derived_requires_defined_io(component->derived_type)))
            return 1;
    }
    return 0;
}

static int iolength_inquiry(const F2cStatement *statement) {
    size_t index;
    if (statement == NULL || statement->kind != F2C_STMT_INQUIRE)
        return 0;
    for (index = 0U; index < statement->control_count; ++index)
        if (statement->io_controls[index].kind == F2C_IO_CONTROL_IOLENGTH)
            return 1;
    return 0;
}

static void validate_io_item_semantics(Context *context, Unit *unit, const F2cStatement *statement,
                                       const F2cIoItem *item, int input, int unformatted,
                                       int namelist) {
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
            validate_io_item_semantics(context, unit, statement, &item->children[i], input,
                                       unformatted, namelist);
        return;
    }
    if (input && item->expression != NULL && !item->expression->definable) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, item->expression),
                          1, "READ item must be definable");
    }
    if (iolength_inquiry(statement) && item->expression != NULL &&
        item->expression->type == TYPE_DERIVED && item->expression->derived_type != NULL) {
        if (derived_has_io_binding(item->expression->derived_type, 0, 1)) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &item->expression->span, 1,
                "INQUIRE(IOLENGTH=) output item of derived type '%s' requires defined "
                "unformatted I/O",
                item->expression->derived_type->name);
        } else if (derived_requires_defined_io(item->expression->derived_type)) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, &item->expression->span, 1,
                "INQUIRE(IOLENGTH=) output item of derived type '%s' has pointer, "
                "allocatable, or procedure-pointer subcomponents",
                item->expression->derived_type->name);
        }
    } else if (!namelist && item->expression != NULL && item->expression->type == TYPE_DERIVED &&
               item->expression->derived_type != NULL &&
               !derived_has_io_binding(item->expression->derived_type, input, unformatted) &&
               derived_requires_defined_io(item->expression->derived_type)) {
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, &item->expression->span, 1,
            "%s %s of derived type '%s' with dynamic components requires defined I/O",
            unformatted ? "unformatted" : "formatted", io_statement_name(statement->kind),
            item->expression->derived_type->name);
    }
}

void f2c_validation_io_statement(Context *context, Unit *unit, F2cStatement *statement) {
    unsigned char seen[F2C_IO_CONTROL_READWRITE + 1U] = {0};
    size_t positional_count = 0U;
    size_t i;
    int saw_keyword = 0;
    if (!statement->io_syntax_valid) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "malformed %s control or item-list syntax",
                                 io_statement_name(statement->kind));
        return;
    }
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
    if (statement->kind == F2C_STMT_INQUIRE && seen[F2C_IO_CONTROL_IOLENGTH]) {
        if (seen[F2C_IO_CONTROL_UNIT] || seen[F2C_IO_CONTROL_FILE] ||
            statement->control_count != 1U) {
            f2c_diagnostic_at(context, statement->line, 1U, 1,
                              "INQUIRE(IOLENGTH=) cannot contain other inquiry specifiers");
        }
        if (statement->io_item_count == 0U)
            f2c_diagnostic_at(context, statement->line, 1U, 1,
                              "INQUIRE(IOLENGTH=) requires a nonempty output list");
    } else if (statement->kind == F2C_STMT_INQUIRE &&
               seen[F2C_IO_CONTROL_UNIT] == seen[F2C_IO_CONTROL_FILE]) {
        f2c_diagnostic_at(context, statement->line, 1U, 1,
                          "INQUIRE requires exactly one of UNIT= or FILE=");
    } else if (statement->kind != F2C_STMT_INQUIRE && statement->kind != F2C_STMT_PRINT &&
               !seen[F2C_IO_CONTROL_UNIT]) {
        f2c_diagnostic_at(context, statement->line, 1U, 1,
                          "%s requires UNIT=", io_statement_name(statement->kind));
    }
    if (statement->kind == F2C_STMT_PRINT && !seen[F2C_IO_CONTROL_FMT])
        f2c_diagnostic_at(context, statement->line, 1U, 1, "PRINT requires a format specifier");
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
    validate_internal_file_relations(context, statement, seen);
    validate_file_control_relations(context, unit, statement, seen);
    validate_transfer_relations(context, unit, statement, seen);
    for (i = 0U; i < statement->io_item_count; ++i)
        validate_io_item_semantics(
            context, unit, statement, &statement->io_items[i], statement->kind == F2C_STMT_READ,
            !seen[F2C_IO_CONTROL_FMT] && !seen[F2C_IO_CONTROL_NML], seen[F2C_IO_CONTROL_NML]);
}
