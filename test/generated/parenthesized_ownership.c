#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Keep generated allocation lifetimes observable independently of sanitizer
 * availability. The generated program must relinquish every temporary owner. */
static size_t live_allocations;

static void *tracked_malloc(size_t size) {
    void *result = malloc(size);
    if (result != NULL)
        ++live_allocations;
    return result;
}

static void *tracked_calloc(size_t count, size_t size) {
    void *result = calloc(count, size);
    if (result != NULL)
        ++live_allocations;
    return result;
}

static void *tracked_realloc(void *storage, size_t size) {
    const int new_allocation = storage == NULL;
    void *result = realloc(storage, size);
    if (result != NULL && new_allocation)
        ++live_allocations;
    return result;
}

static void tracked_free(void *storage) {
    if (storage != NULL) {
        if (live_allocations == 0U)
            abort();
        --live_allocations;
    }
    free(storage);
}

int f2c_generated_value_entry(void);
#define malloc tracked_malloc
#define calloc tracked_calloc
#define realloc tracked_realloc
#define free tracked_free
#define main f2c_generated_value_entry
#include F2C_PARENTHESIZED_SOURCE
#undef main
#undef malloc
#undef calloc
#undef realloc
#undef free

int main(void) {
    const int result = f2c_generated_value_entry();
    if (result != 0 || live_allocations != 0U) {
        fprintf(stderr, "unreleased parenthesized value allocations: %zu\n", live_allocations);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
