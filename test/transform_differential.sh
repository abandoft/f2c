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
WORK=$ROOT/build/transform-differential
FC_VERSION=$("$FC" -dumpfullversion -dumpversion 2>/dev/null || :)
FC_ID=$("$FC" --version 2>/dev/null | sed -n '1p' || :)
GNU_FORTRAN_13=false

case "$FC_ID:$FC_VERSION" in
    *GNU*Fortran*:13.*) GNU_FORTRAN_13=true ;;
esac

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler not found: $CC" >&2
    exit 2
fi
if ! command -v "$FC" >/dev/null 2>&1; then
    echo "Fortran compiler not found: $FC" >&2
    exit 2
fi

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK"

for fixture in transform_intrinsics transform_character_derived; do
    case_work=$WORK/$fixture
    source=$ROOT/test/fixtures/$fixture.f90
    cmake -E make_directory "$case_work"

    "$F2C" "$source" -o "$case_work/generated.c"
    "$CC" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$case_work/generated.c" -lm \
        -o "$case_work/generated"
    if [ "$fixture" = transform_character_derived ] && [ "$GNU_FORTRAN_13" = true ]; then
        # GNU Fortran 13 incorrectly diagnoses an initialized deferred-length character array
        # when its BLOCK scope is finalized. Keep the diagnostic visible without weakening any
        # other warning or any other compiler configuration.
        "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror \
            -Wno-error=uninitialized -J "$case_work" "$source" -o "$case_work/native"
    else
        "$FC" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror -J "$case_work" "$source" \
            -o "$case_work/native"
    fi

    "$case_work/generated" > "$case_work/generated.out"
    "$case_work/native" > "$case_work/native.out"

    if ! cmp -s "$case_work/generated.out" "$case_work/native.out"; then
        echo "$fixture generated/native transformational intrinsic output mismatch" >&2
        diff -u "$case_work/native.out" "$case_work/generated.out" >&2 || true
        exit 1
    fi
done

echo "transformational intrinsic differential passed"
