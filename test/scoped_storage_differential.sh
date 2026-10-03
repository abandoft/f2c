#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
base=$root/build/scoped-storage-differential
cc=${CC:-cc}
fc=${FC:-gfortran}
cmake -E remove_directory "$base"
for fixture in scoped_storage_access scoped_unaligned_storage; do
    work=$base/$fixture
    cmake -E make_directory "$work"
    "$1" "$root/test/fixtures/$fixture.f90" -o "$work/generated.c"
    case $fixture in
        scoped_storage_access) expected='scoped storage access contracts passed' ;;
        scoped_unaligned_storage) expected='scoped unaligned storage contracts passed' ;;
    esac
    printf '%s\n' "$expected" > "$work/expected.out"
    for optimization in 0 2 3; do
        "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/generated.c" -lm \
            -o "$work/generated-O$optimization"
        # F2008 keeps the original COMMON/EQUIVALENCE source strictly valid without
        # downgrading diagnostics for the Fortran-2018 obsolescent-feature warning.
        "$fc" -std=f2008 -pedantic-errors "-O$optimization" -Wall -Wextra -Werror \
            -Wno-compare-reals -J"$work" "$root/test/fixtures/$fixture.f90" \
            -o "$work/native-O$optimization"
        "$work/generated-O$optimization" > "$work/generated-O$optimization.out"
        "$work/native-O$optimization" > "$work/native-O$optimization.out"
        cmp "$work/expected.out" "$work/generated-O$optimization.out"
        cmp "$work/generated-O$optimization.out" "$work/native-O$optimization.out"
    done
    "$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$work/generated.c" -lm -o "$work/generated-sanitized"
    "$work/generated-sanitized" > "$work/generated-sanitized.out"
    cmp "$work/expected.out" "$work/generated-sanitized.out"
done
echo 'scoped storage access differential passed'
