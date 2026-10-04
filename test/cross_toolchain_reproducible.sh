#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 /path/to/first/f2c /path/to/second/f2c" >&2
    exit 2
fi

FIRST=$1
SECOND=$2
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/reproducible-toolchains

cmake -E remove_directory "$WORK"
cmake -E make_directory "$WORK/first" "$WORK/second"

generate_outputs() {
    translator=$1
    output=$2
    "$translator" "$ROOT/test/fixtures/character_abi.f90" \
        -o "$output/character.c" --header "$output/character.h"
    "$translator" "$ROOT/test/fixtures/assigned_goto.f" \
        -o "$output/fixed.c" --header "$output/fixed.h"
    "$translator" \
        "$ROOT/test/fixtures/project_caller.f90" \
        "$ROOT/test/fixtures/project_definition.f90" \
        -o "$output/project.c" --header "$output/project.h"
    "$translator" "$ROOT/test/fixtures/optional_arguments.f90" \
        -o "$output/optional.c" --header "$output/optional.h"
    "$translator" "$ROOT/test/fixtures/explicit_interface.f90" \
        -o "$output/interface.c" --header "$output/interface.h"
    "$translator" "$ROOT/test/fixtures/procedure_interface.f90" \
        -o "$output/procedure.c" --header "$output/procedure.h"
    "$translator" "$ROOT/test/fixtures/deferred_character.f90" \
        -o "$output/deferred.c" --header "$output/deferred.h"
    for fixture in function_result scalar_result result_identity result_snapshot result_kinds \
        constructor_result constructor_finalization result_external \
        allocation_result allocation_result_values allocation_result_finalization \
        allocation_controls allocation_control_values allocation_control_bounds allocation_native_bounds \
        allocation_control_finalization allocation_control_errors allocation_control_unaligned \
        allocation_control_pointers allocation_control_names \
        extremum_boundaries extremum_policy extremum_arguments \
        lexical_literals numeric_model_intrinsics elemental_procedure operator_kinds operator_power_policy operator_contract; do
        "$translator" "$ROOT/test/fixtures/$fixture.f90" \
            -o "$output/$fixture.c" --header "$output/$fixture.h"
    done
    "$translator" --version > "$output/version.txt"
}

generate_outputs "$FIRST" "$WORK/first"
generate_outputs "$SECOND" "$WORK/second"

FILES='character.c character.h fixed.c fixed.h project.c project.h optional.c optional.h
interface.c interface.h procedure.c procedure.h deferred.c deferred.h version.txt
function_result.c function_result.h scalar_result.c scalar_result.h
result_identity.c result_identity.h result_snapshot.c result_snapshot.h result_kinds.c result_kinds.h
constructor_result.c constructor_result.h constructor_finalization.c constructor_finalization.h
result_external.c result_external.h
allocation_result.c allocation_result.h allocation_result_values.c allocation_result_values.h
allocation_result_finalization.c allocation_result_finalization.h
allocation_controls.c allocation_controls.h allocation_control_values.c allocation_control_values.h
allocation_control_bounds.c allocation_control_bounds.h
allocation_native_bounds.c allocation_native_bounds.h
allocation_control_finalization.c allocation_control_finalization.h
allocation_control_errors.c allocation_control_errors.h
allocation_control_unaligned.c allocation_control_unaligned.h
allocation_control_pointers.c allocation_control_pointers.h
allocation_control_names.c allocation_control_names.h
extremum_boundaries.c extremum_boundaries.h extremum_policy.c extremum_policy.h
extremum_arguments.c extremum_arguments.h
lexical_literals.c lexical_literals.h numeric_model_intrinsics.c numeric_model_intrinsics.h
elemental_procedure.c elemental_procedure.h
operator_kinds.c operator_kinds.h operator_power_policy.c operator_power_policy.h
operator_contract.c operator_contract.h'
for file in $FILES; do
    if ! cmake -E compare_files "$WORK/first/$file" "$WORK/second/$file"; then
        echo "cross-toolchain generated output differs: $file" >&2
        exit 1
    fi
done

# Deliberate splitting: FILES contains only fixed, repository-owned artifact names.
# shellcheck disable=SC2086
(cd "$WORK/first" && cmake -E sha256sum $FILES) \
    > "$WORK/SHA256SUMS"
echo "cross-toolchain reproducibility: all compared outputs are byte-identical"
