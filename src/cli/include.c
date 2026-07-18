#include "include.h"

#include "io.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct F2cCliOwnedInclude {
    char *source_name;
    char *source;
} F2cCliOwnedInclude;

static int path_separator(char value) { return value == '/' || value == '\\'; }

static int absolute_path(const char *path) {
    return path_separator(path[0]) ||
           (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
            path[1] == ':');
}

static char *duplicate_text(const char *text, size_t length) {
    char *copy;
    if (length == SIZE_MAX)
        return NULL;
    copy = (char *)malloc(length + 1U);
    if (copy == NULL)
        return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char *normalized_path(const char *path) {
    const size_t length = strlen(path);
    size_t *segments;
    char *result;
    size_t read = 0U;
    size_t written = 0U;
    size_t prefix_length = 0U;
    size_t segment_count = 0U;
    size_t protected_segments = 0U;
    int rooted = 0;
    if (length > (SIZE_MAX / sizeof(*segments)) - 1U)
        return NULL;
    result = (char *)malloc(length + 2U);
    segments = (size_t *)malloc((length + 1U) * sizeof(*segments));
    if (result == NULL || segments == NULL) {
        free(result);
        free(segments);
        return NULL;
    }
    if (length >= 2U &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') {
        result[written++] = path[0];
        result[written++] = ':';
        read = 2U;
        if (read < length && path_separator(path[read])) {
            result[written++] = '/';
            rooted = 1;
            while (read < length && path_separator(path[read]))
                ++read;
        }
    } else if (length != 0U && path_separator(path[0])) {
        const int network_path = length > 1U && path_separator(path[1]);
        result[written++] = '/';
        if (network_path) {
            result[written++] = '/';
            protected_segments = 2U;
        }
        rooted = 1;
        while (read < length && path_separator(path[read]))
            ++read;
    }
    prefix_length = written;
    while (read < length) {
        size_t begin;
        size_t rollback;
        while (read < length && path_separator(path[read]))
            ++read;
        begin = read;
        while (read < length && !path_separator(path[read]))
            ++read;
        if (read == begin || (read - begin == 1U && path[begin] == '.'))
            continue;
        if (read - begin == 2U && path[begin] == '.' && path[begin + 1U] == '.') {
            size_t previous = segment_count != 0U ? segments[segment_count - 1U] : 0U;
            if (previous < written && result[previous] == '/')
                ++previous;
            if (segment_count > protected_segments &&
                !(written - previous == 2U && result[previous] == '.' &&
                  result[previous + 1U] == '.')) {
                written = segments[--segment_count];
                continue;
            }
            if (rooted)
                continue;
        }
        rollback = written;
        if (written > prefix_length && result[written - 1U] != '/')
            result[written++] = '/';
        segments[segment_count++] = rollback;
        memcpy(result + written, path + begin, read - begin);
        written += read - begin;
    }
    if (written == 0U)
        result[written++] = '.';
    else if (written == 2U && result[1] == ':')
        result[written++] = '.';
    result[written] = '\0';
    free(segments);
    return result;
}

static char *including_directory(const char *source_name) {
    const char *cursor;
    const char *separator = NULL;
    if (source_name == NULL || source_name[0] == '\0' || source_name[0] == '<')
        return duplicate_text(".", 1U);
    for (cursor = source_name; *cursor != '\0'; ++cursor) {
        if (path_separator(*cursor))
            separator = cursor;
    }
    if (separator == NULL)
        return duplicate_text(".", 1U);
    if (separator == source_name)
        return duplicate_text(source_name, 1U);
    return duplicate_text(source_name, (size_t)(separator - source_name));
}

static char *join_path(const char *directory, const char *name) {
    const size_t directory_length = strlen(directory);
    const size_t name_length = strlen(name);
    const int needs_separator =
        directory_length != 0U && !path_separator(directory[directory_length - 1U]);
    char *path;
    size_t length;
    const size_t extra = (size_t)needs_separator + 1U;
    if (name_length > SIZE_MAX - extra || directory_length > SIZE_MAX - name_length - extra)
        return NULL;
    length = directory_length + (size_t)needs_separator + name_length;
    path = (char *)malloc(length + 1U);
    if (path == NULL)
        return NULL;
    memcpy(path, directory, directory_length);
    if (needs_separator)
        path[directory_length] = '/';
    memcpy(path + directory_length + (size_t)needs_separator, name, name_length + 1U);
    return path;
}

static F2cIncludeStatus open_candidate(char *path, F2cIncludeSource *result) {
    F2cCliOwnedInclude *owned;
    size_t length = 0U;
    char *normalized = normalized_path(path);
    char *source;
    free(path);
    if (normalized == NULL) {
        errno = ENOMEM;
        return F2C_INCLUDE_ERROR;
    }
    path = normalized;
    source = f2c_cli_read_source(path, F2C_DEFAULT_MAX_INPUT_BYTES, &length);
    if (source == NULL) {
        const int failure = errno;
        free(path);
        errno = failure;
        return failure == ENOENT || failure == ENOTDIR ? F2C_INCLUDE_NOT_FOUND : F2C_INCLUDE_ERROR;
    }
    owned = (F2cCliOwnedInclude *)malloc(sizeof(*owned));
    if (owned == NULL) {
        free(source);
        free(path);
        errno = ENOMEM;
        return F2C_INCLUDE_ERROR;
    }
    owned->source_name = path;
    owned->source = source;
    result->source_name = path;
    result->source = source;
    result->length = length;
    result->source_form = F2C_SOURCE_AUTO;
    result->handle = owned;
    return F2C_INCLUDE_FOUND;
}

F2cIncludeStatus f2c_cli_resolve_include(const F2cIncludeRequest *request, F2cIncludeSource *result,
                                         void *user_data) {
    const F2cCliIncludeContext *context = (const F2cCliIncludeContext *)user_data;
    F2cIncludeStatus status;
    size_t index;
    memset(result, 0, sizeof(*result));
    if (absolute_path(request->requested_name)) {
        char *path = duplicate_text(request->requested_name, strlen(request->requested_name));
        return path != NULL ? open_candidate(path, result) : F2C_INCLUDE_ERROR;
    }
    if (request->kind == F2C_INCLUDE_QUOTED) {
        char *directory = including_directory(request->including_source_name);
        char *path;
        if (directory == NULL)
            return F2C_INCLUDE_ERROR;
        path = join_path(directory, request->requested_name);
        free(directory);
        if (path == NULL)
            return F2C_INCLUDE_ERROR;
        status = open_candidate(path, result);
        if (status != F2C_INCLUDE_NOT_FOUND)
            return status;
    }
    for (index = 0U; context != NULL && index < context->count; ++index) {
        char *path = join_path(context->paths[index], request->requested_name);
        if (path == NULL)
            return F2C_INCLUDE_ERROR;
        status = open_candidate(path, result);
        if (status != F2C_INCLUDE_NOT_FOUND)
            return status;
    }
    return F2C_INCLUDE_NOT_FOUND;
}

void f2c_cli_release_include(F2cIncludeSource *source, void *user_data) {
    F2cCliOwnedInclude *owned = (F2cCliOwnedInclude *)source->handle;
    (void)user_data;
    if (owned == NULL)
        return;
    free(owned->source_name);
    free(owned->source);
    free(owned);
    memset(source, 0, sizeof(*source));
}
