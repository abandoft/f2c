#ifndef F2C_SEMANTIC_CONSTANT_PRIVATE_H
#define F2C_SEMANTIC_CONSTANT_PRIVATE_H

#include "ir/intrinsic.h"

#include <stddef.h>
#include <stdint.h>

int f2c_constant_fold_bit_intrinsic(F2cIntrinsicId intrinsic, int integer_kind,
                                    const int64_t *arguments, size_t argument_count,
                                    int64_t *result);

#endif
