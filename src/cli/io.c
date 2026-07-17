#include "io.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#endif

typedef struct StagedArtifact {
    const F2cCliArtifact *artifact;
    char *temporary_path;
    char *backup_path;
    int had_original;
    int installed;
} StagedArtifact;

static int stream_path(const char *path) { return path == NULL || strcmp(path, "-") == 0; }

static FILE *open_file(const char *path, const char *mode) {
#if defined(_WIN32)
    FILE *file = NULL;
    const errno_t status = fopen_s(&file, path, mode);
    if (status != 0) {
        errno = (int)status;
        return NULL;
    }
    return file;
#else
    return fopen(path, mode);
#endif
}

static char *temporary_path(const char *path, size_t sequence, const char *suffix) {
    const size_t path_length = strlen(path);
    const size_t suffix_length = strlen(suffix);
    const size_t extra = 48U;
    char *result;
    int written;

    if (path_length > SIZE_MAX - suffix_length - extra) {
        errno = EOVERFLOW;
        return NULL;
    }
    result = (char *)malloc(path_length + suffix_length + extra);
    if (result == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    written = snprintf(result, path_length + suffix_length + extra, "%s.f2c-%s-%zu", path, suffix,
                       sequence);
    if (written < 0 || (size_t)written >= path_length + suffix_length + extra) {
        free(result);
        errno = EOVERFLOW;
        return NULL;
    }
    return result;
}

static int replaceable_path_status(const char *path) {
    struct stat status;
    if (stat(path, &status) == 0) {
#if defined(_WIN32)
        if ((status.st_mode & _S_IFMT) != _S_IFREG) {
#else
        if (!S_ISREG(status.st_mode)) {
#endif
            errno = EISDIR;
            return -1;
        }
        return 1;
    }
    return errno == ENOENT ? 0 : -1;
}

static int path_exists(const char *path) {
    struct stat status;
    if (stat(path, &status) == 0)
        return 1;
    return errno == ENOENT ? 0 : -1;
}

static int replace_path(const char *source, const char *destination) {
#if defined(_WIN32)
    if (MoveFileExA(source, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
        return 0;
    errno = EIO;
    return -1;
#else
    return rename(source, destination);
#endif
}

static int stage_artifact(StagedArtifact *staged, size_t sequence) {
    size_t attempt;
    FILE *file = NULL;
    const size_t length = strlen(staged->artifact->data);

    for (attempt = 0U; attempt < 128U; ++attempt) {
        free(staged->temporary_path);
        staged->temporary_path = temporary_path(staged->artifact->path, sequence + attempt, "tmp");
        if (staged->temporary_path == NULL)
            return -1;
        file = open_file(staged->temporary_path, "wbx");
        if (file != NULL)
            break;
        if (errno != EEXIST)
            return -1;
    }
    if (file == NULL) {
        errno = EEXIST;
        return -1;
    }
    {
        int failed = fwrite(staged->artifact->data, 1U, length, file) != length;
        if (!failed && fflush(file) != 0)
            failed = 1;
        if (fclose(file) != 0)
            failed = 1;
        if (failed) {
            const int saved_error = errno != 0 ? errno : EIO;
            (void)remove(staged->temporary_path);
            errno = saved_error;
            return -1;
        }
    }
    return 0;
}

static void discard_staged_artifacts(StagedArtifact *staged, size_t count) {
    size_t i;
    for (i = 0U; i < count; ++i) {
        if (staged[i].temporary_path != NULL)
            (void)remove(staged[i].temporary_path);
        free(staged[i].temporary_path);
        free(staged[i].backup_path);
    }
}

static void restore_artifacts(StagedArtifact *staged, size_t count) {
    size_t i = count;
    while (i != 0U) {
        --i;
        if (staged[i].installed)
            (void)remove(staged[i].artifact->path);
        if (staged[i].had_original && staged[i].backup_path != NULL)
            (void)replace_path(staged[i].backup_path, staged[i].artifact->path);
    }
}

static int reserve_backups(StagedArtifact *staged, size_t count, const char **failed_path) {
    size_t i;
    for (i = 0U; i < count; ++i) {
        const int exists = replaceable_path_status(staged[i].artifact->path);
        if (exists < 0) {
            const int saved_error = errno;
            *failed_path = staged[i].artifact->path;
            restore_artifacts(staged, i);
            errno = saved_error;
            return -1;
        }
        if (!exists)
            continue;
        staged[i].backup_path = temporary_path(staged[i].temporary_path, i, "old");
        if (staged[i].backup_path == NULL || path_exists(staged[i].backup_path) != 0 ||
            replace_path(staged[i].artifact->path, staged[i].backup_path) != 0) {
            const int saved_error = errno;
            *failed_path = staged[i].artifact->path;
            restore_artifacts(staged, i);
            errno = saved_error;
            return -1;
        }
        staged[i].had_original = 1;
    }
    return 0;
}

static int commit_artifacts(StagedArtifact *staged, size_t count, const char **failed_path) {
    size_t i;
    if (reserve_backups(staged, count, failed_path) != 0)
        return -1;
    for (i = 0U; i < count; ++i) {
        if (replace_path(staged[i].temporary_path, staged[i].artifact->path) != 0) {
            const int saved_error = errno;
            *failed_path = staged[i].artifact->path;
            restore_artifacts(staged, count);
            errno = saved_error;
            return -1;
        }
        staged[i].installed = 1;
    }
    return 0;
}

char *f2c_cli_read_source(const char *path, size_t maximum_bytes, size_t *length) {
    FILE *file;
    char *data;
    size_t capacity;
    size_t used = 0U;
    int close_file;

    if (path == NULL || length == NULL) {
        errno = EINVAL;
        return NULL;
    }
    close_file = strcmp(path, "-") != 0;
    file = close_file ? open_file(path, "rb") : stdin;
    if (file == NULL)
        return NULL;
    if (maximum_bytes == 0U) {
        const int extra = fgetc(file);
        const int read_error = ferror(file);
        if (close_file)
            (void)fclose(file);
        if (extra != EOF || read_error) {
            errno = extra != EOF ? EFBIG : EIO;
            return NULL;
        }
        data = (char *)malloc(1U);
        if (data == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        data[0] = '\0';
        *length = 0U;
        return data;
    }
    if (maximum_bytes == SIZE_MAX)
        --maximum_bytes;
    capacity = maximum_bytes < 65536U ? maximum_bytes : 65536U;
    if (capacity == 0U)
        capacity = 1U;
    data = (char *)malloc(capacity + 1U);
    if (data == NULL) {
        if (close_file)
            (void)fclose(file);
        errno = ENOMEM;
        return NULL;
    }
    for (;;) {
        size_t available = capacity - used;
        size_t got;
        if (available == 0U) {
            size_t next;
            char *replacement;
            if (capacity == maximum_bytes) {
                const int extra = fgetc(file);
                if (extra != EOF) {
                    errno = EFBIG;
                    goto failed;
                }
                if (ferror(file))
                    goto failed;
                break;
            }
            next = capacity > maximum_bytes / 2U ? maximum_bytes : capacity * 2U;
            replacement = (char *)realloc(data, next + 1U);
            if (replacement == NULL) {
                errno = ENOMEM;
                goto failed;
            }
            data = replacement;
            capacity = next;
            available = capacity - used;
        }
        got = fread(data + used, 1U, available, file);
        used += got;
        if (got != available) {
            if (ferror(file))
                goto failed;
            break;
        }
    }
    if (close_file && fclose(file) != 0)
        goto failed_closed;
    data[used] = '\0';
    *length = used;
    return data;

failed: {
    const int saved_error = errno != 0 ? errno : EIO;
    if (close_file)
        (void)fclose(file);
    free(data);
    errno = saved_error;
    return NULL;
}
failed_closed: {
    const int saved_error = errno != 0 ? errno : EIO;
    free(data);
    errno = saved_error;
    return NULL;
}
}

int f2c_cli_write_artifacts(const F2cCliArtifact *artifacts, size_t count,
                            const char **failed_path) {
    StagedArtifact *staged;
    size_t staged_count = 0U;
    size_t i;
    int result = -1;

    if (artifacts == NULL || failed_path == NULL || count == 0U) {
        errno = EINVAL;
        return -1;
    }
    *failed_path = NULL;
    staged = (StagedArtifact *)calloc(count, sizeof(*staged));
    if (staged == NULL) {
        errno = ENOMEM;
        return -1;
    }
    for (i = 0U; i < count; ++i) {
        if (artifacts[i].data == NULL) {
            errno = EINVAL;
            *failed_path = artifacts[i].path;
            goto cleanup;
        }
        if (stream_path(artifacts[i].path))
            continue;
        staged[staged_count].artifact = &artifacts[i];
        if (stage_artifact(&staged[staged_count], i * 128U) != 0) {
            *failed_path = artifacts[i].path;
            goto cleanup;
        }
        ++staged_count;
    }
    if (commit_artifacts(staged, staged_count, failed_path) != 0)
        goto cleanup;
    for (i = 0U; i < count; ++i) {
        const size_t length = strlen(artifacts[i].data);
        if (!stream_path(artifacts[i].path))
            continue;
        if (fwrite(artifacts[i].data, 1U, length, stdout) != length || fflush(stdout) != 0) {
            const int saved_error = errno != 0 ? errno : EIO;
            *failed_path = "stdout";
            restore_artifacts(staged, staged_count);
            errno = saved_error;
            goto cleanup;
        }
    }
    for (i = 0U; i < staged_count; ++i) {
        if (staged[i].backup_path != NULL)
            (void)remove(staged[i].backup_path);
    }
    result = 0;

cleanup: {
    const int saved_error = errno;
    discard_staged_artifacts(staged, staged_count);
    free(staged);
    errno = saved_error;
}
    return result;
}
