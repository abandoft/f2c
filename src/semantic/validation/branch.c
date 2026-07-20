#include "semantic/validation/private.h"

#include <stdlib.h>
#include <string.h>

typedef struct BranchTarget {
    const char *label;
    size_t index;
    int branchable;
} BranchTarget;

typedef struct ConstructRange {
    size_t begin;
    size_t end;
    const char *region;
} ConstructRange;

static const F2cStatement *statement_body(const F2cStatement *statement) {
    return statement != NULL && statement->kind == F2C_STMT_LABEL && statement->nested != NULL
               ? statement->nested
               : statement;
}

static int is_branchable(const F2cStatement *statement) {
    if (statement == NULL)
        return 0;
    switch (statement->kind) {
    case F2C_STMT_INVALID:
    case F2C_STMT_EMPTY:
    case F2C_STMT_DECLARATION:
    case F2C_STMT_FORMAT:
    case F2C_STMT_DATA:
    case F2C_STMT_CASE:
    case F2C_STMT_ELSE:
    case F2C_STMT_ELSE_IF:
    case F2C_STMT_ELSEWHERE:
    case F2C_STMT_TYPE_GUARD:
    case F2C_STMT_END_IF:
    case F2C_STMT_END_DO:
    case F2C_STMT_END_SELECT:
    case F2C_STMT_END_BLOCK_SCOPE:
    case F2C_STMT_END_WHERE:
        return 0;
    default:
        return 1;
    }
}

static int is_construct_terminator(const F2cStatement *statement) {
    return statement != NULL &&
           (statement->kind == F2C_STMT_END_IF || statement->kind == F2C_STMT_END_DO ||
            statement->kind == F2C_STMT_END_SELECT || statement->kind == F2C_STMT_END_BLOCK_SCOPE ||
            statement->kind == F2C_STMT_END_WHERE);
}

static int contains_statement(const F2cStatement *root, const F2cStatement *statement) {
    return root == statement ||
           (root != NULL && root->nested != NULL && contains_statement(root->nested, statement));
}

static size_t owner_index(const Unit *unit, const F2cStatement *owner) {
    size_t index;
    if (owner == NULL)
        return SIZE_MAX;
    for (index = 0U; index < unit->statement_count; ++index) {
        if (contains_statement(&unit->statements[index], owner))
            return index;
    }
    return SIZE_MAX;
}

static const char *construct_region(const F2cStatement *opener) {
    if (opener->kind == F2C_STMT_DO || opener->kind == F2C_STMT_DO_WHILE)
        return "DO construct";
    if (opener->kind == F2C_STMT_IF)
        return "IF construct";
    if (opener->kind == F2C_STMT_SELECT_CASE)
        return "SELECT CASE construct";
    if (opener->kind == F2C_STMT_SELECT_TYPE)
        return "SELECT TYPE construct";
    if (opener->kind == F2C_STMT_WHERE)
        return "WHERE construct";
    return "structured construct";
}

static const char *block_region(const F2cStatement *opener) {
    if (opener->kind == F2C_STMT_IF)
        return "branch block of an IF construct";
    if (opener->kind == F2C_STMT_SELECT_CASE)
        return "case block of a SELECT CASE construct";
    if (opener->kind == F2C_STMT_SELECT_TYPE)
        return "guard block of a SELECT TYPE construct";
    return "branch block of a WHERE construct";
}

static int is_block_boundary(const F2cStatement *statement, const F2cStatement *owner) {
    const F2cStatement *body = statement_body(statement);
    if (body == NULL || body->construct_owner != owner)
        return 0;
    if (owner->kind == F2C_STMT_IF)
        return body->kind == F2C_STMT_ELSE_IF || body->kind == F2C_STMT_ELSE;
    if (owner->kind == F2C_STMT_SELECT_CASE)
        return body->kind == F2C_STMT_CASE;
    if (owner->kind == F2C_STMT_SELECT_TYPE)
        return body->kind == F2C_STMT_TYPE_GUARD;
    if (owner->kind == F2C_STMT_WHERE)
        return body->kind == F2C_STMT_ELSEWHERE;
    return 0;
}

static void append_block_ranges(const Unit *unit, ConstructRange *ranges, size_t capacity,
                                size_t *range_count, const F2cStatement *owner, size_t begin,
                                size_t end) {
    size_t boundary_count = owner->kind == F2C_STMT_IF || owner->kind == F2C_STMT_WHERE ? 1U : 0U;
    size_t index;
    size_t start = SIZE_MAX;
    for (index = begin + 1U; index < end; ++index)
        if (is_block_boundary(&unit->statements[index], owner))
            ++boundary_count;
    if (boundary_count <= 1U)
        return;
    if (owner->kind == F2C_STMT_IF || owner->kind == F2C_STMT_WHERE)
        start = begin;
    for (index = begin + 1U; index <= end; ++index) {
        const int boundary = index < end && is_block_boundary(&unit->statements[index], owner);
        if (start == SIZE_MAX) {
            if (boundary)
                start = index;
            continue;
        }
        if ((boundary || index == end) && *range_count < capacity) {
            ranges[(*range_count)++] = (ConstructRange){start, index - 1U, block_region(owner)};
            start = boundary ? index : SIZE_MAX;
        }
    }
}

static const BranchTarget *find_target(const BranchTarget *targets, size_t target_count,
                                       const char *label) {
    size_t index;
    for (index = 0U; index < target_count; ++index)
        if (f2c_statement_labels_equal(targets[index].label, label))
            return &targets[index];
    return NULL;
}

static void validate_branch_target(Context *context, const BranchTarget *targets,
                                   size_t target_count, const ConstructRange *ranges,
                                   size_t range_count, size_t source_index, const char *label,
                                   const F2cSourceSpan *span, const char *role) {
    const BranchTarget *target = find_target(targets, target_count, label);
    size_t range;
    if (target == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, span, 1,
                                 "%s label %s is not defined in this program unit", role,
                                 label != NULL ? label : "<missing>");
        return;
    }
    if (!target->branchable) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, span, 1,
                                 "%s label %s does not identify an executable branch target", role,
                                 label);
        return;
    }
    for (range = 0U; range < range_count; ++range) {
        const ConstructRange *construct = &ranges[range];
        const int target_inside =
            target->index > construct->begin && target->index <= construct->end;
        const int source_inside = source_index > construct->begin && source_index <= construct->end;
        if (target_inside && !source_inside) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, span, 1,
                                     "%s label %s illegally enters a %s from outside its range",
                                     role, label, construct->region);
            return;
        }
    }
}

static void validate_assignment_label(Context *context, const BranchTarget *targets,
                                      size_t target_count, const F2cStatement *statement) {
    const BranchTarget *target;
    if (statement->label_count != 1U)
        return;
    target = find_target(targets, target_count, statement->labels[0]);
    if (target == NULL)
        f2c_diagnostic_span_code(
            context, F2C_DIAGNOSTIC_SEMANTIC,
            statement->label_spans != NULL ? &statement->label_spans[0] : &statement->span, 1,
            "ASSIGN label %s is not defined in this program unit", statement->labels[0]);
}

static void validate_statement_branches(Context *context, const Unit *unit,
                                        const BranchTarget *targets, size_t target_count,
                                        const ConstructRange *ranges, size_t range_count,
                                        const F2cStatement *statement, size_t source_index) {
    size_t label;
    if (statement == NULL)
        return;
    if (statement->kind == F2C_STMT_ASSIGN_LABEL) {
        validate_assignment_label(context, targets, target_count, statement);
    } else if (statement->kind == F2C_STMT_GOTO && statement->name != NULL) {
        validate_branch_target(
            context, targets, target_count, ranges, range_count, source_index, statement->name,
            statement->label_span.begin.line != 0U ? &statement->label_span : &statement->span,
            "GOTO target");
    } else if (statement->kind == F2C_STMT_GOTO || statement->kind == F2C_STMT_ARITHMETIC_IF ||
               (statement->kind == F2C_STMT_ASSIGNED_GOTO && statement->label_count != 0U)) {
        for (label = 0U; label < statement->label_count; ++label)
            validate_branch_target(
                context, targets, target_count, ranges, range_count, source_index,
                statement->labels[label],
                statement->label_spans != NULL ? &statement->label_spans[label] : &statement->span,
                statement->kind == F2C_STMT_ARITHMETIC_IF   ? "arithmetic IF target"
                : statement->kind == F2C_STMT_ASSIGNED_GOTO ? "assigned GOTO target"
                                                            : "computed GOTO target");
    } else if (statement->kind == F2C_STMT_CALL && statement->label_count != 0U) {
        for (label = 0U; label < statement->label_count; ++label)
            validate_branch_target(context, targets, target_count, ranges, range_count,
                                   source_index, statement->labels[label],
                                   statement->label_spans != NULL ? &statement->label_spans[label]
                                                                  : &statement->span,
                                   "alternate return target");
    } else if (statement->kind == F2C_STMT_ASSIGNED_GOTO && statement->name != NULL) {
        size_t assignment;
        size_t found = 0U;
        for (assignment = 0U; assignment < unit->statement_count; ++assignment) {
            const F2cStatement *candidate = statement_body(&unit->statements[assignment]);
            if (candidate == NULL || candidate->kind != F2C_STMT_ASSIGN_LABEL ||
                candidate->name == NULL || strcmp(candidate->name, statement->name) != 0 ||
                candidate->label_count != 1U)
                continue;
            ++found;
            validate_branch_target(context, targets, target_count, ranges, range_count,
                                   source_index, candidate->labels[0],
                                   candidate->label_spans != NULL ? &candidate->label_spans[0]
                                                                  : &statement->span,
                                   "assigned GOTO target");
        }
        if (found == 0U)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                     "assigned GOTO variable '%s' has no ASSIGN label source",
                                     statement->name);
    }
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_OPEN || statement->kind == F2C_STMT_REWIND ||
        statement->kind == F2C_STMT_BACKSPACE || statement->kind == F2C_STMT_ENDFILE ||
        statement->kind == F2C_STMT_INQUIRE || statement->kind == F2C_STMT_CLOSE) {
        for (label = 0U; label < statement->control_count; ++label) {
            const F2cIoControl *control = &statement->io_controls[label];
            if ((control->kind == F2C_IO_CONTROL_END || control->kind == F2C_IO_CONTROL_EOR ||
                 control->kind == F2C_IO_CONTROL_ERR) &&
                control->value != NULL && control->value->text != NULL)
                validate_branch_target(context, targets, target_count, ranges, range_count,
                                       source_index, control->value->text, &control->span,
                                       "I/O branch target");
        }
    }
    if (statement->nested != NULL)
        validate_statement_branches(context, unit, targets, target_count, ranges, range_count,
                                    statement->nested, source_index);
}

void f2c_validation_branches(Context *context, Unit *unit) {
    BranchTarget *targets;
    ConstructRange *ranges;
    size_t target_count = 0U;
    size_t range_count = 0U;
    size_t range_capacity;
    size_t index;
    if (context == NULL || unit == NULL || unit->statement_count == 0U)
        return;
    if (unit->statement_count > SIZE_MAX / 2U ||
        unit->statement_count * 2U > SIZE_MAX / sizeof(*ranges)) {
        f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                       "out of memory while validating statement-label control flow");
        return;
    }
    range_capacity = unit->statement_count * 2U;
    targets = (BranchTarget *)calloc(unit->statement_count, sizeof(*targets));
    ranges = (ConstructRange *)calloc(range_capacity, sizeof(*ranges));
    if (targets == NULL || ranges == NULL) {
        free(targets);
        free(ranges);
        f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                       "out of memory while validating statement-label control flow");
        return;
    }
    for (index = 0U; index < unit->statement_count; ++index) {
        F2cStatement *root = &unit->statements[index];
        const F2cStatement *body = statement_body(root);
        size_t previous;
        if ((root->kind == F2C_STMT_LABEL || root->kind == F2C_STMT_FORMAT) && root->name != NULL) {
            for (previous = 0U; previous < target_count; ++previous) {
                if (f2c_statement_labels_equal(targets[previous].label, root->name)) {
                    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &root->label_span, 1,
                                             "statement label %s is defined more than once",
                                             root->name);
                    break;
                }
            }
            targets[target_count++] = (BranchTarget){
                root->name, index, root->kind == F2C_STMT_LABEL && is_branchable(body)};
        }
        if (root->terminal_loop_count != 0U) {
            size_t loop;
            for (loop = 0U; loop < root->terminal_loop_count; ++loop) {
                const size_t begin = owner_index(unit, root->terminal_loops[loop]);
                if (begin != SIZE_MAX && range_count < range_capacity)
                    ranges[range_count++] = (ConstructRange){
                        begin, index, construct_region(root->terminal_loops[loop])};
            }
        } else if (is_construct_terminator(body) && body->construct_owner != NULL) {
            const size_t begin = owner_index(unit, body->construct_owner);
            if (begin != SIZE_MAX && range_count < range_capacity) {
                ranges[range_count++] =
                    (ConstructRange){begin, index, construct_region(body->construct_owner)};
                append_block_ranges(unit, ranges, range_capacity, &range_count,
                                    body->construct_owner, begin, index);
            }
        }
    }
    for (index = 0U; index < unit->statement_count; ++index)
        validate_statement_branches(context, unit, targets, target_count, ranges, range_count,
                                    &unit->statements[index], index);
    free(targets);
    free(ranges);
}
