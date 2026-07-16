#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
if ! command -v gfortran >/dev/null 2>&1; then
    echo "gfortran is required for the Reference BLAS performance comparison" >&2
    exit 2
fi

f2c=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/benchmarks/daxpy
lapack_commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca
source_url="https://raw.githubusercontent.com/Reference-LAPACK/lapack/${lapack_commit}/BLAS/SRC/daxpy.f"

rm -rf "$work"
mkdir -p "$work"

curl --fail --location --silent --show-error "$source_url" --output "$work/daxpy.f"
"$f2c" --fixed-form "$work/daxpy.f" -o "$work/daxpy.c"
gcc_major=$(gfortran -dumpversion | cut -d. -f1)
if command -v "gcc-$gcc_major" >/dev/null 2>&1; then
    c_compiler="gcc-$gcc_major"
else
    c_compiler=${CC:-cc}
fi
"$c_compiler" -std=c17 -O3 -ffp-contract=fast -DNDEBUG \
    -c "$work/daxpy.c" -o "$work/daxpy-c.o"
gfortran -O3 -c "$work/daxpy.f" -o "$work/daxpy-fortran.o"
"$c_compiler" -std=c17 -O3 -DNDEBUG "$root/test/daxpy_benchmark.c" \
    "$work/daxpy-c.o" "$work/daxpy-fortran.o" -lm -o "$work/benchmark"
"$work/benchmark"
