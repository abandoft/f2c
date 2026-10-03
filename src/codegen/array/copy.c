#include "codegen/array/copy.h"

#include "codegen/array/private.h"
#include "codegen/names.h"

#include <stdlib.h>

void f2c_array_copy_snapshot(Buffer *output, Unit *unit, const char *target, const char *source,
                             const char *count, unsigned int storage_qualifiers, int depth) {
    f2c_array_indent(output, depth);
    if ((storage_qualifiers & F2C_STORAGE_VOLATILE) != 0U) {
        char *index = f2c_codegen_local_name(unit, "f2c_copy_index");
        if (index == NULL) {
            output->failed = 1;
            return;
        }
        f2c_buffer_printf(output,
                          "for (size_t %s = 0U; %s < (size_t)(%s); ++%s) "
                          "(%s)[%s] = (%s)[%s];\n",
                          index, index, count, index, target, index, source, index);
        free(index);
    } else {
        f2c_buffer_printf(output,
                          "if ((%s) != 0U) memmove(%s, %s, "
                          "(size_t)(%s) * sizeof(*(%s)));\n",
                          count, target, source, count, source);
    }
}
