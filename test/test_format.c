#include "ast/format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static F2cFormat *parse(const char *text, F2cFormatError *error) {
    F2cSourceSpan span = {0};
    span.begin.source_name = "format-test.f90";
    span.end.source_name = "format-test.f90";
    span.begin.line = 7U;
    span.end.line = 7U;
    span.begin.column = 11U;
    span.end.column = 11U + strlen(text);
    return f2c_format_parse(text, strlen(text), &span, error);
}

static void test_complete_descriptor_tree(void) {
    static const char source[] =
        "(2(1X,I5.3,'A''B',/),3P,SP,BN,DC,RN,T4,TL2,TR3,DT'BOX'(1,-2,3),:)";
    F2cFormatError error;
    F2cFormat *format = parse(source, &error);
    const F2cFormatNode *group;
    const F2cFormatNode *descriptor;
    expect(format != NULL && error.code == F2C_FORMAT_ERROR_NONE,
           "complete FORMAT descriptor grammar parses into an AST");
    if (format == NULL)
        return;
    expect(format->root.kind == F2C_FORMAT_GROUP && format->root.child_count == 11U,
           "root FORMAT item list preserves every structured item");
    group = &format->root.children[0];
    expect(group->kind == F2C_FORMAT_GROUP && group->repeat == 2U && group->child_count == 4U,
           "nested repeated groups retain their repetition and children");
    descriptor = &group->children[1];
    expect(descriptor->kind == F2C_FORMAT_DATA && descriptor->code[0] == 'I' &&
               descriptor->width == 5 && descriptor->digits == 3 && descriptor->repeat == 1U,
           "integer edit descriptors retain width and minimum digits");
    expect(group->children[2].kind == F2C_FORMAT_LITERAL && group->children[2].text_length == 3U &&
               memcmp(group->children[2].text, "A'B", 3U) == 0,
           "quoted edit descriptors decode doubled delimiters exactly once");
    descriptor = &format->root.children[9];
    expect(descriptor->kind == F2C_FORMAT_DATA && descriptor->code[0] == 'D' &&
               descriptor->code[1] == 'T' && strcmp(descriptor->text, "BOX") == 0 &&
               descriptor->v_list_count == 3U && descriptor->v_list[0] == 1 &&
               descriptor->v_list[1] == -2 && descriptor->v_list[2] == 3,
           "DT descriptors own their iotype and signed v-list without a fixed array");
    expect(format->root.children[10].kind == F2C_FORMAT_COLON,
           "colon control descriptors are explicit AST nodes");
    f2c_format_free(format);
}

static void test_unlimited_and_legacy_literals(void) {
    F2cFormatError error;
    F2cFormat *format = parse("(*(G0,:,1X),5HHELLO)", &error);
    F2cFormat *copy;
    expect(format != NULL && error.code == F2C_FORMAT_ERROR_NONE,
           "unlimited groups and Hollerith edit descriptors parse");
    if (format == NULL)
        return;
    expect(format->root.children[0].kind == F2C_FORMAT_GROUP &&
               format->root.children[0].unlimited &&
               format->root.children[0].children[0].code[0] == 'G' &&
               format->root.children[0].children[0].width == 0,
           "unlimited FORMAT groups preserve G0 and termination controls");
    expect(format->root.children[1].kind == F2C_FORMAT_LITERAL &&
               format->root.children[1].text_length == 5U &&
               memcmp(format->root.children[1].text, "HELLO", 5U) == 0,
           "Hollerith payload boundaries are represented without source rescanning");
    copy = f2c_format_clone(format);
    expect(copy != NULL && copy->root.children != format->root.children &&
               copy->root.children[1].text != format->root.children[1].text &&
               memcmp(copy->root.children[1].text, "HELLO", 5U) == 0,
           "FORMAT AST cloning deep-copies nested and byte payload storage");
    f2c_format_free(copy);
    f2c_format_free(format);
}

static void test_character_literal_and_dynamic_v_list(void) {
    char source[1024];
    size_t length = 0U;
    size_t index;
    F2cFormatError error;
    F2cSourceSpan span = {0};
    F2cFormat *format = f2c_format_parse_character_literal("'(A,''x'',I2)'", &span, &error);
    expect(format != NULL && format->root.child_count == 3U &&
               format->root.children[1].kind == F2C_FORMAT_LITERAL &&
               format->root.children[1].text_length == 1U &&
               format->root.children[1].text[0] == 'x',
           "constant CHARACTER formats decode the outer Fortran literal before parsing");
    f2c_format_free(format);

    length += (size_t)snprintf(source + length, sizeof(source) - length, "(DT'X'(");
    for (index = 0U; index < 70U; ++index)
        length += (size_t)snprintf(source + length, sizeof(source) - length, "%s%zu",
                                   index == 0U ? "" : ",", index);
    length += (size_t)snprintf(source + length, sizeof(source) - length, "))");
    format = f2c_format_parse(source, length, &span, &error);
    expect(format != NULL && format->root.children[0].v_list_count == 70U &&
               format->root.children[0].v_list[69] == 69,
           "DT v-lists use checked dynamic storage beyond the former 32-item ceiling");
    f2c_format_free(format);
}

static void expect_error(const char *source, F2cFormatErrorCode expected, const char *message) {
    F2cFormatError error;
    F2cFormat *format = parse(source, &error);
    expect(format == NULL && error.code == expected, message);
    f2c_format_free(format);
}

static void test_invalid_formats(void) {
    expect_error("I5)", F2C_FORMAT_ERROR_EXPECTED_LEFT_PARENTHESIS,
                 "FORMAT requires its outer parentheses");
    expect_error("(I5", F2C_FORMAT_ERROR_EXPECTED_RIGHT_PARENTHESIS,
                 "unterminated FORMAT groups fail deterministically");
    expect_error("(0(I2))", F2C_FORMAT_ERROR_INVALID_REPEAT, "zero group repetition is rejected");
    expect_error("(I)", F2C_FORMAT_ERROR_INVALID_DESCRIPTOR_FIELD,
                 "required data edit descriptor fields are validated");
    expect_error("(Y4)", F2C_FORMAT_ERROR_INVALID_DESCRIPTOR,
                 "unknown edit descriptors are hard syntax errors");
    expect_error("(DT'X'(1,))", F2C_FORMAT_ERROR_INVALID_DT_LIST,
                 "malformed DT v-lists are rejected without truncation");
    expect_error("(I2) trailing", F2C_FORMAT_ERROR_TRAILING_TEXT,
                 "text after a complete FORMAT specification is rejected");
    expect_error("(42949672960(I2))", F2C_FORMAT_ERROR_INVALID_NUMBER,
                 "overflowing repeat counts fail without wrapping");
}

int main(void) {
    test_complete_descriptor_tree();
    test_unlimited_and_legacy_literals();
    test_character_literal_and_dynamic_v_list();
    test_invalid_formats();
    if (failures != 0) {
        fprintf(stderr, "%d FORMAT AST test(s) failed\n", failures);
        return 1;
    }
    puts("all FORMAT AST tests passed");
    return 0;
}
