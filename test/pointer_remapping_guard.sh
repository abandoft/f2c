#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

F2C=$1
CC=${CC:-cc}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/pointer-remapping-guard
SOURCE=$ROOT/test/fixtures/pointer_remapping_extent_error.f90
CONTIGUOUS_SOURCE=$ROOT/test/fixtures/contiguous_pointer_error.f90

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
    echo "undersized rank-remapping target was not rejected" >&2
    exit 1
fi

"$F2C" "$CONTIGUOUS_SOURCE" -o "$WORK/generated-contiguous.c"
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror \
    "$WORK/generated-contiguous.c" -lm -o "$WORK/generated-contiguous"

if "$WORK/generated-contiguous" >"$WORK/generated-contiguous.out" \
    2>"$WORK/generated-contiguous.err"; then
    echo "noncontiguous target was accepted by a CONTIGUOUS pointer" >&2
    exit 1
fi

echo "pointer remapping and CONTIGUOUS runtime guard validation passed"
