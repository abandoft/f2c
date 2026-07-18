#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
if ! command -v gfortran >/dev/null 2>&1; then
    echo "gfortran is required for performance code-generation reports" >&2
    exit 2
fi

f2c=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$root/build/diagnostics/performance-codegen
lapack_commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca
source_root=https://raw.githubusercontent.com/Reference-LAPACK/lapack/$lapack_commit
local_source=$root/build/reference-lapack-core/lapack

rm -rf "$work"
mkdir -p "$work"

gcc_major=$(gfortran -dumpversion | cut -d. -f1)
if command -v "gcc-$gcc_major" >/dev/null 2>&1; then
    c_compiler=gcc-$gcc_major
else
    c_compiler=${CC:-cc}
fi

{
    "$c_compiler" --version
    gfortran --version
} >"$work/compiler-versions.txt"

stage_source() {
    path=$1
    name=$2
    source=$work/$name.f
    if [ -f "$local_source/$path/$name.f" ]; then
        cp "$local_source/$path/$name.f" "$source"
    else
        curl --fail --location --silent --show-error \
            "$source_root/$path/$name.f" --output "$source"
    fi
    "$f2c" --fixed-form "$source" -o "$work/$name.c"
}

stage_source BLAS/SRC dgemv
stage_source BLAS/SRC dgemm
stage_source SRC dgetrf
stage_source SRC dpotrf

for name in dgemv dgemm dgetrf dpotrf; do
    "$c_compiler" -std=c17 -O3 -ffp-contract=fast -DF2C_FP_CONTRACT=1 -DNDEBUG \
        -fverbose-asm -fopt-info-vec-all="$work/$name.c.vec" \
        -S "$work/$name.c" -o "$work/$name.c.s"
    "$c_compiler" -std=c17 -O3 -ffp-contract=fast -DF2C_FP_CONTRACT=1 \
        -DF2C_LOOP_UNROLL= -DNDEBUG \
        -fverbose-asm -fopt-info-vec-all="$work/$name.c.auto-unroll.vec" \
        -S "$work/$name.c" -o "$work/$name.c.auto-unroll.s"
    gfortran -O3 -fverbose-asm -fopt-info-vec-all="$work/$name.fortran.vec" \
        -S "$work/$name.f" -o "$work/$name.fortran.s"
done

echo "performance code-generation reports: $work"
