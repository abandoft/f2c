#ifndef F2C_FRONTEND_MODULE_ACCESS_H
#define F2C_FRONTEND_MODULE_ACCESS_H

#include "semantic/model.h"

void f2c_parse_access_statements(Context *context, Unit *unit);
void f2c_finalize_module_accessibility(Context *context, Unit *module);

int f2c_module_symbol_is_public(const Unit *module, const Symbol *symbol);
int f2c_module_derived_type_is_public(const Unit *module, const char *local_name,
                                      const F2cDerivedType *derived);
int f2c_module_procedure_is_public(const Unit *module, const Unit *procedure);

#endif
