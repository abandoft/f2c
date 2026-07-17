#ifndef F2C_CLI_IO_H
#define F2C_CLI_IO_H

#include <stddef.h>

typedef struct F2cCliArtifact {
    const char *path;
    const char *data;
} F2cCliArtifact;

char *f2c_cli_read_source(const char *path, size_t maximum_bytes, size_t *length);
int f2c_cli_write_artifacts(const F2cCliArtifact *artifacts, size_t count,
                            const char **failed_path);

#endif
