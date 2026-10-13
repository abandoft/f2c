#include "codegen/array/copy.h"

#include "codegen/array/private.h"
#include "codegen/names.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

void f2c_array_copy_snapshot(Buffer *output, Unit *unit, const char *target, const char *source,
                             const char *count, const char *element_c_type,
                             unsigned int target_qualifiers, unsigned int source_qualifiers,
                             int depth) {
    char *length = f2c_codegen_local_name(unit, "f2c_copy_count");
    char *destination = f2c_codegen_local_name(unit, "f2c_copy_target");
    char *origin = f2c_codegen_local_name(unit, "f2c_copy_source");
    char *index = f2c_codegen_local_name(unit, "f2c_copy_index");
    if (length == NULL || destination == NULL || origin == NULL || index == NULL) {
        free(length);
        free(destination);
        free(origin);
        free(index);
        output->failed = 1;
        return;
    }
    f2c_array_indent(output, depth);
    f2c_buffer_append(output, "{\n");
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output, "const size_t %s = (size_t)(%s);\n", length, count);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output, "if (%s > SIZE_MAX / sizeof(%s)) abort();\n", length,
                      element_c_type);
    f2c_array_indent(output, depth + 1);
    f2c_buffer_printf(output, "if (%s != 0U) {\n", length);
    f2c_array_indent(output, depth + 2);
    f2c_buffer_printf(output, "%s%s *const %s = (%s%s *)(%s);\n",
                      (target_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "",
                      element_c_type, destination,
                      (target_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "",
                      element_c_type, target);
    f2c_array_indent(output, depth + 2);
    f2c_buffer_printf(output, "const %s%s *const %s = (const %s%s *)(%s);\n",
                      (source_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "",
                      element_c_type, origin,
                      (source_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "",
                      element_c_type, source);
    f2c_array_indent(output, depth + 2);
    if (((target_qualifiers | source_qualifiers) & F2C_STORAGE_VOLATILE) != 0U) {
        f2c_buffer_printf(output,
                          "for (size_t %s = 0U; %s < %s; ++%s) %s[%s] = %s[%s];\n",
                          index, index, length, index, destination, index, origin, index);
    } else {
        f2c_buffer_printf(output, "memmove(%s, %s, %s * sizeof(%s));\n", destination, origin,
                          length, element_c_type);
    }
    f2c_array_indent(output, depth + 1);
    f2c_buffer_append(output, "}\n");
    f2c_array_indent(output, depth);
    f2c_buffer_append(output, "}\n");
    free(length);
    free(destination);
    free(origin);
    free(index);
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
    char *data = f2c_storage_symbol_data(unit, target);
    if (data == NULL) {
        output->failed = 1;
        return;
    }
    f2c_array_copy_snapshot(output, unit, data, source, count, f2c_symbol_c_type(target),
                            f2c_symbol_storage_qualifiers(target), F2C_STORAGE_UNQUALIFIED, depth);
    free(data);
}
