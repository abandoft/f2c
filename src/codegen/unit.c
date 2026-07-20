#include "codegen/unit/private.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit_declarations(Context *context, Unit *unit) {
    Buffer *output = &context->output;
    size_t i;
    f2c_unit_emit_equivalence_declarations(context, unit);
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        size_t dimension;
        const char *name;
        if (!symbol->argument || !f2c_symbol_uses_descriptor(symbol))
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output,
                          "if (f2c_descriptor_%s != NULL && (f2c_descriptor_%s->rank != %zuU || "
                          "f2c_descriptor_%s->element_size != sizeof(%s))) abort();\n",
                          name, name, symbol->rank, name, f2c_symbol_c_type(symbol));
        if (!symbol->optional) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "if (f2c_descriptor_%s == NULL) abort();\n", name);
        }
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output,
                          "%s *%s = f2c_descriptor_%s != NULL ? (%s *)"
                          "f2c_descriptor_%s->data : NULL;\n",
                          f2c_symbol_c_type(symbol), name, name, f2c_symbol_c_type(symbol), name);
        if (symbol->deferred_character) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_len_%s = f2c_descriptor_%s != NULL ? "
                              "f2c_descriptor_%s->character_length : 0U;\n",
                              name, name, name);
        } else if (symbol->type == TYPE_CHARACTER && symbol->character_length != NULL &&
                   strcmp(symbol->character_length, "*") == 0) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "const size_t f2c_len_%s = f2c_descriptor_%s != NULL ? "
                              "f2c_descriptor_%s->character_length : 0U;\n",
                              name, name, name);
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            char *declared_lower = !symbol->allocatable && !symbol->pointer
                                       ? f2c_emit_typed_expression(
                                             unit, symbol->dimensions[dimension].lower_expression)
                                       : NULL;
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(
                output,
                "if (f2c_descriptor_%s != NULL && (f2c_descriptor_%s->lower[%zu] < "
                "INT32_MIN || f2c_descriptor_%s->lower[%zu] > INT32_MAX || "
                "f2c_descriptor_%s->extent[%zu] < 0 || f2c_descriptor_%s->extent[%zu] > "
                "INT32_MAX || (f2c_descriptor_%s->extent[%zu] > 1 && "
                "f2c_descriptor_%s->stride[%zu] == 0))) abort();\n",
                name, name, dimension, name, dimension, name, dimension, name, dimension, name,
                dimension, name, dimension);
            f2c_unit_indent(output, 1);
            if (symbol->allocatable || symbol->pointer)
                f2c_buffer_printf(output,
                                  "int32_t %s_lower_%zu = f2c_descriptor_%s != NULL ? "
                                  "(int32_t)f2c_descriptor_%s->lower[%zu] : 1;\n",
                                  name, dimension + 1U, name, name, dimension);
            else
                f2c_buffer_printf(output, "const int32_t %s_lower_%zu = (int32_t)(%s);\n", name,
                                  dimension + 1U, declared_lower != NULL ? declared_lower : "1");
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "int32_t %s_extent_%zu = f2c_descriptor_%s != NULL ? "
                              "(int32_t)f2c_descriptor_%s->extent[%zu] : 0;\n",
                              name, dimension + 1U, name, name, dimension);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "%sptrdiff_t %s_stride_%zu = f2c_descriptor_%s != NULL ? "
                              "f2c_descriptor_%s->stride[%zu] : 0;\n",
                              symbol->allocatable || symbol->pointer ? "" : "const ", name,
                              dimension + 1U, name, name, dimension);
            free(declared_lower);
        }
    }
    {
        Symbol *result = f2c_unit_function_result(unit);
        if (result != NULL && result->allocatable) {
            size_t dimension;
            const char *name = f2c_symbol_c_name(unit, result);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "%s *%s = NULL;\n", f2c_symbol_c_type(result), name);
            if (result->deferred_character) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "size_t f2c_char_len_%s = 0U;\n", name);
            }
            for (dimension = 0U; dimension < result->rank; ++dimension) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "int32_t %s_lower_%zu = 1;\n", name, dimension + 1U);
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "int32_t %s_extent_%zu = 0;\n", name, dimension + 1U);
            }
            f2c_unit_indent(output, 1);
            f2c_buffer_append(output, "f2c_descriptor f2c_result_descriptor = {0};\n");
        }
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const int persistent = unit->save_all || symbol->saved || symbol->initializer != NULL;
        char *initializer = NULL;
        if (symbol->procedure_pointer && !symbol->argument && !symbol->module_entity) {
            f2c_unit_indent(output, 1);
            if (persistent)
                f2c_buffer_append(output, "static ");
            f2c_emit_procedure_pointer_type(output, symbol, f2c_symbol_c_name(unit, symbol));
            f2c_buffer_append(output, " = NULL;\n");
            continue;
        }
        if (symbol->argument || symbol->parameter || symbol->external || symbol->module_entity ||
            symbol->common_block != NULL || symbol->equivalence_associated ||
            symbol->alias_to != NULL || symbol->statement_function ||
            (unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
             strcmp(symbol->name, unit->result_name) == 0)) {
            continue;
        }
        f2c_unit_indent(output, 1);
        if (persistent)
            f2c_buffer_append(output, "static ");
        if (symbol->parameter)
            f2c_buffer_append(output, "const ");
        if (symbol->allocatable || symbol->pointer) {
            size_t d;
            f2c_buffer_printf(output, "%s *%s = NULL;\n", f2c_symbol_c_type(symbol),
                              f2c_symbol_c_name(unit, symbol));
            if (symbol->deferred_character) {
                f2c_unit_indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "size_t f2c_char_len_%s = 0U;\n",
                                  f2c_symbol_c_name(unit, symbol));
            }
            for (d = 0U; d < symbol->rank; ++d) {
                f2c_unit_indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "int32_t %s_lower_%zu = 1;\n",
                                  f2c_symbol_c_name(unit, symbol), d + 1U);
                f2c_unit_indent(output, 1);
                if (persistent)
                    f2c_buffer_append(output, "static ");
                f2c_buffer_printf(output, "int32_t %s_extent_%zu = 0;\n",
                                  f2c_symbol_c_name(unit, symbol), d + 1U);
                if (symbol->pointer) {
                    f2c_unit_indent(output, 1);
                    if (persistent)
                        f2c_buffer_append(output, "static ");
                    f2c_buffer_printf(output, "ptrdiff_t %s_stride_%zu = 0;\n",
                                      f2c_symbol_c_name(unit, symbol), d + 1U);
                }
            }
            continue;
        }
        if (symbol->automatic_character) {
            char *length = f2c_emit_typed_expression(unit, symbol->character_length_expression);
            char *count =
                symbol->rank != 0U ? f2c_symbol_element_count(unit, symbol) : f2c_strdup("1U");
            const char *name = f2c_symbol_c_name(unit, symbol);
            f2c_buffer_printf(output, "int64_t f2c_char_len_value_%s = (int64_t)(%s);\n", name,
                              length != NULL ? length : "0");
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_len_%s = f2c_char_len_value_%s > 0 ? "
                              "(size_t)f2c_char_len_value_%s : 0U;\n",
                              name, name, name);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "size_t f2c_char_count_%s = (size_t)(%s);\n", name,
                              count != NULL ? count : "0U");
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "if (f2c_char_len_%s != 0U && f2c_char_count_%s > "
                              "SIZE_MAX / f2c_char_len_%s) abort();\n",
                              name, name, name);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "size_t f2c_char_bytes_%s = f2c_char_count_%s * "
                              "f2c_char_len_%s;\n",
                              name, name, name);
            if (symbol->rank == 0U) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "if (f2c_char_bytes_%s == SIZE_MAX) abort();\n", name);
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "++f2c_char_bytes_%s;\n", name);
            }
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "char *%s = (char *)malloc(f2c_char_bytes_%s == 0U ? 1U : "
                              "f2c_char_bytes_%s);\n",
                              name, name, name);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "if (%s == NULL) abort();\n", name);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output,
                              "if (f2c_char_bytes_%s != 0U) memset(%s, 0, "
                              "f2c_char_bytes_%s);\n",
                              name, name, name);
            free(length);
            free(count);
            continue;
        }
        f2c_buffer_printf(output, "%s %s", f2c_symbol_c_type(symbol),
                          f2c_symbol_c_name(unit, symbol));
        if (symbol->type == TYPE_CHARACTER && symbol->rank == 0U &&
            symbol->character_length != NULL) {
            char *length = f2c_emit_typed_expression(unit, symbol->character_length_expression);
            f2c_buffer_printf(output, "[(%s) + 1]", length);
            free(length);
        } else if (symbol->rank != 0U) {
            size_t d;
            f2c_buffer_append(output, "[F2C_MAX(1, ");
            if (symbol->type == TYPE_CHARACTER) {
                char *length =
                    symbol->character_length != NULL
                        ? f2c_emit_typed_expression(unit, symbol->character_length_expression)
                        : f2c_strdup("1U");
                f2c_buffer_printf(output, "(size_t)(%s) * ", length);
                free(length);
            }
            for (d = 0U; d < symbol->rank; ++d) {
                char *lo;
                char *hi;
                lo = f2c_emit_typed_expression(unit, symbol->dimensions[d].lower_expression);
                hi = f2c_emit_typed_expression(unit, symbol->dimensions[d].upper_expression);
                f2c_buffer_printf(output, "%s((%s) - (%s) + 1)", d == 0U ? "" : " * ", hi, lo);
                free(lo);
                free(hi);
            }
            f2c_buffer_append(output, ")]");
        }
        if (symbol->data_element_initializers != NULL) {
            initializer = f2c_unit_data_array_initializer(unit, symbol);
            if (initializer == NULL)
                f2c_diagnostic(context, symbol->declaration_line, 1,
                               "typed DATA initializer for array '%s' cannot be emitted",
                               symbol->name);
        } else if (symbol->initializer != NULL) {
            if (symbol->type == TYPE_CHARACTER) {
                int supported = 0;
                initializer = f2c_character_declaration_initializer(unit, symbol, &supported);
                if (!supported) {
                    f2c_diagnostic(context, symbol->declaration_line, 1,
                                   "unsupported non-constant or shape-incompatible CHARACTER "
                                   "declaration initializer for '%s'",
                                   symbol->name);
                }
            } else {
                initializer = f2c_emit_typed_expression(unit, symbol->initializer_expression);
            }
        }
        if (initializer != NULL)
            f2c_buffer_printf(output, " = %s", initializer);
        else if (symbol->initializer != NULL || symbol->data_element_initializers != NULL)
            f2c_buffer_append(output, " = {0}");
        else if (!symbol->parameter && symbol->rank == 0U)
            f2c_buffer_append(output, " = {0}");
        f2c_buffer_append(output, ";\n");
        if (symbol->scope_begin_line != 0U && symbol->type == TYPE_DERIVED &&
            symbol->derived_type != NULL && !persistent) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "bool f2c_scope_live_%s = false;\n",
                              f2c_symbol_c_name(unit, symbol));
        }
        if (symbol->initializer == NULL && !symbol->parameter && symbol->rank != 0U &&
            !unit->save_all && !symbol->saved) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "memset(%s, 0, sizeof(%s));\n",
                              f2c_symbol_c_name(unit, symbol), f2c_symbol_c_name(unit, symbol));
        }
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL &&
            symbol->scope_begin_line == 0U) {
            const char *name = f2c_symbol_c_name(unit, symbol);
            if (symbol->rank == 0U) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "f2c_initialize_%s(&%s);\n", symbol->derived_type->c_name,
                                  name);
            } else {
                char *count = f2c_symbol_element_count(unit, symbol);
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output,
                                  "for (size_t f2c_derived_index = 0U; "
                                  "f2c_derived_index < (size_t)(%s); ++f2c_derived_index) "
                                  "f2c_initialize_%s(&%s[f2c_derived_index]);\n",
                                  count != NULL ? count : "0U", symbol->derived_type->c_name, name);
                free(count);
            }
        }
        free(initializer);
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const char *name;
        char *count;
        size_t dimension;
        if (!symbol->argument || symbol->intent != F2C_INTENT_OUT)
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        if (symbol->pointer) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "%s = NULL;\n", name);
            if (symbol->deferred_character) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "f2c_char_len_%s = 0U;\n", name);
            }
            for (dimension = 0U; dimension < symbol->rank; ++dimension) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output,
                                  "%s_lower_%zu = 1; %s_extent_%zu = 0; %s_stride_%zu = 0;\n", name,
                                  dimension + 1U, name, dimension + 1U, name, dimension + 1U);
            }
            continue;
        }
        if (symbol->type != TYPE_DERIVED || symbol->derived_type == NULL)
            continue;
        count = symbol->rank == 0U ? f2c_strdup("1U") : f2c_symbol_element_count(unit, symbol);
        f2c_unit_indent(output, 1);
        if (symbol->allocatable) {
            f2c_buffer_printf(output, "if (%s != NULL) {\n", name);
            f2c_unit_indent(output, 2);
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            f2c_unit_indent(output, 2);
            f2c_buffer_printf(output, "free(%s); %s = NULL;\n", name, name);
            f2c_unit_indent(output, 1);
            f2c_buffer_append(output, "}\n");
        } else {
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "%s_%s(%s, (size_t)(%s));\n",
                              symbol->polymorphic ? "f2c_initialize_dynamic"
                                                  : "f2c_initialize_dynamic",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U");
        }
        free(count);
    }
    if (unit->kind == UNIT_FUNCTION && unit->return_type != TYPE_CHARACTER &&
        !f2c_unit_has_allocatable_result(unit)) {
        f2c_unit_indent(output, 1);
        Symbol *result = f2c_unit_function_result(unit);
        f2c_buffer_printf(output, "%s f2c_result = {0};\n", f2c_unit_function_return_type(unit));
        if (result != NULL && result->type == TYPE_DERIVED && result->derived_type != NULL) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "f2c_initialize_%s(&f2c_result);\n",
                              result->derived_type->c_name);
        }
    }
}

static int has_local_declaration(Unit *unit, Symbol *symbol) {
    return !symbol->argument && !symbol->parameter && !symbol->external && !symbol->module_entity &&
           symbol->common_block == NULL && !symbol->equivalence_associated &&
           symbol->alias_to == NULL && !symbol->statement_function &&
           !(unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
             strcmp(symbol->name, unit->result_name) == 0);
}

static size_t statement_function_expansion_count(F2cExpr *expression) {
    size_t count = 0U;
    size_t child;
    Symbol *function;
    if (expression == NULL)
        return 0U;
    for (child = 0U; child < expression->child_count; ++child) {
        const size_t nested = statement_function_expansion_count(expression->children[child]);
        if (nested > SIZE_MAX - count)
            return SIZE_MAX;
        count += nested;
    }
    function = expression->kind == F2C_EXPR_CALL ? expression->symbol : NULL;
    if (function == NULL || !function->statement_function)
        return count;
    if (count == SIZE_MAX)
        return count;
    ++count;
    if (!function->statement_function_expanding &&
        function->statement_function_expression != NULL) {
        size_t nested;
        function->statement_function_expanding = 1;
        nested = statement_function_expansion_count(function->statement_function_expression);
        function->statement_function_expanding = 0;
        if (nested > SIZE_MAX - count)
            return SIZE_MAX;
        count += nested;
    }
    return count;
}

typedef struct StatementFunctionTemporaryAssigner {
    size_t next;
} StatementFunctionTemporaryAssigner;

static void assign_statement_function_temporary(F2cExpr *expression, void *state) {
    StatementFunctionTemporaryAssigner *assigner = (StatementFunctionTemporaryAssigner *)state;
    size_t nested;
    if (expression == NULL || expression->kind != F2C_EXPR_CALL || expression->symbol == NULL ||
        !expression->symbol->statement_function)
        return;
    expression->statement_temporary_index = assigner->next++;
    expression->statement_nested_temporary_begin = assigner->next;
    nested = statement_function_expansion_count(expression->symbol->statement_function_expression);
    if (nested != SIZE_MAX && nested <= SIZE_MAX - assigner->next)
        assigner->next += nested;
}

static void prepare_statement_function_temporaries(Unit *unit) {
    size_t statement;
    StatementFunctionTemporaryAssigner assigner = {0U};
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!f2c_unit_statement_is_function_definition(unit, statement))
            f2c_visit_statement_expressions(&unit->statements[statement],
                                            assign_statement_function_temporary, &assigner);
}

typedef struct StatementFunctionTemporaryEmitter {
    Buffer *output;
    Unit *unit;
} StatementFunctionTemporaryEmitter;

static void emit_statement_function_temporary_for_call(StatementFunctionTemporaryEmitter *emitter,
                                                       Symbol *function, size_t temporary) {
    size_t argument;
    for (argument = 0U; argument < function->statement_function_argument_count; ++argument) {
        Symbol *dummy =
            f2c_find_symbol(emitter->unit, function->statement_function_arguments[argument]);
        f2c_unit_indent(emitter->output, 1);
        f2c_buffer_printf(emitter->output, "%s f2c_statement_argument_%zu_%zu = {0};\n",
                          f2c_symbol_c_type(dummy), temporary, argument);
        f2c_unit_indent(emitter->output, 1);
        f2c_buffer_printf(emitter->output, "(void)f2c_statement_argument_%zu_%zu;\n", temporary,
                          argument);
    }
}

static void emit_nested_statement_function_temporaries(F2cExpr *expression,
                                                       StatementFunctionTemporaryEmitter *emitter,
                                                       size_t *next) {
    size_t child;
    Symbol *function;
    if (expression == NULL)
        return;
    for (child = 0U; child < expression->child_count; ++child)
        emit_nested_statement_function_temporaries(expression->children[child], emitter, next);
    function = expression->kind == F2C_EXPR_CALL ? expression->symbol : NULL;
    if (function == NULL || !function->statement_function)
        return;
    emit_statement_function_temporary_for_call(emitter, function, (*next)++);
    if (!function->statement_function_expanding &&
        function->statement_function_expression != NULL) {
        function->statement_function_expanding = 1;
        emit_nested_statement_function_temporaries(function->statement_function_expression, emitter,
                                                   next);
        function->statement_function_expanding = 0;
    }
}

static void emit_statement_function_temporary(F2cExpr *expression, void *state) {
    StatementFunctionTemporaryEmitter *emitter = (StatementFunctionTemporaryEmitter *)state;
    size_t next;
    if (expression == NULL || expression->kind != F2C_EXPR_CALL || expression->symbol == NULL ||
        !expression->symbol->statement_function ||
        expression->statement_temporary_index == SIZE_MAX)
        return;
    emit_statement_function_temporary_for_call(emitter, expression->symbol,
                                               expression->statement_temporary_index);
    next = expression->statement_nested_temporary_begin;
    emit_nested_statement_function_temporaries(expression->symbol->statement_function_expression,
                                               emitter, &next);
}

static void emit_statement_function_temporaries(Buffer *output, Unit *unit) {
    StatementFunctionTemporaryEmitter emitter = {output, unit};
    size_t statement;
    for (statement = 0U; statement < unit->statement_count; ++statement)
        if (!f2c_unit_statement_is_function_definition(unit, statement))
            f2c_visit_statement_expressions(&unit->statements[statement],
                                            emit_statement_function_temporary, &emitter);
}

typedef struct CharacterTemporaryCleanupEmitter {
    Buffer *output;
    int depth;
} CharacterTemporaryCleanupEmitter;

static int block_scoped_symbol(const Unit *unit, const Symbol *symbol) {
    return symbol->scope_begin_line != 0U && !unit->save_all && !symbol->saved &&
           symbol->initializer == NULL && !symbol->argument && !symbol->module_entity;
}

static void emit_block_symbol_cleanup(Buffer *output, Unit *unit, Symbol *symbol, int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    size_t dimension;
    if (!block_scoped_symbol(unit, symbol))
        return;
    if (symbol->allocatable) {
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "if (%s != NULL) {\n", name);
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            free(count);
        }
        f2c_unit_indent(output, depth + 1);
        f2c_buffer_printf(output, "free(%s); %s = NULL;\n", name, name);
        if (symbol->deferred_character) {
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_char_len_%s = 0U;\n", name);
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "%s_lower_%zu = 1; %s_extent_%zu = 0;\n", name,
                              dimension + 1U, name, dimension + 1U);
        }
        f2c_unit_indent(output, depth);
        f2c_buffer_append(output, "}\n");
    } else if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "if (f2c_scope_live_%s) {\n", name);
        f2c_unit_indent(output, depth + 1);
        if (symbol->rank == 0U) {
            f2c_buffer_printf(output, "f2c_destroy_%s(&%s);\n", symbol->derived_type->c_name, name);
        } else {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            free(count);
        }
        f2c_unit_indent(output, depth + 1);
        f2c_buffer_printf(output, "f2c_scope_live_%s = false;\n", name);
        f2c_unit_indent(output, depth);
        f2c_buffer_append(output, "}\n");
    }
}

void f2c_emit_block_scope_begin(Buffer *output, Unit *unit, size_t line, int depth) {
    size_t i;
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const char *name;
        if (!block_scoped_symbol(unit, symbol) || symbol->scope_begin_line != line ||
            symbol->allocatable || symbol->type != TYPE_DERIVED || symbol->derived_type == NULL)
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        f2c_unit_indent(output, depth);
        if (symbol->rank == 0U) {
            f2c_buffer_printf(output, "f2c_initialize_%s(&%s);\n", symbol->derived_type->c_name,
                              name);
        } else {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_buffer_printf(output,
                              "for (size_t f2c_scope_index = 0U; "
                              "f2c_scope_index < (size_t)(%s); ++f2c_scope_index) "
                              "f2c_initialize_%s(&%s[f2c_scope_index]);\n",
                              count != NULL ? count : "0U", symbol->derived_type->c_name, name);
            free(count);
        }
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_scope_live_%s = true;\n", name);
    }
}

void f2c_emit_block_scope_end(Buffer *output, Unit *unit, size_t line, int depth) {
    size_t i = unit->symbol_count;
    while (i != 0U) {
        Symbol *symbol = &unit->symbols[--i];
        if (symbol->scope_end_line == line)
            emit_block_symbol_cleanup(output, unit, symbol, depth);
    }
}

void f2c_emit_scope_cleanup_plan(Buffer *output, Unit *unit, const F2cScopeCleanupPlan *plan,
                                 int depth) {
    size_t index;
    if (plan == NULL)
        return;
    for (index = 0U; index < plan->symbol_count; ++index)
        emit_block_symbol_cleanup(output, unit, plan->symbols[index], depth);
}

static void emit_character_temporary_cleanup(F2cExpr *expression, void *state) {
    CharacterTemporaryCleanupEmitter *emitter = (CharacterTemporaryCleanupEmitter *)state;
    if (!f2c_unit_expression_is_character_temporary(expression))
        return;
    f2c_unit_indent(emitter->output, emitter->depth);
    f2c_buffer_printf(emitter->output, "free(f2c_character_result_%zu);\n",
                      expression->temporary_index);
}

void f2c_emit_unit_cleanup(Buffer *output, Unit *unit, int depth) {
    size_t i;
    CharacterTemporaryCleanupEmitter emitter = {output, depth};
    Symbol *function_result = f2c_unit_function_result(unit);
    for (i = 0U; i < unit->statement_count; ++i)
        if (!f2c_unit_statement_is_function_definition(unit, i))
            f2c_visit_statement_expressions(&unit->statements[i], emit_character_temporary_cleanup,
                                            &emitter);
    if (function_result != NULL && function_result->allocatable) {
        const char *name = f2c_symbol_c_name(unit, function_result);
        size_t dimension;
        char *character_length = function_result->type == TYPE_CHARACTER
                                     ? f2c_symbol_character_length(unit, function_result)
                                     : NULL;
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.data = %s;\n", name);
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.element_size = sizeof(%s);\n",
                          f2c_symbol_c_type(function_result));
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.rank = %zuU;\n", function_result->rank);
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.character_length = (size_t)(%s);\n",
                          character_length != NULL ? character_length : "0U");
        for (dimension = 0U; dimension < function_result->rank; ++dimension) {
            f2c_unit_indent(output, depth);
            f2c_buffer_printf(output, "f2c_result_descriptor.lower[%zu] = %s_lower_%zu;\n",
                              dimension, name, dimension + 1U);
            f2c_unit_indent(output, depth);
            f2c_buffer_printf(output, "f2c_result_descriptor.extent[%zu] = %s_extent_%zu;\n",
                              dimension, name, dimension + 1U);
            f2c_unit_indent(output, depth);
            if (dimension == 0U)
                f2c_buffer_append(output, "f2c_result_descriptor.stride[0] = 1;\n");
            else
                f2c_buffer_printf(
                    output,
                    "f2c_result_descriptor.stride[%zu] = f2c_descriptor_stride_extent("
                    "f2c_result_descriptor.stride[%zu], "
                    "(size_t)f2c_result_descriptor.extent[%zu]);\n",
                    dimension, dimension - 1U, dimension - 1U);
        }
        free(character_length);
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        size_t dimension;
        if (symbol == function_result || symbol->external)
            continue;
        if ((symbol->allocatable || symbol->pointer) && symbol->argument) {
            const char *name = f2c_symbol_c_name(unit, symbol);
            f2c_unit_indent(output, depth);
            f2c_buffer_printf(output, "if (f2c_descriptor_%s != NULL) {\n", name);
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_descriptor_%s->data = %s;\n", name, name);
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_descriptor_%s->element_size = sizeof(%s);\n", name,
                              f2c_symbol_c_type(symbol));
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_descriptor_%s->rank = %zuU;\n", name, symbol->rank);
            if (symbol->deferred_character) {
                f2c_unit_indent(output, depth + 1);
                f2c_buffer_printf(
                    output, "f2c_descriptor_%s->character_length = f2c_char_len_%s;\n", name, name);
            }
            for (dimension = 0U; dimension < symbol->rank; ++dimension) {
                f2c_unit_indent(output, depth + 1);
                f2c_buffer_printf(output, "f2c_descriptor_%s->lower[%zu] = %s_lower_%zu;\n", name,
                                  dimension, name, dimension + 1U);
                f2c_unit_indent(output, depth + 1);
                f2c_buffer_printf(output, "f2c_descriptor_%s->extent[%zu] = %s_extent_%zu;\n", name,
                                  dimension, name, dimension + 1U);
                f2c_unit_indent(output, depth + 1);
                if (symbol->pointer)
                    f2c_buffer_printf(output, "f2c_descriptor_%s->stride[%zu] = %s_stride_%zu;\n",
                                      name, dimension, name, dimension + 1U);
                else if (dimension == 0U)
                    f2c_buffer_printf(output, "f2c_descriptor_%s->stride[0] = 1;\n", name);
                else
                    f2c_buffer_printf(
                        output,
                        "f2c_descriptor_%s->stride[%zu] = f2c_descriptor_stride_extent("
                        "f2c_descriptor_%s->stride[%zu], "
                        "(size_t)f2c_descriptor_%s->extent[%zu]);\n",
                        name, dimension, name, dimension - 1U, name, dimension - 1U);
            }
            f2c_unit_indent(output, depth);
            f2c_buffer_append(output, "}\n");
            continue;
        }
        if (symbol->allocatable && !symbol->argument && !unit->save_all && !symbol->saved &&
            symbol->initializer == NULL) {
            f2c_unit_indent(output, depth);
            if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
                char *count = f2c_symbol_element_count(unit, symbol);
                f2c_buffer_printf(output,
                                  "if (%s != NULL) f2c_destroy_array_%s(%s, (size_t)(%s), "
                                  "%zuU);\n",
                                  f2c_symbol_c_name(unit, symbol), symbol->derived_type->c_name,
                                  f2c_symbol_c_name(unit, symbol), count != NULL ? count : "0U",
                                  symbol->rank);
                f2c_unit_indent(output, depth);
                free(count);
            }
            f2c_buffer_printf(output, "free(%s);\n", f2c_symbol_c_name(unit, symbol));
            continue;
        }
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL && !symbol->argument &&
            !symbol->module_entity && !unit->save_all && !symbol->saved &&
            symbol->initializer == NULL) {
            f2c_unit_indent(output, depth);
            if (symbol->scope_begin_line != 0U) {
                f2c_buffer_printf(output, "if (f2c_scope_live_%s) {\n",
                                  f2c_symbol_c_name(unit, symbol));
                f2c_unit_indent(output, depth + 1);
            }
            if (symbol->rank == 0U) {
                f2c_buffer_printf(output, "f2c_destroy_%s(&%s);\n", symbol->derived_type->c_name,
                                  f2c_symbol_c_name(unit, symbol));
            } else {
                char *count = f2c_symbol_element_count(unit, symbol);
                f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                                  symbol->derived_type->c_name, f2c_symbol_c_name(unit, symbol),
                                  count != NULL ? count : "0U", symbol->rank);
                free(count);
            }
            if (symbol->scope_begin_line != 0U) {
                f2c_unit_indent(output, depth);
                f2c_buffer_append(output, "}\n");
            }
            continue;
        }
        if (!symbol->automatic_character)
            continue;
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "free(%s);\n", f2c_symbol_c_name(unit, symbol));
    }
}

static void emit_unused_suppression(Buffer *output, Unit *unit) {
    size_t i;
    if (unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER &&
        !f2c_unit_has_allocatable_result(unit)) {
        f2c_unit_indent(output, 1);
        f2c_buffer_append(output, "(void)f2c_result;\n");
        f2c_unit_indent(output, 1);
        f2c_buffer_append(output, "(void)f2c_result_len;\n");
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "(void)%s;\n",
                          symbol != NULL ? f2c_symbol_c_name(unit, symbol) : unit->arguments[i]);
        if (symbol != NULL && f2c_symbol_uses_descriptor(symbol)) {
            size_t dimension;
            for (dimension = 0U; dimension < symbol->rank; ++dimension) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "(void)%s_lower_%zu;\n", f2c_symbol_c_name(unit, symbol),
                                  dimension + 1U);
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "(void)%s_extent_%zu;\n", f2c_symbol_c_name(unit, symbol),
                                  dimension + 1U);
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "(void)%s_stride_%zu;\n", f2c_symbol_c_name(unit, symbol),
                                  dimension + 1U);
            }
        }
        if (symbol != NULL && !symbol->external && symbol->type == TYPE_CHARACTER &&
            !f2c_symbol_uses_descriptor(symbol)) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "(void)f2c_len_%s;\n", f2c_symbol_c_name(unit, symbol));
        }
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        size_t dimension;
        if (!has_local_declaration(unit, symbol))
            continue;
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "(void)%s;\n", f2c_symbol_c_name(unit, symbol));
        if (!symbol->allocatable && !symbol->pointer)
            continue;
        if (symbol->deferred_character) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "(void)f2c_char_len_%s;\n", f2c_symbol_c_name(unit, symbol));
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "(void)%s_lower_%zu;\n", f2c_symbol_c_name(unit, symbol),
                              dimension + 1U);
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "(void)%s_extent_%zu;\n", f2c_symbol_c_name(unit, symbol),
                              dimension + 1U);
            if (symbol->pointer) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "(void)%s_stride_%zu;\n", f2c_symbol_c_name(unit, symbol),
                                  dimension + 1U);
            }
        }
    }
}

static char *restricted_body_name(const Unit *unit) {
    Buffer result = {0};
    f2c_buffer_printf(&result, "f2c_restricted_body_%s", unit->name);
    return f2c_buffer_take(&result);
}

static int needs_stable_procedure_boundary(const Unit *unit) {
    /* GCC's LTO cost model already keeps large numerical kernels out of their
     * callers.  Preserve a boundary for smaller loop-bearing procedures whose
     * full duplication would otherwise inflate every generated caller. */
    const size_t automatic_inline_statement_limit = 96U;
    size_t statement;
    if (unit->statement_count > automatic_inline_statement_limit)
        return 0;
    for (statement = 0U; statement < unit->statement_count; ++statement) {
        const F2cStatementKind kind = unit->statements[statement].kind;
        if (kind == F2C_STMT_DO || kind == F2C_STMT_DO_WHILE)
            return 1;
    }
    return 0;
}

static void emit_wrapper_arguments(Buffer *output, Unit *unit) {
    const int character_result = unit->kind == UNIT_FUNCTION &&
                                 unit->return_type == TYPE_CHARACTER &&
                                 !f2c_unit_has_allocatable_result(unit);
    size_t i;
    int emitted = 0;
    if (character_result) {
        f2c_buffer_append(output, "f2c_result, f2c_result_len");
        emitted = 1;
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        if (emitted)
            f2c_buffer_append(output, ", ");
        if (symbol != NULL && f2c_symbol_uses_descriptor(symbol))
            f2c_buffer_printf(output, "f2c_descriptor_%s", f2c_symbol_c_name(unit, symbol));
        else
            f2c_buffer_append(output, symbol != NULL ? f2c_symbol_c_name(unit, symbol)
                                                     : unit->arguments[i]);
        emitted = 1;
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        if (symbol == NULL || symbol->external || symbol->type != TYPE_CHARACTER ||
            f2c_symbol_uses_descriptor(symbol))
            continue;
        if (emitted)
            f2c_buffer_append(output, ", ");
        f2c_buffer_printf(output, "f2c_len_%s", f2c_symbol_c_name(unit, symbol));
        emitted = 1;
    }
}

static void emit_restricted_wrapper(Buffer *output, Unit *unit, const char *body_name) {
    const int returns_value =
        f2c_unit_has_allocatable_result(unit) ||
        (unit->kind == UNIT_FUNCTION && unit->return_type != TYPE_CHARACTER) ||
        (unit->kind == UNIT_SUBROUTINE && unit->alternate_return_count != 0U);
    f2c_buffer_append(output, "static ");
    f2c_unit_emit_named_signature(output, unit, body_name, 1);
    f2c_buffer_append(output, ";\n");
    if (needs_stable_procedure_boundary(unit))
        f2c_buffer_append(output, "F2C_NOINLINE ");
    f2c_unit_emit_signature(output, unit);
    f2c_buffer_append(output, " {\n    ");
    if (returns_value)
        f2c_buffer_append(output, "return ");
    f2c_buffer_printf(output, "%s(", body_name);
    emit_wrapper_arguments(output, unit);
    f2c_buffer_append(output, ");\n}\n");
}

void f2c_emit_unit(Context *context, Unit *unit) {
    size_t i;
    int depth = 1;
    char *body_name = NULL;
    if (unit->phase != F2C_UNIT_TYPED_IR) {
        f2c_diagnostic(context, context->lines.items[unit->begin].number, 1,
                       "internal compiler error: code generation received an untyped unit");
        return;
    }
    if (unit->kind == UNIT_BLOCK_DATA)
        return;
    f2c_unit_prepare_expression_temporaries(unit);
    prepare_statement_function_temporaries(unit);
    if (unit->kind == UNIT_PROGRAM) {
        f2c_unit_emit_signature(&context->output, unit);
    } else {
        body_name = restricted_body_name(unit);
        emit_restricted_wrapper(&context->output, unit, body_name);
        f2c_buffer_append(&context->output, "static ");
        f2c_unit_emit_named_signature(&context->output, unit, body_name, 1);
    }
    f2c_buffer_append(&context->output, " {\n");
    emit_declarations(context, unit);
    emit_statement_function_temporaries(&context->output, unit);
    f2c_unit_emit_expression_temporaries(&context->output, unit);
    emit_unused_suppression(&context->output, unit);
    f2c_emit_unit_data_initializers(context, unit, depth);
    for (i = unit->begin + 1U; i < unit->end; ++i) {
        const size_t statement_index = i - unit->begin - 1U;
        if (!f2c_unit_line_is_active(unit, &context->lines.items[i]))
            continue;
        if (unit->options.emit_source_comments &&
            !f2c_declaration_tokens(&context->lines.items[i])) {
            f2c_unit_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "/* Fortran %zu: %s */\n",
                              context->lines.items[i].number, context->lines.items[i].text);
        }
        if (statement_index < unit->statement_count)
            (void)f2c_emit_statement(context, unit, &unit->statements[statement_index],
                                     &context->lines.items[i], &depth);
    }
    while (depth > 1) {
        --depth;
        f2c_unit_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_emit_unit_cleanup(&context->output, unit, 1);
    if (unit->kind == UNIT_PROGRAM) {
        f2c_buffer_append(&context->output, "    return 0;\n");
    } else if (unit->kind == UNIT_SUBROUTINE && unit->alternate_return_count != 0U) {
        f2c_buffer_append(&context->output, "    return 0;\n");
    } else if (f2c_unit_has_allocatable_result(unit)) {
        f2c_buffer_append(&context->output, "    return f2c_result_descriptor;\n");
    } else if (unit->kind == UNIT_FUNCTION && unit->return_type != TYPE_CHARACTER) {
        f2c_buffer_append(&context->output, "    return f2c_result;\n");
    }
    f2c_buffer_append(&context->output, "}\n\n");
    free(body_name);
}
