#include "ast/statement/private.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *find_data_delimiter(char *text) {
    int quote = 0;
    int depth = 0;
    char *cursor;
    for (cursor = text; *cursor != '\0'; ++cursor) {
        if ((*cursor == '\'' || *cursor == '"') &&
            (quote == 0 || quote == (unsigned char)*cursor)) {
            if (quote != 0 && cursor[1] == *cursor)
                ++cursor;
            else
                quote = quote == 0 ? (unsigned char)*cursor : 0;
        } else if (quote == 0 && *cursor == '(') {
            ++depth;
        } else if (quote == 0 && *cursor == ')') {
            --depth;
        } else if (quote == 0 && depth == 0 && *cursor == '/') {
            return cursor;
        }
    }
    return NULL;
}

static char *find_repeat_separator(char *text) {
    int quote = 0;
    int depth = 0;
    char *cursor;
    for (cursor = text; *cursor != '\0'; ++cursor) {
        if ((*cursor == '\'' || *cursor == '"') &&
            (quote == 0 || quote == (unsigned char)*cursor)) {
            quote = quote == 0 ? (unsigned char)*cursor : 0;
        } else if (quote == 0 && *cursor == '(') {
            ++depth;
        } else if (quote == 0 && *cursor == ')') {
            --depth;
        } else if (quote == 0 && depth == 0 && *cursor == '*') {
            return cursor;
        }
    }
    return NULL;
}

static int parse_data_targets(Unit *unit, char *text, F2cDataGroup *group) {
    char *clean = f2c_trim(text);
    char **targets = NULL;
    size_t count = 0U;
    size_t i;
    if (*clean == '(') {
        group->targets = (F2cIoItem *)calloc(1U, sizeof(*group->targets));
        if (group->targets == NULL || !f2c_statement_parse_io_item(unit, clean, group->targets))
            return 0;
        group->target_count = 1U;
        return 1;
    }
    targets = f2c_split_arguments(clean, &count);
    if (count == 0U)
        return 0;
    group->targets = (F2cIoItem *)calloc(count, sizeof(*group->targets));
    if (group->targets == NULL)
        goto failed;
    for (i = 0U; i < count; ++i) {
        if (!f2c_statement_parse_io_item(unit, targets[i], &group->targets[i]))
            goto failed;
        ++group->target_count;
    }
    while (count != 0U)
        free(targets[--count]);
    free(targets);
    return 1;

failed:
    while (count != 0U)
        free(targets[--count]);
    free(targets);
    return 0;
}

static int parse_data_values(Unit *unit, char *text, F2cDataGroup *group) {
    char **values;
    size_t count = 0U;
    size_t i;
    values = f2c_statement_split_arguments(f2c_trim(text), &count);
    if (count == 0U)
        return 0;
    group->values = (F2cDataValue *)calloc(count, sizeof(*group->values));
    if (group->values == NULL)
        goto failed;
    group->value_count = count;
    for (i = 0U; i < count; ++i) {
        char *clean = f2c_trim(values[i]);
        char *separator = find_repeat_separator(clean);
        char *value_text = clean;
        if (separator != NULL) {
            *separator = '\0';
            group->values[i].repeat = f2c_parse_expression_ast(unit, f2c_trim(clean), NULL);
            value_text = f2c_trim(separator + 1);
        }
        group->values[i].text = f2c_strdup(value_text);
        group->values[i].expression = f2c_parse_expression_ast(unit, value_text, NULL);
        if (group->values[i].text == NULL || group->values[i].expression == NULL ||
            (separator != NULL && group->values[i].repeat == NULL))
            goto failed;
    }
    while (count != 0U)
        free(values[--count]);
    free(values);
    return 1;

failed:
    while (count != 0U)
        free(values[--count]);
    free(values);
    return 0;
}

void f2c_statement_parse_data(Unit *unit, F2cStatement *statement) {
    char *copy = f2c_strdup(statement->text + strlen("data"));
    char *cursor = copy != NULL ? f2c_trim(copy) : NULL;
    if (cursor == NULL)
        return;
    while (*cursor != '\0') {
        char *first_slash = find_data_delimiter(cursor);
        char *second_slash;
        F2cDataGroup *replacement;
        F2cDataGroup *group;
        if (first_slash == NULL)
            break;
        second_slash = find_data_delimiter(first_slash + 1);
        if (second_slash == NULL)
            break;
        replacement =
            (F2cDataGroup *)realloc(statement->data_groups, (statement->data_group_count + 1U) *
                                                                sizeof(*statement->data_groups));
        if (replacement == NULL)
            break;
        statement->data_groups = replacement;
        group = &statement->data_groups[statement->data_group_count];
        memset(group, 0, sizeof(*group));
        ++statement->data_group_count;
        *first_slash = '\0';
        *second_slash = '\0';
        if (!parse_data_targets(unit, cursor, group) ||
            !parse_data_values(unit, first_slash + 1, group))
            break;
        cursor = f2c_trim(second_slash + 1);
        if (*cursor == ',')
            cursor = f2c_trim(cursor + 1);
    }
    free(copy);
}

void f2c_statement_parse_label(Unit *unit, F2cStatement *statement) {
    const char *cursor = statement->text;
    const char *tail;
    while (isdigit((unsigned char)*cursor))
        ++cursor;
    statement->name = f2c_strdup_n(statement->text, (size_t)(cursor - statement->text));
    while (isspace((unsigned char)*cursor))
        ++cursor;
    tail = cursor;
    if (*tail == '\0' || f2c_starts_word(tail, "format"))
        return;
    statement->nested = (F2cStatement *)calloc(1U, sizeof(*statement->nested));
    if (statement->nested != NULL &&
        !f2c_parse_statement(unit, tail, statement->line, statement->nested)) {
        free(statement->nested);
        statement->nested = NULL;
    }
}

int f2c_statement_parse_arithmetic_labels(F2cStatement *statement) {
    char **labels;
    size_t count = 0U;
    size_t i;
    if (statement->tail == NULL)
        return 0;
    labels = f2c_statement_split_arguments(statement->tail, &count);
    if (labels == NULL || count != 3U)
        goto failed;
    for (i = 0U; i < count; ++i) {
        const char *cursor = labels[i];
        if (*cursor == '\0')
            goto failed;
        while (isdigit((unsigned char)*cursor))
            ++cursor;
        if (*cursor != '\0')
            goto failed;
    }
    statement->labels = labels;
    statement->label_count = count;
    statement->kind = F2C_STMT_ARITHMETIC_IF;
    return 1;

failed:
    while (count != 0U)
        free(labels[--count]);
    free(labels);
    return 0;
}
