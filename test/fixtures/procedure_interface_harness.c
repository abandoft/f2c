#include "generated_procedure_interface.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    int32_t results[10] = {0};
    size_t index;
    procedure_interface_matrix(results);
    for (index = 0U; index < 10U; ++index)
        (void)printf("%" PRId32 "%s", results[index], index + 1U < 10U ? " " : "\n");
    return 0;
}
