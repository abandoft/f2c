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
work=$root/build/benchmarks/dgemv
lapack_commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca
source_root=https://raw.githubusercontent.com/Reference-LAPACK/lapack/$lapack_commit

rm -rf "$work"
mkdir -p "$work"

for name in dgemv lsame xerbla; do
    curl --fail --location --silent --show-error "$source_root/BLAS/SRC/$name.f" \
        --output "$work/$name.f"
    "$f2c" --fixed-form "$work/$name.f" -o "$work/$name.c"
done
gcc_major=$(gfortran -dumpversion | cut -d. -f1)
if command -v "gcc-$gcc_major" >/dev/null 2>&1; then
    c_compiler=gcc-$gcc_major
else
    c_compiler=${CC:-cc}
fi
for name in dgemv lsame xerbla; do
    "$c_compiler" -std=c17 -O3 -flto -ffp-contract=fast -DNDEBUG -c "$work/$name.c" \
        -o "$work/$name-c.o"
    gfortran -O3 -flto -c "$work/$name.f" -o "$work/$name-fortran.o"
done
"$c_compiler" -std=c17 -O3 -flto -DNDEBUG -c "$root/test/dgemv_benchmark.c" \
    -o "$work/benchmark.o"
gfortran -flto "$work/benchmark.o" "$work/dgemv-c.o" "$work/lsame-c.o" "$work/xerbla-c.o" \
    "$work/dgemv-fortran.o" "$work/lsame-fortran.o" "$work/xerbla-fortran.o" -lm \
    -o "$work/benchmark"
"$work/benchmark"
