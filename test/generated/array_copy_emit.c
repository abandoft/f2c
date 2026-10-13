#include "codegen/array/copy.h"

#include <stdio.h>
#include <stdlib.h>

static void emit_copy(Buffer *output, Unit *unit, unsigned int qualifiers) {
    f2c_array_copy_snapshot(output, unit, "take_target()", "take_source()", "take_count()",
                            "int64_t", (qualifiers & 1U) != 0U ? F2C_STORAGE_VOLATILE : 0U,
                            (qualifiers & 2U) != 0U ? F2C_STORAGE_VOLATILE : 0U, 3);
}

int main(void) {
    Buffer output = {0};
    Unit unit = {0};
    Symbol collisions[4] = {0};
    const char *const names[] = {"f2c_copy_count_0", "f2c_copy_target_0", "f2c_copy_source_0",
                                "f2c_copy_index_0"};
    for (size_t index = 0U; index < 4U; ++index) {
        collisions[index].name = (char *)names[index];
        collisions[index].c_name = (char *)names[index];
    }
    unit.symbols = collisions;
    unit.symbol_count = 4U;
    f2c_buffer_append(&output,
                      "#include <stdint.h>\n#include <stddef.h>\n#include <stdlib.h>\n"
                      "#include <string.h>\n#include <signal.h>\n"
                      "static const int64_t values[3] = {INT64_MAX, INT64_MIN, 17};\n"
                      "static int64_t copied[3];\n"
                      "static size_t length, source_calls, target_calls, count_calls;\n"
                      "static const void *take_source(void) { ++source_calls; return values; }\n"
                      "static void *take_target(void) { ++target_calls; return copied; }\n"
                      "static size_t take_count(void) { ++count_calls; return length; }\n"
                      "static void expected_abort(int number) {\n"
                      "    _Exit(number == SIGABRT && count_calls == 1U && source_calls == 0U &&\n"
                      "          target_calls == 0U ? 99 : 98);\n}\n"
                      "int main(int argc, char **argv) {\n"
                      "    (void)argv;\n"
                      "    if (argc > 1) {\n"
                      "        length = SIZE_MAX;\n"
                      "        if (signal(SIGABRT, expected_abort) == SIG_ERR) return 1;\n");
    emit_copy(&output, &unit, 0U);
    f2c_buffer_append(&output,
                      "        return 1;\n    }\n"
                      "    for (unsigned int qualified = 0U; qualified < 4U; ++qualified) {\n"
                      "        for (unsigned int empty = 0U; empty < 2U; ++empty) {\n"
                      "            memset(copied, 0, sizeof(copied));\n"
                      "            source_calls = target_calls = count_calls = 0U;\n"
                      "            length = empty != 0U ? 0U : 3U;\n"
                      "            const int f2c_copy_count_0 = 11, f2c_copy_source_0 = 13;\n"
                      "            const int f2c_copy_target_0 = 17, f2c_copy_index_0 = 19;\n"
                      "            switch (qualified) {\n");
    for (unsigned int qualifiers = 0U; qualifiers < 4U; ++qualifiers) {
        f2c_buffer_printf(&output, "            case %uU:\n", qualifiers);
        emit_copy(&output, &unit, qualifiers);
        f2c_buffer_append(&output, "                break;\n");
    }
    f2c_buffer_append(&output,
                      "            default: return 1;\n            }\n"
                      "            if (f2c_copy_count_0 != 11 || f2c_copy_source_0 != 13 ||\n"
                      "                f2c_copy_target_0 != 17 || f2c_copy_index_0 != 19) return 1;\n"
                      "            if (count_calls != 1U || source_calls != (empty == 0U ? 1U : 0U) ||\n"
                      "                target_calls != (empty == 0U ? 1U : 0U)) return 1;\n"
                      "            for (size_t index = 0U; index < 3U; ++index)\n"
                      "                if (copied[index] != (empty == 0U ? values[index] : 0)) return 1;\n"
                      "        }\n    }\n    return 0;\n}\n");
    char *code = f2c_buffer_take(&output);
    if (code == NULL)
        return EXIT_FAILURE;
    const int status = fputs(code, stdout);
    free(code);
    return status < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
