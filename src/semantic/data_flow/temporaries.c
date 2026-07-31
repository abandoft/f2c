#include "semantic/data_flow.h"

#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

static uint64_t *node_bits(uint64_t *storage, size_t node, size_t word_count) {
    return storage != NULL ? storage + node * word_count : NULL;
}

static const uint64_t *const_node_bits(const uint64_t *storage, size_t node, size_t word_count) {
    return storage != NULL ? storage + node * word_count : NULL;
}

static int bit_is_set(const uint64_t *bits, size_t index) {
    return bits != NULL && (bits[index / 64U] & (UINT64_C(1) << (index % 64U))) != 0U;
}

static void bit_set(uint64_t *bits, size_t index) {
    bits[index / 64U] |= UINT64_C(1) << (index % 64U);
}

void f2c_temporary_flow_clear(Unit *unit) {
    if (unit == NULL)
        return;
    free(unit->temporary_flow.created);
    free(unit->temporary_flow.released);
    free(unit->temporary_flow.live_in);
    free(unit->temporary_flow.live_out);
    memset(&unit->temporary_flow, 0, sizeof(unit->temporary_flow));
}

static int allocate_flow(Context *context, Unit *unit, const F2cControlFlowGraph *graph) {
    F2cTemporaryFlow *flow = &unit->temporary_flow;
    size_t storage_words;
    flow->node_count = graph->node_count;
    flow->word_count = (unit->owned_temporary_count + 63U) / 64U;
    if (flow->word_count == 0U)
        return 1;
    if (flow->node_count > SIZE_MAX / flow->word_count)
        goto failed;
    storage_words = flow->node_count * flow->word_count;
    if (storage_words > SIZE_MAX / sizeof(uint64_t))
        goto failed;
    flow->created = (uint64_t *)calloc(storage_words, sizeof(*flow->created));
    flow->released = (uint64_t *)calloc(storage_words, sizeof(*flow->released));
    flow->live_in = (uint64_t *)calloc(storage_words, sizeof(*flow->live_in));
    flow->live_out = (uint64_t *)calloc(storage_words, sizeof(*flow->live_out));
    if (flow->created != NULL && flow->released != NULL && flow->live_in != NULL &&
        flow->live_out != NULL)
        return 1;

failed:
    f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, unit->begin, 1,
                        "out of memory while analyzing owned temporary flow");
    return 0;
}

static int populate_transfer_sets(Context *context, Unit *unit, const F2cControlFlowGraph *graph) {
    F2cTemporaryFlow *flow = &unit->temporary_flow;
    size_t node;
    for (node = 0U; node < graph->node_count; ++node) {
        const size_t statement_index = graph->nodes[node].statement_index;
        const F2cStatementTemporaryPlan *plan;
        uint64_t *created;
        uint64_t *released;
        size_t item;
        if (statement_index >= unit->statement_count)
            continue;
        plan = &unit->statements[statement_index].temporary_plan;
        if (!plan->ownership_analyzed) {
            f2c_diagnostic_span_code(
                context, F2C_DIAGNOSTIC_INTERNAL, &unit->statements[statement_index].span, 1,
                "temporary data-flow analysis requires a typed ownership plan");
            return 0;
        }
        created = node_bits(flow->created, node, flow->word_count);
        released = node_bits(flow->released, node, flow->word_count);
        for (item = 0U; item < plan->owned_temporary_count; ++item) {
            const size_t temporary = plan->owned_temporaries[item];
            if (temporary >= unit->owned_temporary_count ||
                unit->owned_temporaries[temporary].owner_statement != statement_index) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL,
                                         &unit->statements[statement_index].span, 1,
                                         "typed temporary ownership catalog is inconsistent");
                return 0;
            }
            bit_set(created, temporary);
            bit_set(released, temporary);
        }
    }
    return 1;
}

static int solve_flow(Unit *unit, const F2cControlFlowGraph *graph) {
    F2cTemporaryFlow *flow = &unit->temporary_flow;
    int changed;
    do {
        size_t node;
        changed = 0;
        for (node = 0U; node < graph->node_count; ++node) {
            const F2cControlFlowNode *graph_node = &graph->nodes[node];
            uint64_t *live_in = node_bits(flow->live_in, node, flow->word_count);
            uint64_t *live_out = node_bits(flow->live_out, node, flow->word_count);
            const uint64_t *created = const_node_bits(flow->created, node, flow->word_count);
            const uint64_t *released = const_node_bits(flow->released, node, flow->word_count);
            size_t word;
            for (word = 0U; word < flow->word_count; ++word) {
                uint64_t input = 0U;
                size_t predecessor;
                for (predecessor = 0U; predecessor < graph_node->predecessor_count; ++predecessor) {
                    const size_t source = graph_node->predecessors[predecessor].target;
                    input |= const_node_bits(flow->live_out, source, flow->word_count)[word];
                }
                if (live_in[word] != input) {
                    live_in[word] = input;
                    changed = 1;
                }
                {
                    const uint64_t output = (input | created[word]) & ~released[word];
                    if (live_out[word] != output) {
                        live_out[word] = output;
                        changed = 1;
                    }
                }
            }
        }
    } while (changed);
    return 1;
}

static int validate_flow(Context *context, Unit *unit, const F2cControlFlowGraph *graph) {
    const F2cTemporaryFlow *flow = &unit->temporary_flow;
    size_t node;
    for (node = 0U; node < graph->node_count; ++node) {
        const uint64_t *created = const_node_bits(flow->created, node, flow->word_count);
        const uint64_t *released = const_node_bits(flow->released, node, flow->word_count);
        const uint64_t *live_in = const_node_bits(flow->live_in, node, flow->word_count);
        const uint64_t *live_out = const_node_bits(flow->live_out, node, flow->word_count);
        size_t temporary;
        for (temporary = 0U; temporary < unit->owned_temporary_count; ++temporary) {
            const F2cOwnedTemporary *owned = &unit->owned_temporaries[temporary];
            if (bit_is_set(created, temporary) && bit_is_set(live_in, temporary)) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, &owned->span, 1,
                                         "owned temporary %zu can be recreated while still live",
                                         temporary);
                return 0;
            }
            if (bit_is_set(released, temporary) && !bit_is_set(created, temporary) &&
                !bit_is_set(live_in, temporary)) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, &owned->span, 1,
                                         "owned temporary %zu can be released before creation",
                                         temporary);
                return 0;
            }
            if (bit_is_set(live_out, temporary)) {
                f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_INTERNAL, &owned->span, 1,
                                         "statement-owned temporary %zu escapes its CFG node",
                                         temporary);
                return 0;
            }
        }
    }
    return 1;
}

int f2c_temporary_flow_analyze(Context *context, Unit *unit, const F2cControlFlowGraph *graph) {
    if (context == NULL || unit == NULL || graph == NULL ||
        graph->statement_count != unit->statement_count || !unit->expression_lifetimes_analyzed)
        return 0;
    f2c_temporary_flow_clear(unit);
    if (!allocate_flow(context, unit, graph) || !populate_transfer_sets(context, unit, graph) ||
        !solve_flow(unit, graph) || !validate_flow(context, unit, graph)) {
        f2c_temporary_flow_clear(unit);
        return 0;
    }
    unit->temporary_flow.analyzed = 1;
    return 1;
}

static int flow_query(const Unit *unit, const uint64_t *storage, size_t node, size_t temporary) {
    const F2cTemporaryFlow *flow;
    if (unit == NULL)
        return 0;
    flow = &unit->temporary_flow;
    if (!flow->analyzed || node >= flow->node_count || temporary >= unit->owned_temporary_count ||
        flow->word_count == 0U)
        return 0;
    return bit_is_set(const_node_bits(storage, node, flow->word_count), temporary);
}

int f2c_temporary_flow_is_created(const Unit *unit, size_t node, size_t temporary) {
    return flow_query(unit, unit != NULL ? unit->temporary_flow.created : NULL, node, temporary);
}

int f2c_temporary_flow_is_released(const Unit *unit, size_t node, size_t temporary) {
    return flow_query(unit, unit != NULL ? unit->temporary_flow.released : NULL, node, temporary);
}

int f2c_temporary_flow_is_live_in(const Unit *unit, size_t node, size_t temporary) {
    return flow_query(unit, unit != NULL ? unit->temporary_flow.live_in : NULL, node, temporary);
}

int f2c_temporary_flow_is_live_out(const Unit *unit, size_t node, size_t temporary) {
    return flow_query(unit, unit != NULL ? unit->temporary_flow.live_out : NULL, node, temporary);
}
