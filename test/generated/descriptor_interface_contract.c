#include "descriptor-interface.h"
#include "descriptor-interface-second.h"

#include <stdio.h>
#include <stdlib.h>

/* Compile separately from the generated implementation. Including generated C
 * would conceal a disagreement between its ABI and the user-facing header. */
int main(void) {
    volatile int32_t values[] = {1, 90, 2, 90, 3};
    volatile char text[] = {'a', 'b', 'c', 'd', 'e', 'f'};
    f2c_descriptor target = {.volatile_data = values,
                             .storage_qualifiers = 1U,
                             .element_size = sizeof(int32_t),
                             .rank = 1U,
                             .lower = {-3},
                             .extent = {3},
                             .stride = {2}};
    f2c_descriptor alias = {.element_size = sizeof(int32_t), .rank = 1U};
    f2c_descriptor characters = {.volatile_data = text,
                                 .storage_qualifiers = 1U,
                                 .element_size = sizeof(char),
                                 .rank = 1U,
                                 .lower = {1},
                                 .extent = {2},
                                 .stride = {1},
                                 .character_length = 3U};
    int32_t total = 0;
    descriptor_add(&target);
    descriptor_read(&target, &total);
    if (total != 36 || values[0] != 11 || values[1] != 90 || values[2] != 12 ||
        values[3] != 90 || values[4] != 13)
        return EXIT_FAILURE;
    descriptor_alias(&alias, &target);
    if (alias.volatile_data != target.volatile_data || alias.storage_qualifiers != 1U ||
        alias.deallocatable || alias.extent[0] != 3 || alias.stride[0] != 2)
        return EXIT_FAILURE;
    descriptor_text(&characters);
    if (text[0] != 'o' || text[1] != 'k' || text[2] != ' ' || text[3] != 'o' ||
        text[4] != 'k' || text[5] != ' ')
        return EXIT_FAILURE;
    puts("generated descriptor interface contracts passed");
    return EXIT_SUCCESS;
}
