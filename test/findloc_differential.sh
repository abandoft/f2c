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
WORK=$ROOT/build/findloc-differential
cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

for FIXTURE in findloc_runtime findloc_models; do
    SOURCE=$ROOT/test/fixtures/$FIXTURE.f90
    CASE=$WORK/$FIXTURE
    cmake -E make_directory "$CASE"
    "$F2C" "$SOURCE" -o "$CASE/generated.c"
    if [ "$FIXTURE" = findloc_runtime ]; then
        # GNU 16 mishandles BACK with negative-stride MASK. Only the paired
        # native profile densifies those operands; f2c retains the actual views.
        "$FC" -cpp -DF2C_NATIVE_ORACLE_PROFILE -std=f2018 -pedantic-errors -O2 \
            -Wall -Wextra -Werror -J "$CASE" -I "$CASE" "$SOURCE" -o "$CASE/native"
        "$CASE/native" > "$CASE/expected.out"
    else
        # GNU 16 also misfolds valid mixed numeric FINDLOC constants. This
        # profile asserts independently specified results, never GNU's zeros.
        printf '%s\n' 'FINDLOC independent constant model contracts passed' > "$CASE/expected.out"
    fi
    for OPTIMIZATION in 0 2 3; do
        "$CC" -std=c17 "-O$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -Werror "$CASE/generated.c" -lm \
            -o "$CASE/generated-$OPTIMIZATION"
        "$CASE/generated-$OPTIMIZATION" > "$CASE/generated-$OPTIMIZATION.out"
        cmp "$CASE/expected.out" "$CASE/generated-$OPTIMIZATION.out"
    done
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all "$CASE/generated.c" -lm \
        -o "$CASE/sanitized"
    "$CASE/sanitized" > "$CASE/sanitized.out"
    cmp "$CASE/expected.out" "$CASE/sanitized.out"
done

"$F2C" "$ROOT/test/fixtures/findloc_contract.f90" -o "$WORK/findloc-contract.c" \
    --header "$WORK/findloc-contract.h"
for OPTIMIZATION in 0 2 3; do
    "$CC" -std=c17 "-O$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -I "$WORK" "$WORK/findloc-contract.c" \
        "$ROOT/test/generated/findloc_contract.c" -lm -o "$WORK/contract-$OPTIMIZATION"
    "$WORK/contract-$OPTIMIZATION"
done
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I "$WORK" "$WORK/findloc-contract.c" \
    "$ROOT/test/generated/findloc_contract.c" -lm -o "$WORK/contract-sanitized"
"$WORK/contract-sanitized"
ulimit -c 0
for FAILURE in dimension-wide dimension-zero dimension-negative mask-shape result-kind; do
    STATUS=0
    "$WORK/contract-sanitized" "$FAILURE" > "$WORK/$FAILURE.out" 2> "$WORK/$FAILURE.err" || STATUS=$?
    if [ "$STATUS" -ne 134 ] || \
       grep -Eq 'runtime error:|ERROR: AddressSanitizer|ERROR: UndefinedBehaviorSanitizer' "$WORK/$FAILURE.err"; then
        echo "$FAILURE did not stop with a clean checked SIGABRT: $STATUS" >&2
        exit 1
    fi
done
echo 'FINDLOC runtime differential, independent models and checked contracts passed'
