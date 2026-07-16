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

if [ ! -x "$1" ] || [ ! -d "$objects/TESTING/LIN" ] || [ ! -d "$lapack/TESTING" ] ||
    [ ! -x "$work/native/bin/xlintsts" ]; then
    echo "run reference_lapack_core_compile.sh before the LIN gate" >&2
    exit 2
fi

expected_real='GE 3653
GE 5748
GB 28938
GB 36567
GT 2694
GT 2033
PO 1628
PO 1910
PS 150
PP 1332
PP 1910
PB 3458
PB 4750
PT 953
PT 788
SY 1846
SY 1072
SR 1618
SR 222
SK 1618
SK 222
SA 1320
SA 148
S2 666
S2 74
SP 1404
SP 1072
TR 8008
TP 7392
TB 19888
QR 42840
RQ 28784
LQ 28784
QL 28784
Q3 4410
QK 241365
TZ 252
LS 114660
QT 510
QX 1482
XQ 1482
TQ 510
TS 10800
HH 15900'

expected_complex='GE 3653
GE 5748
GB 28938
GB 36567
GT 2694
GT 2033
PO 1628
PO 1910
PS 150
PP 1332
PP 1910
PB 3458
PB 4750
PT 1778
PT 788
HE 1846
HE 1072
HR 1618
HR 222
HK 1618
HK 222
HA 1320
HA 148
H2 666
H2 74
SA 1320
SA 148
S2 666
S2 74
HP 1404
HP 1072
SY 2122
SY 1240
SR 1822
SR 258
SK 1822
SK 258
SP 1620
SP 1240
TR 8008
TP 7392
TB 19888
QR 42840
RQ 28784
LQ 28784
QL 28784
Q3 4410
QK 241365
TZ 252
LS 114660
QT 510
QX 1482
XQ 1482
TQ 510
TS 10800
HH 15900'

run_precision() {
    precision=$1
    prefix=$2
    main_object=$3
    input=$4
    expected=$5
    output=$work/xlintst${precision}.out
    errors=$work/xlintst${precision}.err
    executable=$work/xlintst${precision}
    native_output=$work/native-results/xlintst${precision}.out
    native_errors=$work/native-results/xlintst${precision}.err
    native_executable=$work/native/bin/xlintst${precision}

    # Link the standard USE_XBLAS=OFF driver.  The distribution gate has
    # compiled every source; alternate mains, optional XBLAS tests, and
    # duplicate matrix-generator helpers do not belong in this executable.
    set -- "$objects/TESTING/LIN/$main_object"
    for object in "$objects"/TESTING/LIN/*.o; do
        base=$(basename "$object")
        case $base in
            schkaa.o|dchkaa.o|cchkaa.o|zchkaa.o|schkrfp.o|dchkrfp.o|cchkrfp.o|zchkrfp.o|dchkab.o|zchkab.o|*lahilb.o|*drvgbx.o|*drvgex.o|*drvsyx.o|*drvhex.o|*drvpox.o|*errvxx.o|*errgex.o|*errsyx.o|*errhex.o|*errpox.o|*ebchvxx.o)
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
    rm -f "$output" "$errors"
    (ulimit -f 204800; "$executable" <"$lapack/TESTING/$input" >"$output" 2>"$errors")

    if [ -s "$errors" ]; then
        echo "Reference LAPACK $prefix LIN driver wrote diagnostics" >&2
        tail -n 40 "$errors" >&2
        exit 1
    fi

    printf '%s\n' "$expected" | while IFS=' ' read -r suffix count; do
        path=$prefix$suffix
        if ! grep -Eq "^  *All tests for ${path} .*\\( *${count} tests run\\) *$" "$output"; then
            echo "Reference LAPACK LIN count mismatch: $path $count" >&2
            tail -n 80 "$output" >&2
            exit 1
        fi
    done

    if grep -Eq '^  *[[:alnum:]]{3}  *[0-9]+  +[0-9]+ *$' "$output"; then
        echo "Reference LAPACK $prefix LIN reported threshold failures" >&2
        grep -E '^  *[[:alnum:]]{3}  *[0-9]+  +[0-9]+ *$' "$output" >&2
        exit 1
    fi

    rm -f "$native_output" "$native_errors"
    (ulimit -f 204800; "$native_executable" <"$lapack/TESTING/$input" \
        >"$native_output" 2>"$native_errors")
    if [ -s "$native_errors" ] ||
        grep -Eqi 'tests? failed|failed the threshold' "$native_output"; then
        echo "native Reference LAPACK $prefix LIN driver reported failures" >&2
        tail -n 80 "$native_output" >&2
        tail -n 40 "$native_errors" >&2
        exit 1
    fi
    python3 "$root/test/lapack_result_diff.py" summary "$output" "$native_output" \
        --suite "lin-$precision" \
        --report "$work/differential-reports/lapack/lin-$precision.json"

    echo "differentially matched the complete generated-C/native-Fortran $prefix LIN suite"
}

run_precision s S schkaa.o stest.in "$expected_real"
run_precision d D dchkaa.o dtest.in "$expected_real"
run_precision c C cchkaa.o ctest.in "$expected_complex"
run_precision z Z zchkaa.o ztest.in "$expected_complex"

echo "differentially matched all 200 official LIN numerical result groups"
