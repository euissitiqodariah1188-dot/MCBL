#ifndef MCBL_MDC_H
#define MCBL_MDC_H

#include "ast.h"

/* -----------------------------------------------------------------------
   McBL# MDC  –  McBL Development C
   Bridges McBL# and C using:
     extern m { ... }  →  McBL# calls C functions
     extern c { ... }  →  C calls McBL# functions
   ----------------------------------------------------------------------- */

/* Generate C glue code from an extern block */
char *mdc_gen_c_glue(const AstNode *extern_block);

/* Generate McBL# wrapper declarations for C functions */
char *mdc_gen_mcbl_wrappers(const AstNode *extern_block);

/* Full interop code generation: returns a heap string of C code */
char *mdc_generate_interop(const AstNode *program);

#endif /* MCBL_MDC_H */
