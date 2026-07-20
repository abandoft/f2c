#include "codegen/statement/private.h"

#include "codegen/descriptor/private.h"

#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int index;
    for (index = 0; index < depth; ++index)
        f2c_buffer_append(output, "    ");
}

static const F2cExpr *actual_value(const F2cExpr *actual) {
    return actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U
               ? actual->children[0]
               : actual;
}

static const F2cExpr *intrinsic_actual(const F2cStatement *statement, const char *keyword,
                                       size_t position) {
    size_t positional = 0U;
    size_t argument;
    for (argument = 0U; statement != NULL && argument < statement->item_count; ++argument) {
        const F2cExpr *actual = statement->arguments[argument];
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (actual->text != NULL && strcmp(actual->text, keyword) == 0)
                return actual_value(actual);
        } else if (positional++ == position) {
            return actual_value(actual);
        }
    }
    return NULL;
}

static void emit_count(Buffer *output, const F2cDescriptorView *view, size_t identifier,
                       int depth) {
    size_t dimension;
    indent(output, depth);
    f2c_buffer_printf(output, "size_t f2c_random_count_%zu = 1U;\n", identifier);
    for (dimension = 0U; dimension < view->rank; ++dimension) {
        indent(output, depth);
        f2c_buffer_printf(output,
                          "if ((size_t)(%s) != 0U && f2c_random_count_%zu > "
                          "SIZE_MAX / (size_t)(%s)) abort();\n",
                          view->extent[dimension], identifier, view->extent[dimension]);
        indent(output, depth);
        f2c_buffer_printf(output, "f2c_random_count_%zu *= (size_t)(%s);\n", identifier,
                          view->extent[dimension]);
    }
}

static int materialize_array(Buffer *output, Unit *unit, const F2cExpr *array, F2cIntent intent,
                             size_t identifier, int depth, const char *operation) {
    F2cDescriptorView view = {0};
    Buffer prelude = {0};
    Buffer cleanup = {0};
    if (!f2c_descriptor_materialize_view(&prelude, &cleanup, unit, array, intent, identifier,
                                         depth + 1, &view))
        return 0;
    indent(output, depth);
    f2c_buffer_append(output, "{\n");
    f2c_buffer_append(output, prelude.data != NULL ? prelude.data : "");
    emit_count(output, &view, identifier, depth + 1);
    if (strcmp(operation, "number") == 0) {
        indent(output, depth + 1);
        f2c_buffer_printf(output,
                          "for (size_t f2c_random_index_%zu = 0U; f2c_random_index_%zu < "
                          "f2c_random_count_%zu; ++f2c_random_index_%zu) "
                          "F2C_RANDOM_NUMBER(&%s[f2c_random_index_%zu]);\n",
                          identifier, identifier, identifier, identifier, view.data, identifier);
    } else {
        indent(output, depth + 1);
        f2c_buffer_printf(output, "f2c_random_seed_%s(%s, f2c_random_count_%zu);\n", operation,
                          view.data, identifier);
    }
    f2c_buffer_append(output, cleanup.data != NULL ? cleanup.data : "");
    indent(output, depth);
    f2c_buffer_append(output, "}\n");
    free(prelude.data);
    free(cleanup.data);
    f2c_descriptor_view_free(&view);
    return 1;
}

static int emit_random_number(Buffer *output, Unit *unit, const F2cStatement *statement,
                              int depth) {
    const F2cExpr *harvest = intrinsic_actual(statement, "harvest", 0U);
    size_t identifier = f2c_statement_unit_index(unit, statement);
    int supported = 0;
    char *code;
    if (harvest == NULL)
        return 0;
    if (harvest->rank != 0U)
        return materialize_array(output, unit, harvest, F2C_INTENT_OUT, identifier, depth,
                                 "number");
    code = f2c_emit_expression_ast(unit, harvest, &supported);
    if (!supported || code == NULL) {
        free(code);
        return 0;
    }
    indent(output, depth);
    f2c_buffer_printf(output, "F2C_RANDOM_NUMBER(&(%s));\n", code);
    free(code);
    return 1;
}

static int emit_random_seed(Buffer *output, Unit *unit, const F2cStatement *statement, int depth) {
    const F2cExpr *size = intrinsic_actual(statement, "size", 0U);
    const F2cExpr *put = intrinsic_actual(statement, "put", 1U);
    const F2cExpr *get = intrinsic_actual(statement, "get", 2U);
    const size_t identifier = f2c_statement_unit_index(unit, statement);
    if (size != NULL) {
        int supported = 0;
        char *code = f2c_emit_expression_ast(unit, size, &supported);
        if (!supported || code == NULL) {
            free(code);
            return 0;
        }
        indent(output, depth);
        f2c_buffer_printf(output, "(%s) = (int32_t)F2C_RANDOM_SEED_WORDS;\n", code);
        free(code);
        return 1;
    }
    if (put != NULL)
        return materialize_array(output, unit, put, F2C_INTENT_IN, identifier, depth, "put");
    if (get != NULL)
        return materialize_array(output, unit, get, F2C_INTENT_OUT, identifier, depth, "get");
    indent(output, depth);
    f2c_buffer_append(output, "f2c_random_seed_reset();\n");
    return 1;
}

int f2c_emit_random_statement(Context *context, Unit *unit, const F2cStatement *statement,
                              int depth) {
    if (context == NULL || unit == NULL || statement == NULL || statement->kind != F2C_STMT_CALL)
        return 0;
    if (statement->intrinsic == F2C_INTRINSIC_RANDOM_NUMBER)
        return emit_random_number(&context->output, unit, statement, depth);
    if (statement->intrinsic == F2C_INTRINSIC_RANDOM_SEED)
        return emit_random_seed(&context->output, unit, statement, depth);
    return 0;
}
