/*******************************************************************\

Module: Show claims

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_GOTO_PROGRAMS_SHOW_CLAIMS_H
#define CPROVER_GOTO_PROGRAMS_SHOW_CLAIMS_H

#include <goto-programs/goto_functions.h>
#include <util/namespace.h>

void show_claims(const namespacet &ns, const goto_functionst &goto_functions);

void show_claims(const namespacet &ns, const goto_programt &goto_program);

#endif
