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
WORK=$ROOT/build/iolength-differential
SOURCE=$ROOT/test/fixtures/iolength.f90

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler not found: $CC" >&2
    exit 2
fi
if ! command -v "$FC" >/dev/null 2>&1; then
    echo "Fortran compiler not found: $FC" >&2
    exit 2
fi

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK/generated" "$WORK/native"

"$F2C" "$SOURCE" -o "$WORK/generated/program.c"
"$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated/program.c" -lm \
    -o "$WORK/generated/program"
"$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror -J "$WORK/native" "$SOURCE" \
    -o "$WORK/native/program"

(cd "$WORK/generated" && ./program > program.out)
(cd "$WORK/native" && ./program > program.out)

if ! cmp -s "$WORK/generated/program.out" "$WORK/native/program.out"; then
    echo "generated/native INQUIRE(IOLENGTH=) output mismatch" >&2
    diff -u "$WORK/native/program.out" "$WORK/generated/program.out" >&2 || true
    exit 1
fi

echo "INQUIRE(IOLENGTH=) differential passed"
