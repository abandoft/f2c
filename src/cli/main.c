#include "f2c/f2c.h"

/* Thin command-line adapter around the public project transpilation API. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *stream) {
    fputs("usage: f2c [options] INPUT...\n"
          "Translate Fortran source to portable, runtime-free C17.\n\n"
          "options:\n"
          "  -o FILE       write output to FILE (default: stdout)\n"
          "  --header FILE write the generated C interface to FILE\n"
          "  --free-form   override extension detection and parse every input as free form\n"
          "  --fixed-form  override extension detection and parse every input as fixed form\n"
          "  --comments    retain source lines as C comments\n"
          "  --version     print version and exit\n"
          "  -h, --help    show this help\n\n"
          "Source form is automatic by default: .f/.for/.ftn are fixed form; modern\n"
          "Fortran extensions such as .f90 are free form. Use an override only when a\n"
          "file name does not describe its physical source layout.\n",
          stream);
}

static char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    char *data;
    long size;
    size_t got;
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (char *)malloc((size_t)size + 1U);
    if (data == NULL) {
        fclose(file);
        errno = ENOMEM;
        return NULL;
    }
    got = fread(data, 1U, (size_t)size, file);
    if (got != (size_t)size && ferror(file)) {
        free(data);
        fclose(file);
        return NULL;
    }
    data[got] = '\0';
    *length = got;
    fclose(file);
    return data;
}

static int write_file(const char *path, const char *data) {
    FILE *file = path == NULL ? stdout : fopen(path, "wb");
    const size_t length = strlen(data);
    int failed;
    if (file == NULL) {
        return -1;
    }
    failed = fwrite(data, 1U, length, file) != length;
    if (path != NULL && fclose(file) != 0) {
        failed = 1;
    }
    return failed ? -1 : 0;
}

int main(int argc, char **argv) {
    F2cOptions options = {NULL, F2C_SOURCE_AUTO, 0};
    const char **input_paths = NULL;
    size_t input_count = 0U;
    const char *output = NULL;
    const char *header_output = NULL;
    char **sources = NULL;
    F2cInput *inputs = NULL;
    F2cResult result;
    int i;
    int status = EXIT_SUCCESS;

    input_paths = (const char **)calloc((size_t)argc, sizeof(*input_paths));
    if (input_paths == NULL) {
        fputs("f2c: out of memory\n", stderr);
        return EXIT_FAILURE;
    }

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            free(input_paths);
            return EXIT_SUCCESS;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("f2c %s\n", f2c_version());
            free(input_paths);
            return EXIT_SUCCESS;
        }
        if (strcmp(arg, "--free-form") == 0) {
            if (options.source_form == F2C_SOURCE_FIXED) {
                fputs("f2c: --free-form conflicts with --fixed-form\n", stderr);
                free(input_paths);
                return EXIT_FAILURE;
            }
            options.source_form = F2C_SOURCE_FREE;
        } else if (strcmp(arg, "--fixed-form") == 0) {
            if (options.source_form == F2C_SOURCE_FREE) {
                fputs("f2c: --fixed-form conflicts with --free-form\n", stderr);
                free(input_paths);
                return EXIT_FAILURE;
            }
            options.source_form = F2C_SOURCE_FIXED;
        } else if (strcmp(arg, "--comments") == 0) {
            options.emit_source_comments = 1;
        } else if (strcmp(arg, "-o") == 0) {
            if (++i == argc) {
                fputs("f2c: -o requires a file name\n", stderr);
                free(input_paths);
                return EXIT_FAILURE;
            }
            output = argv[i];
        } else if (strcmp(arg, "--header") == 0) {
            if (++i == argc) {
                fputs("f2c: --header requires a file name\n", stderr);
                free(input_paths);
                return EXIT_FAILURE;
            }
            header_output = argv[i];
        } else if (arg[0] == '-') {
            fprintf(stderr, "f2c: unknown option: %s\n", arg);
            free(input_paths);
            return EXIT_FAILURE;
        } else {
            input_paths[input_count++] = arg;
        }
    }
    if (input_count == 0U) {
        usage(stderr);
        free(input_paths);
        return EXIT_FAILURE;
    }
    if (output != NULL && header_output != NULL && strcmp(output, header_output) == 0) {
        fputs("f2c: C output and interface header must use different files\n", stderr);
        free(input_paths);
        return EXIT_FAILURE;
    }

    sources = (char **)calloc(input_count, sizeof(*sources));
    inputs = (F2cInput *)calloc(input_count, sizeof(*inputs));
    if (sources == NULL || inputs == NULL) {
        fputs("f2c: out of memory\n", stderr);
        status = EXIT_FAILURE;
        goto cleanup;
    }
    for (i = 0; (size_t)i < input_count; ++i) {
        sources[i] = read_file(input_paths[i], &inputs[i].length);
        if (sources[i] == NULL) {
            fprintf(stderr, "f2c: cannot read '%s': %s\n", input_paths[i], strerror(errno));
            status = EXIT_FAILURE;
            goto cleanup;
        }
        inputs[i].source = sources[i];
        inputs[i].options = options;
        inputs[i].options.source_name = input_paths[i];
    }
    result = f2c_transpile_project(inputs, input_count);

    if (result.diagnostics != NULL && result.diagnostics[0] != '\0') {
        fputs(result.diagnostics, stderr);
    }
    if (result.error_count != 0U || result.code == NULL) {
        status = EXIT_FAILURE;
    } else if (write_file(output, result.code) != 0) {
        fprintf(stderr, "f2c: cannot write output: %s\n", strerror(errno));
        status = EXIT_FAILURE;
    } else if (header_output != NULL && write_file(header_output, result.header) != 0) {
        fprintf(stderr, "f2c: cannot write interface header: %s\n", strerror(errno));
        status = EXIT_FAILURE;
    }
    f2c_result_free(&result);

cleanup:
    if (sources != NULL) {
        for (i = 0; (size_t)i < input_count; ++i)
            free(sources[i]);
    }
    free(inputs);
    free(sources);
    free(input_paths);
    return status;
}
