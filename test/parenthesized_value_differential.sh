#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/parenthesized-value-differential
cc=${CC:-cc}
fc=${FC:-gfortran}
cmake -E remove_directory "$work"
cmake -E make_directory "$work"

for name in parenthesized_intrinsic_values parenthesized_values parenthesized_character_values; do
    "$1" "$root/test/fixtures/$name.f90" -o "$work/$name.c"
    for optimization in 0 2; do
        "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/$name.c" -lm \
            -o "$work/$name-O$optimization"
        "$work/$name-O$optimization" > "$work/$name-O$optimization.out"
    done
    "$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$work/$name.c" -lm -o "$work/$name-sanitized"
    "$work/$name-sanitized" > "$work/$name-sanitized.out"
    cmp "$work/$name-O0.out" "$work/$name-O2.out"
    cmp "$work/$name-O2.out" "$work/$name-sanitized.out"
    "$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all -I"$work" \
        "-DF2C_PARENTHESIZED_SOURCE=\"$name.c\"" \
        "$root/test/generated/parenthesized_ownership.c" -lm -o "$work/$name-ownership"
    "$work/$name-ownership" > "$work/$name-ownership.out"
    cmp "$work/$name-O2.out" "$work/$name-ownership.out"
    case "$name" in
        parenthesized_intrinsic_values) marker='parenthesized intrinsic contracts passed' ;;
        parenthesized_values) marker='parenthesized value contracts passed' ;;
        parenthesized_character_values) marker='parenthesized character contracts passed' ;;
    esac
    printf '%s\n' "$marker" > "$work/$name-expected.out"
    cmp "$work/$name-O2.out" "$work/$name-expected.out"
done

# These numeric/logical/complex snapshots and inquiries are also checked against
# native Fortran at both optimization levels, without rewriting the expressions.
for optimization in 0 2; do
    "$fc" -std=f2018 -pedantic-errors "-O$optimization" -Wall -Wextra -Werror \
        -Wno-compare-reals -J"$work" "$root/test/fixtures/parenthesized_intrinsic_values.f90" \
        -o "$work/native-O$optimization"
    "$work/native-O$optimization" > "$work/native-O$optimization.out"
    cmp "$work/parenthesized_intrinsic_values-O$optimization.out" "$work/native-O$optimization.out"
done
# Character and allocatable derived-component snapshots have independent
# executable contracts: GNU Fortran 16.1 fails their original-source assertions
# (STOP 10 and STOP 29, respectively), so it is not an oracle for those cases.
echo "parenthesized value native differential and ownership contracts passed"
