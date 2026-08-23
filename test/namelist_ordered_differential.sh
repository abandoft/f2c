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
WORK=$ROOT/build/namelist-ordered
SOURCE=$ROOT/test/fixtures/namelist_ordered.f90

for compiler in "$CC" "$FC"; do
    if ! command -v "$compiler" >/dev/null 2>&1; then
        echo "compiler not found: $compiler" >&2
        exit 2
    fi
done

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

"$FC" -std=f2018 -O2 -Wall -Wextra -Werror "$SOURCE" -o "$WORK/native"
"$F2C" "$SOURCE" -o "$WORK/generated.c"
"$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
    -o "$WORK/generated"
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -fno-sanitize-recover=all "$WORK/generated.c" -lm \
    -o "$WORK/generated-sanitized"

"$WORK/native" >"$WORK/native.out"
"$WORK/generated" >"$WORK/generated.out"
cmake -E compare_files "$WORK/native.out" "$WORK/generated.out"

asan_leaks=1
if [ "$(uname -s)" = Darwin ]; then
    asan_leaks=0
fi
ASAN_OPTIONS=detect_leaks=$asan_leaks:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/generated-sanitized" >"$WORK/sanitized.out"
cmake -E compare_files "$WORK/native.out" "$WORK/sanitized.out"

echo "ordered NAMELIST differential validation passed"
