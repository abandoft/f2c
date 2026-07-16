#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
f2c_input=$1
case $f2c_input in
    /*) f2c=$f2c_input ;;
    *) f2c=$root/$f2c_input ;;
esac
work=$root/build/reference-lapack-core
lapack_commit=6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca
expected=3535
strict_cflags='-std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror'

rm -rf "$work"
mkdir -p "$work/generated" "$work/objects" "$work/differential-reports/lapack"

git clone --quiet --filter=blob:none --no-checkout \
    https://github.com/Reference-LAPACK/lapack.git "$work/lapack"
git -C "$work/lapack" sparse-checkout init --cone
git -C "$work/lapack" sparse-checkout set \
    BLAS/SRC BLAS/TESTING CMAKE LAPACKE/include SRC INSTALL TESTING
git -C "$work/lapack" checkout --quiet "$lapack_commit"
stabilization_patch=$root/test/reference-lapack-3.12.1-stabilize.patch
git -C "$work/lapack" apply --check "$stabilization_patch"
git -C "$work/lapack" apply "$stabilization_patch"

find "$work/lapack/BLAS/SRC" "$work/lapack/SRC" "$work/lapack/INSTALL" \
    "$work/lapack/TESTING" -type f \( -iname '*.f' -o -iname '*.f90' \) | sort > \
    "$work/sources.list"

count=$(wc -l < "$work/sources.list" | tr -d ' ')
jobs=${F2C_JOBS:-}
if [ -z "$jobs" ]; then
    if command -v nproc >/dev/null 2>&1; then
        jobs=$(nproc)
    elif command -v sysctl >/dev/null 2>&1; then
        jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 2)
    else
        jobs=2
    fi
fi
case $jobs in
    ''|*[!0-9]*|0) echo "F2C_JOBS must be a positive integer" >&2; exit 2 ;;
esac
if [ "$jobs" -gt 8 ]; then
    jobs=8
fi
xargs -P "$jobs" -n 1 sh "$root/test/reference_lapack_compile_one.sh" "$f2c" \
    "$work/lapack" "$work" "${CC:-cc}" "$strict_cflags" < "$work/sources.list"

if [ "$count" -ne "$expected" ]; then
    echo "expected $expected Reference LAPACK sources, compiled $count" >&2
    exit 1
fi

F2C_JOBS=$jobs sh "$root/test/reference_lapack_native_build.sh" "$work"

# Compile optional implementation variants above, but never link them into the
# canonical LAPACK archive.  Static archives store member basenames, so mixing
# SRC/VARIANTS with SRC would create duplicate public symbols whose selection
# depends on traversal and linker order.  BLAS owns the two shared XERBLA
# objects; exclude their identical SRC copies for the same reason.
find "$work/objects/BLAS/SRC" "$work/objects/SRC" -type f -name '*.o' \
    ! -path '*/SRC/VARIANTS/*' ! -path "$work/objects/SRC/xerbla.o" \
    ! -path "$work/objects/SRC/xerbla_array.o" -print0 | \
    xargs -0 "${AR:-ar}" rcs "$work/libf2c_lapack.a"
"${AR:-ar}" rcs "$work/libf2c_lapack.a" "$work/objects/INSTALL/dlamch.o" \
    "$work/objects/INSTALL/slamch.o"
"${CC:-cc}" -std=c17 -O2 -Wall -Wextra -Werror \
    "$root/test/lapack_dgesv_harness.c" "$work/libf2c_lapack.a" -lm \
    -o "$work/dgesv-check"
"$work/dgesv-check"

for name in test_zcomplexabs test_zcomplexdiv test_zcomplexmult test_zminMax; do
    executable=$work/$name
    output=$work/$name.out
    errors=$work/$name.err
    "${CC:-cc}" -std=c17 -O2 "$work/objects/INSTALL/$name.o" -lm -o "$executable"
    "$executable" >"$output" 2>"$errors"
    if ! grep -q '# All tests pass' "$output"; then
        echo "Reference LAPACK INSTALL check failed: $name" >&2
        tail -n 20 "$output" >&2
        tail -n 20 "$errors" >&2
        exit 1
    fi
    native_output=$work/native-results/$name.out
    native_errors=$work/native-results/$name.err
    "$work/native/$name" >"$native_output" 2>"$native_errors"
    python3 "$root/test/lapack_result_diff.py" install "$output" "$native_output" \
        --suite "install-$name" \
        --report "$work/differential-reports/lapack/install-$name.json"
done

find "$work/objects/TESTING/MATGEN" -type f -name '*.o' -print0 | \
    xargs -0 "${AR:-ar}" rcs "$work/libf2c_matgen.a"
set --
for name in dchkrfp ddrvrfp ddrvrf1 ddrvrf2 ddrvrf3 ddrvrf4 derrrfp dlatb4 \
    dlarhs dget04 dpot01 dpot03 dpot02 chkxer xerbla alaerh aladhd alahd alasvm; do
    set -- "$@" "$work/objects/TESTING/LIN/$name.o"
done
"${CC:-cc}" -std=c17 -O2 "$@" "$work/objects/INSTALL/ilaver.o" \
    "$work/objects/INSTALL/droundup_lwork.o" "$work/objects/INSTALL/dsecnd_NONE.o" \
    "$work/libf2c_matgen.a" "$work/libf2c_lapack.a" -lm -o "$work/xdblat2rfp"
"$work/xdblat2rfp" <"$work/lapack/TESTING/dtest_rfp.in" >"$work/xdblat2rfp.out" \
    2>"$work/xdblat2rfp.err"
require_rfp_result() {
    rfp_output=$1
    rfp_label=$2
    rfp_count=$3
    if ! grep -Eq "^[[:space:]]*${rfp_label}[[:space:]]+${rfp_count}[[:space:]]*$" \
        "$rfp_output" &&
        ! grep -Eq \
            "All tests for .*${rfp_label}.*\([[:space:]]*${rfp_count}[[:space:]]+tests run\)" \
            "$rfp_output"; then
        return 1
    fi
}

for result in 'DPF:2304' 'DLANSF:384' 'RFP conversion:72' 'DTFSM:7776' 'DSFRK:2592'; do
    result_label=${result%:*}
    result_count=${result#*:}
    if ! require_rfp_result "$work/xdblat2rfp.out" "$result_label" "$result_count"; then
        echo "Reference LAPACK double-precision RFP check failed" >&2
        tail -n 40 "$work/xdblat2rfp.out" >&2
        tail -n 20 "$work/xdblat2rfp.err" >&2
        exit 1
    fi
done

set --
for name in schkrfp sdrvrfp sdrvrf1 sdrvrf2 sdrvrf3 sdrvrf4 serrrfp slatb4 \
    slarhs sget04 spot01 spot03 spot02 chkxer xerbla alaerh aladhd alahd alasvm; do
    set -- "$@" "$work/objects/TESTING/LIN/$name.o"
done
"${CC:-cc}" -std=c17 -O2 "$@" "$work/objects/INSTALL/ilaver.o" \
    "$work/objects/INSTALL/sroundup_lwork.o" "$work/objects/INSTALL/droundup_lwork.o" \
    "$work/objects/INSTALL/second_NONE.o" "$work/libf2c_matgen.a" \
    "$work/libf2c_lapack.a" -lm -o "$work/xlintstrfs"
"$work/xlintstrfs" <"$work/lapack/TESTING/stest_rfp.in" >"$work/xlintstrfs.out" \
    2>"$work/xlintstrfs.err"
for result in 'SPF:2304' 'SLANSF:384' 'RFP conversion:72' 'STFSM:7776' 'SSFRK:2592'; do
    require_rfp_result "$work/xlintstrfs.out" "${result%:*}" "${result#*:}" || exit 1
done

set --
for name in cchkrfp cdrvrfp cdrvrf1 cdrvrf2 cdrvrf3 cdrvrf4 cerrrfp claipd \
    clatb4 clarhs csbmv cget04 cpot01 cpot03 cpot02 chkxer xerbla alaerh aladhd \
    alahd alasvm; do
    set -- "$@" "$work/objects/TESTING/LIN/$name.o"
done
"${CC:-cc}" -std=c17 -O2 "$@" "$work/objects/INSTALL/ilaver.o" \
    "$work/objects/INSTALL/sroundup_lwork.o" "$work/objects/INSTALL/droundup_lwork.o" \
    "$work/objects/INSTALL/second_NONE.o" "$work/libf2c_matgen.a" \
    "$work/libf2c_lapack.a" -lm -o "$work/xlintstrfc"
"$work/xlintstrfc" <"$work/lapack/TESTING/ctest_rfp.in" >"$work/xlintstrfc.out" \
    2>"$work/xlintstrfc.err"
for result in 'CPF:2304' 'CLANHF:384' 'RFP conversion:72' 'CTFSM:7776' 'CHFRK:2592'; do
    require_rfp_result "$work/xlintstrfc.out" "${result%:*}" "${result#*:}" || exit 1
done

set --
for name in zchkrfp zdrvrfp zdrvrf1 zdrvrf2 zdrvrf3 zdrvrf4 zerrrfp zlatb4 \
    zlaipd zlarhs zsbmv zget04 zpot01 zpot03 zpot02 chkxer xerbla alaerh aladhd \
    alahd alasvm; do
    set -- "$@" "$work/objects/TESTING/LIN/$name.o"
done
"${CC:-cc}" -std=c17 -O2 "$@" "$work/objects/INSTALL/ilaver.o" \
    "$work/objects/INSTALL/sroundup_lwork.o" "$work/objects/INSTALL/droundup_lwork.o" \
    "$work/objects/INSTALL/dsecnd_NONE.o" "$work/libf2c_matgen.a" \
    "$work/libf2c_lapack.a" -lm -o "$work/xlintstrfz"
"$work/xlintstrfz" <"$work/lapack/TESTING/ztest_rfp.in" >"$work/xlintstrfz.out" \
    2>"$work/xlintstrfz.err"
for result in 'ZPF:2304' 'ZLANHF:384' 'RFP conversion:72' 'ZTFSM:7776' 'ZHFRK:2592'; do
    require_rfp_result "$work/xlintstrfz.out" "${result%:*}" "${result#*:}" || exit 1
done

for precision in s d c z; do
    native_output=$work/native-results/xlintstrf$precision.out
    native_errors=$work/native-results/xlintstrf$precision.err
    "$work/native/bin/xlintstrf$precision" \
        <"$work/lapack/TESTING/${precision}test_rfp.in" \
        >"$native_output" 2>"$native_errors"
    if [ -s "$native_errors" ]; then
        echo "native Reference LAPACK $precision RFP driver wrote diagnostics" >&2
        tail -n 40 "$native_errors" >&2
        exit 1
    fi
    case $precision in
        s) generated_output=$work/xlintstrfs.out ;;
        d) generated_output=$work/xdblat2rfp.out ;;
        c) generated_output=$work/xlintstrfc.out ;;
        z) generated_output=$work/xlintstrfz.out ;;
    esac
    python3 "$root/test/lapack_result_diff.py" rfp \
        "$generated_output" "$native_output" --suite "rfp-$precision" \
        --report "$work/differential-reports/lapack/rfp-$precision.json"
done

if [ "$count" -ne "$expected" ]; then
    echo "internal error: Reference LAPACK source count changed during validation" >&2
    exit 1
fi
echo "translated and warning-clean compiled all $count Reference LAPACK sources"
echo "passed DGESV and all four Reference LAPACK INSTALL numerical checks"
echo "differentially matched all 52,512 generated-C/native-Fortran RFP checks"
