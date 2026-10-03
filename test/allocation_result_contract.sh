#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
F2C=$1
CC=${CC:-cc}
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/allocation-result-contract
cmake -E make_directory "$WORK"
# Keep the complete original evaluation and FINAL assertions. These contracts
# are separate from the raw-source native differential matrix.
for CASE in allocation_result allocation_result_finalization allocation_controls \
    allocation_control_values allocation_control_bounds allocation_control_finalization allocation_control_errors \
    allocation_control_unaligned allocation_control_pointers allocation_control_names; do
    CASE_WORK=$WORK/$CASE
    cmake -E make_directory "$CASE_WORK"
    "$F2C" "$ROOT/test/fixtures/$CASE.f90" -o "$CASE_WORK/generated.c"
    for OPTIMIZATION in 0 2 3; do
        "$CC" -std=c17 -O"$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -Werror "$CASE_WORK/generated.c" -lm \
            -o "$CASE_WORK/generated-O$OPTIMIZATION"
        "$CASE_WORK/generated-O$OPTIMIZATION" >"$CASE_WORK/generated-O$OPTIMIZATION.out"
        cmp "$CASE_WORK/generated-O$OPTIMIZATION.out" "$ROOT/test/fixtures/$CASE.out"
    done
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$CASE_WORK/generated.c" -lm -o "$CASE_WORK/sanitized"
    "$CASE_WORK/sanitized" >"$CASE_WORK/sanitized.out"
    cmp "$CASE_WORK/sanitized.out" "$ROOT/test/fixtures/$CASE.out"
done
echo "allocation result contract and sanitizer validation passed"
