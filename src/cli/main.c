#include "f2c/f2c.h"
#include "include.h"
#include "io.h"

/* Thin command-line adapter around the public project transpilation API. */

#include <ctype.h>
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

static void free_definitions(F2cPreprocessorDefinition *definitions, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        free((char *)definitions[index].name);
        free((char *)definitions[index].value);
    }
    free(definitions);
}

static int valid_definition_name(const char *name, size_t length) {
    size_t index;
    if (length == 0U || (isalpha((unsigned char)name[0]) == 0 && name[0] != '_'))
        return 0;
    for (index = 1U; index < length; ++index) {
        if (isalnum((unsigned char)name[index]) == 0 && name[index] != '_')
            return 0;
    }
    return 1;
}

static int define_condition(F2cPreprocessorDefinition *definitions, size_t *count,
                            const char *specification) {
    const char *equals = strchr(specification, '=');
    const size_t name_length =
        equals != NULL ? (size_t)(equals - specification) : strlen(specification);
    const char *value = equals != NULL ? equals + 1 : "1";
    size_t index;
    char *name;
    char *owned_value;
    if (!valid_definition_name(specification, name_length) || strchr(value, '\n') != NULL ||
        strchr(value, '\r') != NULL)
        return 0;
    name = (char *)malloc(name_length + 1U);
    owned_value = (char *)malloc(strlen(value) + 1U);
    if (name == NULL || owned_value == NULL) {
        free(name);
        free(owned_value);
        return -1;
    }
    memcpy(name, specification, name_length);
    name[name_length] = '\0';
    memcpy(owned_value, value, strlen(value) + 1U);
    for (index = 0U; index < *count; ++index) {
        if (strcmp(definitions[index].name, name) == 0) {
            free((char *)definitions[index].name);
            free((char *)definitions[index].value);
            definitions[index].name = name;
            definitions[index].value = owned_value;
            return 1;
        }
    }
    definitions[*count].name = name;
    definitions[*count].value = owned_value;
    ++*count;
    return 1;
}

static int undefine_condition(F2cPreprocessorDefinition *definitions, size_t *count,
                              const char *name) {
    size_t index;
    const size_t length = strlen(name);
    if (!valid_definition_name(name, length))
        return 0;
    for (index = 0U; index < *count; ++index) {
        if (strcmp(definitions[index].name, name) == 0) {
            free((char *)definitions[index].name);
            free((char *)definitions[index].value);
            --*count;
            if (index != *count)
                definitions[index] = definitions[*count];
            break;
        }
    }
    return 1;
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
          "  -I PATH       add a directory for #include resolution\n"
          "  -DNAME[=EXPR] define a case-sensitive conditional-preprocessing name\n"
          "  -UNAME        remove a previously supplied conditional definition\n"
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
    const char **include_paths = NULL;
    F2cPreprocessorDefinition *definitions = NULL;
    size_t definition_count = 0U;
    size_t include_path_count = 0U;
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

    input_paths = (const char **)calloc((size_t)argc * 2U, sizeof(*input_paths));
    definitions = (F2cPreprocessorDefinition *)calloc((size_t)argc, sizeof(*definitions));
    if (input_paths == NULL || definitions == NULL) {
        fputs("f2c: out of memory\n", stderr);
        free(input_paths);
        free(definitions);
        return EXIT_FAILURE;
    }
    include_paths = input_paths + argc;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            free(input_paths);
            free_definitions(definitions, definition_count);
            return EXIT_SUCCESS;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("f2c %s\n", f2c_version());
            free(input_paths);
            free_definitions(definitions, definition_count);
            return EXIT_SUCCESS;
        }
        if (strcmp(arg, "--free-form") == 0) {
            if (options.source_form == F2C_SOURCE_FIXED) {
                fputs("f2c: --free-form conflicts with --fixed-form\n", stderr);
                free(input_paths);
                free_definitions(definitions, definition_count);
                return EXIT_FAILURE;
            }
            options.source_form = F2C_SOURCE_FREE;
        } else if (strcmp(arg, "--fixed-form") == 0) {
            if (options.source_form == F2C_SOURCE_FREE) {
                fputs("f2c: --fixed-form conflicts with --free-form\n", stderr);
                free(input_paths);
                free_definitions(definitions, definition_count);
                return EXIT_FAILURE;
            }
            options.source_form = F2C_SOURCE_FIXED;
        } else if (strcmp(arg, "--comments") == 0) {
            options.emit_source_comments = 1;
        } else if (strncmp(arg, "-I", 2U) == 0) {
            const char *path = arg + 2;
            if (*path == '\0' && ++i < argc)
                path = argv[i];
            if (*path == '\0') {
                fputs("f2c: -I requires a directory\n", stderr);
                free(input_paths);
                free_definitions(definitions, definition_count);
                return EXIT_FAILURE;
            }
            include_paths[include_path_count++] = path;
        } else if (strncmp(arg, "-D", 2U) == 0) {
            const char *specification = arg + 2;
            int definition_status;
            if (*specification == '\0' && ++i < argc)
                specification = argv[i];
            if (*specification == '\0') {
                fputs("f2c: -D requires NAME or NAME=EXPR\n", stderr);
                free(input_paths);
                free_definitions(definitions, definition_count);
                return EXIT_FAILURE;
            }
            definition_status = define_condition(definitions, &definition_count, specification);
            if (definition_status <= 0) {
                fprintf(stderr, "f2c: %s conditional definition: %s\n",
                        definition_status == 0 ? "invalid" : "cannot allocate", specification);
                free(input_paths);
                free_definitions(definitions, definition_count);
                return EXIT_FAILURE;
            }
        } else if (strncmp(arg, "-U", 2U) == 0) {
            const char *name = arg + 2;
            if (*name == '\0' && ++i < argc)
                name = argv[i];
            if (*name == '\0' || !undefine_condition(definitions, &definition_count, name)) {
                fprintf(stderr, "f2c: invalid conditional name for -U: %s\n", name);
                free(input_paths);
                free_definitions(definitions, definition_count);
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-o") == 0) {
            if (++i == argc) {
                fputs("f2c: -o requires a file name\n", stderr);
                free(input_paths);
                free_definitions(definitions, definition_count);
                return EXIT_FAILURE;
            }
            output = argv[i];
        } else if (strcmp(arg, "--header") == 0) {
            if (++i == argc) {
                fputs("f2c: --header requires a file name\n", stderr);
                free(input_paths);
                free_definitions(definitions, definition_count);
                return EXIT_FAILURE;
            }
            header_output = argv[i];
        } else if (strcmp(arg, "-") == 0) {
            input_paths[input_count++] = arg;
            ++stdin_count;
        } else if (arg[0] == '-') {
            fprintf(stderr, "f2c: unknown option: %s\n", arg);
            free(input_paths);
            free_definitions(definitions, definition_count);
            return EXIT_FAILURE;
        } else {
            input_paths[input_count++] = arg;
        }
    }
    if (input_count == 0U) {
        usage(stderr);
        free(input_paths);
        free_definitions(definitions, definition_count);
        return EXIT_FAILURE;
    }
    if (stdin_count > 1U) {
        fputs("f2c: standard input may be specified only once\n", stderr);
        free(input_paths);
        free_definitions(definitions, definition_count);
        return EXIT_FAILURE;
    }
    if ((output != NULL && header_output != NULL && strcmp(output, header_output) == 0) ||
        ((output == NULL || strcmp(output, "-") == 0) && header_output != NULL &&
         strcmp(header_output, "-") == 0)) {
        fputs("f2c: C output and interface header must use different files\n", stderr);
        free(input_paths);
        free_definitions(definitions, definition_count);
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
    {
        F2cConfig config;
        F2cCliIncludeContext include_context;
        memset(&config, 0, sizeof(config));
        include_context.paths = include_paths;
        include_context.count = include_path_count;
        config.structure_size = sizeof(config);
        config.preprocessor_definitions = definitions;
        config.preprocessor_definition_count = definition_count;
        config.include_resolver = f2c_cli_resolve_include;
        config.include_release = f2c_cli_release_include;
        config.include_user_data = &include_context;
        result = f2c_transpile_project_config(inputs, input_count, &config);
    }

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
    free_definitions(definitions, definition_count);
    return status;
}
