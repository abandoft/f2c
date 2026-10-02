#include "codegen/call/private.h"

#include "codegen/array/private.h"
#include "codegen/lowering/private.h"

#include <stdlib.h>

static const F2cExpr *actual_value(const F2cExpr *actual) {
    return actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U
               ? actual->children[0]
               : actual;
}

static int substitute_actuals(Unit *caller, const Unit *scope, const F2cExpr *call,
                              F2cExpr **expression) {
    F2cExpr *node = *expression;
    size_t argument;
    size_t child;
    if (node == NULL)
        return 1;
    if (node->kind == F2C_EXPR_NAME && node->symbol != NULL && node->symbol->argument) {
        for (argument = 0U; argument < scope->argument_count; ++argument) {
            if (node->symbol == f2c_find_symbol((Unit *)scope, scope->arguments[argument])) {
                const F2cExpr *actual =
                    argument < call->child_count ? actual_value(call->children[argument]) : NULL;
                F2cExpr *replacement =
                    actual != NULL ? f2c_array_clone_expression(caller, actual) : NULL;
                if (replacement == NULL)
                    return 0;
                f2c_codegen_expression_free(caller, node);
                *expression = replacement;
                return 1;
            }
        }
    }
    for (child = 0U; child < node->child_count; ++child)
        if (!substitute_actuals(caller, scope, call, &node->children[child]))
            return 0;
    return 1;
}

char *f2c_call_result_character_length(Unit *unit, const F2cExpr *expression) {
    const Symbol *procedure = expression != NULL ? expression->symbol : NULL;
    const Unit *scope = procedure != NULL ? procedure->character_length_scope : NULL;
    F2cExpr *specialized;
    char *code;
    int supported = 0;
    if (procedure == NULL || scope == NULL || procedure->character_length_expression == NULL)
        return NULL;
    specialized = f2c_array_clone_expression(unit, procedure->character_length_expression);
    if (specialized == NULL || !substitute_actuals(unit, scope, expression, &specialized)) {
        f2c_codegen_expression_free(unit, specialized);
        return NULL;
    }
    code = f2c_emit_expression_ast(unit, specialized, &supported);
    f2c_codegen_expression_free(unit, specialized);
    if (!supported) {
        free(code);
        return NULL;
    }
    return code;
}
