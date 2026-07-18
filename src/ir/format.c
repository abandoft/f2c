#include "ir/format.h"

#include <stdlib.h>
#include <string.h>

static void free_node(F2cFormatNode *node) {
    size_t index;
    if (node == NULL)
        return;
    for (index = 0U; index < node->child_count; ++index)
        free_node(&node->children[index]);
    free(node->children);
    free(node->text);
    free(node->v_list);
    memset(node, 0, sizeof(*node));
}

static int clone_node(F2cFormatNode *destination, const F2cFormatNode *source) {
    size_t index;
    memset(destination, 0, sizeof(*destination));
    *destination = *source;
    destination->text = NULL;
    destination->v_list = NULL;
    destination->children = NULL;
    destination->child_count = 0U;
    destination->child_capacity = 0U;
    if (source->text != NULL) {
        destination->text = (char *)malloc(source->text_length + 1U);
        if (destination->text == NULL)
            goto failed;
        memcpy(destination->text, source->text, source->text_length);
        destination->text[source->text_length] = '\0';
    }
    if (source->v_list_count != 0U) {
        if (source->v_list_count > SIZE_MAX / sizeof(*destination->v_list))
            goto failed;
        destination->v_list =
            (int32_t *)malloc(source->v_list_count * sizeof(*destination->v_list));
        if (destination->v_list == NULL)
            goto failed;
        memcpy(destination->v_list, source->v_list,
               source->v_list_count * sizeof(*destination->v_list));
    }
    if (source->child_count != 0U) {
        if (source->child_count > SIZE_MAX / sizeof(*destination->children))
            goto failed;
        destination->children =
            (F2cFormatNode *)calloc(source->child_count, sizeof(*destination->children));
        if (destination->children == NULL)
            goto failed;
        destination->child_capacity = source->child_count;
        for (index = 0U; index < source->child_count; ++index) {
            if (!clone_node(&destination->children[index], &source->children[index]))
                goto failed;
            ++destination->child_count;
        }
    }
    destination->v_list_count = source->v_list_count;
    return 1;

failed:
    free_node(destination);
    return 0;
}

F2cFormat *f2c_format_clone(const F2cFormat *format) {
    F2cFormat *copy;
    if (format == NULL)
        return NULL;
    copy = (F2cFormat *)calloc(1U, sizeof(*copy));
    if (copy == NULL)
        return NULL;
    copy->span = format->span;
    copy->source_length = format->source_length;
    copy->validated = format->validated;
    if (format->source != NULL) {
        copy->source = (char *)malloc(format->source_length + 1U);
        if (copy->source == NULL)
            goto failed;
        memcpy(copy->source, format->source, format->source_length);
        copy->source[format->source_length] = '\0';
    }
    if (!clone_node(&copy->root, &format->root))
        goto failed;
    return copy;

failed:
    f2c_format_free(copy);
    return NULL;
}

void f2c_format_free(F2cFormat *format) {
    if (format == NULL)
        return;
    free_node(&format->root);
    free(format->source);
    free(format);
}
