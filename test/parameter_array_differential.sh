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
WORK=$ROOT/build/parameter-array-differential
SOURCE=$ROOT/test/fixtures/parameter_array.f90
cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

"$F2C" "$SOURCE" -o "$WORK/generated.c"
"$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
    -J "$WORK" -I "$WORK" "$SOURCE" -o "$WORK/native"
"$WORK/native" > "$WORK/native.out"
for OPTIMIZATION in 0 2 3; do
    "$CC" -std=c17 "-O$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/generated.c" -lm \
        -o "$WORK/generated-$OPTIMIZATION"
    "$WORK/generated-$OPTIMIZATION" > "$WORK/generated-$OPTIMIZATION.out"
    cmp "$WORK/native.out" "$WORK/generated-$OPTIMIZATION.out"
done
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all "$WORK/generated.c" -lm -o "$WORK/sanitized"
"$WORK/sanitized" > "$WORK/sanitized.out"
cmp "$WORK/native.out" "$WORK/sanitized.out"
echo "typed parameter-array storage and association differential passed"
