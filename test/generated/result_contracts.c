#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *allocations[128];
static size_t allocation_count;
static int fail_allocations;
static int mode;
static int32_t *result_contract_target;

static void *tracked_malloc(size_t size) {
    if (fail_allocations)
        return NULL;
    void *storage = malloc(size);
    if (storage != NULL) {
        if (allocation_count == sizeof(allocations) / sizeof(allocations[0]))
            abort();
        allocations[allocation_count++] = storage;
    }
    return storage;
}

static void tracked_free(void *storage) {
    if (storage == NULL)
        return;
    for (size_t index = 0U; index < allocation_count; ++index) {
        if (allocations[index] == storage) {
            allocations[index] = allocations[--allocation_count];
            free(storage);
            return;
        }
    }
    abort(); /* Reject freeing a borrowed target or a non-base array address. */
}

#define malloc tracked_malloc
#define free tracked_free
#include F2C_RESULT_SOURCE
#undef malloc
#undef free

f2c_descriptor borrowed_scalar(void) {
    return (f2c_descriptor){.data = result_contract_target,
                            .deallocatable = true,
                            .element_size = sizeof(*result_contract_target),
                            .rank = mode == 1 ? 1U : 0U};
}

f2c_descriptor owned_scalar(void) {
    int32_t *value = (int32_t *)tracked_malloc(sizeof(*value));
    if (value == NULL)
        abort();
    *value = 73;
    return (f2c_descriptor){.data = value,
                            .deallocatable = true,
                            .element_size = mode == 2 ? 1U : sizeof(*value),
                            .rank = 0U};
}

f2c_descriptor borrowed_array(void) {
    return (f2c_descriptor){.data = &result_contract_target[4],
                            .deallocatable = true,
                            .element_size = sizeof(*result_contract_target),
                            .rank = 1U,
                            .lower = {-5},
                            .extent = {3},
                            .stride = {-2}};
}

f2c_descriptor owned_character(void) {
    char *value = (char *)tracked_malloc(3U);
    if (value == NULL)
        abort();
    memcpy(value, "a\0b", 3U);
    return (f2c_descriptor){.data = value,
                            .deallocatable = mode != 3,
                            .element_size = 1U,
                            .rank = 0U,
                            .character_length = 3U};
}

static void expected_abort(int signal_number) {
    (void)signal_number;
    _Exit(99);
}

int main(int argc, char **argv) {
    int32_t scalar = 0;
    int32_t array[3] = {0};
    char characters[5] = {0};
    result_contract_target = (int32_t *)tracked_malloc(5U * sizeof(*result_contract_target));
    if (result_contract_target == NULL)
        return 1;
    for (size_t index = 0U; index < 5U; ++index)
        result_contract_target[index] = 10 * (int32_t)(index + 1U);
    if (argc == 2) {
        if (signal(SIGABRT, expected_abort) == SIG_ERR)
            return 2;
        if (strcmp(argv[1], "rank") == 0) {
            mode = 1;
            fetch_scalar(&scalar);
        } else if (strcmp(argv[1], "size") == 0) {
            mode = 2;
            fetch_owned(&scalar);
        } else if (strcmp(argv[1], "ownership") == 0) {
            mode = 3;
            fetch_character(characters, 5U);
        } else if (strcmp(argv[1], "allocation") == 0) {
            fail_allocations = 1;
            fetch_array(array);
        } else
            return 2;
        return 1;
    }
    fetch_scalar(&scalar);
    if (scalar != 10 || allocation_count != 1U)
        return 3;
    fetch_owned(&scalar);
    if (scalar != 73 || allocation_count != 1U)
        return 4;
    fetch_array(array);
    if (array[0] != 50 || array[1] != 30 || array[2] != 10 || allocation_count != 1U)
        return 5;
    result_contract_target[2] = 31;
    fetch_character(characters, sizeof(characters));
    if (memcmp(characters, "a\0b  ", 5U) != 0 || allocation_count != 1U)
        return 6;
    tracked_free(result_contract_target);
    if (allocation_count != 0U)
        return 7;
    puts("independent function result contracts passed");
    return 0;
}
