#include "semantic/validation/private.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void validate_allocation_bound(Context *context, const F2cStatement *statement,
                                      const Symbol *symbol, const F2cExpr *bound,
                                      size_t dimension) {
    const F2cExpr *lower = NULL;
    const F2cExpr *upper = bound;
    if (bound != NULL && bound->kind == F2C_EXPR_ARRAY_SECTION && bound->child_count == 3U) {
        lower = bound->children[0];
        upper = bound->children[1];
        if (bound->children[2]->kind != F2C_EXPR_INVALID) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, bound->children[2]), 1,
                "ALLOCATE bound %zu for '%s' cannot have a stride", dimension + 1U, symbol->name);
        }
    }
    if (upper == NULL || upper->kind == F2C_EXPR_INVALID) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, bound), 1,
                          "ALLOCATE bound %zu for '%s' requires an upper bound", dimension + 1U,
                          symbol->name);
        return;
    }
    if (lower != NULL && lower->kind != F2C_EXPR_INVALID &&
        (lower->type != TYPE_INTEGER || lower->rank != 0U)) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, lower), 1,
                          "ALLOCATE lower bound %zu for '%s' must be a scalar INTEGER",
                          dimension + 1U, symbol->name);
    }
    if (upper->type != TYPE_INTEGER || upper->rank != 0U) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, upper), 1,
                          "ALLOCATE upper bound %zu for '%s' must be a scalar INTEGER",
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
    if (target->type == TYPE_DERIVED || value->type == TYPE_DERIVED)
        return target->type == TYPE_DERIVED && value->type == TYPE_DERIVED &&
               target->derived_type == value->derived_type;
    if (target->type == TYPE_CHARACTER || value->type == TYPE_CHARACTER)
        return target->type == TYPE_CHARACTER && value->type == TYPE_CHARACTER;
    return target->type == value->type &&
           (target->kind == 0 || value->type_kind == 0 || target->kind == value->type_kind);
}

void f2c_validation_allocation(Context *context, Unit *unit, F2cStatement *statement) {
    const int allocating = statement->kind == F2C_STMT_ALLOCATE;
    F2cExpr *source = allocating ? allocation_keyword_value(statement, "source") : NULL;
    F2cExpr *mold = allocating ? allocation_keyword_value(statement, "mold") : NULL;
    F2cExpr *errmsg = allocation_keyword_value(statement, "errmsg");
    F2cExpr *model = source != NULL ? source : mold;
    size_t target_count = 0U;
    size_t stat_count = 0U;
    size_t errmsg_count = 0U;
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
    if (allocating && statement->allocation_has_type_spec) {
        if (statement->allocation_type != TYPE_CHARACTER ||
            statement->allocation_character_length == NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_UNSUPPORTED,
                                     &statement->allocation_type_span, 1,
                                     "unsupported or malformed ALLOCATE type specification '%s'",
                                     statement->tail != NULL ? statement->tail : "");
        } else {
            f2c_validation_report_parse_error(context, statement->line, statement->text,
                                              statement->allocation_character_length,
                                              "ALLOCATE CHARACTER length");
            if (statement->allocation_character_length->type != TYPE_INTEGER ||
                statement->allocation_character_length->rank != 0U) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(
                                      statement->text, statement->allocation_character_length),
                                  1, "ALLOCATE CHARACTER length must be a scalar INTEGER");
            }
            f2c_validation_constructor(context, unit, statement->line, statement->text,
                                       statement->allocation_character_length);
            f2c_validation_expression_calls(context, unit, statement->line, statement->text,
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
                    f2c_diagnostic_at(
                        context, statement->line,
                        f2c_validation_expression_start_column(statement->text, argument), 1,
                        "duplicate STAT= in %s", allocating ? "ALLOCATE" : "DEALLOCATE");
                }
                if (value == NULL || value->type != TYPE_INTEGER || value->rank != 0U ||
                    !value->definable) {
                    f2c_diagnostic_at(
                        context, statement->line,
                        f2c_validation_expression_start_column(statement->text, argument), 1,
                        "STAT= in %s must be a definable scalar INTEGER",
                        allocating ? "ALLOCATE" : "DEALLOCATE");
                }
            } else if (strcmp(keyword, "errmsg") == 0) {
                ++errmsg_count;
                if (errmsg_count > 1U) {
                    f2c_diagnostic_at(
                        context, statement->line,
                        f2c_validation_expression_start_column(statement->text, argument), 1,
                        "duplicate ERRMSG= in %s", allocating ? "ALLOCATE" : "DEALLOCATE");
                }
                if (value == NULL || value->type != TYPE_CHARACTER || value->rank != 0U ||
                    !value->definable) {
                    f2c_diagnostic_at(
                        context, statement->line,
                        f2c_validation_expression_start_column(statement->text, argument), 1,
                        "ERRMSG= in %s must be a definable scalar CHARACTER",
                        allocating ? "ALLOCATE" : "DEALLOCATE");
                }
            } else if (allocating &&
                       (strcmp(keyword, "source") == 0 || strcmp(keyword, "mold") == 0)) {
                size_t *keyword_count =
                    strcmp(keyword, "source") == 0 ? &source_count : &mold_count;
                ++*keyword_count;
                if (*keyword_count > 1U) {
                    f2c_diagnostic_at(
                        context, statement->line,
                        f2c_validation_expression_start_column(statement->text, argument), 1,
                        "duplicate %s= in ALLOCATE", keyword);
                }
                if (value == NULL) {
                    f2c_diagnostic_at(
                        context, statement->line,
                        f2c_validation_expression_start_column(statement->text, argument), 1,
                        "%s= in ALLOCATE requires an expression", keyword);
                } else if (value->rank != 0U && value->kind != F2C_EXPR_NAME) {
                    f2c_diagnostic_at(
                        context, statement->line,
                        f2c_validation_expression_start_column(statement->text, value), 1,
                        "array %s= currently requires a whole named array", keyword);
                }
            } else {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, argument),
                                  1, "%s= is not yet supported in %s", keyword,
                                  allocating ? "ALLOCATE" : "DEALLOCATE");
            }
            continue;
        }
        ++target_count;
        symbol = target != NULL ? target->symbol : NULL;
        if (target == NULL || symbol == NULL ||
            (target->kind != F2C_EXPR_NAME && target->kind != F2C_EXPR_ARRAY_REFERENCE &&
             target->kind != F2C_EXPR_COMPONENT)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, target), 1,
                              "%s target must be an allocatable or pointer object",
                              allocating ? "ALLOCATE" : "DEALLOCATE");
            continue;
        }
        if (!allocating && target->kind != F2C_EXPR_NAME && target->kind != F2C_EXPR_COMPONENT) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, target), 1,
                              "DEALLOCATE target '%s' must be a whole allocatable or pointer "
                              "object",
                              symbol->name);
            continue;
        }
        if (!symbol->allocatable && !symbol->pointer) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, target), 1,
                              "%s target '%s' is neither ALLOCATABLE nor POINTER",
                              allocating ? "ALLOCATE" : "DEALLOCATE", symbol->name);
            continue;
        }
        if (allocating && symbol->rank != 0U && target->kind != F2C_EXPR_ARRAY_REFERENCE &&
            !(target->kind == F2C_EXPR_COMPONENT && target->child_count > 1U) && model == NULL) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, target), 1,
                              "ALLOCATE target '%s' requires %zu explicit bounds", symbol->name,
                              symbol->rank);
        }
        if (allocating && target->kind == F2C_EXPR_ARRAY_REFERENCE &&
            target->child_count != symbol->rank) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, target), 1,
                              "ALLOCATE target '%s' has %zu bounds but rank %zu", symbol->name,
                              target->child_count, symbol->rank);
        }
        if (allocating && target->kind == F2C_EXPR_COMPONENT && symbol->rank != 0U &&
            target->child_count != symbol->rank + 1U) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, target), 1,
                              "ALLOCATE component '%s' has %zu bounds but rank %zu", symbol->name,
                              target->child_count > 0U ? target->child_count - 1U : 0U,
                              symbol->rank);
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
                                  f2c_validation_expression_start_column(statement->text, model), 1,
                                  "%s= type is incompatible with ALLOCATE target '%s'",
                                  source != NULL ? "SOURCE" : "MOLD", symbol->name);
            }
            if (symbol->rank == 0U && model->rank != 0U) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, model), 1,
                                  "%s= rank %zu is incompatible with scalar target '%s'",
                                  source != NULL ? "SOURCE" : "MOLD", model->rank, symbol->name);
            } else if (symbol->rank != 0U && target->kind == F2C_EXPR_NAME &&
                       model->rank != symbol->rank) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, model), 1,
                                  "%s= must provide rank-%zu shape for target '%s' without "
                                  "explicit bounds",
                                  source != NULL ? "SOURCE" : "MOLD", symbol->rank, symbol->name);
            } else if (symbol->rank != 0U && target->kind == F2C_EXPR_ARRAY_REFERENCE &&
                       model->rank != 0U && model->rank != symbol->rank) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, model), 1,
                                  "%s= rank %zu does not conform to rank-%zu target '%s'",
                                  source != NULL ? "SOURCE" : "MOLD", model->rank, symbol->rank,
                                  symbol->name);
            }
        }
        if (allocating && symbol->deferred_character &&
            statement->allocation_character_length == NULL && model == NULL) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, target), 1,
                              "deferred-length CHARACTER target '%s' requires an explicit "
                              "CHARACTER length or SOURCE=/MOLD=",
                              symbol->name);
        }
        if (allocating && statement->allocation_character_length != NULL &&
            !symbol->deferred_character) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, target), 1,
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
    if (errmsg != NULL && stat_count == 0U) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, errmsg), 1,
                          "ERRMSG= in %s requires STAT=", allocating ? "ALLOCATE" : "DEALLOCATE");
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
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &actual->span, 1,
                                         "MOVE_ALLOC has no argument named '%s'",
                                         actual->text != NULL ? actual->text : "<unknown>");
                valid = 0;
                continue;
            }
        } else {
            if (saw_keyword) {
                f2c_diagnostic_at(context, statement->line,
                                  f2c_validation_expression_start_column(statement->text, actual),
                                  1,
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
                                  f2c_validation_expression_start_column(statement->text, actual),
                                  1, "MOVE_ALLOC accepts at most four arguments");
                valid = 0;
                continue;
            }
        }
        if (assigned[slot]) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, actual), 1,
                              "MOVE_ALLOC argument '%s' is associated more than once", names[slot]);
            valid = 0;
            continue;
        }
        assigned[slot] = 1U;
        ordered_arguments[slot] = actual;
        ordered_items[slot] = statement->items != NULL ? statement->items[i] : NULL;
        if (f2c_validation_actual_value(actual) == NULL ||
            f2c_validation_actual_value(actual)->kind == F2C_EXPR_ABSENT_ARGUMENT) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, actual), 1,
                              "MOVE_ALLOC argument '%s' cannot be omitted", names[slot]);
            valid = 0;
        }
    }
    for (i = 0U; i < 2U; ++i) {
        if (!assigned[i]) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->name_span, 1,
                                     "required MOVE_ALLOC argument '%s' has no actual argument",
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
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &statement->name_span,
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
                          f2c_validation_expression_start_column(statement->text, expression), 1,
                          "MOVE_ALLOC %s= must be a whole named allocatable object", role);
        return NULL;
    }
    if (!symbol->allocatable) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, expression), 1,
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

void f2c_validation_move_alloc(Context *context, Unit *unit, F2cStatement *statement) {
    const F2cExpr *from_expression;
    const F2cExpr *to_expression;
    const F2cExpr *status_expression;
    const F2cExpr *message_expression;
    Symbol *from;
    Symbol *to;
    if (!bind_move_alloc_arguments(context, statement))
        return;
    from_expression = f2c_validation_actual_value(statement->arguments[0]);
    to_expression = f2c_validation_actual_value(statement->arguments[1]);
    status_expression = f2c_validation_actual_value(statement->arguments[2]);
    message_expression = f2c_validation_actual_value(statement->arguments[3]);
    from = validate_move_alloc_object(context, statement, from_expression, "FROM");
    to = validate_move_alloc_object(context, statement, to_expression, "TO");
    if (from != NULL && to != NULL) {
        if (from == to) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, to_expression), 1,
                "MOVE_ALLOC FROM= and TO= must designate distinct objects");
        }
        if (from->type != to->type ||
            (from->kind_type != TYPE_UNKNOWN && to->kind_type != TYPE_UNKNOWN &&
             from->kind_type != to->kind_type)) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, to_expression), 1,
                "MOVE_ALLOC FROM= and TO= must have the same declared type and "
                "kind");
        }
        if (from->rank != to->rank) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, to_expression), 1,
                "MOVE_ALLOC FROM= rank %zu does not match TO= rank %zu", from->rank, to->rank);
        }
        if (from->type == TYPE_CHARACTER && to->type == TYPE_CHARACTER &&
            !same_move_alloc_character_length(unit, from, to)) {
            f2c_diagnostic_at(
                context, statement->line,
                f2c_validation_expression_start_column(statement->text, to_expression), 1,
                "MOVE_ALLOC CHARACTER objects must have compatible length type "
                "parameters");
        }
    }
    if (status_expression != NULL &&
        (status_expression->type != TYPE_INTEGER || status_expression->rank != 0U ||
         !status_expression->definable)) {
        f2c_diagnostic_at(
            context, statement->line,
            f2c_validation_expression_start_column(statement->text, status_expression), 1,
            "MOVE_ALLOC STAT= must be a definable scalar INTEGER");
    }
    if (message_expression != NULL &&
        (message_expression->type != TYPE_CHARACTER || message_expression->rank != 0U ||
         !message_expression->definable)) {
        f2c_diagnostic_at(
            context, statement->line,
            f2c_validation_expression_start_column(statement->text, message_expression), 1,
            "MOVE_ALLOC ERRMSG= must be a definable scalar CHARACTER");
    }
}
