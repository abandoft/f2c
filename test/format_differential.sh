#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

f2c=$1
cc=${CC:-cc}
fc=${FC:-gfortran}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/format-differential
source=$root/test/fixtures/format_matrix.f90

if ! command -v "$cc" >/dev/null 2>&1; then
    echo "C compiler not found: $cc" >&2
    exit 2
fi
if ! command -v "$fc" >/dev/null 2>&1; then
    echo "Fortran compiler not found: $fc" >&2
    exit 2
fi

cmake -E remove_directory "$work"
cmake -E make_directory "$work"

"$f2c" "$source" -o "$work/generated.c"
"$cc" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/generated.c" -lm \
    -o "$work/generated"
"$fc" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror "$source" \
    -o "$work/native"

"$work/generated" >"$work/generated.out"
"$work/native" >"$work/native.out"
if ! cmp -s "$work/generated.out" "$work/native.out"; then
    echo "generated/native FORMAT descriptor output mismatch" >&2
    diff -u "$work/native.out" "$work/generated.out" >&2 || true
    exit 1
fi

echo "FORMAT descriptor differential passed"
