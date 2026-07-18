#include "frontend/token.h"

#include <stdlib.h>
#include <string.h>

static int same_name(const char *left, const char *right) {
    return left == right || (left != NULL && right != NULL && strcmp(left, right) == 0);
}

static int reserve_segments(F2cSourceMap *map, size_t additional) {
    F2cSourceMapSegment *replacement;
    size_t required;
    size_t capacity;
    if (additional > SIZE_MAX - map->count)
        return 0;
    required = map->count + additional;
    if (required <= map->capacity)
        return 1;
    capacity = map->capacity == 0U ? 8U : map->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U)
            return 0;
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement = (F2cSourceMapSegment *)realloc(map->items, capacity * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    map->items = replacement;
    map->capacity = capacity;
    return 1;
}

static size_t advanced_column(size_t column, size_t length, unsigned char step) {
    if (step == 0U)
        return column;
    return length > SIZE_MAX - column ? SIZE_MAX : column + length;
}

F2cSourceSpan f2c_source_span_cover(const F2cSourceSpan *first, const F2cSourceSpan *last) {
    F2cSourceSpan result = {0};
    if (first == NULL || last == NULL)
        return result;
    result.begin = first->begin;
    result.end = last->end;
    result.has_spelling = first->has_spelling || last->has_spelling;
    if (result.has_spelling) {
        result.spelling_begin = first->has_spelling ? first->spelling_begin : first->begin;
        result.spelling_end = last->has_spelling ? last->spelling_end : last->end;
    }
    return result;
}

int f2c_source_map_append(F2cSourceMap *map, size_t logical_begin, size_t length,
                          F2cSourcePosition expansion, size_t expansion_width,
                          unsigned char expansion_column_step, F2cSourcePosition spelling,
                          size_t spelling_width, unsigned char spelling_column_step,
                          int has_spelling) {
    F2cSourceMapSegment *last;
    if (length == 0U)
        return 1;
    if (logical_begin > SIZE_MAX - length)
        return 0;
    last = map->count != 0U ? &map->items[map->count - 1U] : NULL;
    if (last != NULL && last->logical_begin + last->length == logical_begin &&
        last->expansion.line == expansion.line &&
        same_name(last->expansion.source_name, expansion.source_name) &&
        last->expansion_column_step == expansion_column_step &&
        advanced_column(last->expansion.column, last->length, expansion_column_step) ==
            expansion.column &&
        last->has_spelling == (unsigned char)(has_spelling != 0) &&
        last->spelling.line == spelling.line &&
        same_name(last->spelling.source_name, spelling.source_name) &&
        last->spelling_column_step == spelling_column_step &&
        advanced_column(last->spelling.column, last->length, spelling_column_step) ==
            spelling.column &&
        (expansion_column_step != 0U || last->expansion_width == expansion_width) &&
        (spelling_column_step != 0U || last->spelling_width == spelling_width)) {
        last->length += length;
        if (last->expansion_column_step != 0U)
            last->expansion_width += expansion_width;
        if (last->spelling_column_step != 0U)
            last->spelling_width += spelling_width;
        return 1;
    }
    if (!reserve_segments(map, 1U))
        return 0;
    map->items[map->count++] = (F2cSourceMapSegment){logical_begin,
                                                     length,
                                                     expansion,
                                                     spelling,
                                                     expansion_width,
                                                     spelling_width,
                                                     expansion_column_step,
                                                     spelling_column_step,
                                                     (unsigned char)(has_spelling != 0)};
    return 1;
}

int f2c_source_map_append_slice(F2cSourceMap *destination, size_t logical_begin,
                                const F2cSourceMapSegment *source, size_t source_count,
                                size_t source_begin, size_t length) {
    size_t index;
    size_t covered = 0U;
    size_t source_end;
    if (source_begin > SIZE_MAX - length || logical_begin > SIZE_MAX - length)
        return 0;
    source_end = source_begin + length;
    for (index = 0U; index < source_count; ++index) {
        const F2cSourceMapSegment *segment = &source[index];
        const size_t segment_end = segment->logical_begin + segment->length;
        const size_t overlap_begin =
            segment->logical_begin > source_begin ? segment->logical_begin : source_begin;
        const size_t overlap_end = segment_end < source_end ? segment_end : source_end;
        const size_t offset = overlap_begin - segment->logical_begin;
        F2cSourcePosition expansion;
        F2cSourcePosition spelling;
        size_t expansion_width;
        size_t spelling_width;
        if (overlap_begin >= overlap_end)
            continue;
        expansion = segment->expansion;
        spelling = segment->spelling;
        expansion.column += offset * segment->expansion_column_step;
        spelling.column += offset * segment->spelling_column_step;
        expansion_width = segment->expansion_column_step != 0U ? overlap_end - overlap_begin
                                                               : segment->expansion_width;
        spelling_width = segment->spelling_column_step != 0U ? overlap_end - overlap_begin
                                                             : segment->spelling_width;
        if (!f2c_source_map_append(destination, logical_begin + overlap_begin - source_begin,
                                   overlap_end - overlap_begin, expansion, expansion_width,
                                   segment->expansion_column_step, spelling, spelling_width,
                                   segment->spelling_column_step, segment->has_spelling))
            return 0;
        covered += overlap_end - overlap_begin;
    }
    return covered == length;
}

F2cSourceMapSegment *f2c_source_map_slice(const F2cSourceMapSegment *source, size_t source_count,
                                          size_t begin, size_t length, size_t *result_count) {
    F2cSourceMap result = {0};
    *result_count = 0U;
    if (length == 0U)
        return NULL;
    if (!f2c_source_map_append_slice(&result, 0U, source, source_count, begin, length)) {
        f2c_source_map_discard(&result);
        return NULL;
    }
    *result_count = result.count;
    return result.items;
}

void f2c_source_map_discard(F2cSourceMap *map) {
    if (map == NULL)
        return;
    free(map->items);
    memset(map, 0, sizeof(*map));
}
