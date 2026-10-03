#include "codegen/allocation/context.h"

#include "codegen/lowering/private.h"
#include "codegen/names.h"
#include "codegen/statement/private.h"

#include <stdlib.h>
#include <string.h>

F2cExpr *f2c_allocation_keyword(const F2cAllocationContext *allocation, const char *name) {
    for (size_t index = 0U; index < allocation->statement->item_count; ++index) {
        F2cExpr *argument = allocation->arguments[index];
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            argument->text != NULL && strcmp(argument->text, name) == 0 &&
            argument->child_count == 1U)
            return argument->children[0];
    }
    return NULL;
}

int f2c_allocation_capture(F2cAllocationContext *allocation, F2cArrayCleanupList *cleanup,
                           int depth) {
    if (!f2c_result_retention_capture(&allocation->retention, cleanup, &allocation->context->output,
                                      depth))
        return 0;
    /* Capture transfers runtime release responsibility, not the AST nodes. */
    f2c_array_cleanup_clear(cleanup);
    return 1;
}

int f2c_allocation_prepare_value(F2cAllocationContext *allocation, F2cExpr *expression,
                                 int designator, int depth) {
    Buffer prelude = {0};
    F2cArrayCleanupList cleanup = {0};
    int prepared = 1;
    if (expression == NULL)
        return 1;
    if (designator && f2c_expression_has_pointer_result(expression))
        designator = 0;
    const size_t count = designator ? expression->child_count : 1U;
    for (size_t index = 0U; index < count && prepared; ++index) {
        F2cExpr *value = designator ? expression->children[index] : expression;
        prepared =
            f2c_array_materialize_constructors(allocation->context, allocation->unit, value,
                                               allocation->identifier, "allocation_control",
                                               &allocation->temporary, &prelude, &cleanup, depth) &&
            f2c_array_hoist_scalar_subexpressions(allocation->unit, value, allocation->identifier,
                                                  "allocation_control", &allocation->temporary,
                                                  &prelude, depth, 0);
    }
    if (prepared) {
        if (prelude.data != NULL)
            f2c_buffer_append(&allocation->context->output, prelude.data);
        prepared = f2c_allocation_capture(allocation, &cleanup, depth);
    }
    free(prelude.data);
    f2c_array_cleanup_clear(&cleanup);
    return prepared;
}

int f2c_allocation_context_begin(F2cAllocationContext *allocation, Context *context, Unit *unit,
                                 const F2cStatement *statement, int depth) {
    if (allocation == NULL || context == NULL || unit == NULL || statement == NULL ||
        (statement->item_count != 0U && statement->arguments == NULL) ||
        statement->item_count >= SIZE_MAX / sizeof(*allocation->arguments))
        return 0;
    memset(allocation, 0, sizeof(*allocation));
    allocation->context = context;
    allocation->unit = unit;
    allocation->statement = statement;
    allocation->outer_depth = depth;
    allocation->identifier = f2c_statement_unit_index(unit, statement);
    if (allocation->identifier == SIZE_MAX)
        allocation->identifier = statement->line;
    static const char *const suffixes[] = {
        "ok", "stat", "errmsg", "errmsg_length", "character_length", "message_index"};
    char *preferred = f2c_codegen_local_name(unit, "f2c_allocation_statement");
    allocation->name = preferred != NULL
                           ? f2c_codegen_local_family(unit, preferred, suffixes,
                                                      sizeof(suffixes) / sizeof(suffixes[0]))
                           : NULL;
    free(preferred);
    const size_t roots = statement->item_count + 1U;
    allocation->arguments = (F2cExpr **)calloc(roots, sizeof(*allocation->arguments));
    if (allocation->name == NULL || allocation->arguments == NULL)
        goto failed;
    for (size_t index = 0U; index < statement->item_count; ++index) {
        if (statement->arguments[index] == NULL)
            continue;
        allocation->arguments[index] =
            f2c_array_clone_expression(unit, statement->arguments[index]);
        if (allocation->arguments[index] == NULL)
            goto failed;
    }
    if (statement->allocation_character_length != NULL) {
        allocation->character_length =
            f2c_array_clone_expression(unit, statement->allocation_character_length);
        if (allocation->character_length == NULL)
            goto failed;
    }
    allocation->arguments[statement->item_count] = allocation->character_length;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    ++depth;
    if (!f2c_result_retention_begin_values(&allocation->retention, unit, allocation->arguments,
                                           roots, allocation->name, &context->output, depth))
        goto failed;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "bool %s_ok = true; (void)%s_ok;\n", allocation->name,
                      allocation->name);
    if (!f2c_allocation_controls_prepare(allocation, depth))
        goto failed;
    return 1;
failed:
    f2c_allocation_context_clear(allocation);
    return 0;
}

int f2c_allocation_context_finish(F2cAllocationContext *allocation) {
    Buffer *output = &allocation->context->output;
    const int depth = allocation->outer_depth + 1;
    if (!f2c_allocation_controls_finish(allocation, depth) ||
        !f2c_result_retention_release(&allocation->retention, output, depth))
        return 0;
    f2c_array_indent(output, allocation->outer_depth);
    f2c_buffer_append(output, "}\n");
    return !output->failed;
}

void f2c_allocation_context_clear(F2cAllocationContext *allocation) {
    if (allocation == NULL)
        return;
    if (allocation->arguments != NULL) {
        for (size_t index = 0U; index < allocation->statement->item_count; ++index)
            f2c_codegen_expression_free(allocation->unit, allocation->arguments[index]);
    }
    f2c_codegen_expression_free(allocation->unit, allocation->character_length);
    f2c_result_retention_clear(&allocation->retention);
    free(allocation->arguments);
    free(allocation->name);
    free(allocation->status_address);
    free(allocation->status_type);
    free(allocation->status_suffix);
    free(allocation->message_pointer);
    free(allocation->message_length);
    free(allocation->length_value);
    memset(allocation, 0, sizeof(*allocation));
}
