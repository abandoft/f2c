#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

f2c=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/reference-lapack-smoke
lapack_commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca
source_url="https://raw.githubusercontent.com/Reference-LAPACK/lapack/${lapack_commit}/BLAS/SRC/daxpy.f"

rm -rf "$work"
mkdir -p "$work"

curl --fail --location --silent --show-error "$source_url" --output "$work/daxpy.f"
"$f2c" --fixed-form "$work/daxpy.f" -o "$work/daxpy.c"

cc=${CC:-cc}
"$cc" -std=c17 -O3 -Wall -Wextra -Wpedantic \
    "$work/daxpy.c" "$root/test/lapack_daxpy_harness.c" -lm \
    -o "$work/lapack-daxpy-test"
"$work/lapack-daxpy-test"
