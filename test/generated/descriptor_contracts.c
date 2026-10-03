#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int f2c_descriptor_fixture_entry(void);
static int fail_allocations;

static void *descriptor_malloc(size_t size) { return fail_allocations ? NULL : malloc(size); }

#define malloc descriptor_malloc
#define main f2c_descriptor_fixture_entry
#include F2C_DESCRIPTOR_SOURCE
#undef main
#undef malloc

static void require(int condition) {
    if (!condition)
        abort();
}

static void expected_abort(int signal_number) {
    (void)signal_number;
    _Exit(99);
}

static void test_numeric_records(void) {
    volatile int32_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const int32_t expected[8] = {101, 2, 103, 4, 105, 6, 107, 8};
    f2c_descriptor source = {.volatile_data = &storage[6],
                             .storage_qualifiers = 1U,
                             .element_size = sizeof(int32_t),
                             .rank = 1U,
                             .lower = {-2},
                             .extent = {4},
                             .stride = {-2}};
    f2c_descriptor temporary = {0};
    f2c_descriptor_prepare_contiguous(&temporary, &source, false, true);
    require(temporary.storage_qualifiers == 0U && !temporary.deallocatable &&
            temporary.stride[0] == 1 && temporary.lower[0] == -2);
    for (size_t index = 0U; index < 4U; ++index) {
        int32_t *values = (int32_t *)temporary.data;
        require(values[index] == 7 - 2 * (int32_t)index);
        values[index] += 100;
    }
    f2c_descriptor_finish_contiguous(&source, &temporary, false, true);
    require(temporary.data == NULL && temporary.storage_qualifiers == 0U && temporary.rank == 0U);
    for (size_t index = 0U; index < 8U; ++index)
        require(storage[index] == expected[index]);
    {
        const int32_t readonly[3] = {9, 8, 7};
        f2c_descriptor view = {.readonly_data = readonly,
                               .element_size = sizeof(int32_t),
                               .rank = 1U,
                               .extent = {3},
                               .stride = {1}};
        f2c_descriptor_prepare_contiguous(&temporary, &view, false, true);
        require(memcmp(temporary.readonly_data, readonly, sizeof(readonly)) == 0);
        f2c_descriptor_finish_contiguous(&view, &temporary, false, false);
    }
}

static void test_character_records(void) {
    volatile char storage[9] = {'o', 'n', 'e', 't', 'w', 'o', 's', 'i', 'x'};
    const char expected[] = "onenewsix";
    f2c_descriptor source = {.volatile_data = &storage[6],
                             .storage_qualifiers = 1U,
                             .element_size = 1U,
                             .rank = 1U,
                             .extent = {3},
                             .stride = {-1},
                             .character_length = 3U};
    f2c_descriptor temporary = {0};
    f2c_descriptor_prepare_contiguous(&temporary, &source, true, true);
    require(memcmp(temporary.readonly_data, "sixtwoone", 9U) == 0);
    memcpy((char *)temporary.data + 3U, "new", 3U);
    f2c_descriptor_finish_contiguous(&source, &temporary, true, true);
    for (size_t index = 0U; index < 9U; ++index)
        require(storage[index] == expected[index]);
    {
        const volatile char readonly[3] = {'c', 'v', '!'};
        char snapshot[3] = {0};
        f2c_descriptor view = {.readonly_volatile_data = readonly, .storage_qualifiers = 1U};
        f2c_descriptor_read_record(snapshot, &view, 0, 3U);
        require(memcmp(snapshot, "cv!", 3U) == 0);
    }
}

static void test_empty_records(void) {
    f2c_descriptor source = {.volatile_data = NULL,
                             .storage_qualifiers = 1U,
                             .element_size = sizeof(int32_t),
                             .rank = 1U,
                             .extent = {0},
                             .stride = {-2}};
    f2c_descriptor temporary = {0};
    f2c_descriptor_read_record(NULL, NULL, PTRDIFF_MAX, 0U);
    f2c_descriptor_write_record(NULL, PTRDIFF_MIN, NULL, 0U);
    f2c_descriptor_prepare_contiguous(&temporary, &source, false, true);
    f2c_descriptor_finish_contiguous(&source, &temporary, false, true);
    source.extent[0] = 3;
    source.element_size = 1U;
    source.character_length = 0U;
    f2c_descriptor_prepare_contiguous(&temporary, &source, true, true);
    f2c_descriptor_finish_contiguous(&source, &temporary, true, true);
    require(temporary.data == NULL);
}

static void test_linear_offsets(void) {
    const f2c_descriptor view = {.rank = 2U, .extent = {2, 3}, .stride = {-2, 7}};
    const ptrdiff_t expected[] = {0, -2, 7, 5, 14, 12};
    for (size_t index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index)
        require(f2c_descriptor_linear_offset(&view, index) == expected[index]);
    require(f2c_array_offset(INT64_MIN + 1, INT64_MIN, 2U) == 1U);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        char byte = 0;
        f2c_descriptor source = {
            .data = &byte, .element_size = 1U, .rank = 1U, .extent = {1}, .stride = {1}};
        f2c_descriptor temporary = {0};
        if (signal(SIGABRT, expected_abort) == SIG_ERR)
            return 2;
        if (strcmp(argv[1], "--null-read") == 0)
            f2c_descriptor_read_record(&byte, NULL, 0, 1U);
        else if (strcmp(argv[1], "--null-write") == 0)
            f2c_descriptor_write_record(NULL, 0, &byte, 1U);
        else if (strcmp(argv[1], "--allocation") == 0) {
            fail_allocations = 1;
            f2c_descriptor_prepare_contiguous(&temporary, &source, false, true);
        } else if (strcmp(argv[1], "--offset") == 0) {
            (void)f2c_descriptor_stride_extent(PTRDIFF_MAX, 2U);
        } else if (strcmp(argv[1], "--count") == 0) {
            source.rank = 2U;
            source.extent[0] = INT64_MAX;
            source.extent[1] = 2;
            source.element_size = sizeof(int32_t);
            f2c_descriptor_prepare_contiguous(&temporary, &source, false, true);
        } else if (strcmp(argv[1], "--linear-null") == 0) {
            (void)f2c_descriptor_linear_offset(NULL, 0U);
        } else if (strcmp(argv[1], "--linear-rank-zero") == 0) {
            source.rank = 0U;
            (void)f2c_descriptor_linear_offset(&source, 0U);
        } else if (strcmp(argv[1], "--linear-rank-high") == 0) {
            source.rank = 16U;
            (void)f2c_descriptor_linear_offset(&source, 0U);
        } else if (strcmp(argv[1], "--linear-negative") == 0) {
            source.extent[0] = -1;
            (void)f2c_descriptor_linear_offset(&source, 0U);
        } else if (strcmp(argv[1], "--linear-empty") == 0) {
            source.extent[0] = 0;
            (void)f2c_descriptor_linear_offset(&source, 0U);
        } else if (strcmp(argv[1], "--linear-range") == 0) {
            (void)f2c_descriptor_linear_offset(&source, 1U);
        } else if (strcmp(argv[1], "--linear-overflow") == 0) {
            source.extent[0] = 3;
            source.stride[0] = PTRDIFF_MAX;
            (void)f2c_descriptor_linear_offset(&source, 2U);
        } else if (strcmp(argv[1], "--state-dimension") == 0) {
            source.rank = 16U;
            (void)f2c_descriptor_state_extent(&source, 15U, 1U);
        } else if (strcmp(argv[1], "--subscript-distance") == 0) {
            (void)f2c_array_offset(INT64_MAX, INT64_MIN, SIZE_MAX);
        } else {
            return 2;
        }
        return EXIT_SUCCESS;
    }
    test_numeric_records();
    test_character_records();
    test_empty_records();
    test_linear_offsets();
    puts("descriptor record contracts passed");
    return EXIT_SUCCESS;
}
