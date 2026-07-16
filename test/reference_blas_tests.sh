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
generated=$work/blas-testing/generated
generated_results=$work/blas-testing/generated-results
native_results=$work/blas-testing/native-results
reports=$work/differential-reports/blas
strict_cflags='-std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror'

if [ ! -x "$f2c" ] || [ ! -f "$work/libf2c_lapack.a" ] ||
    [ ! -d "$lapack/BLAS/TESTING" ] || [ ! -x "$work/native/bin/xblat1s" ]; then
    echo "run reference_lapack_core_compile.sh before the official BLAS gate" >&2
    exit 2
fi

rm -rf "$work/blas-testing" "$reports"
mkdir -p "$generated" "$generated_results" "$native_results" "$reports"

result_groups=0
computational_calls=0
for precision in s d c z; do
    for level in 1 2 3; do
        suite=${precision}blat${level}
        source=$lapack/BLAS/TESTING/$suite.f
        executable=$generated/xblat${level}${precision}
        generated_case=$generated_results/$suite
        native_case=$native_results/$suite
        report=$reports/$suite.json

        "$f2c" "$source" -o "$generated/$suite.c"
        # shellcheck disable=SC2086
        "${CC:-cc}" $strict_cflags "$generated/$suite.c" \
            "$work/libf2c_lapack.a" -lm -o "$executable"
        mkdir -p "$generated_case" "$native_case"

        if [ "$level" -eq 1 ]; then
            (cd "$generated_case"; "$executable" >stdout 2>stderr)
            (cd "$native_case"; "$work/native/bin/xblat${level}${precision}" \
                >stdout 2>stderr)
            generated_output=$generated_case/stdout
            native_output=$native_case/stdout
            mode=level1
        else
            input=$lapack/BLAS/TESTING/$suite.in
            summary=$(sed -n "1s/^'\([^']*\)'.*/\1/p" "$input")
            if [ -z "$summary" ]; then
                echo "cannot determine summary output name from $input" >&2
                exit 1
            fi
            (cd "$generated_case"; "$executable" <"$input" >stdout 2>stderr)
            (cd "$native_case"; "$work/native/bin/xblat${level}${precision}" \
                <"$input" >stdout 2>stderr)
            generated_output=$generated_case/$summary
            native_output=$native_case/$summary
            mode=level23
        fi

        if [ -s "$generated_case/stderr" ]; then
            echo "official BLAS suite $suite wrote diagnostics" >&2
            tail -n 40 "$generated_case/stderr" >&2
            exit 1
        fi
        # The Level 1 extreme-value tests deliberately raise IEEE flags.
        # gfortran reports those flags at normal program termination; retain
        # the native transcript but reject every other diagnostic.
        if grep -Ev '^(Note: The following floating-point exceptions are signalling:.*|[[:space:]]*IEEE_[A-Z_]+([[:space:]]+IEEE_[A-Z_]+)*)$' \
            "$native_case/stderr" | grep -q .; then
            echo "native official BLAS suite $suite wrote diagnostics" >&2
            tail -n 40 "$native_case/stderr" >&2
            exit 1
        fi
        python3 "$root/test/blas_result_diff.py" "$mode" \
            "$generated_output" "$native_output" --suite "$suite" --report "$report"
        groups=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["result_groups"])' "$report")
        calls=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["computational_calls"])' "$report")
        result_groups=$((result_groups + groups))
        computational_calls=$((computational_calls + calls))
    done
done

if [ "$result_groups" -ne 146 ] || [ "$computational_calls" -ne 262388 ]; then
    echo "official BLAS coverage mismatch: groups=$result_groups calls=$computational_calls" >&2
    exit 1
fi
echo "differentially matched all 146 official BLAS result groups"
echo "matched all 262,388 Level 2/3 computational calls and 46 Level 1 routine results"
