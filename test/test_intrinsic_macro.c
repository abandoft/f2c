#include "semantic/intrinsic.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    F2cIntrinsicId id;
    for (id = (F2cIntrinsicId)(F2C_INTRINSIC_NONE + 1); id < F2C_INTRINSIC_ID_COUNT;
         id = (F2cIntrinsicId)(id + 1)) {
        const F2cIntrinsicSpecification *specification = f2c_intrinsic_specification(id);
        if (specification == NULL || specification->descriptor.id != id ||
            specification->signature.id != id || specification->descriptor.canonical_name == NULL ||
            strcmp(specification->descriptor.canonical_name, specification->signature.name) != 0) {
            fprintf(stderr, "intrinsic registry corrupted by a platform macro\n");
            return 1;
        }
    }
    return 0;
}
