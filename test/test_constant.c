#include "internal/f2c.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static Symbol *add_parameter(Symbol *symbols, size_t index, const char *name,
                             const char *initializer) {
    Symbol *symbol = &symbols[index];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = (char *)name;
    symbol->c_name = (char *)name;
    symbol->type = TYPE_INTEGER;
    symbol->parameter = 1;
    symbol->initializer = (char *)initializer;
    return symbol;
}

int main(void) {
    Symbol symbols[3];
    Unit unit;
    int64_t value = 0;
    memset(&unit, 0, sizeof(unit));
    add_parameter(symbols, 0U, "base", "4");
    add_parameter(symbols, 1U, "cycle_a", "cycle_b");
    add_parameter(symbols, 2U, "cycle_b", "cycle_a");
    unit.symbols = symbols;
    unit.symbol_count = 3U;

    expect(f2c_evaluate_integer_text(&unit, "base*2+3**2", &value) && value == 17,
           "parameter arithmetic and integer powers are folded");
    expect(f2c_evaluate_integer_text(&unit, "len('A''B')+max(2,5)", &value) && value == 8,
           "character LEN and variadic MAX are folded");
    expect(!f2c_evaluate_integer_text(&unit, "9223372036854775807+1", &value),
           "constant addition overflow is rejected");
    expect(!f2c_evaluate_integer_text(&unit, "1/0", &value),
           "constant division by zero is rejected");
    expect(!f2c_evaluate_integer_text(&unit, "cycle_a", &value),
           "cyclic parameter references terminate with failure");

    if (failures != 0) {
        fprintf(stderr, "%d constant-evaluation test(s) failed\n", failures);
        return 1;
    }
    puts("all constant-evaluation tests passed");
    return 0;
}
