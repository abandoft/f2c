#include <stdlib.h>
#include <string.h>

#include "generated_character_abi.h"

int main(void) {
    char padded[6] = {'?', '?', '?', '?', '?', '?'};
    char short_source[3] = {'A', 'B', ' '};
    char truncated[4] = {'?', '?', '?', '?'};
    char long_source[6] = {'W', 'X', 'Y', 'Z', '1', '2'};
    char fixed[6] = {'?', '?', '?', '?', 'K', 'L'};
    char overlap[5] = {'A', 'B', 'C', 'D', 'E'};
    char substring[6] = {'A', 'B', 'C', 'D', 'E', 'F'};
    static const char expected_padded[6] = {'A', 'B', ' ', ' ', ' ', ' '};
    static const char expected_truncated[4] = {'W', 'X', 'Y', 'Z'};
    static const char expected_fixed[6] = {'A', 'B', ' ', ' ', 'K', 'L'};
    static const char expected_overlap[5] = {'A', 'A', 'B', 'C', 'D'};
    static const char expected_substring[6] = {'A', 'Q', ' ', ' ', ' ', 'F'};
    char equal_left[1] = {'A'};
    char equal_right[2] = {'A', ' '};
    char less_right[2] = {'A', 'B'};
    char greater_left[2] = {'A', 'C'};
    char greater_right[3] = {'A', 'B', ' '};
    char function_result[4] = {'?', '?', '?', '?'};
    char copied_result[4] = {'?', '?', '?', '?'};
    char array_values[8] = {'?', '?', '?', '?', '?', '?', '?', '?'};
    char local_array_result[4] = {'?', '?', '?', '?'};
    char concatenated[6] = {'?', '?', '?', '?', '?', '?'};
    char concat_left[2] = {'A', 'B'};
    char concat_right[3] = {'C', 'D', 'E'};
    char data_values[12] = {'?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?'};
    char section_values[16] = {'A', '1', '1', '1', 'B', '2', '2', '2',
                               'C', '3', '3', '3', 'D', '4', '4', '4'};
    char section_source[8] = {'p', '1', 'q', '2', 'r', '3', 's', '4'};
    char whole_values[12] = {'?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?'};
    char whole_source[6] = {'p', '1', 'q', '2', 'r', '3'};
    char broadcast_values[12] = {'?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?'};
    char constructor_values[12] = {'?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?'};
    char declaration_first[16] = {'?', '?', '?', '?', '?', '?', '?', '?',
                                  '?', '?', '?', '?', '?', '?', '?', '?'};
    char declaration_second[16] = {'?', '?', '?', '?', '?', '?', '?', '?',
                                   '?', '?', '?', '?', '?', '?', '?', '?'};
    char length_value[4] = {'A', 'B', ' ', ' '};
    char length_specification[4] = {'?', '?', '?', '?'};
    int32_t declared_length = 0;
    int32_t trimmed_length = 0;
    char automatic_source[3] = {'A', 'B', 'C'};
    char automatic_output[5] = {'?', '?', '?', '?', '?'};
    int32_t automatic_length = 0;
    int32_t comparison = 0;
    int32_t selector = 1;
    assign_assumed(padded, short_source, sizeof(padded), sizeof(short_source));
    assign_assumed(truncated, long_source, sizeof(truncated), sizeof(long_source));
    assign_fixed(fixed, short_source, sizeof(fixed), sizeof(short_source));
    shift_overlap(overlap, sizeof(overlap));
    pad_substring(substring, sizeof(substring));
    compare_character(equal_left, equal_right, &comparison, sizeof(equal_left),
                      sizeof(equal_right));
    if (comparison != 41)
        return EXIT_FAILURE;
    compare_character(equal_left, less_right, &comparison, sizeof(equal_left), sizeof(less_right));
    if (comparison != 14)
        return EXIT_FAILURE;
    compare_character(greater_left, greater_right, &comparison, sizeof(greater_left),
                      sizeof(greater_right));
    if (memcmp(padded, expected_padded, sizeof(padded)) != 0 ||
        memcmp(truncated, expected_truncated, sizeof(truncated)) != 0 ||
        memcmp(fixed, expected_fixed, sizeof(fixed)) != 0 ||
        memcmp(overlap, expected_overlap, sizeof(overlap)) != 0 ||
        memcmp(substring, expected_substring, sizeof(substring)) != 0 || comparison != 50)
        return EXIT_FAILURE;
    make_character_result(function_result, sizeof(function_result), &selector);
    if (memcmp(function_result, "A   ", sizeof(function_result)) != 0)
        return EXIT_FAILURE;
    selector = 2;
    copy_character_result(copied_result, &selector, sizeof(copied_result));
    if (memcmp(copied_result, "WXYZ", sizeof(copied_result)) != 0)
        return EXIT_FAILURE;
    fill_character_array(array_values, 4U);
    if (memcmp(array_values, "A   WXYZ", sizeof(array_values)) != 0)
        return EXIT_FAILURE;
    copy_local_character_array(local_array_result, sizeof(local_array_result));
    if (memcmp(local_array_result, "R   ", sizeof(local_array_result)) != 0)
        return EXIT_FAILURE;
    concatenate_character(concatenated, concat_left, concat_right, sizeof(concatenated),
                          sizeof(concat_left), sizeof(concat_right));
    if (memcmp(concatenated, "ABCDE ", sizeof(concatenated)) != 0)
        return EXIT_FAILURE;
    copy_character_data(data_values, 4U);
    if (memcmp(data_values, "A   BC  WXYZ", sizeof(data_values)) != 0)
        return EXIT_FAILURE;
    character_sections(section_values, section_source, 4U, 2U);
    if (memcmp(section_values, "s4  Z   q2  Z   ", sizeof(section_values)) != 0)
        return EXIT_FAILURE;
    character_whole(whole_values, whole_source, 4U, 2U);
    if (memcmp(whole_values, "p1  q2  r3  ", sizeof(whole_values)) != 0)
        return EXIT_FAILURE;
    character_broadcast(broadcast_values, 4U);
    if (memcmp(broadcast_values, "K   K   K   ", sizeof(broadcast_values)) != 0)
        return EXIT_FAILURE;
    character_constructor(constructor_values, 4U);
    if (memcmp(constructor_values, "A   BC  WXYZ", sizeof(constructor_values)) != 0)
        return EXIT_FAILURE;
    declaration_character(declaration_first, 4U);
    declaration_character(declaration_second, 4U);
    if (memcmp(declaration_first, "A   BC  WXYZQ   ", sizeof(declaration_first)) != 0 ||
        memcmp(declaration_second, "MUT BC  WXYZR   ", sizeof(declaration_second)) != 0)
        return EXIT_FAILURE;
    character_lengths(length_value, &declared_length, &trimmed_length, sizeof(length_value));
    if (declared_length != 4 || trimmed_length != 2)
        return EXIT_FAILURE;
    character_length_spec(length_specification, sizeof(length_specification));
    if (memcmp(length_specification, "X   ", sizeof(length_specification)) != 0)
        return EXIT_FAILURE;
    automatic_character(automatic_source, automatic_output, &automatic_length,
                        sizeof(automatic_source), sizeof(automatic_output));
    if (automatic_length != 5 || memcmp(automatic_output, "ABCXY", sizeof(automatic_output)) != 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
