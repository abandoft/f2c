#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/qualified-descriptor-differential
cc=${CC:-cc}
fc=${FC:-gfortran}
cmake -E remove_directory "$work"
cmake -E make_directory "$work"
"$1" "$root/test/fixtures/qualified_descriptors.f90" -o "$work/qualified.c"
for optimization in 0 2; do
    "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/qualified.c" -lm \
        -o "$work/generated-O$optimization"
    "$fc" -std=f2018 -pedantic-errors "-O$optimization" -Wall -Wextra -Werror \
        -Wno-compare-reals -J"$work" "$root/test/fixtures/qualified_descriptors.f90" \
        -o "$work/native-O$optimization"
    "$work/generated-O$optimization" > "$work/generated-O$optimization.out"
    "$work/native-O$optimization" > "$work/native-O$optimization.out"
    cmp "$work/generated-O$optimization.out" "$work/native-O$optimization.out"
done
"$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all "$work/qualified.c" -lm -o "$work/generated-sanitized"
"$work/generated-sanitized" > "$work/generated-sanitized.out"
cmp "$work/generated-O2.out" "$work/generated-sanitized.out"
# This legal simply-contiguous VOLATILE association is rejected by GNU Fortran
# 16.1. The fixture retains its original form and has an independent contract.
"$1" "$root/test/fixtures/qualified_contiguous.f90" -o "$work/contiguous.c"
for optimization in 0 2; do
    "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/contiguous.c" -lm \
        -o "$work/contiguous-O$optimization"
    "$work/contiguous-O$optimization" > "$work/contiguous-O$optimization.out"
done
"$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all "$work/contiguous.c" -lm -o "$work/contiguous-sanitized"
"$work/contiguous-sanitized" > "$work/contiguous-sanitized.out"
printf '%s\n' 'qualified contiguous contracts passed' > "$work/contiguous-expected.out"
cmp "$work/contiguous-O0.out" "$work/contiguous-expected.out"
cmp "$work/contiguous-O2.out" "$work/contiguous-expected.out"
cmp "$work/contiguous-sanitized.out" "$work/contiguous-expected.out"
for optimization in 0 2; do
    "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -I"$work" \
        '-DF2C_DESCRIPTOR_SOURCE="qualified.c"' \
        "$root/test/generated/descriptor_contracts.c" -lm -o "$work/records-O$optimization"
    "$work/records-O$optimization"
done
failures='null-read null-write allocation offset count linear-null linear-rank-zero linear-rank-high linear-negative linear-empty linear-range linear-overflow state-dimension subscript-distance'
for failure in $failures; do
    status=0
    "$work/records-O2" "--$failure" > "$work/failure-$failure.out" 2>&1 || status=$?
    if [ "$status" -ne 99 ]; then
        echo "descriptor failure contract did not abort: $failure (status $status)" >&2
        exit 1
    fi
done
"$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I"$work" '-DF2C_DESCRIPTOR_SOURCE="qualified.c"' \
    "$root/test/generated/descriptor_contracts.c" -lm -o "$work/records-sanitized"
"$work/records-sanitized"
for failure in $failures; do
    status=0
    "$work/records-sanitized" "--$failure" > "$work/sanitized-failure-$failure.out" 2>&1 || status=$?
    if [ "$status" -ne 99 ]; then
        echo "sanitized descriptor failure contract did not abort: $failure (status $status)" >&2
        exit 1
    fi
done
echo "qualified descriptor differential and record contracts passed"
