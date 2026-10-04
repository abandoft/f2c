#include "codegen/io/private.h"

#include "codegen/loop/control.h"
#include "codegen/statement/private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int f2c_io_implied_do_begin(Context *context, Unit *unit, const F2cIoItem *item, const char *status,
                            int formatted_status, int depth) {
    F2cPreparedStatementExpression controls[3] = {{0}};
    const F2cExpr *expressions[3] = {item->initial, item->limit, item->step};
    const char *roles[3] = {"start", "limit", "step"};
    const size_t identifier = context->generated_loop_count++;
    char *advance = NULL;
    char *store = NULL;
    Buffer first = {0};
    Buffer condition = {0};
    char *prefix = f2c_loop_local_prefix(unit, "f2c_io_do", identifier);
    int success = 0;
    if (identifier == SIZE_MAX || prefix == NULL || item->iterator == NULL ||
        f2c_integer_loop_suffix(item->iterator->type_kind) == NULL)
        goto cleanup;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    for (size_t index = 0U; index < 3U; ++index) {
        char role[64];
        (void)snprintf(role, sizeof(role), "io_do_%zu_%s", identifier, roles[index]);
        if (!f2c_prepare_statement_expression(context, unit, NULL, expressions[index], role,
                                              item->iterator->span.begin.line, depth + 1,
                                              &controls[index]))
            goto cleanup;
        if (controls[index].prelude.data != NULL)
            f2c_buffer_append(&context->output, controls[index].prelude.data);
        f2c_loop_emit_parameter(&context->output, prefix, identifier, roles[index],
                                item->iterator->type_kind, expressions[index], controls[index].code,
                                depth + 1);
    }
    f2c_loop_emit_state(&context->output, prefix, identifier, status, formatted_status, depth + 1);
    f2c_buffer_printf(&first, "%s_start_%zu", prefix, identifier);
    if (first.failed ||
        (store = f2c_loop_store_expression(unit, item->iterator, first.data)) == NULL ||
        (advance = f2c_loop_advance_expression(unit, item->iterator, prefix, identifier)) == NULL)
        goto cleanup;
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "%s;\n", store);
    /* Keep all bound results until initiation is complete. Their lifetimes do
     * not extend across the transfer body and no bound is evaluated per item. */
    for (size_t index = 0U; index < 3U; ++index)
        if (!f2c_array_cleanup_emit(&context->output, unit, &controls[index].cleanup))
            goto cleanup;
    if (status != NULL)
        f2c_buffer_printf(&condition, "%s %s", status,
                          formatted_status ? "> 0" : "== F2C_IO_STATUS_OK");
    if (condition.failed)
        goto cleanup;
    f2c_loop_emit_header(&context->output, prefix, identifier, advance, condition.data, depth + 1);
    success = !context->output.failed;

cleanup:
    for (size_t index = 0U; index < 3U; ++index)
        f2c_release_statement_expression(&controls[index]);
    free(advance);
    free(store);
    free(first.data);
    free(condition.data);
    free(prefix);
    if (!success)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL,
                                 item->iterator != NULL ? &item->iterator->span : NULL, 1,
                                 "I/O implied-DO control could not be lowered");
    return success;
}

void f2c_io_implied_do_end(Context *context, int depth) {
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}
