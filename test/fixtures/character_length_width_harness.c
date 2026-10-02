/* Execute the same checked length helpers on native and 32-bit WebAssembly. */
int f2c_generated_character_length_main(void);
#define main f2c_generated_character_length_main
#ifndef F2C_CHARACTER_LENGTH_SOURCE
#define F2C_CHARACTER_LENGTH_SOURCE "character_length_parameters.c"
#endif
#include F2C_CHARACTER_LENGTH_SOURCE
#undef main

int main(void) {
    size_t length = 99U;
    if (!f2c_character_parameter_size(INT64_MIN, &length) || length != 0U)
        return 20;
    if (!f2c_character_parameter_size(INT64_C(0), &length) || length != 0U)
        return 21;
    if (f2c_character_parameter_length(INT64_C(-7)) != 0U)
        return 22;
    length = 99U;
#if SIZE_MAX < UINT64_MAX
    const int64_t too_wide = INT64_C(4294967296);
    int32_t status = 0;
    if (f2c_character_parameter_size(INT64_C(4294967296), &length) || length != 99U)
        return 23;
    if (!f2c_character_parameter_size((int64_t)SIZE_MAX, &length) || length != SIZE_MAX)
        return 24;
    f2c_module_character_length_storage_try_length(&too_wide, &status);
    if (status == 0)
        return 26;
#else
    if (!f2c_character_parameter_size(INT64_C(4294967296), &length) ||
        (uint64_t)length != UINT64_C(4294967296))
        return 25;
#endif
    return f2c_generated_character_length_main();
}
