#include "frontend/private.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int statement_begins_block(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE ||
           statement->kind == F2C_STMT_SELECT_CASE || statement->kind == F2C_STMT_SELECT_TYPE ||
           statement->kind == F2C_STMT_BLOCK_SCOPE ||
           (statement->kind == F2C_STMT_WHERE && statement->block) ||
           (statement->kind == F2C_STMT_IF && statement->block);
}

static int statement_begins_loop(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE;
}

static void annotate_loop_hints(Unit *unit) {
    size_t *blocks;
    size_t *loops;
    size_t block_count = 0U;
    size_t loop_count = 0U;
    size_t i;
    if (unit->statement_count == 0U)
        return;
    blocks = (size_t *)calloc(unit->statement_count, sizeof(*blocks));
    loops = (size_t *)calloc(unit->statement_count, sizeof(*loops));
    if (blocks == NULL || loops == NULL) {
        free(blocks);
        free(loops);
        return;
    }
    for (i = 0U; i < unit->statement_count; ++i) {
        F2cStatement *statement = &unit->statements[i];
        if (statement_begins_loop(statement)) {
            size_t ancestor;
            for (ancestor = 0U; ancestor < loop_count; ++ancestor)
                unit->statements[loops[ancestor]].unroll_hint = 0;
            if (loop_count != 0U && statement->kind == F2C_STMT_DO) {
                unit->statements[loops[loop_count - 1U]].unroll_hint = 2;
                statement->unroll_hint = 0;
            } else {
                statement->unroll_hint = statement->kind == F2C_STMT_DO ? 1 : 0;
            }
            loops[loop_count++] = i;
        }
        if (statement_begins_block(statement)) {
            blocks[block_count++] = i;
        } else if ((statement->kind == F2C_STMT_END_IF || statement->kind == F2C_STMT_END_DO ||
                    statement->kind == F2C_STMT_END_WHERE ||
                    statement->kind == F2C_STMT_END_BLOCK_SCOPE ||
                    statement->kind == F2C_STMT_END_SELECT) &&
                   block_count != 0U) {
            const size_t opener = blocks[--block_count];
            if (statement_begins_loop(&unit->statements[opener]) && loop_count != 0U)
                --loop_count;
        }
    }
    free(blocks);
    free(loops);
}

static void annotate_block_scopes(Context *context, Unit *unit) {
    size_t *stack;
    size_t depth = 0U;
    size_t i;
    if (unit->statement_count == 0U)
        return;
    stack = (size_t *)calloc(unit->statement_count, sizeof(*stack));
    if (stack == NULL)
        return;
    for (i = 0U; i < unit->statement_count; ++i) {
        F2cStatement *statement = &unit->statements[i];
        if (statement->kind == F2C_STMT_BLOCK_SCOPE) {
            stack[depth++] = i;
        } else if (statement->kind == F2C_STMT_END_BLOCK_SCOPE) {
            size_t symbol_index;
            F2cStatement *opener;
            if (depth == 0U) {
                f2c_diagnostic(context, statement->line, 1,
                               "END BLOCK has no matching BLOCK construct");
                continue;
            }
            opener = &unit->statements[stack[--depth]];
            for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
                Symbol *symbol = &unit->symbols[symbol_index];
                if (symbol->declaration_line <= opener->line ||
                    symbol->declaration_line >= statement->line)
                    continue;
                if (symbol->scope_begin_line == 0U || opener->line > symbol->scope_begin_line) {
                    symbol->scope_begin_line = opener->line;
                    symbol->scope_end_line = statement->line;
                }
            }
        }
    }
    while (depth != 0U) {
        F2cStatement *opener = &unit->statements[stack[--depth]];
        f2c_diagnostic(context, opener->line, 1, "BLOCK construct is missing END BLOCK");
    }
    free(stack);
}

void f2c_build_statement_ir(Context *context, Unit *unit) {
    size_t i;
    Symbol **select_symbols = NULL;
    F2cDerivedType **select_declared_types = NULL;
    F2cStatementKind *select_kinds = NULL;
    size_t select_depth = 0U;
    unit->statement_count = unit->end > unit->begin + 1U ? unit->end - unit->begin - 1U : 0U;
    if (unit->statement_count != 0U) {
        unit->statements = (F2cStatement *)calloc(unit->statement_count, sizeof(*unit->statements));
        if (unit->statements == NULL) {
            f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                           "out of memory while building statement IR for '%s'", unit->name);
            unit->statement_count = 0U;
            return;
        }
        select_symbols = (Symbol **)calloc(unit->statement_count, sizeof(*select_symbols));
        select_declared_types =
            (F2cDerivedType **)calloc(unit->statement_count, sizeof(*select_declared_types));
        select_kinds = (F2cStatementKind *)calloc(unit->statement_count, sizeof(*select_kinds));
        if (select_symbols == NULL || select_declared_types == NULL || select_kinds == NULL) {
            free(select_symbols);
            free(select_declared_types);
            free(select_kinds);
            f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                           "out of memory while tracking SELECT TYPE scopes");
            return;
        }
        for (i = 0U; i < unit->statement_count; ++i) {
            Line *line = &context->lines.items[unit->begin + 1U + i];
            Line inactive = {0};
            const Line *syntax_line = line;
            if (!f2c_unit_line_is_active(unit, line)) {
                inactive.text = "";
                inactive.source_name = line->source_name;
                inactive.number = line->number;
                syntax_line = &inactive;
            }
            if (!f2c_parse_statement_tokens(unit, syntax_line, &unit->statements[i])) {
                f2c_diagnostic(context, line->number, 1,
                               "out of memory while parsing statement IR");
                break;
            }
            if (unit->statements[i].kind == F2C_STMT_SELECT_TYPE) {
                F2cExpr *selector = unit->statements[i].expression;
                Symbol *symbol =
                    selector != NULL && selector->kind == F2C_EXPR_NAME ? selector->symbol : NULL;
                select_kinds[select_depth] = F2C_STMT_SELECT_TYPE;
                if (symbol == NULL || !symbol->polymorphic || symbol->derived_type == NULL) {
                    f2c_diagnostic(context, line->number, 1,
                                   "SELECT TYPE selector must be a named polymorphic object");
                } else {
                    select_symbols[select_depth] = symbol;
                    select_declared_types[select_depth] = symbol->derived_type;
                }
                ++select_depth;
            } else if (unit->statements[i].kind == F2C_STMT_SELECT_CASE) {
                select_kinds[select_depth++] = F2C_STMT_SELECT_CASE;
            } else if (unit->statements[i].kind == F2C_STMT_TYPE_GUARD && select_depth != 0U &&
                       select_kinds[select_depth - 1U] == F2C_STMT_SELECT_TYPE &&
                       select_symbols[select_depth - 1U] != NULL) {
                Symbol *selector = select_symbols[select_depth - 1U];
                if (unit->statements[i].guard_type != NULL)
                    selector->derived_type = unit->statements[i].guard_type;
                else
                    selector->derived_type = select_declared_types[select_depth - 1U];
            } else if (unit->statements[i].kind == F2C_STMT_END_SELECT && select_depth != 0U) {
                --select_depth;
                if (select_kinds[select_depth] == F2C_STMT_SELECT_TYPE &&
                    select_symbols[select_depth] != NULL)
                    select_symbols[select_depth]->derived_type =
                        select_declared_types[select_depth];
            }
        }
        while (select_depth != 0U) {
            --select_depth;
            if (select_kinds[select_depth] == F2C_STMT_SELECT_TYPE &&
                select_symbols[select_depth] != NULL)
                select_symbols[select_depth]->derived_type = select_declared_types[select_depth];
        }
        free(select_symbols);
        free(select_declared_types);
        free(select_kinds);
        annotate_block_scopes(context, unit);
        annotate_loop_hints(unit);
    }
    f2c_validate_unit_expressions(context, unit);
    unit->phase = F2C_UNIT_TYPED_IR;
}
