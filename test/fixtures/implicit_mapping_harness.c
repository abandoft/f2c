#include "generated_implicit_mapping.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    int32_t output[2] = {0, 0};
    char source[4] = {'F', '2', 'C', '!'};
    char character_output[4] = {0, 0, 0, 0};

    implicit_values(output);
    implicit_character(source, character_output, (size_t)4, (size_t)4);
    (void)printf("%d %d %d %.4s\n", (int)output[0], (int)output[1], (int)index_value(),
                 character_output);
    return output[0] == 7 && output[1] == -3 && index_value() == 11 &&
                   memcmp(character_output, source, sizeof(source)) == 0
               ? 0
               : 1;
}
