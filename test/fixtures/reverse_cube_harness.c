#include <stdint.h>

void reverse_cube(float *cube, const int32_t *n);

int main(void) {
    int32_t n = 2;
    float cube[8] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
    int32_t i;
    reverse_cube(cube, &n);
    for (i = 0; i < 8; ++i) {
        if (cube[i] != (float)(7 - i))
            return 1;
    }
    return 0;
}
