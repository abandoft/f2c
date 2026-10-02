/* Test the helpers in the actual emitted standalone translation unit. */
int f2c_fixture_main(void);
#define main f2c_fixture_main
#include "generated_list_controls.c"
#undef main

#include <locale.h>

typedef struct InputCase {
    const char *text;
    int kind;
    uint64_t lower;
    uint64_t upper;
    uint64_t nearest;
    uint64_t compatible;
} InputCase;

static int failures;

static void expect(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static uint64_t real_bits(double value, int kind) {
    uint64_t result = 0U;
    if (kind == 4) {
        float narrow = (float)value;
        uint32_t bits;
        memcpy(&bits, &narrow, sizeof(bits));
        result = bits;
    } else {
        memcpy(&result, &value, sizeof(result));
    }
    return result;
}

static void test_rounding(void) {
    static const InputCase cases[] = {
        {"0.1", 4, UINT64_C(0x3dcccccc), UINT64_C(0x3dcccccd), UINT64_C(0x3dcccccd),
         UINT64_C(0x3dcccccd)},
        {"0.1", 8, UINT64_C(0x3fb9999999999999), UINT64_C(0x3fb999999999999a),
         UINT64_C(0x3fb999999999999a), UINT64_C(0x3fb999999999999a)},
        {"1.000000059604644775390625", 4, UINT64_C(0x3f800000), UINT64_C(0x3f800001),
         UINT64_C(0x3f800000), UINT64_C(0x3f800001)},
        {"1.000000178813934326171875", 4, UINT64_C(0x3f800001), UINT64_C(0x3f800002),
         UINT64_C(0x3f800002), UINT64_C(0x3f800002)},
        {"0000100000005960464477539062500e-26", 4, UINT64_C(0x3f800000), UINT64_C(0x3f800001),
         UINT64_C(0x3f800000), UINT64_C(0x3f800001)},
        {"1.00000000000000011102230246251565404236316680908203125", 8, UINT64_C(0x3ff0000000000000),
         UINT64_C(0x3ff0000000000001), UINT64_C(0x3ff0000000000000), UINT64_C(0x3ff0000000000001)},
        {"3.4028234663852885981170418348451692545e38", 4, UINT64_C(0x7f7fffff),
         UINT64_C(0x7f800000), UINT64_C(0x7f7fffff), UINT64_C(0x7f7fffff)},
        {"1.7976931348623157081452742373170435680e308", 8, UINT64_C(0x7fefffffffffffff),
         UINT64_C(0x7ff0000000000000), UINT64_C(0x7fefffffffffffff), UINT64_C(0x7fefffffffffffff)},
        {"7."
         "00649232162408535461864791644958065640130970938257885878534141944895541342930300743319094"
         "181060791015625e-46",
         4, UINT64_C(0), UINT64_C(1), UINT64_C(0), UINT64_C(1)},
        {"1e-9999", 8, UINT64_C(0), UINT64_C(1), UINT64_C(0), UINT64_C(0)}};
    static const int environments[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    static const f2c_format_round_mode modes[] = {F2C_FORMAT_ROUND_UP, F2C_FORMAT_ROUND_DOWN,
                                                  F2C_FORMAT_ROUND_ZERO, F2C_FORMAT_ROUND_NEAREST,
                                                  F2C_FORMAT_ROUND_COMPATIBLE};
    const int original = fegetround();
    for (size_t environment = 0U; environment < sizeof(environments) / sizeof(environments[0]);
         ++environment) {
        if (fesetround(environments[environment]) != 0)
            continue;
        for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            for (unsigned negative = 0U; negative < 2U; ++negative) {
                char text[256];
                const uint64_t sign = negative == 0U           ? 0U
                                      : cases[index].kind == 4 ? UINT64_C(0x80000000)
                                                               : UINT64_C(0x8000000000000000);
                (void)snprintf(text, sizeof(text), "%s%s", negative != 0U ? "-" : "",
                               cases[index].text);
                for (size_t mode = 0U; mode < sizeof(modes) / sizeof(modes[0]); ++mode) {
                    uint64_t expected;
                    double value = 42.0;
                    switch (modes[mode]) {
                    case F2C_FORMAT_ROUND_UP:
                        expected = negative != 0U ? cases[index].lower : cases[index].upper;
                        break;
                    case F2C_FORMAT_ROUND_DOWN:
                        expected = negative != 0U ? cases[index].upper : cases[index].lower;
                        break;
                    case F2C_FORMAT_ROUND_ZERO:
                        expected = cases[index].lower;
                        break;
                    case F2C_FORMAT_ROUND_NEAREST:
                        expected = cases[index].nearest;
                        break;
                    default:
                        expected = cases[index].compatible;
                        break;
                    }
                    const bool overflow =
                        expected == (cases[index].kind == 4 ? UINT64_C(0x7f800000)
                                                            : UINT64_C(0x7ff0000000000000));
                    const int status =
                        f2c_io_parse_real(text, false, cases[index].kind, modes[mode], &value);
                    if ((overflow && (status != 0 || value != 42.0)) ||
                        (!overflow && (status != 1 ||
                                       real_bits(value, cases[index].kind) != (expected | sign)))) {
                        fprintf(stderr, "input=%s kind=%d mode=%d ambient=%d\n", text,
                                cases[index].kind, (int)modes[mode], environments[environment]);
                        ++failures;
                    }
                    expect(fegetround() == environments[environment],
                           "input preserves caller rounding mode");
                }
            }
        }
    }
    if (original >= 0)
        (void)fesetround(original);
}

static void test_shared_list_fields(void) {
    const size_t zeros = 32768U;
    const size_t length = zeros * 2U + 7U;
    char *record = (char *)malloc(length + 1U);
    f2c_io_stream stream;
    f2c_complex_double value = f2c_make_z(42.0, 43.0);
    expect(record != NULL, "wide complex field allocation succeeds");
    if (record == NULL)
        return;
    record[0] = '(';
    memset(record + 1U, '0', zeros);
    memcpy(record + 1U + zeros, ".1,", 3U);
    memset(record + 4U + zeros, '0', zeros);
    memcpy(record + 4U + zeros * 2U, ".2)", 3U);
    record[length] = '\0';
    expect(f2c_stream_initialize_internal(&stream, record, length, 1U, true),
           "wide complex stream initializes");
    stream.controls.rounding = F2C_FORMAT_ROUND_UP;
    expect(f2c_read_z(&stream, &value) == 1 &&
               real_bits(creal(value), 8) == UINT64_C(0x3fb999999999999a) &&
               real_bits(cimag(value), 8) == UINT64_C(0x3fc999999999999a),
           "both complex components use untruncated target-kind conversion");
    free(record);

    char invalid_complex[] = "(0.1,invalid)";
    expect(f2c_stream_initialize_internal(&stream, invalid_complex, sizeof(invalid_complex) - 1U,
                                          1U, true),
           "invalid complex stream initializes");
    value = f2c_make_z(42.0, 43.0);
    expect(f2c_read_z(&stream, &value) == 0 && creal(value) == 42.0 && cimag(value) == 43.0,
           "failed second complex component preserves entire destination");

    char invalid_logical[] = "1";
    bool logical = false;
    expect(f2c_stream_initialize_internal(&stream, invalid_logical, sizeof(invalid_logical) - 1U,
                                          1U, true),
           "invalid logical stream initializes");
    expect(f2c_read_bool(&stream, &logical) == 0 && !logical,
           "numeric logical extension is rejected without assignment");

    char invalid_character[] = "'unclosed";
    char character[16];
    expect(f2c_stream_initialize_internal(&stream, invalid_character,
                                          sizeof(invalid_character) - 1U, 1U, true),
           "invalid character stream initializes");
    expect(f2c_read_character(&stream, character, sizeof(character)) == 0,
           "unclosed list character quotes report an input error");
}

static void test_control_scopes(void) {
    char record[32];
    f2c_io_stream stream;
    f2c_io_control_scope outer = {0}, inner = {0};
    expect(f2c_stream_initialize_internal(&stream, record, sizeof(record), 1U, false),
           "scoped control stream initializes");
    expect(f2c_io_enter_controls(&outer, &stream, -999, "comma", 5U, "up", 2U, "plus", 4U, "quote",
                                 5U),
           "parent controls enter");
    expect(f2c_io_enter_controls(&inner, &stream, -999, "point", 5U, "invalid", 7U, NULL, 0U, NULL,
                                 0U) == false &&
               stream.controls.decimal_comma && stream.controls.rounding == F2C_FORMAT_ROUND_UP,
           "invalid multi-control override changes no active mode");
    f2c_io_leave_controls(&inner);
    expect(stream.controls_active && stream.controls.decimal_comma &&
               stream.controls.sign == F2C_SIGN_PLUS && stream.controls.delim == F2C_DELIM_QUOTE,
           "failed nested override restores parent controls");
    f2c_io_leave_controls(&outer);
    expect(!stream.controls_active && !stream.controls.decimal_comma &&
               stream.controls.rounding == F2C_FORMAT_ROUND_PROCESSOR &&
               stream.controls.sign == F2C_SIGN_PROCESSOR &&
               stream.controls.delim == F2C_DELIM_NONE,
           "outer scope restores connection defaults");
}

static void test_invalid_and_wide_input(void) {
    static const char *const invalid[] = {"",      "1e",  "1e+",    "0x1p0",
                                          "1.2.3", "1 2", "1e9999", "nan(payload)"};
    char *wide = (char *)malloc(32771U);
    double value = 42.0;
    expect(wide != NULL, "wide field allocation succeeds");
    if (wide == NULL)
        return;
    memset(wide, '0', 32768U);
    memcpy(wide + 32768U, ".1", 3U);
    expect(f2c_io_parse_real(wide, false, 8, F2C_FORMAT_ROUND_UP, &value) == 1 &&
               real_bits(value, 8) == UINT64_C(0x3fb999999999999a),
           "long real input is not truncated");
    free(wide);
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        value = 42.0;
        expect(f2c_io_parse_real(invalid[index], false, 8, F2C_FORMAT_ROUND_NEAREST, &value) == 0,
               "invalid numeric field is rejected");
        expect(value == 42.0, "failed scalar conversion preserves destination");
    }
    expect(f2c_io_parse_real(" 1,25D+1 ", true, 8, F2C_FORMAT_ROUND_NEAREST, &value) == 1 &&
               value == 12.5,
           "decimal comma and D exponent normalize without global locale changes");
    expect(f2c_io_parse_real("1.25-1", false, 8, F2C_FORMAT_ROUND_NEAREST, &value) == 1 &&
               value == 0.125,
           "implicit exponent parses through the common conversion path");
}

static void test_output_rounding(void) {
    char record[64];
    f2c_io_stream stream;
    const int original = fegetround();
    expect(f2c_io_binary_halfway(1.25, 1, false), "fixed decimal exact midpoint is detected");
    expect(f2c_io_binary_halfway(125.0, 2, true), "significant decimal exact midpoint is detected");
    expect(!f2c_io_binary_halfway(1.35, 1, false),
           "near midpoint is not mistaken for an exact midpoint");
    if (fesetround(FE_DOWNWARD) == 0) {
        memset(record, ' ', sizeof(record));
        expect(f2c_stream_initialize_internal(&stream, record, sizeof(record), 1U, false),
               "output stream initializes");
        stream.controls.rounding = F2C_FORMAT_ROUND_COMPATIBLE;
        f2c_io_write_real(&stream, -125.0, 2);
        expect(memcmp(record, "-1.3e+02", 8U) == 0,
               "compatible output exact midpoint rounds away from zero");
        expect(fegetround() == FE_DOWNWARD, "output restores caller rounding mode");
    }
    if (original >= 0)
        (void)fesetround(original);
}

static void test_locale_interoperation(void) {
    static const char *const candidates[] = {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "fr_FR",
                                             "German_Germany.1252"};
    const char *original = setlocale(LC_NUMERIC, NULL);
    const size_t original_length = original != NULL ? strlen(original) : 0U;
    char *saved = (char *)malloc(original_length + 1U);
    char record[32];
    f2c_io_stream stream;
    double value;
    bool exercised = false;
    expect(original != NULL && saved != NULL, "numeric locale can be saved by the test driver");
    if (original == NULL || saved == NULL) {
        free(saved);
        return;
    }
    memcpy(saved, original, original_length + 1U);
    for (size_t index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (setlocale(LC_NUMERIC, candidates[index]) == NULL ||
            strcmp(localeconv()->decimal_point, ",") != 0)
            continue;
        exercised = true;
        expect(f2c_io_parse_real("1.25", false, 8, F2C_FORMAT_ROUND_NEAREST, &value) == 1 &&
                   value == 1.25,
               "POINT input uses the existing comma C locale without changing it");
        expect(f2c_io_parse_real("1,25", true, 4, F2C_FORMAT_ROUND_NEAREST, &value) == 1 &&
                   value == 1.25,
               "COMMA input uses the existing comma C locale without changing it");
        memset(record, ' ', sizeof(record));
        expect(f2c_stream_initialize_internal(&stream, record, sizeof(record), 1U, false),
               "localized output stream initializes");
        f2c_io_write_real(&stream, 1.25, 3);
        expect(memcmp(record, "1.25", 4U) == 0, "POINT output does not expose the C locale radix");
        stream.controls.decimal_comma = true;
        f2c_io_write_real(&stream, 1.25, 3);
        expect(memcmp(record + 4U, "1,25", 4U) == 0, "COMMA output preserves its Fortran radix");
        expect(strcmp(localeconv()->decimal_point, ",") == 0,
               "conversion helpers do not alter the caller numeric locale");
        break;
    }
    if (!exercised)
        fprintf(stderr, "NOTE: comma LC_NUMERIC locale unavailable; locale matrix not exercised\n");
    expect(setlocale(LC_NUMERIC, saved) != NULL, "test driver restores original numeric locale");
    free(saved);
}

int main(void) {
    test_rounding();
    test_invalid_and_wide_input();
    test_shared_list_fields();
    test_control_scopes();
    test_output_rounding();
    test_locale_interoperation();
    return failures == 0 ? 0 : 1;
}
