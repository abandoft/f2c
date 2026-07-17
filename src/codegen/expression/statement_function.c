#include "codegen/expression/private.h"

#include <stdlib.h>
#include <string.h>

static void release_partial_clone(F2cExpr *expression) { f2c_expr_free(expression); }

static F2cExpr *clone_expression(const F2cExpr *source) {
    F2cExpr *clone;
    size_t index;
    if (source == NULL)
        return NULL;
    clone = (F2cExpr *)calloc(1U, sizeof(*clone));
    if (clone == NULL)
        return NULL;
    *clone = *source;
    clone->text = NULL;
    clone->source = NULL;
    clone->lowered_c = NULL;
    clone->children = NULL;
    clone->child_count = 0U;
    clone->child_capacity = 0U;
    if (source->text != NULL && (clone->text = f2c_strdup(source->text)) == NULL)
        goto failed;
    if (source->source != NULL && (clone->source = f2c_strdup(source->source)) == NULL)
        goto failed;
    if (source->lowered_c != NULL && (clone->lowered_c = f2c_strdup(source->lowered_c)) == NULL)
        goto failed;
    if (source->child_count != 0U) {
        if (source->child_count > SIZE_MAX / sizeof(*clone->children))
            goto failed;
        clone->children = (F2cExpr **)calloc(source->child_count, sizeof(*clone->children));
        if (clone->children == NULL)
            goto failed;
        clone->child_capacity = source->child_count;
        for (index = 0U; index < source->child_count; ++index) {
            clone->children[index] = clone_expression(source->children[index]);
            if (clone->children[index] == NULL)
                goto failed;
            ++clone->child_count;
        }
    }
    return clone;

failed:
    release_partial_clone(clone);
    return NULL;
}

static size_t dummy_index(const Symbol *function, const F2cExpr *expression) {
    size_t index;
    if (expression == NULL || expression->kind != F2C_EXPR_NAME || expression->text == NULL)
        return SIZE_MAX;
    for (index = 0U; index < function->statement_function_argument_count; ++index) {
        if (strcmp(expression->text, function->statement_function_arguments[index]) == 0)
            return index;
    }
    return SIZE_MAX;
}

static int apply_substitutions(F2cExpr *expression, const Symbol *function, char *const *actuals) {
    const size_t dummy = dummy_index(function, expression);
    size_t index;
    if (dummy != SIZE_MAX) {
        char *replacement = f2c_strdup(actuals[dummy]);
        if (replacement == NULL)
            return 0;
        free(expression->lowered_c);
        expression->lowered_c = replacement;
    }
    for (index = 0U; index < expression->child_count; ++index) {
        if (!apply_substitutions(expression->children[index], function, actuals))
            return 0;
    }
    return 1;
}

static void free_actuals(char **actuals, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index)
        free(actuals[index]);
    free(actuals);
}

/* A nested statement-function expansion has no standalone C statement in
 * which to materialize another temporary.  Reusing an actual is safe only
 * when its tree cannot invoke user code.  Top-level calls always use explicit
 * per-call temporaries and therefore accept arbitrary scalar expressions. */
static int expression_is_reusable(const F2cExpr *expression) {
    size_t child;
    if (expression == NULL)
        return 0;
    if (expression->kind == F2C_EXPR_CALL &&
        (expression->symbol == NULL || expression->symbol->statement_function ||
         !f2c_is_intrinsic_name(expression->text)))
        return 0;
    for (child = 0U; child < expression->child_count; ++child)
        if (!expression_is_reusable(expression->children[child]))
            return 0;
    return 1;
}

static size_t statement_function_expansion_count(F2cExpr *expression) {
    size_t count = 0U;
    size_t child;
    Symbol *function;
    if (expression == NULL)
        return 0U;
    for (child = 0U; child < expression->child_count; ++child) {
        const size_t nested = statement_function_expansion_count(expression->children[child]);
        if (nested > SIZE_MAX - count)
            return SIZE_MAX;
        count += nested;
    }
    function = expression->kind == F2C_EXPR_CALL ? expression->symbol : NULL;
    if (function == NULL || !function->statement_function)
        return count;
    if (count == SIZE_MAX)
        return count;
    ++count;
    if (!function->statement_function_expanding &&
        function->statement_function_expression != NULL) {
        size_t nested;
        function->statement_function_expanding = 1;
        nested = statement_function_expansion_count(function->statement_function_expression);
        function->statement_function_expanding = 0;
        if (nested > SIZE_MAX - count)
            return SIZE_MAX;
        count += nested;
    }
    return count;
}

static int assign_nested_temporaries(F2cExpr *expression, size_t *next) {
    size_t child;
    Symbol *function;
    size_t nested;
    if (expression == NULL)
        return 1;
    for (child = 0U; child < expression->child_count; ++child)
        if (!assign_nested_temporaries(expression->children[child], next))
            return 0;
    function = expression->kind == F2C_EXPR_CALL ? expression->symbol : NULL;
    if (function == NULL || !function->statement_function)
        return 1;
    if (*next == SIZE_MAX)
        return 0;
    expression->statement_temporary_index = (*next)++;
    expression->statement_nested_temporary_begin = *next;
    nested = statement_function_expansion_count(function->statement_function_expression);
    if (nested == SIZE_MAX || nested > SIZE_MAX - *next)
        return 0;
    *next += nested;
    return 1;
}

char *f2c_expression_statement_function(Unit *unit, const F2cExpr *expression, int *supported) {
    Symbol *function = expression != NULL ? expression->symbol : NULL;
    char **actuals = NULL;
    char **substitutions = NULL;
    F2cExpr *body = NULL;
    char *code = NULL;
    char *converted = NULL;
    Buffer sequenced = {0};
    size_t index;
    if (function == NULL || !function->statement_function ||
        function->statement_function_expression == NULL || function->statement_function_expanding ||
        expression->child_count != function->statement_function_argument_count) {
        *supported = 0;
        return NULL;
    }
    if (expression->child_count != 0U) {
        actuals = (char **)calloc(expression->child_count, sizeof(*actuals));
        substitutions = (char **)calloc(expression->child_count, sizeof(*substitutions));
        if (actuals == NULL || substitutions == NULL) {
            free(actuals);
            free(substitutions);
            *supported = 0;
            return NULL;
        }
    }
    for (index = 0U; index < expression->child_count; ++index) {
        const F2cExpr *actual = expression->children[index];
        Symbol *dummy = f2c_find_symbol(unit, function->statement_function_arguments[index]);
        char *raw;
        if (actual == NULL || actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            *supported = 0;
            goto cleanup;
        }
        raw = f2c_expression_emit(unit, actual, supported);
        if (!*supported || raw == NULL)
            goto cleanup;
        actuals[index] =
            dummy != NULL ? f2c_emit_numeric_conversion(raw, actual->type, dummy->type) : raw;
        if (dummy != NULL)
            free(raw);
        if (actuals[index] == NULL) {
            *supported = 0;
            goto cleanup;
        }
        if (expression->statement_temporary_index != SIZE_MAX) {
            Buffer name = {0};
            f2c_buffer_printf(&name, "f2c_statement_argument_%zu_%zu",
                              expression->statement_temporary_index, index);
            substitutions[index] = f2c_buffer_take(&name);
        } else if (expression_is_reusable(actual)) {
            substitutions[index] = f2c_strdup(actuals[index]);
        }
        if (substitutions[index] == NULL) {
            *supported = 0;
            goto cleanup;
        }
    }
    body = clone_expression(function->statement_function_expression);
    if (body == NULL || !apply_substitutions(body, function, substitutions)) {
        *supported = 0;
        goto cleanup;
    }
    if (expression->statement_nested_temporary_begin != SIZE_MAX) {
        size_t next = expression->statement_nested_temporary_begin;
        if (!assign_nested_temporaries(body, &next)) {
            *supported = 0;
            goto cleanup;
        }
    }
    function->statement_function_expanding = 1;
    code = f2c_expression_emit(unit, body, supported);
    function->statement_function_expanding = 0;
    if (!*supported || code == NULL)
        goto cleanup;
    converted = f2c_emit_numeric_conversion(code, body->type, function->type);
    free(code);
    code = NULL;
    if (converted == NULL)
        *supported = 0;
    if (*supported && expression->statement_temporary_index != SIZE_MAX) {
        f2c_buffer_append(&sequenced, "(");
        for (index = 0U; index < expression->child_count; ++index)
            f2c_buffer_printf(&sequenced, "%s = %s, ", substitutions[index], actuals[index]);
        f2c_buffer_printf(&sequenced, "%s)", converted);
        free(converted);
        converted = f2c_buffer_take(&sequenced);
        if (converted == NULL)
            *supported = 0;
    }

cleanup:
    function->statement_function_expanding = 0;
    free(code);
    f2c_expr_free(body);
    free_actuals(actuals, expression != NULL ? expression->child_count : 0U);
    free_actuals(substitutions, expression != NULL ? expression->child_count : 0U);
    free(sequenced.data);
    return converted;
}
