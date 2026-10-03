#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *allocations[128];
static size_t allocation_count;
static int fail_allocations;
static int fail_reallocations;
static int mode;
static int32_t *result_contract_target;
static void *retained_results[32];
static size_t retained_result_count;
static int check_retention;
static int allocation_budget = -1;
static size_t owned_scalar_calls;
static size_t owned_character_calls;

static int is_live_allocation(const void *storage) {
    for (size_t index = 0U; index < allocation_count; ++index)
        if (allocations[index] == storage)
            return 1;
    return 0;
}

static void check_previous_results(void) {
    if (check_retention)
        for (size_t index = 0U; index < retained_result_count; ++index)
            if (!is_live_allocation(retained_results[index]))
                abort(); /* Earlier loop results must survive until the construct ends. */
}

static void remember_result(void *storage) {
    if (check_retention) {
        if (retained_result_count == sizeof(retained_results) / sizeof(retained_results[0]))
            abort();
        retained_results[retained_result_count++] = storage;
    }
}

static void *tracked_malloc(size_t size) {
    if (fail_allocations || allocation_budget == 0)
        return NULL;
    if (allocation_budget > 0)
        --allocation_budget;
    void *storage = malloc(size);
    if (storage != NULL) {
        if (allocation_count == sizeof(allocations) / sizeof(allocations[0]))
            abort();
        allocations[allocation_count++] = storage;
    }
    return storage;
}

static void *tracked_calloc(size_t count, size_t size) {
    if (size != 0U && count > SIZE_MAX / size)
        return NULL;
    void *storage = tracked_malloc(count * size);
    if (storage != NULL)
        memset(storage, 0, count * size);
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

static void *tracked_realloc(void *storage, size_t size) {
    if (fail_reallocations)
        return NULL;
    if (storage == NULL)
        return tracked_malloc(size);
    for (size_t index = 0U; index < allocation_count; ++index)
        if (allocations[index] == storage) {
            void *replacement = realloc(storage, size);
            if (replacement != NULL)
                allocations[index] = replacement;
            return replacement;
        }
    abort();
}

#define malloc tracked_malloc
#define calloc tracked_calloc
#define free tracked_free
#define realloc tracked_realloc
#include F2C_RESULT_SOURCE
#undef malloc
#undef calloc
#undef free
#undef realloc

f2c_descriptor borrowed_scalar(void) {
    return (f2c_descriptor){.data = result_contract_target,
                            .deallocatable = true,
                            .element_size = sizeof(*result_contract_target),
                            .rank = mode == 1 ? 1U : 0U};
}

f2c_descriptor owned_scalar(void) {
    ++owned_scalar_calls;
    check_previous_results();
    int32_t *value = (int32_t *)tracked_malloc(sizeof(*value));
    if (value == NULL)
        abort();
    *value = 73;
    remember_result(value);
    return (f2c_descriptor){.data = value,
                            .deallocatable = true,
                            .element_size = mode == 2 ? 1U : sizeof(*value),
                            .rank = 0U};
}

f2c_descriptor borrowed_array(void) {
    return (f2c_descriptor){.data = mode == 5 ? NULL : &result_contract_target[4],
                            .deallocatable = true,
                            .element_size = sizeof(*result_contract_target),
                            .rank = 1U,
                            .lower = {-5},
                            .extent = {mode >= 5 ? 0 : 3},
                            .stride = {-2}};
}

f2c_descriptor owned_character(void) {
    ++owned_character_calls;
    check_previous_results();
    char *value = (char *)tracked_malloc(3U);
    if (value == NULL)
        abort();
    memcpy(value, "a\0b", 3U);
    remember_result(value);
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
    int32_t constructor_values[17] = {0};
    char constructor_characters[15] = {0};
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
        } else if (strcmp(argv[1], "retention_growth") == 0) {
            check_retention = 1;
            fail_reallocations = 1;
            fetch_owned_constructor(constructor_values);
        } else if (strcmp(argv[1], "mold_association") == 0) {
            mode = 5;
            allocate_from_mold(array);
        } else if (strcmp(argv[1], "value_association") == 0) {
            mode = 5;
            fetch_empty_array(&scalar);
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
    check_retention = 1;
    fetch_owned_constructor(constructor_values);
    if (retained_result_count != 17U || allocation_count != 1U)
        return 8;
    for (size_t index = 0U; index < 17U; ++index)
        if (constructor_values[index] != 73)
            return 9;
    retained_result_count = 0U;
    fetch_nested_constructor(constructor_values);
    if (retained_result_count != 18U || allocation_count != 1U ||
        constructor_values[0] != 17 * 73 || constructor_values[1] != 73)
        return 12;
    retained_result_count = 0U;
    fetch_character_constructor(constructor_characters, 5U);
    if (retained_result_count != 3U || allocation_count != 1U)
        return 10;
    for (size_t index = 0U; index < 3U; ++index)
        if (memcmp(constructor_characters + index * 5U, "a\0b  ", 5U) != 0)
            return 11;
    check_retention = 0;
    const size_t scalar_calls_before = owned_scalar_calls;
    const size_t character_calls_before = owned_character_calls;
    allocate_from_results(array, characters, sizeof(characters));
    if (owned_scalar_calls != scalar_calls_before + 1U ||
        owned_character_calls != character_calls_before + 1U || allocation_count != 1U ||
        array[0] != 146 || array[1] != 146 || array[2] != 146 ||
        memcmp(characters, "a\0b  ", 5U) != 0)
        return 13;
    /* MOLD needs only one target allocation. A value snapshot would consume
     * the budget and fail even though the source metadata is valid. */
    allocation_budget = 1;
    allocate_from_mold(array);
    if (allocation_budget != 0 || allocation_count != 1U || array[0] != 3 || array[1] != 1 ||
        result_contract_target[2] != 31)
        return 14;
    /* The function result succeeds; only the destination allocation fails.
     * STAT must report it and result storage must still be released. */
    allocation_budget = 1;
    scalar = 0;
    allocate_result_failure(&scalar);
    if (scalar == 0 || allocation_count != 1U || allocation_budget != 0)
        return 15;
    allocation_budget = -1;
    mode = 6;
    scalar = -1;
    fetch_empty_array(&scalar);
    if (scalar != 0 || allocation_count != 1U)
        return 16;
    allocation_budget = 1;
    allocate_from_mold(array);
    if (array[0] != 0 || array[1] != 1 || allocation_count != 1U || allocation_budget != 0)
        return 17;
    allocation_budget = -1;
    mode = 0;
    tracked_free(result_contract_target);
    if (allocation_count != 0U)
        return 7;
    puts("independent function result contracts passed");
    return 0;
}
