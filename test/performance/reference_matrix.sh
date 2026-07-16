#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
if ! command -v gfortran >/dev/null 2>&1; then
    echo "gfortran is required for the Reference BLAS/LAPACK performance comparison" >&2
    exit 2
fi

f2c=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$root/build/benchmarks/extended-matrix
lapack_commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca
source_root=https://raw.githubusercontent.com/Reference-LAPACK/lapack/$lapack_commit
local_source=$root/build/reference-lapack-core/lapack

rm -rf "$work"
mkdir -p "$work"

stage_source() {
    path=$1
    name=$2
    extension=$3
    destination=$work/$name.$extension
    if [ -f "$local_source/$path/$name.$extension" ]; then
        cp "$local_source/$path/$name.$extension" "$destination"
    else
        curl --fail --location --silent --show-error \
            "$source_root/$path/$name.$extension" --output "$destination"
    fi
    "$f2c" "$destination" -o "$work/$name.c"
}

for name in ddot dscal dger dtrsm dsyrk dgemm lsame xerbla idamax; do
    stage_source BLAS/SRC "$name" f
done
stage_source BLAS/SRC dnrm2 f90
for name in dgetrf dgetrf2 dlaswp dpotrf dpotrf2 disnan dlaisnan; do
    stage_source SRC "$name" f
done
stage_source INSTALL dlamch f

gcc_major=$(gfortran -dumpversion | cut -d. -f1)
if command -v "gcc-$gcc_major" >/dev/null 2>&1; then
    c_compiler=gcc-$gcc_major
else
    c_compiler=${CC:-cc}
fi

sources='ddot dnrm2 dscal dger dtrsm dsyrk dgemm lsame xerbla idamax dgetrf dgetrf2 dlaswp dpotrf dpotrf2 disnan dlaisnan dlamch'
for name in $sources; do
    extension=f
    [ "$name" = dnrm2 ] && extension=f90
    "$c_compiler" -std=c17 -O3 -ffp-contract=fast -DNDEBUG -c "$work/$name.c" \
        -o "$work/$name-c.o"
    gfortran -O3 -c "$work/$name.$extension" -o "$work/$name-fortran.o"
done

for name in matrix level1 level2 level3 lapack; do
    "$c_compiler" -std=c17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -c "$root/test/performance/$name.c" -o "$work/$name.o"
done

set -- "$work/matrix.o" "$work/level1.o" "$work/level2.o" "$work/level3.o" \
    "$work/lapack.o"
for name in $sources; do
    set -- "$@" "$work/$name-c.o"
done
for name in $sources; do
    set -- "$@" "$work/$name-fortran.o"
done
gfortran "$@" -lm -o "$work/benchmark"
"$work/benchmark"
