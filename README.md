# F2C

[English](README.md) · [中文文档](README-ZH.md)

A modern, high-performance Fortran-to-C17 transpiler designed to generate portable,
production-grade C code. It is written in C17 and requires no additional generated-code runtime.

## Highlights

- Automatic free-form and fixed-form source detection, with explicit CLI overrides.
- Typed expression and statement ASTs with kind, rank, shape, and value-category metadata.
- Runtime-free C17 output intended for desktop, server, mobile, and WebAssembly toolchains.
- Strict cross-platform CI, sanitizers, deterministic-output checks, fuzzing, BLAS/LAPACK
  differential validation, and performance gates.

## Requirements and build

The project requires CMake 3.20 or newer and a C17 compiler. Python 3 and gfortran are additionally
required for the complete numerical differential suite.

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DF2C_BUILD_TESTING=ON \
  -DF2C_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

All generated files and local build products must stay below the root `build/` directory. The
project deliberately has no CMake installation rules.

## Command-line usage

Translate one source file and compile the generated C:

```sh
build/f2c input.f90 -o output.c
cc -std=c17 -O3 output.c -lm -o output
```

Translate several files as one project and emit a shared C interface:

```sh
build/f2c caller.f90 implementation.f90 -o project.c --header project.h
```

Source form is automatic by default. `.f`, `.for`, and `.ftn` use fixed form; modern extensions
such as `.f90` use free form. `--free-form` and `--fixed-form` override detection when a file name
does not describe its physical layout. `--comments` retains source lines as generated C comments.

Run `build/f2c --help` for the complete CLI reference.

## Library API

The public interface in [`include/f2c/f2c.h`](include/f2c/f2c.h) supports in-memory translation:

```c
#include <f2c/f2c.h>

F2cOptions options = {"input.f90", F2C_SOURCE_AUTO, 0};
F2cResult result = f2c_transpile(source, source_length, &options);

if (result.error_count == 0U) {
    /* result.code contains self-contained C17. */
}

f2c_result_free(&result);
```

Use `f2c_transpile_project` with an `F2cInput[]` array to translate several sources with a shared
procedure registry. `result.header` contains declarations for the external procedures defined by
that project. On a hard error, `code` and `header` are `NULL` and `diagnostics` contains actionable
file, line, and column information.

## Support status

The currently tested implementation includes:

- normalized free and fixed source forms, continuations, labels, preprocessing used by LAPACK,
  program units, internal procedures, modules, host association, and `USE` association;
- intrinsic numeric, logical, CHARACTER, and complex types; explicit and implicit typing; typed
  expressions, array constructors, sections, vector subscripts, reductions, and selected
  transformational intrinsics;
- explicit, abstract, generic, and procedure-pointer interfaces; positional, keyword, optional,
  and procedure arguments on the supported ABI paths;
- allocatable and pointer objects, descriptors, automatic reallocation, `SOURCE=`, `MOLD=`,
  `MOVE_ALLOC`, derived components, intrinsic assignment, and ownership cleanup;
- derived types, inheritance, dynamic type tags, `SELECT TYPE`, type-bound dispatch, exact-rank
  `FINAL` procedures, and construct-scope finalization on supported control-flow paths;
- length-aware CHARACTER assignment and comparison, substrings, arrays, deferred lengths, function
  results, and the gfortran-compatible trailing-length ABI used by the validation corpus;
- structured and legacy control flow, formatted and list-directed I/O, internal files, nonadvancing
  I/O, defined I/O, and recursive NAMELIST handling on the documented paths;
- `RESHAPE`, `PACK`, `UNPACK`, `SPREAD`, `CSHIFT`, `EOSHIFT`, and `FINDLOC` lowering for the tested
  numeric, CHARACTER, and derived-type combinations.

Important remaining work includes complete token-stream coverage for declarations and modules,
all kind/rank and arbitrary-array-expression combinations, complete module generics and submodules,
dynamic polymorphic allocation, named/association construct finalization boundaries, every
formatted-I/O layout rule, pointer reassociation during NAMELIST input, and multi-compiler ABI
certification. Unsupported semantics must produce diagnostics rather than plausible but incorrect
C. The detailed checklist is maintained in [TODO.md](TODO.md).

## Validation

The pinned Reference LAPACK corpus currently provides these automated gates:

- 155/155 Reference BLAS and 3,535/3,535 BLAS/LAPACK/INSTALL/TESTING sources translated and
  warning-clean compiled as strict C17;
- a generated BLAS/LAPACK archive executing an end-to-end `DGESV` solve;
- all four INSTALL checks and 52,512 S/D/C/Z RFP checks matched against native Fortran;
- official BLAS Level 1/2/3, complete S/D/C/Z LIN, and all 80 EIG input suites differentially
  checked against the same pinned native build;
- exhaustive internal audit artifacts for 100 numerical drivers and 5,807,941 union records;
- a 71-case generated-C/native-Fortran performance matrix with a per-case 1.05 ratio ceiling.

The exhaustive audit intentionally records finite rounding differences, unmatched internal
records, and NaN-policy differences. Passing it means no new generated-side official-threshold or
coverage regression; it does not claim bitwise equivalence with native Fortran.

Representative local gates are:

```sh
sh test/reference_blas_compile.sh build/f2c
sh test/reference_lapack_core_compile.sh build/f2c
sh test/reference_blas_tests.sh build/f2c
sh test/reference_lapack_lin.sh build/f2c
sh test/reference_lapack_eig.sh build/f2c
sh test/reference_blas_exhaustive.sh build/f2c
sh test/reference_lapack_exhaustive.sh build/f2c
sh test/reference_performance_matrix.sh build/f2c
```

CI responsibilities and trigger policies are documented in
[`.github/workflows/README.md`](.github/workflows/README.md).

## Project layout

```text
include/f2c/   Public embedding API
src/cli/       Command-line adapter
src/core/      Public API implementation and pipeline orchestration
src/frontend/  Source normalization, program units, declarations, and interfaces
src/semantic/  Types, constants, intrinsics, CHARACTER, and procedure semantics
src/ast/       Owned expression/statement AST, lexer, parser, and visitors
src/codegen/   C17 lowering, arrays, I/O, ownership, types, and program units
src/internal/  Private cross-module data structures and interfaces
test/          Unit, execution, differential, fuzz, and performance tests
```

## Contributing

Focused changes that improve portable C17 generation and auditable Fortran semantics are welcome.
Please preserve these project invariants:

- use the project name `f2c` and keep all generated artifacts below `build/`;
- do not change archived sources or the project scope specification;
- generated C may depend on ISO C17, libc, and libm only—do not add a separate runtime;
- extend the typed AST/statement IR instead of reparsing generated text;
- diagnose unsupported semantics instead of emitting an approximate implementation;
- preserve unrelated worktree changes and format C code with the repository `.clang-format`.

Every parser, semantic, ABI, ownership, control-flow, I/O, or code-generation change should add an
executed regression. Run the strict CMake/CTest commands above before submitting a change. Broad
translator changes should also run the affected BLAS/LAPACK gates; performance-sensitive changes
must run `test/reference_performance_matrix.sh` on an otherwise idle machine.

## Security

The current default branch and latest tagged release receive security fixes. Do not disclose a
suspected vulnerability in a public issue, discussion, or pull request. Use GitHub private
vulnerability reporting and include, when available:

- the affected version or commit, platform, and C compiler;
- the smallest Fortran input that reproduces the issue;
- generated C that can be shared safely;
- impact, sanitizer diagnostics, and resource-usage observations.

Generated-code memory safety, parser resource exhaustion, path handling, release provenance, and
unexpected access outside the requested inputs are treated as security issues. Maintainers will
acknowledge, reproduce, assess, and coordinate disclosure through the private report.

Tagged release assets include SHA-256 checksums, SPDX JSON SBOMs, and GitHub artifact attestations:

```sh
sha256sum -c f2c-<version>-<platform>.<archive>.sha256
gh attestation verify f2c-<version>-<platform>.<archive> --repo abandoft/f2c
```

On macOS, use `shasum -a 256 -c` for the checksum file.
