#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

F2C=$1
CC=${CC:-cc}
FC=${FC:-gfortran}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/pointer-association-differential

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler not found: $CC" >&2
    exit 2
fi
if ! command -v "$FC" >/dev/null 2>&1; then
    echo "Fortran compiler not found: $FC" >&2
    exit 2
fi

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

for fixture in pointer_section derived_pointer_component; do
    source=$ROOT/test/fixtures/$fixture.f90
    case_work=$WORK/$fixture
    cmake -E make_directory "$case_work"

    "$F2C" "$source" -o "$case_work/generated.c"
    "$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$case_work/generated.c" -lm \
        -o "$case_work/generated"
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$case_work/generated.c" -lm \
        -o "$case_work/generated-sanitized"
    "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
        -J"$case_work" -I"$case_work" "$source" -o "$case_work/native"

    "$case_work/generated" >"$case_work/generated.out"
    "$case_work/generated-sanitized" >"$case_work/generated-sanitized.out"
    "$case_work/native" >"$case_work/native.out"

    if ! cmp -s "$case_work/generated.out" "$case_work/native.out"; then
        echo "generated/native pointer-association behavior mismatch: $fixture" >&2
        diff -u "$case_work/native.out" "$case_work/generated.out" >&2 || true
        exit 1
    fi
    if ! cmp -s "$case_work/generated.out" "$case_work/generated-sanitized.out"; then
        echo "optimized/sanitized pointer-association output mismatch: $fixture" >&2
        diff -u "$case_work/generated.out" "$case_work/generated-sanitized.out" >&2 || true
        exit 1
    fi
done

echo "pointer-association differential and sanitizer validation passed"
