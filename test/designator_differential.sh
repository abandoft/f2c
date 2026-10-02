#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/designator-differential
f2c=$1
cc=${CC:-cc}
fc=${FC:-gfortran}
cmake -E remove_directory "$work"
cmake -E make_directory "$work"
for name in character_designators character_array_actuals character_length_parameters \
    scalar_component_actuals \
    procedure_pointer_component reduction_designators nested_array_reductions; do
    source=$root/test/fixtures/$name.f90
    "$f2c" "$source" -o "$work/$name.c"
    "$cc" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/$name.c" -lm -o "$work/$name"
    "$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$work/$name.c" -lm -o "$work/$name-sanitized"
    # Exact-value comparisons and fixed-length truncation are intentional test cases.
    "$fc" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
        -Wno-compare-reals -Wno-character-truncation \
        -J"$work" "$source" -o "$work/$name-native"
    "$work/$name" >"$work/$name.out"
    "$work/$name-sanitized" >"$work/$name-sanitized.out"
    "$work/$name-native" >"$work/$name-native.out"
    cmp "$work/$name.out" "$work/$name-native.out"
    cmp "$work/$name.out" "$work/$name-sanitized.out"
done
"$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I"$work" \
    "$root/test/fixtures/character_length_width_harness.c" -lm -o "$work/character-length-width"
"$work/character-length-width" >"$work/character-length-width.out"
cmp "$work/character-length-width.out" "$work/character_length_parameters-native.out"
# Independent contracts: GNU Fortran 16.1 repeats certain substring bounds and
# retains deferred array length on scalar assignment, contrary to F2018
# 10.2.1.3(3). Differential checks above do not inherit those oracle behaviors.
for name in character_actual_evaluation character_broadcast_reallocation; do
    "$f2c" "$root/test/fixtures/$name.f90" -o "$work/$name.c"
    "$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$work/$name.c" -lm -o "$work/$name"
    "$work/$name"
done
echo "character designator and nested array reduction differential passed"
