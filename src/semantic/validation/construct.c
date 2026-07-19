#include "semantic/validation/private.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConstructFrame {
    F2cStatement *opener;
    int has_branch;
    int has_else;
} ConstructFrame;

static int names_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL)
        return left == right;
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == *right;
}

static int opens_construct(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE ||
           statement->kind == F2C_STMT_SELECT_CASE || statement->kind == F2C_STMT_SELECT_TYPE ||
           statement->kind == F2C_STMT_BLOCK_SCOPE ||
           (statement->kind == F2C_STMT_WHERE && statement->block) ||
           (statement->kind == F2C_STMT_IF && statement->block);
}

static int closes_construct(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_END_IF || statement->kind == F2C_STMT_END_DO ||
           statement->kind == F2C_STMT_END_BLOCK_SCOPE || statement->kind == F2C_STMT_END_SELECT ||
           statement->kind == F2C_STMT_END_WHERE;
}

static int terminator_matches(F2cStatementKind opener, F2cStatementKind terminator) {
    if (terminator == F2C_STMT_END_SELECT)
        return opener == F2C_STMT_SELECT_CASE || opener == F2C_STMT_SELECT_TYPE;
    if (terminator == F2C_STMT_END_BLOCK_SCOPE)
        return opener == F2C_STMT_BLOCK_SCOPE;
    if (terminator == F2C_STMT_END_WHERE)
        return opener == F2C_STMT_WHERE;
    if (terminator == F2C_STMT_END_IF)
        return opener == F2C_STMT_IF;
    return terminator == F2C_STMT_END_DO && (opener == F2C_STMT_DO || opener == F2C_STMT_DO_WHILE);
}

static const char *construct_name(F2cStatementKind kind) {
    switch (kind) {
    case F2C_STMT_SELECT_CASE:
        return "SELECT CASE";
    case F2C_STMT_SELECT_TYPE:
        return "SELECT TYPE";
    case F2C_STMT_BLOCK_SCOPE:
        return "BLOCK";
    case F2C_STMT_WHERE:
        return "WHERE";
    case F2C_STMT_IF:
        return "IF";
    case F2C_STMT_DO:
    case F2C_STMT_DO_WHILE:
        return "DO";
    default:
        return "construct";
    }
}

static const char *terminator_name(F2cStatementKind kind) {
    switch (kind) {
    case F2C_STMT_END_SELECT:
        return "END SELECT";
    case F2C_STMT_END_BLOCK_SCOPE:
        return "END BLOCK";
    case F2C_STMT_END_IF:
        return "END IF";
    case F2C_STMT_END_DO:
        return "END DO";
    case F2C_STMT_END_WHERE:
        return "END WHERE";
    default:
        return "construct terminator";
    }
}

static void report_missing_terminator(Context *context, const F2cStatement *opener) {
    if (opener->terminal_label != NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &opener->terminal_label_span, 1,
                                 "DO construct is missing terminal statement label %s",
                                 opener->terminal_label);
        return;
    }
    const char *required =
        opener->kind == F2C_STMT_SELECT_CASE || opener->kind == F2C_STMT_SELECT_TYPE ? "END SELECT"
        : opener->kind == F2C_STMT_BLOCK_SCOPE                                       ? "END BLOCK"
        : opener->kind == F2C_STMT_IF                                                ? "END IF"
        : opener->kind == F2C_STMT_WHERE                                             ? "END WHERE"
                                                                                     : "END DO";
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &opener->span, 1,
                             "%s construct is missing %s", construct_name(opener->kind), required);
}

static size_t matching_frame(const ConstructFrame *frames, size_t depth,
                             F2cStatementKind terminator) {
    while (depth != 0U) {
        --depth;
        if (terminator_matches(frames[depth].opener->kind, terminator) &&
            !(terminator == F2C_STMT_END_DO && frames[depth].opener->terminal_label != NULL))
            return depth;
    }
    return SIZE_MAX;
}

static int is_loop(const F2cStatement *statement);

static int prohibited_terminal_action(F2cStatementKind kind, int shared) {
    if (kind == F2C_STMT_ARITHMETIC_IF || kind == F2C_STMT_CYCLE || kind == F2C_STMT_EXIT ||
        kind == F2C_STMT_GOTO || kind == F2C_STMT_ASSIGNED_GOTO || kind == F2C_STMT_RETURN ||
        kind == F2C_STMT_STOP)
        return 1;
    if (!shared && kind == F2C_STMT_CONTINUE)
        return 0; /* A labeled CONTINUE is the end of a block DO construct. */
    return kind == F2C_STMT_INVALID || kind == F2C_STMT_EMPTY || kind == F2C_STMT_DECLARATION ||
           kind == F2C_STMT_FORMAT || kind == F2C_STMT_DATA || kind == F2C_STMT_END_IF ||
           kind == F2C_STMT_END_SELECT || kind == F2C_STMT_END_WHERE ||
           kind == F2C_STMT_END_BLOCK_SCOPE || (shared && kind == F2C_STMT_END_DO);
}

static int bind_labeled_do_terminal(Context *context, ConstructFrame *frames, size_t *depth,
                                    F2cStatement *label_statement) {
    F2cStatement *action;
    size_t match = SIZE_MAX;
    size_t outer;
    size_t index;
    size_t count;
    if (label_statement->kind != F2C_STMT_LABEL || label_statement->name == NULL)
        return 0;
    for (index = *depth; index != 0U; --index) {
        F2cStatement *opener = frames[index - 1U].opener;
        if (is_loop(opener) && opener->terminal_label != NULL &&
            f2c_statement_labels_equal(opener->terminal_label, label_statement->name)) {
            match = index - 1U;
            break;
        }
    }
    if (match == SIZE_MAX)
        return 0;
    outer = match;
    while (outer != 0U) {
        F2cStatement *candidate = frames[outer - 1U].opener;
        if (!is_loop(candidate) || candidate->terminal_label == NULL ||
            !f2c_statement_labels_equal(candidate->terminal_label, label_statement->name))
            break;
        --outer;
    }
    for (index = *depth; index > match + 1U; --index)
        report_missing_terminator(context, frames[index - 1U].opener);
    count = match - outer + 1U;
    free(label_statement->terminal_loops);
    label_statement->terminal_loops =
        (F2cStatement **)calloc(count, sizeof(*label_statement->terminal_loops));
    label_statement->terminal_loop_count = 0U;
    if (label_statement->terminal_loops == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &label_statement->span, 1,
                                 "out of memory while binding labeled DO termination");
        *depth = outer;
        return 1;
    }
    for (index = match + 1U; index > outer; --index)
        label_statement->terminal_loops[label_statement->terminal_loop_count++] =
            frames[index - 1U].opener;
    action = label_statement->nested;
    if (action == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &label_statement->span, 1,
                                 "labeled DO terminal label %s requires a terminal statement",
                                 label_statement->name);
    } else if (opens_construct(action) || prohibited_terminal_action(action->kind, count > 1U)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &action->span, 1,
                                 "%s cannot terminate %s labeled DO construct%s",
                                 action->text != NULL ? action->text : "statement",
                                 count > 1U ? "shared" : "a", count > 1U ? "s" : "");
    }
    if (action != NULL)
        action->construct_owner = label_statement->terminal_loops[0];
    *depth = outer;
    return 1;
}

static int is_loop(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE;
}

static void validate_associated_name(Context *context, const F2cStatement *statement,
                                     const F2cStatement *owner, const char *role,
                                     int required_for_named_owner) {
    if (owner->construct_name != NULL && required_for_named_owner &&
        statement->construct_name == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "%s must specify construct name '%s'", role,
                                 owner->construct_name);
    } else if (statement->construct_name != NULL && owner->construct_name == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "%s names an unnamed construct", role);
    } else if (statement->construct_name != NULL &&
               !names_equal(statement->construct_name, owner->construct_name)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "%s construct name '%s' does not match '%s'", role,
                                 statement->construct_name, owner->construct_name);
    }
}

static void bind_branch(Context *context, ConstructFrame *frames, size_t depth,
                        F2cStatement *statement, F2cStatementKind owner_kind, const char *role) {
    if (depth == 0U || frames[depth - 1U].opener->kind != owner_kind) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                 "%s must be directly enclosed by %s", role,
                                 owner_kind == F2C_STMT_IF      ? "IF"
                                 : owner_kind == F2C_STMT_WHERE ? "WHERE"
                                                                : "SELECT TYPE");
        return;
    }
    statement->construct_owner = frames[depth - 1U].opener;
    validate_associated_name(context, statement, statement->construct_owner, role, 0);
}

static void bind_control_transfer(Context *context, ConstructFrame *frames, size_t depth,
                                  F2cStatement *statement) {
    size_t index = depth;
    F2cStatement *named_construct = NULL;
    while (index != 0U) {
        F2cStatement *candidate = frames[--index].opener;
        if (statement->control_name != NULL) {
            if (candidate->construct_name != NULL &&
                names_equal(candidate->construct_name, statement->control_name)) {
                named_construct = candidate;
                break;
            }
        } else if (is_loop(candidate)) {
            statement->control_target = candidate;
            return;
        }
    }
    if (statement->control_name == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                 "%s statement is not enclosed by a DO construct",
                                 statement->kind == F2C_STMT_CYCLE ? "CYCLE" : "EXIT");
    } else if (named_construct == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                 "%s names unknown construct '%s'",
                                 statement->kind == F2C_STMT_CYCLE ? "CYCLE" : "EXIT",
                                 statement->control_name);
    } else if (!is_loop(named_construct)) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                 "%s target '%s' is not a DO construct",
                                 statement->kind == F2C_STMT_CYCLE ? "CYCLE" : "EXIT",
                                 statement->control_name);
    } else {
        statement->control_target = named_construct;
    }
}

void f2c_validation_bind_constructs(Context *context, Unit *unit) {
    ConstructFrame *frames;
    size_t depth = 0U;
    size_t index;
    if (unit == NULL || unit->statement_count == 0U)
        return;
    if (unit->statement_count > SIZE_MAX / sizeof(*frames)) {
        f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                       "out of memory while binding statement constructs");
        return;
    }
    frames = (ConstructFrame *)calloc(unit->statement_count, sizeof(*frames));
    if (frames == NULL) {
        f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                       "out of memory while binding statement constructs");
        return;
    }
    for (index = 0U; index < unit->statement_count; ++index) {
        F2cStatement *statement = &unit->statements[index];
        statement->construct_owner = NULL;
        statement->control_target = NULL;
        free(statement->terminal_loops);
        statement->terminal_loops = NULL;
        statement->terminal_loop_count = 0U;
        if (bind_labeled_do_terminal(context, frames, &depth, statement))
            continue;
        if (statement->kind == F2C_STMT_LABEL && statement->nested != NULL) {
            statement = statement->nested;
            statement->construct_owner = NULL;
            statement->control_target = NULL;
        }
        if (statement->nested != NULL && (statement->nested->kind == F2C_STMT_CYCLE ||
                                          statement->nested->kind == F2C_STMT_EXIT)) {
            statement->nested->control_target = NULL;
            bind_control_transfer(context, frames, depth, statement->nested);
        }
        if (statement->kind == F2C_STMT_WHERE && !statement->block) {
            if (depth != 0U && frames[depth - 1U].opener->kind == F2C_STMT_WHERE)
                statement->construct_owner = frames[depth - 1U].opener;
            if (statement->nested != NULL)
                statement->nested->construct_owner = statement;
        }
        if (statement->kind == F2C_STMT_CASE) {
            if (depth != 0U && frames[depth - 1U].opener->kind == F2C_STMT_SELECT_CASE) {
                statement->construct_owner = frames[depth - 1U].opener;
                frames[depth - 1U].has_branch = 1;
                validate_associated_name(context, statement, statement->construct_owner, "CASE", 0);
            }
            continue;
        }
        if (statement->kind == F2C_STMT_TYPE_GUARD) {
            if (depth != 0U && frames[depth - 1U].opener->kind == F2C_STMT_SELECT_TYPE) {
                statement->construct_owner = frames[depth - 1U].opener;
                frames[depth - 1U].has_branch = 1;
                validate_associated_name(context, statement, statement->construct_owner,
                                         "TYPE/CLASS guard", 0);
            } else {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                    "TYPE/CLASS guard must be directly enclosed by SELECT TYPE");
            }
            continue;
        }
        if (statement->kind == F2C_STMT_ELSE_IF || statement->kind == F2C_STMT_ELSE) {
            bind_branch(context, frames, depth, statement, F2C_STMT_IF,
                        statement->kind == F2C_STMT_ELSE_IF ? "ELSE IF" : "ELSE");
            if (statement->construct_owner != NULL) {
                ConstructFrame *frame = &frames[depth - 1U];
                if (frame->has_else) {
                    f2c_diagnostic_span_code(
                        context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                        statement->kind == F2C_STMT_ELSE_IF
                            ? "ELSE IF cannot follow ELSE in the same IF construct"
                            : "IF construct cannot contain more than one ELSE statement");
                }
                if (statement->kind == F2C_STMT_ELSE)
                    frame->has_else = 1;
            }
            continue;
        }
        if (statement->kind == F2C_STMT_ELSEWHERE) {
            bind_branch(context, frames, depth, statement, F2C_STMT_WHERE, "ELSEWHERE");
            if (statement->construct_owner != NULL) {
                ConstructFrame *frame = &frames[depth - 1U];
                if (frame->has_else) {
                    f2c_diagnostic_span_code(
                        context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                        statement->expression != NULL
                            ? "masked ELSEWHERE cannot follow an unmasked ELSEWHERE"
                            : "WHERE construct cannot contain more than one unmasked ELSEWHERE");
                }
                if (statement->expression == NULL)
                    frame->has_else = 1;
            }
            continue;
        }
        if (statement->kind == F2C_STMT_CYCLE || statement->kind == F2C_STMT_EXIT) {
            bind_control_transfer(context, frames, depth, statement);
            continue;
        }
        if (closes_construct(statement)) {
            const size_t owner = matching_frame(frames, depth, statement->kind);
            size_t skipped;
            if (owner == SIZE_MAX) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                         "%s has no matching opening construct",
                                         terminator_name(statement->kind));
                continue;
            }
            for (skipped = depth; skipped > owner + 1U; --skipped)
                report_missing_terminator(context, frames[skipped - 1U].opener);
            statement->construct_owner = frames[owner].opener;
            validate_associated_name(context, statement, statement->construct_owner,
                                     terminator_name(statement->kind), 1);
            depth = owner;
            continue;
        }
        if (depth != 0U && frames[depth - 1U].opener->kind == F2C_STMT_SELECT_CASE &&
            !frames[depth - 1U].has_branch && statement->kind != F2C_STMT_EMPTY) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                     "SELECT CASE block must begin with CASE or END SELECT");
        }
        if (depth != 0U && frames[depth - 1U].opener->kind == F2C_STMT_WHERE) {
            F2cStatement *where = frames[depth - 1U].opener;
            if (statement->kind == F2C_STMT_ASSIGNMENT) {
                statement->construct_owner = where;
            } else if (statement->kind == F2C_STMT_WHERE) {
                statement->construct_owner = where;
            } else if (statement->kind != F2C_STMT_EMPTY) {
                f2c_diagnostic_span_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                    "WHERE body may contain only intrinsic assignment or nested WHERE constructs");
            }
        }
        if (opens_construct(statement)) {
            size_t active;
            if (statement->construct_name != NULL) {
                for (active = 0U; active < depth; ++active) {
                    const char *name = frames[active].opener->construct_name;
                    if (name != NULL && names_equal(name, statement->construct_name)) {
                        f2c_diagnostic_span_code(
                            context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                            "construct name '%s' duplicates an active construct name",
                            statement->construct_name);
                        break;
                    }
                }
            }
            frames[depth].opener = statement;
            frames[depth].has_branch = 0;
            frames[depth].has_else = 0;
            ++depth;
        }
    }
    while (depth != 0U)
        report_missing_terminator(context, frames[--depth].opener);
    free(frames);
}
