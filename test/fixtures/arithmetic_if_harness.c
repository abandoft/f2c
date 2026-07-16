#include <stdint.h>

void arithmetic_branch(const float *value, int32_t *result);

static int check(float value, int32_t expected) {
    int32_t result = 99;
    arithmetic_branch(&value, &result);
    return result == expected;
}

int main(void) { return check(-2.0f, -1) && check(0.0f, 0) && check(3.0f, 1) ? 0 : 1; }
