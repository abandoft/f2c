#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

F2C=$1
CC=${CC:-cc}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/transform-ownership
SOURCE=$ROOT/test/fixtures/nested_transform_derived_ownership.f90

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler not found: $CC" >&2
    exit 2
fi

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

"$F2C" "$SOURCE" -o "$WORK/generated.c"
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer "$WORK/generated.c" -lm -o "$WORK/generated"
asan_leaks=1
if [ "$(uname -s)" = Darwin ]; then
    asan_leaks=0
fi
ASAN_OPTIONS=detect_leaks=$asan_leaks:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/generated"

echo "nested derived transform ownership passed"
