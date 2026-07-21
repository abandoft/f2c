#include "semantic/semantic.h"

#include "internal/f2c.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct HostCaptureMarker {
    Unit *unit;
    int changed;
} HostCaptureMarker;

typedef struct ProcedureValueValidator {
    Context *context;
} ProcedureValueValidator;

static void mark_capture(Symbol *symbol, HostCaptureMarker *marker) {
    if (symbol == NULL || marker == NULL || !symbol->host_associated || symbol->host_capture)
        return;
    symbol->host_capture = 1;
    marker->changed = 1;
}

static Symbol *find_associated_symbol(Unit *unit, size_t host_symbol_index) {
    size_t symbol_index;
    if (unit == NULL)
        return NULL;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        Symbol *symbol = &unit->symbols[symbol_index];
        if (symbol->host_associated && symbol->host_symbol_index == host_symbol_index)
            return symbol;
    }
    return NULL;
}

static void inherit_captures(Unit *caller, const Unit *callee, HostCaptureMarker *marker) {
    size_t symbol_index;
    if (caller == NULL || callee == NULL || marker == NULL || !caller->internal ||
        !callee->internal || caller->host_index != callee->host_index)
        return;
    for (symbol_index = 0U; symbol_index < callee->symbol_count; ++symbol_index) {
        const Symbol *capture = &callee->symbols[symbol_index];
        if (capture->host_associated && capture->host_capture)
            mark_capture(find_associated_symbol(caller, capture->host_symbol_index), marker);
    }
}

static void mark_expression_capture(F2cExpr *expression, void *state) {
    HostCaptureMarker *marker = (HostCaptureMarker *)state;
    if (expression == NULL || marker == NULL)
        return;
    mark_capture(expression->symbol, marker);
    inherit_captures(marker->unit, expression->resolved_procedure, marker);
}

static void visit_symbol_expressions(Symbol *symbol, HostCaptureMarker *marker) {
    size_t dimension;
    if (symbol == NULL || marker == NULL)
        return;
    f2c_visit_expression(symbol->initializer_expression, mark_expression_capture, marker);
    f2c_visit_expression(symbol->character_length_expression, mark_expression_capture, marker);
    f2c_visit_expression(symbol->statement_function_expression, mark_expression_capture, marker);
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        f2c_visit_expression(symbol->dimensions[dimension].lower_expression,
                             mark_expression_capture, marker);
        f2c_visit_expression(symbol->dimensions[dimension].upper_expression,
                             mark_expression_capture, marker);
    }
}

static int mark_unit_captures(Unit *unit) {
    HostCaptureMarker marker;
    size_t index;
    if (unit == NULL || !unit->internal)
        return 0;
    marker.unit = unit;
    marker.changed = 0;
    for (index = 0U; index < unit->symbol_count; ++index) {
        Symbol *symbol = &unit->symbols[index];
        if (!symbol->host_associated || symbol->host_capture)
            visit_symbol_expressions(symbol, &marker);
    }
    for (index = 0U; index < unit->statement_count; ++index) {
        F2cStatement *statement = &unit->statements[index];
        f2c_visit_statement_expressions(statement, mark_expression_capture, &marker);
        inherit_captures(unit, statement->resolved_procedure, &marker);
    }
    return marker.changed;
}

static const Symbol *host_symbol(const Context *context, const Unit *unit, const Symbol *capture) {
    const Unit *host;
    if (context == NULL || unit == NULL || capture == NULL || !unit->internal ||
        unit->host_index >= context->units.count)
        return NULL;
    host = &context->units.items[unit->host_index];
    return capture->host_symbol_index < host->symbol_count
               ? &host->symbols[capture->host_symbol_index]
               : NULL;
}

static int validate_capture(Context *context, Unit *unit, Symbol *capture) {
    const Symbol *source = host_symbol(context, unit, capture);
    const char *name = source != NULL && source->name != NULL ? source->name : capture->name;
    size_t line = capture->declaration_line;
    if (line == 0U)
        line = context->lines.items[unit->begin].number;
    if (source == NULL) {
        f2c_diagnostic(context, line, 1,
                       "host-associated entity '%s' has no matching host declaration", name);
        return 0;
    }
    if (capture->procedure_pointer) {
        f2c_diagnostic(context, line, 1,
                       "host association of procedure pointer '%s' requires closure-aware "
                       "procedure values",
                       name);
        return 0;
    }
    if (f2c_symbol_uses_descriptor(source) && !source->argument) {
        f2c_diagnostic(context, line, 1,
                       "host association of local dynamic descriptor '%s' is not yet supported",
                       name);
        return 0;
    }
    return 1;
}

static int append_capture_arguments(Context *context, Unit *unit) {
    char **arguments;
    F2cSourceSpan *spans;
    size_t capture_count = 0U;
    size_t capture_index = 0U;
    size_t symbol_index;
    size_t total;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        Symbol *symbol = &unit->symbols[symbol_index];
        if (!symbol->host_associated)
            continue;
        symbol->argument = symbol->host_capture;
        if (!symbol->host_capture)
            continue;
        (void)validate_capture(context, unit, symbol);
        ++capture_count;
    }
    unit->host_capture_begin = unit->argument_count;
    unit->host_capture_count = capture_count;
    if (capture_count == 0U)
        return 1;
    if (unit->argument_count > SIZE_MAX - capture_count)
        return 0;
    total = unit->argument_count + capture_count;
    if (total > SIZE_MAX / sizeof(*arguments) || total > SIZE_MAX / sizeof(*spans))
        return 0;
    arguments = (char **)calloc(total, sizeof(*arguments));
    spans = (F2cSourceSpan *)calloc(total, sizeof(*spans));
    if (arguments == NULL || spans == NULL) {
        free(arguments);
        free(spans);
        return 0;
    }
    if (unit->argument_count != 0U) {
        memcpy(arguments, unit->arguments, unit->argument_count * sizeof(*arguments));
        if (unit->argument_spans != NULL)
            memcpy(spans, unit->argument_spans, unit->argument_count * sizeof(*spans));
    }
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        Symbol *symbol = &unit->symbols[symbol_index];
        size_t destination;
        if (!symbol->host_associated || !symbol->host_capture)
            continue;
        destination = unit->argument_count + capture_index;
        symbol->argument = 1;
        arguments[destination] = f2c_strdup(symbol->name);
        spans[destination] = symbol->declaration_span;
        if (arguments[destination] == NULL) {
            size_t added;
            for (added = 0U; added < capture_index; ++added)
                free(arguments[unit->argument_count + added]);
            free(arguments);
            free(spans);
            return 0;
        }
        ++capture_index;
    }
    free(unit->arguments);
    free(unit->argument_spans);
    unit->arguments = arguments;
    unit->argument_spans = spans;
    unit->argument_count = total;
    return 1;
}

static void validate_procedure_value(F2cExpr *expression, void *state) {
    ProcedureValueValidator *validator = (ProcedureValueValidator *)state;
    const Unit *procedure;
    const char *name;
    if (expression == NULL || validator == NULL || expression->kind != F2C_EXPR_NAME ||
        expression->symbol == NULL)
        return;
    procedure = expression->symbol->procedure_interface;
    if (procedure == NULL || !procedure->internal || procedure->host_capture_count == 0U)
        return;
    name = procedure->fortran_name != NULL ? procedure->fortran_name : procedure->name;
    f2c_diagnostic_span_code(
        validator->context, F2C_DIAGNOSTIC_UNSUPPORTED, &expression->span, 1,
        "internal procedure '%s' captures host entities and cannot be used as a procedure value "
        "until closure-aware procedure values are implemented",
        name);
}

static void validate_unit_procedure_values(Context *context, Unit *unit) {
    ProcedureValueValidator validator;
    size_t statement_index;
    validator.context = context;
    for (statement_index = 0U; statement_index < unit->statement_count; ++statement_index) {
        f2c_visit_statement_expressions(&unit->statements[statement_index],
                                        validate_procedure_value, &validator);
    }
}

int f2c_finalize_host_association(Context *context) {
    int changed;
    size_t unit_index;
    if (context == NULL)
        return 0;
    do {
        changed = 0;
        for (unit_index = 0U; unit_index < context->units.count; ++unit_index)
            changed |= mark_unit_captures(&context->units.items[unit_index]);
    } while (changed);
    for (unit_index = 0U; unit_index < context->units.count; ++unit_index) {
        Unit *unit = &context->units.items[unit_index];
        if (unit->internal && !append_capture_arguments(context, unit))
            return 0;
    }
    for (unit_index = 0U; unit_index < context->units.count; ++unit_index)
        validate_unit_procedure_values(context, &context->units.items[unit_index]);
    return 1;
}
