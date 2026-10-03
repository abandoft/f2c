#include "codegen/allocation/context.h"

#include "codegen/expression/private.h"
#include "codegen/lowering/private.h"

#include <stdlib.h>

static char *control_name(const F2cAllocationContext *allocation, const char *suffix) {
    Buffer name = {0};
    f2c_buffer_printf(&name, "%s_%s", allocation->name, suffix);
    return f2c_buffer_take(&name);
}

static char *emit_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *code = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(code);
        return NULL;
    }
    return code;
}

static int prepare_status(F2cAllocationContext *allocation, F2cExpr *status, int depth) {
    Unit *unit = allocation->unit;
    Buffer *output = &allocation->context->output;
    char *address = NULL;
    int supported = 0;
    if (status == NULL)
        return 1;
    if (!f2c_allocation_prepare_value(allocation, status, 1, depth))
        return 0;
    allocation->status_address = control_name(allocation, "stat");
    allocation->status_type = f2c_strdup(f2c_expression_c_type(status));
    if (allocation->status_address == NULL || allocation->status_type == NULL)
        return 0;
    if (status->symbol != NULL && status->symbol->equivalence_unaligned) {
        const char *suffix = f2c_unaligned_access_suffix(status->symbol);
        address = f2c_emit_unaligned_designator_address(unit, status, &supported);
        allocation->status_suffix = suffix != NULL ? f2c_strdup(suffix) : NULL;
        if (!supported || address == NULL || allocation->status_suffix == NULL)
            goto failed;
        f2c_array_indent(output, depth);
        f2c_buffer_printf(output, "unsigned char *const %s = %s;\n", allocation->status_address,
                          address);
    } else {
        address = f2c_expression_storage_designator(unit, status, &supported);
        if (!supported || address == NULL)
            goto failed;
        f2c_array_indent(output, depth);
        f2c_buffer_printf(output, "%s%s *const %s = &(%s);\n",
                          (f2c_lowering_storage_qualifiers(unit, status) & F2C_STORAGE_VOLATILE) !=
                                  0U
                              ? "volatile "
                              : "",
                          allocation->status_type, allocation->status_address, address);
    }
    if (f2c_expression_has_pointer_result(status)) {
        f2c_array_indent(output, depth);
        f2c_buffer_printf(output, "if (%s == NULL) abort();\n", allocation->status_address);
    }
    free(address);
    return allocation->status_address != NULL && allocation->status_type != NULL;
failed:
    free(address);
    return 0;
}

static int prepare_message(F2cAllocationContext *allocation, F2cExpr *message, int depth) {
    char *code = NULL;
    char *pointer = NULL;
    char *length = NULL;
    if (message == NULL)
        return 1;
    if (!f2c_allocation_prepare_value(allocation, message, 1, depth))
        return 0;
    code = emit_expression(allocation->unit, message);
    length = f2c_character_length_expression(allocation->unit, message);
    pointer = code != NULL ? f2c_character_source_pointer(allocation->unit, message, code) : NULL;
    allocation->message_pointer = control_name(allocation, "errmsg");
    allocation->message_length = control_name(allocation, "errmsg_length");
    allocation->message_qualifiers = f2c_lowering_storage_qualifiers(allocation->unit, message);
    const int prepared = code != NULL && length != NULL && pointer != NULL &&
                         allocation->message_pointer != NULL && allocation->message_length != NULL;
    if (prepared) {
        Buffer *output = &allocation->context->output;
        f2c_array_indent(output, depth);
        f2c_buffer_printf(
            output, "%schar *const %s = %s;\n",
            (allocation->message_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "",
            allocation->message_pointer, pointer);
        f2c_array_indent(output, depth);
        f2c_buffer_printf(output, "const size_t %s = (size_t)(%s);\n", allocation->message_length,
                          length);
        if (f2c_expression_has_pointer_result(message)) {
            f2c_array_indent(output, depth);
            f2c_buffer_printf(output, "if (%s == NULL) abort();\n", allocation->message_pointer);
        }
    }
    free(code);
    free(pointer);
    free(length);
    return prepared;
}

int f2c_allocation_controls_prepare(F2cAllocationContext *allocation, int depth) {
    if (!prepare_status(allocation, f2c_allocation_keyword(allocation, "stat"), depth) ||
        !prepare_message(allocation, f2c_allocation_keyword(allocation, "errmsg"), depth))
        return 0;
    if (allocation->character_length != NULL) {
        if (!f2c_allocation_prepare_value(allocation, allocation->character_length, 0, depth))
            return 0;
        char *length = emit_expression(allocation->unit, allocation->character_length);
        allocation->length_value = control_name(allocation, "character_length");
        if (length == NULL || allocation->length_value == NULL) {
            free(length);
            return 0;
        }
        f2c_array_indent(&allocation->context->output, depth);
        f2c_buffer_printf(&allocation->context->output, "const int64_t %s = (int64_t)(%s);\n",
                          allocation->length_value, length);
        free(length);
    }
    return 1;
}

void f2c_allocation_failure(F2cAllocationContext *allocation, const char *success,
                            const char *message, int depth) {
    Buffer *output = &allocation->context->output;
    f2c_array_indent(output, depth);
    f2c_buffer_printf(output, "if (!%s) {\n", success);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output, "%s_ok = false;\n", allocation->name);
    if (allocation->status_address == NULL) {
        f2c_array_indent(output, depth + 1);
        f2c_buffer_append(output, "abort();\n");
    } else if (allocation->message_pointer != NULL) {
        f2c_array_indent(output, depth + 1);
        if ((allocation->message_qualifiers & F2C_STORAGE_VOLATILE) != 0U) {
            f2c_buffer_printf(output,
                              "for (size_t %s_message_index = 0U; "
                              "%s_message_index < %s; ++%s_message_index)\n",
                              allocation->name, allocation->name, allocation->message_length,
                              allocation->name);
            f2c_array_indent(output, depth + 2);
            f2c_buffer_printf(output,
                              "%s[%s_message_index] = "
                              "%s_message_index < sizeof(\"%s\") - 1U ? "
                              "\"%s\"[%s_message_index] : ' ';\n",
                              allocation->message_pointer, allocation->name, allocation->name,
                              message, message, allocation->name);
        } else {
            f2c_buffer_printf(output, "f2c_store_message(%s, %s, \"%s\");\n",
                              allocation->message_pointer, allocation->message_length, message);
        }
    }
    f2c_array_indent(output, depth);
    f2c_buffer_append(output, "}\n");
}

int f2c_allocation_controls_finish(F2cAllocationContext *allocation, int depth) {
    if (allocation->status_address == NULL)
        return 1;
    Buffer *output = &allocation->context->output;
    f2c_array_indent(output, depth);
    if (allocation->status_suffix != NULL)
        f2c_buffer_printf(output, "f2c_unaligned_store_%s(%s, (%s)(%s_ok ? 0 : 1));\n",
                          allocation->status_suffix, allocation->status_address,
                          allocation->status_type, allocation->name);
    else
        f2c_buffer_printf(output, "*%s = (%s)(%s_ok ? 0 : 1);\n", allocation->status_address,
                          allocation->status_type, allocation->name);
    return !output->failed;
}
