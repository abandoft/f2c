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
WORK=$ROOT/build/loop-differential
cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

for FIXTURE in loop_control loop_storage loop_result_bounds loop_names loop_inquire loop_data loop_real_controls loop_legacy; do
    EXTENSION=f90
    STANDARD=f2018
    CONVERSION_WARNING=-Wconversion
    if [ "$FIXTURE" = loop_legacy ]; then EXTENSION=f; STANDARD=legacy; fi
    # The mixed-width EQUIVALENCE layout is an explicit processor extension,
    # independently checked as a storage contract, not strict-F2018 conformance.
    if [ "$FIXTURE" = loop_storage ]; then STANDARD=legacy; fi
    # Fractional legacy controls intentionally exercise the numeric conversion.
    # Suppress only that expected warning for this dedicated source profile.
    if [ "$FIXTURE" = loop_real_controls ]; then
        STANDARD=legacy
        CONVERSION_WARNING=-Wno-conversion
    fi
    SOURCE=$ROOT/test/fixtures/$FIXTURE.$EXTENSION
    "$F2C" "$SOURCE" -o "$WORK/$FIXTURE.c"
    "$FC" "-std=$STANDARD" -O2 -Wall -Wextra -Werror -Wno-zerotrip "$CONVERSION_WARNING" \
        -J "$WORK" -I "$WORK" "$SOURCE" -o "$WORK/$FIXTURE-native"
    (cd "$WORK" && "./$FIXTURE-native") > "$WORK/$FIXTURE-native.out"
    for OPTIMIZATION in 0 2 3; do
        "$CC" -std=c17 "-O$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -Werror "$WORK/$FIXTURE.c" -lm \
            -o "$WORK/$FIXTURE-$OPTIMIZATION"
        (cd "$WORK" && "./$FIXTURE-$OPTIMIZATION") > "$WORK/$FIXTURE-$OPTIMIZATION.out"
        cmp "$WORK/$FIXTURE-native.out" "$WORK/$FIXTURE-$OPTIMIZATION.out"
    done
    "$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$WORK/$FIXTURE.c" -lm -o "$WORK/$FIXTURE-sanitized"
    (cd "$WORK" && "./$FIXTURE-sanitized") > "$WORK/$FIXTURE-sanitized.out"
    cmp "$WORK/$FIXTURE-native.out" "$WORK/$FIXTURE-sanitized.out"
done

# Do not run native Fortran outside its defined integer model. A separate C
# caller checks full signed-storage intervals, INT64_MIN steps, exact ABI widths,
# and f2c's explicit modular post-loop policy against independent expected values.
"$F2C" "$ROOT/test/fixtures/loop_contract.f90" -o "$WORK/loop-contract.c" \
    --header "$WORK/loop-contract.h"
for OPTIMIZATION in 0 2 3; do
    "$CC" -std=c17 "-O$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -I "$WORK" "$WORK/loop-contract.c" \
        "$ROOT/test/generated/loop_contract.c" -lm -o "$WORK/loop-contract-$OPTIMIZATION"
    "$WORK/loop-contract-$OPTIMIZATION"
done
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I "$WORK" "$WORK/loop-contract.c" \
    "$ROOT/test/generated/loop_contract.c" -lm -o "$WORK/loop-contract-sanitized"
"$WORK/loop-contract-sanitized"
ulimit -c 0
for FAILURE in zero integer-range real-range real-nan real-inf; do
    STATUS=0
    "$WORK/loop-contract-sanitized" "$FAILURE" > "$WORK/$FAILURE.out" 2> "$WORK/$FAILURE.err" || STATUS=$?
    if [ "$STATUS" -ne 134 ] || grep -Eq 'runtime error:|ERROR: AddressSanitizer|ERROR: UndefinedBehaviorSanitizer' "$WORK/$FAILURE.err"; then
        echo "$FAILURE did not stop with a clean checked SIGABRT: $STATUS" >&2
        exit 1
    fi
done
echo "integer loop controls, I/O and independent ABI differential passed"
