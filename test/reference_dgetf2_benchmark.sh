#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
if ! command -v gfortran >/dev/null 2>&1; then
    echo "gfortran is required for the Reference LAPACK performance comparison" >&2
    exit 2
fi

f2c=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/benchmarks/dgetf2
lapack_commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca
source_root=https://raw.githubusercontent.com/Reference-LAPACK/lapack/$lapack_commit

rm -rf "$work"
mkdir -p "$work"

fetch_translate() {
    path=$1
    name=$2
    curl --fail --location --silent --show-error "$source_root/$path" --output "$work/$name.f"
    "$f2c" --fixed-form "$work/$name.f" -o "$work/$name.c"
}

fetch_translate SRC/dgetf2.f dgetf2
fetch_translate INSTALL/dlamch.f dlamch
for name in idamax dswap dscal dger lsame xerbla; do
    fetch_translate "BLAS/SRC/$name.f" "$name"
done

gcc_major=$(gfortran -dumpversion | cut -d. -f1)
if command -v "gcc-$gcc_major" >/dev/null 2>&1; then
    c_compiler=gcc-$gcc_major
else
    c_compiler=${CC:-cc}
fi
for name in dgetf2 dlamch idamax dswap dscal dger lsame xerbla; do
    "$c_compiler" -std=c17 -O3 -flto -ffp-contract=fast -DF2C_FP_CONTRACT=1 -DNDEBUG \
        -c "$work/$name.c" \
        -o "$work/$name-c.o"
    gfortran -O3 -flto -c "$work/$name.f" -o "$work/$name-fortran.o"
done
"$c_compiler" -std=c17 -O3 -flto -DNDEBUG -c "$root/test/dgetf2_benchmark.c" \
    -o "$work/benchmark.o"
gfortran -flto "$work/benchmark.o" \
    "$work/dgetf2-c.o" "$work/dlamch-c.o" "$work/idamax-c.o" "$work/dswap-c.o" \
    "$work/dscal-c.o" "$work/dger-c.o" "$work/lsame-c.o" "$work/xerbla-c.o" \
    "$work/dgetf2-fortran.o" "$work/dlamch-fortran.o" "$work/idamax-fortran.o" \
    "$work/dswap-fortran.o" "$work/dscal-fortran.o" "$work/dger-fortran.o" \
    "$work/lsame-fortran.o" \
    "$work/xerbla-fortran.o" -lm -o "$work/benchmark"
"$work/benchmark"
