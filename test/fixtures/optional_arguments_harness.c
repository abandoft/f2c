#include "generated_optional_arguments.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    int32_t output[10] = {0};
    size_t i;
    optional_matrix(output);
    for (i = 0U; i < 10U; ++i)
        (void)printf("%" PRId32 "%s", output[i], i + 1U < 10U ? " " : "\n");
    return 0;
}
