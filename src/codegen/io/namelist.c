#include "codegen/io/private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void f2c_io_emit_namelist_value(Context *context, Unit *unit, const char *file,
                                const Symbol *symbol, const char *value,
                                const char *character_length_override, int input,
                                const char *status, int depth) {
    if (symbol->type == TYPE_CHARACTER) {
        char *owned_length =
            character_length_override == NULL ? f2c_symbol_character_length(unit, symbol) : NULL;
        const char *length = character_length_override != NULL
                                 ? character_length_override
                                 : (owned_length != NULL ? owned_length : "1U");
        f2c_io_indent(&context->output, depth);
        if (input) {
            f2c_buffer_printf(&context->output,
                              "%s = f2c_namelist_read_status("
                              "f2c_read_character(%s, %s, (size_t)(%s)));\n",
                              status, file, value, length);
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_namelist_write_character(%s, %s, (size_t)(%s));\n", file, value,
                              length);
        }
        free(owned_length);
    } else if (symbol->type == TYPE_LOGICAL) {
        f2c_io_indent(&context->output, depth);
        if (input) {
            f2c_buffer_append(&context->output,
                              "{ bool f2c_namelist_logical = false; int f2c_namelist_read = ");
            f2c_buffer_printf(&context->output,
                              "f2c_read_bool(%s, &f2c_namelist_logical); %s = "
                              "f2c_namelist_read_status(f2c_namelist_read); if ("
                              "f2c_namelist_read > 0) %s = f2c_namelist_logical ? 1 : 0; }\n",
                              file, status, value);
        } else {
            f2c_buffer_printf(&context->output, "f2c_write_bool(%s, (%s) != 0);\n", file, value);
        }
    } else {
        f2c_io_indent(&context->output, depth);
        if (input)
            f2c_buffer_printf(&context->output,
                              "%s = f2c_namelist_read_status(F2C_READ(%s, &%s));\n", status, file,
                              value);
        else
            f2c_buffer_printf(&context->output, "F2C_WRITE(%s, (%s));\n", file, value);
    }
}

static int emit_unaligned_namelist_value(Context *context, Unit *unit, const char *file,
                                         const Symbol *symbol, const char *offset, int input,
                                         const char *status, int depth) {
    const char *suffix = f2c_unaligned_access_suffix(symbol);
    char *address = f2c_emit_unaligned_linear_address(unit, (Symbol *)symbol, offset);
    if (suffix == NULL || address == NULL) {
        free(address);
        return 0;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "unsigned char *f2c_unaligned_io_address = %s;\n", address);
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "%s f2c_unaligned_io_value = f2c_unaligned_load_%s("
                      "f2c_unaligned_io_address);\n",
                      f2c_symbol_c_type(symbol), suffix);
    f2c_io_emit_namelist_value(context, unit, file, symbol, "f2c_unaligned_io_value", NULL, input,
                               status, depth + 1);
    if (input) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "f2c_unaligned_store_%s(f2c_unaligned_io_address, "
                          "f2c_unaligned_io_value);\n",
                          suffix);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(address);
    return 1;
}

static char *namelist_component_count(Unit *unit, const Symbol *symbol, const char *owner) {
    Buffer count = {0};
    size_t dimension;
    if (symbol->rank == 0U)
        return f2c_strdup("1U");
    if ((symbol->allocatable || symbol->pointer) && owner != NULL) {
        for (dimension = 0U; dimension < symbol->rank; ++dimension)
            f2c_buffer_printf(&count, "%s(size_t)(%s).%s_extent_%zu", dimension == 0U ? "" : " * ",
                              owner, f2c_symbol_c_name(unit, symbol), dimension + 1U);
        return f2c_buffer_take(&count);
    }
    return f2c_symbol_element_count(unit, (Symbol *)symbol);
}

static char *namelist_character_length(Unit *unit, const Symbol *symbol, const char *owner) {
    Buffer length = {0};
    if (symbol->deferred_character && owner != NULL) {
        f2c_buffer_printf(&length, "(size_t)(%s).%s_character_length", owner,
                          f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&length);
    }
    return f2c_symbol_character_length(unit, symbol);
}

static void emit_namelist_autoallocation(Context *context, Unit *unit, const char *file,
                                         const Symbol *symbol, const char *value, const char *owner,
                                         const char *path, const char *status, int depth) {
    const char *type;
    const int body_depth = depth + (symbol->rank != 0U ? 2 : 1);
    size_t dimension;
    if (!symbol->allocatable || symbol->pointer || symbol->procedure_pointer)
        return;
    type = f2c_symbol_c_type(symbol);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (%s == F2C_IO_STATUS_OK && %s == NULL && ", status,
                      value);
    if (symbol->rank == 0U)
        f2c_buffer_printf(&context->output,
                          "f2c_namelist_designator_prefix(%s, f2c_namelist_group_start, %s)) {\n",
                          file, path);
    else
        f2c_buffer_append(&context->output, "true) {\n");
    if (symbol->rank != 0U) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "int64_t f2c_namelist_lower_%zu[%zu], "
                          "f2c_namelist_upper_%zu[%zu];\n",
                          (size_t)depth, symbol->rank, (size_t)depth, symbol->rank);
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_namelist_designator_bounds(%s, "
                          "f2c_namelist_group_start, %s, %zuU, f2c_namelist_lower_%zu, "
                          "f2c_namelist_upper_%zu)) {\n",
                          file, path, symbol->rank, (size_t)depth, (size_t)depth);
        f2c_io_indent(&context->output, body_depth);
        f2c_buffer_printf(&context->output,
                          "size_t f2c_namelist_extents_%zu[%zu] = {0U}; "
                          "size_t f2c_namelist_elements_%zu = 1U;\n",
                          (size_t)depth, symbol->rank, (size_t)depth);
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            f2c_io_indent(&context->output, body_depth);
            f2c_buffer_printf(&context->output,
                              "if (%s == F2C_IO_STATUS_OK && (f2c_namelist_upper_%zu[%zu] < "
                              "f2c_namelist_lower_%zu[%zu] || f2c_namelist_lower_%zu[%zu] < "
                              "INT32_MIN || f2c_namelist_upper_%zu[%zu] > INT32_MAX)) %s = "
                              "F2C_IO_STATUS_RECORD;\n",
                              status, (size_t)depth, dimension, (size_t)depth, dimension,
                              (size_t)depth, dimension, (size_t)depth, dimension, status);
            f2c_io_indent(&context->output, body_depth);
            f2c_buffer_printf(
                &context->output,
                "if (%s == F2C_IO_STATUS_OK) { const uint64_t f2c_namelist_span = "
                "(uint64_t)(f2c_namelist_upper_%zu[%zu] - f2c_namelist_lower_%zu[%zu]) + "
                "UINT64_C(1); if (f2c_namelist_span > (uint64_t)INT32_MAX || "
                "f2c_namelist_span > SIZE_MAX || (f2c_namelist_span != 0U && "
                "f2c_namelist_elements_%zu > SIZE_MAX / (size_t)f2c_namelist_span)) %s = "
                "F2C_IO_STATUS_RECORD; else { f2c_namelist_extents_%zu[%zu] = "
                "(size_t)f2c_namelist_span; f2c_namelist_elements_%zu *= "
                "(size_t)f2c_namelist_span; } }\n",
                status, (size_t)depth, dimension, (size_t)depth, dimension, (size_t)depth, status,
                (size_t)depth, dimension, (size_t)depth);
        }
    } else {
        f2c_io_indent(&context->output, body_depth);
        f2c_buffer_printf(&context->output, "size_t f2c_namelist_elements_%zu = 1U;\n",
                          (size_t)depth);
    }
    if (symbol->type == TYPE_CHARACTER) {
        char *length =
            symbol->deferred_character ? NULL : namelist_character_length(unit, symbol, owner);
        f2c_io_indent(&context->output, body_depth);
        if (symbol->deferred_character)
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_namelist_character_length_%zu = "
                              "f2c_namelist_designator_character_length(%s, "
                              "f2c_namelist_group_start, %s); ",
                              (size_t)depth, file, path);
        else
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_namelist_character_length_%zu = (size_t)(%s); ",
                              (size_t)depth, length != NULL ? length : "0U");
        f2c_buffer_printf(&context->output,
                          "if (f2c_namelist_character_length_%zu != 0U && "
                          "f2c_namelist_elements_%zu > SIZE_MAX / "
                          "f2c_namelist_character_length_%zu) %s = "
                          "F2C_IO_STATUS_RECORD;\n",
                          (size_t)depth, (size_t)depth, (size_t)depth, status);
        free(length);
    }
    f2c_io_indent(&context->output, body_depth);
    f2c_buffer_printf(&context->output, "%s *f2c_namelist_allocation_%zu = NULL;\n", type,
                      (size_t)depth);
    f2c_io_indent(&context->output, body_depth);
    f2c_buffer_printf(&context->output, "if (%s == F2C_IO_STATUS_OK) ", status);
    if (symbol->type == TYPE_CHARACTER)
        f2c_buffer_printf(&context->output,
                          "f2c_namelist_allocation_%zu = (%s *)calloc("
                          "f2c_namelist_elements_%zu == 0U || "
                          "f2c_namelist_character_length_%zu == 0U ? 1U : "
                          "f2c_namelist_elements_%zu * f2c_namelist_character_length_%zu, "
                          "sizeof(%s));\n",
                          (size_t)depth, type, (size_t)depth, (size_t)depth, (size_t)depth,
                          (size_t)depth, type);
    else
        f2c_buffer_printf(&context->output,
                          "f2c_namelist_allocation_%zu = (%s *)calloc("
                          "f2c_namelist_elements_%zu == 0U ? 1U : "
                          "f2c_namelist_elements_%zu, sizeof(%s));\n",
                          (size_t)depth, type, (size_t)depth, (size_t)depth, type);
    f2c_io_indent(&context->output, body_depth);
    f2c_buffer_printf(&context->output,
                      "if (%s == F2C_IO_STATUS_OK && f2c_namelist_allocation_%zu == NULL) "
                      "%s = F2C_IO_STATUS_RECORD;\n",
                      status, (size_t)depth, status);
    if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        f2c_io_indent(&context->output, body_depth);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK) for (size_t "
                          "f2c_namelist_initialize_%zu = 0U; "
                          "f2c_namelist_initialize_%zu < f2c_namelist_elements_%zu; "
                          "++f2c_namelist_initialize_%zu) f2c_initialize_%s("
                          "&f2c_namelist_allocation_%zu[f2c_namelist_initialize_%zu]);\n",
                          status, (size_t)depth, (size_t)depth, (size_t)depth, (size_t)depth,
                          symbol->derived_type->c_name, (size_t)depth, (size_t)depth);
    }
    f2c_io_indent(&context->output, body_depth);
    f2c_buffer_printf(&context->output,
                      "if (%s == F2C_IO_STATUS_OK) %s = f2c_namelist_allocation_%zu;\n", status,
                      value, (size_t)depth);
    if (symbol->type == TYPE_CHARACTER && symbol->deferred_character) {
        f2c_io_indent(&context->output, body_depth);
        if (owner != NULL)
            f2c_buffer_printf(&context->output,
                              "if (%s == F2C_IO_STATUS_OK) (%s).%s_character_length = "
                              "f2c_namelist_character_length_%zu;\n",
                              status, owner, f2c_symbol_c_name(unit, symbol), (size_t)depth);
        else
            f2c_buffer_printf(&context->output,
                              "if (%s == F2C_IO_STATUS_OK) f2c_char_len_%s = "
                              "f2c_namelist_character_length_%zu;\n",
                              status, value, (size_t)depth);
    }
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        f2c_io_indent(&context->output, body_depth);
        if (owner != NULL)
            f2c_buffer_printf(&context->output,
                              "if (%s == F2C_IO_STATUS_OK) { (%s).%s_lower_%zu = "
                              "(int32_t)f2c_namelist_lower_%zu[%zu]; (%s).%s_extent_%zu = "
                              "(int32_t)f2c_namelist_extents_%zu[%zu]; }\n",
                              status, owner, f2c_symbol_c_name(unit, symbol), dimension + 1U,
                              (size_t)depth, dimension, owner, f2c_symbol_c_name(unit, symbol),
                              dimension + 1U, (size_t)depth, dimension);
        else
            f2c_buffer_printf(&context->output,
                              "if (%s == F2C_IO_STATUS_OK) { %s_lower_%zu = "
                              "(int32_t)f2c_namelist_lower_%zu[%zu]; %s_extent_%zu = "
                              "(int32_t)f2c_namelist_extents_%zu[%zu]; }\n",
                              status, value, dimension + 1U, (size_t)depth, dimension, value,
                              dimension + 1U, (size_t)depth, dimension);
    }
    if (symbol->rank != 0U) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "else if (f2c_namelist_designator_prefix(%s, "
                          "f2c_namelist_group_start, %s)) %s = F2C_IO_STATUS_RECORD;\n",
                          file, path, status);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

static void emit_namelist_object(Context *context, Unit *unit, const char *file,
                                 const Symbol *symbol, const char *value, const char *owner,
                                 const char *path, int input, const char *unit_number,
                                 const char *status, int depth, int scalarized, int indirect) {
    const size_t path_id = (size_t)depth;
    const F2cDefinedIoKind namelist_kind =
        input ? F2C_DEFINED_IO_READ_FORMATTED : F2C_DEFINED_IO_WRITE_FORMATTED;
    if (input && !scalarized)
        emit_namelist_autoallocation(context, unit, file, symbol, value, owner, path, status,
                                     depth);
    if (!scalarized && symbol->type == TYPE_DERIVED && symbol->derived_type != NULL &&
        f2c_io_defined_binding(symbol->derived_type, namelist_kind) != NULL) {
        char *count = namelist_component_count(unit, symbol, owner);
        if (input) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (%s == F2C_IO_STATUS_OK && f2c_namelist_member("
                              "%s, f2c_namelist_group_start, %s)) {\n",
                              status, file, path);
            ++depth;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "++f2c_namelist_matched_designators;\n");
        } else {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "(void)f2c_stream_putc(' ', %s); "
                              "f2c_stream_write_string(%s, %s); "
                              "(void)f2c_stream_putc('=', %s);\n",
                              file, file, path, file);
        }
        if (symbol->rank == 0U) {
            Buffer scalar = {0};
            if (indirect)
                f2c_buffer_printf(&scalar, "*(%s)", value);
            else
                f2c_buffer_append(&scalar, value);
            (void)f2c_io_emit_defined_io_call(context, scalar.data, symbol->derived_type,
                                              namelist_kind, unit_number, "\"NAMELIST\"", NULL,
                                              "0U", input ? status : NULL, depth);
            free(scalar.data);
        } else {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_namelist_dtio_%zu = 0U; "
                              "f2c_namelist_dtio_%zu < %s; ++f2c_namelist_dtio_%zu) {\n",
                              path_id, path_id, count != NULL ? count : "0U", path_id);
            {
                Buffer element = {0};
                f2c_buffer_printf(&element, "%s[f2c_namelist_dtio_%zu]", value, path_id);
                (void)f2c_io_emit_defined_io_call(context, element.data, symbol->derived_type,
                                                  namelist_kind, unit_number, "\"NAMELIST\"", NULL,
                                                  "0U", input ? status : NULL, depth + 1);
                free(element.data);
            }
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        if (input) {
            --depth;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        free(count);
        return;
    }
    if (!scalarized && symbol->rank != 0U &&
        ((symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) || symbol->allocatable)) {
        char *count = namelist_component_count(unit, symbol, owner);
        size_t dimension;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_namelist_count_%zu = (size_t)(%s);\n",
                          path_id, count != NULL ? count : "0U");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_namelist_index_%zu = 0U; "
                          "f2c_namelist_index_%zu < f2c_namelist_count_%zu; "
                          "++f2c_namelist_index_%zu) {\n",
                          path_id, path_id, path_id, path_id);
        f2c_io_indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output, "char f2c_namelist_path_%zu[512];\n", path_id);
        f2c_io_indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output,
                          "if (snprintf(f2c_namelist_path_%zu, "
                          "sizeof(f2c_namelist_path_%zu), \"%%s(",
                          path_id, path_id);
        for (dimension = 0U; dimension < symbol->rank; ++dimension)
            f2c_buffer_append(&context->output, dimension == 0U ? "%lld" : ",%lld");
        f2c_buffer_printf(&context->output, ")\", %s", path);
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            char *extent;
            char *lower;
            if ((symbol->allocatable || symbol->pointer) && owner != NULL) {
                Buffer dynamic_extent = {0};
                Buffer dynamic_lower = {0};
                f2c_buffer_printf(&dynamic_extent, "(size_t)(%s).%s_extent_%zu", owner,
                                  f2c_symbol_c_name(unit, symbol), dimension + 1U);
                f2c_buffer_printf(&dynamic_lower, "(int64_t)(%s).%s_lower_%zu", owner,
                                  f2c_symbol_c_name(unit, symbol), dimension + 1U);
                extent = f2c_buffer_take(&dynamic_extent);
                lower = f2c_buffer_take(&dynamic_lower);
            } else {
                extent = f2c_symbol_dimension_extent(unit, symbol, dimension);
                lower = f2c_symbol_dimension_lower(unit, symbol, dimension);
            }
            f2c_buffer_printf(&context->output,
                              ", (long long)((int64_t)(%s) + "
                              "(int64_t)((f2c_namelist_index_%zu / (size_t)(",
                              lower != NULL ? lower : "1", path_id);
            for (size_t prior = 0U; prior < dimension; ++prior) {
                char *prior_extent;
                if ((symbol->allocatable || symbol->pointer) && owner != NULL) {
                    Buffer dynamic = {0};
                    f2c_buffer_printf(&dynamic, "(size_t)(%s).%s_extent_%zu", owner,
                                      f2c_symbol_c_name(unit, symbol), prior + 1U);
                    prior_extent = f2c_buffer_take(&dynamic);
                } else {
                    prior_extent = f2c_symbol_dimension_extent(unit, symbol, prior);
                }
                f2c_buffer_printf(&context->output, "%s(%s)", prior == 0U ? "" : " * ",
                                  prior_extent != NULL ? prior_extent : "1U");
                free(prior_extent);
            }
            if (dimension == 0U)
                f2c_buffer_append(&context->output, "1U");
            f2c_buffer_printf(&context->output, ")) %% (size_t)(%s)))",
                              extent != NULL ? extent : "1U");
            free(extent);
            free(lower);
        }
        f2c_buffer_printf(&context->output, ") < 0) abort();\n");
        {
            Buffer element = {0};
            Buffer element_path = {0};
            if (symbol->type == TYPE_CHARACTER) {
                char *length = namelist_character_length(unit, symbol, owner);
                f2c_buffer_printf(&element, "%s + f2c_namelist_index_%zu * (size_t)(%s)", value,
                                  path_id, length != NULL ? length : "0U");
                free(length);
            } else {
                f2c_buffer_printf(&element, "%s[f2c_namelist_index_%zu]", value, path_id);
            }
            f2c_buffer_printf(&element_path, "f2c_namelist_path_%zu", path_id);
            emit_namelist_object(context, unit, file, symbol, element.data, owner,
                                 element_path.data, input, unit_number, status, depth + 2, 1, 0);
            free(element.data);
            free(element_path.data);
        }
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(count);
        return;
    }
    if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        Buffer object = {0};
        size_t component_index;
        if (indirect)
            f2c_buffer_printf(&object, "*(%s)", value);
        else
            f2c_buffer_append(&object, value);
        for (component_index = 0U; component_index < symbol->derived_type->component_count;
             ++component_index) {
            const Symbol *component = &symbol->derived_type->components[component_index];
            Buffer component_value = {0};
            Buffer component_path = {0};
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{\n");
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output, "char f2c_namelist_path_%zu[512];\n", path_id);
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (snprintf(f2c_namelist_path_%zu, "
                              "sizeof(f2c_namelist_path_%zu), \"%%s%%%%%s\", %s) < 0) abort();\n",
                              path_id, path_id, component->name, path);
            f2c_buffer_printf(&component_value, "(%s).%s", object.data,
                              f2c_symbol_c_name(unit, component));
            f2c_buffer_printf(&component_path, "f2c_namelist_path_%zu", path_id);
            if (input && component->allocatable)
                emit_namelist_autoallocation(context, unit, file, component, component_value.data,
                                             object.data, component_path.data, status, depth + 1);
            if (component->allocatable || component->pointer) {
                f2c_io_indent(&context->output, depth + 1);
                f2c_buffer_printf(&context->output, "if (%s != NULL) {\n", component_value.data);
            }
            emit_namelist_object(
                context, unit, file, component, component_value.data, object.data,
                component_path.data, input, unit_number, status,
                depth + 1 + ((component->allocatable || component->pointer) ? 1 : 0), 0,
                component->rank == 0U && (component->allocatable || component->pointer) &&
                    component->type != TYPE_CHARACTER);
            free(component_value.data);
            free(component_path.data);
            if (component->allocatable || component->pointer) {
                f2c_io_indent(&context->output, depth + 1);
                f2c_buffer_append(&context->output, "}\n");
            }
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        free(object.data);
        return;
    }
    if (input) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK && f2c_namelist_member("
                          "%s, f2c_namelist_group_start, %s)) {\n",
                          status, file, path);
        ++depth;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "++f2c_namelist_matched_designators;\n");
    } else {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "(void)f2c_stream_putc(' ', %s); f2c_stream_write_string(%s, %s); "
                          "(void)f2c_stream_putc('=', %s);\n",
                          file, file, path, file);
    }
    if (symbol->rank == 0U || scalarized) {
        Buffer scalar = {0};
        char *character_length =
            symbol->type == TYPE_CHARACTER ? namelist_character_length(unit, symbol, owner) : NULL;
        if (symbol->equivalence_unaligned && symbol->rank == 0U && !scalarized) {
            if (input)
                f2c_buffer_append(&scalar, value);
            else
                (void)emit_unaligned_namelist_value(context, unit, file, symbol, NULL, input,
                                                    status, depth);
        } else if (indirect && symbol->type != TYPE_CHARACTER)
            f2c_buffer_printf(&scalar, "*(%s)", value);
        else
            f2c_buffer_append(&scalar, value);
        if (!(symbol->equivalence_unaligned && symbol->rank == 0U && !scalarized && !input))
            f2c_io_emit_namelist_value(context, unit, file, symbol, scalar.data, character_length,
                                       input, status, depth);
        free(character_length);
        free(scalar.data);
    } else {
        char *element_count = namelist_component_count(unit, symbol, owner);
        char *character_length =
            symbol->type == TYPE_CHARACTER ? namelist_character_length(unit, symbol, owner) : NULL;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_namelist_value_%zu = 0U; "
                          "f2c_namelist_value_%zu < %s; ++f2c_namelist_value_%zu) {\n",
                          path_id, path_id, element_count != NULL ? element_count : "0U", path_id);
        {
            Buffer element = {0};
            if (symbol->type == TYPE_CHARACTER)
                f2c_buffer_printf(&element, "%s + f2c_namelist_value_%zu * (size_t)(%s)", value,
                                  path_id, character_length != NULL ? character_length : "1U");
            else if (symbol->equivalence_unaligned) {
                if (input)
                    f2c_buffer_printf(&element, "%s[f2c_namelist_value_%zu]", value, path_id);
                else {
                    char offset[80];
                    (void)snprintf(offset, sizeof(offset), "f2c_namelist_value_%zu", path_id);
                    (void)emit_unaligned_namelist_value(context, unit, file, symbol, offset, input,
                                                        status, depth + 1);
                }
            } else
                f2c_buffer_printf(&element, "%s[f2c_namelist_value_%zu]", value, path_id);
            if (!symbol->equivalence_unaligned || input)
                f2c_io_emit_namelist_value(context, unit, file, symbol, element.data,
                                           character_length, input, status, depth + 1);
            free(element.data);
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(element_count);
        free(character_length);
    }
    if (input) {
        --depth;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
}

int f2c_io_emit_namelist(Context *context, Unit *unit, const char *file,
                         const F2cNamelistGroup *group, int input, const char *unit_number,
                         const char *status, int depth) {
    size_t i;
    if (group == NULL)
        return 0;
    if (input) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "long f2c_namelist_start = f2c_stream_tell(%s);\n"
                          "%*slong f2c_namelist_group_start = "
                          "f2c_namelist_group(%s, f2c_namelist_start, \"%s\");\n",
                          file, depth * 4, "", file, group->name);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_namelist_group_start < 0L) %s = "
                          "F2C_IO_STATUS_RECORD;\n",
                          status);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "static const char *const f2c_namelist_members[] = {");
        for (i = 0U; i < group->member_count; ++i) {
            char *member = f2c_io_c_string_literal(group->members[i], strlen(group->members[i]));
            f2c_buffer_printf(&context->output, "%s%s", i == 0U ? "" : ", ",
                              member != NULL ? member : "\"\"");
            free(member);
        }
        f2c_buffer_append(&context->output, "};\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "size_t f2c_namelist_designator_count = 0U, "
                                            "f2c_namelist_matched_designators = 0U;\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK && !f2c_namelist_validate_members("
                          "%s, f2c_namelist_group_start, f2c_namelist_members, "
                          "sizeof(f2c_namelist_members) / sizeof(f2c_namelist_members[0]), "
                          "&f2c_namelist_designator_count)) "
                          "%s = F2C_IO_STATUS_RECORD;\n",
                          status, file, status);
        if (!f2c_io_begin_namelist_transaction(context, unit, group, status, depth))
            return 0;
    } else {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_stream_write_string(%s, \"&%s\");\n", file,
                          group->name);
    }
    for (i = 0U; i < group->member_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[i]);
        Symbol staged_symbol;
        char staged_name[96];
        const char *name;
        char *path;
        if (symbol == NULL)
            continue;
        if (input) {
            staged_symbol = *symbol;
            (void)snprintf(staged_name, sizeof(staged_name), "f2c_namelist_stage_%zu", i);
            staged_symbol.c_name = staged_name;
            symbol = &staged_symbol;
            name = staged_name;
        } else {
            name = f2c_symbol_c_name(unit, symbol);
        }
        path = f2c_io_c_string_literal(group->members[i], strlen(group->members[i]));
        emit_namelist_object(context, unit, file, symbol, name, NULL, path != NULL ? path : "\"\"",
                             input, unit_number, status, depth, 0,
                             symbol->rank == 0U && (symbol->allocatable || symbol->pointer) &&
                                 symbol->type != TYPE_CHARACTER);
        free(path);
    }
    f2c_io_indent(&context->output, depth);
    if (input) {
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK && "
                          "f2c_namelist_matched_designators != "
                          "f2c_namelist_designator_count) %s = F2C_IO_STATUS_RECORD;\n",
                          status, status);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (!f2c_namelist_end(%s, f2c_namelist_group_start)) %s = "
                          "F2C_IO_STATUS_RECORD;\n",
                          file, status);
        if (!f2c_io_end_namelist_transaction(context, unit, group, status, depth))
            return 0;
    } else {
        f2c_buffer_printf(&context->output, "f2c_stream_write_string(%s, \" /\\n\");\n", file);
    }
    return 1;
}
