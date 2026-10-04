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
BASE=$ROOT/build/reduction-intrinsic-differential

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler not found: $CC" >&2
    exit 2
fi
if ! command -v "$FC" >/dev/null 2>&1; then
    echo "Fortran compiler not found: $FC" >&2
    exit 2
fi

cmake -E remove_directory "$BASE"
for NAME in reduction_intrinsics volatile_reductions extremum_boundaries extremum_arguments extremum_policy; do
    WORK=$BASE/$NAME
    SOURCE=$ROOT/test/fixtures/$NAME.f90
    cmake -E make_directory "$WORK"

    "$F2C" "$SOURCE" -o "$WORK/generated.c"
    "$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
        -o "$WORK/generated"
    "$CC" -std=c17 -O0 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
        -o "$WORK/generated-unoptimized"
    "$CC" -std=c17 -O3 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
        -o "$WORK/generated-highly-optimized"
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$WORK/generated.c" -lm -o "$WORK/generated-sanitized"
    if [ "$NAME" != extremum_policy ]; then
        "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror -Wno-compare-reals \
            -Wno-conversion \
            -J "$WORK" -I "$WORK" "$SOURCE" -o "$WORK/native"
    fi

    "$WORK/generated" >"$WORK/generated.out"
    "$WORK/generated-unoptimized" >"$WORK/generated-unoptimized.out"
    cmp "$WORK/generated.out" "$WORK/generated-unoptimized.out"
    "$WORK/generated-highly-optimized" >"$WORK/generated-highly-optimized.out"
    cmp "$WORK/generated.out" "$WORK/generated-highly-optimized.out"
    "$WORK/generated-sanitized" >"$WORK/generated-sanitized.out"
    if [ "$NAME" != extremum_policy ]; then
        "$WORK/native" >"$WORK/native.out"
        if ! cmp -s "$WORK/generated.out" "$WORK/native.out"; then
            echo "generated/native reduction intrinsic output mismatch" >&2
            diff -u "$WORK/native.out" "$WORK/generated.out" >&2 || true
            exit 1
        fi
    fi
    if ! cmp -s "$WORK/generated.out" "$WORK/generated-sanitized.out"; then
        echo "optimized/sanitized reduction intrinsic output mismatch" >&2
        diff -u "$WORK/generated.out" "$WORK/generated-sanitized.out" >&2 || true
        exit 1
    fi

    if [ "$NAME" = extremum_policy ]; then
        echo "$NAME processor-contract, optimization and sanitizer validation passed (no native policy oracle)"
    else
        echo "$NAME differential and sanitizer validation passed"
    fi
done
