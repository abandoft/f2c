#include "codegen/io/namelist/state.h"
#include "codegen/io/private.h"

#include <stdio.h>
#include <stdlib.h>

static int dynamic_storage(const Symbol *symbol) {
    return symbol->allocatable || (symbol->pointer && !symbol->procedure_pointer);
}

static int pointer_storage(const Symbol *symbol) {
    return symbol->pointer && !symbol->procedure_pointer;
}

static int character_storage(const Symbol *symbol) { return symbol->type == TYPE_CHARACTER; }

static int derived_storage(const Symbol *symbol) {
    return symbol->type == TYPE_DERIVED && symbol->derived_type != NULL;
}

static void stage_name(char *buffer, size_t capacity, size_t member) {
    (void)snprintf(buffer, capacity, "f2c_namelist_stage_%zu", member);
}

static void emit_target_reference(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                                  int depth) {
    if (f2c_namelist_has_descriptor_state(symbol)) {
        if (!f2c_namelist_emit_state_snapshot(context, unit, symbol, member, depth))
            context->output.failed = 1;
        return;
    }
    const char *name = f2c_symbol_c_name(unit, symbol);
    const char *type = f2c_symbol_c_type(symbol);
    size_t dimension;

    if (!symbol->equivalence_unaligned) {
        f2c_io_indent(&context->output, depth);
        if (dynamic_storage(symbol))
            f2c_buffer_printf(&context->output, "%s **f2c_namelist_target_%zu = &%s;\n", type,
                              member, name);
        else if (symbol->rank != 0U || character_storage(symbol))
            f2c_buffer_printf(&context->output, "%s *f2c_namelist_target_%zu = %s;\n", type, member,
                              name);
        else
            f2c_buffer_printf(&context->output, "%s *f2c_namelist_target_%zu = &%s;\n", type,
                              member, name);
    }
    if (!dynamic_storage(symbol))
        return;
    if (symbol->deferred_character) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "size_t *f2c_namelist_target_character_length_%zu = "
                          "&f2c_char_len_%s;\n",
                          member, name);
    }
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "int32_t *f2c_namelist_target_lower_%zu_%zu = &%s_lower_%zu;\n", member,
                          dimension + 1U, name, dimension + 1U);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "int32_t *f2c_namelist_target_extent_%zu_%zu = &%s_extent_%zu;\n", member,
                          dimension + 1U, name, dimension + 1U);
        if (pointer_storage(symbol)) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "ptrdiff_t *f2c_namelist_target_stride_%zu_%zu = "
                              "&%s_stride_%zu;\n",
                              member, dimension + 1U, name, dimension + 1U);
        }
    }
}

static void emit_original_count(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                                const char *status, int depth) {
    size_t dimension;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t f2c_namelist_original_count_%zu = 1U;\n", member);
    if (symbol->rank == 0U)
        return;
    if (dynamic_storage(symbol)) {
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (*f2c_namelist_target_extent_%zu_%zu < 0 || "
                              "!f2c_namelist_size_product(f2c_namelist_original_count_%zu, "
                              "(size_t)*f2c_namelist_target_extent_%zu_%zu, "
                              "&f2c_namelist_original_count_%zu)) %s = F2C_IO_STATUS_RECORD;\n",
                              member, dimension + 1U, member, member, dimension + 1U, member,
                              status);
        }
    } else {
        char *count = f2c_symbol_element_count(unit, (Symbol *)symbol);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_namelist_original_count_%zu = (size_t)(%s);\n",
                          member, count != NULL ? count : "0U");
        free(count);
    }
}

static char *stage_character_length(Unit *unit, const Symbol *symbol, size_t member) {
    Buffer result = {0};
    if (symbol->deferred_character) {
        f2c_buffer_printf(&result, "f2c_char_len_f2c_namelist_stage_%zu", member);
        return f2c_buffer_take(&result);
    }
    return f2c_symbol_character_length(unit, symbol);
}

static void emit_dynamic_metadata(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                                  int depth) {
    char name[96];
    size_t dimension;
    (void)unit;
    stage_name(name, sizeof(name), member);
    if (symbol->deferred_character) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "size_t f2c_char_len_%s = "
                          "*f2c_namelist_target_character_length_%zu;\n",
                          name, member);
    }
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "int32_t %s_lower_%zu = *f2c_namelist_target_lower_%zu_%zu;\n", name,
                          dimension + 1U, member, dimension + 1U);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "int32_t %s_extent_%zu = *f2c_namelist_target_extent_%zu_%zu;\n", name,
                          dimension + 1U, member, dimension + 1U);
        if (pointer_storage(symbol)) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "ptrdiff_t %s_stride_%zu = "
                              "*f2c_namelist_target_stride_%zu_%zu;\n",
                              name, dimension + 1U, member, dimension + 1U);
        }
    }
}

static void emit_unaligned_stage(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                                 const char *status, int depth) {
    char name[96];
    const char *type = f2c_symbol_c_type(symbol);
    const char *suffix = f2c_unaligned_access_suffix(symbol);
    char *load;
    stage_name(name, sizeof(name), member);
    if (suffix == NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = F2C_IO_STATUS_RECORD;\n", status);
        return;
    }
    if (symbol->rank == 0U) {
        load = f2c_emit_unaligned_linear_load(unit, (Symbol *)symbol, "0U");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s %s = %s;\n", type, name, load != NULL ? load : "0");
        free(load);
        return;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "%s *%s = (%s *)malloc(f2c_namelist_original_count_%zu == 0U ? "
                      "1U : f2c_namelist_original_count_%zu * sizeof(*%s));\n",
                      type, name, type, member, member, name);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (%s == NULL) %s = F2C_IO_STATUS_RECORD; else for (size_t i = 0U; "
                      "i < f2c_namelist_original_count_%zu; ++i) %s[i] = "
                      "f2c_unaligned_load_%s(",
                      name, status, member, name, suffix);
    {
        char *address = f2c_emit_unaligned_linear_address(unit, (Symbol *)symbol, "i");
        f2c_buffer_printf(&context->output, "%s);\n", address != NULL ? address : "NULL");
        free(address);
    }
}

static void emit_stage(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                       const char *status, int depth) {
    char name[96];
    const char *type = f2c_symbol_c_type(symbol);
    char *length = NULL;
    stage_name(name, sizeof(name), member);

    if (symbol->equivalence_unaligned) {
        emit_unaligned_stage(context, unit, symbol, member, status, depth);
        return;
    }
    if (dynamic_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s *%s = NULL;\n", type, name);
        emit_dynamic_metadata(context, unit, symbol, member, depth);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (*f2c_namelist_target_%zu != NULL && %s == "
                          "F2C_IO_STATUS_OK) {\n",
                          member, status);
        if (character_storage(symbol)) {
            length = stage_character_length(unit, symbol, member);
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(
                &context->output,
                "size_t bytes = 0U; if (!f2c_namelist_size_product("
                "f2c_namelist_original_count_%zu, (size_t)(%s), &bytes)) "
                "%s = F2C_IO_STATUS_RECORD; else { %s = (%s *)malloc(bytes == 0U ? 1U : "
                "bytes); if (%s == NULL) %s = F2C_IO_STATUS_RECORD; else if (bytes != 0U) "
                "memmove(%s, *f2c_namelist_target_%zu, bytes); }\n",
                member, length != NULL ? length : "0U", status, name, type, name, status, name,
                member);
        } else if (derived_storage(symbol) && symbol->polymorphic) {
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "%s = f2c_clone_dynamic_%s(*f2c_namelist_target_%zu, "
                              "f2c_namelist_original_count_%zu); if (%s == NULL) %s = "
                              "F2C_IO_STATUS_RECORD;\n",
                              name, symbol->derived_type->c_name, member, member, name, status);
        } else {
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (f2c_namelist_original_count_%zu > SIZE_MAX / sizeof(*%s)) "
                              "%s = F2C_IO_STATUS_RECORD; else { %s = (%s *)calloc("
                              "f2c_namelist_original_count_%zu == 0U ? 1U : "
                              "f2c_namelist_original_count_%zu, sizeof(*%s)); if (%s == NULL) "
                              "%s = F2C_IO_STATUS_RECORD; }\n",
                              member, name, status, name, type, member, member, name, name, status);
            if (derived_storage(symbol)) {
                f2c_io_indent(&context->output, depth + 1);
                f2c_buffer_printf(&context->output,
                                  "if (%s == F2C_IO_STATUS_OK) for (size_t i = 0U; "
                                  "i < f2c_namelist_original_count_%zu; ++i) "
                                  "f2c_clone_%s(&%s[i], &(*f2c_namelist_target_%zu)[i]);\n",
                                  status, member, symbol->derived_type->c_name, name, member);
            } else {
                f2c_io_indent(&context->output, depth + 1);
                f2c_buffer_printf(
                    &context->output,
                    "if (%s == F2C_IO_STATUS_OK && f2c_namelist_original_count_%zu != 0U) "
                    "memmove(%s, *f2c_namelist_target_%zu, "
                    "f2c_namelist_original_count_%zu * sizeof(*%s));\n",
                    status, member, name, member, member, name);
            }
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(length);
        return;
    }
    if (symbol->rank == 0U && !character_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        if (derived_storage(symbol)) {
            f2c_buffer_printf(&context->output, "%s %s = {0};\n", type, name);
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "f2c_clone_%s(&%s, f2c_namelist_target_%zu);\n",
                              symbol->derived_type->c_name, name, member);
        } else {
            f2c_buffer_printf(&context->output, "%s %s = *f2c_namelist_target_%zu;\n", type, name,
                              member);
        }
        return;
    }
    length = character_storage(symbol) ? stage_character_length(unit, symbol, member) : NULL;
    f2c_io_indent(&context->output, depth);
    if (character_storage(symbol)) {
        f2c_buffer_printf(
            &context->output,
            "size_t f2c_namelist_stage_bytes_%zu = 0U; "
            "if (!f2c_namelist_size_product(f2c_namelist_original_count_%zu, "
            "(size_t)(%s), &f2c_namelist_stage_bytes_%zu)) %s = F2C_IO_STATUS_RECORD;\n",
            member, member, length != NULL ? length : "0U", member, status);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "%s *%s = (%s *)malloc(f2c_namelist_stage_bytes_%zu == 0U ? 1U : "
                          "f2c_namelist_stage_bytes_%zu);\n",
                          type, name, type, member, member);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(
            &context->output,
            "if (%s == NULL) %s = F2C_IO_STATUS_RECORD; else if ("
            "f2c_namelist_stage_bytes_%zu != 0U) memmove(%s, f2c_namelist_target_%zu, "
            "f2c_namelist_stage_bytes_%zu);\n",
            name, status, member, name, member, member);
    } else {
        f2c_buffer_printf(&context->output,
                          "%s *%s = (%s *)calloc(f2c_namelist_original_count_%zu == 0U ? 1U : "
                          "f2c_namelist_original_count_%zu, sizeof(*%s));\n",
                          type, name, type, member, member, name);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (%s == NULL) %s = F2C_IO_STATUS_RECORD;\n", name,
                          status);
        if (derived_storage(symbol)) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (%s == F2C_IO_STATUS_OK) for (size_t i = 0U; "
                              "i < f2c_namelist_original_count_%zu; ++i) "
                              "f2c_clone_%s(&%s[i], &f2c_namelist_target_%zu[i]);\n",
                              status, member, symbol->derived_type->c_name, name, member);
        } else {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(
                &context->output,
                "if (%s == F2C_IO_STATUS_OK && f2c_namelist_original_count_%zu != 0U) "
                "memmove(%s, f2c_namelist_target_%zu, "
                "f2c_namelist_original_count_%zu * sizeof(*%s));\n",
                status, member, name, member, member, name);
        }
    }
    free(length);
}

static void emit_root_binding(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                              const char *status, int depth) {
    char name[96];
    const char *original;
    char original_buffer[96];
    (void)unit;
    stage_name(name, sizeof(name), member);
    if (symbol->equivalence_unaligned)
        return;
    if (dynamic_storage(symbol)) {
        (void)snprintf(original_buffer, sizeof(original_buffer), "*f2c_namelist_target_%zu",
                       member);
        original = original_buffer;
    } else {
        (void)snprintf(original_buffer, sizeof(original_buffer), "f2c_namelist_target_%zu", member);
        original = original_buffer;
    }
    if (symbol->rank == 0U && !dynamic_storage(symbol) && !character_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK && !f2c_namelist_transaction_bind("
                          "&f2c_namelist_transaction_state, %s, &%s)) %s = F2C_IO_STATUS_RECORD;\n",
                          status, original, name, status);
        return;
    } else {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(
            &context->output,
            "if (%s == F2C_IO_STATUS_OK && %s != NULL && %s != NULL && "
            "!f2c_namelist_transaction_bind(&f2c_namelist_transaction_state, %s, %s)) "
            "%s = F2C_IO_STATUS_RECORD;\n",
            status, original, name, original, name, status);
    }
    if (!derived_storage(symbol) || symbol->polymorphic)
        return;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(
        &context->output,
        "if (%s == F2C_IO_STATUS_OK && %s != NULL && %s != NULL) for (size_t i = 0U; "
        "i < f2c_namelist_original_count_%zu; ++i) if (!f2c_namelist_transaction_bind("
        "&f2c_namelist_transaction_state, &(%s)[i], &%s[i])) { %s = "
        "F2C_IO_STATUS_RECORD; break; }\n",
        status, original, name, member, original, name, status);
}

static void emit_stage_fields(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                              const char *status, int depth) {
    char name[96];
    const char *original;
    char original_buffer[96];
    if (!derived_storage(symbol) || symbol->equivalence_unaligned)
        return;
    (void)unit;
    stage_name(name, sizeof(name), member);
    if (dynamic_storage(symbol))
        (void)snprintf(original_buffer, sizeof(original_buffer), "*f2c_namelist_target_%zu",
                       member);
    else
        (void)snprintf(original_buffer, sizeof(original_buffer), "f2c_namelist_target_%zu", member);
    original = original_buffer;
    if (symbol->polymorphic) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK && %s != NULL && %s != NULL && "
                          "!f2c_namelist_stage_dynamic_%s(%s, %s, f2c_namelist_original_count_%zu, "
                          "&f2c_namelist_transaction_state)) %s = F2C_IO_STATUS_RECORD;\n",
                          status, original, name, symbol->derived_type->c_name, name, original,
                          member, status);
        return;
    }
    if (symbol->rank == 0U && !dynamic_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK && !f2c_namelist_stage_fields_%s("
                          "&%s, %s, &f2c_namelist_transaction_state)) %s = F2C_IO_STATUS_RECORD;\n",
                          status, symbol->derived_type->c_name, name, original, status);
        return;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (%s == F2C_IO_STATUS_OK && %s != NULL && %s != NULL) for (size_t i = 0U; "
                      "i < f2c_namelist_original_count_%zu; ++i) if (!f2c_namelist_stage_fields_%s("
                      "&%s[i], &(%s)[i], &f2c_namelist_transaction_state)) { %s = "
                      "F2C_IO_STATUS_RECORD; break; }\n",
                      status, original, name, member, symbol->derived_type->c_name, name, original,
                      status);
}

int f2c_io_begin_namelist_transaction(Context *context, Unit *unit, const F2cNamelistGroup *group,
                                      const char *status, int depth) {
    size_t member;
    if (context == NULL || unit == NULL || group == NULL || status == NULL)
        return 0;
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL)
            emit_target_reference(context, unit, symbol, member, depth);
    }
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL &&
            (symbol->rank != 0U || dynamic_storage(symbol) || character_storage(symbol)))
            emit_original_count(context, unit, symbol, member, status, depth);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    ++depth;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "f2c_namelist_transaction f2c_namelist_transaction_state;\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "f2c_namelist_transaction_initialize(&f2c_namelist_transaction_state);\n");
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL)
            emit_stage(context, unit, symbol, member, status, depth);
    }
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL)
            emit_root_binding(context, unit, symbol, member, status, depth);
    }
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL)
            emit_stage_fields(context, unit, symbol, member, status, depth);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (%s == F2C_IO_STATUS_OK) {\n", status);
    return 1;
}

static void emit_finish_count(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                              const char *status, int depth) {
    char name[96];
    size_t dimension;
    (void)unit;
    stage_name(name, sizeof(name), member);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "size_t f2c_namelist_finish_count_%zu = 1U;\n", member);
    if (symbol->rank == 0U)
        return;
    if (!dynamic_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "f2c_namelist_finish_count_%zu = "
                          "f2c_namelist_original_count_%zu;\n",
                          member, member);
        return;
    }
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s_extent_%zu < 0 || !f2c_namelist_size_product("
                          "f2c_namelist_finish_count_%zu, (size_t)%s_extent_%zu, "
                          "&f2c_namelist_finish_count_%zu)) %s = F2C_IO_STATUS_RECORD;\n",
                          name, dimension + 1U, member, name, dimension + 1U, member, status);
    }
}

static void emit_rebind_root(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                             const char *status, int depth) {
    char name[96];
    const char *original;
    char original_buffer[96];
    (void)unit;
    stage_name(name, sizeof(name), member);
    if (!derived_storage(symbol) || symbol->equivalence_unaligned)
        return;
    if (dynamic_storage(symbol))
        (void)snprintf(original_buffer, sizeof(original_buffer), "*f2c_namelist_target_%zu",
                       member);
    else
        (void)snprintf(original_buffer, sizeof(original_buffer), "f2c_namelist_target_%zu", member);
    original = original_buffer;
    if (symbol->polymorphic) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(
            &context->output,
            "if (%s == F2C_IO_STATUS_OK && %s != NULL && %s != NULL) "
            "f2c_namelist_rebind_dynamic_%s(%s, %s, f2c_namelist_finish_count_%zu, %zuU);\n",
            status, original, name, symbol->derived_type->c_name, name, original, member,
            symbol->rank);
        return;
    }
    if (symbol->rank == 0U && !dynamic_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s == F2C_IO_STATUS_OK) f2c_namelist_rebind_fields_%s("
                          "&%s, %s);\n",
                          status, symbol->derived_type->c_name, name, original);
        return;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (%s == F2C_IO_STATUS_OK && %s != NULL && %s != NULL) for (size_t i = 0U; "
                      "i < f2c_namelist_finish_count_%zu; ++i) "
                      "f2c_namelist_rebind_fields_%s(&%s[i], &(%s)[i]);\n",
                      status, original, name, member, symbol->derived_type->c_name, name, original);
}

static void emit_unaligned_commit(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                                  int depth) {
    char name[96];
    const char *suffix = f2c_unaligned_access_suffix(symbol);
    char *address;
    stage_name(name, sizeof(name), member);
    if (suffix == NULL)
        return;
    if (symbol->rank == 0U) {
        address = f2c_emit_unaligned_linear_address(unit, (Symbol *)symbol, "0U");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_unaligned_store_%s(%s, %s);\n", suffix,
                          address != NULL ? address : "NULL", name);
        free(address);
        return;
    }
    address = f2c_emit_unaligned_linear_address(unit, (Symbol *)symbol, "i");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "for (size_t i = 0U; i < f2c_namelist_finish_count_%zu; ++i) "
                      "f2c_unaligned_store_%s(%s, %s[i]);\n",
                      member, suffix, address != NULL ? address : "NULL", name);
    free(address);
}

static void emit_commit_root(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                             int depth) {
    char name[96];
    const char *type = f2c_symbol_c_type(symbol);
    size_t dimension;
    stage_name(name, sizeof(name), member);
    if (symbol->equivalence_unaligned) {
        emit_unaligned_commit(context, unit, symbol, member, depth);
        return;
    }
    if (symbol->allocatable) {
        if (derived_storage(symbol)) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (*f2c_namelist_target_%zu != NULL) %s_%s("
                              "*f2c_namelist_target_%zu, f2c_namelist_original_count_%zu, %zuU);\n",
                              member,
                              symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                              symbol->derived_type->c_name, member, member, symbol->rank);
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "free(*f2c_namelist_target_%zu); "
                          "*f2c_namelist_target_%zu = %s; %s = NULL;\n",
                          member, member, name, name);
        if (symbol->deferred_character) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "*f2c_namelist_target_character_length_%zu = "
                              "f2c_char_len_%s;\n",
                              member, name);
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "*f2c_namelist_target_lower_%zu_%zu = %s_lower_%zu; "
                              "*f2c_namelist_target_extent_%zu_%zu = %s_extent_%zu;\n",
                              member, dimension + 1U, name, dimension + 1U, member, dimension + 1U,
                              name, dimension + 1U);
        }
        if (f2c_namelist_has_descriptor_state(symbol) &&
            !f2c_namelist_emit_state_commit(context, unit, symbol, member, depth))
            context->output.failed = 1;
        return;
    }
    if (pointer_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s != NULL && *f2c_namelist_target_%zu != NULL) {\n", name, member);
        if (derived_storage(symbol)) {
            f2c_io_indent(&context->output, depth + 1);
            if (symbol->polymorphic)
                f2c_buffer_printf(&context->output,
                                  "f2c_copy_dynamic_%s(*f2c_namelist_target_%zu, %s, "
                                  "f2c_namelist_finish_count_%zu);\n",
                                  symbol->derived_type->c_name, member, name, member);
            else
                f2c_buffer_printf(&context->output,
                                  "for (size_t i = 0U; i < f2c_namelist_finish_count_%zu; ++i) "
                                  "f2c_copy_%s(&(*f2c_namelist_target_%zu)[i], &%s[i]);\n",
                                  member, symbol->derived_type->c_name, member, name);
        } else if (character_storage(symbol)) {
            char *length = stage_character_length(unit, symbol, member);
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(
                &context->output,
                "size_t bytes = 0U; if (f2c_namelist_size_product("
                "f2c_namelist_finish_count_%zu, (size_t)(%s), &bytes) && bytes != 0U) "
                "memmove(*f2c_namelist_target_%zu, %s, bytes);\n",
                member, length != NULL ? length : "0U", member, name);
            free(length);
        } else {
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (f2c_namelist_finish_count_%zu != 0U) memmove("
                              "*f2c_namelist_target_%zu, %s, f2c_namelist_finish_count_%zu * "
                              "sizeof(%s));\n",
                              member, member, name, member, type);
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        return;
    }
    if (symbol->rank == 0U && !character_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        if (derived_storage(symbol)) {
            if (symbol->polymorphic)
                f2c_buffer_printf(&context->output,
                                  "f2c_copy_dynamic_%s(f2c_namelist_target_%zu, &%s, 1U);\n",
                                  symbol->derived_type->c_name, member, name);
            else
                f2c_buffer_printf(&context->output, "f2c_copy_%s(f2c_namelist_target_%zu, &%s);\n",
                                  symbol->derived_type->c_name, member, name);
        } else
            f2c_buffer_printf(&context->output, "*f2c_namelist_target_%zu = %s;\n", member, name);
        return;
    }
    f2c_io_indent(&context->output, depth);
    if (derived_storage(symbol)) {
        if (symbol->polymorphic)
            f2c_buffer_printf(&context->output,
                              "f2c_copy_dynamic_%s(f2c_namelist_target_%zu, %s, "
                              "f2c_namelist_finish_count_%zu);\n",
                              symbol->derived_type->c_name, member, name, member);
        else
            f2c_buffer_printf(&context->output,
                              "for (size_t i = 0U; i < f2c_namelist_finish_count_%zu; ++i) "
                              "f2c_copy_%s(&f2c_namelist_target_%zu[i], &%s[i]);\n",
                              member, symbol->derived_type->c_name, member, name);
    } else if (character_storage(symbol)) {
        char *length = stage_character_length(unit, symbol, member);
        f2c_buffer_printf(&context->output,
                          "{ size_t bytes = 0U; if (f2c_namelist_size_product("
                          "f2c_namelist_finish_count_%zu, (size_t)(%s), &bytes) && bytes != 0U) "
                          "memmove(f2c_namelist_target_%zu, %s, bytes); }\n",
                          member, length != NULL ? length : "0U", member, name);
        free(length);
    } else {
        f2c_buffer_printf(
            &context->output,
            "if (f2c_namelist_finish_count_%zu != 0U) memmove("
            "f2c_namelist_target_%zu, %s, f2c_namelist_finish_count_%zu * sizeof(%s));\n",
            member, member, name, member, type);
    }
}

static void emit_cleanup_root(Context *context, Unit *unit, const Symbol *symbol, size_t member,
                              int depth) {
    char name[96];
    (void)unit;
    stage_name(name, sizeof(name), member);
    if (symbol->rank == 0U && !dynamic_storage(symbol) && !character_storage(symbol) &&
        !symbol->equivalence_unaligned) {
        if (derived_storage(symbol)) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "f2c_destroy_%s(&%s);\n",
                              symbol->derived_type->c_name, name);
        }
        return;
    }
    if (derived_storage(symbol)) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (%s != NULL) %s_%s(%s, f2c_namelist_finish_count_%zu, "
                          "%zuU);\n",
                          name, symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                          symbol->derived_type->c_name, name, member, symbol->rank);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "free(%s);\n", name);
}

int f2c_io_end_namelist_transaction(Context *context, Unit *unit, const F2cNamelistGroup *group,
                                    const char *status, int depth) {
    size_t member;
    if (context == NULL || unit == NULL || group == NULL || status == NULL)
        return 0;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL && (symbol->rank != 0U || pointer_storage(symbol) ||
                               (character_storage(symbol) && !symbol->allocatable) ||
                               (symbol->allocatable && derived_storage(symbol))))
            emit_finish_count(context, unit, symbol, member, status, depth);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (%s == F2C_IO_STATUS_OK) {\n", status);
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL)
            emit_rebind_root(context, unit, symbol, member, status, depth + 1);
    }
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "f2c_namelist_transaction_commit(&f2c_namelist_transaction_state);\n");
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL)
            emit_commit_root(context, unit, symbol, member, depth + 1);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    for (member = 0U; member < group->member_count; ++member) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[member]);
        if (symbol != NULL)
            emit_cleanup_root(context, unit, symbol, member, depth);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "f2c_namelist_transaction_destroy(&f2c_namelist_transaction_state);\n");
    --depth;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    return 1;
}
