#include "internal/f2c.h"

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
        statement->kind == F2C_STMT_REWIND || statement->kind == F2C_STMT_CLOSE)
        features->io = 1;
    if (statement->kind == F2C_STMT_CALL && statement->name != NULL &&
        strcmp(statement->name, "random_number") == 0)
        features->random = 1;
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

void f2c_free_unit(Unit *unit) {
    size_t j;
    free(unit->name);
    free(unit->fortran_name);
    free(unit->result_name);
    free(unit->result_character_length);
    free(unit->interface_generic_name);
    free((char *)unit->options.source_name);
    for (j = 0U; j < unit->argument_count; ++j)
        free(unit->arguments[j]);
    free(unit->arguments);
    for (j = 0U; j < 26U; ++j)
        free(unit->implicit_character_lengths[j]);
    for (j = 0U; j < unit->statement_count; ++j)
        f2c_statement_free(&unit->statements[j]);
    free(unit->statements);
    for (j = 0U; j < unit->namelist_count; ++j) {
        size_t member;
        free(unit->namelists[j].name);
        for (member = 0U; member < unit->namelists[j].member_count; ++member)
            free(unit->namelists[j].members[member]);
        free(unit->namelists[j].members);
    }
    free(unit->namelists);
    for (j = 0U; j < unit->symbol_count; ++j) {
        size_t d;
        free(unit->symbols[j].name);
        free(unit->symbols[j].c_name);
        free(unit->symbols[j].initializer);
        f2c_expr_free(unit->symbols[j].initializer_expression);
        free(unit->symbols[j].character_length);
        f2c_expr_free(unit->symbols[j].character_length_expression);
        free(unit->symbols[j].procedure_interface_name);
        free(unit->symbols[j].alias_to);
        free(unit->symbols[j].common_block);
        free(unit->symbols[j].derived_type_name);
        free(unit->symbols[j].c_type);
        for (d = 0U; d < unit->symbols[j].rank; ++d) {
            free(unit->symbols[j].dimensions[d].lower);
            free(unit->symbols[j].dimensions[d].upper);
            f2c_expr_free(unit->symbols[j].dimensions[d].lower_expression);
            f2c_expr_free(unit->symbols[j].dimensions[d].upper_expression);
        }
    }
    free(unit->symbols);
    for (j = 0U; j < unit->derived_type_count; ++j) {
        size_t component;
        size_t finalizer;
        free(unit->derived_types[j].name);
        free(unit->derived_types[j].c_name);
        free(unit->derived_types[j].parent_name);
        for (component = 0U; component < unit->derived_types[j].component_count; ++component) {
            Symbol *symbol = &unit->derived_types[j].components[component];
            size_t dimension;
            free(symbol->name);
            free(symbol->c_name);
            free(symbol->initializer);
            f2c_expr_free(symbol->initializer_expression);
            free(symbol->character_length);
            f2c_expr_free(symbol->character_length_expression);
            free(symbol->procedure_interface_name);
            free(symbol->alias_to);
            free(symbol->common_block);
            free(symbol->derived_type_name);
            free(symbol->c_type);
            for (dimension = 0U; dimension < symbol->rank; ++dimension) {
                free(symbol->dimensions[dimension].lower);
                free(symbol->dimensions[dimension].upper);
                f2c_expr_free(symbol->dimensions[dimension].lower_expression);
                f2c_expr_free(symbol->dimensions[dimension].upper_expression);
            }
        }
        free(unit->derived_types[j].components);
        for (finalizer = 0U; finalizer < unit->derived_types[j].finalizer_count; ++finalizer)
            free(unit->derived_types[j].finalizers[finalizer]);
        free(unit->derived_types[j].finalizers);
        free(unit->derived_types[j].finalizer_procedures);
        free(unit->derived_types[j].finalizer_ranks);
        for (component = 0U; component < unit->derived_types[j].binding_count; ++component) {
            F2cTypeBinding *binding = &unit->derived_types[j].bindings[component];
            Symbol *symbol = &binding->procedure;
            free(binding->name);
            free(binding->target_name);
            free(binding->interface_name);
            free(binding->pass_name);
            free(symbol->name);
            free(symbol->c_name);
            free(symbol->character_length);
            free(symbol->procedure_interface_name);
        }
        free(unit->derived_types[j].bindings);
        for (component = 0U; component < F2C_DEFINED_IO_COUNT; ++component)
            free(unit->derived_types[j].defined_io_bindings[component]);
    }
    free(unit->derived_types);
    free(unit->imported_derived_types);
    for (j = 0U; j < unit->interface_count; ++j)
        f2c_free_unit(&unit->interfaces[j]);
    free(unit->interfaces);
}

static void free_context(Context *context) {
    size_t i;
    for (i = 0U; i < context->lines.count; ++i) {
        free(context->lines.items[i].text);
        free(context->lines.items[i].source_name);
        free(context->lines.items[i].tokens);
    }
    free(context->lines.items);
    for (i = 0U; i < context->units.count; ++i)
        f2c_free_unit(&context->units.items[i]);
    free(context->units.items);
    for (i = 0U; i < context->modules.count; ++i)
        f2c_free_unit(&context->modules.items[i]);
    free(context->modules.items);
    free(context->procedures.items);
    free(context->output.data);
    free(context->header.data);
    free(context->diagnostics.data);
}

F2cResult f2c_transpile_project(const F2cInput *inputs, size_t input_count) {
    Context context;
    F2cOptions defaults = {"<input>", F2C_SOURCE_AUTO, 0};
    size_t i;
    memset(&context, 0, sizeof(context));
    context.options = &defaults;
    if (inputs == NULL || input_count == 0U) {
        f2c_diagnostic(&context, 1U, 1, "no project inputs were provided");
    }
    for (i = 0U; i < input_count; ++i) {
        const char *source = inputs[i].source != NULL ? inputs[i].source : "";
        const F2cOptions *options = &inputs[i].options;
        F2cSourceForm form = options->source_form;
        context.options = options;
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
            f2c_diagnostic(&context, 1U, 1, "out of memory while normalizing project input");
            break;
        }
    }
    if (context.result.error_count == 0U &&
        (!f2c_rewrite_labeled_do(&context) || !f2c_tokenize_lines(&context) ||
         !f2c_discover_modules(&context) || !f2c_discover_units(&context))) {
        f2c_diagnostic(&context, 1U, 1, "out of memory");
    }
    for (i = 0U; i < context.modules.count; ++i) {
        context.options = &context.modules.items[i].options;
        f2c_analyze_module(&context, &context.modules.items[i]);
    }
    for (i = 0U; i < context.units.count; ++i) {
        context.options = &context.units.items[i].options;
        f2c_analyze_unit(&context, &context.units.items[i]);
    }
    if (context.result.error_count == 0U && !f2c_build_procedure_registry(&context))
        f2c_diagnostic(&context, 1U, 1, "out of memory while building procedure registry");
    if (context.result.error_count == 0U)
        f2c_resolve_derived_semantics(&context);
    if (context.result.error_count == 0U) {
        for (i = 0U; i < context.units.count; ++i) {
            context.options = &context.units.items[i].options;
            f2c_validate_implicit_external(&context, &context.units.items[i]);
        }
    }
    if (context.result.error_count == 0U) {
        for (i = 0U; i < context.units.count; ++i) {
            context.options = &context.units.items[i].options;
            f2c_build_statement_ir(&context, &context.units.items[i]);
        }
    }
    if (context.result.error_count == 0U) {
        F2cRequiredFeatures features = {0};
        size_t u;
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
        f2c_buffer_append(&context.output,
                          "/* Generated by f2c. Portable C17; no libf2c dependency. */\n"
                          "#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201710L\n"
                          "#error \"f2c-generated code requires ISO C17 or newer\"\n"
                          "#endif\n"
                          "#include <stdbool.h>\n#include <stddef.h>\n#include "
                          "<stdint.h>\n#include <stdio.h>\n#include "
                          "<ctype.h>\n#include "
                          "<stdlib.h>\n#include <string.h>\n#include <float.h>\n"
                          "#include <math.h>\n");
        if (needs_complex) {
            f2c_buffer_append(&context.output, "#include <complex.h>\n");
        }
        f2c_buffer_append(&context.output, "#if defined(__clang__)\n"
                                           "#if defined(F2C_FP_CONTRACT)\n"
                                           "#pragma STDC FP_CONTRACT ON\n"
                                           "#else\n"
                                           "#pragma STDC FP_CONTRACT OFF\n"
                                           "#endif\n"
                                           "#endif\n");
        f2c_buffer_append(&context.output,
                          "\n#if defined(_MSC_VER)\n#define F2C_RESTRICT __restrict\n"
                          "#define F2C_UNUSED\n#elif defined(__GNUC__) || defined(__clang__)\n"
                          "#define F2C_RESTRICT restrict\n"
                          "#define F2C_UNUSED __attribute__((unused))\n#else\n"
                          "#define F2C_RESTRICT restrict\n#define F2C_UNUSED\n#endif\n");
        f2c_buffer_append(&context.output,
                          "#if !defined(F2C_LOOP_UNROLL)\n"
                          "#if defined(__clang__)\n"
                          "#define F2C_LOOP_UNROLL _Pragma(\"clang loop unroll_count(4)\")\n"
                          "#elif defined(__GNUC__)\n"
                          "#define F2C_LOOP_UNROLL _Pragma(\"GCC unroll 4\")\n"
                          "#else\n"
                          "#define F2C_LOOP_UNROLL\n"
                          "#endif\n"
                          "#endif\n");
        f2c_buffer_append(&context.output, "typedef struct f2c_descriptor {\n"
                                           "    void *data;\n"
                                           "    size_t element_size;\n"
                                           "    size_t rank;\n"
                                           "    int64_t lower[15];\n"
                                           "    int64_t extent[15];\n"
                                           "    size_t character_length;\n"
                                           "} f2c_descriptor;\n");
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
                "static inline F2C_UNUSED float complex f2c_transfer_c(const void *p) { float "
                "complex r; "
                "memcpy(&r, p, sizeof(r)); return r; }\n"
                "static inline F2C_UNUSED double complex f2c_transfer_z(const void *p) { double "
                "complex r; "
                "memcpy(&r, p, sizeof(r)); return r; }\n"
                "static inline F2C_UNUSED int32_t f2c_transfer_i32(const void *p) { int32_t r; "
                "memcpy(&r, p, sizeof(r)); return r; }\n"
                "#define F2C_TRANSFER(source, mold) _Generic((mold), float complex: "
                "f2c_transfer_c, double complex: f2c_transfer_z, int32_t: "
                "f2c_transfer_i32)(&(source))\n");
        }
        f2c_buffer_append(
            &context.output,
            needs_complex
                ? "#define F2C_ABS(x) _Generic((x), int32_t: abs, float: fabsf, double: fabs, "
                  "long double: fabsl, float complex: cabsf, double complex: cabs, default: "
                  "fabs)((x))\n"
                : "#define F2C_ABS(x) _Generic((x), int32_t: abs, float: fabsf, double: fabs, "
                  "long double: fabsl, default: fabs)((x))\n");
        f2c_buffer_append(&context.output, "#define F2C_MAX(a, b) ((a) > (b) ? (a) : (b))\n"
                                           "#define F2C_MIN(a, b) ((a) < (b) ? (a) : (b))\n");
        if (needs_complex) {
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED float complex f2c_square_c(float complex value) { "
                "return value * value; }\n"
                "static inline F2C_UNUSED double complex f2c_square_z(double complex value) { "
                "return value * value; }\n"
                "static inline F2C_UNUSED float complex f2c_make_c(float real_part, float "
                "imag_part) { float parts[2] = {real_part, imag_part}; float complex value; "
                "memcpy(&value, parts, sizeof(value)); return value; }\n"
                "static inline F2C_UNUSED double complex f2c_make_z(double real_part, double "
                "imag_part) { double parts[2] = {real_part, imag_part}; double complex value; "
                "memcpy(&value, parts, sizeof(value)); return value; }\n"
                "static inline F2C_UNUSED float complex f2c_cdiv(float complex a, float "
                "complex b) { float br = crealf(b), bi = cimagf(b), scale = fmaxf(fabsf(br), "
                "fabsf(bi)); if (isfinite(scale) && scale > 0.0f) { float ars = crealf(a) / "
                "scale, ais = cimagf(a) / scale, brs = br / scale, bis = bi / scale, den = "
                "brs * brs + bis * bis; return f2c_make_c((ars * brs + ais * bis) / den, "
                "(ais * brs - ars * bis) / den); } return a / b; }\n"
                "static inline F2C_UNUSED double complex f2c_zdiv(double complex a, double "
                "complex b) { double br = creal(b), bi = cimag(b), scale = fmax(fabs(br), "
                "fabs(bi)); if (isfinite(scale) && scale > 0.0) { double ars = creal(a) / "
                "scale, ais = cimag(a) / scale, brs = br / scale, bis = bi / scale, den = brs "
                "* brs + bis * bis; return f2c_make_z((ars * brs + ais * bis) / den, (ais * "
                "brs - ars * bis) / den); } return a / b; }\n");
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
        }
        if (needs_random) {
            f2c_buffer_append(
                &context.output,
                "static uint64_t f2c_random_state = UINT64_C(0xD1B54A32D192ED03);\n"
                "static inline F2C_UNUSED uint64_t f2c_random_bits(void) { uint64_t x = "
                "f2c_random_state; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; "
                "f2c_random_state = x; return x * UINT64_C(2685821657736338717); }\n"
                "static inline F2C_UNUSED void f2c_random_float(float *v) { *v = "
                "(float)((f2c_random_bits() >> 40) * 0x1p-24); }\n"
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
                "-2 ? \"end of record\" : status <= 0 ? \"I/O error\" : \"\"; size_t n = "
                "strlen(message); if (n > length) n = length; if (n != 0U) memmove(value, "
                "message, n); if (length > n) memset(value + n, ' ', length - n); }\n");
            f2c_buffer_append(
                &context.output,
                "typedef struct f2c_unit_entry { int32_t unit; FILE *file; struct "
                "f2c_unit_entry *next; } f2c_unit_entry;\n"
                "static _Thread_local f2c_unit_entry *f2c_unit_list;\n"
                "static _Thread_local int32_t f2c_internal_unit_next = -1;\n"
                "static _Thread_local unsigned f2c_child_io_depth;\n"
                "static inline F2C_UNUSED f2c_unit_entry *f2c_find_unit(int32_t unit) { "
                "f2c_unit_entry *entry; for (entry = f2c_unit_list; entry != NULL; entry = "
                "entry->next) if (entry->unit == unit) return entry; return NULL; }\n"
                "static inline F2C_UNUSED FILE *f2c_unit_file(int32_t unit, bool input) { "
                "f2c_unit_entry *entry = f2c_find_unit(unit); if (entry != NULL) return "
                "entry->file; if (input) return stdin; return unit == 0 ? stderr : stdout; }\n"
                "static inline F2C_UNUSED char *f2c_trimmed_copy(const char *value, size_t "
                "length) { char *copy; while (length != 0U && value[length - 1U] == ' ') "
                "--length; copy = (char *)malloc(length + 1U); if (copy == NULL) return NULL; "
                "memcpy(copy, value, length); copy[length] = '\\0'; return copy; }\n"
                "static inline F2C_UNUSED bool f2c_string_equal_ci(const char *left, const "
                "char *right) { while (*left != '\\0' && *right != '\\0') { if "
                "(tolower((unsigned char)*left) != tolower((unsigned char)*right)) return "
                "false; ++left; ++right; } return *left == '\\0' && *right == '\\0'; }\n"
                "static inline F2C_UNUSED bool f2c_close_unit(int32_t unit) { "
                "f2c_unit_entry **link = &f2c_unit_list; while (*link != NULL && "
                "(*link)->unit != unit) link = &(*link)->next; if (*link == NULL) return true; "
                "{ f2c_unit_entry *entry = *link; bool ok = fclose(entry->file) == 0; *link = "
                "entry->next; free(entry); return ok; } }\n"
                "static inline F2C_UNUSED bool f2c_open_unit(int32_t unit, const char *path, "
                "size_t path_length, const char *status, size_t status_length, const char *form, "
                "size_t form_length) { char *name = "
                "f2c_trimmed_copy(path, path_length); char *state = "
                "f2c_trimmed_copy(status, status_length); char *representation = "
                "f2c_trimmed_copy(form, form_length); FILE *file = NULL; f2c_unit_entry "
                "*entry; if (name == NULL || state == NULL || representation == NULL) { "
                "free(name); free(state); free(representation); return false; } "
                "(void)f2c_close_unit(unit); if (f2c_string_equal_ci(state, "
                "\"scratch\")) file = tmpfile(); else file = fopen(name, "
                "f2c_string_equal_ci(state, \"old\") ? "
                "(f2c_string_equal_ci(representation, \"unformatted\") ? \"rb+\" : \"r+\") : "
                "(f2c_string_equal_ci(representation, \"unformatted\") ? \"wb+\" : \"w+\")); "
                "free(name); free(state); free(representation); if (file == NULL) return false; "
                "entry = (f2c_unit_entry "
                "*)malloc(sizeof(*entry)); if (entry == NULL) { fclose(file); return false; } "
                "entry->unit = unit; entry->file = file; entry->next = f2c_unit_list; "
                "f2c_unit_list = entry; return true; }\n"
                "static inline F2C_UNUSED int32_t f2c_register_internal_unit(FILE *file) { "
                "f2c_unit_entry *entry = (f2c_unit_entry *)malloc(sizeof(*entry)); int32_t unit; "
                "if (entry == NULL) abort(); do { unit = f2c_internal_unit_next; "
                "f2c_internal_unit_next = unit == INT32_MIN ? -1 : unit - 1; } while "
                "(f2c_find_unit(unit) != NULL); entry->unit = unit; entry->file = file; "
                "entry->next = f2c_unit_list; f2c_unit_list = entry; return unit; }\n"
                "static inline F2C_UNUSED void f2c_unregister_internal_unit(int32_t unit) { "
                "f2c_unit_entry **link = &f2c_unit_list; while (*link != NULL && "
                "(*link)->unit != unit) link = &(*link)->next; if (*link != NULL) { "
                "f2c_unit_entry *entry = *link; *link = entry->next; free(entry); } }\n"
                "static inline F2C_UNUSED bool f2c_rewind_unit(int32_t unit) { FILE *file = "
                "f2c_unit_file(unit, false); if (file == NULL) return false; clearerr(file); "
                "return fseek(file, 0L, SEEK_SET) == 0; }\n");
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED void f2c_write_i8(FILE *f, int8_t v) { fprintf(f, "
                "\" %d\", (int)v); }\n"
                "static inline F2C_UNUSED void f2c_write_i16(FILE *f, int16_t v) { fprintf(f, "
                "\" %d\", (int)v); }\n"
                "static inline F2C_UNUSED void f2c_write_i32(FILE *f, int32_t v) { fprintf(f, \" "
                "%d\", "
                "(int)v); }\n"
                "static inline F2C_UNUSED void f2c_write_i64(FILE *f, int64_t v) { fprintf(f, "
                "\" %lld\", (long long)v); }\n"
                "static inline F2C_UNUSED void f2c_write_float(FILE *f, float v) { fprintf(f, \" "
                "%.9g\", "
                "(double)v); }\n"
                "static inline F2C_UNUSED void f2c_write_double(FILE *f, double v) { fprintf(f, \" "
                "%.17g\", v); }\n"
                "static inline F2C_UNUSED void f2c_write_bool(FILE *f, bool v) { fputs(v ? \" T\" "
                ": \" "
                "F\", f); }\n"
                "static inline F2C_UNUSED void f2c_write_char(FILE *f, char v) { fprintf(f, \" "
                "%c\", v); "
                "}\n"
                "static inline F2C_UNUSED void f2c_write_string(FILE *f, const char *v) { "
                "fprintf(f, \" "
                "%s\", v); }\n"
                "static inline F2C_UNUSED void f2c_write_character(FILE *f, const char *v, "
                "size_t length) { fputc(' ', f); (void)fwrite(v, 1U, length, f); }\n"
                "static inline F2C_UNUSED int f2c_read_number_token(FILE *f, char *v, size_t n) { "
                "int c; size_t i = 0U; if (n == 0U) return 0; do { c = fgetc(f); } while (c != "
                "EOF && (isspace((unsigned char)c) || c == ',')); if (c == EOF) return EOF; do { "
                "if (i + 1U < n) v[i++] = (char)c; c = fgetc(f); } while (c != EOF && "
                "!isspace((unsigned char)c) && c != ',' && c != ')'); if (c != EOF) ungetc(c, f); "
                "v[i] = '\\0'; for (i = 0U; v[i] != '\\0'; ++i) if (v[i] == 'd' || v[i] == "
                "'D') v[i] = 'e'; return 1; }\n"
                "static inline F2C_UNUSED int f2c_read_i32(FILE *f, int32_t *v) { char t[128]; "
                "int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = "
                "(int32_t)strtol(t, NULL, 10); return r; }\n"
                "static inline F2C_UNUSED int f2c_read_i8(FILE *f, int8_t *v) { char t[128]; "
                "int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = "
                "(int8_t)strtol(t, NULL, 10); return r; }\n"
                "static inline F2C_UNUSED int f2c_read_i16(FILE *f, int16_t *v) { char t[128]; "
                "int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = "
                "(int16_t)strtol(t, NULL, 10); return r; }\n"
                "static inline F2C_UNUSED int f2c_read_i64(FILE *f, int64_t *v) { char t[128]; "
                "int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = "
                "(int64_t)strtoll(t, NULL, 10); return r; }\n"
                "static inline F2C_UNUSED int f2c_read_float(FILE *f, float *v) { char t[128]; int "
                "r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = strtof(t, NULL); "
                "return r; }\n"
                "static inline F2C_UNUSED int f2c_read_double(FILE *f, double *v) { char t[128]; "
                "int r = f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = strtod(t, NULL); "
                "return r; }\n"
                "static inline F2C_UNUSED int f2c_read_bool(FILE *f, bool *v) { char t[16]; int r "
                "= f2c_read_number_token(f, t, sizeof(t)); if (r == 1) *v = t[0] == 'T' || t[0] "
                "== 't' || t[0] == '1'; return r; }\n"
                "static inline F2C_UNUSED int f2c_read_char(FILE *f, char *v) { return fscanf(f, "
                "\" %c\", "
                "v); }\n");
            f2c_buffer_append(
                &context.output,
                "static inline F2C_UNUSED int f2c_read_character(FILE *f, char *v, size_t "
                "length) { int c, quote = 0; size_t i = 0U; do { c = fgetc(f); } while (c != "
                "EOF && (isspace((unsigned char)c) || c == ',')); if (c == EOF) return EOF; "
                "if (c == '\\'' || c == '\"') quote = c; else if (i < length) v[i++] = "
                "(char)c; while ((c = fgetc(f)) != EOF) { if (quote != 0) { if (c == quote) { "
                "int next = fgetc(f); if (next == quote) c = next; else { if (next != EOF) "
                "ungetc(next, f); break; } } } else if (isspace((unsigned char)c) || c == ',') "
                "{ ungetc(c, f); break; } if (i < length) v[i++] = (char)c; } if (i < length) "
                "memset(v + i, ' ', length - i); return 1; }\n"
                "static inline F2C_UNUSED int f2c_read_record(FILE *f, char *v, size_t capacity) { "
                "char *r; size_t length; if (capacity == 0U) return 0; r = fgets(v, (int)capacity, "
                "f); if (r == NULL) return EOF; length = strcspn(v, \"\\r\\n\"); if (v[length] "
                "!= '\\0') { int c = v[length]; v[length] = '\\0'; if (c != '\\n') { do { c = "
                "fgetc(f); } while (c != '\\n' && c != EOF); } } memset(v + length, ' ', capacity "
                "- length - 1U); v[capacity - 1U] = '\\0'; return 1; }\n"
                "static inline F2C_UNUSED void f2c_finish_read(FILE *f) { int c; do { c = "
                "fgetc(f); } while (c != '\\n' && c != EOF); }\n");
            if (needs_complex) {
                f2c_buffer_append(
                    &context.output,
                    "static inline F2C_UNUSED void f2c_write_c(FILE *f, float complex v) { "
                    "fprintf(f, \" "
                    "(%.9g,%.9g)\", (double)crealf(v), (double)cimagf(v)); }\n"
                    "static inline F2C_UNUSED void f2c_write_z(FILE *f, double complex v) { "
                    "fprintf(f, \" "
                    "(%.17g,%.17g)\", creal(v), cimag(v)); }\n"
                    "static inline F2C_UNUSED int f2c_read_complex_parts(FILE *f, double *r, "
                    "double *i) { char a[128], b[128]; size_t p = 0U; int c; do { c = fgetc(f); "
                    "} while (c != EOF && (isspace((unsigned char)c) || c == ',')); if (c != '(') "
                    "return c == EOF ? EOF : 0; while ((c = fgetc(f)) != EOF && c != ',') if (p + "
                    "1U < sizeof(a)) a[p++] = (char)c; if (c == EOF) return EOF; a[p] = '\\0'; p "
                    "= 0U; while ((c = fgetc(f)) != EOF && c != ')') if (p + 1U < sizeof(b)) "
                    "b[p++] = (char)c; if (c == EOF) return EOF; b[p] = '\\0'; for (p = 0U; "
                    "a[p] != '\\0'; ++p) if (a[p] == 'd' || a[p] == 'D') a[p] = 'e'; for (p = "
                    "0U; b[p] != '\\0'; ++p) if (b[p] == 'd' || b[p] == 'D') b[p] = 'e'; *r = "
                    "strtod(a, NULL); *i = strtod(b, NULL); return 2; }\n"
                    "static inline F2C_UNUSED int f2c_read_c(FILE *f, float complex *v) { double "
                    "r, i; int n = f2c_read_complex_parts(f, &r, &i); if (n == 2) *v = "
                    "f2c_make_c((float)r, (float)i); return n; }\n"
                    "static inline F2C_UNUSED int f2c_read_z(FILE *f, double complex *v) { double "
                    "r, i; int n = f2c_read_complex_parts(f, &r, &i); if (n == 2) *v = "
                    "f2c_make_z(r, i); return n; }\n");
            }
            f2c_buffer_append(
                &context.output,
                needs_complex
                    ? "#define F2C_WRITE(f, v) _Generic((v), int8_t: f2c_write_i8, int16_t: "
                      "f2c_write_i16, int32_t: f2c_write_i32, int64_t: f2c_write_i64, float: "
                      "f2c_write_float, double: f2c_write_double, bool: f2c_write_bool, char: "
                      "f2c_write_char, char *: f2c_write_string, const char *: "
                      "f2c_write_string, float complex: f2c_write_c, double complex: "
                      "f2c_write_z)((f), (v))\n"
                      "#define F2C_READ(f, v) _Generic((v), int8_t *: f2c_read_i8, int16_t *: "
                      "f2c_read_i16, int32_t *: f2c_read_i32, int64_t *: f2c_read_i64, float *: "
                      "f2c_read_float, double *: f2c_read_double, bool *: f2c_read_bool, char *: "
                      "f2c_read_char, float complex *: f2c_read_c, double complex *: "
                      "f2c_read_z)((f), (v))\n"
                    : "#define F2C_WRITE(f, v) _Generic((v), int8_t: f2c_write_i8, int16_t: "
                      "f2c_write_i16, int32_t: f2c_write_i32, int64_t: f2c_write_i64, float: "
                      "f2c_write_float, double: f2c_write_double, bool: f2c_write_bool, char: "
                      "f2c_write_char, char *: f2c_write_string, const char *: "
                      "f2c_write_string)((f), (v))\n"
                      "#define F2C_READ(f, v) _Generic((v), int8_t *: f2c_read_i8, int16_t *: "
                      "f2c_read_i16, int32_t *: f2c_read_i32, int64_t *: f2c_read_i64, float *: "
                      "f2c_read_float, double *: f2c_read_double, bool *: f2c_read_bool, char *: "
                      "f2c_read_char)((f), (v))\n");
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
    }
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

#ifndef F2C_VERSION
#define F2C_VERSION "1.0.0"
#endif

const char *f2c_version(void) { return F2C_VERSION; }
