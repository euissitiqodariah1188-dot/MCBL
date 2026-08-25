#ifndef MCBL_PARSER_H
#define MCBL_PARSER_H

#include "lexer.h"
#include "ast.h"

/* -----------------------------------------------------------------------
   McBL# Parser  –  builds an AST from the token stream
   ----------------------------------------------------------------------- */

typedef struct {
    const Token *tokens;
    size_t       count;
    size_t       pos;
    int          error;          /* non-zero if a parse error occurred */
    char         errmsg[512];
} Parser;

Parser  *parser_create(const Token *tokens, size_t count);
void     parser_destroy(Parser *p);

/* Parse the full token stream → returns an AST_PROGRAM node (caller frees) */
AstNode *parser_parse(Parser *p);

#endif /* MCBL_PARSER_H */
