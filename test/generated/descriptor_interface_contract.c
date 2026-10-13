#include "descriptor-byte-views.h"
#include "descriptor-interface-second.h"
#include "descriptor-interface.h"

#include <stdio.h>
#include <stdlib.h>

static int byte_layout_client(void) {
    struct record {
        int32_t guard;
        double value;
        char text[5];
    } records[3] = {{71, 1.0, {'a', 'b', 'c', 'd', 'e'}},
                    {72, 2.0, {'a', 'b', 'c', 'd', 'e'}},
                    {73, 3.0, {'a', 'b', 'c', 'd', 'e'}}};
    const ptrdiff_t bytes = (ptrdiff_t)sizeof(records[0]);
    f2c_descriptor target = {.data = &records[2].value,
                             .element_size = sizeof(double),
                             .rank = 1U,
                             .lower = {-4},
                             .extent = {3},
                             .byte_stride = {-bytes}};
    f2c_descriptor view = {.element_size = sizeof(double), .rank = 1U};
    f2c_module_byte_views_retain_real(&target, &view);
    if (view.data != &records[2].value || view.byte_stride[0] != -bytes ||
        view.element_size != sizeof(double) || view.extent[0] != 3 || view.deallocatable)
        return 0;
    target.data = &records[0].text[1];
    target.element_size = sizeof(char);
    target.character_length = 2U;
    target.byte_stride[0] = bytes;
    view.element_size = sizeof(char);
    f2c_module_byte_views_retain_character(&target, &view);
    if (view.data != &records[0].text[1] || view.character_length != 2U ||
        view.byte_stride[0] != bytes || view.extent[0] != 3 || view.deallocatable)
        return 0;
    view = f2c_module_byte_views_text_view(&target);
    return view.data == target.data && view.character_length == 2U &&
           view.byte_stride[0] == bytes && !view.deallocatable;
}

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
                             .byte_stride = {2 * (ptrdiff_t)sizeof(int32_t)}};
    f2c_descriptor alias = {.element_size = sizeof(int32_t), .rank = 1U};
    f2c_descriptor characters = {.volatile_data = text,
                                 .storage_qualifiers = 1U,
                                 .element_size = sizeof(char),
                                 .rank = 1U,
                                 .lower = {1},
                                 .extent = {2},
                                 .byte_stride = {3},
                                 .character_length = 3U};
    int32_t total = 0;
    descriptor_add(&target);
    descriptor_read(&target, &total);
    if (total != 36 || values[0] != 11 || values[1] != 90 || values[2] != 12 || values[3] != 90 ||
        values[4] != 13)
        return EXIT_FAILURE;
    descriptor_alias(&alias, &target);
    if (alias.volatile_data != target.volatile_data || alias.storage_qualifiers != 1U ||
        alias.deallocatable || alias.extent[0] != 3 ||
        alias.byte_stride[0] != 2 * (ptrdiff_t)sizeof(int32_t))
        return EXIT_FAILURE;
    descriptor_text(&characters);
    if (text[0] != 'o' || text[1] != 'k' || text[2] != ' ' || text[3] != 'o' || text[4] != 'k' ||
        text[5] != ' ')
        return EXIT_FAILURE;
    if (!byte_layout_client())
        return EXIT_FAILURE;
    f2c_module_byte_views_run();
    puts("generated descriptor interface contracts passed");
    return EXIT_SUCCESS;
}
