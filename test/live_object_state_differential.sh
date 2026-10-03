#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/live-object-state-differential
cc=${CC:-cc}
fc=${FC:-gfortran}
cmake -E remove_directory "$work"
cmake -E make_directory "$work"
"$1" "$root/test/fixtures/live_object_state.f90" -o "$work/generated.c"
"$1" "$root/test/fixtures/live_external_state.f90" -o "$work/generated_live_external_state.c"
"$1" "$root/test/fixtures/live_character_result_state.f90" -o "$work/character-result.c"
"$1" "$root/test/fixtures/live_namelist_transaction.f90" -o "$work/namelist-transaction.c"
"$1" "$root/test/fixtures/live_component_assignment.f90" -o "$work/component-assignment.c"
for optimization in 0 2 3; do
    "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/generated.c" -lm \
        -o "$work/generated-O$optimization"
    "$fc" -std=f2008 -pedantic-errors "-O$optimization" -Wall -Wextra -Werror \
        -J"$work" "$root/test/fixtures/live_object_state.f90" \
        -o "$work/native-O$optimization"
    "$work/generated-O$optimization" > "$work/generated-O$optimization.out"
    "$work/native-O$optimization" > "$work/native-O$optimization.out"
    test "$("$work/generated-O$optimization")" = 'live object state contracts passed'
    cmp "$work/generated-O$optimization.out" "$work/native-O$optimization.out"
    "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -I"$work" \
        "$root/test/generated/live_state_contracts.c" -lm -o "$work/external-O$optimization"
    "$work/external-O$optimization"
    # GNU Fortran 16.1 restores the old pointer length in this legal dynamic
    # result-specification case. Keep the unmodified source and its independent
    # assertions; do not count it as a passing native differential.
    "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/character-result.c" -lm \
        -o "$work/character-result-O$optimization"
    "$work/character-result-O$optimization"
    "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/namelist-transaction.c" -lm \
        -o "$work/namelist-transaction-O$optimization"
    "$work/namelist-transaction-O$optimization"
    # GNU Fortran 16.1 evaluates this original allocatable-component owner
    # subscript five times. Preserve the source and its independent once-only
    # contract rather than treating that compiler output as a passing oracle.
    "$cc" -std=c17 "-O$optimization" -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/component-assignment.c" -lm \
        -o "$work/component-assignment-O$optimization"
    "$work/component-assignment-O$optimization"
done
for source in generated.c character-result.c namelist-transaction.c component-assignment.c external; do
    input=$work/$source
    if [ "$source" = external ]; then
        input=$root/test/generated/live_state_contracts.c
    fi
    "$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -I"$work" \
        -fsanitize=address,undefined -fno-sanitize-recover=all "$input" -lm \
        -o "$work/$source-sanitized"
    "$work/$source-sanitized"
done
echo 'live object state differential passed'
