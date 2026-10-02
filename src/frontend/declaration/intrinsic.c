#include "frontend/declaration/symbol.h"
#include "frontend/private.h"

#include "semantic/intrinsic.h"

void f2c_bind_intrinsic_declaration(Context *context, Unit *unit, const Line *line,
                                    const F2cToken *name) {
    Symbol *symbol = f2c_declaration_symbol(context, unit, line, name);
    const F2cIntrinsicSignature *signature;
    if (symbol == NULL)
        return;
    signature = f2c_find_intrinsic(symbol->name);
    if (signature == NULL) {
        const F2cIntrinsicSpecification *specification =
            f2c_find_intrinsic_specification(symbol->name);
        if (specification != NULL)
            signature = &specification->signature;
    }
    if (signature == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, name, 1,
                                  "unknown or unsupported INTRINSIC procedure '%s'", symbol->name);
        return;
    }
    if (symbol->intrinsic != NULL && symbol->association == F2C_ASSOCIATION_LOCAL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, name, 1,
                                  "duplicate INTRINSIC attribute for '%s'", symbol->name);
        return;
    }
    symbol->intrinsic = signature;
    symbol->intrinsic_span = name->span;
    symbol->value_category = F2C_VALUE_PROCEDURE;
}

void f2c_parse_intrinsic_declaration(Context *context, Unit *unit, const Line *line) {
    size_t index =
        line != NULL && line->token_count != 0U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U
                                                                                            : 0U;
    if (!f2c_line_token_equals(line, index, "intrinsic"))
        return;
    ++index;
    /* INTRINSIC is not a reserved word: intrinsic = ... is an assignment. */
    if (index < line->token_count && (line->tokens[index].kind == F2C_TOKEN_OPERATOR ||
                                      line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN))
        return;
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON)
        ++index;
    if (index == line->token_count) {
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_SYNTAX, line->number, 1,
                            "INTRINSIC declaration requires a procedure name");
        return;
    }
    for (;;) {
        if (line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[index], 1,
                                      "malformed INTRINSIC declaration entity");
            return;
        }
        f2c_bind_intrinsic_declaration(context, unit, line, &line->tokens[index++]);
        if (index == line->token_count)
            return;
        if (line->tokens[index].kind != F2C_TOKEN_COMMA || index + 1U == line->token_count) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[index], 1,
                                      "malformed INTRINSIC declaration entity list");
            return;
        }
        ++index;
    }
}

void f2c_validate_intrinsic_declarations(Context *context, Unit *unit) {
    size_t index;
    const Symbol *result_symbol = unit->kind == UNIT_FUNCTION && unit->result_name != NULL
                                      ? f2c_find_symbol(unit, unit->result_name)
                                      : NULL;
    for (index = 0U; index < unit->symbol_count; ++index) {
        Symbol *symbol = &unit->symbols[index];
        const F2cIntrinsicDescriptor *descriptor;
        if (symbol->intrinsic == NULL)
            continue;
        descriptor = f2c_intrinsic_descriptor(symbol->intrinsic->id);
        if (descriptor != NULL &&
            descriptor->procedure_kind == F2C_INTRINSIC_PROCEDURE_SUBROUTINE &&
            symbol->type != TYPE_UNKNOWN)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->intrinsic_span, 1,
                                     "INTRINSIC subroutine '%s' cannot have a declared result type",
                                     symbol->name);
        if (symbol->external || symbol->argument || symbol == result_symbol || symbol->rank != 0U ||
            symbol->parameter || symbol->allocatable || symbol->pointer || symbol->target ||
            symbol->saved || symbol->value || symbol->asynchronous || symbol->volatile_entity ||
            symbol->initializer != NULL || symbol->common_block != NULL ||
            symbol->equivalence_associated || symbol->procedure_pointer ||
            symbol->intent != F2C_INTENT_UNSPECIFIED) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &symbol->intrinsic_span, 1,
                                     "INTRINSIC procedure '%s' cannot have EXTERNAL, dummy/result, "
                                     "or data-object attributes",
                                     symbol->name);
        }
        symbol->value_category = F2C_VALUE_PROCEDURE;
    }
}
