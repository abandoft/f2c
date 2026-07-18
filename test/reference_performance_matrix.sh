#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 /path/to/f2c [full|daxpy|dgemv|dgemm|dgetf2|extended]" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
f2c=$1
scope=${2:-full}
case $scope in
    full) expected_count=71 ;;
    daxpy) expected_count=6 ;;
    dgemv) expected_count=4 ;;
    dgemm) expected_count=9 ;;
    dgetf2) expected_count=3 ;;
    extended) expected_count=49 ;;
    *)
        echo "unsupported performance scope: $scope" >&2
        exit 2
        ;;
esac
work=$root/build/benchmarks/performance-matrix
raw=$work/raw.log
rm -rf "$work"
mkdir -p "$work"
: >"$raw"

run_benchmark() {
    name=$1
    script=$2
    output=$work/$name.out
    if ! sh "$root/test/$script" "$f2c" >"$output" 2>&1; then
        cat "$output" >&2
        exit 1
    fi
    cat "$output"
    cat "$output" >>"$raw"
}

case $scope in
    full)
        run_benchmark daxpy reference_daxpy_benchmark.sh
        run_benchmark dgemv reference_dgemv_benchmark.sh
        run_benchmark dgemm reference_dgemm_benchmark.sh
        run_benchmark dgetf2 reference_dgetf2_benchmark.sh
        run_benchmark extended-matrix performance/reference_matrix.sh
        ;;
    daxpy) run_benchmark daxpy reference_daxpy_benchmark.sh ;;
    dgemv) run_benchmark dgemv reference_dgemv_benchmark.sh ;;
    dgemm) run_benchmark dgemm reference_dgemm_benchmark.sh ;;
    dgetf2) run_benchmark dgetf2 reference_dgetf2_benchmark.sh ;;
    extended) run_benchmark extended-matrix performance/reference_matrix.sh ;;
esac

csv=$work/results.csv
json=$work/results.json
summary=$work/summary.md
awk -F, '
    BEGIN { print "kernel,case,generated_c_seconds,fortran_seconds,ratio" }
    /^F2C_PERF,/ { print $2 ",\"" $3 "\"," $4 "," $5 "," $6 }
' "$raw" >"$csv"

count=$(awk -F, 'NR > 1 { ++count } END { print count + 0 }' "$csv")
if [ "$count" -ne "$expected_count" ]; then
    echo "performance scope $scope expected $expected_count cases, found $count" >&2
    exit 1
fi

awk -F, '
    BEGIN { print "[" }
    NR > 1 {
        gsub(/^"|"$/, "", $2)
        if (seen++) print ","
        printf "  {\"kernel\":\"%s\",\"case\":\"%s\",\"generated_c_seconds\":%s,\"fortran_seconds\":%s,\"ratio\":%s}", $1, $2, $3, $4, $5
    }
    END { print "\n]" }
' "$csv" >"$json"
python3 -m json.tool "$json" >/dev/null

awk -F, -v scope="$scope" '
    BEGIN {
        print "# f2c performance matrix"
        print ""
        print "Scope: `" scope "`"
        print ""
        print "Parity criterion: every generated-C case must be within 5% of the matching Fortran build."
        print ""
        print "| Kernel | Case | Generated C (s) | Fortran (s) | Ratio |"
        print "|---|---:|---:|---:|---:|"
    }
    NR > 1 {
        gsub(/^"|"$/, "", $2)
        printf "| %s | %s | %.6f | %.6f | %.3f |\n", $1, $2, $3, $4, $5
        logarithms += log($5)
        if ($5 > maximum) maximum = $5
        ++count
    }
    END {
        print ""
        printf "Geometric-mean ratio: %.3f  \n", exp(logarithms / count)
        printf "Worst-case ratio: %.3f\n", maximum
    }
' "$csv" >"$summary"

status=0
awk -F, '
    NR > 1 {
        ratio = $5 + 0.0
        if (ratio > 1.05) {
            printf "performance regression: %s %s ratio %.3f exceeds 1.050\n", $1, $2, ratio > "/dev/stderr"
            failed = 1
        }
    }
    END { exit failed }
' "$csv" || status=$?

cat "$summary"
echo "performance reports: $csv $json $summary"
exit "$status"
