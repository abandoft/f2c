#ifndef F2C_INTERNAL_H
#define F2C_INTERNAL_H

/*
 * Shared umbrella for implementation files that span compiler domains. New
 * modules should include the narrowest domain header directly; no data model
 * is defined here and this header is not a public compatibility surface.
 */
#include "codegen/codegen.h"
#include "frontend/frontend.h"
#include "frontend/pipeline.h"
#include "frontend/token.h"
#include "internal/base.h"
#include "internal/context.h"
#include "ir/expression.h"
#include "ir/statement.h"
#include "ir/type.h"
#include "semantic/intrinsic.h"
#include "semantic/model.h"
#include "semantic/semantic.h"

void f2c_free_unit(Unit *unit);

#endif
