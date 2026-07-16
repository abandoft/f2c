#include "matrix.h"

#include <stdlib.h>

int main(void) {
    int passed = 1;
    passed = f2c_benchmark_level1() && passed;
    passed = f2c_benchmark_level2() && passed;
    passed = f2c_benchmark_level3() && passed;
    passed = f2c_benchmark_lapack() && passed;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
