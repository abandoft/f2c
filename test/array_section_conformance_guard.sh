#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

F2C=$1
CC=${CC:-cc}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/array-section-conformance-guard
SOURCE=$ROOT/test/fixtures/array_section_conformance_error.f90

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler not found: $CC" >&2
    exit 2
fi

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

"$F2C" "$SOURCE" -o "$WORK/generated.c"
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror \
    "$WORK/generated.c" -lm -o "$WORK/generated"

if "$WORK/generated" >"$WORK/generated.out" 2>"$WORK/generated.err"; then
    echo "nonconformable dynamic array-section assignment was not rejected" >&2
    exit 1
fi

echo "dynamic array-section conformance guard passed"
