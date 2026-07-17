#include "f2c/f2c.h"
#include "io.h"

/* Thin command-line adapter around the public project transpilation API. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *system_error_message(int error, char *buffer, size_t capacity) {
#if defined(_MSC_VER)
    if (strerror_s(buffer, capacity, error) == 0)
        return buffer;
#else
    const char *message = strerror(error);
    if (message != NULL)
        return message;
#endif
    (void)snprintf(buffer, capacity, "system error %d", error);
    return buffer;
}

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
          "Use '-' as an input path to read standard input, or as an output path to\n"
          "write standard output. File outputs are transactionally replaced only after\n"
          "every requested artifact has been staged successfully.\n\n"
          "Source form is automatic by default: .f/.for/.ftn are fixed form; modern\n"
          "Fortran extensions such as .f90 are free form. Use an override only when a\n"
          "file name does not describe its physical source layout.\n",
          stream);
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
    char error_message[256];
    size_t total_input_bytes = 0U;
    size_t stdin_count = 0U;
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
        } else if (strcmp(arg, "-") == 0) {
            input_paths[input_count++] = arg;
            ++stdin_count;
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
    if (stdin_count > 1U) {
        fputs("f2c: standard input may be specified only once\n", stderr);
        free(input_paths);
        return EXIT_FAILURE;
    }
    if ((output != NULL && header_output != NULL && strcmp(output, header_output) == 0) ||
        ((output == NULL || strcmp(output, "-") == 0) && header_output != NULL &&
         strcmp(header_output, "-") == 0)) {
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
        const size_t remaining = F2C_DEFAULT_MAX_INPUT_BYTES - total_input_bytes;
        sources[i] = f2c_cli_read_source(input_paths[i], remaining, &inputs[i].length);
        if (sources[i] == NULL) {
            fprintf(stderr, "f2c: cannot read '%s': %s\n", input_paths[i],
                    system_error_message(errno, error_message, sizeof(error_message)));
            status = EXIT_FAILURE;
            goto cleanup;
        }
        inputs[i].source = sources[i];
        inputs[i].options = options;
        inputs[i].options.source_name =
            strcmp(input_paths[i], "-") == 0 ? "<stdin>" : input_paths[i];
        total_input_bytes += inputs[i].length;
    }
    result = f2c_transpile_project(inputs, input_count);

    if (result.diagnostics != NULL && result.diagnostics[0] != '\0') {
        fputs(result.diagnostics, stderr);
    }
    if (result.error_count != 0U || result.code == NULL) {
        status = EXIT_FAILURE;
    } else {
        F2cCliArtifact artifacts[2];
        const char *failed_path = NULL;
        size_t artifact_count = 1U;
        artifacts[0].path = output;
        artifacts[0].data = result.code;
        if (header_output != NULL) {
            artifacts[1].path = header_output;
            artifacts[1].data = result.header;
            artifact_count = 2U;
        }
        if (f2c_cli_write_artifacts(artifacts, artifact_count, &failed_path) != 0) {
            fprintf(stderr, "f2c: cannot write '%s': %s\n",
                    failed_path != NULL ? failed_path : "output",
                    system_error_message(errno, error_message, sizeof(error_message)));
            status = EXIT_FAILURE;
        }
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
