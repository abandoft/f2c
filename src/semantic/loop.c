#include "internal/f2c.h"

#include <stdlib.h>

static int begins_block(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE ||
           statement->kind == F2C_STMT_SELECT_CASE || statement->kind == F2C_STMT_SELECT_TYPE ||
           statement->kind == F2C_STMT_BLOCK_SCOPE ||
           (statement->kind == F2C_STMT_WHERE && statement->block) ||
           (statement->kind == F2C_STMT_IF && statement->block);
}

static int begins_loop(const F2cStatement *statement) {
    return statement->kind == F2C_STMT_DO || statement->kind == F2C_STMT_DO_WHILE;
}

static F2cStatement *body(F2cStatement *statement) {
    return statement != NULL && statement->kind == F2C_STMT_LABEL && statement->nested != NULL
               ? statement->nested
               : statement;
}

static int prevents_unrolling(const F2cStatement *statement) {
    const int prevents = statement->kind == F2C_STMT_CYCLE || statement->kind == F2C_STMT_EXIT ||
                         statement->kind == F2C_STMT_GOTO ||
                         statement->kind == F2C_STMT_ASSIGNED_GOTO ||
                         statement->kind == F2C_STMT_ARITHMETIC_IF ||
                         statement->kind == F2C_STMT_RETURN || statement->kind == F2C_STMT_STOP;
    return prevents || (statement->nested != NULL && prevents_unrolling(statement->nested));
}

static const Symbol *named_object(const F2cExpr *expression) {
    expression = f2c_expr_value_source(expression);
    return expression != NULL && expression->kind == F2C_EXPR_NAME ? expression->symbol : NULL;
}

static int reads_other_column(const F2cExpr *expression, const F2cExpr *target,
                              const Symbol *index) {
    if (expression == NULL)
        return 0;
    if (expression->kind == F2C_EXPR_ARRAY_REFERENCE && expression->symbol == target->symbol &&
        expression->child_count == target->child_count &&
        named_object(expression->children[0]) == index) {
        for (size_t dimension = 1U; dimension < target->child_count; ++dimension) {
            const Symbol *left = named_object(target->children[dimension]);
            const Symbol *right = named_object(expression->children[dimension]);
            if (left != NULL && right != NULL && left != right)
                return 1;
        }
    }
    for (size_t child = 0U; child < expression->child_count; ++child)
        if (reads_other_column(expression->children[child], target, index))
            return 1;
    return 0;
}

/* This is a backend-cost hint, not a dependence or no-alias assertion. Only
 * direct first-dimension induction accesses qualify; reductions and rank-one
 * operations keep the established policy. No procedure spelling is inspected. */
static int is_column_update(const F2cStatement *statement, const F2cStatement *loop) {
    if (statement->nested != NULL)
        return is_column_update(statement->nested, loop);
    const F2cExpr *target = statement->left;
    const Symbol *index = named_object(loop->left);
    if (statement->kind != F2C_STMT_ASSIGNMENT || target == NULL || index == NULL ||
        target->kind != F2C_EXPR_ARRAY_REFERENCE || target->symbol == NULL ||
        target->symbol->rank < 2U || target->child_count != target->symbol->rank ||
        named_object(target->children[0]) != index)
        return 0;
    return reads_other_column(statement->right, target, index);
}

void f2c_analyze_loop_hints(Unit *unit) {
    size_t *blocks;
    size_t *loops;
    size_t block_count = 0U;
    size_t loop_count = 0U;
    if (unit->statement_count == 0U)
        return;
    blocks = (size_t *)calloc(unit->statement_count, sizeof(*blocks));
    loops = (size_t *)calloc(unit->statement_count, sizeof(*loops));
    if (blocks == NULL || loops == NULL) {
        free(blocks);
        free(loops);
        return; /* Missing an optional optimization hint cannot change semantics. */
    }
    for (size_t i = 0U; i < unit->statement_count; ++i) {
        F2cStatement *root = &unit->statements[i];
        F2cStatement *statement = body(root);
        if (prevents_unrolling(root))
            for (size_t active = 0U; active < loop_count; ++active)
                body(&unit->statements[loops[active]])->loop_hint = F2C_LOOP_HINT_NONE;
        if (begins_loop(statement)) {
            for (size_t ancestor = 0U; ancestor < loop_count; ++ancestor)
                body(&unit->statements[loops[ancestor]])->loop_hint = F2C_LOOP_HINT_NONE;
            statement->loop_hint =
                statement->kind == F2C_STMT_DO ? F2C_LOOP_HINT_UNROLL : F2C_LOOP_HINT_NONE;
            loops[loop_count++] = i;
        } else if (loop_count != 0U) {
            F2cStatement *loop = body(&unit->statements[loops[loop_count - 1U]]);
            if (loop->loop_hint != F2C_LOOP_HINT_NONE && is_column_update(body(statement), loop))
                loop->loop_hint = F2C_LOOP_HINT_COLUMN_UPDATE;
        }
        if (begins_block(statement)) {
            blocks[block_count++] = i;
        } else if (root->terminal_loop_count != 0U) {
            for (size_t terminal = 0U; terminal < root->terminal_loop_count; ++terminal) {
                F2cStatement *loop = root->terminal_loops[terminal];
                if (block_count != 0U && body(&unit->statements[blocks[block_count - 1U]]) == loop)
                    --block_count;
                if (loop_count != 0U && body(&unit->statements[loops[loop_count - 1U]]) == loop)
                    --loop_count;
            }
        } else if ((statement->kind == F2C_STMT_END_IF || statement->kind == F2C_STMT_END_DO ||
                    statement->kind == F2C_STMT_END_WHERE ||
                    statement->kind == F2C_STMT_END_BLOCK_SCOPE ||
                    statement->kind == F2C_STMT_END_SELECT) &&
                   block_count != 0U) {
            const size_t opener = blocks[--block_count];
            if (begins_loop(body(&unit->statements[opener])) && loop_count != 0U)
                --loop_count;
        }
    }
    free(blocks);
    free(loops);
}
