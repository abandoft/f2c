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
for name in character_designators character_array_actuals scalar_component_actuals \
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
# This contract regression asserts single evaluation, independently of an oracle.
# Local GNU Fortran 16.1 evaluates these array substring bounds more than once;
# value/result differential tests above do not infer correctness from that behavior.
name=character_actual_evaluation
"$f2c" "$root/test/fixtures/$name.f90" -o "$work/$name.c"
"$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all "$work/$name.c" -lm -o "$work/$name"
"$work/$name"
echo "character designator and nested array reduction differential passed"
