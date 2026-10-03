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
WORK=$ROOT/build/function-result-differential

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

for CASE in function_result scalar_result result_identity result_snapshot result_kinds; do
    SOURCE=$ROOT/test/fixtures/$CASE.f90
    CASE_WORK=$WORK/$CASE
    cmake -E make_directory "$CASE_WORK"
    "$F2C" "$SOURCE" -o "$CASE_WORK/generated.c"
    "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror -Wno-surprising \
        -Wno-aggressive-loop-optimizations -J"$CASE_WORK" -I"$CASE_WORK" "$SOURCE" \
        -o "$CASE_WORK/native"
    "$CASE_WORK/native" >"$CASE_WORK/native.out"
    for OPTIMIZATION in 0 2 3; do
        "$CC" -std=c17 -O"$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -Werror "$CASE_WORK/generated.c" -lm \
            -o "$CASE_WORK/generated-O$OPTIMIZATION"
        "$CASE_WORK/generated-O$OPTIMIZATION" >"$CASE_WORK/generated-O$OPTIMIZATION.out"
        if ! cmp -s "$CASE_WORK/generated-O$OPTIMIZATION.out" "$CASE_WORK/native.out"; then
            echo "$CASE: generated/native result mismatch at O$OPTIMIZATION" >&2
            diff -u "$CASE_WORK/native.out" "$CASE_WORK/generated-O$OPTIMIZATION.out" >&2 || true
            exit 1
        fi
    done
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$CASE_WORK/generated.c" -lm -o "$CASE_WORK/sanitized"
    "$CASE_WORK/sanitized" >"$CASE_WORK/sanitized.out"
    if ! cmp -s "$CASE_WORK/native.out" "$CASE_WORK/sanitized.out"; then
        echo "$CASE: native/sanitized result mismatch" >&2
        diff -u "$CASE_WORK/native.out" "$CASE_WORK/sanitized.out" >&2 || true
        exit 1
    fi
done

"$F2C" "$ROOT/test/fixtures/result_external.f90" -o "$WORK/external.c"
for OPTIMIZATION in 0 2 3; do
    "$CC" -std=c17 -O"$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -DF2C_RESULT_SOURCE="\"$WORK/external.c\"" \
        "$ROOT/test/generated/result_contracts.c" -lm -o "$WORK/external-O$OPTIMIZATION"
    "$WORK/external-O$OPTIMIZATION"
    for CONTRACT in rank size ownership allocation retention_growth mold_association value_association; do
        STATUS=0
        "$WORK/external-O$OPTIMIZATION" "$CONTRACT" || STATUS=$?
        if [ "$STATUS" -ne 99 ]; then
            echo "result contract failed to abort: $CONTRACT ($STATUS)" >&2
            exit 1
        fi
    done
done
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all -DF2C_RESULT_SOURCE="\"$WORK/external.c\"" \
    "$ROOT/test/generated/result_contracts.c" -lm -o "$WORK/external-sanitized"
"$WORK/external-sanitized"

echo "function-result differential and sanitizer validation passed"
