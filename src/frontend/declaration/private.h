#ifndef F2C_FRONTEND_DECLARATION_PRIVATE_H
#define F2C_FRONTEND_DECLARATION_PRIVATE_H

#include "frontend/private.h"

typedef struct F2cDeclarationTypeSpec {
    Type type;
    Type kind_type;
    int kind;
    int polymorphic;
    size_t end;
    char *derived_type_name;
    char *character_length;
    F2cTokenRange character_length_syntax;
} F2cDeclarationTypeSpec;

int f2c_parse_type_spec_tokens(Context *context, Unit *unit, const Line *line, size_t begin,
                               F2cDeclarationTypeSpec *specification);
void f2c_release_type_spec(F2cDeclarationTypeSpec *specification);
Type f2c_kind_type_from_tokens(Unit *unit, const Line *line, size_t begin, size_t end);
void f2c_parse_entity_declaration(Context *context, Unit *unit, Line *source_line);

#endif
