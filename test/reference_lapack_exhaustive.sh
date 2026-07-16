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
lapack=$work/lapack
objects=$work/objects
trace=$work/exhaustive
instrument=$root/test/lapack_trace_instrument.py
compare=$root/test/exhaustive_result_diff.py
cc=${CC:-cc}
fc=${FC:-gfortran}
strict_cflags='-std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror'
trace_fp_flags='-DF2C_FP_CONTRACT=1'
resume=${F2C_EXHAUSTIVE_RESUME:-OFF}
differential_failures=0
case $resume in
    ON|OFF) ;;
    *) echo "F2C_EXHAUSTIVE_RESUME must be ON or OFF" >&2; exit 2 ;;
esac

if [ ! -x "$f2c" ] || [ ! -d "$objects/TESTING/LIN" ] ||
    [ ! -x "$work/native/bin/xlintsts" ] || [ ! -x "$work/native/bin/xlintstrfs" ]; then
    echo "run reference_lapack_core_compile.sh before the exhaustive gate" >&2
    exit 2
fi
if ! command -v "$fc" >/dev/null 2>&1; then
    echo "a native Fortran compiler is required for exhaustive differential testing" >&2
    exit 2
fi

if [ "$resume" = OFF ]; then
    rm -rf "$trace"
fi
mkdir -p "$trace/input" "$trace/source" "$trace/c" \
    "$trace/object/generated" "$trace/object/native" "$trace/bin" \
    "$trace/transcript" "$trace/report"

compile_trace_source() {
    name=$1
    source=$lapack/TESTING/LIN/$name.f
    instrumented=$trace/source/$name.f
    generated_c=$trace/c/$name.c
    generated_object=$trace/object/generated/$name.o
    native_object=$trace/object/native/$name.o
    python3 "$instrument" instrument "$source" "$instrumented"
    "$f2c" "$instrumented" -o "$generated_c"
    # shellcheck disable=SC2086
    "$cc" $strict_cflags $trace_fp_flags -c "$generated_c" -o "$generated_object"
    "$fc" -O2 -c "$instrumented" -o "$native_object"
}

link_generated_lin() {
    precision=$1
    executable=$trace/bin/xlintst$precision-generated
    set -- "$objects/TESTING/LIN/${precision}chkaa.o"
    for object in "$objects"/TESTING/LIN/*.o; do
        base=$(basename "$object")
        case $base in
            schkaa.o|dchkaa.o|cchkaa.o|zchkaa.o|schkrfp.o|dchkrfp.o|cchkrfp.o|zchkrfp.o|dchkab.o|zchkab.o|*lahilb.o|*drvgbx.o|*drvgex.o|*drvsyx.o|*drvhex.o|*drvpox.o|*errvxx.o|*errgex.o|*errsyx.o|*errhex.o|*errpox.o|*ebchvxx.o)
                continue
                ;;
        esac
        replacement=$trace/object/generated/$base
        if [ -f "$replacement" ]; then
            set -- "$@" "$replacement"
        else
            set -- "$@" "$object"
        fi
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
    "$cc" -std=c17 -O2 "$@" "$work/libf2c_lapack.a" -lm -o "$executable"
}

link_generated_rfp() {
    precision=$1
    executable=$trace/bin/xlintstrf$precision-generated
    case $precision in
        s) names='schkrfp sdrvrfp sdrvrf1 sdrvrf2 sdrvrf3 sdrvrf4 serrrfp slatb4 slarhs sget04 spot01 spot03 spot02 chkxer xerbla alaerh aladhd alahd alasvm' ;;
        d) names='dchkrfp ddrvrfp ddrvrf1 ddrvrf2 ddrvrf3 ddrvrf4 derrrfp dlatb4 dlarhs dget04 dpot01 dpot03 dpot02 chkxer xerbla alaerh aladhd alahd alasvm' ;;
        c) names='cchkrfp cdrvrfp cdrvrf1 cdrvrf2 cdrvrf3 cdrvrf4 cerrrfp claipd clatb4 clarhs csbmv cget04 cpot01 cpot03 cpot02 chkxer xerbla alaerh aladhd alahd alasvm' ;;
        z) names='zchkrfp zdrvrfp zdrvrf1 zdrvrf2 zdrvrf3 zdrvrf4 zerrrfp zlatb4 zlaipd zlarhs zsbmv zget04 zpot01 zpot03 zpot02 chkxer xerbla alaerh aladhd alahd alasvm' ;;
        *) echo "unsupported precision: $precision" >&2; exit 2 ;;
    esac
    set --
    for name in $names; do
        replacement=$trace/object/generated/$name.o
        if [ -f "$replacement" ]; then
            set -- "$@" "$replacement"
        else
            set -- "$@" "$objects/TESTING/LIN/$name.o"
        fi
    done
    set -- "$@" "$objects/INSTALL/ilaver.o" \
        "$objects/INSTALL/sroundup_lwork.o" \
        "$objects/INSTALL/droundup_lwork.o" \
        "$objects/INSTALL/second_NONE.o" \
        "$objects/INSTALL/dsecnd_NONE.o"
    "$cc" -std=c17 -O2 "$@" "$work/libf2c_matgen.a" \
        "$work/libf2c_lapack.a" -lm -o "$executable"
}

link_native() {
    target=$1
    destination=$2
    shift 2
    link_script=$work/native/TESTING/LIN/CMakeFiles/$target.dir/link.txt
    set -- "$link_script" "$destination" "$@"
    python3 "$instrument" relink "$@"
}

run_precision() {
    precision=$1
    case $precision in
        s)
            trace_sources='schktr sdrvrf1'
            lin_records=746166
            lin_generated_nan=234
            lin_native_nan=0
            rfp_generated_nan=0
            rfp_native_nan=0
            ;;
        d)
            trace_sources='dchktr ddrvrf1'
            lin_records=746166
            lin_generated_nan=234
            lin_native_nan=0
            rfp_generated_nan=0
            rfp_native_nan=0
            ;;
        c)
            trace_sources='cchktr cdrvrf1'
            lin_records=759257
            lin_generated_nan=234
            lin_native_nan=0
            rfp_generated_nan=0
            rfp_native_nan=0
            ;;
        z)
            trace_sources='zchktr zdrvrf1'
            lin_records=759257
            lin_generated_nan=234
            lin_native_nan=0
            rfp_generated_nan=0
            rfp_native_nan=0
            ;;
        *) echo "unsupported precision: $precision" >&2; exit 2 ;;
    esac

    for name in $trace_sources; do
        compile_trace_source "$name"
    done
    python3 "$instrument" zero-threshold "$lapack/TESTING/${precision}test.in" \
        "$trace/input/${precision}test.in" --line 13
    python3 "$instrument" zero-threshold "$lapack/TESTING/${precision}test_rfp.in" \
        "$trace/input/${precision}test_rfp.in" --line 8

    link_generated_lin "$precision"
    set --
    for name in $trace_sources; do
        case $name in
            *drvrf1) continue ;;
        esac
        set -- "$@" "$name.f.o=$trace/object/native/$name.o"
    done
    link_native "xlintst$precision" "$trace/bin/xlintst$precision-native" "$@"

    link_generated_rfp "$precision"
    link_native "xlintstrf$precision" "$trace/bin/xlintstrf$precision-native" \
        "${precision}drvrf1.f.o=$trace/object/native/${precision}drvrf1.o"

    generated_lin=$trace/transcript/lin-$precision-generated.out
    native_lin=$trace/transcript/lin-$precision-native.out
    generated_rfp=$trace/transcript/rfp-$precision-generated.out
    native_rfp=$trace/transcript/rfp-$precision-native.out
    (ulimit -f 409600; "$trace/bin/xlintst$precision-generated" \
        <"$trace/input/${precision}test.in" >"$generated_lin" \
        2>"$trace/transcript/lin-$precision-generated.err")
    (ulimit -f 409600; "$trace/bin/xlintst$precision-native" \
        <"$trace/input/${precision}test.in" >"$native_lin" \
        2>"$trace/transcript/lin-$precision-native.err")
    (ulimit -f 409600; "$trace/bin/xlintstrf$precision-generated" \
        <"$trace/input/${precision}test_rfp.in" >"$generated_rfp" \
        2>"$trace/transcript/rfp-$precision-generated.err")
    (ulimit -f 409600; "$trace/bin/xlintstrf$precision-native" \
        <"$trace/input/${precision}test_rfp.in" >"$native_rfp" \
        2>"$trace/transcript/rfp-$precision-native.err")

    for errors in "$trace/transcript/lin-$precision-generated.err" \
        "$trace/transcript/rfp-$precision-generated.err"; do
        if [ -s "$errors" ]; then
            echo "instrumented generated driver wrote diagnostics: $errors" >&2
            tail -n 40 "$errors" >&2
            exit 1
        fi
    done

    if ! python3 "$compare" lin "$generated_lin" "$native_lin" --threshold 30 \
        --fail-on regression \
        --expected-generated-nan "$lin_generated_nan" \
        --expected-native-nan "$lin_native_nan" \
        --records "$trace/report/lin-$precision-records.jsonl.gz" \
        --report "$trace/report/lin-$precision.json"; then
        differential_failures=$((differential_failures + 1))
    fi
    python3 -c 'import json,sys; actual=json.load(open(sys.argv[1]))["records"]; expected=int(sys.argv[2]); sys.exit(0 if actual == expected else 1)' \
        "$trace/report/lin-$precision.json" "$lin_records"

    if ! python3 "$compare" rfp "$generated_rfp" "$native_rfp" --threshold 30 \
        --fail-on regression \
        --expected-generated-nan "$rfp_generated_nan" \
        --expected-native-nan "$rfp_native_nan" \
        --records "$trace/report/rfp-$precision-records.jsonl.gz" \
        --report "$trace/report/rfp-$precision.json"; then
        differential_failures=$((differential_failures + 1))
    fi
    python3 -c 'import json,sys; actual=json.load(open(sys.argv[1]))["records"]; expected=int(sys.argv[2]); sys.exit(0 if actual == expected else 1)' \
        "$trace/report/rfp-$precision.json" 13056
}

if [ "$resume" = OFF ] || [ ! -x "$trace/bin/xlintstz-native" ]; then
    run_precision s
    run_precision d
    run_precision c
    run_precision z
fi

compile_eig_trace_sources() {
    eig_source=$trace/source/eig
    eig_list=$trace/eig-sources.list
    python3 "$instrument" instrument-directory "$lapack/TESTING/EIG" \
        "$eig_source" "$eig_list" --expected-files 102 --expected-guards 202
    while IFS= read -r name; do
        "$f2c" "$eig_source/$name.f" -o "$trace/c/eig-$name.c"
        # shellcheck disable=SC2086
        "$cc" $strict_cflags $trace_fp_flags -c "$trace/c/eig-$name.c" \
            -o "$trace/object/generated/eig-$name.o"
        "$fc" -O2 -c "$eig_source/$name.f" \
            -o "$trace/object/native/eig-$name.o"
    done <"$eig_list"
}

compile_ec_trace_sources() {
    ec_source=$trace/source/ec
    ec_list=$trace/ec-sources.list
    python3 "$instrument" instrument-ec-directory "$lapack/TESTING/EIG" \
        "$ec_source" "$ec_list" --expected-files 30 --expected-sites 98
    while IFS= read -r name; do
        "$f2c" "$ec_source/$name.f" -o "$trace/c/ec-$name.c"
        # shellcheck disable=SC2086
        "$cc" $strict_cflags $trace_fp_flags -c "$trace/c/ec-$name.c" \
            -o "$trace/object/generated/ec-$name.o"
        "$fc" -O2 -c "$ec_source/$name.f" \
            -o "$trace/object/native/ec-$name.o"
    done <"$ec_list"
}

compile_balance_trace_sources() {
    balance_source=$trace/source/balance
    balance_list=$trace/balance-sources.list
    python3 "$instrument" instrument-balance-directory "$lapack/TESTING/EIG" \
        "$balance_source" "$balance_list" --expected-files 16 --expected-sites 20
    while IFS= read -r name; do
        "$f2c" "$balance_source/$name.f" -o "$trace/c/balance-$name.c"
        # shellcheck disable=SC2086
        "$cc" $strict_cflags $trace_fp_flags -c "$trace/c/balance-$name.c" \
            -o "$trace/object/generated/balance-$name.o"
        "$fc" -O2 -c "$balance_source/$name.f" \
            -o "$trace/object/native/balance-$name.o"
    done <"$balance_list"
}

link_generated_eig() {
    precision=$1
    executable=$trace/bin/xeigtst$precision-generated
    set -- "$objects/TESTING/EIG/${precision}chkee.o"
    for object in "$objects"/TESTING/EIG/*.o; do
        base=$(basename "$object")
        case $base in
            schkee.o|dchkee.o|cchkee.o|zchkee.o|schkdmd.o|dchkdmd.o|cchkdmd.o|zchkdmd.o)
                continue
                ;;
        esac
        name=${base%.o}
        replacement=$trace/object/generated/eig-$name.o
        if [ -f "$replacement" ]; then
            set -- "$@" "$replacement"
        else
            set -- "$@" "$object"
        fi
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
    "$cc" -std=c17 -O2 "$@" "$work/libf2c_lapack.a" -lm -o "$executable"
}

link_native_eig() {
    precision=$1
    link_script=$work/native/TESTING/EIG/CMakeFiles/xeigtst$precision.dir/link.txt
    destination=$trace/bin/xeigtst$precision-native
    set --
    while IFS= read -r name; do
        object_name=$name.f.o
        if grep -q "$object_name" "$link_script"; then
            set -- "$@" "$object_name=$trace/object/native/eig-$name.o"
        fi
    done <"$trace/eig-sources.list"
    python3 "$instrument" relink "$link_script" "$destination" "$@"
}

link_generated_ec() {
    precision=$1
    executable=$trace/bin/xectst$precision-generated
    set -- "$objects/TESTING/EIG/${precision}chkee.o"
    for object in "$objects"/TESTING/EIG/*.o; do
        base=$(basename "$object")
        case $base in
            schkee.o|dchkee.o|cchkee.o|zchkee.o|schkdmd.o|dchkdmd.o|cchkdmd.o|zchkdmd.o)
                continue
                ;;
        esac
        name=${base%.o}
        ec_replacement=$trace/object/generated/ec-$name.o
        eig_replacement=$trace/object/generated/eig-$name.o
        if [ -f "$ec_replacement" ]; then
            set -- "$@" "$ec_replacement"
        elif [ -f "$eig_replacement" ]; then
            set -- "$@" "$eig_replacement"
        else
            set -- "$@" "$object"
        fi
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
    "$cc" -std=c17 -O2 "$@" "$work/libf2c_lapack.a" -lm -o "$executable"
}

link_native_ec() {
    precision=$1
    link_script=$work/native/TESTING/EIG/CMakeFiles/xeigtst$precision.dir/link.txt
    destination=$trace/bin/xectst$precision-native
    set --
    while IFS= read -r name; do
        object_name=$name.f.o
        if grep -q "$object_name" "$link_script"; then
            set -- "$@" "$object_name=$trace/object/native/eig-$name.o"
        fi
    done <"$trace/eig-sources.list"
    while IFS= read -r name; do
        object_name=$name.f.o
        if grep -q "$object_name" "$link_script"; then
            set -- "$@" "$object_name=$trace/object/native/ec-$name.o"
        fi
    done <"$trace/ec-sources.list"
    python3 "$instrument" relink "$link_script" "$destination" "$@"
}

link_generated_balance() {
    precision=$1
    executable=$trace/bin/xbaltst$precision-generated
    set -- "$objects/TESTING/EIG/${precision}chkee.o"
    for object in "$objects"/TESTING/EIG/*.o; do
        base=$(basename "$object")
        case $base in
            schkee.o|dchkee.o|cchkee.o|zchkee.o|schkdmd.o|dchkdmd.o|cchkdmd.o|zchkdmd.o)
                continue
                ;;
        esac
        name=${base%.o}
        balance_replacement=$trace/object/generated/balance-$name.o
        eig_replacement=$trace/object/generated/eig-$name.o
        if [ -f "$balance_replacement" ]; then
            set -- "$@" "$balance_replacement"
        elif [ -f "$eig_replacement" ]; then
            set -- "$@" "$eig_replacement"
        else
            set -- "$@" "$object"
        fi
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
    "$cc" -std=c17 -O2 "$@" "$work/libf2c_lapack.a" -lm -o "$executable"
}

link_native_balance() {
    precision=$1
    link_script=$work/native/TESTING/EIG/CMakeFiles/xeigtst$precision.dir/link.txt
    destination=$trace/bin/xbaltst$precision-native
    set --
    while IFS= read -r name; do
        object_name=$name.f.o
        if grep -q "$object_name" "$link_script"; then
            set -- "$@" "$object_name=$trace/object/native/eig-$name.o"
        fi
    done <"$trace/eig-sources.list"
    while IFS= read -r name; do
        object_name=$name.f.o
        if grep -q "$object_name" "$link_script"; then
            set -- "$@" "$object_name=$trace/object/native/balance-$name.o"
        fi
    done <"$trace/balance-sources.list"
    python3 "$instrument" relink "$link_script" "$destination" "$@"
}

run_eig_case() {
    precision=$1
    input_name=$2
    threshold=$3
    input=$lapack/TESTING/$input_name.in
    generated_output=$trace/transcript/eig-$precision-$input_name-generated.out
    native_output=$trace/transcript/eig-$precision-$input_name-native.out
    generated_errors=$trace/transcript/eig-$precision-$input_name-generated.err
    native_errors=$trace/transcript/eig-$precision-$input_name-native.err
    report=$trace/report/eig-$precision-$input_name.json
    records=$trace/report/eig-$precision-$input_name-records.jsonl.gz
    if [ "$resume" = ON ] && [ -s "$report" ] && [ -s "$records" ]; then
        return
    fi
    (ulimit -f 409600; "$trace/bin/xeigtst$precision-generated" <"$input" \
        >"$generated_output" 2>"$generated_errors")
    (ulimit -f 409600; "$trace/bin/xeigtst$precision-native" <"$input" \
        >"$native_output" 2>"$native_errors")
    if [ -s "$generated_errors" ] || [ -s "$native_errors" ]; then
        echo "instrumented EIG driver wrote diagnostics: $precision $input_name" >&2
        tail -n 40 "$generated_errors" >&2
        tail -n 40 "$native_errors" >&2
        exit 1
    fi
    if ! python3 "$compare" eig "$generated_output" "$native_output" \
        --threshold "$threshold" --fail-on regression \
        --records "$records" --report "$report"; then
        differential_failures=$((differential_failures + 1))
    fi
}

run_eig_precision() {
    precision=$1
    prefix=$2
    for specification in \
        nep:20 sep:50 se2:50 svd:50 "${prefix}ed:20" \
        "${prefix}gg:20" "${prefix}gd:20" "${prefix}sb:20" "${prefix}sg:20" \
        "${prefix}bb:20" glm:20 gqr:20 gsv:20 csd:30 lse:20; do
        input_name=${specification%%:*}
        threshold=${specification#*:}
        run_eig_case "$precision" "$input_name" "$threshold"
    done
}

run_ec_case() {
    precision=$1
    input_name=${precision}ec
    input=$lapack/TESTING/$input_name.in
    generated_output=$trace/transcript/ec-$precision-generated.out
    native_output=$trace/transcript/ec-$precision-native.out
    generated_errors=$trace/transcript/ec-$precision-generated.err
    native_errors=$trace/transcript/ec-$precision-native.err
    report=$trace/report/ec-$precision.json
    records=$trace/report/ec-$precision-records.jsonl.gz
    if [ "$resume" = ON ] && [ -s "$report" ] && [ -s "$records" ]; then
        return
    fi
    (ulimit -f 409600; "$trace/bin/xectst$precision-generated" <"$input" \
        >"$generated_output" 2>"$generated_errors")
    (ulimit -f 409600; "$trace/bin/xectst$precision-native" <"$input" \
        >"$native_output" 2>"$native_errors")
    if [ -s "$generated_errors" ] || [ -s "$native_errors" ]; then
        echo "instrumented EC driver wrote diagnostics: $precision" >&2
        tail -n 40 "$generated_errors" >&2
        tail -n 40 "$native_errors" >&2
        exit 1
    fi
    if ! python3 "$compare" ec "$generated_output" "$native_output" \
        --threshold 20 --fail-on regression \
        --records "$records" --report "$report"; then
        differential_failures=$((differential_failures + 1))
    fi
}

run_balance_case() {
    precision=$1
    input_name=$2
    expected_records=$3
    input=$lapack/TESTING/$input_name.in
    generated_output=$trace/transcript/balance-$precision-$input_name-generated.out
    native_output=$trace/transcript/balance-$precision-$input_name-native.out
    generated_errors=$trace/transcript/balance-$precision-$input_name-generated.err
    native_errors=$trace/transcript/balance-$precision-$input_name-native.err
    report=$trace/report/balance-$precision-$input_name.json
    records=$trace/report/balance-$precision-$input_name-records.jsonl.gz
    if [ "$resume" = ON ] && [ -s "$report" ] && [ -s "$records" ]; then
        return
    fi
    (ulimit -f 409600; "$trace/bin/xbaltst$precision-generated" <"$input" \
        >"$generated_output" 2>"$generated_errors")
    (ulimit -f 409600; "$trace/bin/xbaltst$precision-native" <"$input" \
        >"$native_output" 2>"$native_errors")
    if [ -s "$generated_errors" ] || [ -s "$native_errors" ]; then
        echo "instrumented balance driver wrote diagnostics: $precision $input_name" >&2
        tail -n 40 "$generated_errors" >&2
        tail -n 40 "$native_errors" >&2
        exit 1
    fi
    if ! python3 "$compare" balance "$generated_output" "$native_output" \
        --fail-on regression --records "$records" --report "$report"; then
        differential_failures=$((differential_failures + 1))
    fi
    python3 -c 'import json,sys; actual=json.load(open(sys.argv[1]))["records"]; expected=int(sys.argv[2]); sys.exit(0 if actual == expected else 1)' \
        "$report" "$expected_records"
}

compile_eig_trace_sources
compile_ec_trace_sources
compile_balance_trace_sources
for precision in s d c z; do
    link_generated_eig "$precision"
    link_native_eig "$precision"
    link_generated_ec "$precision"
    link_native_ec "$precision"
    link_generated_balance "$precision"
    link_native_balance "$precision"
done

run_eig_precision s s
run_eig_precision d d
run_eig_precision c c
run_eig_precision z z
for precision in s d c z; do
    run_ec_case "$precision"
done
for precision in s d; do
    run_balance_case "$precision" "${precision}bal" 13
    run_balance_case "$precision" "${precision}bak" 7
    run_balance_case "$precision" "${precision}gbal" 8
    run_balance_case "$precision" "${precision}gbak" 16
done
for precision in c z; do
    run_balance_case "$precision" "${precision}bal" 13
    run_balance_case "$precision" "${precision}bak" 7
    run_balance_case "$precision" "${precision}gbal" 10
    run_balance_case "$precision" "${precision}gbak" 20
done

if ! python3 "$compare" manifest "$trace/report" \
    --fail-on regression --report "$trace/report/manifest.json"; then
    differential_failures=$((differential_failures + 1))
fi
python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); ok=(r["suites"] == 88 and r["records"] == 5504845 and r["generated_records"] == 5504162 and r["native_records"] == 5504279 and r["generated_only_records"] == 566 and r["native_only_records"] == 683); sys.exit(0 if ok else 1)' \
    "$trace/report/manifest.json"
if [ "$differential_failures" -ne 0 ]; then
    echo "exhaustive LAPACK differential found differences in $differential_failures checks" >&2
    exit 1
fi
