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
WORK=$ROOT/build/operator-differential
SOURCE=$ROOT/test/fixtures/operator_kinds.f90
cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

"$F2C" "$SOURCE" -o "$WORK/generated.c"
"$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror -Wno-compare-reals \
    -Wno-conversion -J "$WORK" -I "$WORK" "$SOURCE" -o "$WORK/native"
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
    -fno-sanitize-recover=all "$WORK/generated.c" -lm -o "$WORK/generated-sanitized"
"$WORK/generated-sanitized" > "$WORK/generated-sanitized.out"
cmp "$WORK/native.out" "$WORK/generated-sanitized.out"

# Processor representation/invalid-operation contracts are independent of the
# native oracle. A C client supplies noncanonical nonzero logical payloads and
# invokes the public generated ABI, rather than relying on the translator to
# construct both sides of the test.
"$F2C" "$ROOT/test/fixtures/operator_contract.f90" \
    -o "$WORK/operator-contract.c" --header "$WORK/operator-contract.h"
for OPTIMIZATION in 0 2 3; do
    "$CC" -std=c17 "-O$OPTIMIZATION" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -I "$WORK" \
        "$WORK/operator-contract.c" "$ROOT/test/generated/operator_contract.c" -lm \
        -o "$WORK/operator-contract-$OPTIMIZATION"
    "$WORK/operator-contract-$OPTIMIZATION"
done
"$CC" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I "$WORK" "$WORK/operator-contract.c" \
    "$ROOT/test/generated/operator_contract.c" -lm -o "$WORK/operator-contract-sanitized"
"$WORK/operator-contract-sanitized"
ulimit -c 0
for FAILURE in overflow narrow zero; do
    STATUS=0
    "$WORK/operator-contract-sanitized" "$FAILURE" > "$WORK/guard-$FAILURE.out" \
        2> "$WORK/guard-$FAILURE.err" || STATUS=$?
    if [ "$STATUS" -ne 134 ]; then
        echo "expected SIGABRT for $FAILURE, got exit status $STATUS" >&2
        exit 1
    fi
    if grep -Eq 'runtime error:|ERROR: AddressSanitizer|ERROR: UndefinedBehaviorSanitizer' \
        "$WORK/guard-$FAILURE.err"; then
        echo "sanitizer error before $FAILURE guard" >&2
        exit 1
    fi
done
echo "operator kind, power, precedence and evaluation differential passed"
