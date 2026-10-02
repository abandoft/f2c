#ifndef F2C_SEMANTIC_SYMBOL_H
#define F2C_SEMANTIC_SYMBOL_H

#include "semantic/model.h"

/* Release the owned payload without invalidating the symbol's table slot. */
void f2c_discard_symbol(Symbol *symbol);

#endif
