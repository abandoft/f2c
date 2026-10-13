#include "codegen/expression/private.h"

#include "codegen/descriptor/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

char *f2c_expression_emit(Unit *unit, const F2cExpr *expression, int *supported);

char *f2c_expression_associated_scalar_target(Unit *unit, const F2cExpr *target, int *supported) {
    const Symbol *symbol = target != NULL ? target->symbol : NULL;
    Buffer result = {0};
    char *code;
    if (symbol == NULL)
        return NULL;
    if (target->kind == F2C_EXPR_ARRAY_REFERENCE && target->rank == 0U) {
        code = f2c_expression_emit(unit, target, supported);
        if (!*supported || code == NULL) {
            free(code);
            return NULL;
        }
        f2c_buffer_printf(&result, "&(%s)", code);
        free(code);
        return f2c_buffer_take(&result);
    }
    if (target->kind == F2C_EXPR_SUBSTRING) {
        code = f2c_expression_emit(unit, target, supported);
        if (!*supported || code == NULL) {
            free(code);
            return NULL;
        }
        f2c_buffer_append(&result, code);
        free(code);
        return f2c_buffer_take(&result);
    }
    if (target->kind == F2C_EXPR_COMPONENT) {
        if (target->rank == 0U && target->symbol->rank != 0U && target->child_count > 1U) {
            code = f2c_expression_emit(unit, target, supported);
            if (!*supported || code == NULL) {
                free(code);
                return NULL;
            }
            f2c_buffer_printf(&result, "&(%s)", code);
            free(code);
            return f2c_buffer_take(&result);
        }
        const F2cStorageReference reference = f2c_ir_storage_reference(target);
        code = f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
        if (code == NULL)
            return NULL;
        f2c_buffer_printf(
            &result, "%s%s",
            symbol->pointer || symbol->allocatable || symbol->type == TYPE_CHARACTER ? "" : "&",
            code);
        free(code);
        return f2c_buffer_take(&result);
    }
    if (target->kind != F2C_EXPR_NAME)
        return NULL;
    code = f2c_storage_symbol_data(unit, symbol);
    if (code == NULL)
        return NULL;
    f2c_buffer_printf(&result, "%s%s",
                      symbol->pointer || symbol->allocatable || symbol->argument ||
                              symbol->type == TYPE_CHARACTER
                          ? ""
                          : "&",
                      code);
    free(code);
    return f2c_buffer_take(&result);
}

static void append_view(Buffer *output, const F2cDescriptorView *view) {
    f2c_buffer_printf(output, "&(f2c_descriptor){.%s = %s, .rank = %zuU, .extent = {",
                      f2c_descriptor_address_member(view->storage_qualifiers, 1), view->data,
                      view->rank);
    for (size_t dimension = 0U; dimension < view->rank; ++dimension)
        f2c_buffer_printf(output, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ",
                          view->extent[dimension]);
    f2c_buffer_append(output, "}, .byte_stride = {");
    for (size_t dimension = 0U; dimension < view->rank; ++dimension)
        f2c_buffer_printf(output, "%s(ptrdiff_t)(%s)", dimension == 0U ? "" : ", ",
                          view->byte_stride[dimension]);
    f2c_buffer_printf(output, "}, .character_length = (size_t)(%s)}",
                      view->character_length != NULL ? view->character_length : "0U");
}

char *f2c_expression_associated_array_target(Unit *unit, const F2cExpr *pointer,
                                             const F2cExpr *target, const char *pointer_storage,
                                             int *supported) {
    F2cDescriptorView pointer_view = {0};
    F2cDescriptorView target_view = {0};
    Buffer result = {0};
    (void)pointer_storage;
    if (pointer == NULL || target == NULL || pointer->rank == 0U || target->rank != pointer->rank ||
        !f2c_descriptor_view(unit, pointer, &pointer_view) ||
        !f2c_descriptor_view(unit, target, &target_view))
        goto failed;
    if (pointer->type == TYPE_CHARACTER) {
        if (pointer_view.character_length == NULL)
            pointer_view.character_length = f2c_character_length_expression(unit, pointer);
        if (target_view.character_length == NULL)
            target_view.character_length = f2c_character_length_expression(unit, target);
        if (pointer_view.character_length == NULL || target_view.character_length == NULL)
            goto failed;
    }
    f2c_buffer_append(&result, "f2c_descriptor_same_target(");
    append_view(&result, &pointer_view);
    f2c_buffer_append(&result, ", ");
    append_view(&result, &target_view);
    f2c_buffer_printf(&result, ", %s)", pointer->type == TYPE_CHARACTER ? "true" : "false");
    f2c_descriptor_view_free(&pointer_view);
    f2c_descriptor_view_free(&target_view);
    return f2c_buffer_take(&result);
failed:
    *supported = 0;
    f2c_descriptor_view_free(&pointer_view);
    f2c_descriptor_view_free(&target_view);
    free(result.data);
    return NULL;
}
