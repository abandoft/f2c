#!/usr/bin/env sh
set -eu

if [ "$#" -ne 6 ]; then
    echo "usage: $0 /path/to/f2c /path/to/lapack /path/to/work cc 'cflags' source" >&2
    exit 2
fi

f2c=$1
lapack=$2
work=$3
c_compiler=$4
strict_cflags=$5
source=$6
relative=${source#"$lapack"/}
generated=$work/generated/${relative%.*}.c
object=$work/objects/${relative%.*}.o
# Reference LAPACK's Release Fortran build contracts eligible multiply-add
# expressions.  Use the generated-code opt-in consistently so the C and
# native baselines exercise the same optimized floating-point profile.  The
# single-precision BLAS kernels retain deterministic rounding: contracting
# their reduction updates pushes the official STFSM residual over threshold.
fp_flags='-DF2C_FP_CONTRACT=1'
case $relative in
    BLAS/SRC/sgemm.f|BLAS/SRC/strsm.f) fp_flags= ;;
esac

mkdir -p "$(dirname -- "$generated")" "$(dirname -- "$object")"
"$f2c" "$source" -o "$generated"
# shellcheck disable=SC2086
"$c_compiler" $strict_cflags $fp_flags -c "$generated" -o "$object"
