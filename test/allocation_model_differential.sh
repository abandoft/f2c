#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

F2C=$1
CC=${CC:-cc}
FC=${FC:-gfortran}
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/allocation-model-differential
SOURCES="allocation_model allocation_model_rich allocation_result_values"

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

for NAME in $SOURCES; do
    SOURCE=$ROOT/test/fixtures/$NAME.f90
    "$F2C" "$SOURCE" -o "$WORK/$NAME.c"
    "$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/$NAME.c" -lm \
        -o "$WORK/$NAME-generated"
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$WORK/$NAME.c" -lm -o "$WORK/$NAME-sanitized"
    "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror -J"$WORK" -I"$WORK" \
        "$SOURCE" -o "$WORK/$NAME-native"

    "$WORK/$NAME-generated" >"$WORK/$NAME-generated.out"
    "$WORK/$NAME-sanitized" >"$WORK/$NAME-sanitized.out"
    "$WORK/$NAME-native" >"$WORK/$NAME-native.out"

    if ! cmp -s "$WORK/$NAME-generated.out" "$WORK/$NAME-native.out"; then
        echo "generated/native ALLOCATE model behavior mismatch: $NAME" >&2
        diff -u "$WORK/$NAME-native.out" "$WORK/$NAME-generated.out" >&2 || true
        exit 1
    fi
    if ! cmp -s "$WORK/$NAME-generated.out" "$WORK/$NAME-sanitized.out"; then
        echo "optimized/sanitized ALLOCATE model output mismatch: $NAME" >&2
        diff -u "$WORK/$NAME-generated.out" "$WORK/$NAME-sanitized.out" >&2 || true
        exit 1
    fi
done

echo "ALLOCATE SOURCE/MOLD differential and sanitizer validation passed"
