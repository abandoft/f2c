#include "core/generated/private.h"

void f2c_emit_process_cpu_time_support(Buffer *output) {
    f2c_buffer_append(output, "static inline F2C_UNUSED float f2c_etime(volatile float *user, "
                              "volatile float *system_time) {\n"
                              "#if F2C_PROCESS_CPU_TIME\n"
                              "    struct rusage usage;\n"
                              "    if (getrusage(RUSAGE_SELF, &usage) == 0) {\n"
                              "        *user = (float)((double)usage.ru_utime.tv_sec + "
                              "(double)usage.ru_utime.tv_usec / 1000000.0);\n"
                              "        *system_time = (float)((double)usage.ru_stime.tv_sec + "
                              "(double)usage.ru_stime.tv_usec / 1000000.0);\n"
                              "        return *user + *system_time;\n"
                              "    }\n"
                              "#endif\n"
                              "    *user = -1.0f; *system_time = -1.0f; return -1.0f;\n"
                              "}\n");
}
