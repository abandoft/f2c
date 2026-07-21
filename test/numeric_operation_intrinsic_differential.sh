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
WORK=$ROOT/build/numeric-operation-intrinsic-differential
SOURCE=$ROOT/test/fixtures/numeric_operation_intrinsics.f90
OWNERSHIP_SOURCE=$ROOT/test/fixtures/numeric_operation_derived_ownership.f90

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

"$F2C" "$SOURCE" -o "$WORK/generated.c"
"$F2C" "$OWNERSHIP_SOURCE" -o "$WORK/generated-ownership.c"
"$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
    -o "$WORK/generated"
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all "$WORK/generated.c" -lm -o "$WORK/generated-sanitized"
"$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated-ownership.c" -lm \
    -o "$WORK/generated-ownership"
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all "$WORK/generated-ownership.c" -lm \
    -o "$WORK/generated-ownership-sanitized"
# GCC 14 reports the descriptor fields of an allocatable component in a
# function result as uninitialized before standard automatic allocation.
# Preserve the diagnostic without making this compiler false positive fatal.
"$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror -Wno-error=uninitialized \
    -Wno-compare-reals \
    "$SOURCE" -o "$WORK/native"

"$WORK/generated" >"$WORK/generated.out"
"$WORK/generated-sanitized" >"$WORK/generated-sanitized.out"
"$WORK/generated-ownership" >"$WORK/generated-ownership.out"
"$WORK/generated-ownership-sanitized" >"$WORK/generated-ownership-sanitized.out"
"$WORK/native" >"$WORK/native.out"

if ! cmp -s "$WORK/generated.out" "$WORK/native.out"; then
    echo "generated/native numeric operation intrinsic output mismatch" >&2
    diff -u "$WORK/native.out" "$WORK/generated.out" >&2 || true
    exit 1
fi
if ! cmp -s "$WORK/generated.out" "$WORK/generated-sanitized.out"; then
    echo "optimized/sanitized numeric operation intrinsic output mismatch" >&2
    diff -u "$WORK/generated.out" "$WORK/generated-sanitized.out" >&2 || true
    exit 1
fi
if ! cmp -s "$WORK/generated-ownership.out" "$WORK/generated-ownership-sanitized.out"; then
    echo "optimized/sanitized derived ownership output mismatch" >&2
    diff -u "$WORK/generated-ownership.out" "$WORK/generated-ownership-sanitized.out" >&2 || true
    exit 1
fi

echo "numeric operation intrinsic differential and sanitizer validation passed"
