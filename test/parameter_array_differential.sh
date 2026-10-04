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
cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

for FIXTURE in parameter_array parameter_transform parameter_components; do
    SOURCE=$ROOT/test/fixtures/$FIXTURE.f90
    CASE=$WORK/$FIXTURE
    cmake -E make_directory "$CASE"
    "$F2C" "$SOURCE" -o "$CASE/generated.c"
    "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
        -J "$CASE" -I "$CASE" "$SOURCE" -o "$CASE/native"
    "$CASE/native" > "$CASE/native.out"
    for OPTIMIZATION in 0 2 3; do
        "$CC" -std=c17 "-O$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -Werror "$CASE/generated.c" -lm \
            -o "$CASE/generated-$OPTIMIZATION"
        "$CASE/generated-$OPTIMIZATION" > "$CASE/generated-$OPTIMIZATION.out"
        cmp "$CASE/native.out" "$CASE/generated-$OPTIMIZATION.out"
    done
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all "$CASE/generated.c" -lm \
        -o "$CASE/sanitized"
    "$CASE/sanitized" > "$CASE/sanitized.out"
    cmp "$CASE/native.out" "$CASE/sanitized.out"
done
echo "typed parameter-array storage, association and constant transformation differential passed"
