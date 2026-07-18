#include "ast/statement/private.h"

#include <ctype.h>
#include <stdlib.h>

void f2c_statement_parse_label(Unit *unit, F2cStatement *statement) {
    const char *cursor = statement->text;
    const char *tail;
    if (!isdigit((unsigned char)*cursor)) {
        statement->kind = F2C_STMT_INVALID;
        return;
    }
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
    size_t index;
    if (statement->tail == NULL)
        return 0;
    labels = f2c_statement_split_arguments(statement->tail, &count);
    if (labels == NULL || count != 3U)
        goto failed;
    for (index = 0U; index < count; ++index) {
        const char *cursor = labels[index];
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
