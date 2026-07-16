#include "f2c/f2c.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FUZZ_CAPACITY = 1024, FUZZ_ITERATIONS = 600 };

static uint32_t next_random(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void mutate(char *buffer, size_t *length, uint32_t *state) {
    static const char alphabet[] = " abcdefghijklmnopqrstuvwxyz0123456789()=,:+-*/&_'\".\n";
    const uint32_t operation = next_random(state) % 3U;
    if (operation == 0U && *length != 0U) {
        const size_t position = (size_t)(next_random(state) % (uint32_t)*length);
        buffer[position] = alphabet[next_random(state) % (sizeof(alphabet) - 1U)];
    } else if (operation == 1U && *length != 0U) {
        const size_t position = (size_t)(next_random(state) % (uint32_t)*length);
        memmove(buffer + position, buffer + position + 1U, *length - position - 1U);
        --*length;
    } else if (*length + 1U < FUZZ_CAPACITY) {
        const size_t position =
            *length == 0U ? 0U : (size_t)(next_random(state) % (uint32_t)(*length + 1U));
        memmove(buffer + position + 1U, buffer + position, *length - position);
        buffer[position] = alphabet[next_random(state) % (sizeof(alphabet) - 1U)];
        ++*length;
    }
    buffer[*length] = '\0';
}

int main(void) {
    static const char *const seeds[] = {
        "program p\ninteger :: i\ndo i=1,3\nprint *, i\nend do\nend program p\n",
        "subroutine s(a,n)\ninteger,intent(in)::n\nreal::a(n)\na(1:n)=a(n:1:-1)\nend\n",
        "double precision function f(x)\ndouble precision::x\nf=sqrt(abs(x))\nend\n",
        ("      subroutine fixed(a,n)\n      integer n\n      real a(n)\n     1a(1)=1.0\n      "
         "end\n")};
    F2cOptions options = {"fuzz.f90", F2C_SOURCE_FREE, 0};
    uint32_t state = UINT32_C(0x9e3779b9);
    size_t seed_index;
    for (seed_index = 0U; seed_index < sizeof(seeds) / sizeof(seeds[0]); ++seed_index) {
        int iteration;
        for (iteration = 0; iteration < FUZZ_ITERATIONS; ++iteration) {
            char input[FUZZ_CAPACITY];
            size_t length = strlen(seeds[seed_index]);
            unsigned int edits = 1U + next_random(&state) % 12U;
            unsigned int edit;
            F2cResult result;
            memcpy(input, seeds[seed_index], length + 1U);
            for (edit = 0U; edit < edits; ++edit)
                mutate(input, &length, &state);
            options.source_form = seed_index == 3U ? F2C_SOURCE_FIXED : F2C_SOURCE_FREE;
            result = f2c_transpile(input, length, &options);
            if (result.diagnostics == NULL ||
                (result.error_count == 0U && (result.code == NULL || result.header == NULL))) {
                fputs("f2c mutation fuzz invariant failed\n", stderr);
                f2c_result_free(&result);
                return EXIT_FAILURE;
            }
            f2c_result_free(&result);
        }
    }
    return EXIT_SUCCESS;
}
