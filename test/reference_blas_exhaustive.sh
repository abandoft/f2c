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
core=$root/build/reference-lapack-core
lapack=$core/lapack
work=$core/blas-exhaustive
instrument=$root/test/blas_trace_instrument.py
relink=$root/test/lapack_trace_instrument.py
compare=$root/test/exhaustive_result_diff.py
cc=${CC:-cc}
fc=${FC:-gfortran}
strict_cflags='-std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror'
differential_failures=0

if [ ! -x "$f2c" ] || [ ! -f "$core/libf2c_lapack.a" ] ||
    [ ! -x "$core/native/bin/xblat1s" ]; then
    echo "run reference_lapack_core_compile.sh before the exhaustive BLAS gate" >&2
    exit 2
fi
if ! command -v "$fc" >/dev/null 2>&1; then
    echo "a native Fortran compiler is required for exhaustive BLAS testing" >&2
    exit 2
fi

rm -rf "$work"
mkdir -p "$work/source" "$work/c" "$work/object" "$work/bin" \
    "$work/case" "$work/transcript" "$work/report"

run_suite() {
    precision=$1
    level=$2
    suite=${precision}blat${level}
    source=$lapack/BLAS/TESTING/$suite.f
    traced_source=$work/source/$suite.f
    generated_c=$work/c/$suite.c
    native_object=$work/object/$suite-native.o
    generated_executable=$work/bin/$suite-generated
    native_executable=$work/bin/$suite-native
    generated_case=$work/case/$suite-generated
    native_case=$work/case/$suite-native
    generated_output=$work/transcript/$suite-generated.out
    native_output=$work/transcript/$suite-native.out
    generated_errors=$work/transcript/$suite-generated.err
    native_errors=$work/transcript/$suite-native.err
    report=$work/report/$suite.json
    records=$work/report/$suite-records.jsonl.gz

    if [ "$level" -eq 1 ]; then
        expected_sites=8
        if [ "$precision" = d ]; then
            expected_sites=12
        fi
        python3 "$instrument" level1 "$source" "$traced_source" \
            --expected-sites "$expected_sites"
        mode=blas1
        generated_trace=$generated_output
        native_trace=$native_output
    else
        python3 "$instrument" level23 "$source" "$traced_source" \
            --expected-sites 6
        mode=blas
        generated_trace=$generated_errors
        native_trace=$native_errors
    fi

    "$f2c" "$traced_source" -o "$generated_c"
    # shellcheck disable=SC2086
    "$cc" $strict_cflags -DF2C_FP_CONTRACT=1 "$generated_c" \
        "$core/libf2c_lapack.a" -lm -o "$generated_executable"
    "$fc" -O2 -c "$traced_source" -o "$native_object"
    link_script=$core/native/BLAS/TESTING/CMakeFiles/xblat${level}${precision}.dir/link.txt
    python3 "$relink" relink "$link_script" "$native_executable" \
        "$suite.f.o=$native_object"

    mkdir -p "$generated_case" "$native_case"
    if [ "$level" -eq 1 ]; then
        (cd "$generated_case"; "$generated_executable" \
            >"$generated_output" 2>"$generated_errors")
        (cd "$native_case"; "$native_executable" \
            >"$native_output" 2>"$native_errors")
    else
        input=$lapack/BLAS/TESTING/$suite.in
        (cd "$generated_case"; "$generated_executable" <"$input" \
            >"$generated_output" 2>"$generated_errors")
        (cd "$native_case"; "$native_executable" <"$input" \
            >"$native_output" 2>"$native_errors")
    fi
    if [ "$level" -eq 1 ]; then
        if [ -s "$generated_errors" ]; then
            echo "instrumented generated BLAS suite wrote diagnostics: $suite" >&2
            tail -n 40 "$generated_errors" >&2
            exit 1
        fi
        if grep -Ev '^(Note: The following floating-point exceptions are signalling:.*|[[:space:]]*IEEE_[A-Z_]+([[:space:]]+IEEE_[A-Z_]+)*)$' \
            "$native_errors" | grep -q .; then
            echo "instrumented native BLAS suite wrote diagnostics: $suite" >&2
            tail -n 40 "$native_errors" >&2
            exit 1
        fi
    else
        for trace_output in "$generated_errors" "$native_errors"; do
            if ! grep -q '@' "$trace_output" ||
                grep -v '@' "$trace_output" | grep -q '[^[:space:]]'; then
                echo "instrumented BLAS trace is empty or contains diagnostics: $suite" >&2
                tail -n 40 "$trace_output" >&2
                exit 1
            fi
        done
    fi
    if ! python3 "$compare" "$mode" "$generated_trace" "$native_trace" \
        --threshold 16 --fail-on regression \
        --records "$records" --report "$report"; then
        differential_failures=$((differential_failures + 1))
    fi
    case $level:$precision in
        1:s|1:d) expected_records=5162 ;;
        1:c|1:z) expected_records=4178 ;;
        2:s|2:d) expected_records=33776 ;;
        2:c|2:z) expected_records=35552 ;;
        3:s|3:d) expected_records=34470 ;;
        3:c|3:z) expected_records=38410 ;;
        *) echo "unsupported BLAS trace suite: $suite" >&2; exit 2 ;;
    esac
    python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); expected=int(sys.argv[2]); ok=(r["records"] == expected and r["generated_records"] == expected and r["native_records"] == expected and r["generated_only_records"] == 0 and r["native_only_records"] == 0); sys.exit(0 if ok else 1)' \
        "$report" "$expected_records"
}

python3 "$instrument" self-test
for precision in s d c z; do
    for level in 1 2 3; do
        run_suite "$precision" "$level"
    done
done

if ! python3 "$compare" manifest "$work/report" \
    --fail-on regression --report "$work/report/manifest.json"; then
    differential_failures=$((differential_failures + 1))
fi
python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); sys.exit(0 if r["suites"] == 12 and r["records"] == 303096 else 1)' \
    "$work/report/manifest.json"
if [ "$differential_failures" -ne 0 ]; then
    echo "exhaustive BLAS differential found differences in $differential_failures checks" >&2
    exit 1
fi
