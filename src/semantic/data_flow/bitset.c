#include "semantic/data_flow.h"

#include <stdlib.h>
#include <string.h>

static void copy_bits(uint64_t *target, const uint64_t *source, size_t word_count) {
    if (word_count == 0U)
        return;
    if (source != NULL)
        memcpy(target, source, word_count * sizeof(*target));
    else
        memset(target, 0, word_count * sizeof(*target));
}

static int merge_state(F2cBitFlowState *target, const F2cBitFlowState *source, size_t word_count) {
    size_t word;
    int changed = !target->initialized;
    if (!target->initialized) {
        target->initialized = 1;
        target->flags = source->flags;
        copy_bits(target->bits, source->bits, word_count);
        return 1;
    }
    if ((source->flags & ~target->flags) != 0U) {
        target->flags |= source->flags;
        changed = 1;
    }
    for (word = 0U; word < word_count; ++word) {
        const uint64_t merged = target->bits[word] | source->bits[word];
        if (merged != target->bits[word]) {
            target->bits[word] = merged;
            changed = 1;
        }
    }
    return changed;
}

static int enqueue(size_t *queue, unsigned char *queued, size_t capacity, size_t *tail,
                   size_t *count, size_t node) {
    if (node >= capacity)
        return 0;
    if (queued[node])
        return 1;
    if (*count >= capacity)
        return 0;
    queue[*tail] = node;
    *tail = (*tail + 1U) % capacity;
    ++*count;
    queued[node] = 1U;
    return 1;
}

int f2c_bit_flow_solve(const F2cControlFlowGraph *graph, size_t entry_node, size_t word_count,
                       const uint64_t *entry_bits, uint64_t entry_flags,
                       F2cBitFlowTransfer transfer, F2cBitFlowEdgeFilter edge_filter, void *user,
                       F2cBitFlowResult *result) {
    F2cBitFlowState output;
    size_t *queue = NULL;
    unsigned char *queued = NULL;
    size_t head = 0U;
    size_t tail = 0U;
    size_t queue_count = 0U;
    size_t node;
    if (graph == NULL || result == NULL || entry_node >= graph->node_count)
        return 0;
    memset(result, 0, sizeof(*result));
    if (graph->node_count > SIZE_MAX / sizeof(*result->states) ||
        (word_count != 0U && graph->node_count > SIZE_MAX / word_count) ||
        (word_count != 0U &&
         graph->node_count * word_count > SIZE_MAX / sizeof(*result->storage)) ||
        graph->node_count > SIZE_MAX / sizeof(*queue))
        return 0;
    result->states = (F2cBitFlowState *)calloc(graph->node_count, sizeof(*result->states));
    result->storage = word_count != 0U ? (uint64_t *)calloc(graph->node_count * word_count,
                                                            sizeof(*result->storage))
                                       : NULL;
    output.bits = word_count != 0U ? (uint64_t *)calloc(word_count, sizeof(*output.bits)) : NULL;
    queue = (size_t *)calloc(graph->node_count, sizeof(*queue));
    queued = (unsigned char *)calloc(graph->node_count, sizeof(*queued));
    if (result->states == NULL ||
        (word_count != 0U && (result->storage == NULL || output.bits == NULL)) || queue == NULL ||
        queued == NULL)
        goto failure;
    result->state_count = graph->node_count;
    result->word_count = word_count;
    for (node = 0U; node < graph->node_count; ++node)
        result->states[node].bits = word_count != 0U ? result->storage + node * word_count : NULL;
    result->states[entry_node].initialized = 1;
    result->states[entry_node].flags = entry_flags;
    copy_bits(result->states[entry_node].bits, entry_bits, word_count);
    if (!enqueue(queue, queued, graph->node_count, &tail, &queue_count, entry_node))
        goto failure;
    while (queue_count != 0U) {
        const F2cControlFlowNode *flow_node;
        const F2cBitFlowState *input;
        size_t edge;
        node = queue[head];
        head = (head + 1U) % graph->node_count;
        --queue_count;
        queued[node] = 0U;
        flow_node = &graph->nodes[node];
        input = &result->states[node];
        copy_bits(output.bits, input->bits, word_count);
        output.flags = input->flags;
        output.initialized = 1;
        if (transfer != NULL && !transfer(user, node, input, &output))
            goto failure;
        for (edge = 0U; edge < flow_node->successor_count; ++edge) {
            const F2cControlFlowEdge *successor = &flow_node->successors[edge];
            if (successor->target >= graph->node_count)
                goto failure;
            if (edge_filter != NULL && !edge_filter(user, node, successor, &output))
                continue;
            if (merge_state(&result->states[successor->target], &output, word_count) &&
                !enqueue(queue, queued, graph->node_count, &tail, &queue_count, successor->target))
                goto failure;
        }
    }
    free(output.bits);
    free(queue);
    free(queued);
    return 1;

failure:
    free(output.bits);
    free(queue);
    free(queued);
    f2c_bit_flow_free(result);
    return 0;
}

void f2c_bit_flow_free(F2cBitFlowResult *result) {
    if (result == NULL)
        return;
    free(result->states);
    free(result->storage);
    memset(result, 0, sizeof(*result));
}
