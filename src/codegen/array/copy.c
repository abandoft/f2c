#include "codegen/array/copy.h"

#include "codegen/array/private.h"
#include "codegen/names.h"

#include <stdlib.h>

void f2c_array_copy_snapshot(Buffer *output, Unit *unit, const char *target, const char *source,
                             const char *count, const char *element_c_type,
                             unsigned int target_qualifiers, unsigned int source_qualifiers,
                             int depth) {
    f2c_array_indent(output, depth);
    if (((target_qualifiers | source_qualifiers) & F2C_STORAGE_VOLATILE) != 0U) {
        char *index = f2c_codegen_local_name(unit, "f2c_copy_index");
        if (index == NULL) {
            output->failed = 1;
            return;
        }
        f2c_buffer_printf(output,
                          "for (size_t %s = 0U; %s < (size_t)(%s); ++%s) "
                          "((%s%s *)(%s))[%s] = ((const %s%s *)(%s))[%s];\n",
                          index, index, count, index,
                          (target_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "",
                          element_c_type, target, index,
                          (source_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "",
                          element_c_type, source, index);
        free(index);
    } else {
        f2c_buffer_printf(output,
                          "if ((%s) != 0U) memmove(%s, %s, "
                          "(size_t)(%s) * sizeof(*(%s)));\n",
                          count, target, source, count, source);
    }
}

void f2c_array_copy_to_symbol(Buffer *output, Unit *unit, Symbol *target, const char *source,
                              const char *count, int depth) {
    if (target->equivalence_unaligned && target->type != TYPE_CHARACTER) {
        char *index = f2c_codegen_local_name(unit, "f2c_copy_index");
        char *address =
            index != NULL ? f2c_emit_unaligned_linear_address(unit, target, index) : NULL;
        const char *suffix = f2c_unaligned_access_suffix(target);
        if (index == NULL || address == NULL || suffix == NULL) {
            free(index);
            free(address);
            output->failed = 1;
            return;
        }
        f2c_array_indent(output, depth);
        f2c_buffer_printf(output,
                          "for (size_t %s = 0U; %s < (size_t)(%s); ++%s) "
                          "f2c_unaligned_store_%s(%s, (%s)[%s]);\n",
                          index, index, count, index, suffix, address, source, index);
        free(index);
        free(address);
        return;
    }
    f2c_array_copy_snapshot(output, unit, f2c_symbol_c_name(unit, target), source, count,
                            f2c_symbol_c_type(target), f2c_symbol_storage_qualifiers(target),
                            F2C_STORAGE_UNQUALIFIED, depth);
}
