#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$root/build/diagnostics/performance-loop-policies
commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca
source=https://raw.githubusercontent.com/Reference-LAPACK/lapack/$commit/BLAS/SRC
major=$(gfortran -dumpversion | cut -d. -f1)
cc=gcc-$major
command -v "$cc" >/dev/null 2>&1 || cc=${CC:-cc}
cmake -E remove_directory "$work"
cmake -E make_directory "$work"
{
    "$cc" --version
    gfortran --version
    uname -a
    if command -v lscpu >/dev/null 2>&1; then lscpu; fi
} > "$work/toolchain.txt"

for name in dtrsm dsyrk lsame xerbla; do
    curl --fail --location --silent --show-error "$source/$name.f" -o "$work/$name.f"
    "$1" "$work/$name.f" -o "$work/$name.c"
    gfortran -O3 -flto -c "$work/$name.f" -o "$work/$name-fortran.o"
done
for name in loop_policy level3; do
    "$cc" -std=c17 -O3 -flto -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -c "$root/test/performance/$name.c" -o "$work/$name.o"
done

for policy in 4 2 auto; do
    directory=$work/$policy
    cmake -E make_directory "$directory"
    case "$policy" in
        4) flag='-DF2C_LOOP_UNROLL=_Pragma("GCC unroll 4")' ;;
        2) flag='-DF2C_LOOP_UNROLL=_Pragma("GCC unroll 2")' ;;
        auto) flag='-DF2C_LOOP_UNROLL=' ;;
    esac
    for name in dtrsm dsyrk lsame xerbla; do
        "$cc" -std=c17 -O3 -flto -ffp-contract=fast -DF2C_FP_CONTRACT=1 \
            "$flag" -DNDEBUG -c "$work/$name.c" -o "$directory/$name-c.o"
    done
    gfortran -flto "$work/loop_policy.o" "$work/level3.o" \
        "$directory/dtrsm-c.o" "$directory/dsyrk-c.o" \
        "$directory/lsame-c.o" "$directory/xerbla-c.o" \
        "$work/dtrsm-fortran.o" "$work/dsyrk-fortran.o" \
        "$work/lsame-fortran.o" "$work/xerbla-fortran.o" -lm -o "$directory/benchmark"
    "$directory/benchmark" > "$directory/results.log"
    count=$(awk '/^F2C_PERF,/ { ++count } END { print count + 0 }' "$directory/results.log")
    test "$count" -eq 3 || { echo "policy $policy omitted a diagnostic case" >&2; exit 1; }
    printf '\nLoop policy: %s (diagnostic only; not the full parity gate)\n' "$policy"
    cat "$directory/results.log"
    objdump -drwC "$directory/benchmark" > "$directory/benchmark.objdump"
done
