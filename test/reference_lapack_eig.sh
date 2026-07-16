#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/reference-lapack-core
objects=$work/objects
lapack=$work/lapack

if [ ! -x "$1" ] || [ ! -d "$objects/TESTING/EIG" ] || [ ! -f "$work/libf2c_lapack.a" ] ||
    [ ! -x "$work/native/bin/xeigtsts" ]; then
    echo "run reference_lapack_core_compile.sh before the EIG gate" >&2
    exit 2
fi

link_precision() {
    precision=$1
    main=$2
    executable=$work/xeigtst$precision
    set -- "$objects/TESTING/EIG/$main.o"
    for object in "$objects"/TESTING/EIG/*.o; do
        base=$(basename "$object")
        case $base in
            schkee.o|dchkee.o|cchkee.o|zchkee.o|schkdmd.o|dchkdmd.o|cchkdmd.o|zchkdmd.o)
                continue
                ;;
        esac
        set -- "$@" "$object"
    done
    for object in "$objects"/TESTING/MATGEN/*.o; do
        set -- "$@" "$object"
    done
    set -- "$@" \
        "$objects/INSTALL/ilaver.o" \
        "$objects/INSTALL/sroundup_lwork.o" \
        "$objects/INSTALL/droundup_lwork.o" \
        "$objects/INSTALL/second_NONE.o" \
        "$objects/INSTALL/dsecnd_NONE.o"
    "${CC:-cc}" -std=c17 -O2 "$@" "$work/libf2c_lapack.a" -lm -o "$executable"
}

require_count() {
    output=$1
    code=$2
    count=$3
    if grep -Eq "^[[:space:]]*${code}[[:space:]]+${count}[[:space:]]*$" "$output" ||
        grep -Eq "passed the threshold.*\([[:space:]]*${count}[[:space:]]+tests run\)" "$output"; then
        return
    fi
    echo "Reference LAPACK EIG count mismatch: $code $count" >&2
    tail -n 80 "$output" >&2
    exit 1
}

run_case() {
    precision=$1
    input=$2
    shift 2
    output=$work/eig-$precision-$input.out
    errors=$work/eig-$precision-$input.err
    executable=$work/xeigtst$precision
    native_output=$work/native-results/eig-$precision-$input.out
    native_errors=$work/native-results/eig-$precision-$input.err
    native_executable=$work/native/bin/xeigtst$precision
    rm -f "$output" "$errors"
    (ulimit -f 204800; "$executable" <"$lapack/TESTING/$input.in" >"$output" 2>"$errors")
    if [ -s "$errors" ] || grep -Eqi 'tests? failed|failed the threshold' "$output"; then
        echo "Reference LAPACK $precision EIG case $input reported failures" >&2
        tail -n 80 "$output" >&2
        tail -n 40 "$errors" >&2
        exit 1
    fi
    for expected in "$@"; do
        code=${expected%%:*}
        count=${expected#*:}
        require_count "$output" "$code" "$count"
    done
    rm -f "$native_output" "$native_errors"
    (ulimit -f 204800; "$native_executable" <"$lapack/TESTING/$input.in" \
        >"$native_output" 2>"$native_errors")
    if [ -s "$native_errors" ]; then
        echo "native Reference LAPACK $precision EIG case $input wrote diagnostics" >&2
        tail -n 40 "$native_errors" >&2
        exit 1
    fi
    case $input in
        *bal|*bak)
            comparison_mode=balance
            ;;
        *)
            comparison_mode=summary
            ;;
    esac
    python3 "$root/test/lapack_result_diff.py" "$comparison_mode" \
        "$output" "$native_output" --suite "eig-$precision-$input" \
        --report "$work/differential-reports/lapack/eig-$precision-$input.json"
}

run_balance_cases() {
    precision=$1
    prefix=$2
    generalized_total=$3
    run_case "$precision" "${prefix}bal"
    run_case "$precision" "${prefix}bak"
    run_case "$precision" "${prefix}gbal"
    run_case "$precision" "${prefix}gbak"
    require_balance_count "$work/eig-$precision-${prefix}bal.out" 13
    require_balance_count "$work/eig-$precision-${prefix}bak.out" 7
    require_balance_count "$work/eig-$precision-${prefix}gbal.out" "$generalized_total"
    require_balance_count "$work/eig-$precision-${prefix}gbak.out" "$generalized_total"
}

require_balance_count() {
    output=$1
    count=$2
    if grep -Eqi "total number of examples tested[[:space:]]*=[[:space:]]*${count}[[:space:]]*\$" \
        "$output" || grep -Eq "^[[:space:]]*${count}[[:space:]]*\$" "$output"; then
        return
    fi
    echo "Reference LAPACK balance count mismatch: expected $count" >&2
    tail -n 40 "$output" >&2
    exit 1
}

run_real_precision() {
    precision=$1
    prefix=$2
    path=$3
    run_case "$precision" nep "${path}HS:2016"
    run_case "$precision" sep "${path}ST:4440" "${path}ST:13464"
    run_case "$precision" se2 "${path}ST:4440" "${path}ST:13464"
    run_case "$precision" svd "${path}BD:10260" "${path}BD:14820"
    if [ "$path" = D ]; then
        run_case "$precision" "${prefix}ec" "${path}EC:501261"
    else
        run_case "$precision" "${prefix}ec" "${path}EC:501251"
    fi
    run_case "$precision" "${prefix}ed" "${path}SX:3508" "${path}VX:6282"
    run_case "$precision" "${prefix}gg" "${path}GG:2184"
    if [ "$path" = D ]; then
        run_case "$precision" "${prefix}gd" DGS:1560 DGV:1092 DGX:150 DGX:20 DXV:5000 DXV:8
    else
        run_case "$precision" "${prefix}gd" SXV:5000 SXV:8
    fi
    run_case "$precision" "${prefix}sb" "${path}SB:810"
    run_case "$precision" "${prefix}sg" "${path}SG:11172"
    run_balance_cases "$precision" "$prefix" 8
    run_case "$precision" "${prefix}bb" "${path}BB:3000"
    run_case "$precision" glm GLM:48
    run_case "$precision" gqr GQR:1728
    run_case "$precision" gsv GSV:384
    run_case "$precision" csd CSD:600
    run_case "$precision" lse LSE:96
    echo "passed all 20 Reference LAPACK $path EIG input suites"
}

run_complex_precision() {
    precision=$1
    prefix=$2
    path=$3
    condition_count=$4
    gsv_count=$5
    run_case "$precision" nep "${path}HS:2016"
    run_case "$precision" sep "${path}ST:4440" "${path}ST:11016"
    run_case "$precision" se2 "${path}ST:4440" "${path}ST:11016"
    run_case "$precision" svd "${path}BD:4085" "${path}BD:14340"
    run_case "$precision" "${prefix}ec" "${path}EC:${condition_count}"
    run_case "$precision" "${prefix}ed" "${path}SX:3994" "${path}VX:5172"
    run_case "$precision" "${prefix}gg" "${path}GG:2184"
    run_case "$precision" "${prefix}gd" "${path}XV:8"
    run_case "$precision" "${prefix}sb" "${path}HB:810"
    run_case "$precision" "${prefix}sg" "${path}SG:11172"
    run_balance_cases "$precision" "$prefix" 10
    run_case "$precision" "${prefix}bb" "${path}BB:3000"
    run_case "$precision" glm GLM:48
    run_case "$precision" gqr GQR:1728
    run_case "$precision" gsv "GSV:${gsv_count}"
    run_case "$precision" csd CSD:600
    run_case "$precision" lse LSE:96
    echo "passed all 20 Reference LAPACK $path EIG input suites"
}

link_precision s schkee
link_precision d dchkee
link_precision c cchkee
link_precision z zchkee

run_real_precision s s S
run_real_precision d d D
# CCKGSV adds the LAPACK issue-411 NaN regression; ZCKGSV retains the 384
# parameterized cases.  ZEC's fixed zec.in dataset contains 6,222 checks.
run_complex_precision c c C 5966 385
run_complex_precision z z Z 6222 384

echo "differentially matched all 80 generated-C/native-Fortran S/D/C/Z EIG suites"
