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
WORK=$ROOT/build/intrinsic-result-differential
cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"
MODEL=$ROOT/test/fixtures/intrinsic_result_model.f90
DRIVER=$ROOT/test/fixtures/intrinsic_result_model_main.f90
"$F2C" "$MODEL" "$DRIVER" -o "$WORK/generated.c"
"$F2C" "$MODEL" -o "$WORK/intrinsic-result-model.c" --header "$WORK/intrinsic-result-model.h"
"$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror -J "$WORK" \
    "$MODEL" "$DRIVER" -o "$WORK/native"
"$WORK/native" > "$WORK/native.out"
for PROFILE in 0 2 3 sanitized; do
    OPTIMIZATION="-O$PROFILE"
    INSTRUMENTATION=
    if [ "$PROFILE" = sanitized ]; then
        OPTIMIZATION=-O1
        INSTRUMENTATION='-g -fsanitize=address,undefined -fno-sanitize-recover=all'
    fi
    # The instrumentation variable deliberately expands to individual flags.
    "$CC" -std=c17 "$OPTIMIZATION" $INSTRUMENTATION -Wall -Wextra -Wpedantic \
        -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror \
        "$WORK/generated.c" -lm -o "$WORK/generated-$PROFILE"
    "$WORK/generated-$PROFILE" > "$WORK/generated-$PROFILE.out"
    cmp "$WORK/native.out" "$WORK/generated-$PROFILE.out"
    "$CC" -std=c17 "$OPTIMIZATION" $INSTRUMENTATION -Wall -Wextra -Wpedantic \
        -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror -I "$WORK" \
        "$WORK/intrinsic-result-model.c" "$ROOT/test/generated/intrinsic_result_contract.c" \
        -lm -o "$WORK/contract-$PROFILE"
    "$WORK/contract-$PROFILE"
done
echo 'intrinsic result models, native generic selection and independent ABI checks passed'
