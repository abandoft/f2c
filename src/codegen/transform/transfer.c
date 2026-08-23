#include "codegen/transform/private.h"

#include "codegen/value/private.h"

#include <stdlib.h>

static char *mold_element_size(Unit *unit, const F2cExpr *mold) {
    Buffer result = {0};
    if (mold == NULL || mold->type == TYPE_UNKNOWN ||
        (mold->type == TYPE_DERIVED && mold->derived_type == NULL))
        return NULL;
    if (mold->type == TYPE_CHARACTER)
        return f2c_character_length_expression(unit, mold);
    f2c_buffer_printf(&result, "sizeof(%s)", f2c_expression_c_type(mold));
    return f2c_buffer_take(&result);
}

static int emit_scalar_source(Context *context, Unit *unit, const F2cExpr *source, int depth,
                              int *destroy_source) {
    char *value;
    if (source->type == TYPE_DERIVED && source->derived_type != NULL) {
        const char *type = f2c_expression_c_type(source);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s f2c_transfer_source_value = {0};\n", type);
        if (!f2c_emit_derived_clone_expression(&context->output, unit, source,
                                               "f2c_transfer_source_value", "transfer_source", 0U,
                                               depth))
            return 0;
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "const void *f2c_transfer_source = &f2c_transfer_source_value;\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "const size_t f2c_transfer_source_bytes = "
                                            "sizeof(f2c_transfer_source_value);\n");
        *destroy_source = 1;
        return 1;
    }
    value = f2c_transform_emit_expression(unit, source);
    if (value == NULL)
        return 0;
    if (source->type == TYPE_CHARACTER) {
        char *pointer = f2c_character_source_pointer(unit, source, value);
        char *length = f2c_character_length_expression(unit, source);
        if (pointer == NULL || length == NULL) {
            free(pointer);
            free(length);
            free(value);
            return 0;
        }
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const char *f2c_transfer_source = %s;\n", pointer);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transfer_source_bytes = (size_t)(%s);\n", length);
        free(pointer);
        free(length);
    } else {
        const char *type = f2c_expression_c_type(source);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s f2c_transfer_source_value = (%s);\n", type, value);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "const void *f2c_transfer_source = &f2c_transfer_source_value;\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "const size_t f2c_transfer_source_bytes = "
                                            "sizeof(f2c_transfer_source_value);\n");
    }
    free(value);
    return 1;
}

static void emit_scalar_source_cleanup(Context *context, const F2cExpr *source, int destroy_source,
                                       int depth) {
    if (!destroy_source || source == NULL || source->derived_type == NULL)
        return;
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "f2c_destroy_%s(&f2c_transfer_source_value);\n",
                      source->derived_type->c_name);
}

static int complex_type(Type type) { return type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX; }

static int resolved_kind(Type type, int kind) { return kind != 0 ? kind : f2c_default_kind(type); }

static char *numeric_assignment_value(const F2cExpr *source, const Symbol *target,
                                      const char *value) {
    const int source_kind = resolved_kind(source->type, source->type_kind);
    const int target_kind = resolved_kind(target->type, target->kind);
    const char *target_type = f2c_symbol_c_type(target);
    Buffer result = {0};
    if (source->type == TYPE_LOGICAL && target->type == TYPE_LOGICAL) {
        f2c_buffer_printf(&result, "((%s)((%s) != 0))", target_type, value);
        return f2c_buffer_take(&result);
    }
    if (!f2c_type_is_numeric(source->type) || !f2c_type_is_numeric(target->type))
        return NULL;
    if (complex_type(target->type)) {
        const char *constructor = target_kind == 4   ? "f2c_make_c"
                                  : target_kind == 8 ? "f2c_make_z"
                                                     : "f2c_make_q";
        const char *component = target_kind == 4   ? "float"
                                : target_kind == 8 ? "double"
                                                   : "long double";
        if (!complex_type(source->type)) {
            f2c_buffer_printf(&result, "%s((%s)(%s), (%s)0)", constructor, component, value,
                              component);
        } else if (source_kind == target_kind) {
            f2c_buffer_append(&result, value);
        } else {
            const char *conversion = source_kind == 4 && target_kind == 8    ? "f2c_c_to_z"
                                     : source_kind == 4 && target_kind == 16 ? "f2c_c_to_q"
                                     : source_kind == 8 && target_kind == 4  ? "f2c_z_to_c"
                                     : source_kind == 8 && target_kind == 16 ? "f2c_z_to_q"
                                     : source_kind == 16 && target_kind == 4 ? "f2c_q_to_c"
                                     : source_kind == 16 && target_kind == 8 ? "f2c_q_to_z"
                                                                             : NULL;
            if (conversion == NULL)
                return NULL;
            f2c_buffer_printf(&result, "%s(%s)", conversion, value);
        }
        return f2c_buffer_take(&result);
    }
    if (complex_type(source->type)) {
        const char *component = source_kind == 4 ? "crealf" : source_kind == 8 ? "creal" : "creall";
        f2c_buffer_printf(&result, "((%s)%s(%s))", target_type, component, value);
    } else {
        f2c_buffer_printf(&result, "((%s)(%s))", target_type, value);
    }
    return f2c_buffer_take(&result);
}

static void emit_intrinsic_result_allocation(Context *context, const F2cExpr *call,
                                             const char *mold_size, int depth) {
    const char *type = f2c_expression_c_type(call);
    f2c_transform_indent(&context->output, depth);
    if (call->type == TYPE_CHARACTER) {
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transfer_result_element_bytes = (size_t)(%s);\n",
                          mold_size);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "const size_t f2c_transfer_result_bytes = f2c_size_multiply_checked("
                          "f2c_transform_result_count, f2c_transfer_result_element_bytes);\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "char *f2c_transfer_result = (char *)malloc("
                                            "f2c_transfer_result_bytes == 0U ? 1U : "
                                            "f2c_transfer_result_bytes);\n");
    } else {
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transfer_result_bytes = f2c_size_multiply_checked("
                          "f2c_transform_result_count, sizeof(%s));\n",
                          type);
        f2c_transform_indent(&context->output, depth);
        if (call->type == TYPE_DERIVED)
            f2c_buffer_printf(&context->output,
                              "%s *f2c_transfer_result = (%s *)calloc("
                              "f2c_transform_result_count == 0U ? 1U : "
                              "f2c_transform_result_count, sizeof(%s));\n",
                              type, type, type);
        else
            f2c_buffer_printf(&context->output,
                              "%s *f2c_transfer_result = (%s *)malloc("
                              "f2c_transfer_result_bytes == 0U ? sizeof(%s) : "
                              "f2c_transfer_result_bytes);\n",
                              type, type, type);
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_transfer_result == NULL) abort();\n");
}

static void emit_intrinsic_result_transfer(Context *context, const F2cExpr *call, int depth) {
    const char *type = f2c_expression_c_type(call);
    f2c_transform_indent(&context->output, depth);
    if (call->type == TYPE_DERIVED && call->derived_type != NULL) {
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "unsigned char *f2c_transfer_raw = (unsigned char *)calloc("
                          "f2c_transfer_result_bytes == 0U ? 1U : "
                          "f2c_transfer_result_bytes, 1U);\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "if (f2c_transfer_raw == NULL) abort();\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "f2c_transfer_copy(f2c_transfer_raw, f2c_transfer_result_bytes, "
                          "f2c_transfer_source, f2c_transfer_source_bytes);\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_transfer_index = 0U; f2c_transfer_index < "
                          "f2c_transform_result_count; ++f2c_transfer_index) f2c_clone_%s("
                          "&f2c_transfer_result[f2c_transfer_index], &((const %s *)(const void *)"
                          "f2c_transfer_raw)[f2c_transfer_index]);\n",
                          call->derived_type->c_name, type);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "free(f2c_transfer_raw);\n");
    } else {
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "f2c_transfer_copy(f2c_transfer_result, "
                                            "f2c_transfer_result_bytes, f2c_transfer_source, "
                                            "f2c_transfer_source_bytes);\n");
    }
}

static int emit_assignment_result(Context *context, Unit *unit, const Symbol *target,
                                  const F2cExpr *call, const F2cExpr *mold, int depth) {
    f2c_transform_emit_result_allocation(context, unit, target, mold, depth);
    f2c_transform_indent(&context->output, depth);
    if (target->type == TYPE_CHARACTER && call->type == TYPE_CHARACTER) {
        f2c_buffer_append(
            &context->output,
            "for (size_t f2c_transfer_index = 0U; f2c_transfer_index < "
            "f2c_transform_result_count; ++f2c_transfer_index) { "
            "char *f2c_transfer_destination = f2c_transform_result + "
            "f2c_transfer_index * f2c_transform_result_element_length; "
            "const char *f2c_transfer_value = f2c_transfer_result + "
            "f2c_transfer_index * f2c_transfer_result_element_bytes; "
            "size_t f2c_transfer_copy_bytes = "
            "f2c_transform_result_element_length < f2c_transfer_result_element_bytes ? "
            "f2c_transform_result_element_length : f2c_transfer_result_element_bytes; "
            "if (f2c_transfer_copy_bytes != 0U) memmove(f2c_transfer_destination, "
            "f2c_transfer_value, f2c_transfer_copy_bytes); "
            "if (f2c_transform_result_element_length > f2c_transfer_copy_bytes) "
            "memset(f2c_transfer_destination + f2c_transfer_copy_bytes, ' ', "
            "f2c_transform_result_element_length - f2c_transfer_copy_bytes); }\n");
        return 1;
    }
    if (target->type == TYPE_DERIVED && call->type == TYPE_DERIVED &&
        target->derived_type == call->derived_type) {
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_transfer_index = 0U; f2c_transfer_index < "
                          "f2c_transform_result_count; ++f2c_transfer_index) f2c_clone_%s("
                          "&f2c_transform_result[f2c_transfer_index], "
                          "&f2c_transfer_result[f2c_transfer_index]);\n",
                          target->derived_type->c_name);
        return 1;
    }
    {
        char *converted =
            numeric_assignment_value(call, target, "f2c_transfer_result[f2c_transfer_index]");
        if (converted == NULL)
            return 0;
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_transfer_index = 0U; f2c_transfer_index < "
                          "f2c_transform_result_count; ++f2c_transfer_index) "
                          "f2c_transform_result[f2c_transfer_index] = %s;\n",
                          converted);
        free(converted);
    }
    return 1;
}

static void emit_intrinsic_result_cleanup(Context *context, const F2cExpr *call, int depth) {
    f2c_transform_indent(&context->output, depth);
    if (call->type == TYPE_DERIVED && call->derived_type != NULL) {
        f2c_buffer_printf(&context->output,
                          "f2c_destroy_array_%s(f2c_transfer_result, "
                          "f2c_transform_result_count, 1U);\n",
                          call->derived_type->c_name);
        f2c_transform_indent(&context->output, depth);
    }
    f2c_buffer_append(&context->output, "free(f2c_transfer_result);\n");
}

int f2c_transform_emit_transfer(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                                size_t line, int depth) {
    const F2cExpr *source = f2c_transform_argument(call, "source", 0U);
    const F2cExpr *mold = f2c_transform_argument(call, "mold", 1U);
    const F2cExpr *size = f2c_transform_argument(call, "size", 2U);
    TransformArray source_array = {0};
    char *mold_size = mold_element_size(unit, mold);
    char *size_code = size != NULL ? f2c_transform_emit_expression(unit, size) : NULL;
    int destroy_scalar_source = 0;
    if (source == NULL || mold == NULL || mold_size == NULL || target->rank != 1U ||
        call->rank != 1U || (size != NULL && size_code == NULL) ||
        (size == NULL && mold->rank == 0U)) {
        f2c_diagnostic(context, line, 1,
                       "TRANSFER requires SOURCE/MOLD and a rank-one target when SIZE or an "
                       "array MOLD determines an array result");
        free(mold_size);
        free(size_code);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (source->rank == 0U) {
        if (!emit_scalar_source(context, unit, source, depth + 1, &destroy_scalar_source))
            goto unsupported;
    } else {
        if (!f2c_transform_array_view(unit, source, &source_array) ||
            !f2c_transform_materialize_array(context, unit, &source_array, "transfer_source",
                                             depth + 1))
            goto unsupported;
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const void *f2c_transfer_source = %s;\n",
                          source_array.pointer);
        f2c_transform_indent(&context->output, depth + 1);
        if (source->type == TYPE_CHARACTER)
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transfer_source_bytes = f2c_size_multiply_checked("
                              "(size_t)(%s), (size_t)(%s));\n",
                              source_array.count,
                              source_array.element_length != NULL ? source_array.element_length
                                                                  : "0U");
        else
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transfer_source_bytes = "
                              "f2c_size_multiply_checked((size_t)(%s), sizeof(%s));\n",
                              source_array.count, f2c_expression_c_type(source));
    }
    if (size != NULL) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const int64_t f2c_transfer_size = (int64_t)(%s);\n",
                          size_code);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "const size_t f2c_transform_result_extent_1 = "
                                            "f2c_transfer_extent(f2c_transfer_size);\n");
    } else {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transfer_mold_bytes = (size_t)(%s);\n", mold_size);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_append(
            &context->output,
            "if (f2c_transfer_mold_bytes == 0U && f2c_transfer_source_bytes != 0U) abort();\n");
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_append(
            &context->output,
            "const size_t f2c_transform_result_extent_1 = f2c_transfer_mold_bytes == 0U ? "
            "0U : f2c_transfer_source_bytes / f2c_transfer_mold_bytes + "
            "(f2c_transfer_source_bytes % f2c_transfer_mold_bytes != 0U ? 1U : 0U);\n");
    }
    f2c_transform_emit_result_count(context, 1U, depth + 1);
    emit_intrinsic_result_allocation(context, call, mold_size, depth + 1);
    emit_intrinsic_result_transfer(context, call, depth + 1);
    emit_scalar_source_cleanup(context, source, destroy_scalar_source, depth + 1);
    f2c_transform_emit_array_cleanup(context, &source_array, depth + 1);
    if (!emit_assignment_result(context, unit, target, call, mold, depth + 1))
        goto incompatible;
    emit_intrinsic_result_cleanup(context, call, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, 1U, depth + 1);
    f2c_transform_free_array(&source_array);
    free(mold_size);
    free(size_code);
    return 1;

incompatible:
    f2c_diagnostic(context, line, 1,
                   "TRANSFER result could not be assigned to the target element type");
    f2c_transform_free_array(&source_array);
    free(mold_size);
    free(size_code);
    return 1;

unsupported:
    f2c_diagnostic(context, line, 1, "TRANSFER SOURCE could not be materialized exactly once");
    f2c_transform_free_array(&source_array);
    free(mold_size);
    free(size_code);
    return 1;
}
