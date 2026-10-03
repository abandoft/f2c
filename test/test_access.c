#include "codegen/array/copy.h"
#include "codegen/array/private.h"
#include "codegen/array/view.h"
#include "codegen/descriptor/private.h"
#include "codegen/expression/private.h"
#include "codegen/lowering/private.h"
#include "internal/f2c.h"
#include "ir/storage.h"

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

static F2cExpr *parse(Unit *unit, const char *source) {
    const char *error = NULL;
    F2cExpr *expression = f2c_parse_expression_ast(unit, source, &error);
    expect(expression != NULL && error == NULL, source);
    return expression;
}

static void expect_view(Unit *unit, const char *source, unsigned int qualifiers) {
    F2cExpr *expression = parse(unit, source);
    F2cArrayView view = {0};
    int supported = 1;
    expect(expression != NULL && f2c_array_view(unit, expression, &view, &supported) && supported,
           "array views describe valid storage");
    expect(view.pointer != NULL && view.count != NULL && view.stride != NULL &&
               view.storage_qualifiers == qualifiers,
           source);
    f2c_array_view_discard(&view);
    expect(view.pointer == NULL && view.storage_qualifiers == F2C_STORAGE_UNQUALIFIED,
           "view disposal clears owned storage and access metadata");
    f2c_expr_free(expression);
}

static void expect_descriptor_view(Unit *unit, const char *source, unsigned int qualifiers,
                                   int readonly_storage) {
    F2cExpr *expression = parse(unit, source);
    F2cDescriptorView view = {0};
    expect(f2c_descriptor_view(unit, expression, &view) && view.data != NULL &&
               view.storage_qualifiers == qualifiers && view.readonly_storage == readonly_storage,
           source);
    f2c_descriptor_view_free(&view);
    expect(view.data == NULL && view.storage_qualifiers == F2C_STORAGE_UNQUALIFIED &&
               !view.readonly_storage,
           "descriptor disposal clears both address ownership and access metadata");
    f2c_expr_free(expression);
}

static void expect_scoped_access(Unit *unit, const char *source) {
    F2cExpr *expression = parse(unit, source);
    int supported = 1;
    char *access = f2c_emit_expression_ast(unit, expression, &supported);
    char *storage = f2c_expression_storage_designator(unit, expression, &supported);
    const F2cStorageReference reference = f2c_ir_storage_reference(expression);
    const int live_state = (reference.state_qualifiers & F2C_STORAGE_VOLATILE) != 0U;
    expect(supported && access != NULL && storage != NULL && strstr(access, "volatile ") != NULL &&
               (live_state ? strstr(storage, "const volatile") != NULL
                           : strstr(storage, "volatile ") == NULL),
           "scoped reads preserve qualification independently of physical argument addresses");
    free(access);
    free(storage);
    f2c_expr_free(expression);
}

static void initialize_symbol(Symbol *symbol, const char *name, Type type, size_t rank) {
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = (char *)name;
    symbol->c_name = (char *)name;
    symbol->type = type;
    symbol->kind = f2c_default_kind(type);
    symbol->rank = rank;
    symbol->value_category = F2C_VALUE_VARIABLE;
    if (rank != 0U) {
        symbol->dimensions[0].kind = F2C_DIMENSION_EXPLICIT;
        symbol->dimensions[0].lower_expression = f2c_expr_new_integer_constant(1);
        symbol->dimensions[0].upper_expression = f2c_expr_new_integer_constant(4);
    }
    f2c_shape_from_symbol(NULL, &symbol->shape, symbol);
}

int main(void) {
    Context context = {0};
    Symbol symbols[7];
    Symbol component;
    F2cDerivedType derived = {0};
    Unit unit = {0};
    F2cExpr *expression;
    size_t index;
    initialize_symbol(&symbols[0], "observed", TYPE_REAL, 1U);
    initialize_symbol(&symbols[1], "pending", TYPE_REAL, 1U);
    initialize_symbol(&symbols[2], "ordinary", TYPE_REAL, 1U);
    initialize_symbol(&symbols[3], "record", TYPE_DERIVED, 0U);
    initialize_symbol(&symbols[4], "text", TYPE_CHARACTER, 0U);
    initialize_symbol(&symbols[5], "callback", TYPE_INTEGER, 0U);
    initialize_symbol(&symbols[6], "position", TYPE_INTEGER, 0U);
    initialize_symbol(&component, "values", TYPE_REAL, 1U);
    symbols[0].volatile_entity = 1;
    symbols[1].asynchronous = 1;
    symbols[3].volatile_entity = 1;
    symbols[3].asynchronous = 1;
    symbols[3].derived_type = &derived;
    symbols[4].volatile_entity = 1;
    symbols[4].character_length = (char *)"8";
    symbols[5].procedure_pointer = 1;
    symbols[5].external = 1;
    symbols[5].volatile_entity = 1;
    symbols[5].value_category = F2C_VALUE_PROCEDURE;
    symbols[6].volatile_entity = 1;
    derived.name = (char *)"record_type";
    derived.c_name = (char *)"record_type";
    derived.components = &component;
    derived.component_count = 1U;
    unit.symbols = symbols;
    unit.context = &context;
    unit.symbol_count = sizeof(symbols) / sizeof(symbols[0]);
    unit.derived_types = &derived;
    unit.derived_type_count = 1U;

    expression = parse(&unit, "observed(4:1:-1)");
    expect(expression != NULL && expression->storage_qualifiers == F2C_STORAGE_VOLATILE,
           "array sections retain the designated object's volatile attribute");
    if (expression != NULL) {
        F2cExpr *clone = f2c_expr_clone_substitute_integers(expression, NULL, 0U);
        expect(clone != NULL && clone->storage_qualifiers == expression->storage_qualifiers,
               "semantic clones preserve storage qualifiers");
        f2c_expr_free(clone);
    }
    f2c_expr_free(expression);

    expression = parse(&unit, "callback");
    expect(expression != NULL && expression->value_category == F2C_VALUE_PROCEDURE &&
               expression->storage_qualifiers == F2C_STORAGE_VOLATILE,
           "procedure pointer designators retain association-state access attributes");
    f2c_expr_free(expression);

    expect_view(&unit, "observed", F2C_STORAGE_VOLATILE);
    expect_view(&unit, "observed(4:1:-1)", F2C_STORAGE_VOLATILE);
    expect_view(&unit, "ordinary", F2C_STORAGE_UNQUALIFIED);
    expect_view(&unit, "pending", F2C_STORAGE_ASYNCHRONOUS);
    expect_view(&unit, "reshape(observed,[2,2])", F2C_STORAGE_VOLATILE);
    expect_view(&unit, "[observed(1),ordinary(2)]", F2C_STORAGE_UNQUALIFIED);
    expect_descriptor_view(&unit, "observed", F2C_STORAGE_VOLATILE, 0);
    expect_descriptor_view(&unit, "observed(4:1:-1)", F2C_STORAGE_VOLATILE, 0);
    expect_descriptor_view(&unit, "pending", F2C_STORAGE_ASYNCHRONOUS, 0);
    expect_descriptor_view(&unit, "record%values(:)",
                           F2C_STORAGE_VOLATILE | F2C_STORAGE_ASYNCHRONOUS, 0);
    expect_scoped_access(&unit, "observed(2)");
    expect_scoped_access(&unit, "record%values(2)");
    expect_scoped_access(&unit, "record");
    symbols[0].argument = 1;
    expect_scoped_access(&unit, "observed(2)");
    symbols[0].argument = 0;
    symbols[0].pointer = 1;
    expect_scoped_access(&unit, "observed(2)");
    symbols[0].pointer = 0;
    expression = parse(&unit, "observed(position)");
    {
        int supported = 1;
        char *storage = f2c_expression_storage_designator(&unit, expression, &supported);
        expect(supported && storage != NULL && strstr(storage, "volatile int32_t") != NULL &&
                   strstr(storage, "volatile float") == NULL,
               "physical address calculation still performs scoped qualified index reads");
        free(storage);
    }
    f2c_expr_free(expression);
    symbols[0].equivalence_unaligned = 1;
    expression = parse(&unit, "observed(2)");
    {
        int supported = 1;
        char *access = f2c_emit_expression_ast(&unit, expression, &supported);
        expect(supported && access != NULL &&
                   strstr(access, "f2c_unaligned_load_r4_volatile(") != NULL &&
                   strstr(access, "(*(volatile") == NULL,
               "qualified unaligned loads access bytes instead of taking a value's address");
        free(access);
    }
    f2c_expr_free(expression);
    symbols[0].equivalence_unaligned = 0;
    {
        Buffer copy = {0};
        f2c_array_copy_snapshot(&copy, &unit, "target", "source", "4U", "float",
                                F2C_STORAGE_VOLATILE, F2C_STORAGE_UNQUALIFIED, 0);
        expect(copy.data != NULL && strstr(copy.data, "((volatile float *)(target))") != NULL &&
                   strstr(copy.data, "((const float *)(source))") != NULL &&
                   strstr(copy.data, "const volatile") == NULL,
               "a qualified destination does not qualify the owned source snapshot");
        free(f2c_buffer_take(&copy));
        f2c_array_copy_snapshot(&copy, &unit, "target", "source", "4U", "float",
                                F2C_STORAGE_UNQUALIFIED, F2C_STORAGE_VOLATILE, 0);
        expect(copy.data != NULL && strstr(copy.data, "((float *)(target))") != NULL &&
                   strstr(copy.data, "((const volatile float *)(source))") != NULL,
               "a qualified source does not qualify the newly owned destination");
        free(f2c_buffer_take(&copy));
    }
    symbols[2].argument = 1;
    symbols[2].intent = F2C_INTENT_IN;
    expect_descriptor_view(&unit, "ordinary", F2C_STORAGE_UNQUALIFIED, 1);
    symbols[2].pointer = 1;
    expect_descriptor_view(&unit, "ordinary", F2C_STORAGE_UNQUALIFIED, 0);
    symbols[2].pointer = 0;
    symbols[2].argument = 0;
    symbols[2].intent = F2C_INTENT_UNSPECIFIED;
    expect(strcmp(f2c_descriptor_address_member(F2C_STORAGE_VOLATILE, 1),
                  "readonly_volatile_data") == 0 &&
               strcmp(f2c_descriptor_address_member(F2C_STORAGE_ASYNCHRONOUS, 1),
                      "readonly_data") == 0,
           "descriptor addresses preserve CV qualification without conflating ASYNCHRONOUS");

    expression = parse(&unit, "observed");
    if (expression != NULL) {
        const char *ordinals[] = {"index"};
        F2cExpr *element;
        expect(f2c_lowering_copy_code(&unit, expression, "snapshot") &&
                   f2c_lowering_set_array_temporary(&unit, expression, 1),
               "an owned array snapshot is available for scalarization");
        element = f2c_array_element_expression(&unit, expression, 1U, ordinals);
        expect(element != NULL && element->storage_qualifiers == F2C_STORAGE_UNQUALIFIED,
               "owned snapshot elements do not inherit volatile source storage");
        f2c_codegen_expression_free(&unit, element);
        expect(f2c_lowering_set_storage_access(&unit, expression, F2C_STORAGE_VOLATILE, 1),
               "a cached alias view has explicit storage metadata");
        element = f2c_array_element_expression(&unit, expression, 1U, ordinals);
        expect(element != NULL && element->storage_qualifiers == F2C_STORAGE_VOLATILE,
               "alias view elements retain access qualification during scalarization");
        {
            int supported = 1;
            char *access = f2c_emit_expression_ast(&unit, element, &supported);
            expect(supported && access != NULL && strstr(access, "volatile float") != NULL,
                   "cached alias elements retain qualified loads after scalarization");
            free(access);
        }
        f2c_codegen_expression_free(&unit, element);
    }
    f2c_codegen_expression_free(&unit, expression);

    expression = parse(&unit, "pending");
    expect(expression != NULL && expression->storage_qualifiers == F2C_STORAGE_ASYNCHRONOUS,
           "asynchronous access is distinct from volatile access");
    f2c_expr_free(expression);

    expression = parse(&unit, "ordinary");
    expect(expression != NULL && expression->storage_qualifiers == F2C_STORAGE_UNQUALIFIED,
           "ordinary storage does not gain volatile access");
    f2c_expr_free(expression);

    expression = parse(&unit, "record%values(:)");
    expect(expression != NULL &&
               expression->storage_qualifiers == (F2C_STORAGE_VOLATILE | F2C_STORAGE_ASYNCHRONOUS),
           "derived subobjects inherit their parent's storage attributes");
    f2c_expr_free(expression);

    expression = parse(&unit, "text(2:4)");
    expect(expression != NULL && expression->storage_qualifiers == F2C_STORAGE_VOLATILE,
           "character substrings inherit the parent attribute");
    f2c_expr_free(expression);

    expression = parse(&unit, "sum(array=observed)");
    expect(expression != NULL && expression->storage_qualifiers == F2C_STORAGE_UNQUALIFIED &&
               expression->child_count == 1U &&
               expression->children[0]->storage_qualifiers == F2C_STORAGE_VOLATILE,
           "keyword argument aliases retain qualifiers while intrinsic results are plain values");
    f2c_expr_free(expression);

    expression = parse(&unit, "observed + ordinary");
    expect(expression != NULL && expression->storage_qualifiers == F2C_STORAGE_UNQUALIFIED &&
               expression->children[0]->storage_qualifiers == F2C_STORAGE_VOLATILE,
           "arithmetic results are not incorrectly treated as volatile storage aliases");
    f2c_expr_free(expression);

    expression = parse(&unit, "[observed(1),ordinary(2)]");
    expect(expression != NULL && expression->storage_qualifiers == F2C_STORAGE_UNQUALIFIED,
           "array constructor values do not inherit storage attributes");
    f2c_expr_free(expression);

    expression = parse(&unit, "observed");
    if (expression != NULL) {
        F2cStatement statement = {0};
        statement.kind = F2C_STMT_ASSIGNMENT;
        statement.right = expression;
        unit.statements = &statement;
        unit.statement_count = 1U;
        symbols[0].volatile_entity = 0;
        symbols[0].asynchronous = 1;
        f2c_analyze_unit_access(&unit);
        expect(expression->storage_qualifiers == F2C_STORAGE_ASYNCHRONOUS,
               "final typed analysis refreshes attributes after binding finalization");
        unit.statements = NULL;
        unit.statement_count = 0U;
    }
    f2c_expr_free(expression);
    for (index = 0U; index < unit.symbol_count; ++index) {
        f2c_expr_free(symbols[index].dimensions[0].lower_expression);
        f2c_expr_free(symbols[index].dimensions[0].upper_expression);
    }
    f2c_expr_free(component.dimensions[0].lower_expression);
    f2c_expr_free(component.dimensions[0].upper_expression);
    f2c_lowering_clear(&context);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
