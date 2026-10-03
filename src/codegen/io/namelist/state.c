#include "codegen/io/namelist/state.h"

#include "codegen/io/private.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

int f2c_namelist_has_descriptor_state(const Symbol *symbol) {
    return f2c_ir_symbol_storage_reference(symbol).state_source == F2C_OBJECT_STATE_DESCRIPTOR;
}

static int snapshot_property(Context *context, Unit *unit, const F2cStorageReference *reference,
                             F2cObjectStateProperty property, const char *field, const char *type,
                             size_t dimension, size_t member, int depth) {
    char *value = f2c_storage_read_property(unit, reference, property, dimension);
    if (value == NULL)
        return 0;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s f2c_namelist_original_%s_%zu_%zu = %s;\n", type, field,
                      member, dimension + 1U, value);
    f2c_io_indent(&context->output, depth);
    if (property == F2C_OBJECT_CHARACTER_LENGTH)
        f2c_buffer_printf(&context->output,
                          "%s *f2c_namelist_target_%s_%zu = &f2c_namelist_original_%s_%zu_%zu;\n",
                          type, field, member, field, member, dimension + 1U);
    else
        f2c_buffer_printf(&context->output,
                          "%s *f2c_namelist_target_%s_%zu_%zu = "
                          "&f2c_namelist_original_%s_%zu_%zu;\n",
                          type, field, member, dimension + 1U, field, member, dimension + 1U);
    free(value);
    return !context->output.failed;
}

int f2c_namelist_emit_state_snapshot(Context *context, Unit *unit, const Symbol *symbol,
                                     size_t member, int depth) {
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(symbol);
    char *data = f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
    if (!f2c_namelist_has_descriptor_state(symbol) || data == NULL) {
        free(data);
        return 0;
    }
    /* These are owned I/O transaction snapshots, not process-entry caches.
     * No C typed double-pointer aliases a descriptor's void * data field. */
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "%s *f2c_namelist_original_storage_%zu = %s;\n",
                      f2c_symbol_c_type(symbol), member, data);
    free(data);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "%s **f2c_namelist_target_%zu = &f2c_namelist_original_storage_%zu;\n",
                      f2c_symbol_c_type(symbol), member, member);
    if (symbol->deferred_character &&
        !snapshot_property(context, unit, &reference, F2C_OBJECT_CHARACTER_LENGTH,
                           "character_length", "size_t", 0U, member, depth))
        return 0;
    for (size_t dimension = 0U; dimension < symbol->rank; ++dimension) {
        if (!snapshot_property(context, unit, &reference, F2C_OBJECT_LOWER, "lower", "int32_t",
                               dimension, member, depth) ||
            !snapshot_property(context, unit, &reference, F2C_OBJECT_EXTENT, "extent", "int32_t",
                               dimension, member, depth) ||
            (symbol->pointer &&
             !snapshot_property(context, unit, &reference, F2C_OBJECT_STRIDE, "stride", "ptrdiff_t",
                                dimension, member, depth)))
            return 0;
    }
    return !context->output.failed;
}

int f2c_namelist_emit_state_commit(Context *context, Unit *unit, const Symbol *symbol,
                                   size_t member, int depth) {
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(symbol);
    Buffer value = {0};
    if (!symbol->allocatable || !f2c_namelist_has_descriptor_state(symbol))
        return 0;
    f2c_buffer_printf(&value, "*f2c_namelist_target_%zu", member);
    int success = f2c_storage_emit_store(&context->output, unit, &reference, F2C_OBJECT_DATA, 0U,
                                         NULL, value.data, depth);
    free(f2c_buffer_take(&value));
    if (success && symbol->deferred_character) {
        f2c_buffer_printf(&value, "f2c_char_len_f2c_namelist_stage_%zu", member);
        success = f2c_storage_emit_store(&context->output, unit, &reference,
                                         F2C_OBJECT_CHARACTER_LENGTH, 0U, NULL, value.data, depth);
        free(f2c_buffer_take(&value));
    }
    for (size_t dimension = 0U; success && dimension < symbol->rank; ++dimension) {
        Buffer lower = {0};
        Buffer extent = {0};
        f2c_buffer_printf(&lower, "f2c_namelist_stage_%zu_lower_%zu", member, dimension + 1U);
        f2c_buffer_printf(&extent, "f2c_namelist_stage_%zu_extent_%zu", member, dimension + 1U);
        success = f2c_storage_emit_contiguous_dimension(
            &context->output, unit, &reference, dimension, NULL, lower.data, extent.data, depth);
        free(f2c_buffer_take(&lower));
        free(f2c_buffer_take(&extent));
    }
    return success;
}
