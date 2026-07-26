#include "codegen/host/private.h"

#include "codegen/descriptor/private.h"

#include <stdlib.h>

typedef enum F2cHostDescriptorStyle {
    F2C_HOST_DESCRIPTOR_STATEMENT,
    F2C_HOST_DESCRIPTOR_EXPRESSION
} F2cHostDescriptorStyle;

static void emit_indent(Buffer *output, int depth) {
    int level;
    for (level = 0; level < depth; ++level)
        f2c_buffer_append(output, "    ");
}

static int emit_descriptor_initialization(Buffer *output, Unit *caller, const Symbol *actual,
                                          size_t identifier, F2cHostDescriptorStyle style,
                                          int depth) {
    const char *name = f2c_symbol_c_name(caller, actual);
    const char *c_type = f2c_symbol_c_type(actual);
    char *character_length =
        actual->type == TYPE_CHARACTER ? f2c_symbol_character_length(caller, actual) : NULL;
    size_t dimension;
    if (style == F2C_HOST_DESCRIPTOR_STATEMENT)
        emit_indent(output, depth);
    f2c_buffer_printf(output,
                      "%sf2c_host%s_descriptor_%zu = %s{.data = %s, "
                      ".deallocatable = ",
                      style == F2C_HOST_DESCRIPTOR_STATEMENT ? "f2c_descriptor " : "",
                      style == F2C_HOST_DESCRIPTOR_STATEMENT ? "_call" : "", identifier,
                      style == F2C_HOST_DESCRIPTOR_STATEMENT ? "" : "(f2c_descriptor)", name);
    if (actual->pointer)
        f2c_buffer_printf(output, "%s_deallocatable", name);
    else
        f2c_buffer_printf(output, "(%s != NULL)", name);
    f2c_buffer_printf(output, ", .element_size = sizeof(%s), .rank = %zuU", c_type, actual->rank);
    if (actual->rank != 0U) {
        f2c_buffer_append(output, ", .lower = {");
        for (dimension = 0U; dimension < actual->rank; ++dimension) {
            char *lower = f2c_symbol_dimension_lower(caller, actual, dimension);
            if (lower == NULL) {
                free(character_length);
                return 0;
            }
            f2c_buffer_printf(output, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ", lower);
            free(lower);
        }
        f2c_buffer_append(output, "}, .extent = {");
        for (dimension = 0U; dimension < actual->rank; ++dimension) {
            char *extent = f2c_symbol_dimension_extent(caller, actual, dimension);
            if (extent == NULL) {
                free(character_length);
                return 0;
            }
            f2c_buffer_printf(output, "%sf2c_descriptor_extent((size_t)(%s))",
                              dimension == 0U ? "" : ", ", extent);
            free(extent);
        }
        f2c_buffer_append(output, "}, .stride = {");
        for (dimension = 0U; dimension < actual->rank; ++dimension) {
            char *stride = f2c_descriptor_source_stride(caller, actual, dimension);
            if (stride == NULL) {
                free(character_length);
                return 0;
            }
            f2c_buffer_printf(output, "%s(ptrdiff_t)(%s)", dimension == 0U ? "" : ", ", stride);
            free(stride);
        }
        f2c_buffer_append(output, "}");
    }
    f2c_buffer_printf(output, ", .character_length = (size_t)(%s)}%s",
                      character_length != NULL ? character_length : "0U",
                      style == F2C_HOST_DESCRIPTOR_STATEMENT ? ";\n" : ", ");
    free(character_length);
    return !output->failed;
}

static int emit_descriptor_forward_sync(Buffer *output, Unit *caller, const Symbol *actual,
                                        const char *descriptor, F2cHostDescriptorStyle style,
                                        int descriptor_nonnull, int depth) {
    const char *name = f2c_symbol_c_name(caller, actual);
    const char *c_type = f2c_symbol_c_type(actual);
    char *character_length =
        actual->type == TYPE_CHARACTER ? f2c_symbol_character_length(caller, actual) : NULL;
    size_t dimension;
#define F2C_HOST_SYNC(format, ...)                                                                 \
    do {                                                                                           \
        if (style == F2C_HOST_DESCRIPTOR_STATEMENT)                                                \
            emit_indent(output, depth);                                                            \
        f2c_buffer_printf(output, format, __VA_ARGS__);                                            \
        f2c_buffer_append(output, style == F2C_HOST_DESCRIPTOR_STATEMENT ? ";\n" : ", ");          \
    } while (0)

    if (!descriptor_nonnull)
        F2C_HOST_SYNC("%s != NULL ? (void)0 : abort()", descriptor);
    F2C_HOST_SYNC("(%s)->data = %s", descriptor, name);
    if (actual->pointer)
        F2C_HOST_SYNC("(%s)->deallocatable = %s_deallocatable", descriptor, name);
    else
        F2C_HOST_SYNC("(%s)->deallocatable = (%s != NULL)", descriptor, name);
    F2C_HOST_SYNC("(%s)->element_size = sizeof(%s)", descriptor, c_type);
    F2C_HOST_SYNC("(%s)->rank = %zuU", descriptor, actual->rank);
    F2C_HOST_SYNC("(%s)->character_length = (size_t)(%s)", descriptor,
                  character_length != NULL ? character_length : "0U");
    for (dimension = 0U; dimension < actual->rank; ++dimension) {
        char *lower = f2c_symbol_dimension_lower(caller, actual, dimension);
        char *extent = f2c_symbol_dimension_extent(caller, actual, dimension);
        char *stride = f2c_descriptor_source_stride(caller, actual, dimension);
        if (lower == NULL || extent == NULL || stride == NULL) {
            free(lower);
            free(extent);
            free(stride);
            free(character_length);
            return 0;
        }
        F2C_HOST_SYNC("(%s)->lower[%zu] = (int64_t)(%s)", descriptor, dimension, lower);
        F2C_HOST_SYNC("(%s)->extent[%zu] = f2c_descriptor_extent((size_t)(%s))", descriptor,
                      dimension, extent);
        F2C_HOST_SYNC("(%s)->stride[%zu] = (ptrdiff_t)(%s)", descriptor, dimension, stride);
        free(lower);
        free(extent);
        free(stride);
    }
#undef F2C_HOST_SYNC
    free(character_length);
    return !output->failed;
}

static int emit_descriptor_writeback(Buffer *output, Unit *caller, const Symbol *actual,
                                     const char *descriptor, int descriptor_is_pointer,
                                     F2cHostDescriptorStyle style, int depth) {
    const char *name = f2c_symbol_c_name(caller, actual);
    const char *c_type = f2c_symbol_c_type(actual);
    const char *member_prefix = descriptor_is_pointer ? "(" : "";
    const char *member_operator = descriptor_is_pointer ? ")->" : ".";
    size_t dimension;
#define F2C_HOST_WRITEBACK(format, ...)                                                            \
    do {                                                                                           \
        if (style == F2C_HOST_DESCRIPTOR_STATEMENT)                                                \
            emit_indent(output, depth);                                                            \
        f2c_buffer_printf(output, format, __VA_ARGS__);                                            \
        f2c_buffer_append(output, style == F2C_HOST_DESCRIPTOR_STATEMENT ? ";\n" : ", ");          \
    } while (0)

    F2C_HOST_WRITEBACK("f2c_descriptor_bridge_valid(%s%s, %zuU, sizeof(%s)) ? "
                       "(void)0 : abort()",
                       descriptor_is_pointer ? "" : "&", descriptor, actual->rank, c_type);
    F2C_HOST_WRITEBACK("%s = (%s *)%s%s%sdata", name, c_type, member_prefix, descriptor,
                       member_operator);
    if (actual->pointer)
        F2C_HOST_WRITEBACK("%s_deallocatable = %s%s%sdeallocatable", name, member_prefix,
                           descriptor, member_operator);
    if (actual->deferred_character)
        F2C_HOST_WRITEBACK("f2c_char_len_%s = %s%s%scharacter_length", name, member_prefix,
                           descriptor, member_operator);
    for (dimension = 0U; dimension < actual->rank; ++dimension) {
        F2C_HOST_WRITEBACK("%s_lower_%zu = (int32_t)%s%s%slower[%zu]", name, dimension + 1U,
                           member_prefix, descriptor, member_operator, dimension);
        F2C_HOST_WRITEBACK("%s_extent_%zu = (int32_t)%s%s%sextent[%zu]", name, dimension + 1U,
                           member_prefix, descriptor, member_operator, dimension);
        if (actual->pointer || (actual->argument && f2c_symbol_uses_descriptor(actual)))
            F2C_HOST_WRITEBACK("%s_stride_%zu = %s%s%sstride[%zu]", name, dimension + 1U,
                               member_prefix, descriptor, member_operator, dimension);
    }
#undef F2C_HOST_WRITEBACK
    return !output->failed;
}

static int emit_descriptors(Buffer *setup, Buffer *cleanup, Unit *caller, const Unit *procedure,
                            size_t descriptor_begin, F2cHostDescriptorStyle style, int depth) {
    size_t capture;
    size_t descriptor_ordinal = 0U;
    if (setup == NULL || cleanup == NULL || caller == NULL)
        return 0;
    if (procedure == NULL || !procedure->internal)
        return 1;
    for (capture = 0U; capture < procedure->host_capture_count; ++capture) {
        const Symbol *actual = f2c_host_capture_actual(caller, procedure, capture, NULL);
        const size_t identifier = descriptor_begin + descriptor_ordinal;
        Buffer descriptor = {0};
        Buffer descriptor_pointer = {0};
        const int local = f2c_host_capture_is_local_descriptor(caller, actual);
        const int function_result =
            f2c_host_function_result_symbol(caller, actual) && actual->allocatable;
        if (!f2c_host_capture_needs_descriptor_lifecycle(actual))
            continue;
        if (local) {
            if (descriptor_begin == SIZE_MAX ||
                !emit_descriptor_initialization(setup, caller, actual, identifier, style, depth))
                return 0;
            f2c_buffer_printf(&descriptor, "f2c_host%s_descriptor_%zu",
                              style == F2C_HOST_DESCRIPTOR_STATEMENT ? "_call" : "", identifier);
            ++descriptor_ordinal;
        } else if (function_result) {
            f2c_buffer_append(&descriptor, "f2c_result_descriptor");
        } else {
            f2c_buffer_printf(&descriptor, "f2c_descriptor_%s", f2c_symbol_c_name(caller, actual));
        }
        if (descriptor.data == NULL)
            return 0;
        if (local || function_result)
            f2c_buffer_printf(&descriptor_pointer, "&%s", descriptor.data);
        else
            f2c_buffer_append(&descriptor_pointer, descriptor.data);
        if (descriptor_pointer.data == NULL ||
            (!local && !emit_descriptor_forward_sync(setup, caller, actual, descriptor_pointer.data,
                                                     style, function_result, depth)) ||
            !emit_descriptor_writeback(cleanup, caller, actual, descriptor.data,
                                       !local && !function_result, style, depth)) {
            free(descriptor.data);
            free(descriptor_pointer.data);
            return 0;
        }
        free(descriptor.data);
        free(descriptor_pointer.data);
    }
    return 1;
}

int f2c_emit_host_capture_statement_descriptors(Buffer *prelude, Buffer *postlude, Unit *caller,
                                                const Unit *procedure, int depth) {
    return emit_descriptors(prelude, postlude, caller, procedure, 0U, F2C_HOST_DESCRIPTOR_STATEMENT,
                            depth);
}

int f2c_emit_host_capture_expression_descriptors(Buffer *setup, Buffer *cleanup, Unit *caller,
                                                 const Unit *procedure, size_t descriptor_begin) {
    return emit_descriptors(setup, cleanup, caller, procedure, descriptor_begin,
                            F2C_HOST_DESCRIPTOR_EXPRESSION, 0);
}
