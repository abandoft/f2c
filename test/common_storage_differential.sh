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
WORK=$ROOT/build/common-storage-differential
SOURCE=$ROOT/test/fixtures/common_storage.f90

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
"$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
    -o "$WORK/generated"
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all "$WORK/generated.c" -lm -o "$WORK/generated-sanitized"
"$FC" -std=legacy -pedantic -O2 -Wall -Wextra "$SOURCE" -o "$WORK/native"

"$WORK/generated" >"$WORK/generated.out"
"$WORK/generated-sanitized" >"$WORK/generated-sanitized.out"
"$WORK/native" >"$WORK/native.out"

if ! cmp -s "$WORK/generated.out" "$WORK/native.out"; then
    echo "generated/native COMMON storage output mismatch" >&2
    diff -u "$WORK/native.out" "$WORK/generated.out" >&2 || true
    exit 1
fi
if ! cmp -s "$WORK/generated.out" "$WORK/generated-sanitized.out"; then
    echo "optimized/sanitized COMMON storage output mismatch" >&2
    diff -u "$WORK/generated.out" "$WORK/generated-sanitized.out" >&2 || true
    exit 1
fi

echo "COMMON storage differential and sanitizer validation passed"
