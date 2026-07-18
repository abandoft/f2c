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
    uname -a
    if command -v lscpu >/dev/null 2>&1; then
        lscpu
    fi
} >"$work/compiler-versions.txt"

stage_source() {
    path=$1
    name=$2
    extension=${3:-f}
    source=$work/$name.$extension
    if [ -f "$local_source/$path/$name.$extension" ]; then
        cp "$local_source/$path/$name.$extension" "$source"
    else
        curl --fail --location --silent --show-error \
            "$source_root/$path/$name.$extension" --output "$source"
    fi
    "$f2c" "$source" -o "$work/$name.c"
}

stage_source BLAS/SRC dgemv
for name in ddot dscal dger dtrsm dsyrk dgemm lsame xerbla idamax; do
    stage_source BLAS/SRC "$name"
done
stage_source BLAS/SRC dnrm2 f90
for name in dgetrf dgetrf2 dlaswp dpotrf dpotrf2 disnan dlaisnan; do
    stage_source SRC "$name"
done
stage_source INSTALL dlamch

for name in dgemv dgemm dger dtrsm dgetrf dpotrf; do
    "$c_compiler" -std=c17 -O3 -ffp-contract=fast -DF2C_FP_CONTRACT=1 -DNDEBUG \
        -fverbose-asm -fopt-info-vec-all="$work/$name.c.vec" \
        -S "$work/$name.c" -o "$work/$name.c.s"
    "$c_compiler" -std=c17 -O3 -ffp-contract=fast -DF2C_FP_CONTRACT=1 \
        -DF2C_LOOP_UNROLL= -DNDEBUG \
        -fverbose-asm -fopt-info-vec-all="$work/$name.c.auto-unroll.vec" \
        -S "$work/$name.c" -o "$work/$name.c.auto-unroll.s"
    "$c_compiler" -std=c17 -O3 -ffp-contract=fast -DF2C_FP_CONTRACT=1 \
        '-DF2C_LOOP_UNROLL=_Pragma("GCC unroll 2")' -DNDEBUG \
        -fverbose-asm -fopt-info-vec-all="$work/$name.c.unroll-2.vec" \
        -S "$work/$name.c" -o "$work/$name.c.unroll-2.s"
    "$c_compiler" -std=c17 -O3 -ffp-contract=fast -DF2C_FP_CONTRACT=1 \
        '-DF2C_LOOP_UNROLL=_Pragma("GCC unroll 8")' -DNDEBUG \
        -fverbose-asm -fopt-info-vec-all="$work/$name.c.unroll-8.vec" \
        -S "$work/$name.c" -o "$work/$name.c.unroll-8.s"
    gfortran -O3 -fverbose-asm -fopt-info-vec-all="$work/$name.fortran.vec" \
        -S "$work/$name.f" -o "$work/$name.fortran.s"
    "$c_compiler" -std=c17 -O3 -ffp-contract=fast -DF2C_FP_CONTRACT=1 -DNDEBUG \
        -fopt-info-loop-all="$work/$name.c.loop" \
        -S "$work/$name.c" -o "$work/$name.c.loop.s"
    gfortran -O3 -fopt-info-loop-all="$work/$name.fortran.loop" \
        -S "$work/$name.f" -o "$work/$name.fortran.loop.s"
done

lto=$work/lto
mkdir -p "$lto"
lto_sources='ddot dnrm2 dscal dger dtrsm dsyrk dgemm lsame xerbla idamax dgetrf dgetrf2 dlaswp dpotrf dpotrf2 disnan dlaisnan dlamch'
for name in $lto_sources; do
    extension=f
    [ "$name" = dnrm2 ] && extension=f90
    "$c_compiler" -std=c17 -O3 -flto -ffp-contract=fast -DF2C_FP_CONTRACT=1 \
        -DNDEBUG -c "$work/$name.c" -o "$lto/$name-c.o"
    gfortran -O3 -flto -c "$work/$name.$extension" -o "$lto/$name-fortran.o"
done
for name in matrix level1 level2 level3 lapack; do
    "$c_compiler" -std=c17 -O3 -flto -DNDEBUG \
        -c "$root/test/performance/$name.c" -o "$lto/$name.o"
done

(
    cd "$lto"
    set -- matrix.o level1.o level2.o level3.o lapack.o
    for name in $lto_sources; do
        set -- "$@" "$name-c.o"
    done
    for name in $lto_sources; do
        set -- "$@" "$name-fortran.o"
    done
    gfortran -O3 -flto -save-temps=obj "$@" -lm -o benchmark
)
objdump -drwC "$lto/benchmark" >"$lto/benchmark.objdump"
nm "$lto/benchmark" >"$lto/benchmark.symbols"

link_blas_benchmark() {
    kernel=$1
    linked=$work/lto-$kernel
    mkdir -p "$linked"
    for dependency in "$kernel" lsame xerbla; do
        "$c_compiler" -std=c17 -O3 -flto -ffp-contract=fast -DF2C_FP_CONTRACT=1 \
            -DNDEBUG -c "$work/$dependency.c" -o "$linked/$dependency-c.o"
        gfortran -O3 -flto -c "$work/$dependency.f" -o "$linked/$dependency-fortran.o"
    done
    "$c_compiler" -std=c17 -O3 -flto -DNDEBUG \
        -c "$root/test/${kernel}_benchmark.c" -o "$linked/benchmark.o"
    (
        cd "$linked"
        gfortran -O3 -flto -save-temps=obj benchmark.o "$kernel-c.o" lsame-c.o xerbla-c.o \
            "$kernel-fortran.o" lsame-fortran.o xerbla-fortran.o -lm -o benchmark
    )
    objdump -drwC "$linked/benchmark" >"$linked/benchmark.objdump"
    nm "$linked/benchmark" >"$linked/benchmark.symbols"
}

link_blas_benchmark dgemv
link_blas_benchmark dgemm

echo "performance code-generation reports: $work"
