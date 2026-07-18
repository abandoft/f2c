#include "codegen/io/private.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static int valid_program_name(const char *name) {
    const unsigned char *cursor = (const unsigned char *)name;
    if (cursor == NULL || (*cursor != '_' && !isalpha(*cursor)))
        return 0;
    while (*++cursor != '\0')
        if (*cursor != '_' && !isalnum(*cursor))
            return 0;
    return 1;
}

static char *v_list_name(const char *program_name, size_t index) {
    Buffer name = {0};
    f2c_buffer_printf(&name, "%s_v_list_%zu", program_name, index);
    return f2c_buffer_take(&name);
}

static int instruction_count(const F2cFormatNode *node, size_t *count) {
    size_t index;
    size_t add = node->kind == F2C_FORMAT_GROUP ? 2U : 1U;
    if (*count > SIZE_MAX - add)
        return 0;
    *count += add;
    for (index = 0U; index < node->child_count; ++index)
        if (!instruction_count(&node->children[index], count))
            return 0;
    return 1;
}

static int emit_v_lists(Context *context, const F2cFormatNode *node, const char *name,
                        size_t *v_list_index, int depth) {
    size_t index;
    if (node->v_list_count != 0U) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "static const int32_t %s_v_list_%zu[] = {", name,
                          *v_list_index);
        for (index = 0U; index < node->v_list_count; ++index)
            f2c_buffer_printf(&context->output, "%s%" PRId32, index == 0U ? "" : ", ",
                              node->v_list[index]);
        f2c_buffer_append(&context->output, "};\n");
        ++*v_list_index;
    }
    for (index = 0U; index < node->child_count; ++index)
        if (!emit_v_lists(context, &node->children[index], name, v_list_index, depth))
            return 0;
    return !context->output.failed;
}

static const char *opcode(F2cFormatNodeKind kind) {
    static const char *const names[] = {
        [F2C_FORMAT_GROUP] = "F2C_FORMAT_OP_GROUP_BEGIN",
        [F2C_FORMAT_DATA] = "F2C_FORMAT_OP_DATA",
        [F2C_FORMAT_LITERAL] = "F2C_FORMAT_OP_LITERAL",
        [F2C_FORMAT_RECORD] = "F2C_FORMAT_OP_RECORD",
        [F2C_FORMAT_COLON] = "F2C_FORMAT_OP_COLON",
        [F2C_FORMAT_SPACE] = "F2C_FORMAT_OP_SPACE",
        [F2C_FORMAT_POSITION] = "F2C_FORMAT_OP_POSITION",
        [F2C_FORMAT_SCALE] = "F2C_FORMAT_OP_SCALE",
        [F2C_FORMAT_SIGN] = "F2C_FORMAT_OP_SIGN",
        [F2C_FORMAT_BLANK] = "F2C_FORMAT_OP_BLANK",
        [F2C_FORMAT_DECIMAL] = "F2C_FORMAT_OP_DECIMAL",
        [F2C_FORMAT_ROUND] = "F2C_FORMAT_OP_ROUND",
    };
    return (size_t)kind < sizeof(names) / sizeof(names[0]) && names[kind] != NULL
               ? names[kind]
               : "F2C_FORMAT_OP_INVALID";
}

static int runtime_control(const F2cFormatNode *node) {
    if (node->kind == F2C_FORMAT_POSITION) {
        if (node->control == F2C_FORMAT_POSITION_LEFT)
            return -1;
        if (node->control == F2C_FORMAT_POSITION_RIGHT)
            return 1;
        return 0;
    }
    if (node->kind == F2C_FORMAT_SIGN)
        return node->control == F2C_FORMAT_SIGN_PLUS;
    if (node->kind == F2C_FORMAT_BLANK)
        return node->control == F2C_FORMAT_BLANK_ZERO;
    if (node->kind == F2C_FORMAT_DECIMAL)
        return node->control == F2C_FORMAT_DECIMAL_COMMA;
    return node->control;
}

static char *instruction_text(const F2cFormatNode *node, size_t *length) {
    Buffer iotype = {0};
    char *text;
    char *literal;
    *length = node->text_length;
    if (node->kind != F2C_FORMAT_DATA || node->code[0] != 'D' || node->code[1] != 'T')
        return node->text != NULL ? f2c_io_c_string_literal(node->text, node->text_length) : NULL;
    f2c_buffer_append(&iotype, "DT");
    if (node->text != NULL)
        f2c_buffer_append_n(&iotype, node->text, node->text_length);
    *length = iotype.length;
    text = f2c_buffer_take(&iotype);
    if (text == NULL)
        return NULL;
    literal = f2c_io_c_string_literal(text, *length);
    free(text);
    return literal;
}

static int emit_instruction(Context *context, const F2cFormatNode *node, const char *name,
                            size_t *v_list_index, int depth, const char *override_opcode) {
    size_t text_length;
    char *literal = instruction_text(node, &text_length);
    char *owned_v_list = NULL;
    const char *v_list = "NULL";
    const int requires_text = node->text != NULL || (node->kind == F2C_FORMAT_DATA &&
                                                     node->code[0] == 'D' && node->code[1] == 'T');
    if (requires_text && literal == NULL)
        return 0;
    if (node->v_list_count != 0U) {
        owned_v_list = v_list_name(name, *v_list_index);
        if (owned_v_list == NULL) {
            free(literal);
            return 0;
        }
        v_list = owned_v_list;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "{.opcode = %s, .repeat = %" PRIu32 "U, .unlimited = %s, .control = %d, "
                      ".code = {'%c', '%c', '\\0'}, .width = %d, .digits = %d, .exponent = %d, "
                      ".text = %s, .text_length = %zuU, .v_list = %s, .v_list_count = %zuU},\n",
                      override_opcode != NULL ? override_opcode : opcode(node->kind),
                      node->repeat != 0U ? node->repeat : 1U, node->unlimited ? "true" : "false",
                      runtime_control(node), node->code[0] != '\0' ? node->code[0] : ' ',
                      node->code[1] != '\0' ? node->code[1] : ' ', node->width, node->digits,
                      node->exponent, literal != NULL ? literal : "NULL", text_length, v_list,
                      node->v_list_count);
    if (node->v_list_count != 0U)
        ++*v_list_index;
    free(owned_v_list);
    free(literal);
    return !context->output.failed;
}

static int emit_nodes(Context *context, const F2cFormatNode *node, const char *name,
                      size_t *v_list_index, int depth) {
    size_t index;
    if (node->kind == F2C_FORMAT_GROUP) {
        if (!emit_instruction(context, node, name, v_list_index, depth, NULL))
            return 0;
        for (index = 0U; index < node->child_count; ++index)
            if (!emit_nodes(context, &node->children[index], name, v_list_index, depth))
                return 0;
        return emit_instruction(context, node, name, v_list_index, depth,
                                "F2C_FORMAT_OP_GROUP_END");
    }
    return emit_instruction(context, node, name, v_list_index, depth, NULL);
}

int f2c_io_emit_format_program(Context *context, const F2cFormat *format, const char *name,
                               int depth) {
    size_t count = 0U;
    size_t v_list_index = 0U;
    size_t index;
    if (context == NULL || format == NULL || !format->validated || !valid_program_name(name))
        return 0;
    for (index = 0U; index < format->root.child_count; ++index)
        if (!instruction_count(&format->root.children[index], &count))
            return 0;
    for (index = 0U; index < format->root.child_count; ++index)
        if (!emit_v_lists(context, &format->root.children[index], name, &v_list_index, depth))
            return 0;
    v_list_index = 0U;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "static const f2c_format_instruction %s[] = {\n", name);
    if (count == 0U) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "{.opcode = F2C_FORMAT_OP_COLON},\n");
    }
    for (index = 0U; index < format->root.child_count; ++index)
        if (!emit_nodes(context, &format->root.children[index], name, &v_list_index, depth + 1))
            return 0;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "};\n");
    return !context->output.failed;
}
