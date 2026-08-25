#ifndef MCBL_UI_H
#define MCBL_UI_H

#include "ast.h"

/* -----------------------------------------------------------------------
   McBL# UI Markup Processor
   Converts <McBL#ui> markup blocks into:
     - HTML/CSS/JS  (for .modcbl files)
     - Native win32 / platform code (for .exe target)
   ----------------------------------------------------------------------- */

typedef enum {
    UI_TARGET_HTML,    /* browser-like output */
    UI_TARGET_NATIVE   /* native window (stub) */
} UiTarget;

typedef struct {
    char  *output;
    size_t len;
    size_t cap;
    UiTarget target;
    int    indent;
} UiGen;

UiGen  *ui_gen_create(UiTarget target);
void    ui_gen_destroy(UiGen *g);

/* Compile a UI page AST node to target format */
int     ui_gen_compile(UiGen *g, const AstNode *ui_node);

/* Write output to file */
int     ui_gen_write(UiGen *g, const char *path);

#endif /* MCBL_UI_H */
