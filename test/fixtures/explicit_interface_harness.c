#include "generated_explicit_interface.h"

#include <stdint.h>
#include <stdio.h>

void adjust(int32_t *value, const float *scale, float *values, const char *tag, size_t tag_length);
float score_impl(const float *values, const float *offset, const float *bias);
float classify_integer(const int32_t *value);
float classify_real(const float *value);

void adjust(int32_t *value, const float *scale, float *values, const char *tag, size_t tag_length) {
    size_t index;
    if (tag_length == 4U && tag[0] == 'A' && tag[1] == 'B')
        *value += 1;
    *value += scale != NULL ? (int32_t)*scale : INT32_C(7);
    for (index = 0U; index < 3U; ++index)
        values[index] += (float)*value;
}

float score_impl(const float *values, const float *offset, const float *bias) {
    return values[0] + values[1] + values[2] + (offset != NULL ? *offset : 0.0F) + *bias;
}

float classify_integer(const int32_t *value) { return (float)*value + 100.0F; }

float classify_real(const float *value) { return *value + 200.0F; }

int main(void) {
    int32_t results[9] = {0};
    explicit_interface_matrix(results);
    (void)printf("%d %d %d %d %d %d %d %d %d\n", (int)results[0], (int)results[1], (int)results[2],
                 (int)results[3], (int)results[4], (int)results[5], (int)results[6],
                 (int)results[7], (int)results[8]);
    return 0;
}
