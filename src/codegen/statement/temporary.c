#include "codegen/statement/private.h"

#include "codegen/array/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *f2c_emit_statement_expression(Context *context, Unit *unit, const F2cExpr *expression,
                                    size_t line) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported || result == NULL) {
        free(result);
        f2c_diagnostic(context, line, 1,
                       "code generation does not support this typed statement expression");
        return f2c_strdup("0 /* unsupported typed statement expression */");
    }
    return result;
}

static size_t expression_identifier(const Unit *unit, const F2cStatement *statement,
                                    size_t source_line) {
    const size_t identifier = f2c_statement_unit_index(unit, statement);
    return identifier != SIZE_MAX ? identifier : source_line;
}

int f2c_prepare_statement_expression(Context *context, Unit *unit, const F2cStatement *statement,
                                     const F2cExpr *expression, const char *role,
                                     size_t source_line, int depth,
                                     F2cPreparedStatementExpression *prepared) {
    const size_t previous_errors = context != NULL ? context->result.error_count : 0U;
    size_t temporary = 0U;
    if (context == NULL || unit == NULL || expression == NULL || role == NULL || prepared == NULL)
        return 0;
    memset(prepared, 0, sizeof(*prepared));
    prepared->expression = expression;
    if (f2c_array_contains_unmaterialized_value(expression)) {
        prepared->owned_expression = f2c_array_clone_expression(expression);
        if (prepared->owned_expression == NULL ||
            !f2c_array_materialize_constructors(context, unit, prepared->owned_expression,
                                                expression_identifier(unit, statement, source_line),
                                                role, &temporary, &prepared->prelude,
                                                &prepared->cleanup, depth)) {
            if (context->result.error_count == previous_errors)
                f2c_diagnostic(context, source_line, 1,
                               "array value in %s expression could not be materialized", role);
            f2c_release_statement_expression(prepared);
            return 0;
        }
        prepared->expression = prepared->owned_expression;
        prepared->materialized = 1;
    }
    prepared->code =
        f2c_emit_statement_expression(context, unit, prepared->expression, source_line);
    if (prepared->code == NULL) {
        f2c_release_statement_expression(prepared);
        return 0;
    }
    return 1;
}

void f2c_release_statement_expression(F2cPreparedStatementExpression *prepared) {
    if (prepared == NULL)
        return;
    f2c_expr_free(prepared->owned_expression);
    free(prepared->code);
    free(prepared->prelude.data);
    free(prepared->cleanup.data);
    memset(prepared, 0, sizeof(*prepared));
}

static int materialized_else_if_owned_by(const F2cStatement *statement,
                                         const F2cStatement *opener) {
    if (statement == NULL)
        return 0;
    if (statement->kind == F2C_STMT_ELSE_IF && statement->construct_owner == opener &&
        f2c_array_contains_unmaterialized_value(statement->expression))
        return 1;
    return materialized_else_if_owned_by(statement->nested, opener);
}

size_t f2c_if_materialized_else_wrappers(const Unit *unit, const F2cStatement *terminator) {
    const F2cStatement *opener;
    size_t wrappers = 0U;
    size_t index;
    if (unit == NULL || terminator == NULL || terminator->kind != F2C_STMT_END_IF)
        return 0U;
    opener = terminator->construct_owner;
    if (opener == NULL || opener->kind != F2C_STMT_IF)
        return 0U;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *candidate = &unit->statements[index];
        if (candidate == terminator)
            break;
        if (materialized_else_if_owned_by(candidate, opener))
            ++wrappers;
    }
    return wrappers;
}
