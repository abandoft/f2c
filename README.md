# F2C

[English](https://github.com/abandoft/f2c) · [中文文档](https://github.com/abandoft/f2c/blob/main/README-ZH.md)

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

Use `-` as an input or output path for pipelines, for example
`build/f2c - --free-form -o - < input.f90`. File outputs are staged before replacement; if any
requested artifact cannot be written, existing C and header outputs are preserved.

Source form is automatic by default. `.f`, `.for`, and `.ftn` use fixed form; modern extensions
such as `.f90` use free form. `--free-form` and `--fixed-form` override detection when a file name
does not describe its physical layout. `--comments` retains source lines as generated C comments.

Conditional preprocessing is explicit and case-sensitive. Use `-DNAME` or `-DNAME=EXPR` to
provide a definition and `-UNAME` to remove an earlier command-line definition, for example:

```sh
build/f2c -I include -DUSE_ISNAN=1 source.F90 -o output.c
```

The built-in contract supports object-like `#define` expansion, `#undef`, `#if`, `#ifdef`,
`#ifndef`, `#elif`, `#else`, `#endif`, `#include`, `#line`, numeric line markers, and standard
Fortran `INCLUDE`. Integer conditions implement normal precedence and short-circuit evaluation.
Macro diagnostics retain both expansion and definition spelling ranges. Function-like macros are
still rejected with a source-positioned error. No project- or platform-specific feature macro is
implicitly guessed. API and CLI definitions apply to every project input, while definitions made
inside a source file remain local to that input. `-I` configures CLI include directories; quoted
includes search the including file's directory first.

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

Long-running services can set request-local resource budgets without process-global state:

```c
F2cConfig config = {0};
config.structure_size = sizeof(config);
config.limits.max_input_bytes = 64U * 1024U * 1024U;
config.limits.max_output_bytes = 128U * 1024U * 1024U;

F2cPreprocessorDefinition definitions[] = {{"USE_ISNAN", "1"}};
config.preprocessor_definitions = definitions;
config.preprocessor_definition_count = 1U;

/* Optional: provide #include/INCLUDE sources without binding the library to a file system. */
config.include_resolver = resolve_include;
config.include_release = release_include;
config.include_user_data = resolver_context;

F2cResult result = f2c_transpile_project_config(inputs, input_count, &config);
```

A zero limit selects the corresponding `F2C_DEFAULT_*` value. Budgets cover aggregate input and
retained preprocessed bytes, conditional definitions, macro/include depth and include count,
logical lines, canonical tokens, expression AST nodes and depth, constant-evaluation work,
diagnostics, diagnostic bytes, and each generated code/header artifact. Limit failures return no
partial generated C. Include resolution is request-local and callback-driven, so the core library
does not require a file system. A synchronous `F2cDiagnosticCallback` can additionally consume
structured diagnostic categories, severity, expansion/spelling source ranges, and messages
without parsing the text rendering.
`structure_size` must equal `sizeof(F2cConfig)`. The project has not frozen a public ABI yet, so
older or larger configuration layouts are rejected and all fields belong to the current API.

## Support status

The currently tested implementation includes:

- normalized free and fixed source forms, continuations, labels, bounded object-macro and
  conditional preprocessing, callback-provided includes, line remapping, program units, internal
  procedures, modules, host association, and `USE` association;
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
- exhaustive internal audit artifacts for 100 numerical drivers and 5,807,798 union records;
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

Tagged release assets include SHA-256 checksums and GitHub artifact attestations:

```sh
sha256sum -c f2c-<version>-<platform>.<archive>.sha256
gh attestation verify f2c-<version>-<platform>.<archive> --repo abandoft/f2c
```

On macOS, use `shasum -a 256 -c` for the checksum file.

## License

* The code in [f2c](https://github.com/abandoft/f2c) is licensed under the [MIT License](LICENSE).
* The code in [netlib-f2c](https://www.netlib.org/f2c) remains subject to the original project’s license.

> **Note:** `f2c` is a newly refactored project and does not depend on `netlib-f2c`.
