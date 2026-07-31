#include "semantic/data_flow.h"

#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

typedef struct VariableFlowBuilder {
    Unit *unit;
    F2cVariableFlow *flow;
    size_t node;
} VariableFlowBuilder;

static size_t symbol_index(const Unit *unit, const Symbol *symbol) {
    size_t index;
    if (unit == NULL || symbol == NULL)
        return SIZE_MAX;
    for (index = 0U; index < unit->symbol_count; ++index)
        if (&unit->symbols[index] == symbol)
            return index;
    return SIZE_MAX;
}

static uint64_t *node_words(uint64_t *storage, const F2cVariableFlow *flow, size_t node) {
    return storage != NULL && flow != NULL && node < flow->node_count
               ? storage + node * flow->word_count
               : NULL;
}

static void mark_symbol(VariableFlowBuilder *builder, uint64_t *storage, const Symbol *symbol) {
    const size_t index = symbol_index(builder->unit, symbol);
    uint64_t *words;
    if (index == SIZE_MAX || (symbol->external && !symbol->procedure_pointer) ||
        symbol->statement_function)
        return;
    words = node_words(storage, builder->flow, builder->node);
    if (words != NULL)
        words[index / 64U] |= UINT64_C(1) << (index % 64U);
}

static const F2cExpr *argument_value(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   expression->child_count == 1U
               ? expression->children[0]
               : expression;
}

static const Symbol *designator_symbol(const Unit *unit, const F2cExpr *expression) {
    size_t child;
    expression = argument_value(expression);
    if (expression == NULL)
        return NULL;
    if (symbol_index(unit, expression->symbol) != SIZE_MAX)
        return expression->symbol;
    for (child = 0U; child < expression->child_count; ++child) {
        const Symbol *symbol = designator_symbol(unit, expression->children[child]);
        if (symbol != NULL)
            return symbol;
    }
    return NULL;
}

static F2cIntent definition_intent(const Unit *definition, size_t parameter) {
    Symbol *dummy;
    if (definition == NULL || parameter >= definition->argument_count)
        return F2C_INTENT_UNSPECIFIED;
    dummy = f2c_find_symbol((Unit *)definition, definition->arguments[parameter]);
    return dummy != NULL ? dummy->intent : F2C_INTENT_UNSPECIFIED;
}

static F2cIntent signature_intent(const Symbol *procedure, size_t parameter) {
    return procedure != NULL && parameter < procedure->external_parameter_count
               ? procedure->external_parameter_intents[parameter]
               : F2C_INTENT_UNSPECIFIED;
}

static void mark_value(VariableFlowBuilder *builder, const F2cExpr *expression);

static void mark_definition(VariableFlowBuilder *builder, const F2cExpr *expression,
                            int preserve_value) {
    const F2cExpr *value = argument_value(expression);
    const Symbol *symbol = designator_symbol(builder->unit, value);
    const int whole_name = value != NULL && value->kind == F2C_EXPR_NAME && value->symbol == symbol;
    if (preserve_value || !whole_name)
        mark_value(builder, value);
    mark_symbol(builder, builder->flow->definitions, symbol);
}

static void mark_actual(VariableFlowBuilder *builder, const F2cExpr *actual, F2cIntent intent) {
    actual = argument_value(actual);
    if (actual == NULL || actual->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return;
    if (intent == F2C_INTENT_IN) {
        mark_value(builder, actual);
    } else if (intent == F2C_INTENT_OUT) {
        mark_definition(builder, actual, 0);
    } else if (actual->definable || designator_symbol(builder->unit, actual) != NULL) {
        mark_definition(builder, actual, 1);
    } else {
        mark_value(builder, actual);
    }
}

static size_t type_bound_parameter(const Symbol *procedure, size_t child) {
    size_t parameter = 0U;
    size_t explicit_child;
    if (procedure == NULL || !procedure->type_bound)
        return child;
    if (child == 0U)
        return procedure->type_bound_nopass ? SIZE_MAX : procedure->type_bound_pass_index;
    for (explicit_child = 1U; explicit_child < child; ++explicit_child) {
        ++parameter;
        if (!procedure->type_bound_nopass && parameter == procedure->type_bound_pass_index)
            ++parameter;
    }
    if (!procedure->type_bound_nopass && parameter == procedure->type_bound_pass_index)
        ++parameter;
    return parameter;
}

static void mark_call(VariableFlowBuilder *builder, const F2cExpr *expression) {
    const Symbol *procedure = expression->symbol;
    size_t child;
    if (procedure != NULL && procedure->procedure_pointer)
        mark_symbol(builder, builder->flow->uses, procedure);
    for (child = 0U; child < expression->child_count; ++child) {
        const size_t parameter = type_bound_parameter(procedure, child);
        F2cIntent intent;
        if (parameter == SIZE_MAX) {
            mark_value(builder, expression->children[child]);
            continue;
        }
        intent = expression->resolved_procedure != NULL && !procedure
                     ? definition_intent(expression->resolved_procedure, parameter)
                     : signature_intent(procedure, parameter);
        if (expression->resolved_procedure != NULL && (procedure == NULL || !procedure->type_bound))
            intent = definition_intent(expression->resolved_procedure, parameter);
        if (expression->resolved_procedure == NULL && procedure == NULL)
            intent = F2C_INTENT_IN;
        mark_actual(builder, expression->children[child], intent);
    }
}

static void mark_value(VariableFlowBuilder *builder, const F2cExpr *expression) {
    size_t child;
    if (expression == NULL || expression->kind == F2C_EXPR_ABSENT_ARGUMENT)
        return;
    if (expression->kind == F2C_EXPR_CALL) {
        mark_call(builder, expression);
        return;
    }
    mark_symbol(builder, builder->flow->uses, expression->symbol);
    for (child = 0U; child < expression->child_count; ++child)
        mark_value(builder, expression->children[child]);
}

static int output_control(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_IOSTAT || kind == F2C_IO_CONTROL_IOMSG ||
           kind == F2C_IO_CONTROL_SIZE || kind == F2C_IO_CONTROL_NEWUNIT ||
           kind == F2C_IO_CONTROL_IOLENGTH || kind == F2C_IO_CONTROL_EXIST ||
           kind == F2C_IO_CONTROL_OPENED || kind == F2C_IO_CONTROL_NUMBER ||
           kind == F2C_IO_CONTROL_NAMED || kind == F2C_IO_CONTROL_NAME ||
           kind == F2C_IO_CONTROL_SEQUENTIAL || kind == F2C_IO_CONTROL_DIRECT ||
           kind == F2C_IO_CONTROL_FORMATTED || kind == F2C_IO_CONTROL_UNFORMATTED ||
           kind == F2C_IO_CONTROL_NEXTREC || kind == F2C_IO_CONTROL_POSITION ||
           kind == F2C_IO_CONTROL_READ || kind == F2C_IO_CONTROL_WRITE ||
           kind == F2C_IO_CONTROL_READWRITE;
}

static int allocation_output(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
           argument->text != NULL &&
           (strcmp(argument->text, "stat") == 0 || strcmp(argument->text, "errmsg") == 0);
}

static void mark_io_item(VariableFlowBuilder *builder, const F2cIoItem *item, int input) {
    size_t child;
    if (item == NULL)
        return;
    if (item->implied_do) {
        mark_value(builder, item->initial);
        mark_value(builder, item->limit);
        mark_value(builder, item->step);
        mark_definition(builder, item->iterator, 1);
    } else if (input) {
        /*
         * An input transfer may fail after defining only a prefix of the
         * input list. Preserve the prior value in the conservative summary
         * used on END/EOR/ERR successors.
         */
        mark_definition(builder, item->expression, 1);
    } else {
        mark_value(builder, item->expression);
    }
    for (child = 0U; child < item->child_count; ++child)
        mark_io_item(builder, &item->children[child], input);
}

static F2cIntent statement_call_intent(const F2cStatement *statement, const Symbol *procedure,
                                       size_t argument) {
    if (statement->resolved_procedure != NULL)
        return definition_intent(statement->resolved_procedure, argument);
    return signature_intent(procedure, argument);
}

static void mark_statement_call(VariableFlowBuilder *builder, const F2cStatement *statement) {
    const Symbol *procedure = statement->expression != NULL
                                  ? statement->expression->symbol
                                  : f2c_find_symbol(builder->unit, statement->name);
    size_t argument;
    if (statement->expression != NULL) {
        mark_value(builder, statement->expression);
        if (procedure != NULL && procedure->type_bound && !procedure->type_bound_nopass &&
            statement->expression->child_count != 0U)
            mark_actual(builder, statement->expression->children[0],
                        signature_intent(procedure, procedure->type_bound_pass_index));
    }
    for (argument = 0U; argument < statement->item_count; ++argument) {
        size_t parameter = argument;
        const F2cExpr *actual =
            statement->arguments != NULL ? statement->arguments[argument] : NULL;
        if (procedure != NULL && procedure->type_bound && !procedure->type_bound_nopass &&
            parameter >= procedure->type_bound_pass_index)
            ++parameter;
        mark_actual(builder, actual, statement_call_intent(statement, procedure, parameter));
    }
}

static void mark_statement(VariableFlowBuilder *builder, const F2cStatement *statement) {
    size_t index;
    if (statement == NULL)
        return;
    if (statement->kind == F2C_STMT_BLOCK_SCOPE) {
        for (index = 0U; index < builder->unit->symbol_count; ++index) {
            Symbol *symbol = &builder->unit->symbols[index];
            if (symbol->scope_begin_line == statement->line)
                mark_symbol(builder, builder->flow->definitions, symbol);
        }
    } else if (statement->kind == F2C_STMT_END_BLOCK_SCOPE) {
        for (index = builder->unit->symbol_count; index != 0U; --index) {
            Symbol *symbol = &builder->unit->symbols[index - 1U];
            if (symbol->scope_end_line == statement->line) {
                mark_symbol(builder, builder->flow->uses, symbol);
                mark_symbol(builder, builder->flow->definitions, symbol);
            }
        }
    } else if (statement->kind == F2C_STMT_ASSIGNMENT ||
               statement->kind == F2C_STMT_POINTER_ASSIGNMENT) {
        if (statement->resolved_procedure != NULL) {
            mark_actual(builder, statement->left,
                        definition_intent(statement->resolved_procedure, 0U));
            mark_actual(builder, statement->right,
                        definition_intent(statement->resolved_procedure, 1U));
        } else {
            mark_definition(builder, statement->left, 0);
            mark_value(builder, statement->right);
        }
    } else if (statement->kind == F2C_STMT_DO) {
        mark_definition(builder, statement->left, 0);
        mark_value(builder, statement->right);
        mark_value(builder, statement->limit);
        mark_value(builder, statement->step);
    } else if (statement->kind == F2C_STMT_CALL) {
        mark_statement_call(builder, statement);
    } else if (statement->kind == F2C_STMT_ASSIGN_LABEL) {
        mark_definition(builder, statement->expression, 0);
    } else if (statement->kind == F2C_STMT_ALLOCATE || statement->kind == F2C_STMT_DEALLOCATE) {
        for (index = 0U; index < statement->item_count; ++index) {
            const F2cExpr *argument =
                statement->arguments != NULL ? statement->arguments[index] : NULL;
            if (allocation_output(argument))
                mark_definition(builder, argument, 0);
            else if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT)
                mark_value(builder, argument);
            else
                mark_definition(builder, argument, 1);
        }
    } else if (statement->kind == F2C_STMT_MOVE_ALLOC) {
        for (index = 0U; index < statement->item_count; ++index)
            mark_definition(builder,
                            statement->arguments != NULL ? statement->arguments[index] : NULL,
                            index < 2U);
    } else if (statement->kind == F2C_STMT_NULLIFY) {
        for (index = 0U; index < statement->item_count; ++index)
            mark_definition(builder,
                            statement->arguments != NULL ? statement->arguments[index] : NULL, 0);
    } else {
        mark_value(builder, statement->expression);
        mark_value(builder, statement->left);
        mark_value(builder, statement->right);
        mark_value(builder, statement->limit);
        mark_value(builder, statement->step);
        for (index = 0U; index < statement->item_count; ++index)
            mark_value(builder, statement->arguments != NULL ? statement->arguments[index] : NULL);
    }
    mark_value(builder, statement->allocation_character_length);
    for (index = 0U; index < statement->control_count; ++index) {
        const F2cIoControl *control = &statement->io_controls[index];
        if (output_control(control->kind))
            mark_definition(builder, control->value, 1);
        else
            mark_value(builder, control->value);
    }
    for (index = 0U; index < statement->io_item_count; ++index)
        mark_io_item(builder, &statement->io_items[index], statement->kind == F2C_STMT_READ);
    for (index = 0U; index < statement->case_range_count; ++index) {
        mark_value(builder, statement->case_ranges[index].lower);
        mark_value(builder, statement->case_ranges[index].upper);
    }
    mark_statement(builder, statement->nested);
}

static int liveness_transfer(void *user, size_t node, const F2cBitFlowState *output,
                             F2cBitFlowState *input) {
    const F2cVariableFlow *flow = (const F2cVariableFlow *)user;
    const uint64_t *uses = node_words(flow->uses, flow, node);
    const uint64_t *definitions = node_words(flow->definitions, flow, node);
    size_t word;
    (void)output;
    for (word = 0U; word < flow->word_count; ++word)
        input->bits[word] = uses[word] | (input->bits[word] & ~definitions[word]);
    return 1;
}

void f2c_variable_flow_clear(Unit *unit) {
    F2cVariableFlow *flow;
    if (unit == NULL)
        return;
    flow = &unit->variable_flow;
    free(flow->uses);
    free(flow->definitions);
    free(flow->live_in);
    free(flow->live_out);
    memset(flow, 0, sizeof(*flow));
}

static int allocate_flow(Context *context, Unit *unit, size_t node_count) {
    F2cVariableFlow *flow = &unit->variable_flow;
    size_t cell_count;
    if (unit->symbol_count > SIZE_MAX - 63U)
        goto failed;
    flow->word_count = (unit->symbol_count + 63U) / 64U;
    flow->node_count = node_count;
    if (flow->word_count == 0U)
        return 1;
    if (node_count > SIZE_MAX / flow->word_count)
        goto failed;
    cell_count = node_count * flow->word_count;
    if (cell_count > SIZE_MAX / sizeof(*flow->uses))
        goto failed;
    flow->uses = (uint64_t *)calloc(cell_count, sizeof(*flow->uses));
    flow->definitions = (uint64_t *)calloc(cell_count, sizeof(*flow->definitions));
    flow->live_in = (uint64_t *)calloc(cell_count, sizeof(*flow->live_in));
    flow->live_out = (uint64_t *)calloc(cell_count, sizeof(*flow->live_out));
    if (flow->uses != NULL && flow->definitions != NULL && flow->live_in != NULL &&
        flow->live_out != NULL)
        return 1;
failed:
    if (context != NULL)
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &unit->header_span, 1,
                                 "out of memory analyzing ordinary-variable data flow");
    return 0;
}

int f2c_variable_flow_analyze(Context *context, Unit *unit, const F2cControlFlowGraph *graph) {
    F2cBitFlowResult result;
    F2cVariableFlow *flow;
    VariableFlowBuilder builder;
    size_t node;
    size_t cells;
    if (unit == NULL || graph == NULL || graph->node_count == 0U)
        return 0;
    f2c_variable_flow_clear(unit);
    if (!allocate_flow(context, unit, graph->node_count)) {
        f2c_variable_flow_clear(unit);
        return 0;
    }
    flow = &unit->variable_flow;
    builder.unit = unit;
    builder.flow = flow;
    for (node = 0U; node < graph->node_count; ++node) {
        const F2cControlFlowNode *flow_node = &graph->nodes[node];
        builder.node = node;
        if (flow_node->kind == F2C_CFG_NODE_STATEMENT &&
            flow_node->statement_index < unit->statement_count) {
            mark_statement(&builder, &unit->statements[flow_node->statement_index]);
        } else if (flow_node->kind == F2C_CFG_NODE_LOOP_LATCH &&
                   flow_node->statement_index < unit->statement_count) {
            mark_definition(&builder, unit->statements[flow_node->statement_index].left, 1);
        }
    }
    memset(&result, 0, sizeof(result));
    if (!f2c_bit_flow_solve_backward(graph, flow->word_count, NULL, 0U, liveness_transfer, NULL,
                                     flow, &result)) {
        if (context != NULL)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, &unit->header_span, 1,
                                     "ordinary-variable liveness did not converge");
        f2c_variable_flow_clear(unit);
        return 0;
    }
    cells = flow->node_count * flow->word_count;
    if (cells != 0U)
        memcpy(flow->live_out, result.storage, cells * sizeof(*flow->live_out));
    for (node = 0U; node < flow->node_count; ++node) {
        F2cBitFlowState output = {node_words(flow->live_out, flow, node), 0U, 1};
        F2cBitFlowState input = {node_words(flow->live_in, flow, node), 0U, 1};
        if (flow->word_count != 0U)
            memcpy(input.bits, output.bits, flow->word_count * sizeof(*input.bits));
        (void)liveness_transfer(flow, node, &output, &input);
    }
    f2c_bit_flow_free(&result);
    flow->analyzed = 1;
    return 1;
}

static int flow_contains(const Unit *unit, const uint64_t *storage, size_t node,
                         const Symbol *symbol) {
    const F2cVariableFlow *flow;
    const size_t index = symbol_index(unit, symbol);
    const uint64_t *words;
    if (unit == NULL || index == SIZE_MAX)
        return 0;
    flow = &unit->variable_flow;
    if (!flow->analyzed || node >= flow->node_count)
        return 0;
    words = node_words((uint64_t *)storage, flow, node);
    return words != NULL && (words[index / 64U] & (UINT64_C(1) << (index % 64U))) != 0U;
}

int f2c_variable_flow_is_used(const Unit *unit, size_t node, const Symbol *symbol) {
    return unit != NULL && flow_contains(unit, unit->variable_flow.uses, node, symbol);
}

int f2c_variable_flow_is_defined(const Unit *unit, size_t node, const Symbol *symbol) {
    return unit != NULL && flow_contains(unit, unit->variable_flow.definitions, node, symbol);
}

int f2c_variable_flow_is_live_in(const Unit *unit, size_t node, const Symbol *symbol) {
    return unit != NULL && flow_contains(unit, unit->variable_flow.live_in, node, symbol);
}

int f2c_variable_flow_is_live_out(const Unit *unit, size_t node, const Symbol *symbol) {
    return unit != NULL && flow_contains(unit, unit->variable_flow.live_out, node, symbol);
}
