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

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler not found: $CC" >&2
    exit 2
fi

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

asan_leaks=1
if [ "$(uname -s)" = Darwin ]; then
    asan_leaks=0
fi

for fixture in nested_transform_derived_ownership transfer_derived_ownership; do
    source=$ROOT/test/fixtures/$fixture.f90
    case_work=$WORK/$fixture
    cmake -E make_directory "$case_work"
    "$F2C" "$source" -o "$case_work/generated.c"
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-omit-frame-pointer "$case_work/generated.c" -lm -o "$case_work/generated"
    ASAN_OPTIONS=detect_leaks=$asan_leaks:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$case_work/generated"
done

echo "derived transform ownership passed"
