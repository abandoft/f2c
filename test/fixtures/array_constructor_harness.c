#include "generated_array_constructor.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const int32_t expected[] = {11, 12, 13, 101, 102, 201, 202, 3, 2, 1, 900, 901};
    static const char expected_words[] = "CCCBB A  ";
    int32_t n = 3;
    int32_t offset = 10;
    int32_t values[12] = {0};
    char words[9] = {0};
    size_t i;

    constructor_values(&n, &offset, values, words, 3U);
    if (memcmp(values, expected, sizeof(expected)) != 0 ||
        memcmp(words, expected_words, sizeof(words)) != 0)
        return 1;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); ++i)
        (void)printf("%s%d", i == 0U ? "" : " ", (int)values[i]);
    (void)putchar('\n');
    (void)printf("|%.3s|%.3s|%.3s|\n", words, words + 3, words + 6);
    return 0;
}
