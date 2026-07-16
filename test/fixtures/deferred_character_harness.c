#include "generated_deferred_character.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    int32_t output[56] = {0};
    size_t i;
    deferred_character_matrix(output);
    for (i = 0U; i < 56U; ++i)
        (void)printf("%" PRId32 "%s", output[i], i + 1U < 56U ? " " : "\n");
    return 0;
}
