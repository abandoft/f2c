#include "frontend/declaration/private.h"

#include <stdlib.h>
#include <string.h>

void f2c_parse_namelist_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = source_line != NULL && source_line->token_count > 1U &&
                           source_line->tokens[0].kind == F2C_TOKEN_NUMBER
                       ? 1U
                       : 0U;
    if (!f2c_line_token_equals(source_line, index, "namelist"))
        return;
    ++index;
    while (index < source_line->token_count) {
        char *name;
        char **members = NULL;
        size_t member_count = 0U;
        size_t member_capacity = 0U;
        F2cNamelistGroup *group;
        if (source_line->tokens[index].kind == F2C_TOKEN_COMMA)
            ++index;
        if (index + 2U >= source_line->token_count ||
            source_line->tokens[index].kind != F2C_TOKEN_OPERATOR ||
            !f2c_token_equals(&source_line->tokens[index], "/") ||
            source_line->tokens[index + 1U].kind != F2C_TOKEN_IDENTIFIER ||
            source_line->tokens[index + 2U].kind != F2C_TOKEN_OPERATOR ||
            !f2c_token_equals(&source_line->tokens[index + 2U], "/")) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      index < source_line->token_count ? &source_line->tokens[index]
                                                                       : NULL,
                                      1, "NAMELIST group must begin with '/name/'");
            return;
        }
        name = f2c_token_text(&source_line->tokens[index + 1U]);
        index += 3U;
        while (index < source_line->token_count) {
            char *member;
            char **replacement;
            if (source_line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
                f2c_token_equals(&source_line->tokens[index], "/"))
                break;
            if (source_line->tokens[index].kind == F2C_TOKEN_COMMA) {
                ++index;
                if (index < source_line->token_count &&
                    source_line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
                    f2c_token_equals(&source_line->tokens[index], "/"))
                    break;
            }
            if (index >= source_line->token_count ||
                source_line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
                f2c_diagnostic(context, source_line->number, 1,
                               "NAMELIST object must be a simple named entity");
                goto namelist_failed;
            }
            member = f2c_token_text(&source_line->tokens[index++]);
            if (member_count == member_capacity) {
                const size_t capacity = member_capacity == 0U ? 4U : member_capacity * 2U;
                replacement =
                    capacity >= member_capacity && capacity <= SIZE_MAX / sizeof(*replacement)
                        ? (char **)realloc(members, capacity * sizeof(*replacement))
                        : NULL;
                if (replacement == NULL) {
                    free(member);
                    goto namelist_failed;
                }
                members = replacement;
                member_capacity = capacity;
            }
            if (member == NULL)
                goto namelist_failed;
            members[member_count++] = member;
            if (index < source_line->token_count &&
                source_line->tokens[index].kind != F2C_TOKEN_COMMA &&
                !(source_line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
                  f2c_token_equals(&source_line->tokens[index], "/"))) {
                f2c_diagnostic(context, source_line->number, 1, "malformed NAMELIST object list");
                goto namelist_failed;
            }
        }
        if (name == NULL || member_count == 0U)
            goto namelist_failed;
        group = f2c_find_namelist(unit, name);
        if (group != NULL) {
            f2c_diagnostic(context, source_line->number, 1, "duplicate NAMELIST group '%s'", name);
            goto namelist_failed;
        }
        if (unit->namelist_count == unit->namelist_capacity) {
            const size_t capacity =
                unit->namelist_capacity == 0U ? 4U : unit->namelist_capacity * 2U;
            F2cNamelistGroup *replacement =
                capacity >= unit->namelist_capacity && capacity <= SIZE_MAX / sizeof(*replacement)
                    ? (F2cNamelistGroup *)realloc(unit->namelists, capacity * sizeof(*replacement))
                    : NULL;
            if (replacement == NULL)
                goto namelist_failed;
            unit->namelists = replacement;
            unit->namelist_capacity = capacity;
        }
        group = &unit->namelists[unit->namelist_count++];
        memset(group, 0, sizeof(*group));
        group->name = name;
        group->members = members;
        group->member_count = member_count;
        for (size_t member = 0U; member < member_count; ++member) {
            if (f2c_ensure_symbol_impl(unit, group->members[member]) == NULL)
                f2c_diagnostic(context, source_line->number, 1,
                               "out of memory recording NAMELIST object");
        }
        continue;

    namelist_failed:
        for (size_t member = 0U; member < member_count; ++member)
            free(members[member]);
        free(members);
        free(name);
        if (context->result.error_count == 0U)
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory parsing NAMELIST declaration");
        return;
    }
}
