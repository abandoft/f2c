#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/f2c" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$root/build/scope-association-differential
cc=${CC:-cc}
fc=${FC:-gfortran}
cmake -E remove_directory "$work"
cmake -E make_directory "$work"
for name in scope_association scope_storage_bindings intrinsic_bindings intrinsic_alias_bindings; do
    "$1" "$root/test/fixtures/$name.f90" -o "$work/$name.c"
    "$cc" -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror "$work/$name.c" -lm -o "$work/$name"
    "$cc" -std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror -fsanitize=address,undefined \
        -fno-sanitize-recover=all "$work/$name.c" -lm -o "$work/$name-sanitized"
    "$work/$name" > "$work/$name.out"
    "$work/$name-sanitized" > "$work/$name-sanitized.out"
    cmp "$work/$name.out" "$work/$name-sanitized.out"
done
# COMMON is standard F90/F2008, but classified as obsolescent under F2018.
"$fc" -std=f2008 -pedantic-errors -O2 -Wall -Wextra -Werror -J"$work" \
    "$root/test/fixtures/scope_association.f90" -o "$work/native"
"$work/native" > "$work/native.out"
cmp "$work/scope_association.out" "$work/native.out"
"$fc" -std=f2008 -pedantic-errors -O2 -Wall -Wextra -Werror -J"$work" \
    "$root/test/fixtures/intrinsic_bindings.f90" -o "$work/native-intrinsics"
"$work/native-intrinsics" > "$work/native-intrinsics.out"
cmp "$work/intrinsic_bindings.out" "$work/native-intrinsics.out"
# F2018 15.5.5.2(3) permits USE-renamed intrinsic procedures. GNU 16.1
# omits these generic exports, so retain the independent executable contract.
printf '%s\n' 'intrinsic alias contracts passed' > "$work/intrinsic-alias-expected.out"
cmp "$work/intrinsic_alias_bindings.out" "$work/intrinsic-alias-expected.out"
# The separate DATA shadow fixture checks F2018 19.5.1.4 directly; GNU 16.1
# rejects that valid binding, so it is not used as an oracle for this case.
echo "scope association native differential and independent storage contracts passed"
