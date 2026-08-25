#include "codegen.h"
#include "ast.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* -----------------------------------------------------------------------
   McBL# Bytecode Generator  –  walks AST and emits BcChunk instructions
   ----------------------------------------------------------------------- */

BytecodeGen *bcgen_create(void) {
    BytecodeGen *g = (BytecodeGen *)mcbl_calloc(1, sizeof(BytecodeGen));
    g->chunk = bc_chunk_create();
    return g;
}

void bcgen_destroy(BytecodeGen *g) {
    if (!g) return;
    bc_chunk_destroy(g->chunk);
    g->chunk = NULL;
    mcbl_free((void **)&g);
}

static void bcgen_node(BytecodeGen *g, const AstNode *n);

static void bcgen_list(BytecodeGen *g, const AstList *l) {
    for (size_t i = 0; i < l->count; i++)
        bcgen_node(g, l->items[i]);
}

static void bcgen_expr(BytecodeGen *g, const AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_EXPR_INT:    bc_emit_i(g->chunk, OP_BC_PUSH_INT,   n->ival); break;
        case AST_EXPR_FLOAT:  bc_emit_f(g->chunk, OP_BC_PUSH_FLOAT, n->fval); break;
        case AST_EXPR_STRING: bc_emit_s(g->chunk, OP_BC_PUSH_STR,   n->sval ? n->sval : ""); break;
        case AST_EXPR_BOOL:   bc_emit_i(g->chunk, OP_BC_PUSH_BOOL,  n->bval); break;
        case AST_EXPR_IDENT:  bc_emit_s(g->chunk, OP_BC_LOAD,       n->sval ? n->sval : ""); break;
        case AST_EXPR_BINOP: {
            bcgen_expr(g, n->binop.left);
            bcgen_expr(g, n->binop.right);
            BytecodeOp op;
            switch (n->binop.op) {
                case OP_ADD:    op = OP_BC_ADD;    break;
                case OP_SUB:    op = OP_BC_SUB;    break;
                case OP_MUL:    op = OP_BC_MUL;    break;
                case OP_DIV:    op = OP_BC_DIV;    break;
                case OP_MOD:    op = OP_BC_MOD;    break;
                case OP_EQ:     op = OP_BC_EQ;     break;
                case OP_NEQ:    op = OP_BC_NEQ;    break;
                case OP_LT:     op = OP_BC_LT;     break;
                case OP_GT:     op = OP_BC_GT;     break;
                case OP_LE:     op = OP_BC_LE;     break;
                case OP_GE:     op = OP_BC_GE;     break;
                case OP_AND:    op = OP_BC_AND;    break;
                case OP_OR:     op = OP_BC_OR;     break;
                case OP_CONCAT: op = OP_BC_CONCAT; break;
                default:        op = OP_BC_ADD;    break;
            }
            bc_emit(g->chunk, op);
            break;
        }
        case AST_EXPR_UNOP:
            bcgen_expr(g, n->binop.left);
            if (n->binop.op == OP_NOT) bc_emit(g->chunk, OP_BC_NOT);
            else                       bc_emit(g->chunk, OP_BC_NEG);
            break;
        case AST_EXPR_CALL:
            for (size_t i = 0; i < n->call.args.count; i++)
                bcgen_expr(g, n->call.args.items[i]);
            if (n->call.target && strcmp(n->call.target, "inputxt") == 0)
                bc_emit(g->chunk, OP_BC_INPUT);
            else
                bc_emit_s(g->chunk, OP_BC_CALL, n->call.target ? n->call.target : "");
            break;
        default:
            bcgen_node(g, n);
            break;
    }
}

static void bcgen_node(BytecodeGen *g, const AstNode *n) {
    if (!n || g->error) return;
    switch (n->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            bcgen_list(g, &n->block.stmts);
            break;

        case AST_INC_DECL:
            bc_emit_s(g->chunk, OP_BC_INC_ENTER, n->inc.name ? n->inc.name : "");
            bcgen_list(g, &n->inc.body);
            bc_emit(g->chunk, OP_BC_INC_EXIT);
            break;

        case AST_DEV_DECL:
            /* Dev functions are emitted in a second pass after main code */
            break;

        case AST_PR_STMT:
            for (size_t i = 0; i < n->call.args.count; i++)
                bcgen_expr(g, n->call.args.items[i]);
            bc_emit_i(g->chunk, OP_BC_PRINT, (int64_t)n->call.args.count);
            break;

        case AST_VAR_DECL:
        case AST_REG_VAR_DECL:
        case AST_CONSTEXPR_DECL:
            bcgen_expr(g, n->var.value);
            bc_emit_s(g->chunk, OP_BC_STORE, n->var.name ? n->var.name : "");
            break;

        case AST_ORGATE_DECL:
            bcgen_expr(g, n->var.value);
            bc_emit_s(g->chunk, OP_BC_STORE_ORGATE, n->var.name ? n->var.name : "");
            break;

        case AST_ASSIGN:
            bcgen_expr(g, n->assign.value);
            bc_emit_s(g->chunk, OP_BC_STORE, n->assign.name ? n->assign.name : "");
            break;

        case AST_IF_STMT: {
            bcgen_expr(g, n->if_stmt.cond);
            int jf = bc_emit_i(g->chunk, OP_BC_JMP_FALSE, 0);
            bcgen_list(g, &n->if_stmt.then_body);
            int je = bc_emit_i(g->chunk, OP_BC_JMP, 0);
            bc_patch_jump(g->chunk, (size_t)jf);
            bcgen_list(g, &n->if_stmt.else_body);
            bc_patch_jump(g->chunk, (size_t)je);
            break;
        }

        case AST_FOR_STMT: {
            if (n->for_stmt.var_name && n->for_stmt.start)
                bcgen_expr(g, n->for_stmt.start);
            else
                bc_emit_i(g->chunk, OP_BC_PUSH_INT, 0);
            bc_emit_s(g->chunk, OP_BC_STORE, n->for_stmt.var_name ? n->for_stmt.var_name : "i");

            size_t loop_top = g->chunk->count;
            bc_emit_s(g->chunk, OP_BC_LOAD,    n->for_stmt.var_name ? n->for_stmt.var_name : "i");
            bcgen_expr(g, n->for_stmt.end);
            bc_emit(g->chunk, OP_BC_LT);
            int jf = bc_emit_i(g->chunk, OP_BC_JMP_FALSE, 0);

            bcgen_list(g, &n->for_stmt.body);

            /* increment */
            bc_emit_s(g->chunk, OP_BC_LOAD, n->for_stmt.var_name ? n->for_stmt.var_name : "i");
            if (n->for_stmt.step) bcgen_expr(g, n->for_stmt.step);
            else bc_emit_i(g->chunk, OP_BC_PUSH_INT, 1);
            bc_emit(g->chunk, OP_BC_ADD);
            bc_emit_s(g->chunk, OP_BC_STORE, n->for_stmt.var_name ? n->for_stmt.var_name : "i");

            bc_emit_i(g->chunk, OP_BC_JMP, (int64_t)loop_top);
            bc_patch_jump(g->chunk, (size_t)jf);
            break;
        }

        case AST_LOOP_STMT:
        case AST_WHILE_STMT: {
            size_t loop_top = g->chunk->count;
            /* Save break list start */
            int break_list[64];
            int break_count = 0;
            g->break_list_start = break_count;

            if (n->loop_stmt.cond) {
                bcgen_expr(g, n->loop_stmt.cond);
                int jf = bc_emit_i(g->chunk, OP_BC_JMP_FALSE, 0);
                bcgen_list(g, &n->loop_stmt.body);
                bc_emit_i(g->chunk, OP_BC_JMP, (int64_t)loop_top);
                bc_patch_jump(g->chunk, (size_t)jf);
            } else {
                bcgen_list(g, &n->loop_stmt.body);
                bc_emit_i(g->chunk, OP_BC_JMP, (int64_t)loop_top);
            }
            /* Patch all breaks to jump here */
            for (int i = 0; i < g->break_count; i++) {
                bc_patch_jump(g->chunk, (size_t)g->break_list[i]);
            }
            g->break_count = 0;
            break;
        }

        case AST_BREAK_STMT:
            /* Emit break jump - will be patched after loop */
            if (g->break_count < 64) {
                g->break_list[g->break_count++] = (int)g->chunk->count;
            }
            bc_emit_i(g->chunk, OP_BC_BREAK, 0);
            break;

        case AST_RETURN_STMT:
            if (n->ret.value) { bcgen_expr(g, n->ret.value); bc_emit(g->chunk, OP_BC_RET_VAL); }
            else               bc_emit(g->chunk, OP_BC_RET);
            break;

        case AST_CONTINUE_STMT:
            /* continue is handled by the loop's jmp instruction */
            break;

        case AST_USE_STMT:
            bc_emit_s(g->chunk, OP_BC_USE, n->call.target ? n->call.target : "");
            break;

        case AST_WAIT_FOR_STMT:
            bc_emit_s(g->chunk, OP_BC_WAIT_FOR, n->call.target ? n->call.target : "");
            break;

        case AST_RESPONSE_STMT:
            bc_emit(g->chunk, OP_BC_RESPONSE);
            break;

        case AST_INPUTXT_STMT:
            /* Statement form: just pop the prompt, don't read stdin yet */
            for (size_t i = 0; i < n->call.args.count; i++)
                bcgen_expr(g, n->call.args.items[i]);
            bc_emit(g->chunk, OP_BC_POP); /* discard prompt */
            break;

        case AST_THREAD_STMT:
            bc_emit(g->chunk, OP_BC_THREAD_SPAWN);
            bcgen_list(g, &n->thread.body);
            bc_emit(g->chunk, OP_BC_THREAD_JOIN);
            break;

        case AST_CARGO_M_STMT:
            bc_emit_i(g->chunk, OP_BC_CARGO_CREATE, n->cargo.size);
            break;

        case AST_CLEAR_CARGO_STMT:
            bc_emit(g->chunk, OP_BC_CARGO_CLEAR);
            break;

        case AST_AUTOCLEAR_STMT:
            bc_emit_i(g->chunk, OP_BC_CARGO_FREE, 1);
            break;

        case AST_USING_CPU_STMT:
            bc_emit(g->chunk, OP_BC_MOV_REG);
            break;

        case AST_MOV_STMT:
            bcgen_expr(g, n->mov.value);
            bc_emit_s(g->chunk, OP_BC_MOV_REG, n->mov.reg ? n->mov.reg : "EAX");
            break;

        case AST_SEND_STMT:
            bc_emit_s(g->chunk, OP_BC_SEND,
                      n->send.data_name ? n->send.data_name : "");
            break;

        case AST_READFILE_STMT:
            if (n->call.args.count > 0) bcgen_expr(g, n->call.args.items[0]);
            bc_emit(g->chunk, OP_BC_READFILE);
            break;

        case AST_CREATEWINDOW_STMT:
            for (size_t i = 0; i < n->call.args.count; i++)
                bcgen_expr(g, n->call.args.items[i]);
            bc_emit(g->chunk, OP_BC_CREATEWIN);
            break;

        case AST_OPEN_PAGE_STMT:
            bc_emit_s(g->chunk, OP_BC_OPENPAGE, n->call.target ? n->call.target : "");
            break;

        case AST_EXTERN_M_BLOCK:
            bc_emit_s(g->chunk, OP_BC_EXTERN_M, n->ext.raw_code ? n->ext.raw_code : "");
            break;

        case AST_EXTERN_C_BLOCK:
            bc_emit_s(g->chunk, OP_BC_EXTERN_C, n->ext.raw_code ? n->ext.raw_code : "");
            break;

        case AST_SPLIT_STRING_STMT:
            bcgen_expr(g, n->str_op.str);
            bcgen_expr(g, n->str_op.delim);
            bc_emit_s(g->chunk, OP_BC_SPLIT, n->str_op.result ? n->str_op.result : "");
            break;

        case AST_COMBINE_STRING_STMT:
            bcgen_expr(g, n->str_op.str);
            bcgen_expr(g, n->str_op.delim);
            bc_emit(g->chunk, OP_BC_COMBINE);
            break;

        case AST_CALL_STMT:
        case AST_EXPR_CALL:
            for (size_t i = 0; i < n->call.args.count; i++)
                bcgen_expr(g, n->call.args.items[i]);
            if (n->call.target && strcmp(n->call.target, "inputxt") == 0)
                bc_emit(g->chunk, OP_BC_INPUT);
            else
                bc_emit_s(g->chunk, OP_BC_CALL, n->call.target ? n->call.target : "");
            break;

        case AST_NOP:
        default:
            break;
    }
}

/* Forward declare */
static void bcgen_emit_dev(BytecodeGen *g, const AstNode *n);

/* Recursively find and emit dev function declarations */
static void bcgen_collect_devs(BytecodeGen *g, const AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < n->block.stmts.count; i++)
                bcgen_collect_devs(g, n->block.stmts.items[i]);
            break;
        case AST_INC_DECL:
            for (size_t i = 0; i < n->inc.body.count; i++)
                bcgen_collect_devs(g, n->inc.body.items[i]);
            break;
        case AST_DEV_DECL:
            bcgen_emit_dev(g, n);
            break;
        default:
            break;
    }
}

static void bcgen_emit_dev(BytecodeGen *g, const AstNode *n) {
    const char *fname = n->dev.name ? n->dev.name : "";
    symtable_insert(g->symbols, fname, SYM_FUNC, '#');
    Symbol *sym = symtable_lookup(g->symbols, fname);
    if (sym) sym->kind = SYM_FUNC;

    int func_start = (int)g->chunk->count;
    if (sym) sym->func_addr = func_start;

    /* Store params from stack into local variables (reverse order - stack is LIFO) */
    for (size_t i = n->dev.params.count; i > 0; i--) {
        const AstNode *param = n->dev.params.items[i - 1];
        if (param && param->kind == AST_EXPR_IDENT) {
            bc_emit_s(g->chunk, OP_BC_STORE,
                      param->sval ? param->sval : "_param");
        }
    }
    bcgen_list(g, &n->dev.body);

    /* If no explicit return, push 0 and return */
    bc_emit_i(g->chunk, OP_BC_PUSH_INT, 0);
    bc_emit(g->chunk, OP_BC_RET_VAL);
}

int bcgen_compile(BytecodeGen *g, const AstNode *program) {
    if (!g || !program) return -1;
    /* Pass 1: emit main code (dev declarations are skipped) */
    bcgen_node(g, program);
    bc_emit(g->chunk, OP_BC_HALT);
    /* Pass 2: emit dev function bodies after HALT */
    bcgen_collect_devs(g, program);
    return g->error ? -1 : 0;
}

/* -----------------------------------------------------------------------
   McBL# C Transpiler  –  AST → C source → GCC → x86_64 binary
   ----------------------------------------------------------------------- */

#define CGEN_INIT_CAP 65536

CGen *cgen_create(void) {
    CGen *g = (CGen *)mcbl_calloc(1, sizeof(CGen));
    g->output = (char *)mcbl_malloc(CGEN_INIT_CAP);
    g->output[0] = '\0';
    g->cap    = CGEN_INIT_CAP;
    g->len    = 0;
    g->indent = 0;
    return g;
}

void cgen_destroy(CGen *g) {
    if (!g) return;
    mcbl_free((void **)&g->output);
    mcbl_free((void **)&g);
}

static void cgen_grow(CGen *g, size_t extra) {
    while (g->len + extra + 1 >= g->cap) {
        g->cap   *= 2;
        g->output = (char *)mcbl_realloc(g->output, g->cap);
    }
}

static void cgen_write(CGen *g, const char *fmt, ...) {
    va_list ap;
    char    tmp[4096];
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    cgen_grow(g, (size_t)n + 1);
    memcpy(g->output + g->len, tmp, (size_t)n);
    g->len += (size_t)n;
    g->output[g->len] = '\0';
}

static void cgen_indent(CGen *g) {
    for (int i = 0; i < g->indent; i++) cgen_write(g, "    ");
}

static void cgen_line(CGen *g, const char *fmt, ...) {
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    cgen_indent(g);
    cgen_write(g, "%s\n", tmp);
}

/* Track declared variable names so re-assignment doesn't redeclare (which would shadow and cause infinite loops) */
static int cgen_is_declared(CGen *g, const char *name) {
    if (!g || !name) return 0;
    for (int i = 0; i < g->declared_count; i++)
        if (g->declared[i] && strcmp(g->declared[i], name) == 0) return 1;
    return 0;
}

static void cgen_mark_declared(CGen *g, const char *name) {
    if (!g || !name) return;
    if (cgen_is_declared(g, name)) return;
    if (g->declared_count < 256)
        g->declared[g->declared_count++] = strdup(name);
}

static void cgen_reset_decls(CGen *g) {
    if (!g) return;
    for (int i = 0; i < g->declared_count; i++) { free(g->declared[i]); g->declared[i] = NULL; }
    g->declared_count = 0;
}

/* Escape a C string literal */
static void cgen_string_escaped(CGen *g, const char *s) {
    cgen_write(g, "\"");
    if (!s) { cgen_write(g, "\""); return; }
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  cgen_write(g, "\\\""); break;
            case '\\': cgen_write(g, "\\\\"); break;
            case '\n': cgen_write(g, "\\n");  break;
            case '\t': cgen_write(g, "\\t");  break;
            default:   cgen_write(g, "%c", *p); break;
        }
    }
    cgen_write(g, "\"");
}

/* Choose the right MCBL_MK* macro based on expression type */
static const char *mcbl_mk_macro(const AstNode *expr) {
    if (!expr) return "MCBL_MKVAL";
    switch (expr->kind) {
        case AST_EXPR_STRING: return "MCBL_MKSTR";
        case AST_EXPR_FLOAT:  return "MCBL_MKFLO";
        case AST_EXPR_BOOL:   return "mcbl_mkbool_v";
        case AST_EXPR_BINOP:
        case AST_EXPR_CALL:
        case AST_EXPR_UNOP:
        case AST_EXPR_INDEX:
        case AST_EXPR_MEMBER:
        case AST_EXPR_IDENT: return "";
        default:              return "MCBL_MKVAL";
    }
}

/* Forward decls for recursive calls */
static void cgen_node(CGen *g, const AstNode *n);
static void cgen_expr(CGen *g, const AstNode *n);

static void cgen_expr(CGen *g, const AstNode *n) {
    if (!n) { cgen_write(g, "0"); return; }
    switch (n->kind) {
        case AST_EXPR_INT:   cgen_write(g, "%lld", n->ival); break;
        case AST_EXPR_FLOAT: cgen_write(g, "%g",   n->fval); break;
        case AST_EXPR_BOOL:  cgen_write(g, "%s",   n->bval ? "1" : "0"); break;
        case AST_EXPR_STRING:
            cgen_string_escaped(g, n->sval);
            break;
        case AST_EXPR_IDENT:
            if (n->sval) {
                /* Map register names */
                if (strcmp(n->sval, "__mcbl_eax") == 0 || strcmp(n->sval, "EAX") == 0)
                    cgen_write(g, "mcbl_var_EAX");
                else if (strcmp(n->sval, "__mcbl_ebx") == 0 || strcmp(n->sval, "EBX") == 0)
                    cgen_write(g, "mcbl_var_EBX");
                else if (strcmp(n->sval, "__mcbl_ecx") == 0 || strcmp(n->sval, "ECX") == 0)
                    cgen_write(g, "mcbl_var_ECX");
                else if (strcmp(n->sval, "__mcbl_edx") == 0 || strcmp(n->sval, "EDX") == 0)
                    cgen_write(g, "mcbl_var_EDX");
                else
                    cgen_write(g, "mcbl_var_%s", n->sval);
            } else {
                cgen_write(g, "mcbl_var__");
            }
            break;
        case AST_EXPR_BINOP: {
            if (n->binop.op == OP_CONCAT) {
                cgen_write(g, "mcbl_mkstr_v(mcbl_concat(");
                if (n->binop.left->kind == AST_EXPR_STRING) {
                    cgen_string_escaped(g, n->binop.left->sval);
                } else {
                    cgen_write(g, "mcbl_to_cstr(");
                    cgen_expr(g, n->binop.left);
                    cgen_write(g, ")");
                }
                cgen_write(g, ", ");
                if (n->binop.right->kind == AST_EXPR_STRING) {
                    cgen_string_escaped(g, n->binop.right->sval);
                } else {
                    cgen_write(g, "mcbl_to_cstr(");
                    cgen_expr(g, n->binop.right);
                    cgen_write(g, ")");
                }
                cgen_write(g, "))");
                return;
            }
            /* Use runtime helper functions for McblValue arithmetic */
            const char *fn = NULL;
            switch (n->binop.op) {
                case OP_ADD:  fn = "mcbl_op_add";  break;
                case OP_SUB:  fn = "mcbl_op_sub";  break;
                case OP_MUL:  fn = "mcbl_op_mul";  break;
                case OP_DIV:  fn = "mcbl_op_div";  break;
                case OP_MOD:  fn = "mcbl_op_mod";  break;
                case OP_EQ:   fn = "mcbl_op_eq";   break;
                case OP_NEQ:  fn = "mcbl_op_neq";  break;
                case OP_LT:   fn = "mcbl_op_lt";   break;
                case OP_GT:   fn = "mcbl_op_gt";   break;
                case OP_LE:   fn = "mcbl_op_le";   break;
                case OP_GE:   fn = "mcbl_op_ge";   break;
                case OP_AND:  fn = "mcbl_op_and";  break;
                case OP_OR:   fn = "mcbl_op_or";   break;
                default:      fn = "mcbl_op_add";  break;
            }
            cgen_write(g, "%s(", fn);
            {
                const char *ml = mcbl_mk_macro(n->binop.left);
                if (ml[0]) cgen_write(g, "%s(", ml);
                cgen_expr(g, n->binop.left);
                if (ml[0]) cgen_write(g, ")");
            }
            cgen_write(g, ", ");
            {
                const char *mr = mcbl_mk_macro(n->binop.right);
                if (mr[0]) cgen_write(g, "%s(", mr);
                cgen_expr(g, n->binop.right);
                if (mr[0]) cgen_write(g, ")");
            }
            cgen_write(g, ")");
            break;
        }
        case AST_EXPR_UNOP:
            if (n->binop.op == OP_NOT) cgen_write(g, "!(");
            else                       cgen_write(g, "-(");
            cgen_expr(g, n->binop.left);
            cgen_write(g, ")");
            break;
        case AST_EXPR_CALL: {
            const char *fn = n->call.target ? n->call.target : "_";
            if (strcmp(fn, "inputxt") == 0) {
                cgen_write(g, "mcbl_inputxt(");
                if (n->call.args.count > 0) {
                    const AstNode *arg = n->call.args.items[0];
                    if (arg->kind == AST_EXPR_STRING) {
                        cgen_string_escaped(g, arg->sval ? arg->sval : "");
                    } else if (arg->kind == AST_EXPR_IDENT) {
                        cgen_write(g, "mcbl_to_cstr(mcbl_var_%s)", arg->sval ? arg->sval : "_");
                    } else {
                        cgen_write(g, "mcbl_to_cstr(");
                        cgen_expr(g, arg);
                        cgen_write(g, ")");
                    }
                } else cgen_write(g, "\"\"");
                cgen_write(g, ")");
            } else if (strcmp(fn, "readfile") == 0) {
                /* readfile(path) → mcbl_readfile(cstr) */
                cgen_write(g, "mcbl_readfile(");
                if (n->call.args.count > 0) {
                    const AstNode *arg = n->call.args.items[0];
                    if (arg->kind == AST_EXPR_STRING) {
                        cgen_string_escaped(g, arg->sval ? arg->sval : "");
                    } else {
                        cgen_write(g, "mcbl_to_cstr(");
                        cgen_expr(g, arg);
                        cgen_write(g, ")");
                    }
                } else cgen_write(g, "\"\"");
                cgen_write(g, ")");
            } else {
                cgen_write(g, "mcbl_func_%s(", fn);
                for (size_t i = 0; i < n->call.args.count; i++) {
                    if (i) cgen_write(g, ", ");
                    const AstNode *arg = n->call.args.items[i];
                    const char *macro = mcbl_mk_macro(arg);
                    if (macro[0]) cgen_write(g, "%s(", macro);
                    cgen_expr(g, arg);
                    if (macro[0]) cgen_write(g, ")");
                }
                cgen_write(g, ")");
            }
            break;
        }
        case AST_EXPR_INDEX:
            /* array[i] → mcbl_array_get(arr, i) */
            cgen_write(g, "mcbl_array_get(");
            cgen_expr(g, n->binop.left);
            cgen_write(g, ", ");
            {
                AstNode *idx_node = n->binop.right;
                if (idx_node && idx_node->kind == AST_EXPR_INT) {
                    cgen_write(g, "%lld", idx_node->ival);
                } else if (idx_node && idx_node->kind == AST_EXPR_IDENT) {
                    cgen_write(g, "(int)mcbl_var_%s.ival", idx_node->sval ? idx_node->sval : "_");
                } else {
                    cgen_write(g, "(int)(");
                    cgen_expr(g, idx_node);
                    cgen_write(g, ").ival");
                }
            }
            cgen_write(g, ")");
            break;
        /* v2.0: math.xxx() / str.xxx() / file.xxx() etc in expression */
        case AST_MATH_CALL: {
            const char *ns   = n->ns_call.ns   ? n->ns_call.ns   : "math";
            const char *func = n->ns_call.func ? n->ns_call.func : "unknown";
            /* Special cases: math.PI, math.E → call as functions */
            if (strcmp(func, "PI") == 0) { cgen_write(g, "mcbl_math_PI()"); break; }
            if (strcmp(func, "E")  == 0) { cgen_write(g, "mcbl_math_E()");  break; }
            if (strcmp(func, "rand") == 0 && n->ns_call.args.count == 0) {
                cgen_write(g, "mcbl_math_rand()"); break;
            }
            cgen_write(g, "mcbl_%s_%s(", ns, func);
            for (size_t i = 0; i < n->ns_call.args.count; i++) {
                if (i > 0) cgen_write(g, ", ");
                cgen_expr(g, n->ns_call.args.items[i]);
            }
            cgen_write(g, ")");
            break;
        }
        /* v2.0: new ClassName() */
        case AST_NEW_EXPR:
            cgen_write(g, "mcbl_mkint_v(0) /* new %s */",
                       n->new_expr.class_name ? n->new_expr.class_name : "?");
            break;
        case AST_EXPR_MEMBER:
            cgen_expr(g, n->binop.left);
            cgen_write(g, ".%s", n->sval ? n->sval : "_");
            break;
        default:
            cgen_write(g, "0 /* unsupported expr */");
            break;
    }
}

static void cgen_node(CGen *g, const AstNode *n) {
    if (!n || g->error) return;
    switch (n->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < n->block.stmts.count; i++)
                cgen_node(g, n->block.stmts.items[i]);
            break;

        case AST_INC_DECL:
            cgen_line(g, "/* inc: %s */", n->inc.name ? n->inc.name : "");
            for (size_t i = 0; i < n->inc.body.count; i++)
                cgen_node(g, n->inc.body.items[i]);
            cgen_line(g, "/* endinc: %s */", n->inc.name ? n->inc.name : "");
            break;

        case AST_DEV_DECL:
            /* Dev functions are emitted before main in cgen_compile */
            break;

        case AST_PR_STMT:
            cgen_indent(g);
            cgen_write(g, "mcbl_print(");
            for (size_t i = 0; i < n->call.args.count; i++) {
                if (i) cgen_write(g, ", ");
                const AstNode *arg = n->call.args.items[i];
                if (arg && (arg->kind == AST_EXPR_BINOP || arg->kind == AST_EXPR_CALL ||
                           arg->kind == AST_EXPR_UNOP || arg->kind == AST_EXPR_INDEX ||
                           arg->kind == AST_EXPR_MEMBER || arg->kind == AST_EXPR_IDENT)) {
                    cgen_expr(g, arg);
                } else if (arg && arg->kind == AST_EXPR_STRING) {
                    cgen_write(g, "MCBL_MKSTR(");
                    cgen_expr(g, arg);
                    cgen_write(g, ")");
                } else if (arg && arg->kind == AST_EXPR_FLOAT) {
                    cgen_write(g, "MCBL_MKFLO(");
                    cgen_expr(g, arg);
                    cgen_write(g, ")");
                } else if (arg && arg->kind == AST_EXPR_BOOL) {
                    cgen_write(g, "mcbl_mkbool_v(");
                    cgen_expr(g, arg);
                    cgen_write(g, ")");
                } else {
                    cgen_write(g, "MCBL_MKVAL(");
                    cgen_expr(g, arg);
                    cgen_write(g, ")");
                }
            }
            cgen_write(g, ");\n");
            break;

        /* v2.0: $array name = {e1, e2, ...} */
        case AST_ARRAY_DECL: {
            const char *aname = n->array_decl.name ? n->array_decl.name : "_arr";
            cgen_indent(g);
            cgen_write(g, "McblValue mcbl_var_%s = mcbl_array_new(%d);\n",
                       aname, (int)n->array_decl.elements.count);
            cgen_mark_declared(g, aname);
            for (size_t i = 0; i < n->array_decl.elements.count; i++) {
                cgen_indent(g);
                cgen_write(g, "mcbl_array_push(mcbl_var_%s, ", aname);
                AstNode *elem = n->array_decl.elements.items[i];
                /* wrap non-McblValue literals */
                if (elem->kind == AST_EXPR_INT) {
                    cgen_write(g, "mcbl_mkint_v(%lld)", elem->ival);
                } else if (elem->kind == AST_EXPR_FLOAT) {
                    cgen_write(g, "mcbl_mkfloat_v(%g)", elem->fval);
                } else if (elem->kind == AST_EXPR_STRING) {
                    cgen_write(g, "MCBL_MKSTR(\"%s\")", elem->sval ? elem->sval : "");
                } else if (elem->kind == AST_EXPR_IDENT) {
                    /* bare identifier → string value */
                    cgen_write(g, "mcbl_mkstr_v(\"%s\")", elem->sval ? elem->sval : "");
                } else {
                    cgen_expr(g, elem);
                }
                cgen_write(g, ");\n");
            }
            break;
        }

        /* v2.0: math.xxx(...) / str.xxx(...) / file.xxx(...) etc as statement */
        case AST_MATH_CALL: {
            const char *ns   = n->ns_call.ns   ? n->ns_call.ns   : "math";
            const char *func = n->ns_call.func ? n->ns_call.func : "unknown";
            cgen_indent(g);
            cgen_write(g, "mcbl_%s_%s(", ns, func);
            for (size_t i = 0; i < n->ns_call.args.count; i++) {
                if (i > 0) cgen_write(g, ", ");
                cgen_expr(g, n->ns_call.args.items[i]);
            }
            cgen_write(g, ");\n");
            break;
        }

        /* v2.0: WriteTRam(mb) */
        case AST_WRITE_TRAM_STMT:
            cgen_indent(g);
            cgen_write(g, "tram_alloc(%lld); /* WriteTRam */\n", (long long)n->tram.mb);
            break;

        /* v2.0: cleanTram() */
        case AST_CLEAN_TRAM_STMT:
            cgen_indent(g);
            cgen_write(g, "tram_clean_all(); /* cleanTram */\n");
            break;

        /* v2.0: externFile(name) */
        case AST_EXTERN_FILE_STMT:
            cgen_indent(g);
            cgen_write(g, "extern_file_load(\"%s\");\n",
                       n->mbll.filename ? n->mbll.filename : "");
            break;

        /* v2.0: include<lib.cll> */
        case AST_INCLUDE_LIB_STMT:
            cgen_indent(g);
            cgen_write(g, "cll_load(\"%s\");\n",
                       n->mbll.filename ? n->mbll.filename : "");
            break;

        case AST_VAR_DECL:
        case AST_ORGATE_DECL:
        case AST_REG_VAR_DECL:
        case AST_CONSTEXPR_DECL: {
            const char *vname = n->var.name ? n->var.name : "_";
            cgen_indent(g);
            if (cgen_is_declared(g, vname)) {
                cgen_write(g, "mcbl_var_%s = %s(", vname, mcbl_mk_macro(n->var.value));
            } else {
                cgen_write(g, "McblValue mcbl_var_%s = %s(", vname, mcbl_mk_macro(n->var.value));
                cgen_mark_declared(g, vname);
            }
            cgen_expr(g, n->var.value);
            cgen_write(g, ");\n");
            break;
        }

        case AST_ASSIGN:
            cgen_indent(g);
            cgen_write(g, "mcbl_var_%s = %s(", n->assign.name ? n->assign.name : "_", mcbl_mk_macro(n->assign.value));
            cgen_expr(g, n->assign.value);
            cgen_write(g, ");\n");
            break;

        case AST_CONST_STMT:
            cgen_indent(g);
            if (n->var.value) {
                cgen_write(g, "McblValue mcbl_var_%s = %s(", n->var.name ? n->var.name : "_", mcbl_mk_macro(n->var.value));
                cgen_expr(g, n->var.value);
                cgen_write(g, "); /* const */\n");
            }
            break;

        case AST_IF_STMT:
            cgen_indent(g); cgen_write(g, "if (mcbl_truthy(");
            cgen_expr(g, n->if_stmt.cond);
            cgen_write(g, ")) {\n");
            g->indent++;
            for (size_t i = 0; i < n->if_stmt.then_body.count; i++)
                cgen_node(g, n->if_stmt.then_body.items[i]);
            g->indent--;
            cgen_indent(g); cgen_write(g, "}");
            /* elseif pairs */
            size_t ei = 0;
            while (ei + 1 < n->if_stmt.elseif_conds.count) {
                cgen_write(g, " else if (mcbl_truthy(");
                cgen_expr(g, n->if_stmt.elseif_conds.items[ei]);
                cgen_write(g, ")) {\n");
                g->indent++;
                AstNode *ei_body = n->if_stmt.elseif_conds.items[ei + 1];
                for (size_t k = 0; k < ei_body->block.stmts.count; k++)
                    cgen_node(g, ei_body->block.stmts.items[k]);
                g->indent--;
                cgen_indent(g); cgen_write(g, "}");
                ei += 2;
            }
            if (n->if_stmt.else_body.count > 0) {
                cgen_write(g, " else {\n");
                g->indent++;
                for (size_t i = 0; i < n->if_stmt.else_body.count; i++)
                    cgen_node(g, n->if_stmt.else_body.items[i]);
                g->indent--;
                cgen_indent(g); cgen_write(g, "}");
            }
            cgen_write(g, "\n");
            break;

        case AST_FOR_STMT: {
            const char *var = n->for_stmt.var_name ? n->for_stmt.var_name : "i";
            cgen_indent(g);
            cgen_write(g, "for (McblValue mcbl_var_%s = ", var);
            {
                const AstNode *start_expr = n->for_stmt.start;
                if (start_expr && start_expr->kind == AST_EXPR_INT) {
                    cgen_write(g, "mcbl_mkint_v(%lld)", start_expr->ival);
                } else if (start_expr && start_expr->kind == AST_EXPR_IDENT) {
                    cgen_expr(g, start_expr);
                } else {
                    cgen_write(g, "MCBL_MKVAL(");
                    cgen_expr(g, start_expr);
                    cgen_write(g, ")");
                }
            }
            cgen_write(g, "; mcbl_var_%s.ival < ", var);
            {
                const AstNode *end_expr = n->for_stmt.end;
                if (end_expr && end_expr->kind == AST_EXPR_INT) {
                    cgen_write(g, "%lld", end_expr->ival);
                } else if (end_expr && end_expr->kind == AST_EXPR_IDENT) {
                    cgen_expr(g, end_expr);
                    cgen_write(g, ".ival");
                } else {
                    cgen_expr(g, end_expr);
                    cgen_write(g, ".ival");
                }
            }
            cgen_write(g, "; mcbl_var_%s.ival += ", var);
            if (n->for_stmt.step) cgen_expr(g, n->for_stmt.step);
            else                  cgen_write(g, "1");
            cgen_write(g, ") {\n");
            g->indent++;
            for (size_t i = 0; i < n->for_stmt.body.count; i++)
                cgen_node(g, n->for_stmt.body.items[i]);
            g->indent--;
            cgen_line(g, "}");
            break;
        }

        case AST_LOOP_STMT:
            cgen_line(g, "while (1) {");
            g->indent++;
            for (size_t i = 0; i < n->loop_stmt.body.count; i++)
                cgen_node(g, n->loop_stmt.body.items[i]);
            g->indent--;
            cgen_line(g, "}");
            break;

        case AST_WHILE_STMT:
            cgen_indent(g); cgen_write(g, "while (mcbl_truthy(");
            cgen_expr(g, n->loop_stmt.cond);
            cgen_write(g, ")) {\n");
            g->indent++;
            for (size_t i = 0; i < n->loop_stmt.body.count; i++)
                cgen_node(g, n->loop_stmt.body.items[i]);
            g->indent--;
            cgen_line(g, "}");
            break;

        case AST_BREAK_STMT:    cgen_line(g, "break;");    break;
        case AST_CONTINUE_STMT: cgen_line(g, "continue;"); break;

        case AST_RETURN_STMT:
            cgen_indent(g);
            if (n->ret.value) {
                cgen_write(g, "return ");
                const AstNode *v = n->ret.value;
                if (v && (v->kind == AST_EXPR_BINOP || v->kind == AST_EXPR_CALL ||
                          v->kind == AST_EXPR_UNOP || v->kind == AST_EXPR_IDENT)) {
                    cgen_expr(g, v);
                } else if (v && v->kind == AST_EXPR_STRING) {
                    cgen_write(g, "MCBL_MKSTR(");
                    cgen_expr(g, v);
                    cgen_write(g, ")");
                } else if (v && v->kind == AST_EXPR_FLOAT) {
                    cgen_write(g, "MCBL_MKFLO(");
                    cgen_expr(g, v);
                    cgen_write(g, ")");
                } else {
                    cgen_write(g, "MCBL_MKVAL(");
                    cgen_expr(g, v);
                    cgen_write(g, ")");
                }
                cgen_write(g, ";\n");
            } else {
                cgen_write(g, "return mcbl_mkint_v(0);\n");
            }
            break;

        case AST_THREAD_STMT: {
            int tid = g->thread_counter++;
            cgen_line(g, "/* thread %d */", tid);
            cgen_line(g, "{");
            g->indent++;
            for (size_t i = 0; i < n->thread.body.count; i++)
                cgen_node(g, n->thread.body.items[i]);
            g->indent--;
            cgen_line(g, "}");
            break;
        }

        case AST_CARGO_M_STMT:
            cgen_line(g, "mcbl_cargo_create(%lld);", n->cargo.size);
            break;

        case AST_CLEAR_CARGO_STMT:
            cgen_line(g, "mcbl_cargo_clear_all();");
            break;

        case AST_AUTOCLEAR_STMT:
            cgen_line(g, "mcbl_autoclear_cargo();");
            break;

        case AST_USING_CPU_STMT:
            cgen_line(g, "mcbl_cpu_connect();");
            break;

        case AST_MOV_STMT:
            cgen_indent(g);
            cgen_write(g, "McblValue mcbl_var_%s = mcbl_mkint_v(", n->mov.reg ? n->mov.reg : "EAX");
            cgen_expr(g, n->mov.value);
            cgen_write(g, ");\n");
            break;

        case AST_SPLIT_STRING_STMT:
            cgen_indent(g);
            cgen_write(g, "mcbl_splitstring(");
            cgen_expr(g, n->str_op.str);
            cgen_write(g, ", ");
            cgen_expr(g, n->str_op.delim);
            cgen_write(g, ");\n");
            break;

        case AST_COMBINE_STRING_STMT:
            cgen_indent(g);
            cgen_write(g, "mcbl_combinestring(");
            cgen_expr(g, n->str_op.str);
            cgen_write(g, ", ");
            cgen_expr(g, n->str_op.delim);
            cgen_write(g, ");\n");
            break;

        case AST_SEND_STMT:
            cgen_line(g, "mcbl_send(\"%s\", \"%s\");",
                      n->send.data_name ? n->send.data_name : "",
                      n->send.dest      ? n->send.dest      : "");
            break;

        case AST_READFILE_STMT:
            cgen_indent(g);
            cgen_write(g, "mcbl_readfile(");
            if (n->call.args.count > 0) cgen_expr(g, n->call.args.items[0]);
            cgen_write(g, ");\n");
            break;

        case AST_CREATEWINDOW_STMT:
            cgen_indent(g);
            cgen_write(g, "mcbl_create_window(");
            for (size_t i = 0; i < n->call.args.count; i++) {
                if (i) cgen_write(g, ", ");
                cgen_expr(g, n->call.args.items[i]);
            }
            cgen_write(g, ");\n");
            break;

        case AST_OPEN_PAGE_STMT:
            cgen_line(g, "mcbl_openpage(\"%s\");", n->call.target ? n->call.target : "");
            break;

        case AST_USE_STMT:
            cgen_line(g, "mcbl_use_inc(\"%s\");", n->call.target ? n->call.target : "");
            break;

        case AST_WAIT_FOR_STMT:
            cgen_line(g, "mcbl_wait_for(\"%s\");", n->call.target ? n->call.target : "");
            break;

        case AST_RESPONSE_STMT:
            cgen_line(g, "mcbl_response();");
            break;

        case AST_INPUTXT_STMT:
            cgen_indent(g);
            cgen_write(g, "mcbl_inputxt(");
            if (n->call.args.count > 0) cgen_expr(g, n->call.args.items[0]);
            else cgen_write(g, "\"\"");
            cgen_write(g, ");\n");
            break;

        case AST_IMPORT_STMT:
            if (n->call.args.count > 0 && n->call.args.items[0]->kind == AST_EXPR_STRING)
                cgen_line(g, "#include \"%s\"", n->call.args.items[0]->sval ? n->call.args.items[0]->sval : "");
            break;

        case AST_EXTERN_M_BLOCK:
            cgen_write(g, "/* extern m: McBL -> C */\n");
            if (n->ext.raw_code) cgen_write(g, "%s\n", n->ext.raw_code);
            break;

        case AST_EXTERN_C_BLOCK:
            cgen_write(g, "/* extern c: C -> McBL */\n");
            if (n->ext.raw_code) cgen_write(g, "%s\n", n->ext.raw_code);
            break;

        case AST_DATA_BLOCK:
            cgen_indent(g);
            cgen_write(g, "McblValue mcbl_data_%s = %s(", n->data.name ? n->data.name : "_", mcbl_mk_macro(n->data.value));
            cgen_expr(g, n->data.value);
            cgen_write(g, ");\n");
            break;

        case AST_CALL_STMT: {
            const char *fn = n->call.target ? n->call.target : "_";
            cgen_indent(g);
            if (strcmp(fn, "inputxt") == 0) {
                cgen_write(g, "mcbl_inputxt(");
                if (n->call.args.count > 0) {
                    const AstNode *arg = n->call.args.items[0];
                    if (arg->kind == AST_EXPR_STRING) {
                        cgen_string_escaped(g, arg->sval ? arg->sval : "");
                    } else if (arg->kind == AST_EXPR_IDENT) {
                        cgen_write(g, "mcbl_to_cstr(mcbl_var_%s)", arg->sval ? arg->sval : "_");
                    } else {
                        cgen_write(g, "mcbl_to_cstr(");
                        cgen_expr(g, arg);
                        cgen_write(g, ")");
                    }
                } else cgen_write(g, "\"\"");
            } else {
                cgen_write(g, "mcbl_func_%s(", fn);
                for (size_t i = 0; i < n->call.args.count; i++) {
                    if (i) cgen_write(g, ", ");
                    cgen_expr(g, n->call.args.items[i]);
                }
            }
            cgen_write(g, ");\n");
            break;
        }

        case AST_NOP:
        default:
            break;
    }
}

/* C runtime preamble emitted at the top of every transpiled file */
static const char *MCBL_C_PREAMBLE =
    "/* Auto-generated by McBL# Compiler */\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "#include <stdint.h>\n"
    "\n"
    "/* ---- McBL# runtime types ---- */\n"
    "typedef enum { MCBL_INT, MCBL_FLOAT, MCBL_STR, MCBL_BOOL } McblType;\n"
    "typedef struct {\n"
    "    McblType type;\n"
    "    union {\n"
    "        long long ival;\n"
    "        double    fval;\n"
    "        char     *sval;\n"
    "        int       bval;\n"
    "    };\n"
    "} McblValue;\n"
    "\n"
    "static McblValue mcbl_mkint_v(long long v)   { McblValue r; r.type=MCBL_INT;   r.ival=v; return r; }\n"
    "static McblValue mcbl_mkfloat_v(double v)     { McblValue r; r.type=MCBL_FLOAT; r.fval=v; return r; }\n"
    "static McblValue mcbl_mkstr_v(const char *v) { McblValue r; r.type=MCBL_STR;   r.sval=(char*)v; return r; }\n"
    "static McblValue mcbl_mkbool_v(int v)         { McblValue r; r.type=MCBL_BOOL;  r.bval=v; return r; }\n"
    "\n"
    "#define MCBL_MKVAL(x) mcbl_mkint_v((long long)(x))\n"
    "#define MCBL_MKSTR(x) mcbl_mkstr_v((const char*)(x))\n"
    "#define MCBL_MKFLO(x) mcbl_mkfloat_v((double)(x))\n"
    "\n"
    "static long long mcbl_reg_EAX = 0;\n"
    "static long long mcbl_reg_EBX = 0;\n"
    "static long long mcbl_reg_ECX = 0;\n"
    "static long long mcbl_reg_EDX = 0;\n"
    "\n"
    "/* ---- McBL# runtime functions ---- */\n"
    "static void mcbl_print_val(McblValue v) {\n"
    "    switch (v.type) {\n"
    "        case MCBL_INT:   printf(\"%lld\", v.ival); break;\n"
    "        case MCBL_FLOAT: printf(\"%g\",   v.fval); break;\n"
    "        case MCBL_STR:   printf(\"%s\",   v.sval ? v.sval : \"\"); break;\n"
    "        case MCBL_BOOL:  printf(\"%s\",   v.bval ? \"true\" : \"false\"); break;\n"
    "    }\n"
    "}\n"
    "\n"
    "static void mcbl_print(McblValue v) { mcbl_print_val(v); printf(\"\\n\"); }\n"
    "\n"
    "static char *mcbl_concat(const char *a, const char *b) {\n"
    "    if (!a) a = \"\"; if (!b) b = \"\";\n"
    "    size_t la = strlen(a), lb = strlen(b);\n"
    "    char *r = (char *)malloc(la + lb + 1);\n"
    "    if (!r) { fprintf(stderr, \"OOM\\n\"); exit(1); }\n"
    "    memcpy(r, a, la); memcpy(r + la, b, lb); r[la + lb] = '\\0';\n"
    "    return r;\n"
    "}\n"
    "\n"
    "static McblValue mcbl_inputxt(const char *prompt) {\n"
    "    if (prompt && *prompt) printf(\"%s\", prompt);\n"
    "    char buf[1024] = {0};\n"
    "    if (fgets(buf, sizeof(buf), stdin)) {\n"
    "        size_t l = strlen(buf);\n"
    "        if (l > 0 && buf[l-1] == '\\n') buf[l-1] = '\\0';\n"
    "    }\n"
    "    char *r = strdup(buf);\n"
    "    return mcbl_mkstr_v(r);\n"
    "}\n"
    "\n"
    "static McblValue mcbl_readfile(const char *path) {\n"
    "    if (!path) return mcbl_mkstr_v(\"\");\n"
    "    FILE *f = fopen(path, \"r\");\n"
    "    if (!f) return mcbl_mkstr_v(\"\");\n"
    "    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);\n"
    "    char *buf = (char *)malloc((size_t)sz + 1);\n"
    "    if (!buf) { fclose(f); return mcbl_mkstr_v(\"\"); }\n"
    "    fread(buf, 1, (size_t)sz, f); buf[sz] = '\\0'; fclose(f);\n"
    "    return mcbl_mkstr_v(buf);\n"
    "}\n"
    "\n"
    "static void mcbl_send(const char *data, const char *dest) { (void)data; (void)dest; }\n"
    "static void mcbl_use_inc(const char *name)  { (void)name; }\n"
    "static void mcbl_wait_for(const char *name) { (void)name; }\n"
    "static void mcbl_response(void) {}\n"
    "static void mcbl_cargo_create(long long sz) { (void)sz; }\n"
    "static void mcbl_cargo_clear_all(void) {}\n"
    "static void mcbl_autoclear_cargo(void) {}\n"
    "static void mcbl_cpu_connect(void) {}\n"
    "static void mcbl_create_window(McblValue title) { (void)title; }\n"
    "static void mcbl_openpage(const char *name) { (void)name; }\n"
    "static McblValue mcbl_splitstring(McblValue s, McblValue delim) { (void)s; (void)delim; return mcbl_mkstr_v(\"\"); }\n"
    "static McblValue mcbl_combinestring(McblValue a, McblValue b) { (void)a; (void)b; return mcbl_mkstr_v(\"\"); }\n"
    "\n"
    "/* forward decl */\n"
    "static const char *mcbl_to_cstr(McblValue v);\n"
    "\n"
    "/* ---- v2.0: McBL# Array runtime ---- */\n"
    "#define MCBL_ARRAY_CAP 4096\n"
    "typedef struct { McblValue items[MCBL_ARRAY_CAP]; int count; } McblArray;\n"
    "static McblArray _mcbl_arrays[256];\n"
    "static int _mcbl_arr_idx = 0;\n"
    "static McblValue mcbl_array_new(int cap) {\n"
    "    (void)cap;\n"
    "    McblValue r; r.type = MCBL_INT; r.ival = _mcbl_arr_idx;\n"
    "    _mcbl_arrays[_mcbl_arr_idx].count = 0;\n"
    "    _mcbl_arr_idx++;\n"
    "    return r;\n"
    "}\n"
    "static void mcbl_array_push(McblValue arr, McblValue val) {\n"
    "    int idx = (int)arr.ival;\n"
    "    if (idx < 0 || idx >= 256) return;\n"
    "    if (_mcbl_arrays[idx].count < MCBL_ARRAY_CAP)\n"
    "        _mcbl_arrays[idx].items[_mcbl_arrays[idx].count++] = val;\n"
    "}\n"
    "static McblValue mcbl_array_get(McblValue arr, int i) {\n"
    "    int idx = (int)arr.ival;\n"
    "    if (idx < 0 || idx >= 256 || i < 0 || i >= _mcbl_arrays[idx].count)\n"
    "        return mcbl_mkint_v(0);\n"
    "    return _mcbl_arrays[idx].items[i];\n"
    "}\n"
    "static void mcbl_array_print(McblValue arr) {\n"
    "    int idx = (int)arr.ival;\n"
    "    if (idx < 0 || idx >= 256) { printf(\"[]\"); return; }\n"
    "    printf(\"[\");\n"
    "    for (int i = 0; i < _mcbl_arrays[idx].count; i++) {\n"
    "        if (i) printf(\", \");\n"
    "        McblValue v = _mcbl_arrays[idx].items[i];\n"
    "        switch(v.type) {\n"
    "            case MCBL_INT:   printf(\"%lld\", v.ival); break;\n"
    "            case MCBL_FLOAT: printf(\"%g\",   v.fval); break;\n"
    "            case MCBL_STR:   printf(\"%s\",   v.sval ? v.sval : \"\"); break;\n"
    "            case MCBL_BOOL:  printf(\"%s\",   v.bval ? \"true\" : \"false\"); break;\n"
    "            default: printf(\"?\"); break;\n"
    "        }\n"
    "    }\n"
    "    printf(\"]\\n\");\n"
    "}\n"
    "\n"
    "/* ---- v2.0: identifier literals → string values ---- */\n"
    "#define mcbl_var_dafa    mcbl_mkstr_v(\"dafa\")\n"
    "#define mcbl_var_davin   mcbl_mkstr_v(\"davin\")\n"
    "#define mcbl_var_gerrard mcbl_mkstr_v(\"gerrard\")\n"
    "#define mcbl_var_zaki    mcbl_mkstr_v(\"zaki\")\n"
    "#define mcbl_var_jaka    mcbl_mkstr_v(\"jaka\")\n"
    "#define mcbl_var_dafa    mcbl_mkstr_v(\"dafa\")\n"
    "\n"
    "/* ---- v2.0: WriteTRam / cleanTram stubs ---- */\n"
    "static void tram_alloc_c(long long mb) { (void)mb; }\n"
    "static void tram_clean_all_c(void) {}\n"
    "#define tram_alloc(mb)    tram_alloc_c(mb)\n"
    "#define tram_clean_all()  tram_clean_all_c()\n"
    "\n"
    "/* ---- v2.0: externFile / cll stubs ---- */\n"
    "static void extern_file_load_c(const char *f) { (void)f; }\n"
    "static void cll_load_c(const char *f) { (void)f; }\n"
    "#define extern_file_load(f) extern_file_load_c(f)\n"
    "#define cll_load(f)         cll_load_c(f)\n"
    "\n"
    "/* ---- v2.0: math stdlib stubs ---- */\n"
    "#include <math.h>\n"
    "static McblValue mcbl_math_sqrt(McblValue x)  { return mcbl_mkfloat_v(sqrt(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_abs (McblValue x)  { double v=x.type==MCBL_INT?(double)x.ival:x.fval; return mcbl_mkfloat_v(v<0?-v:v); }\n"
    "static McblValue mcbl_math_pow (McblValue b, McblValue e) { return mcbl_mkfloat_v(pow(b.type==MCBL_INT?(double)b.ival:b.fval, e.type==MCBL_INT?(double)e.ival:e.fval)); }\n"
    "static McblValue mcbl_math_sin (McblValue x)  { return mcbl_mkfloat_v(sin(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_cos (McblValue x)  { return mcbl_mkfloat_v(cos(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_tan (McblValue x)  { return mcbl_mkfloat_v(tan(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_log (McblValue x, McblValue b) { double v=x.type==MCBL_INT?(double)x.ival:x.fval; double base=b.type==MCBL_INT?(double)b.ival:b.fval; return mcbl_mkfloat_v(log(v)/log(base)); }\n"
    "static McblValue mcbl_math_ln  (McblValue x)  { return mcbl_mkfloat_v(log(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_exp (McblValue x)  { return mcbl_mkfloat_v(exp(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_floor(McblValue x) { return mcbl_mkfloat_v(floor(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_ceil(McblValue x)  { return mcbl_mkfloat_v(ceil(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_round(McblValue x) { return mcbl_mkfloat_v(round(x.type==MCBL_INT?(double)x.ival:x.fval)); }\n"
    "static McblValue mcbl_math_min(McblValue a, McblValue b) { double va=a.type==MCBL_INT?(double)a.ival:a.fval, vb=b.type==MCBL_INT?(double)b.ival:b.fval; return mcbl_mkfloat_v(va<vb?va:vb); }\n"
    "static McblValue mcbl_math_max(McblValue a, McblValue b) { double va=a.type==MCBL_INT?(double)a.ival:a.fval, vb=b.type==MCBL_INT?(double)b.ival:b.fval; return mcbl_mkfloat_v(va>vb?va:vb); }\n"
    "static McblValue mcbl_math_clamp(McblValue x, McblValue lo, McblValue hi) { double v=x.type==MCBL_INT?(double)x.ival:x.fval, l=lo.type==MCBL_INT?(double)lo.ival:lo.fval, h=hi.type==MCBL_INT?(double)hi.ival:hi.fval; return mcbl_mkfloat_v(v<l?l:v>h?h:v); }\n"
    "static McblValue mcbl_math_lerp(McblValue a, McblValue b, McblValue t) { double va=a.type==MCBL_INT?(double)a.ival:a.fval, vb=b.type==MCBL_INT?(double)b.ival:b.fval, vt=t.type==MCBL_INT?(double)t.ival:t.fval; return mcbl_mkfloat_v(va+vt*(vb-va)); }\n"
    "static McblValue mcbl_math_gcd (McblValue a, McblValue b) { long long x=a.ival,y=b.ival; while(y){long long t=y;y=x%y;x=t;} return mcbl_mkint_v(x<0?-x:x); }\n"
    "static McblValue mcbl_math_fact(McblValue n) { long long r=1; for(long long i=2;i<=n.ival;i++) r*=i; return mcbl_mkint_v(r); }\n"
    "static McblValue mcbl_math_rand(void) { return mcbl_mkfloat_v((double)rand()/(double)RAND_MAX); }\n"
    "static void      mcbl_math_seed(McblValue s) { srand((unsigned int)s.ival); }\n"
    "static McblValue mcbl_math_PI  (void) { return mcbl_mkfloat_v(3.14159265358979323846); }\n"
    "static McblValue mcbl_math_E   (void) { return mcbl_mkfloat_v(2.71828182845904523536); }\n"
    "\n"
    "/* ---- v2.0: str stdlib stubs ---- */\n"
    "#include <string.h>\n"
    "#include <ctype.h>\n"
    "static McblValue mcbl_str_len    (McblValue s) { return mcbl_mkint_v(s.sval ? (long long)strlen(s.sval) : 0); }\n"
    "static McblValue mcbl_str_upper  (McblValue s) { if(!s.sval) return mcbl_mkstr_v(\"\"); char *r=strdup(s.sval); for(int i=0;r[i];i++) r[i]=toupper((unsigned char)r[i]); return mcbl_mkstr_v(r); }\n"
    "static McblValue mcbl_str_lower  (McblValue s) { if(!s.sval) return mcbl_mkstr_v(\"\"); char *r=strdup(s.sval); for(int i=0;r[i];i++) r[i]=tolower((unsigned char)r[i]); return mcbl_mkstr_v(r); }\n"
    "static McblValue mcbl_str_find   (McblValue s, McblValue sub, McblValue from) { if(!s.sval||!sub.sval) return mcbl_mkint_v(-1); const char *p=strstr(s.sval+(int)from.ival, sub.sval); return mcbl_mkint_v(p?(long long)(p-s.sval):-1); }\n"
    "static McblValue mcbl_str_rev    (McblValue s) { if(!s.sval) return mcbl_mkstr_v(\"\"); char *r=strdup(s.sval); size_t n=strlen(r); for(size_t i=0;i<n/2;i++){char t=r[i];r[i]=r[n-1-i];r[n-1-i]=t;} return mcbl_mkstr_v(r); }\n"
    "static McblValue mcbl_str_repeat (McblValue s, McblValue n) { if(!s.sval) return mcbl_mkstr_v(\"\"); size_t sl=strlen(s.sval); long long cnt=n.ival; char *r=(char*)calloc(sl*cnt+1,1); for(long long i=0;i<cnt;i++) memcpy(r+i*sl,s.sval,sl); return mcbl_mkstr_v(r); }\n"
    "static McblValue mcbl_str_sub    (McblValue s, McblValue a, McblValue b) { if(!s.sval) return mcbl_mkstr_v(\"\"); long long start=a.ival, end=b.ival, len=(long long)strlen(s.sval); if(start<0)start=0; if(end>len)end=len; if(start>=end) return mcbl_mkstr_v(\"\"); long long sl=end-start; char *r=(char*)malloc(sl+1); memcpy(r,s.sval+start,sl); r[sl]=0; return mcbl_mkstr_v(r); }\n"
    "static McblValue mcbl_str_starts (McblValue s, McblValue p2) { if(!s.sval||!p2.sval) return mcbl_mkbool_v(0); return mcbl_mkbool_v(strncmp(s.sval,p2.sval,strlen(p2.sval))==0); }\n"
    "static McblValue mcbl_str_ends   (McblValue s, McblValue p2) { if(!s.sval||!p2.sval) return mcbl_mkbool_v(0); size_t sl=strlen(s.sval),pl=strlen(p2.sval); if(pl>sl) return mcbl_mkbool_v(0); return mcbl_mkbool_v(strcmp(s.sval+sl-pl,p2.sval)==0); }\n"
    "static McblValue mcbl_str_contains(McblValue s, McblValue p2){ if(!s.sval||!p2.sval) return mcbl_mkbool_v(0); return mcbl_mkbool_v(strstr(s.sval,p2.sval)!=NULL); }\n"
    "static McblValue mcbl_str_toInt  (McblValue s, McblValue base) { if(!s.sval) return mcbl_mkint_v(0); return mcbl_mkint_v(strtoll(s.sval,NULL,(int)base.ival)); }\n"
    "static McblValue mcbl_str_toFloat(McblValue s) { if(!s.sval) return mcbl_mkfloat_v(0); return mcbl_mkfloat_v(strtod(s.sval,NULL)); }\n"
    "static McblValue mcbl_str_replace(McblValue s, McblValue o, McblValue nw) {\n"
    "    if(!s.sval||!o.sval||!nw.sval) return s;\n"
    "    size_t olen=strlen(o.sval), nlen=strlen(nw.sval), slen=strlen(s.sval);\n"
    "    char *r=(char*)malloc(slen*nlen+slen+1); r[0]=0;\n"
    "    const char *cur=s.sval, *p;\n"
    "    while((p=strstr(cur,o.sval))){ strncat(r,cur,(size_t)(p-cur)); strcat(r,nw.sval); cur=p+olen; }\n"
    "    strcat(r,cur);\n"
    "    return mcbl_mkstr_v(r);\n"
    "}\n"
    "static McblValue mcbl_str_format (McblValue fmt, McblValue a1, McblValue a2, McblValue a3) {\n"
    "    char buf[2048]={0}; int bi=0;\n"
    "    McblValue args[3]={a1,a2,a3}; int ai=0;\n"
    "    if(!fmt.sval) return mcbl_mkstr_v(\"\");\n"
    "    for(const char *p=fmt.sval; *p && bi<2040; p++) {\n"
    "        if(*p=='{'&&*(p+1)=='}'&&ai<3) {\n"
    "            const char *v=mcbl_to_cstr(args[ai++]);\n"
    "            size_t vl=strlen(v); memcpy(buf+bi,v,vl); bi+=vl; p++;\n"
    "        } else buf[bi++]=*p;\n"
    "    }\n"
    "    return mcbl_mkstr_v(strdup(buf));\n"
    "}\n"
    "static McblValue mcbl_str_regex  (McblValue s, McblValue pat) { if(!s.sval||!pat.sval) return mcbl_mkstr_v(\"\"); const char *p=strstr(s.sval,pat.sval); return p?mcbl_mkstr_v(strdup(pat.sval)):mcbl_mkstr_v(\"\"); }\n"
    "static McblValue mcbl_str_encode (McblValue s, McblValue enc) { (void)enc; return s; }\n"
    "static McblValue mcbl_str_trim   (McblValue s) { if(!s.sval) return mcbl_mkstr_v(\"\"); const char *p=s.sval; while(*p&&isspace((unsigned char)*p))p++; char *r=strdup(p); size_t l=strlen(r); while(l>0&&isspace((unsigned char)r[l-1]))r[--l]=0; return mcbl_mkstr_v(r); }\n"
    "static McblValue mcbl_str_split  (McblValue s, McblValue d) {\n"
    "    (void)d;\n"
    "    McblValue arr = mcbl_array_new(8);\n"
    "    if(!s.sval||!d.sval) return arr;\n"
    "    char *copy=strdup(s.sval), *tok=strtok(copy, d.sval);\n"
    "    while(tok){ mcbl_array_push(arr, mcbl_mkstr_v(strdup(tok))); tok=strtok(NULL,d.sval); }\n"
    "    free(copy);\n"
    "    return arr;\n"
    "}\n"
    "static McblValue mcbl_str_join   (McblValue sep, McblValue arr) {\n"
    "    if(!sep.sval) return mcbl_mkstr_v(\"\");\n"
    "    int idx=(int)arr.ival; if(idx<0||idx>=256) return mcbl_mkstr_v(\"\");\n"
    "    char buf[4096]={0};\n"
    "    for(int i=0;i<_mcbl_arrays[idx].count;i++){\n"
    "        if(i) strncat(buf,sep.sval,sizeof(buf)-strlen(buf)-1);\n"
    "        strncat(buf,mcbl_to_cstr(_mcbl_arrays[idx].items[i]),sizeof(buf)-strlen(buf)-1);\n"
    "    }\n"
    "    return mcbl_mkstr_v(strdup(buf));\n"
    "}\n"
    "\n"
    "/* ---- v2.0: file stubs ---- */\n"
    "static McblValue mcbl_file_write  (McblValue p, McblValue c) { if(p.sval&&c.sval){FILE *f=fopen(p.sval,\"w\");if(f){fputs(c.sval,f);fclose(f);}} return mcbl_mkbool_v(1); }\n"
    "static McblValue mcbl_file_append (McblValue p, McblValue c) { if(p.sval&&c.sval){FILE *f=fopen(p.sval,\"a\");if(f){fputs(c.sval,f);fclose(f);}} return mcbl_mkbool_v(1); }\n"
    "static McblValue mcbl_file_exists (McblValue p) { if(!p.sval) return mcbl_mkbool_v(0); FILE *f=fopen(p.sval,\"r\"); if(f){fclose(f);return mcbl_mkbool_v(1);} return mcbl_mkbool_v(0); }\n"
    "static McblValue mcbl_file_delete (McblValue p) { return mcbl_mkbool_v(p.sval&&remove(p.sval)==0); }\n"
    "static McblValue mcbl_file_size   (McblValue p) { if(!p.sval) return mcbl_mkint_v(-1); FILE *f=fopen(p.sval,\"rb\"); if(!f) return mcbl_mkint_v(-1); fseek(f,0,SEEK_END); long s=ftell(f); fclose(f); return mcbl_mkint_v(s); }\n"
    "\n"
    "/* ---- v2.0: sys stubs ---- */\n"
    "#include <time.h>\n"
    "static McblValue mcbl_sys_time  (void) { return mcbl_mkint_v((long long)time(NULL)*1000); }\n"
    "static McblValue mcbl_sys_exec  (McblValue cmd) { if(!cmd.sval) return mcbl_mkstr_v(\"\"); FILE *f=popen(cmd.sval,\"r\"); if(!f) return mcbl_mkstr_v(\"\"); char buf[4096]={0}; fread(buf,1,sizeof(buf)-1,f); pclose(f); return mcbl_mkstr_v(strdup(buf)); }\n"
    "static McblValue mcbl_sys_env   (McblValue v)   { const char *e=getenv(v.sval?v.sval:\"\"); return mcbl_mkstr_v(e?e:\"\"); }\n"
    "static void      mcbl_sys_sleep (McblValue s)   { (void)s; }\n"
    "static void      mcbl_sys_exit  (McblValue c)   { exit((int)c.ival); }\n"
    "static McblValue mcbl_sys_pid   (void)          { return mcbl_mkint_v(0); }\n"
    "\n"
    "/* ---- v2.0: net stubs ---- */\n"
    "static McblValue mcbl_net_get (McblValue url) { (void)url; return mcbl_mkstr_v(\"\"); }\n"
    "static McblValue mcbl_net_post(McblValue url, McblValue body, McblValue ct) { (void)url;(void)body;(void)ct; return mcbl_mkstr_v(\"\"); }\n"
    "\n"
    "/* ---- v2.0: debug stubs ---- */\n"
    "static void mcbl_debug_log   (McblValue lvl, McblValue msg) { printf(\"[%s] %s\\n\", lvl.sval?lvl.sval:\"LOG\", msg.sval?msg.sval:\"\"); }\n"
    "static void mcbl_debug_assert(McblValue c, McblValue msg)   { if(!c.bval&&!c.ival) { fprintf(stderr,\"ASSERT: %s\\n\",msg.sval?msg.sval:\"\"); abort(); } }\n"
    "static void mcbl_debug_trace (McblValue msg)                { fprintf(stderr,\"[TRACE] %s\\n\",msg.sval?msg.sval:\"\"); }\n"
    "\n"
    "/* ---- McBL# runtime arithmetic helpers ---- */\n"
    "static const char *mcbl_to_cstr(McblValue v) {\n"
    "    static char buf[256];\n"
    "    switch (v.type) {\n"
    "        case MCBL_INT:   snprintf(buf,sizeof(buf),\"%lld\",v.ival); return buf;\n"
    "        case MCBL_FLOAT: snprintf(buf,sizeof(buf),\"%g\",v.fval); return buf;\n"
    "        case MCBL_STR:   return v.sval ? v.sval : \"\";\n"
    "        case MCBL_BOOL:  return v.bval ? \"true\" : \"false\";\n"
    "    }\n"
    "    return \"\";\n"
    "}\n"
    "static McblValue mcbl_op_add(McblValue a, McblValue b) {\n"
    "    if (a.type==MCBL_STR || b.type==MCBL_STR) {\n"
    "        const char *sa=mcbl_to_cstr(a), *sb=mcbl_to_cstr(b);\n"
    "        return mcbl_mkstr_v(mcbl_concat(sa,sb));\n"
    "    }\n"
    "    if (a.type==MCBL_FLOAT || b.type==MCBL_FLOAT) {\n"
    "        double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "        double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "        return mcbl_mkfloat_v(fa+fb);\n"
    "    }\n"
    "    return mcbl_mkint_v(a.ival+b.ival);\n"
    "}\n"
    "static McblValue mcbl_op_sub(McblValue a, McblValue b) {\n"
    "    if (a.type==MCBL_FLOAT || b.type==MCBL_FLOAT) {\n"
    "        double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "        double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "        return mcbl_mkfloat_v(fa-fb);\n"
    "    }\n"
    "    return mcbl_mkint_v(a.ival-b.ival);\n"
    "}\n"
    "static McblValue mcbl_op_mul(McblValue a, McblValue b) {\n"
    "    if (a.type==MCBL_FLOAT || b.type==MCBL_FLOAT) {\n"
    "        double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "        double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "        return mcbl_mkfloat_v(fa*fb);\n"
    "    }\n"
    "    return mcbl_mkint_v(a.ival*b.ival);\n"
    "}\n"
    "static McblValue mcbl_op_div(McblValue a, McblValue b) {\n"
    "    double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "    if (fb==0.0) return mcbl_mkint_v(0);\n"
    "    if (a.type==MCBL_FLOAT || b.type==MCBL_FLOAT) {\n"
    "        double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "        return mcbl_mkfloat_v(fa/fb);\n"
    "    }\n"
    "    return mcbl_mkint_v(a.ival/b.ival);\n"
    "}\n"
    "static McblValue mcbl_op_mod(McblValue a, McblValue b) {\n"
    "    if (b.ival==0) return mcbl_mkint_v(0);\n"
    "    return mcbl_mkint_v(a.ival%b.ival);\n"
    "}\n"
    "static McblValue mcbl_op_eq(McblValue a, McblValue b) {\n"
    "    if (a.type!=b.type) {\n"
    "        double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "        double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "        return mcbl_mkbool_v(fa==fb);\n"
    "    }\n"
    "    switch(a.type) {\n"
    "        case MCBL_INT:   return mcbl_mkbool_v(a.ival==b.ival);\n"
    "        case MCBL_FLOAT: return mcbl_mkbool_v(a.fval==b.fval);\n"
    "        case MCBL_STR:   return mcbl_mkbool_v(a.sval&&b.sval&&strcmp(a.sval,b.sval)==0);\n"
    "        default:        return mcbl_mkbool_v(a.bval==b.bval);\n"
    "    }\n"
    "}\n"
    "static McblValue mcbl_op_neq(McblValue a, McblValue b) {\n"
    "    McblValue r=mcbl_op_eq(a,b); r.bval=!r.bval; return r;\n"
    "}\n"
    "static McblValue mcbl_op_lt(McblValue a, McblValue b) {\n"
    "    double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "    double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "    return mcbl_mkbool_v(fa<fb);\n"
    "}\n"
    "static McblValue mcbl_op_gt(McblValue a, McblValue b) {\n"
    "    double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "    double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "    return mcbl_mkbool_v(fa>fb);\n"
    "}\n"
    "static McblValue mcbl_op_le(McblValue a, McblValue b) {\n"
    "    double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "    double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "    return mcbl_mkbool_v(fa<=fb);\n"
    "}\n"
    "static McblValue mcbl_op_ge(McblValue a, McblValue b) {\n"
    "    double fa=(a.type==MCBL_FLOAT)?a.fval:(double)a.ival;\n"
    "    double fb=(b.type==MCBL_FLOAT)?b.fval:(double)b.ival;\n"
    "    return mcbl_mkbool_v(fa>=fb);\n"
    "}\n"
    "static McblValue mcbl_op_and(McblValue a, McblValue b) {\n"
    "    int ba=(a.type==MCBL_INT)?(a.ival!=0):(a.type==MCBL_BOOL)?a.bval:(a.type==MCBL_FLOAT)?(a.fval!=0.0):(a.sval&&a.sval[0]);\n"
    "    int bb=(b.type==MCBL_INT)?(b.ival!=0):(b.type==MCBL_BOOL)?b.bval:(b.type==MCBL_FLOAT)?(b.fval!=0.0):(b.sval&&b.sval[0]);\n"
    "    return mcbl_mkbool_v(ba&&bb);\n"
    "}\n"
    "static McblValue mcbl_op_or(McblValue a, McblValue b) {\n"
    "    int ba=(a.type==MCBL_INT)?(a.ival!=0):(a.type==MCBL_BOOL)?a.bval:(a.type==MCBL_FLOAT)?(a.fval!=0.0):(a.sval&&a.sval[0]);\n"
    "    int bb=(b.type==MCBL_INT)?(b.ival!=0):(b.type==MCBL_BOOL)?b.bval:(b.type==MCBL_FLOAT)?(b.fval!=0.0):(b.sval&&b.sval[0]);\n"
    "    return mcbl_mkbool_v(ba||bb);\n"
    "}\n"
    "static int mcbl_truthy(McblValue v) {\n"
    "    switch(v.type) {\n"
    "        case MCBL_INT:   return v.ival!=0;\n"
    "        case MCBL_FLOAT: return v.fval!=0.0;\n"
    "        case MCBL_STR:   return v.sval&&v.sval[0]!='\\0';\n"
    "        case MCBL_BOOL:  return v.bval;\n"
    "        default:        return 0;\n"
    "    }\n"
    "}\n"
    "\n"
    "";

static const char MCBL_C_MAIN_OPEN[] = "int main(void) {\n";
static const char MCBL_C_EPILOGUE[] = "    return 0;\n}\n";

/* Forward declare */
static void cgen_collect_devs_c(CGen *g, const AstNode *n);

/* Recursively find dev functions and emit them */
static void cgen_collect_devs_c(CGen *g, const AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < n->block.stmts.count; i++)
                cgen_collect_devs_c(g, n->block.stmts.items[i]);
            break;
        case AST_INC_DECL:
            for (size_t i = 0; i < n->inc.body.count; i++)
                cgen_collect_devs_c(g, n->inc.body.items[i]);
            break;
        case AST_DEV_DECL: {
            /* emit as static function before main */
            g->indent = 0;
            cgen_reset_decls(g);
            cgen_write(g, "\nstatic McblValue mcbl_func_%s(", n->dev.name ? n->dev.name : "_");
            for (size_t i = 0; i < n->dev.params.count; i++) {
                if (i) cgen_write(g, ", ");
                const AstNode *param = n->dev.params.items[i];
                const char *pname = param->sval ? param->sval : "_";
                cgen_write(g, "McblValue mcbl_var_%s", pname);
                cgen_mark_declared(g, pname);
            }
            if (n->dev.params.count == 0) cgen_write(g, "void");
            cgen_write(g, ") {\n");
            g->indent++;
            for (size_t i = 0; i < n->dev.body.count; i++)
                cgen_node(g, n->dev.body.items[i]);
            cgen_indent(g);
            cgen_write(g, "return mcbl_mkint_v(0);\n");
            g->indent--;
            cgen_write(g, "}\n");
            break;
        }
        default: break;
    }
}

int cgen_compile(CGen *g, const AstNode *program) {
    if (!g || !program) return -1;

    /* Emit preamble (helper functions, before main) */
    {
        size_t plen = strlen(MCBL_C_PREAMBLE);
        cgen_grow(g, plen + 1);
        memcpy(g->output + g->len, MCBL_C_PREAMBLE, plen);
        g->len += plen;
        g->output[g->len] = '\0';
    }

    /* Pass 1: emit dev functions before main */
    cgen_collect_devs_c(g, program);

    /* Open main() */
    cgen_reset_decls(g);
    cgen_write(g, "%s", MCBL_C_MAIN_OPEN);

    /* Pass 2: emit main code (dev declarations are skipped) */
    g->indent = 1;
    cgen_node(g, program);
    g->indent = 0;
    cgen_write(g, "%s", MCBL_C_EPILOGUE);

    return g->error ? -1 : 0;
}

int cgen_emit_and_compile(CGen *g, const char *out_c_path, const char *out_bin_path) {
    if (!g || !out_c_path || !out_bin_path) return -1;

    /* Write C source */
    FILE *f = fopen(out_c_path, "w");
    if (!f) { fprintf(stderr, "McBL# codegen: cannot open %s\n", out_c_path); return -1; }
    fwrite(g->output, 1, g->len, f);
    fclose(f);

    /* Invoke GCC */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "gcc -O2 -march=native -o \"%s\" \"%s\" 2>&1",
             out_bin_path, out_c_path);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "McBL# codegen: GCC compilation failed (exit %d)\n", ret);
        return -1;
    }
    return 0;
}

/* v2.0 additions */
void bcgen_set_opt(BytecodeGen *g, int level) {
    if (g) g->opt_level = level;
}
