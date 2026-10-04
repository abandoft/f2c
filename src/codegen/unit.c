#include "codegen/descriptor/private.h"
#include "codegen/names.h"
#include "codegen/storage/private.h"
#include "codegen/unit/private.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_local_declaration(Unit *unit, Symbol *symbol) {
    return !symbol->argument && !symbol->parameter && !symbol->external &&
           symbol->intrinsic == NULL && !symbol->module_entity && !symbol->host_associated &&
           symbol->common_block == NULL && !symbol->equivalence_associated &&
           symbol->alias_to == NULL && !symbol->statement_function &&
           !(unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
             strcmp(symbol->name, unit->result_name) == 0);
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
        if (!f2c_statement_is_function_definition(unit, statement))
            f2c_visit_statement_expressions(&unit->statements[statement],
                                            emit_statement_function_temporary, &emitter);
}

typedef struct CharacterTemporaryCleanupEmitter {
    Buffer *output;
    int depth;
} CharacterTemporaryCleanupEmitter;

static void emit_character_temporary_cleanup(F2cExpr *expression, void *state) {
    CharacterTemporaryCleanupEmitter *emitter = (CharacterTemporaryCleanupEmitter *)state;
    if (!f2c_expression_is_character_temporary(expression))
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
        if (!f2c_statement_is_function_definition(unit, i))
            f2c_visit_statement_expressions(&unit->statements[i], emit_character_temporary_cleanup,
                                            &emitter);
    if (function_result != NULL && f2c_unit_has_descriptor_result(unit)) {
        const char *name = f2c_symbol_c_name(unit, function_result);
        size_t dimension;
        char *character_length = function_result->type == TYPE_CHARACTER
                                     ? f2c_symbol_character_length(unit, function_result)
                                     : NULL;
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(
            output, "f2c_result_descriptor.%s = %s;\n",
            f2c_descriptor_address_member(f2c_symbol_storage_qualifiers(function_result), 0), name);
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.storage_qualifiers = %uU;\n",
                          f2c_symbol_storage_qualifiers(function_result));
        f2c_unit_indent(output, depth);
        if (function_result->pointer)
            f2c_buffer_printf(output, "f2c_result_descriptor.deallocatable = %s_deallocatable;\n",
                              name);
        else
            f2c_buffer_append(output, "f2c_result_descriptor.deallocatable = true;\n");
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.element_size = sizeof(%s);\n",
                          f2c_symbol_c_type(function_result));
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.rank = %zuU;\n", function_result->rank);
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_result_descriptor.character_length = (size_t)(%s);\n",
                          character_length != NULL ? character_length : "0U");
        for (dimension = 0U; dimension < function_result->rank; ++dimension) {
            char *lower = function_result->allocatable || function_result->pointer
                              ? f2c_symbol_dimension_lower(unit, function_result, dimension)
                              : f2c_strdup("1");
            char *extent = f2c_symbol_dimension_extent(unit, function_result, dimension);
            f2c_unit_indent(output, depth);
            f2c_buffer_printf(output, "f2c_result_descriptor.lower[%zu] = %s;\n", dimension,
                              lower != NULL ? lower : "1");
            f2c_unit_indent(output, depth);
            f2c_buffer_printf(output, "f2c_result_descriptor.extent[%zu] = (int64_t)(%s);\n",
                              dimension, extent != NULL ? extent : "0");
            f2c_unit_indent(output, depth);
            if (function_result->pointer)
                f2c_buffer_printf(output, "f2c_result_descriptor.stride[%zu] = %s_stride_%zu;\n",
                                  dimension, name, dimension + 1U);
            else if (dimension == 0U)
                f2c_buffer_append(output, "f2c_result_descriptor.stride[0] = 1;\n");
            else
                f2c_buffer_printf(
                    output,
                    "f2c_result_descriptor.stride[%zu] = f2c_descriptor_stride_extent("
                    "f2c_result_descriptor.stride[%zu], "
                    "(size_t)f2c_result_descriptor.extent[%zu]);\n",
                    dimension, dimension - 1U, dimension - 1U);
            free(lower);
            free(extent);
        }
        free(character_length);
    }
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        if (symbol->host_associated && !symbol->host_capture)
            continue;
        if (symbol == function_result || symbol->external || symbol->intrinsic != NULL)
            continue;
        if ((symbol->allocatable || symbol->pointer) && symbol->argument) {
            /* Association/allocation changes are committed at their typed
             * mutation sites. Never copy an entry snapshot over live state. */
            continue;
        }
        if (f2c_symbol_is_automatic_array(unit, symbol)) {
            f2c_unit_emit_automatic_array_cleanup(output, unit, symbol, depth);
            continue;
        }
        if (symbol->allocatable && !symbol->argument && !unit->save_all && !symbol->saved &&
            symbol->initializer == NULL) {
            f2c_unit_indent(output, depth);
            if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
                char *count = f2c_symbol_element_count(unit, symbol);
                f2c_buffer_printf(output,
                                  "if (%s != NULL) %s_%s(%s, (size_t)(%s), "
                                  "%zuU);\n",
                                  f2c_symbol_c_name(unit, symbol),
                                  symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                                  symbol->derived_type->c_name, f2c_symbol_c_name(unit, symbol),
                                  count != NULL ? count : "0U", symbol->rank);
                f2c_unit_indent(output, depth);
                free(count);
            }
            f2c_buffer_printf(output, "free(%s);\n", f2c_symbol_c_name(unit, symbol));
            continue;
        }
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL && !symbol->argument &&
            !symbol->pointer && !symbol->module_entity && !unit->save_all && !symbol->saved &&
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
    f2c_unit_emit_value_cleanup(output, unit, depth);
}

static void emit_unused_suppression(Buffer *output, Unit *unit) {
    size_t i;
    if (unit->kind == UNIT_FUNCTION && unit->return_type == TYPE_CHARACTER &&
        !f2c_unit_has_descriptor_result(unit)) {
        f2c_unit_indent(output, 1);
        f2c_buffer_append(output, "(void)f2c_result;\n");
        f2c_unit_indent(output, 1);
        f2c_buffer_append(output, "(void)f2c_result_len;\n");
    }
    for (i = 0U; i < unit->argument_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[i]);
        if (symbol != NULL && (symbol->pointer || symbol->allocatable) &&
            f2c_symbol_uses_descriptor(symbol)) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "(void)f2c_descriptor_%s;\n",
                              f2c_symbol_c_name(unit, symbol));
            continue;
        }
        f2c_unit_indent(output, 1);
        f2c_buffer_printf(output, "(void)%s;\n",
                          symbol != NULL ? f2c_symbol_c_name(unit, symbol) : unit->arguments[i]);
        if (symbol != NULL && f2c_symbol_uses_descriptor(symbol)) {
            size_t dimension;
            if (symbol->pointer) {
                f2c_unit_indent(output, 1);
                f2c_buffer_printf(output, "(void)%s_deallocatable;\n",
                                  f2c_symbol_c_name(unit, symbol));
            }
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
        if (symbol->pointer) {
            f2c_unit_indent(output, 1);
            f2c_buffer_printf(output, "(void)%s_deallocatable;\n", f2c_symbol_c_name(unit, symbol));
        }
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
                                 !f2c_unit_has_descriptor_result(unit);
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
        f2c_unit_has_descriptor_result(unit) ||
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
    if (!unit->expression_lifetimes_analyzed) {
        f2c_diagnostic_code(
            context, F2C_DIAGNOSTIC_INTERNAL, context->lines.items[unit->begin].number, 1,
            "code generation rejected typed IR without semantic temporary-lifetime planning");
        return;
    }
    if (!unit->temporary_flow.analyzed) {
        f2c_diagnostic_code(
            context, F2C_DIAGNOSTIC_INTERNAL, context->lines.items[unit->begin].number, 1,
            "code generation rejected typed IR without owned-temporary data-flow analysis");
        return;
    }
    if (unit->kind == UNIT_PROGRAM) {
        f2c_unit_emit_signature(&context->output, unit);
    } else {
        body_name = restricted_body_name(unit);
        emit_restricted_wrapper(&context->output, unit, body_name);
        f2c_buffer_append(&context->output, "static ");
        f2c_unit_emit_named_signature(&context->output, unit, body_name, 1);
    }
    f2c_buffer_append(&context->output, " {\n");
    f2c_unit_emit_declarations(context, unit);
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
    } else if (f2c_unit_has_descriptor_result(unit)) {
        f2c_buffer_append(&context->output, "    return f2c_result_descriptor;\n");
    } else if (unit->kind == UNIT_FUNCTION && unit->return_type != TYPE_CHARACTER) {
        f2c_buffer_append(&context->output, "    return f2c_result;\n");
    }
    f2c_buffer_append(&context->output, "}\n\n");
    free(body_name);
}
