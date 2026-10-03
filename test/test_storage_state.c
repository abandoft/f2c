#include "codegen/storage/private.h"
#include "ir/expression.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void expect_code(char *code, const char *expected, const char *message) {
    expect(code != NULL && strstr(code, expected) != NULL, message);
    free(code);
}

static void test_live_descriptor_properties(void) {
    Symbol symbol = {0};
    Unit unit = {0};
    symbol.name = (char *)"values";
    symbol.c_name = (char *)"values";
    symbol.type = TYPE_INTEGER;
    symbol.kind = 4;
    symbol.rank = 2U;
    symbol.pointer = 1;
    symbol.argument = 1;
    symbol.volatile_entity = 1;
    symbol.asynchronous = 1;
    symbol.intent = F2C_INTENT_INOUT;
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(&symbol);
    expect(reference.state_source == F2C_OBJECT_STATE_DESCRIPTOR &&
               reference.object_qualifiers == (F2C_STORAGE_VOLATILE | F2C_STORAGE_ASYNCHRONOUS) &&
               reference.state_qualifiers == reference.object_qualifiers &&
               !reference.readonly_state && !reference.readonly_storage,
           "a dynamic dummy denotes one live record with independent access attributes");
    expect_code(f2c_storage_read_property(&unit, &reference, F2C_OBJECT_DATA, 0U),
                "f2c_descriptor_state_data(f2c_descriptor_values, 3U)",
                "data addresses are read from live qualified state");
    expect_code(f2c_storage_read_property(&unit, &reference, F2C_OBJECT_EXTENT, 1U),
                "f2c_descriptor_state_extent(f2c_descriptor_values, 1U, 3U)",
                "shape reads are not entry snapshots");
    expect_code(f2c_storage_write_property(&unit, &reference, F2C_OBJECT_DATA, 0U),
                "void *volatile", "state writes qualify the pointer object, not its pointee");
    expect_code(f2c_storage_write_property(&unit, &reference, F2C_OBJECT_LOWER, 0U),
                "volatile *)&(f2c_descriptor_values->lower[0])",
                "bounds writes preserve descriptor-state qualification");
    expect(f2c_storage_write_property(&unit, &reference, F2C_OBJECT_EXTENT, 2U) == NULL &&
               f2c_storage_read_property(&unit, &reference, F2C_OBJECT_STRIDE, 2U) == NULL,
           "out-of-rank property accesses are rejected");
    symbol.argument = 0;
    const F2cStorageReference local = f2c_ir_symbol_storage_reference(&symbol);
    expect(local.state_source == F2C_OBJECT_STATE_LOCAL,
           "local object state remains distinct from dummy descriptor state");
    expect_code(f2c_storage_read_property(&unit, &local, F2C_OBJECT_DATA, 0U),
                "int32_t *const volatile", "local reads qualify association-state objects");
    expect_code(f2c_storage_write_property(&unit, &local, F2C_OBJECT_DATA, 0U), "int32_t *volatile",
                "local state writes use mutable pointer-object lvalues");
    Buffer commit = {0};
    expect(f2c_storage_emit_descriptor_commit(&commit, &unit, &local, NULL, "&bridge",
                                              F2C_STORAGE_STATEMENT, 1),
           "owned call bridges commit through the same typed property provider");
    expect(commit.data != NULL && strstr(commit.data, "f2c_descriptor_bridge_valid") != NULL &&
               strstr(commit.data, "values_extent_2") != NULL &&
               strstr(commit.data, "values_stride_2") != NULL &&
               strstr(commit.data, "int32_t *volatile") != NULL,
           "bridge validation precedes qualified data and shape commits");
    free(f2c_buffer_take(&commit));
}

static void test_readonly_state_and_components(void) {
    Symbol symbol = {0};
    Unit unit = {0};
    symbol.name = (char *)"value";
    symbol.c_name = (char *)"value";
    symbol.type = TYPE_INTEGER;
    symbol.kind = 4;
    symbol.pointer = 1;
    symbol.argument = 1;
    symbol.intent = F2C_INTENT_IN;
    F2cStorageReference reference = f2c_ir_symbol_storage_reference(&symbol);
    expect(reference.readonly_state && !reference.readonly_storage,
           "INTENT(IN) pointer association is immutable but its target can be defined");
    expect(f2c_storage_write_property(&unit, &reference, F2C_OBJECT_DATA, 0U) == NULL,
           "immutable association-state writes are rejected");
    symbol.pointer = 0;
    symbol.allocatable = 1;
    reference = f2c_ir_symbol_storage_reference(&symbol);
    expect(reference.readonly_state && reference.readonly_storage,
           "INTENT(IN) allocatables prohibit both state and data mutation");
    expect_code(f2c_storage_read_property(&unit, &reference, F2C_OBJECT_DATA, 0U),
                "const int32_t *", "readonly allocatable addresses preserve const access");
    Buffer output = {0};
    expect(f2c_storage_emit_descriptor_commit(&output, &unit, &reference, NULL, "&bridge",
                                              F2C_STORAGE_COMMA_EXPRESSION, 0) &&
               output.length == 0U,
           "readonly bridges never perform exit-time metadata writes");
    free(f2c_buffer_take(&output));

    Symbol owner = {0};
    Symbol component = {0};
    owner.type = TYPE_DERIVED;
    owner.argument = 1;
    owner.intent = F2C_INTENT_IN;
    component.pointer = 1;
    component.type = TYPE_INTEGER;
    component.kind = 4;
    F2cExpr base = {0};
    F2cExpr member = {0};
    F2cExpr *children[] = {&base};
    base.kind = F2C_EXPR_NAME;
    base.symbol = &owner;
    member.kind = F2C_EXPR_COMPONENT;
    member.symbol = &component;
    member.children = children;
    member.child_count = 1U;
    reference = f2c_ir_storage_reference(&member);
    expect(reference.owner == &base && reference.state_source == F2C_OBJECT_STATE_COMPONENT &&
               reference.state_qualifiers == F2C_STORAGE_UNQUALIFIED && reference.readonly_state &&
               !reference.readonly_storage,
           "readonly derived objects protect pointer-component association, not target values");
    owner.pointer = 1;
    reference = f2c_ir_storage_reference(&member);
    expect(!reference.readonly_state && !reference.readonly_storage,
           "INTENT(IN) pointer owners still permit defining their target's components");
    F2cExpr value = {0};
    F2cExpr *value_children[] = {&member};
    value.kind = F2C_EXPR_PARENTHESIZED;
    value.children = value_children;
    value.child_count = 1U;
    reference = f2c_ir_storage_reference(&value);
    expect(reference.symbol == NULL && reference.state_source == F2C_OBJECT_STATE_NONE,
           "parenthesized values do not retain definable storage identity");
    expect(f2c_ir_storage_reference(NULL).symbol == NULL &&
               f2c_storage_read_property(&unit, NULL, F2C_OBJECT_DATA, 0U) == NULL,
           "missing storage references fail without generating guessed code");
}

static void test_intent_definition_contexts(void) {
    static const struct {
        const char *source;
        const char *diagnostic;
    } invalid[] = {
        {"subroutine invalid(p)\ninteger, pointer, intent(in) :: p\np => null()\nend\n",
         "pointer-assignment object state is not definable"},
        {"subroutine invalid(p)\ninteger, pointer, intent(in) :: p\nnullify(p)\nend\n",
         "NULLIFY object state is not definable"},
        {"subroutine invalid(p)\ninteger, pointer, intent(in) :: p\nallocate(p)\nend\n",
         "ALLOCATE object state is not definable"},
        {"subroutine invalid(p)\ninteger, pointer, intent(in) :: p\ndeallocate(p)\nend\n",
         "DEALLOCATE object state is not definable"},
        {"subroutine invalid(a,b)\ninteger, allocatable, intent(in) :: a\n"
         "integer, allocatable, intent(inout) :: b\ncall move_alloc(a,b)\nend\n",
         "MOVE_ALLOC FROM= object state is not definable"},
        {"module m\ncontains\nsubroutine invalid(p)\ninteger, pointer, intent(in) :: p\n"
         "call mutate(p)\nend\nsubroutine mutate(p)\ninteger, pointer, intent(inout) :: p\n"
         "nullify(p)\nend\nend module\n",
         "not definable"},
        {"module m\ntype box\ninteger, pointer :: p\nend type\ncontains\n"
         "subroutine invalid(x)\ntype(box), intent(in) :: x\nnullify(x%p)\nend\nend module\n",
         "NULLIFY object state is not definable"},
        {"subroutine invalid(p)\ninterface\nsubroutine callback()\nend\nend interface\n"
         "procedure(callback), pointer, intent(in) :: p\nnullify(p)\nend\n",
         "NULLIFY object state is not definable"}};
    const F2cOptions options = {"object-state-intent.f90", F2C_SOURCE_FREE, 0};
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        F2cResult result =
            f2c_transpile(invalid[index].source, strlen(invalid[index].source), &options);
        expect(result.code == NULL && result.error_count != 0U && result.diagnostics != NULL &&
                   strstr(result.diagnostics, invalid[index].diagnostic) != NULL,
               invalid[index].diagnostic);
        f2c_result_free(&result);
    }
    static const char valid[] = "module m\ntype box\ninteger, pointer :: p\nend type\ncontains\n"
                                "subroutine assign_target(p,x)\ninteger, pointer, intent(in) :: p\n"
                                "type(box), intent(in) :: x\np = 19\nx%p = 23\nend\nend module\n";
    F2cResult result = f2c_transpile(valid, sizeof(valid) - 1U, &options);
    expect(result.error_count == 0U && result.code != NULL,
           "pointer target definitions remain legal in readonly association contexts");
    f2c_result_free(&result);
}

int main(void) {
    test_live_descriptor_properties();
    test_readonly_state_and_components();
    test_intent_definition_contexts();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
