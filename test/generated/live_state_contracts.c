#ifndef F2C_LIVE_STATE_SOURCE
#define F2C_LIVE_STATE_SOURCE "generated_live_external_state.c"
#endif
#include F2C_LIVE_STATE_SOURCE

/* This deliberately changes the same record without going through the
 * translator's call bridges. A stale entry cache or exit-time copyback must
 * therefore fail independently of the implementation's own state writers. */
static f2c_descriptor *active_state;
static int mode;
static int32_t replacement[] = {10, 99, 20, 99, 30, 99, 40, 99};
static char replacement_text[] = {'l', 'i', 'v', 'e', '!'};

void change_state(void) {
    if (mode == 0) {
        active_state->data = replacement;
        active_state->lower[0] = -2;
        active_state->extent[0] = 3;
        active_state->stride[0] = 2;
    } else if (mode == 1) {
        active_state->data = NULL;
        active_state->extent[0] = 0;
    } else {
        active_state->data = replacement_text;
        active_state->character_length = sizeof(replacement_text);
    }
}

int main(void) {
    int failures = 0;
    int32_t initial[] = {1, 2, 3, 4};
    int32_t results[8] = {0};
    const int32_t expected[] = {1, 4, 1, 10, 3, -2, 0, 60};
    char text[] = {'o', 'k'};
    f2c_descriptor state = {.data = initial,
                            .element_size = sizeof(int32_t),
                            .rank = 1U,
                            .lower = {1},
                            .extent = {4},
                            .stride = {1}};
    active_state = &state;
    observe_live_pointer(&state, results);
    for (size_t index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        if (results[index] != expected[index]) {
            fprintf(stderr, "pointer result %zu: expected %d, got %d\n", index,
                    (int)expected[index], (int)results[index]);
            ++failures;
        }
    }
    if (state.data != replacement || state.lower[0] != -2 || state.extent[0] != 3 ||
        state.stride[0] != 2) {
        fprintf(stderr, "return overwrote externally updated pointer state\n");
        ++failures;
    }
    state.data = initial;
    state.lower[0] = 1;
    state.extent[0] = 4;
    state.stride[0] = 1;
    mode = 1;
    observe_live_allocatable(&state, results);
    if (results[0] != 1 || results[1] != 0 || state.data != NULL) {
        fprintf(stderr, "allocation status was stale or overwritten on return\n");
        ++failures;
    }
    state = (f2c_descriptor){
        .data = text, .element_size = sizeof(char), .character_length = sizeof(text)};
    mode = 2;
    observe_live_character(&state, results);
    if (results[0] != 2 || results[1] != 5 || state.data != replacement_text ||
        state.character_length != sizeof(replacement_text)) {
        fprintf(stderr, "character state was stale or overwritten on return\n");
        ++failures;
    }
    if (failures == 0)
        puts("external live state contracts passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
