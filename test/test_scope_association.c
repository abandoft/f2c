#include "ast/declaration/bindings.h"
#include "f2c/f2c.h"
#include "frontend/declaration/symbol.h"
#include "frontend/frontend.h"
#include "internal/context.h"
#include "semantic/intrinsic.h"
#include "semantic/scope.h"
#include "semantic/symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check_reset_identity(void) {
    Context context = {0};
    char dummy[] = "actual";
    char *arguments[] = {dummy};
    Unit unit = {.context = &context, .begin = 77U, .arguments = arguments, .argument_count = 1U};
    Symbol *symbol = f2c_ensure_symbol(&unit, "captured");
    size_t index;
    if (symbol == NULL) {
        ++failures;
        free(unit.symbols);
        return;
    }
    symbol->association = F2C_ASSOCIATION_HOST;
    symbol->argument = 1;
    symbol->host_associated = 1;
    symbol->saved = 1;
    symbol->volatile_entity = 1;
    symbol->intrinsic = f2c_find_intrinsic("max");
    if (!f2c_reset_associated_symbol(&unit, symbol) || symbol->argument || symbol->saved ||
        symbol->host_associated || symbol->volatile_entity || symbol->intrinsic != NULL ||
        symbol->association != F2C_ASSOCIATION_LOCAL || symbol->declaration_scope_id != 78U)
        ++failures;
    symbol = f2c_ensure_symbol(&unit, "actual");
    if (symbol == NULL || !f2c_reset_associated_symbol(&unit, symbol) || !symbol->argument)
        ++failures;
    for (index = 0U; index < unit.symbol_count; ++index)
        f2c_discard_symbol(&unit.symbols[index]);
    free(unit.symbols);
}

static void check_scope_identity(void) {
    Context context = {0};
    Unit current = {.context = &context, .begin = 27U};
    Unit host = {.begin = 44U};
    Unit interface_scope = {.begin = 55U};
    Unit module = {.begin = 11U, .interfaces = &interface_scope, .interface_count = 1U};
    Unit relocated = module;
    Symbol symbol = {.declaration_scope_id = 12U, .parameter = 1};
    context.modules.items = &module;
    context.modules.count = 1U;
    if (f2c_symbol_declaration_scope(&current, &symbol) != &module)
        ++failures;
    context.modules.items = &relocated;
    if (f2c_symbol_declaration_scope(&current, &symbol) != &relocated ||
        f2c_symbol_specification_scope(&current, &symbol) != &relocated)
        ++failures;
    symbol.declaration_scope_id = 56U;
    if (f2c_symbol_declaration_scope(&current, &symbol) != &interface_scope)
        ++failures;
    symbol.declaration_scope_id = 45U;
    current.signature_host = &host;
    if (f2c_symbol_declaration_scope(&current, &symbol) != &host)
        ++failures;
    symbol.declaration_scope_id = 999U;
    if (f2c_symbol_declaration_scope(&current, &symbol) != NULL)
        ++failures;
    symbol.parameter = 0;
    if (f2c_symbol_specification_scope(&current, &symbol) != &current)
        ++failures; /* Dynamic HOST capture specifications use the caller's descriptor aliases. */
    symbol.module_entity = 1;
    if (f2c_symbol_specification_scope(&current, &symbol) != NULL)
        ++failures;
}

static void check_expansion_budget(void) {
    static const char *const declarations[] = {
        "integer :: values(1000000000) = 3", "real :: values(1000000000) = 1.5",
        "complex :: values(1000000000) = (1.0, 2.0)", "logical :: values(1000000000) = .true."};
    size_t index;
    for (index = 0U; index < sizeof(declarations) / sizeof(declarations[0]); ++index) {
        char source[256];
        F2cInput input;
        F2cConfig config = {.structure_size = sizeof(config)};
        F2cResult result;
        (void)snprintf(source, sizeof(source), "module bounded\n %s\nend module\n",
                       declarations[index]);
        input = (F2cInput){source, strlen(source), {"bounded.f90", F2C_SOURCE_FREE, 0}};
        config.limits.max_output_bytes = 65536U;
        result = f2c_transpile_project_config(&input, 1U, &config);
        if (result.code != NULL || result.error_count == 0U || result.diagnostics == NULL ||
            strstr(result.diagnostics, "generated output limit") == NULL) {
            fprintf(stderr, "FAIL: static broadcast output budget\n%s\n",
                    result.diagnostics != NULL ? result.diagnostics : "");
            ++failures;
        }
        f2c_result_free(&result);
    }
}

static void check_source(const char *source, int accepted, const char *needle) {
    const F2cOptions options = {"scope-association.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    const char *text = accepted ? result.code : result.diagnostics;
    if ((accepted && (result.code == NULL || result.error_count != 0U)) ||
        (!accepted && (result.code != NULL || result.error_count == 0U)) ||
        (needle != NULL && (text == NULL || strstr(text, needle) == NULL))) {
        fprintf(stderr, "FAIL: scope association contract\n%s\n",
                result.diagnostics != NULL ? result.diagnostics : "");
        ++failures;
    }
    f2c_result_free(&result);
}

static void check_bindings(const char *source, const char *expected) {
    F2cTokenStream stream;
    F2cToken *tokens = NULL;
    size_t count = 0U;
    Line line = {0};
    F2cDeclarationBindingsSyntax syntax;
    char names[256] = {0};
    size_t length = 0U;
    size_t index;
    f2c_token_stream_init(&stream, source, 41U, 3U);
    for (;;) {
        F2cToken *replacement;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        replacement = (F2cToken *)realloc(tokens, (count + 1U) * sizeof(*tokens));
        if (replacement == NULL) {
            free(tokens);
            ++failures;
            return;
        }
        tokens = replacement;
        tokens[count++] = stream.token;
    }
    line.text = (char *)source;
    line.number = 41U;
    line.tokens = tokens;
    line.token_count = count;
    if (f2c_parse_declaration_bindings_syntax(&line, &syntax) < 0) {
        ++failures;
    } else {
        for (index = 0U; index < syntax.count; ++index) {
            const F2cToken *token = syntax.names[index];
            if (length + token->length + 2U >= sizeof(names)) {
                ++failures;
                break;
            }
            if (index != 0U)
                names[length++] = ',';
            memcpy(names + length, token->begin, token->length);
            length += token->length;
            names[length] = '\0';
            if (token->line != 41U || token->column < 3U)
                ++failures;
        }
        if (strcmp(names, expected) != 0) {
            fprintf(stderr, "FAIL: bindings for %s: %s != %s\n", source, names, expected);
            ++failures;
        }
    }
    f2c_declaration_bindings_syntax_discard(&syntax);
    free(tokens);
}

static void check_deep_storage_bindings(void) {
    const size_t depth = 512U;
    char *source = (char *)malloc(depth * 16U + 32U);
    size_t length = 0U;
    size_t index;
    if (source == NULL) {
        ++failures;
        return;
    }
    memcpy(source, "data ", 5U);
    length = 5U;
    for (index = 0U; index < depth; ++index)
        source[length++] = '(';
    memcpy(source + length, "object(1)", 9U);
    length += 9U;
    for (index = 0U; index < depth; ++index) {
        memcpy(source + length, ",i=1,1)", 7U);
        length += 7U;
    }
    memcpy(source + length, " /1/", 5U);
    check_bindings(source, "object");
    free(source);
}

int main(void) {
    check_reset_identity();
    check_scope_identity();
    check_expansion_budget();
    check_deep_storage_bindings();
    check_source("program builtins\n implicit none\n intrinsic :: max, conjg\n"
                 "integer :: n\n complex :: z\n n=max(2,5)\n z=conjg((1.0,2.0))\n"
                 "end program\n",
                 1, NULL);
    check_source("program typed_builtin\n implicit none\n real, intrinsic :: sqrt\n"
                 "real :: x\n x=sqrt(4.0)\n end program\n",
                 1, NULL);
    check_source("program invalid\n real, intrinsic :: cpu_time\n end program\n", 0,
                 "cannot have a declared result type");
    check_source("function invalid() result(sin)\n intrinsic :: sin\n end function\n", 0,
                 "cannot have EXTERNAL");
    check_source("program invalid\n intrinsic :: nonexistent\n end program\n", 0,
                 "unknown or unsupported INTRINSIC");
    check_source("program invalid\n intrinsic :: sin\n external :: sin\n end program\n", 0,
                 "cannot have EXTERNAL");
    check_source("program invalid\n external :: sin\n intrinsic :: sin\n end program\n", 0,
                 "cannot have EXTERNAL");
    check_source("program invalid\n real, external, intrinsic :: sin\n end program\n", 0,
                 "cannot have EXTERNAL");
    check_source("subroutine invalid(sin)\n real :: sin\n intrinsic :: sin\n end subroutine\n", 0,
                 "cannot have EXTERNAL");
    check_source("program invalid\n intrinsic :: sin\n real :: sin(2)\n end program\n", 0,
                 "cannot have EXTERNAL");
    check_source("program invalid\n intrinsic :: sin\n volatile :: sin\n end program\n", 0,
                 "cannot have EXTERNAL");
    check_source("program invalid\n intrinsic :: sin\n save :: sin\n end program\n", 0,
                 "cannot have EXTERNAL");
    check_source("program invalid\n intrinsic :: sin,\n end program\n", 0, "malformed INTRINSIC");
    check_source("program invalid\n intrinsic\n end program\n", 0, "requires a procedure name");
    check_source("program invalid\n intrinsic :: sin\n intrinsic :: sin\n end program\n", 0,
                 "duplicate INTRINSIC");
    check_source("module owner\n integer :: max\n end module\n"
                 "program invalid\n use owner\n intrinsic :: max\n end program\n",
                 0, "conflicting entity name");
    check_source("program names\n integer :: intrinsic(2)\n intrinsic(1)=3\n end program\n", 1,
                 NULL);
    check_source("program adapter\n intrinsic :: sin\n call consumer(sin)\n end program\n", 0,
                 "unsupported ABI adapter");
    check_source("module zero_product\n"
                 " integer :: values(2147483647,2147483647,2147483647,0) = 3\n"
                 "end module\n",
                 1, "f2c_module_zero_product_values[1U] = {0}");
    check_source("module overflowing_shape\n"
                 " integer(8), parameter :: lo=-9223372036854775807_8-1_8\n"
                 " integer(8), parameter :: hi=9223372036854775807_8\n"
                 " integer :: values(lo:hi) = 0\nend module\n",
                 0, NULL);
    check_bindings("integer(kind=host_kind), dimension(2,3) :: first, second(4)=0", "first,second");
    check_bindings("character*(host_length) first*2, second", "first,second");
    check_bindings("double precision first, second", "first,second");
    check_bindings("intrinsic :: max, conjg", "max,conjg");
    check_bindings("real, intrinsic :: sqrt", "sqrt");
    check_bindings("intrinsic(1) = 3", "");
    check_bindings("type(item), pointer :: object => null()", "object");
    check_bindings("procedure(callback), optional :: first, second", "first,second");
    check_bindings("parameter(first=2, second=first+1)", "first,second");
    check_bindings("common /named/ first(2), second /another/ third", "first,second,third");
    check_bindings("common // first, second", "first,second");
    check_bindings("equivalence(first(2),second), (third,fourth)", "first,second,third,fourth");
    check_bindings("data (first(i), (second(i,j), j=1,2), i=1,2) /8*1/, third /2/",
                   "first,second,third");
    check_bindings("data(2) = 3/2", "");
    check_bindings("volatile :: shared", "");
    check_bindings("asynchronous :: shared", "");
    check_bindings("optional :: shared", "");
    check_bindings("contiguous :: shared", "");
    check_bindings("value :: shared", "");
    check_bindings("equivalence (object%field(1),other(2))", "object,other");
    check_source("module owner\n integer, parameter :: wide=8\n contains\n"
                 " subroutine child(argument)\n integer(wide) :: argument\n"
                 " argument=1\n end subroutine\n end module\n",
                 1, "int64_t *argument");
    check_source("module owner\n integer, parameter :: wide=8\n contains\n"
                 " subroutine child(argument)\n integer(wide) :: argument\n"
                 " integer :: wide\n argument=1\n end subroutine\n end module\n",
                 0, NULL);
    check_source("module owner\n integer :: shared\n end module\n"
                 "program conflict\n use owner\n real :: shared\n end program\n",
                 0, "conflicting");
    check_source("module owner\n integer :: shared\n end module\n"
                 "program conflict\n use owner\n target :: shared\n end program\n",
                 0, "conflicting");
    check_source("module owner\n integer :: shared\n end module\n"
                 "program conflict\n use owner\n data shared /1/\n end program\n",
                 0, "conflicting");
    check_source("module owner\n integer :: shared\n end module\n"
                 "program conflict\n use owner\n integer :: local\n equivalence(shared,local)\n"
                 "end program\n",
                 0, "conflicting");
    check_source("module owner\n type :: item\n integer :: first\n end type\n end module\n"
                 "program conflict\n use owner\n type :: item\n real :: second\n end type\n"
                 "end program\n",
                 0, "conflicting derived types");
    check_source("module owner\n integer :: shared\n end module\n"
                 "program attributes\n use owner\n volatile :: shared\n shared=1\n"
                 "end program\n",
                 1, "(*(volatile int32_t *)&f2c_module_owner_shared)");
    check_source("module owner\n integer :: shared(2)\n end module\n"
                 "program attributes\n use owner\n volatile :: shared\n shared(1)=1\n"
                 "end program\n",
                 1, "((volatile int32_t *)f2c_module_owner_shared)[");
    check_source("module owner\n integer :: shared\n end module\n"
                 "program attributes\n use owner\n asynchronous :: shared\n shared=1\n"
                 "end program\n",
                 1, NULL);
    check_source("module owner\n integer :: shared\n contains\n"
                 "subroutine invalid\n optional :: shared\n end subroutine\n end module\n",
                 0, "OPTIONAL cannot change");
    check_source("module owner\n integer :: shared\n contains\n"
                 "subroutine invalid\n value :: shared\n end subroutine\n end module\n",
                 0, "VALUE cannot change");
    check_source("module owner\n integer, pointer :: shared(:)\n contains\n"
                 "subroutine invalid\n contiguous :: shared\n end subroutine\n end module\n",
                 0, "CONTIGUOUS cannot change");
    check_source("module owner\n integer, pointer :: shared(:)\n end module\n"
                 "program invalid\n use owner\n contiguous :: shared\n end program\n",
                 0, "CONTIGUOUS cannot change");
    check_source("module owner\n integer :: shared\n contains\n"
                 "subroutine child(shared)\n integer :: shared\n optional :: shared\n"
                 "value :: shared\n end subroutine\n end module\n",
                 1, NULL);
    check_source("program invalid\n integer, optional :: local\n end program\n", 0,
                 "OPTIONAL entity 'local' is not a dummy argument");
    check_source("module owner\n integer :: shared\n end module\n"
                 "module wrapper\n use owner\n volatile :: shared\n contains\n"
                 "subroutine child\n use owner, only: shared\n shared=1\n"
                 "end subroutine\n end module\n",
                 1, NULL);
    check_source("module owner\n integer, parameter :: shared=1\n end module\n"
                 "program invalid_attribute\n use owner\n volatile :: shared\n"
                 "end program\n",
                 0, "cannot be ASYNCHRONOUS or VOLATILE");
    check_source("module owner\n integer, volatile :: shared\n end module\n"
                 "program attributes\n use owner\n volatile :: shared\n shared=1\n"
                 "end program\n",
                 1, NULL);
    check_source("module owner\n integer, volatile :: shared\n end module\n"
                 "program duplicate\n use owner\n volatile :: shared\n volatile :: shared\n"
                 "end program\n",
                 0, "duplicate VOLATILE");
    check_source("module owner\n integer :: shared\n end module\n"
                 "module wrapper\n use owner\n volatile :: shared\n end module\n"
                 "program incompatible\n use owner\n use wrapper\n shared=1\n"
                 "end program\n",
                 0, "different ASYNCHRONOUS or VOLATILE");
    check_source("module owner\n integer :: shared\n end module\n"
                 "module wrapper\n use owner\n volatile :: shared\n end module\n"
                 "program compatible\n use owner\n use wrapper\n volatile :: shared\n shared=1\n"
                 "end program\n",
                 1, NULL);
    return failures != 0;
}
