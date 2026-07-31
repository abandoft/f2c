#include "codegen/lowering/private.h"

#include <stdint.h>
#include <stdlib.h>

typedef enum F2cLoweringSlotState {
    F2C_LOWERING_SLOT_EMPTY,
    F2C_LOWERING_SLOT_OCCUPIED,
    F2C_LOWERING_SLOT_TOMBSTONE
} F2cLoweringSlotState;

typedef struct F2cExpressionLowering {
    const F2cExpr *expression;
    char *code;
    char *extent;
    char *character_length;
    int array_temporary;
    int argument_materialized;
    F2cLoweringSlotState state;
} F2cExpressionLowering;

struct F2cExpressionLoweringStore {
    F2cExpressionLowering *slots;
    size_t capacity;
    size_t count;
    size_t tombstone_count;
};

static size_t expression_hash(const F2cExpr *expression) {
    uintptr_t value = (uintptr_t)(const void *)expression;
    value ^= value >> 4U;
    value *= (uintptr_t)UINT32_C(2654435761);
    value ^= value >> 16U;
    return (size_t)value;
}

static void lowering_strings_free(F2cExpressionLowering *lowering) {
    free(lowering->code);
    free(lowering->extent);
    free(lowering->character_length);
    lowering->code = NULL;
    lowering->extent = NULL;
    lowering->character_length = NULL;
}

static F2cExpressionLowering *find_slot(F2cExpressionLoweringStore *store,
                                        const F2cExpr *expression, int inserting) {
    size_t index;
    size_t first_tombstone = SIZE_MAX;
    if (store == NULL || store->capacity == 0U || expression == NULL)
        return NULL;
    index = expression_hash(expression) & (store->capacity - 1U);
    for (;;) {
        F2cExpressionLowering *slot = &store->slots[index];
        if (slot->state == F2C_LOWERING_SLOT_EMPTY) {
            if (!inserting)
                return NULL;
            return first_tombstone != SIZE_MAX ? &store->slots[first_tombstone] : slot;
        }
        if (slot->state == F2C_LOWERING_SLOT_OCCUPIED && slot->expression == expression)
            return slot;
        if (slot->state == F2C_LOWERING_SLOT_TOMBSTONE && first_tombstone == SIZE_MAX)
            first_tombstone = index;
        index = (index + 1U) & (store->capacity - 1U);
    }
}

static int rehash_store(F2cExpressionLoweringStore *store, size_t capacity) {
    F2cExpressionLowering *old_slots = store->slots;
    const size_t old_capacity = store->capacity;
    const size_t old_count = store->count;
    const size_t old_tombstone_count = store->tombstone_count;
    size_t index;
    if (capacity < 16U)
        capacity = 16U;
    if (capacity > SIZE_MAX / sizeof(*store->slots))
        return 0;
    store->slots = (F2cExpressionLowering *)calloc(capacity, sizeof(*store->slots));
    if (store->slots == NULL) {
        store->slots = old_slots;
        return 0;
    }
    store->capacity = capacity;
    store->count = 0U;
    store->tombstone_count = 0U;
    for (index = 0U; index < old_capacity; ++index) {
        F2cExpressionLowering *source = &old_slots[index];
        F2cExpressionLowering *target;
        if (source->state != F2C_LOWERING_SLOT_OCCUPIED)
            continue;
        target = find_slot(store, source->expression, 1);
        if (target == NULL) {
            free(store->slots);
            store->slots = old_slots;
            store->capacity = old_capacity;
            store->count = old_count;
            store->tombstone_count = old_tombstone_count;
            return 0;
        }
        *target = *source;
        ++store->count;
    }
    free(old_slots);
    return 1;
}

static int ensure_capacity(F2cExpressionLoweringStore *store) {
    size_t capacity;
    size_t used;
    if (store->count > SIZE_MAX - store->tombstone_count ||
        store->count + store->tombstone_count == SIZE_MAX)
        return 0;
    used = store->count + store->tombstone_count + 1U;
    if (store->capacity != 0U && used <= store->capacity / 2U)
        return 1;
    if (store->capacity == 0U)
        capacity = 16U;
    else if (store->count + 1U <= store->capacity / 2U)
        capacity = store->capacity;
    else {
        if (store->capacity > SIZE_MAX / 2U)
            return 0;
        capacity = store->capacity * 2U;
    }
    return rehash_store(store, capacity);
}

static F2cExpressionLoweringStore *context_store(Context *context, int create) {
    if (context == NULL)
        return NULL;
    if (context->expression_lowering == NULL && create) {
        context->expression_lowering =
            (F2cExpressionLoweringStore *)calloc(1U, sizeof(*context->expression_lowering));
    }
    return context->expression_lowering;
}

static F2cExpressionLowering *unit_lowering(Unit *unit, const F2cExpr *expression, int create) {
    F2cExpressionLoweringStore *store;
    F2cExpressionLowering *slot;
    if (unit == NULL || expression == NULL)
        return NULL;
    store = context_store(unit->context, create);
    if (store == NULL)
        return NULL;
    slot = find_slot(store, expression, 0);
    if (slot != NULL || !create)
        return slot;
    if (!ensure_capacity(store))
        return NULL;
    slot = find_slot(store, expression, 1);
    if (slot == NULL)
        return NULL;
    if (slot->state == F2C_LOWERING_SLOT_TOMBSTONE)
        --store->tombstone_count;
    *slot = (F2cExpressionLowering){0};
    slot->expression = expression;
    slot->state = F2C_LOWERING_SLOT_OCCUPIED;
    ++store->count;
    return slot;
}

static const F2cExpressionLowering *unit_lowering_const(const Unit *unit,
                                                        const F2cExpr *expression) {
    F2cExpressionLoweringStore *store;
    if (unit == NULL || unit->context == NULL || expression == NULL)
        return NULL;
    store = unit->context->expression_lowering;
    return find_slot(store, expression, 0);
}

const char *f2c_lowering_code(const Unit *unit, const F2cExpr *expression) {
    const F2cExpressionLowering *lowering = unit_lowering_const(unit, expression);
    return lowering != NULL ? lowering->code : NULL;
}

const char *f2c_lowering_extent(const Unit *unit, const F2cExpr *expression) {
    const F2cExpressionLowering *lowering = unit_lowering_const(unit, expression);
    return lowering != NULL ? lowering->extent : NULL;
}

const char *f2c_lowering_character_length(const Unit *unit, const F2cExpr *expression) {
    const F2cExpressionLowering *lowering = unit_lowering_const(unit, expression);
    return lowering != NULL ? lowering->character_length : NULL;
}

int f2c_lowering_is_array_temporary(const Unit *unit, const F2cExpr *expression) {
    const F2cExpressionLowering *lowering = unit_lowering_const(unit, expression);
    return lowering != NULL && lowering->array_temporary;
}

int f2c_lowering_argument_materialized(const Unit *unit, const F2cExpr *expression) {
    const F2cExpressionLowering *lowering = unit_lowering_const(unit, expression);
    return lowering != NULL && lowering->argument_materialized;
}

static int take_string(Unit *unit, const F2cExpr *expression, char *value, size_t member) {
    F2cExpressionLowering *lowering = unit_lowering(unit, expression, value != NULL);
    char **target;
    if (lowering == NULL) {
        free(value);
        return value == NULL;
    }
    if (member == 0U)
        target = &lowering->code;
    else if (member == 1U)
        target = &lowering->extent;
    else
        target = &lowering->character_length;
    free(*target);
    *target = value;
    return 1;
}

int f2c_lowering_take_code(Unit *unit, const F2cExpr *expression, char *code) {
    return take_string(unit, expression, code, 0U);
}

int f2c_lowering_take_extent(Unit *unit, const F2cExpr *expression, char *extent) {
    return take_string(unit, expression, extent, 1U);
}

int f2c_lowering_take_character_length(Unit *unit, const F2cExpr *expression, char *length) {
    return take_string(unit, expression, length, 2U);
}

static int copy_string(Unit *unit, const F2cExpr *expression, const char *value, size_t member) {
    char *copy = value != NULL ? f2c_strdup(value) : NULL;
    if (value != NULL && copy == NULL)
        return 0;
    return take_string(unit, expression, copy, member);
}

int f2c_lowering_copy_code(Unit *unit, const F2cExpr *expression, const char *code) {
    return copy_string(unit, expression, code, 0U);
}

int f2c_lowering_copy_extent(Unit *unit, const F2cExpr *expression, const char *extent) {
    return copy_string(unit, expression, extent, 1U);
}

int f2c_lowering_copy_character_length(Unit *unit, const F2cExpr *expression, const char *length) {
    return copy_string(unit, expression, length, 2U);
}

int f2c_lowering_set_array_temporary(Unit *unit, const F2cExpr *expression, int value) {
    F2cExpressionLowering *lowering = unit_lowering(unit, expression, value != 0);
    if (lowering == NULL)
        return value == 0;
    lowering->array_temporary = value != 0;
    return 1;
}

int f2c_lowering_set_argument_materialized(Unit *unit, const F2cExpr *expression, int value) {
    F2cExpressionLowering *lowering = unit_lowering(unit, expression, value != 0);
    if (lowering == NULL)
        return value == 0;
    lowering->argument_materialized = value != 0;
    return 1;
}

int f2c_lowering_clone(Unit *unit, const F2cExpr *target, const F2cExpr *source) {
    const F2cExpressionLowering *source_lowering = unit_lowering_const(unit, source);
    F2cExpressionLowering *target_lowering;
    char *code = NULL;
    char *extent = NULL;
    char *character_length = NULL;
    int array_temporary;
    int argument_materialized;
    if (unit == NULL || target == NULL || source == NULL)
        return 0;
    if (target == source)
        return 1;
    f2c_lowering_forget(unit, target);
    if (source_lowering == NULL)
        return 1;
    array_temporary = source_lowering->array_temporary;
    argument_materialized = source_lowering->argument_materialized;
    if ((source_lowering->code != NULL && (code = f2c_strdup(source_lowering->code)) == NULL) ||
        (source_lowering->extent != NULL &&
         (extent = f2c_strdup(source_lowering->extent)) == NULL) ||
        (source_lowering->character_length != NULL &&
         (character_length = f2c_strdup(source_lowering->character_length)) == NULL)) {
        free(code);
        free(extent);
        free(character_length);
        return 0;
    }
    target_lowering = unit_lowering(unit, target, 1);
    if (target_lowering == NULL) {
        free(code);
        free(extent);
        free(character_length);
        return 0;
    }
    target_lowering->code = code;
    target_lowering->extent = extent;
    target_lowering->character_length = character_length;
    target_lowering->array_temporary = array_temporary;
    target_lowering->argument_materialized = argument_materialized;
    return 1;
}

void f2c_lowering_forget(Unit *unit, const F2cExpr *expression) {
    F2cExpressionLoweringStore *store;
    F2cExpressionLowering *lowering;
    if (unit == NULL || unit->context == NULL || expression == NULL)
        return;
    store = unit->context->expression_lowering;
    lowering = find_slot(store, expression, 0);
    if (lowering == NULL)
        return;
    lowering_strings_free(lowering);
    lowering->expression = NULL;
    lowering->array_temporary = 0;
    lowering->argument_materialized = 0;
    lowering->state = F2C_LOWERING_SLOT_TOMBSTONE;
    --store->count;
    ++store->tombstone_count;
}

void f2c_lowering_forget_tree(Unit *unit, const F2cExpr *expression) {
    size_t child;
    if (expression == NULL)
        return;
    for (child = 0U; child < expression->child_count; ++child)
        f2c_lowering_forget_tree(unit, expression->children[child]);
    f2c_lowering_forget(unit, expression);
}

void f2c_lowering_clear(Context *context) {
    F2cExpressionLoweringStore *store = context_store(context, 0);
    size_t index;
    if (store == NULL)
        return;
    for (index = 0U; index < store->capacity; ++index) {
        if (store->slots[index].state == F2C_LOWERING_SLOT_OCCUPIED)
            lowering_strings_free(&store->slots[index]);
    }
    free(store->slots);
    free(store);
    context->expression_lowering = NULL;
}

void f2c_codegen_expression_free(Unit *unit, F2cExpr *expression) {
    f2c_lowering_forget_tree(unit, expression);
    f2c_expr_free(expression);
}
