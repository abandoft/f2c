#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

f2c=$1
cc=${CC:-cc}
fc=${FC:-gfortran}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/format-differential

if ! command -v "$cc" >/dev/null 2>&1; then
    echo "C compiler not found: $cc" >&2
    exit 2
fi
if ! command -v "$fc" >/dev/null 2>&1; then
    echo "Fortran compiler not found: $fc" >&2
    exit 2
fi

run_case() {
    name=$1
    source=$2
    case_dir=$work/$name
    cmake -E make_directory "$case_dir"
    "$f2c" "$source" -o "$case_dir/generated.c"
    "$cc" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$case_dir/generated.c" -lm \
        -o "$case_dir/generated"
    "$fc" -std=f2018 -pedantic-errors -O2 -Wall -Wextra -Werror "$source" \
        -o "$case_dir/native"
    "$case_dir/generated" >"$case_dir/generated.out"
    "$case_dir/native" >"$case_dir/native.out"
    if ! cmp -s "$case_dir/generated.out" "$case_dir/native.out"; then
        echo "generated/native FORMAT descriptor output mismatch: $name" >&2
        diff -u "$case_dir/native.out" "$case_dir/generated.out" >&2 || true
        exit 1
    fi
}

cmake -E remove_directory "$work"
cmake -E make_directory "$work"

run_case basic "$root/test/fixtures/format_matrix.f90"
run_case real "$root/test/fixtures/format_real_matrix.f90"
run_case integer "$root/test/fixtures/format_integer_matrix.f90"

echo "FORMAT descriptor differential passed"
