#include "internal/f2c.h"

#include "core/generated/private.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int extension_equals(const char *extension, const char *expected) {
    while (*extension != '\0' && *expected != '\0') {
        if (tolower((unsigned char)*extension) != tolower((unsigned char)*expected)) {
            return 0;
        }
        ++extension;
        ++expected;
    }
    return *extension == '\0' && *expected == '\0';
}

typedef struct F2cRequiredFeatures {
    int complex_values;
    int mod;
    int transfer;
    int maxloc;
    int maxval;
    int reduction;
    int min;
    int max;
    int io;
    int random;
    int bit_intrinsic;
} F2cRequiredFeatures;

static void collect_expression_feature(F2cExpr *expression, void *state) {
    F2cRequiredFeatures *features = (F2cRequiredFeatures *)state;
    const char *name;
    if (expression == NULL)
        return;
    if (expression->type == TYPE_COMPLEX || expression->type == TYPE_DOUBLE_COMPLEX)
        features->complex_values = 1;
    if (expression->kind != F2C_EXPR_CALL || expression->text == NULL)
        return;
    if (expression->intrinsic != F2C_INTRINSIC_NONE)
        features->bit_intrinsic = 1;
    name = expression->text;
    if (strcmp(name, "mod") == 0)
        features->mod = 1;
    else if (strcmp(name, "transfer") == 0)
        features->transfer = 1;
    else if (strcmp(name, "maxloc") == 0) {
        features->maxloc = 1;
        features->reduction = 1;
    } else if (strcmp(name, "maxval") == 0) {
        features->maxval = 1;
        features->reduction = 1;
    } else if (strcmp(name, "sum") == 0 || strcmp(name, "product") == 0 ||
               strcmp(name, "count") == 0 || strcmp(name, "any") == 0 || strcmp(name, "all") == 0 ||
               strcmp(name, "dot_product") == 0 || strcmp(name, "minval") == 0 ||
               strcmp(name, "minloc") == 0)
        features->reduction = 1;
    else if (strcmp(name, "min") == 0)
        features->min = 1;
    else if (strcmp(name, "max") == 0)
        features->max = 1;
    else if (strcmp(name, "random_number") == 0)
        features->random = 1;
}

static void collect_statement_features(F2cStatement *statement, F2cRequiredFeatures *features) {
    if (statement == NULL)
        return;
    if (statement->kind == F2C_STMT_READ || statement->kind == F2C_STMT_WRITE ||
        statement->kind == F2C_STMT_PRINT || statement->kind == F2C_STMT_OPEN ||
        statement->kind == F2C_STMT_REWIND || statement->kind == F2C_STMT_BACKSPACE ||
        statement->kind == F2C_STMT_ENDFILE || statement->kind == F2C_STMT_INQUIRE ||
        statement->kind == F2C_STMT_CLOSE)
        features->io = 1;
    if (statement->kind == F2C_STMT_CALL && statement->name != NULL &&
        strcmp(statement->name, "random_number") == 0)
        features->random = 1;
    if (statement->intrinsic == F2C_INTRINSIC_MVBITS)
        features->bit_intrinsic = 1;
    f2c_visit_statement_expressions(statement, collect_expression_feature, features);
    collect_statement_features(statement->nested, features);
}

static void collect_unit_features(Unit *unit, F2cRequiredFeatures *features) {
    size_t i;
    size_t dimension;
    if (unit->return_type == TYPE_COMPLEX || unit->return_type == TYPE_DOUBLE_COMPLEX)
        features->complex_values = 1;
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        if (symbol->type == TYPE_COMPLEX || symbol->type == TYPE_DOUBLE_COMPLEX)
            features->complex_values = 1;
        f2c_visit_expression(symbol->initializer_expression, collect_expression_feature, features);
        f2c_visit_expression(symbol->character_length_expression, collect_expression_feature,
                             features);
        f2c_visit_expression(symbol->statement_function_expression, collect_expression_feature,
                             features);
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            f2c_visit_expression(symbol->dimensions[dimension].lower_expression,
                                 collect_expression_feature, features);
            f2c_visit_expression(symbol->dimensions[dimension].upper_expression,
                                 collect_expression_feature, features);
        }
    }
    for (i = 0U; i < unit->derived_type_count; ++i) {
        size_t component;
        for (component = 0U; component < unit->derived_types[i].component_count; ++component) {
            Symbol *symbol = &unit->derived_types[i].components[component];
            if (symbol->type == TYPE_COMPLEX || symbol->type == TYPE_DOUBLE_COMPLEX)
                features->complex_values = 1;
        }
    }
    for (i = 0U; i < unit->statement_count; ++i)
        collect_statement_features(&unit->statements[i], features);
}

static void free_context(Context *context) {
    size_t i;
    for (i = 0U; i < context->lines.count; ++i) {
        free(context->lines.items[i].text);
        free(context->lines.items[i].tokens);
        free(context->lines.items[i].source_map);
    }
    free(context->lines.items);
    for (i = 0U; i < context->units.count; ++i)
        f2c_free_unit(&context->units.items[i]);
    free(context->units.items);
    for (i = 0U; i < context->modules.count; ++i)
        f2c_free_unit(&context->modules.items[i]);
    free(context->modules.items);
    free(context->procedures.items);
    for (i = 0U; i < context->source_name_count; ++i)
        free(context->source_names[i]);
    free(context->source_names);
    free(context->output.data);
    free(context->header.data);
    free(context->diagnostics.data);
}

F2cResult f2c_transpile_project_config(const F2cInput *inputs, size_t input_count,
                                       const F2cConfig *config) {
    Context context;
    F2cOptions defaults = {"<input>", F2C_SOURCE_AUTO, 0};
    size_t i;
    memset(&context, 0, sizeof(context));
    context.options = &defaults;
    if (f2c_context_source_name(&context, defaults.source_name) == NULL)
        f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1,
                            "out of memory initializing source names");
    if (!f2c_initialize_context_limits(&context, config))
        f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_INVALID_ARGUMENT, 1U, 1,
                            "configuration structure size does not match this f2c build");
    if (inputs == NULL || input_count == 0U) {
        f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_INVALID_ARGUMENT, 1U, 1,
                            "no project inputs were provided");
    }
    for (i = 0U; inputs != NULL && i < input_count && context.result.error_count == 0U; ++i) {
        const char *source = inputs[i].source != NULL ? inputs[i].source : "";
        const F2cOptions *options = &inputs[i].options;
        F2cSourceForm form = options->source_form;
        context.options = options;
        if (inputs[i].source == NULL && inputs[i].length != 0U) {
            f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_INVALID_ARGUMENT, 1U, 1,
                                "project input has a null source buffer with a nonzero length");
            break;
        }
        if (inputs[i].source != NULL && memchr(inputs[i].source, '\0', inputs[i].length) != NULL) {
            f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_INVALID_ARGUMENT, 1U, 1,
                                "project input contains an embedded NUL byte");
            break;
        }
        if (form != F2C_SOURCE_AUTO && form != F2C_SOURCE_FREE && form != F2C_SOURCE_FIXED) {
            f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_INVALID_ARGUMENT, 1U, 1,
                                "project input has an invalid source-form value");
            break;
        }
        if (inputs[i].length > context.limits.max_input_bytes - context.input_bytes) {
            f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, 1U, 1,
                                "project input limit of %zu bytes exceeded",
                                context.limits.max_input_bytes);
            break;
        }
        context.input_bytes += inputs[i].length;
        if (form == F2C_SOURCE_AUTO) {
            const char *name = options->source_name;
            const char *extension = name != NULL ? strrchr(name, '.') : NULL;
            form = extension != NULL && (extension_equals(extension, ".f") ||
                                         extension_equals(extension, ".for") ||
                                         extension_equals(extension, ".ftn"))
                       ? F2C_SOURCE_FIXED
                       : F2C_SOURCE_FREE;
        }
        if (!f2c_normalize_source(&context, source, inputs[i].length, form)) {
            if (context.result.error_count == 0U)
                f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1,
                                    "out of memory while normalizing project input");
            break;
        }
    }
    if (context.result.error_count == 0U && f2c_build_syntax_program(&context))
        (void)f2c_build_typed_program(&context);
    if (context.result.error_count == 0U) {
        if (context.phase != F2C_COMPILATION_TYPED_IR) {
            f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_INTERNAL, 1U, 1,
                                "internal compiler error: emitter requires typed IR");
        } else {
            for (i = 0U; i < context.modules.count; ++i) {
                if (context.modules.items[i].phase != F2C_UNIT_TYPED_IR) {
                    f2c_diagnostic_code(
                        &context, F2C_DIAGNOSTIC_INTERNAL, 1U, 1,
                        "internal compiler error: module emitter requires typed IR");
                    break;
                }
            }
            for (i = 0U; i < context.units.count && context.result.error_count == 0U; ++i) {
                if (context.units.items[i].phase != F2C_UNIT_TYPED_IR) {
                    f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_INTERNAL, 1U, 1,
                                        "internal compiler error: unit emitter requires typed IR");
                    break;
                }
            }
        }
    }
    if (context.result.error_count == 0U) {
        F2cRequiredFeatures features = {0};
        size_t u;
        for (u = 0U; u < context.modules.count; ++u)
            collect_unit_features(&context.modules.items[u], &features);
        for (u = 0U; u < context.units.count; ++u)
            collect_unit_features(&context.units.items[u], &features);
        if (f2c_supported_module_needs_complex(&context))
            features.complex_values = 1;
        const int needs_complex = features.complex_values;
        const int needs_mod = features.mod;
        const int needs_transfer = features.transfer;
        const int needs_maxloc = features.maxloc;
        const int needs_maxval = features.maxval;
        const int needs_reduction = features.reduction;
        const int needs_min = features.min;
        const int needs_max = features.max;
        const int needs_io = features.io;
        const int needs_random = features.random;
        const int needs_bit_intrinsic = features.bit_intrinsic;
        f2c_buffer_append(
            &context.output,
            "/* Generated by f2c. Portable C17; no libf2c dependency. */\n"
            "#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201710L\n"
            "#error \"f2c-generated code requires ISO C17 or newer\"\n"
            "#endif\n"
            "#include <stdbool.h>\n#include <stddef.h>\n#include "
            "<stdint.h>\n#include <stdio.h>\n#include <limits.h>\n#include <stdarg.h>\n#include "
            "<ctype.h>\n#include "
            "<stdlib.h>\n#include <string.h>\n#include <float.h>\n"
            "#include <math.h>\n");
        if (needs_complex) {
            f2c_buffer_append(&context.output, "#include <complex.h>\n");
        }
        f2c_buffer_append(&context.output, "#if defined(__clang__)\n"
                                           "#if __has_warning(\"-Wpass-failed\")\n"
                                           "#pragma clang diagnostic ignored \"-Wpass-failed\"\n"
                                           "#endif\n"
                                           "#if defined(F2C_FP_CONTRACT)\n"
                                           "#pragma STDC FP_CONTRACT ON\n"
                                           "#else\n"
                                           "#pragma STDC FP_CONTRACT OFF\n"
                                           "#endif\n"
                                           "#endif\n");
        f2c_buffer_append(&context.output,
                          "\n#if defined(_MSC_VER)\n#define F2C_RESTRICT __restrict\n"
                          "#define F2C_NOINLINE __declspec(noinline)\n"
                          "#define F2C_UNUSED\n#elif defined(__GNUC__) || defined(__clang__)\n"
                          "#define F2C_RESTRICT restrict\n"
                          "#define F2C_NOINLINE __attribute__((noinline))\n"
                          "#define F2C_UNUSED __attribute__((unused))\n#else\n"
                          "#define F2C_RESTRICT restrict\n#define F2C_NOINLINE\n"
                          "#define F2C_UNUSED\n#endif\n");
        if (needs_bit_intrinsic)
            f2c_emit_bit_intrinsic_support(&context.output);
        f2c_buffer_append(&context.output,
                          "#if !defined(F2C_LOOP_UNROLL)\n"
                          "#if defined(__clang__) && defined(__OPTIMIZE__)\n"
                          "#define F2C_LOOP_UNROLL _Pragma(\"clang loop unroll_count(4)\")\n"
                          "#elif defined(__GNUC__) && defined(__OPTIMIZE__)\n"
                          "#define F2C_LOOP_UNROLL _Pragma(\"GCC unroll 4\")\n"
                          "#else\n"
                          "#define F2C_LOOP_UNROLL\n"
                          "#endif\n"
                          "#endif\n");
        f2c_buffer_append(
            &context.output,
            "/* Preserve the legacy Fortran ABI when a read-only actual is passed through an "
            "implicit external interface. */\n"
            "static inline F2C_UNUSED void *f2c_implicit_mutable_actual(const void *pointer) { "
            "void *mutable_pointer = NULL; memcpy(&mutable_pointer, &pointer, "
            "sizeof(mutable_pointer)); return mutable_pointer; }\n");
        if (needs_complex) {
            f2c_buffer_append(&context.output,
                              "#ifndef F2C_GENERATED_COMPLEX_TYPES\n"
                              "#define F2C_GENERATED_COMPLEX_TYPES\n"
                              "#if defined(_MSC_VER) && !defined(__clang__)\n"
                              "typedef _Fcomplex f2c_complex_float;\n"
                              "typedef _Dcomplex f2c_complex_double;\n"
                              "typedef _Lcomplex f2c_complex_long_double;\n"
                              "#define F2C_COMPLEX_FLOAT_INITIALIZER(real_part, imag_part) "
                              "{(real_part), (imag_part)}\n"
                              "#define F2C_COMPLEX_DOUBLE_INITIALIZER(real_part, imag_part) "
                              "{(real_part), (imag_part)}\n"
                              "#else\n"
                              "typedef float complex f2c_complex_float;\n"
                              "typedef double complex f2c_complex_double;\n"
                              "typedef long double complex f2c_complex_long_double;\n"
                              "#define F2C_COMPLEX_FLOAT_INITIALIZER(real_part, imag_part) "
                              "((real_part) + (imag_part) * I)\n"
                              "#define F2C_COMPLEX_DOUBLE_INITIALIZER(real_part, imag_part) "
                              "((real_part) + (imag_part) * I)\n"
                              "#endif\n"
                              "#endif\n");
        }
        f2c_buffer_append(&context.output, "typedef struct f2c_descriptor {\n"
                                           "    void *data;\n"
                                           "    size_t element_size;\n"
                                           "    size_t rank;\n"
                                           "    int64_t lower[15];\n"
                                           "    int64_t extent[15];\n"
                                           "    ptrdiff_t stride[15];\n"
                                           "    size_t character_length;\n"
                                           "} f2c_descriptor;\n");
        f2c_buffer_append(&context.output,
                          "static inline F2C_UNUSED size_t f2c_array_offset(int64_t subscript, "
                          "int64_t lower, size_t extent) { uint64_t offset; if (subscript < lower) "
                          "abort(); offset = (uint64_t)(subscript - lower); if (offset >= "
                          "(uint64_t)extent) abort(); return (size_t)offset; }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED ptrdiff_t f2c_descriptor_stride_multiply(ptrdiff_t a, "
            "ptrdiff_t b) { if (a > 0 ? (b > 0 ? a > PTRDIFF_MAX / b : b < PTRDIFF_MIN / a) "
            ": a < 0 ? (b > 0 ? a < PTRDIFF_MIN / b : b < 0 && a < PTRDIFF_MAX / b) : "
            "false) abort(); return a * b; }\n"
            "static inline F2C_UNUSED ptrdiff_t f2c_descriptor_stride_extent(ptrdiff_t stride, "
            "size_t extent) { if (extent > (size_t)PTRDIFF_MAX) abort(); return "
            "f2c_descriptor_stride_multiply(stride, (ptrdiff_t)extent); }\n"
            "static inline F2C_UNUSED ptrdiff_t f2c_descriptor_stride_step(ptrdiff_t stride, "
            "int64_t step) { if (step == 0 || step < (int64_t)PTRDIFF_MIN || step > "
            "(int64_t)PTRDIFF_MAX) abort(); return f2c_descriptor_stride_multiply(stride, "
            "(ptrdiff_t)step); }\n"
            "static inline F2C_UNUSED ptrdiff_t f2c_descriptor_offset_add(ptrdiff_t left, "
            "ptrdiff_t right) { if ((right > 0 && left > PTRDIFF_MAX - right) || (right < 0 && "
            "left < PTRDIFF_MIN - right)) abort(); return left + right; }\n"
            "static inline F2C_UNUSED ptrdiff_t f2c_array_descriptor_offset(size_t rank, const "
            "int64_t *subscripts, const int64_t *lowers, const size_t *extents, const ptrdiff_t "
            "*strides) { size_t dimension; ptrdiff_t result = 0; for (dimension = 0U; dimension "
            "< rank; ++dimension) { size_t ordinal = f2c_array_offset(subscripts[dimension], "
            "lowers[dimension], extents[dimension]); if (ordinal > (size_t)PTRDIFF_MAX) abort(); "
            "result = f2c_descriptor_offset_add(result, f2c_descriptor_stride_multiply("
            "(ptrdiff_t)ordinal, strides[dimension])); } return result; }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED bool f2c_size_multiply(size_t left, size_t right, "
            "size_t *result) { if (right != 0U && left > SIZE_MAX / right) return false; "
            "*result = left * right; return true; }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED size_t f2c_inquiry_extent(int64_t dimension, size_t "
            "rank, const size_t *extents) { if (dimension < 1 || (uint64_t)dimension > "
            "(uint64_t)rank) abort(); return extents[(size_t)dimension - 1U]; }\n"
            "static inline F2C_UNUSED size_t f2c_inquiry_size(size_t rank, const size_t "
            "*extents) { size_t result = 1U; size_t dimension; for (dimension = 0U; "
            "dimension < rank; ++dimension) if (!f2c_size_multiply(result, "
            "extents[dimension], &result)) abort(); return result; }\n"
            "static inline F2C_UNUSED int64_t f2c_inquiry_lower_bound(int64_t lower, size_t "
            "extent) { return extent == 0U ? INT64_C(1) : lower; }\n"
            "static inline F2C_UNUSED int64_t f2c_inquiry_lower(int64_t dimension, size_t "
            "rank, const int64_t *lowers, const size_t *extents) { size_t index; if "
            "(dimension < 1 || (uint64_t)dimension > (uint64_t)rank) abort(); index = "
            "(size_t)dimension - 1U; return f2c_inquiry_lower_bound(lowers[index], "
            "extents[index]); }\n"
            "static inline F2C_UNUSED int64_t f2c_inquiry_upper(int64_t lower, size_t extent) "
            "{ uint64_t delta; if (extent == 0U) { if (lower == INT64_MIN) abort(); return "
            "lower - INT64_C(1); } delta = (uint64_t)extent - UINT64_C(1); if (delta > "
            "(uint64_t)INT64_MAX || lower > INT64_MAX - (int64_t)delta) abort(); return lower "
            "+ (int64_t)delta; }\n"
            "static inline F2C_UNUSED int64_t f2c_inquiry_upper_dimension(int64_t dimension, "
            "size_t rank, const int64_t *lowers, const size_t *extents) { size_t index; if "
            "(dimension < 1 || (uint64_t)dimension > (uint64_t)rank) abort(); index = "
            "(size_t)dimension - 1U; return f2c_inquiry_upper("
            "f2c_inquiry_lower_bound(lowers[index], extents[index]), extents[index]); }\n"
            "static inline F2C_UNUSED int64_t f2c_inquiry_size_integer(size_t value, int "
            "kind) { uint64_t maximum = kind == 1 ? UINT64_C(127) : kind == 2 ? "
            "UINT64_C(32767) : kind == 4 ? UINT64_C(2147483647) : kind == 8 ? "
            "(uint64_t)INT64_MAX : UINT64_C(0); if (maximum == 0U || (uint64_t)value > "
            "maximum) abort(); return (int64_t)value; }\n"
            "static inline F2C_UNUSED int64_t f2c_inquiry_bound_integer(int64_t value, int "
            "kind) { int64_t minimum = kind == 1 ? INT64_C(-128) : kind == 2 ? "
            "INT64_C(-32768) : kind == 4 ? INT64_C(-2147483647) - INT64_C(1) : kind == 8 ? "
            "INT64_MIN : INT64_C(1); int64_t maximum = kind == 1 ? INT64_C(127) : kind == 2 "
            "? INT64_C(32767) : kind == 4 ? INT64_C(2147483647) : kind == 8 ? INT64_MAX : "
            "INT64_C(0); if (minimum > maximum || value < minimum || value > maximum) "
            "abort(); return value; }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED int64_t f2c_descriptor_extent(size_t extent) { if "
            "((uint64_t)extent > (uint64_t)INT64_MAX) abort(); return (int64_t)extent; }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED int f2c_character_compare(const char *left, size_t "
            "left_length, const char *right, size_t right_length) { size_t i; size_t length = "
            "left_length > right_length ? left_length : right_length; for (i = 0U; i < length; "
            "++i) { unsigned char l = i < left_length ? (unsigned char)left[i] : (unsigned "
            "char)' '; unsigned char r = i < right_length ? (unsigned char)right[i] : (unsigned "
            "char)' '; if (l < r) return -1; if (l > r) return 1; } return 0; }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED size_t f2c_character_trim_length(const char *value, "
            "size_t length) { while (length != 0U && value[length - 1U] == ' ') --length; "
            "return length; }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED char *f2c_character_temporary_resize(char *storage, "
            "size_t length) { char *replacement; if (length == SIZE_MAX) abort(); replacement = "
            "(char *)realloc(storage, length + 1U); if (replacement == NULL) abort(); return "
            "replacement; }\n"
            "static inline F2C_UNUSED char *f2c_character_concatenation_resize(char *storage, "
            "size_t left_length, size_t right_length) { if (left_length > SIZE_MAX - "
            "right_length) abort(); return f2c_character_temporary_resize(storage, left_length + "
            "right_length); }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED size_t f2c_substring_offset(size_t length, int64_t lower, "
            "int64_t upper) { if (lower < 1 || (upper > 0 && (uint64_t)upper > "
            "(uint64_t)length) || (lower > upper && lower - 1 != upper)) abort(); return "
            "(size_t)(lower - 1); }\n"
            "static inline F2C_UNUSED size_t f2c_substring_length(size_t length, int64_t lower, "
            "int64_t upper) { (void)f2c_substring_offset(length, lower, upper); return upper >= "
            "lower ? (size_t)((uint64_t)upper - (uint64_t)lower + UINT64_C(1)) : 0U; }\n");
        f2c_buffer_append(
            &context.output,
            "static inline F2C_UNUSED float f2c_square_f(float value) { return value * value; "
            "}\n"
            "static inline F2C_UNUSED double f2c_square_d(double value) { return value * value; "
            "}\n");
        if (needs_complex && needs_transfer) {
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED f2c_complex_float f2c_transfer_c(const void *p) { "
                "f2c_complex_float r; "
                "memcpy(&r, p, sizeof(r)); return r; }\n"
                "static inline F2C_UNUSED f2c_complex_double f2c_transfer_z(const void *p) { "
                "f2c_complex_double r; "
                "memcpy(&r, p, sizeof(r)); return r; }\n"
                "static inline F2C_UNUSED int32_t f2c_transfer_i32(const void *p) { int32_t r; "
                "memcpy(&r, p, sizeof(r)); return r; }\n"
                "#define F2C_TRANSFER(source, mold) _Generic((mold), f2c_complex_float: "
                "f2c_transfer_c, f2c_complex_double: f2c_transfer_z, int32_t: "
                "f2c_transfer_i32)(&(source))\n");
        }
        f2c_buffer_append(
            &context.output,
            needs_complex
                ? "#define F2C_ABS(x) _Generic((x), int32_t: abs, float: fabsf, double: fabs, "
                  "long double: fabsl, f2c_complex_float: cabsf, f2c_complex_double: cabs, "
                  "default: fabs)((x))\n"
                : "#define F2C_ABS(x) _Generic((x), int32_t: abs, float: fabsf, double: fabs, "
                  "long double: fabsl, default: fabs)((x))\n");
        f2c_buffer_append(&context.output, "#define F2C_MAX(a, b) ((a) > (b) ? (a) : (b))\n"
                                           "#define F2C_MIN(a, b) ((a) < (b) ? (a) : (b))\n");
        if (needs_complex) {
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED f2c_complex_float f2c_make_c(float real_part, float "
                "imag_part) {\n#if defined(_MSC_VER) && !defined(__clang__)\nreturn "
                "_FCbuild(real_part, imag_part);\n#else\nfloat parts[2] = {real_part, imag_part}; "
                "f2c_complex_float value; memcpy(&value, parts, sizeof(value)); return value;\n"
                "#endif\n}\n"
                "static inline F2C_UNUSED f2c_complex_double f2c_make_z(double real_part, "
                "double imag_part) {\n#if defined(_MSC_VER) && !defined(__clang__)\nreturn "
                "_Cbuild(real_part, imag_part);\n#else\ndouble parts[2] = {real_part, imag_part}; "
                "f2c_complex_double value; memcpy(&value, parts, sizeof(value)); return value;\n"
                "#endif\n}\n"
                "static inline F2C_UNUSED f2c_complex_double "
                "f2c_c_to_z(f2c_complex_float value) { return f2c_make_z((double)crealf(value), "
                "(double)cimagf(value)); }\n"
                "static inline F2C_UNUSED f2c_complex_float "
                "f2c_z_to_c(f2c_complex_double value) { return f2c_make_c((float)creal(value), "
                "(float)cimag(value)); }\n"
                "static inline F2C_UNUSED f2c_complex_float f2c_cadd(f2c_complex_float a, "
                "f2c_complex_float b) { return f2c_make_c(crealf(a) + crealf(b), cimagf(a) + "
                "cimagf(b)); }\n"
                "static inline F2C_UNUSED f2c_complex_double f2c_zadd(f2c_complex_double a, "
                "f2c_complex_double b) { return f2c_make_z(creal(a) + creal(b), cimag(a) + "
                "cimag(b)); }\n"
                "static inline F2C_UNUSED f2c_complex_float f2c_csub(f2c_complex_float a, "
                "f2c_complex_float b) { return f2c_make_c(crealf(a) - crealf(b), cimagf(a) - "
                "cimagf(b)); }\n"
                "static inline F2C_UNUSED f2c_complex_double f2c_zsub(f2c_complex_double a, "
                "f2c_complex_double b) { return f2c_make_z(creal(a) - creal(b), cimag(a) - "
                "cimag(b)); }\n"
                "static inline F2C_UNUSED f2c_complex_float f2c_cneg(f2c_complex_float a) { "
                "return f2c_make_c(-crealf(a), -cimagf(a)); }\n"
                "static inline F2C_UNUSED f2c_complex_double f2c_zneg(f2c_complex_double a) { "
                "return f2c_make_z(-creal(a), -cimag(a)); }\n"
                "static inline F2C_UNUSED f2c_complex_float f2c_cmul(f2c_complex_float a, "
                "f2c_complex_float b) { float ar = crealf(a), ai = cimagf(a), br = crealf(b), "
                "bi = cimagf(b); return f2c_make_c(ar * br - ai * bi, ar * bi + ai * br); }\n"
                "static inline F2C_UNUSED f2c_complex_double f2c_zmul(f2c_complex_double a, "
                "f2c_complex_double b) { double ar = creal(a), ai = cimag(a), br = creal(b), "
                "bi = cimag(b); return f2c_make_z(ar * br - ai * bi, ar * bi + ai * br); }\n"
                "static inline F2C_UNUSED bool f2c_ceq(f2c_complex_float a, f2c_complex_float "
                "b) { return crealf(a) == crealf(b) && cimagf(a) == cimagf(b); }\n"
                "static inline F2C_UNUSED bool f2c_zeq(f2c_complex_double a, f2c_complex_double "
                "b) { return creal(a) == creal(b) && cimag(a) == cimag(b); }\n"
                "static inline F2C_UNUSED f2c_complex_float f2c_square_c(f2c_complex_float "
                "value) { return f2c_cmul(value, value); }\n"
                "static inline F2C_UNUSED f2c_complex_double f2c_square_z(f2c_complex_double "
                "value) { return f2c_zmul(value, value); }\n");
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED f2c_complex_float f2c_cdiv(f2c_complex_float a, "
                "f2c_complex_float b) { float ar = crealf(a), ai = cimagf(a), br = crealf(b), "
                "bi = cimagf(b), scale = fmaxf(fabsf(br), fabsf(bi)); if (isnan(br) || "
                "isnan(bi)) return f2c_make_c(NAN, NAN); if (isfinite(scale) && scale > 0.0f) { "
                "float ars = ar / scale, ais = ai / scale, brs = br / scale, bis = bi / scale, "
                "denominator = brs * brs + bis * bis, real_part = (ars * brs + ais * bis) / "
                "denominator, imag_part = (ais * brs - ars * bis) / denominator; if "
                "(isnan(real_part) && isnan(imag_part) && (isinf(ar) || isinf(ai))) { ar = "
                "copysignf(isinf(ar) ? 1.0f : 0.0f, ar); ai = copysignf(isinf(ai) ? 1.0f : "
                "0.0f, ai); real_part = INFINITY * (ar * brs + ai * bis); imag_part = INFINITY "
                "* (ai * brs - ar * bis); } return f2c_make_c(real_part, imag_part); } if (scale "
                "== 0.0f && (!isnan(ar) || !isnan(ai))) { float infinity = copysignf(INFINITY, "
                "br); return f2c_make_c(infinity * ar, infinity * ai); } if (isinf(scale) && "
                "isfinite(ar) && isfinite(ai)) { br = copysignf(isinf(br) ? 1.0f : 0.0f, br); "
                "bi = copysignf(isinf(bi) ? 1.0f : 0.0f, bi); return f2c_make_c(0.0f * (ar * br "
                "+ ai * bi), 0.0f * (ai * br - ar * bi)); } return f2c_make_c(NAN, NAN); }\n"
                "static inline F2C_UNUSED f2c_complex_double f2c_zdiv(f2c_complex_double a, "
                "f2c_complex_double b) { double ar = creal(a), ai = cimag(a), br = creal(b), "
                "bi = cimag(b), scale = fmax(fabs(br), fabs(bi)); if (isnan(br) || isnan(bi)) "
                "return f2c_make_z(NAN, NAN); if (isfinite(scale) && scale > 0.0) { double ars "
                "= ar / scale, ais = ai / scale, brs = br / scale, bis = bi / scale, denominator "
                "= brs * brs + bis * bis, real_part = (ars * brs + ais * bis) / denominator, "
                "imag_part = (ais * brs - ars * bis) / denominator; if (isnan(real_part) && "
                "isnan(imag_part) && (isinf(ar) || isinf(ai))) { ar = copysign(isinf(ar) ? 1.0 "
                ": 0.0, ar); ai = copysign(isinf(ai) ? 1.0 : 0.0, ai); real_part = INFINITY * "
                "(ar * brs + ai * bis); imag_part = INFINITY * (ai * brs - ar * bis); } return "
                "f2c_make_z(real_part, imag_part); } if (scale == 0.0 && (!isnan(ar) || "
                "!isnan(ai))) { double infinity = copysign(INFINITY, br); return "
                "f2c_make_z(infinity * ar, infinity * ai); } if (isinf(scale) && isfinite(ar) && "
                "isfinite(ai)) { br = copysign(isinf(br) ? 1.0 : 0.0, br); bi = "
                "copysign(isinf(bi) ? 1.0 : 0.0, bi); return f2c_make_z(0.0 * (ar * br + ai * "
                "bi), 0.0 * (ai * br - ar * bi)); } return f2c_make_z(NAN, NAN); }\n");
        }
        if (needs_max) {
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED float f2c_fortran_smax(float a, float b) { return "
                "isnan(a) || isnan(b) ? a + b : (a > b ? a : b); }\n"
                "static inline F2C_UNUSED double f2c_fortran_dmax(double a, double b) { return "
                "isnan(a) || isnan(b) ? a + b : (a > b ? a : b); }\n"
                "static inline F2C_UNUSED int32_t f2c_fortran_imax(int32_t a, int32_t b) { "
                "return a > b ? a : b; }\n"
                "#define F2C_FORTRAN_MAX(a, b) _Generic(((a) + (b)), float: "
                "f2c_fortran_smax, double: f2c_fortran_dmax, default: "
                "f2c_fortran_imax)((a), (b))\n");
        }
        if (needs_min) {
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED float f2c_fortran_smin(float a, float b) { return "
                "isnan(a) || isnan(b) ? a + b : (a < b ? a : b); }\n"
                "static inline F2C_UNUSED double f2c_fortran_dmin(double a, double b) { return "
                "isnan(a) || isnan(b) ? a + b : (a < b ? a : b); }\n"
                "static inline F2C_UNUSED int32_t f2c_fortran_imin(int32_t a, int32_t b) { "
                "return a < b ? a : b; }\n"
                "#define F2C_FORTRAN_MIN(a, b) _Generic(((a) + (b)), float: "
                "f2c_fortran_smin, double: f2c_fortran_dmin, default: "
                "f2c_fortran_imin)((a), (b))\n");
        }
        if (needs_mod) {
            f2c_buffer_append(
                &context.output,
                "static inline int32_t f2c_mod_i32(int32_t a, int32_t b) { return a % b; }\n"
                "#define F2C_MOD(a, b) _Generic((a), int32_t: f2c_mod_i32, "
                "float: fmodf, double: fmod, long double: fmodl, default: fmod)((a), (b))\n");
        }
        if (needs_maxloc) {
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED int32_t f2c_smaxloc(const float *v, int32_t n) { int32_t "
                "i, p = "
                "n > 0 ? 1 : 0; for (i = 1; i < n; ++i) if (v[i] > v[p - 1]) p = i + 1; "
                "return p; }\n"
                "static inline F2C_UNUSED int32_t f2c_dmaxloc(const double *v, int32_t n) { "
                "int32_t i, p = "
                "n > 0 ? 1 : 0; for (i = 1; i < n; ++i) if (v[i] > v[p - 1]) p = i + 1; "
                "return p; }\n"
                "#define F2C_MAXLOC(v, n) _Generic(*(v), float: f2c_smaxloc, double: "
                "f2c_dmaxloc)((v), (n))\n");
        }
        if (needs_maxval) {
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED float f2c_smaxval(const float *v, int32_t n) { int32_t "
                "i; float "
                "r = n > 0 ? v[0] : 0.0f; for (i = 1; i < n; ++i) if (v[i] > r) r = v[i]; "
                "return r; }\n"
                "static inline F2C_UNUSED double f2c_dmaxval(const double *v, int32_t n) { int32_t "
                "i; "
                "double r = n > 0 ? v[0] : 0.0; for (i = 1; i < n; ++i) if (v[i] > r) r = "
                "v[i]; return r; }\n"
                "#define F2C_MAXVAL(v, n) _Generic(*(v), float: f2c_smaxval, double: "
                "f2c_dmaxval)((v), (n))\n");
        }
        if (needs_reduction) {
            f2c_buffer_append(
                &context.output,
                "#define F2C_DEFINE_REDUCTIONS(s, t, zero, one, low, high) "
                "static inline F2C_UNUSED t f2c_sum_##s(const t *v, size_t n, ptrdiff_t d) { "
                "size_t i; t r = (zero); for (i = 0U; i < n; ++i) r = (t)(r + v[(ptrdiff_t)i "
                "* d]); return r; } "
                "static inline F2C_UNUSED t f2c_product_##s(const t *v, size_t n, ptrdiff_t d) "
                "{ size_t i; t r = (one); for (i = 0U; i < n; ++i) r = (t)(r * "
                "v[(ptrdiff_t)i * d]); return r; } "
                "static inline F2C_UNUSED t f2c_maxval_##s(const t *v, size_t n, ptrdiff_t d) { "
                "size_t i; t r = (low); for (i = 0U; i < n; ++i) if (v[(ptrdiff_t)i * d] > r) "
                "r = v[(ptrdiff_t)i * d]; return r; } "
                "static inline F2C_UNUSED t f2c_minval_##s(const t *v, size_t n, ptrdiff_t d) { "
                "size_t i; t r = (high); for (i = 0U; i < n; ++i) if (v[(ptrdiff_t)i * d] < r) "
                "r = v[(ptrdiff_t)i * d]; return r; } "
                "static inline F2C_UNUSED int32_t f2c_maxloc_##s(const t *v, size_t n, "
                "ptrdiff_t d) { size_t i; int32_t p = n != 0U ? 1 : 0; for (i = 1U; i < n; "
                "++i) if (v[(ptrdiff_t)i * d] > v[(ptrdiff_t)(p - 1) * d]) p = (int32_t)i + "
                "1; return p; } "
                "static inline F2C_UNUSED int32_t f2c_minloc_##s(const t *v, size_t n, "
                "ptrdiff_t d) { size_t i; int32_t p = n != 0U ? 1 : 0; for (i = 1U; i < n; "
                "++i) if (v[(ptrdiff_t)i * d] < v[(ptrdiff_t)(p - 1) * d]) p = (int32_t)i + "
                "1; return p; } "
                "static inline F2C_UNUSED t f2c_dot_##s(const t *a, ptrdiff_t ad, const t *b, "
                "ptrdiff_t bd, size_t n) { size_t i; t r = (zero); for (i = 0U; i < n; ++i) r "
                "= (t)(r + a[(ptrdiff_t)i * ad] * b[(ptrdiff_t)i * bd]); return r; }\n"
                "F2C_DEFINE_REDUCTIONS(i8, int8_t, INT8_C(0), INT8_C(1), INT8_MIN, INT8_MAX)\n"
                "F2C_DEFINE_REDUCTIONS(i16, int16_t, INT16_C(0), INT16_C(1), INT16_MIN, "
                "INT16_MAX)\n"
                "F2C_DEFINE_REDUCTIONS(i32, int32_t, INT32_C(0), INT32_C(1), INT32_MIN, "
                "INT32_MAX)\n"
                "F2C_DEFINE_REDUCTIONS(i64, int64_t, INT64_C(0), INT64_C(1), INT64_MIN, "
                "INT64_MAX)\n"
                "F2C_DEFINE_REDUCTIONS(f, float, 0.0f, 1.0f, -HUGE_VALF, HUGE_VALF)\n"
                "F2C_DEFINE_REDUCTIONS(d, double, 0.0, 1.0, -HUGE_VAL, HUGE_VAL)\n"
                "#undef F2C_DEFINE_REDUCTIONS\n");
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED int32_t f2c_count_l(const int32_t *v, size_t n, "
                "ptrdiff_t d) { size_t i; int32_t r = 0; for (i = 0U; i < n; ++i) if "
                "(v[(ptrdiff_t)i * d]) ++r; return r; }\n"
                "static inline F2C_UNUSED bool f2c_any_l(const int32_t *v, size_t n, ptrdiff_t d) "
                "{ size_t i; for (i = 0U; i < n; ++i) if (v[(ptrdiff_t)i * d]) return true; "
                "return false; }\n"
                "static inline F2C_UNUSED bool f2c_all_l(const int32_t *v, size_t n, ptrdiff_t d) "
                "{ size_t i; for (i = 0U; i < n; ++i) if (!v[(ptrdiff_t)i * d]) return false; "
                "return true; }\n"
                "static inline F2C_UNUSED bool f2c_dot_l(const int32_t *a, ptrdiff_t ad, const "
                "int32_t *b, ptrdiff_t bd, size_t n) { size_t i; for (i = 0U; i < n; ++i) if "
                "(a[(ptrdiff_t)i * ad] && b[(ptrdiff_t)i * bd]) return true; return false; }\n"
                "#define F2C_SUM(v, n, d) _Generic(*(v), int8_t: f2c_sum_i8, int16_t: "
                "f2c_sum_i16, int32_t: f2c_sum_i32, int64_t: f2c_sum_i64, float: f2c_sum_f, "
                "double: f2c_sum_d)((v), (n), (d))\n"
                "#define F2C_PRODUCT(v, n, d) _Generic(*(v), int8_t: f2c_product_i8, int16_t: "
                "f2c_product_i16, int32_t: f2c_product_i32, int64_t: f2c_product_i64, float: "
                "f2c_product_f, double: f2c_product_d)((v), (n), (d))\n"
                "#define F2C_MAXIMUM(v, n, d) _Generic(*(v), int8_t: f2c_maxval_i8, int16_t: "
                "f2c_maxval_i16, int32_t: f2c_maxval_i32, int64_t: f2c_maxval_i64, float: "
                "f2c_maxval_f, double: f2c_maxval_d)((v), (n), (d))\n"
                "#define F2C_MINIMUM(v, n, d) _Generic(*(v), int8_t: f2c_minval_i8, int16_t: "
                "f2c_minval_i16, int32_t: f2c_minval_i32, int64_t: f2c_minval_i64, float: "
                "f2c_minval_f, double: f2c_minval_d)((v), (n), (d))\n"
                "#define F2C_MAXIMUM_LOCATION(v, n, d) _Generic(*(v), int8_t: f2c_maxloc_i8, "
                "int16_t: f2c_maxloc_i16, int32_t: f2c_maxloc_i32, int64_t: f2c_maxloc_i64, "
                "float: f2c_maxloc_f, double: f2c_maxloc_d)((v), (n), (d))\n"
                "#define F2C_MINIMUM_LOCATION(v, n, d) _Generic(*(v), int8_t: f2c_minloc_i8, "
                "int16_t: f2c_minloc_i16, int32_t: f2c_minloc_i32, int64_t: f2c_minloc_i64, "
                "float: f2c_minloc_f, double: f2c_minloc_d)((v), (n), (d))\n"
                "#define F2C_DOT(a, ad, b, bd, n) _Generic(*(a), int8_t: f2c_dot_i8, int16_t: "
                "f2c_dot_i16, int32_t: f2c_dot_i32, int64_t: f2c_dot_i64, float: f2c_dot_f, "
                "double: f2c_dot_d)((a), (ad), (b), (bd), (n))\n"
                "#define F2C_LOGICAL_DOT(a, ad, b, bd, n) f2c_dot_l((a), (ad), (b), (bd), "
                "(n))\n");
            f2c_emit_relation_reduction_support(&context.output, needs_complex);
        }
        if (needs_random) {
            f2c_buffer_append(
                &context.output,
                "static uint64_t f2c_random_state = UINT64_C(0xD1B54A32D192ED03);\n"
                "static inline F2C_UNUSED uint64_t f2c_random_bits(void) { uint64_t x = "
                "f2c_random_state; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; "
                "f2c_random_state = x; return x * UINT64_C(2685821657736338717); }\n"
                "static inline F2C_UNUSED void f2c_random_float(float *v) { *v = "
                "(float)(f2c_random_bits() >> 40) * 0x1p-24f; }\n"
                "static inline F2C_UNUSED void f2c_random_double(double *v) { *v = "
                "(double)(f2c_random_bits() >> 11) * 0x1p-53; }\n"
                "#define F2C_RANDOM_NUMBER(v) _Generic((v), float *: f2c_random_float, "
                "double *: f2c_random_double)((v))\n");
        }
        if (needs_io) {
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED bool f2c_advance_enabled(const char *value, size_t "
                "length) { size_t begin = 0U; while (begin < length && value[begin] == ' ') "
                "++begin; while (length > begin && value[length - 1U] == ' ') --length; if "
                "(length - begin == 3U && tolower((unsigned char)value[begin]) == 'y' && "
                "tolower((unsigned char)value[begin + 1U]) == 'e' && "
                "tolower((unsigned char)value[begin + 2U]) == 's') return true; if (length - "
                "begin == 2U && tolower((unsigned char)value[begin]) == 'n' && "
                "tolower((unsigned char)value[begin + 1U]) == 'o') return false; abort(); }\n"
                "static inline F2C_UNUSED void f2c_set_iomsg(char *value, size_t length, int "
                "status) { const char *message = status == EOF ? \"end of file\" : status == "
                "-2 ? \"end of record\" : status == 2 ? \"invalid unit\" : status == 3 ? "
                "\"unit action mismatch\" : status == 4 ? \"formatted/unformatted connection "
                "mismatch\" : status == 5 ? \"sequential/direct access mismatch\" : status == "
                "6 ? \"record transfer failed\" : status == 7 ? \"corrupt unformatted record\" "
                ": status == 8 ? \"record offset overflow\" : status <= 0 ? \"I/O error\" : "
                "\"\"; size_t n = "
                "strlen(message); if (n > length) n = length; if (n != 0U) memmove(value, "
                "message, n); if (length > n) memset(value + n, ' ', length - n); }\n");
            f2c_emit_io_stream_support(&context.output);
            f2c_emit_file_unit_support(&context.output);
            f2c_emit_record_io_support(&context.output);
            f2c_emit_list_io_support(&context.output, needs_complex);
            f2c_emit_namelist_support(&context);
            f2c_emit_format_support(&context);
        }
        f2c_buffer_append(&context.output, "\n");
        f2c_emit_common_blocks(&context);
        f2c_emit_derived_types(&context);
        f2c_emit_project_modules(&context);
        f2c_emit_prototypes(&context);
        f2c_emit_interface_header(&context);
        f2c_emit_supported_modules(&context);
        if (context.units.count != 0U)
            f2c_buffer_append(&context.output, "\n");
        for (i = 0U; i < context.units.count; ++i) {
            context.options = &context.units.items[i].options;
            f2c_emit_unit(&context, &context.units.items[i]);
        }
        context.phase = F2C_COMPILATION_EMITTED;
    }
    if (context.output.limit_exceeded || context.header.limit_exceeded)
        f2c_diagnostic_code(&context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, 1U, 1,
                            "generated output limit of %zu bytes exceeded",
                            context.limits.max_output_bytes);
    context.result.code = f2c_buffer_take(&context.output);
    context.result.header = f2c_buffer_take(&context.header);
    context.result.diagnostics = f2c_buffer_take(&context.diagnostics);
    if (context.result.diagnostics == NULL ||
        (context.result.error_count == 0U &&
         (context.result.code == NULL || context.result.header == NULL))) {
        free(context.result.code);
        free(context.result.header);
        free(context.result.diagnostics);
        context.result.code = NULL;
        context.result.header = NULL;
        context.result.diagnostics = f2c_strdup("f2c: fatal: out of memory\n");
        ++context.result.error_count;
    } else if (context.result.error_count != 0U) {
        free(context.result.code);
        free(context.result.header);
        context.result.code = NULL;
        context.result.header = NULL;
    }
    {
        F2cResult result = context.result;
        context.result.code = NULL;
        context.result.header = NULL;
        context.result.diagnostics = NULL;
        free_context(&context);
        return result;
    }
}

F2cResult f2c_transpile_project(const F2cInput *inputs, size_t input_count) {
    return f2c_transpile_project_config(inputs, input_count, NULL);
}

F2cResult f2c_transpile(const char *source, size_t length, const F2cOptions *options) {
    F2cInput input;
    input.source = source;
    input.length = source != NULL ? length : 0U;
    input.options = options != NULL ? *options : (F2cOptions){"<input>", F2C_SOURCE_AUTO, 0};
    return f2c_transpile_project(&input, 1U);
}

void f2c_result_free(F2cResult *result) {
    if (result != NULL) {
        free(result->code);
        free(result->header);
        free(result->diagnostics);
        memset(result, 0, sizeof(*result));
    }
}

const char *f2c_version(void) { return F2C_VERSION_STRING; }
