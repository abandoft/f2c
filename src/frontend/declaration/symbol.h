#ifndef F2C_FRONTEND_DECLARATION_SYMBOL_H
#define F2C_FRONTEND_DECLARATION_SYMBOL_H

#include "internal/context.h"

/* Local declarations hide host association, but may not redeclare USE entities. */
Symbol *f2c_declaration_symbol(Context *context, Unit *unit, const Line *line,
                               const F2cToken *name_token);
int f2c_reset_associated_symbol(Unit *unit, Symbol *symbol);
int f2c_predeclare_local_bindings(Context *context, Unit *unit);

#endif
