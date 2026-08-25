#ifndef MCBL_CODEGEN_H
#define MCBL_CODEGEN_H

#include "ast.h"
#include "bytecode.h"
#include "symbols.h"

/* -----------------------------------------------------------------------
   McBL# Code Generator  (v2.0 — UPGRADED)
   Three output modes:
     1. Bytecode emission  (MDK VM path)
     2. C transpilation    (GCC → x86_64)
     3. MVM direct         (via MVM native codegen)
   ----------------------------------------------------------------------- */

/* ---- Bytecode code gen ---- */
typedef struct {
    BcChunk  *chunk;
    SymTable *symbols;
    int       error;
    char      errmsg[512];

    /* Break/continue stacks */
    int       break_list[256];     /* enlarged */
    int       break_count;
    int       break_list_start;
    int       continue_list[256];
    int       continue_count;

    /* Try/catch stack */
    int       try_stack[64];
    int       try_depth;

    /* Class context */
    char     *current_class;
    char     *current_method;
    int       in_static_method;

    /* Optimization level 0-3 */
    int       opt_level;

    /* Async context */
    int       in_async;
} BytecodeGen;

BytecodeGen *bcgen_create(void);
void         bcgen_destroy(BytecodeGen *g);
int          bcgen_compile(BytecodeGen *g, const AstNode *program);

/* Set optimization level */
void         bcgen_set_opt(BytecodeGen *g, int level);

/* ---- C transpiler ---- */
typedef struct {
    char  *output;
    size_t len;
    size_t cap;
    int    indent;
    int    error;
    char   errmsg[512];
    int    thread_counter;
    int    label_counter;
    int    tmp_counter;    /* for temp variables */

    /* declared names */
    char  *declared[1024]; /* enlarged */
    int    declared_count;

    /* class definitions emitted */
    char  *classes[256];
    int    class_count;

    /* current class being compiled */
    char   current_class[128];
    int    in_class;
    int    in_static;

    /* try/catch depth */
    int    try_depth;

    /* async function emitting */
    int    in_async;
} CGen;

CGen   *cgen_create(void);
void    cgen_destroy(CGen *g);

/* Transpile AST → C source code stored in g->output */
int     cgen_compile(CGen *g, const AstNode *program);

/* Write generated C source to file, invoke GCC to produce binary */
int     cgen_emit_and_compile(CGen *g, const char *out_c_path,
                               const char *out_bin_path);

/* Optimization pass on generated C (via GCC flags) */
int     cgen_optimize(CGen *g, int level);

/* Emit Windows-compatible binary (MinGW/MSYS2) */
int     cgen_emit_windows(CGen *g, const char *out_c_path,
                           const char *out_exe_path);

/* ---- MVM direct codegen ---- */
typedef struct MvmInstance MvmInstance; /* forward */

typedef struct {
    MvmInstance *mvm;
    SymTable    *symbols;
    int          error;
    char         errmsg[512];
} MvmGen;

MvmGen *mvmgen_create(MvmInstance *mvm);
void    mvmgen_destroy(MvmGen *g);
int     mvmgen_compile(MvmGen *g, const AstNode *program);

#endif /* MCBL_CODEGEN_H */
