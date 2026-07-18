#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_space(const char *cursor) {
    while (isspace((unsigned char)*cursor) != 0)
        ++cursor;
    return cursor;
}

static void include_diagnostic(Preprocessor *preprocessor, F2cDiagnosticCode code, size_t line,
                               size_t column, const char *format, const char *name) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    ++span.end.column;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, format, name);
}

static int extension_equal(const char *extension, const char *expected) {
    while (*extension != '\0' && *expected != '\0') {
        if (tolower((unsigned char)*extension++) != tolower((unsigned char)*expected++))
            return 0;
    }
    return *extension == '\0' && *expected == '\0';
}

static F2cSourceForm detected_form(const char *source_name, F2cSourceForm parent_form) {
    const char *extension = source_name != NULL ? strrchr(source_name, '.') : NULL;
    if (extension == NULL)
        return parent_form;
    if (extension_equal(extension, ".f") || extension_equal(extension, ".for") ||
        extension_equal(extension, ".ftn"))
        return F2C_SOURCE_FIXED;
    if (extension_equal(extension, ".f90") || extension_equal(extension, ".f95") ||
        extension_equal(extension, ".f03") || extension_equal(extension, ".f08"))
        return F2C_SOURCE_FREE;
    return parent_form;
}

static int include_cycle(const PreprocessorIncludeFrame *frame, const char *source_name) {
    for (; frame != NULL; frame = frame->parent) {
        if (strcmp(frame->source_name, source_name) == 0)
            return 1;
    }
    return 0;
}

int f2c_preprocessor_process_include(Preprocessor *preprocessor, const char *rest, size_t line,
                                     size_t column) {
    const char *cursor = skip_space(rest);
    const char opening = *cursor;
    char closing = '\0';
    const char *name_begin;
    const char *name_end;
    char *requested_name = NULL;
    F2cIncludeRequest request;
    F2cIncludeSource source;
    F2cIncludeStatus status;
    F2cSourceForm form;
    const char *source_name;
    int result = 0;
    int release_source = 0;
    if (opening == '"' || opening == '\'')
        closing = opening;
    else if (opening == '<')
        closing = '>';
    if (closing == '\0') {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line,
                           column + (size_t)(cursor - rest),
                           "expected a quoted or system include name%s", "");
        return 0;
    }
    name_begin = ++cursor;
    while (*cursor != '\0' && *cursor != closing)
        ++cursor;
    name_end = cursor;
    if (*cursor != closing || name_end == name_begin ||
        (*skip_space(cursor + 1) != '\0' && *skip_space(cursor + 1) != '!')) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line,
                           column + (size_t)(cursor - rest), "malformed #include operand%s", "");
        return 0;
    }
    requested_name = f2c_strdup_n(name_begin, (size_t)(name_end - name_begin));
    if (requested_name == NULL) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                           "out of memory parsing include '%s'", "");
        return 0;
    }
    if (preprocessor->context->include_resolver == NULL) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_UNSUPPORTED, line, column,
                           "#include requires a configured resolver: '%s'", requested_name);
        goto cleanup;
    }
    if (preprocessor->include_depth >= preprocessor->context->limits.max_include_depth) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                           "include depth limit exceeded for '%s'", requested_name);
        goto cleanup;
    }
    if (preprocessor->context->include_file_count >=
        preprocessor->context->limits.max_include_files) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                           "include file limit exceeded for '%s'", requested_name);
        goto cleanup;
    }
    request.including_source_name = preprocessor->current_source_name;
    request.requested_name = requested_name;
    request.kind = opening == '<' ? F2C_INCLUDE_SYSTEM : F2C_INCLUDE_QUOTED;
    memset(&source, 0, sizeof(source));
    status = preprocessor->context->include_resolver(&request, &source,
                                                     preprocessor->context->include_user_data);
    if (status == F2C_INCLUDE_NOT_FOUND) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_INCLUDE, line, column,
                           "include file not found: '%s'", requested_name);
        goto cleanup;
    }
    if (status != F2C_INCLUDE_FOUND) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_INCLUDE, line, column,
                           "include resolver failed for '%s'", requested_name);
        goto cleanup;
    }
    release_source = 1;
    if (source.source == NULL && source.length != 0U) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_INVALID_ARGUMENT, line, column,
                           "include resolver returned a null buffer for '%s'", requested_name);
        goto cleanup;
    }
    if (source.source != NULL && memchr(source.source, '\0', source.length) != NULL) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_INVALID_ARGUMENT, line, column,
                           "include resolver returned an embedded NUL for '%s'", requested_name);
        goto cleanup;
    }
    if (source.source_form != F2C_SOURCE_AUTO && source.source_form != F2C_SOURCE_FREE &&
        source.source_form != F2C_SOURCE_FIXED) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_INVALID_ARGUMENT, line, column,
                           "include resolver returned an invalid source form for '%s'",
                           requested_name);
        goto cleanup;
    }
    if (source.length >
        preprocessor->context->limits.max_input_bytes - preprocessor->context->input_bytes) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                           "project input limit exceeded by include '%s'", requested_name);
        goto cleanup;
    }
    source_name = f2c_context_source_name(
        preprocessor->context, source.source_name != NULL && source.source_name[0] != '\0'
                                   ? source.source_name
                                   : requested_name);
    if (source_name == NULL) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                           "out of memory recording include '%s'", requested_name);
        goto cleanup;
    }
    if (include_cycle(preprocessor->include_parent, source_name)) {
        include_diagnostic(preprocessor, F2C_DIAGNOSTIC_INCLUDE, line, column,
                           "recursive include cycle for '%s'", requested_name);
        goto cleanup;
    }
    ++preprocessor->context->include_file_count;
    preprocessor->context->input_bytes += source.length;
    form = source.source_form == F2C_SOURCE_AUTO
               ? detected_form(source_name, preprocessor->source_form)
               : source.source_form;
    result = f2c_preprocessor_process_buffer(
        preprocessor, source.source != NULL ? source.source : "", source.length, form, source_name,
        preprocessor->include_depth + 1U, preprocessor->include_parent, preprocessor->output,
        preprocessor->output_source_map);

cleanup:
    if (release_source && preprocessor->context->include_release != NULL)
        preprocessor->context->include_release(&source, preprocessor->context->include_user_data);
    free(requested_name);
    return result;
}
