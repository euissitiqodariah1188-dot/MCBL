/*
 * cx64_core.cpp — CompilerX64 Main Pipeline
 * ==========================================
 * Translate McBL# source/bytecode → x86_64 native binary.
 * Pipeline: McBL# source → Lexer → AST → IR → Optimize → Emit → Run
 *
 * Cara pakai:
 *   cx64 run   file.cbl      → compile + langsung jalankan
 *   cx64 build file.cbl -o out → compile ke binary
 *   cx64 ir    file.cbl      → dump IR
 *   cx64 bench file.cbl      → benchmark mode (12x unroll)
 */
#include "cx64_defs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <time.h>

#if defined(_WIN32) || defined(__MINGW32__)
  #include <windows.h>
  #define MEM_RWX(sz)  VirtualAlloc(NULL,(sz),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE)
  #define MEM_FREE(p)  VirtualFree((p),0,MEM_RELEASE)
#elif defined(CX64_NO_ASM)
  #include <cstdlib>
  #define MEM_RWX(sz)  malloc(sz)
  #define MEM_FREE(p)  free(p)
#else
  #include <sys/mman.h>
  #include <unistd.h>
  #define MEM_RWX(sz)  mmap(NULL,(sz),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)
  #define MEM_FREE(p)  munmap((p),CX64_CODE_BUF_SZ)
#endif

/* Forward decls */
extern "C" {
    void cx64_asm_call_native(void *fn);
    void cx64_asm_cache_flush(uint8_t *start, size_t len);
    void cx64_asm_loop_burst(void *fn, int count);
    void cx64_asm_mfence(void);
    void cx64_asm_sfence(void);
}
Cx64Compiler *cx64_compiler_create(int cores);
void          cx64_compiler_destroy(Cx64Compiler *c);
int           cx64_compile_and_run(Cx64Compiler *c, const void *ast);
void          cx64_dump_stats(const Cx64Compiler *c);
void          cx64_optimize(Cx64Unit *u, const Cx64OptFlags *flags);
Cx64CodeBuf  *cx64_buf_create(int core_id);
void          cx64_buf_destroy(Cx64CodeBuf *b);
void          cx64_buf_dump(const Cx64CodeBuf *b, size_t max_bytes);
void          cx64_emit_prologue(Cx64CodeBuf *b, int frame_size);
void          cx64_emit_epilogue(Cx64CodeBuf *b);
void          cx64_emit_mov_reg_imm64(Cx64CodeBuf *b, int reg, int64_t imm);
void          cx64_emit_add_reg_imm32(Cx64CodeBuf *b, int reg, int32_t imm);
void          cx64_emit_inc_reg(Cx64CodeBuf *b, int reg);
void          cx64_emit_dec_reg(Cx64CodeBuf *b, int reg);
void          cx64_emit_cmp_reg_imm32(Cx64CodeBuf *b, int reg, int32_t imm);
void          cx64_emit_jl_rel32(Cx64CodeBuf *b, int32_t rel);
void          cx64_emit_jmp_rel32(Cx64CodeBuf *b, int32_t rel);
void          cx64_emit_call_abs(Cx64CodeBuf *b, void *fn);
void          cx64_emit_ret(Cx64CodeBuf *b);
void          cx64_patch_jump(Cx64CodeBuf *b, size_t patch_pos, size_t target_pos);

/* ---------------------------------------------------------------
   Simple McBL# mini-parser (subset) untuk cx64 standalone
   Full parsing di-handle oleh lexer.c/parser.c dari McBL# MSDK.
   cx64_core hanya perlu read pre-parsed token stream.
   --------------------------------------------------------------- */

typedef enum {
    MINI_TOK_EOF = 0,
    MINI_TOK_INT,
    MINI_TOK_FLOAT,
    MINI_TOK_STRING,
    MINI_TOK_IDENT,
    MINI_TOK_HASH,      /* # var */
    MINI_TOK_PR,        /* pr */
    MINI_TOK_INC,       /* inc */
    MINI_TOK_ENDINC,    /* endinc */
    MINI_TOK_FOR,       /* for */
    MINI_TOK_RANGE,     /* range */
    MINI_TOK_DO,        /* do */
    MINI_TOK_IF,        /* if */
    MINI_TOK_ELSE,      /* else */
    MINI_TOK_DEV,       /* dev */
    MINI_TOK_RETURN,    /* return */
    MINI_TOK_PLUS,
    MINI_TOK_MINUS,
    MINI_TOK_STAR,
    MINI_TOK_SLASH,
    MINI_TOK_PERCENT,
    MINI_TOK_EQ,        /* = */
    MINI_TOK_EQEQ,     /* == */
    MINI_TOK_NEQ,       /* != */
    MINI_TOK_LT,
    MINI_TOK_GT,
    MINI_TOK_LE,
    MINI_TOK_GE,
    MINI_TOK_LPAREN,
    MINI_TOK_RPAREN,
    MINI_TOK_LBRACE,
    MINI_TOK_RBRACE,
    MINI_TOK_SEMICOLON,
    MINI_TOK_COMMA,
    MINI_TOK_NEWLINE,
} MiniTok;

struct MiniToken {
    MiniTok     kind;
    char        val[256];
    int         line;
};

struct MiniLexer {
    const char *src;
    size_t      pos;
    size_t      len;
    int         line;
    MiniToken   cur;
};

static void mini_advance(MiniLexer *l) {
    l->pos++;
    if (l->pos <= l->len && l->src[l->pos-1] == '\n') l->line++;
}

static char mini_peek(MiniLexer *l) {
    return l->pos < l->len ? l->src[l->pos] : '\0';
}
static char mini_peek2(MiniLexer *l) {
    return l->pos + 1 < l->len ? l->src[l->pos+1] : '\0';
}

static void mini_skip_ws(MiniLexer *l) {
    while (l->pos < l->len) {
        char c = mini_peek(l);
        if (c == ' ' || c == '\t' || c == '\r') { mini_advance(l); }
        else if (c == '/' && mini_peek2(l) == '/') {
            while (l->pos < l->len && mini_peek(l) != '\n') mini_advance(l);
        }
        else break;
    }
}

static const struct { const char *kw; MiniTok tok; } MINI_KWS[] = {
    {"pr",      MINI_TOK_PR},
    {"inc",     MINI_TOK_INC},
    {"endinc",  MINI_TOK_ENDINC},
    {"for",     MINI_TOK_FOR},
    {"range",   MINI_TOK_RANGE},
    {"do",      MINI_TOK_DO},
    {"if",      MINI_TOK_IF},
    {"else",    MINI_TOK_ELSE},
    {"dev",     MINI_TOK_DEV},
    {"return",  MINI_TOK_RETURN},
    {nullptr,   MINI_TOK_EOF}
};

static void mini_next(MiniLexer *l) {
    mini_skip_ws(l);
    l->cur.val[0] = '\0';
    l->cur.line   = l->line;

    if (l->pos >= l->len) { l->cur.kind = MINI_TOK_EOF; return; }

    char c = mini_peek(l);

    /* newline */
    if (c == '\n') { mini_advance(l); l->cur.kind = MINI_TOK_NEWLINE; return; }

    /* number */
    if (c >= '0' && c <= '9') {
        int j = 0;
        while (l->pos < l->len && mini_peek(l) >= '0' && mini_peek(l) <= '9')
            l->cur.val[j++] = mini_peek(l), mini_advance(l);
        if (mini_peek(l) == '.') {
            l->cur.val[j++] = '.'; mini_advance(l);
            while (l->pos < l->len && mini_peek(l) >= '0' && mini_peek(l) <= '9')
                l->cur.val[j++] = mini_peek(l), mini_advance(l);
            l->cur.kind = MINI_TOK_FLOAT;
        } else {
            l->cur.kind = MINI_TOK_INT;
        }
        l->cur.val[j] = '\0';
        return;
    }

    /* string */
    if (c == '"') {
        mini_advance(l);
        int j = 0;
        while (l->pos < l->len && mini_peek(l) != '"') {
            if (mini_peek(l) == '\\') { mini_advance(l); }
            l->cur.val[j++] = mini_peek(l); mini_advance(l);
        }
        if (l->pos < l->len) mini_advance(l); /* closing " */
        l->cur.val[j] = '\0';
        l->cur.kind = MINI_TOK_STRING;
        return;
    }

    /* ident / keyword */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        int j = 0;
        while (l->pos < l->len && (
               (mini_peek(l) >= 'a' && mini_peek(l) <= 'z') ||
               (mini_peek(l) >= 'A' && mini_peek(l) <= 'Z') ||
               (mini_peek(l) >= '0' && mini_peek(l) <= '9') ||
               mini_peek(l) == '_'))
            l->cur.val[j++] = mini_peek(l), mini_advance(l);
        l->cur.val[j] = '\0';
        l->cur.kind = MINI_TOK_IDENT;
        for (int k = 0; MINI_KWS[k].kw; k++) {
            if (strcmp(l->cur.val, MINI_KWS[k].kw) == 0) {
                l->cur.kind = MINI_KWS[k].tok; break;
            }
        }
        return;
    }

    /* sigil # */
    if (c == '#') { mini_advance(l); l->cur.kind = MINI_TOK_HASH; l->cur.val[0]='#'; l->cur.val[1]=0; return; }

    /* operators */
    mini_advance(l);
    switch (c) {
        case '+': l->cur.kind = MINI_TOK_PLUS;      break;
        case '-': l->cur.kind = MINI_TOK_MINUS;     break;
        case '*': l->cur.kind = MINI_TOK_STAR;      break;
        case '/': l->cur.kind = MINI_TOK_SLASH;     break;
        case '%': l->cur.kind = MINI_TOK_PERCENT;   break;
        case '=': if (mini_peek(l)=='='){mini_advance(l);l->cur.kind=MINI_TOK_EQEQ;}
                  else l->cur.kind=MINI_TOK_EQ; break;
        case '!': if (mini_peek(l)=='='){mini_advance(l);l->cur.kind=MINI_TOK_NEQ;}
                  else l->cur.kind=MINI_TOK_IDENT; break;
        case '<': if (mini_peek(l)=='='){mini_advance(l);l->cur.kind=MINI_TOK_LE;}
                  else l->cur.kind=MINI_TOK_LT; break;
        case '>': if (mini_peek(l)=='='){mini_advance(l);l->cur.kind=MINI_TOK_GE;}
                  else l->cur.kind=MINI_TOK_GT; break;
        case '(': l->cur.kind = MINI_TOK_LPAREN;    break;
        case ')': l->cur.kind = MINI_TOK_RPAREN;    break;
        case '{': l->cur.kind = MINI_TOK_LBRACE;    break;
        case '}': l->cur.kind = MINI_TOK_RBRACE;    break;
        case ';': l->cur.kind = MINI_TOK_SEMICOLON; break;
        case ',': l->cur.kind = MINI_TOK_COMMA;     break;
        default:  l->cur.kind = MINI_TOK_IDENT; l->cur.val[0]=c; l->cur.val[1]=0; break;
    }
}

/* ---------------------------------------------------------------
   Simple variable table for cx64 mini-compiler
   --------------------------------------------------------------- */
#define CX64_VAR_TAB 512
struct Cx64Var {
    char     name[64];
    Cx64Type type;
    int      slot;    /* stack slot index */
    int64_t  const_val; /* if constant-folded */
    int      is_const;
};

struct Cx64CompileCtx {
    Cx64Var      vars[CX64_VAR_TAB];
    int          var_count;
    int          next_slot;
    int          next_label;
    int          loop_depth;
    /* loop stack for break/continue */
    int          loop_end_label[64];
    int          loop_cont_label[64];
    Cx64Unit    *unit;
    Cx64CodeBuf *code;
};

static int ctx_find_var(Cx64CompileCtx *ctx, const char *name) {
    for (int i = 0; i < ctx->var_count; i++)
        if (strcmp(ctx->vars[i].name, name) == 0) return i;
    return -1;
}

static int ctx_new_var(Cx64CompileCtx *ctx, const char *name, Cx64Type type) {
    if (ctx->var_count >= CX64_VAR_TAB) return -1;
    int idx = ctx->var_count++;
    strncpy(ctx->vars[idx].name, name, 63);
    ctx->vars[idx].type      = type;
    ctx->vars[idx].slot      = ctx->next_slot++;
    ctx->vars[idx].is_const  = 0;
    ctx->vars[idx].const_val = 0;
    return idx;
}

static int ctx_new_label(Cx64CompileCtx *ctx) { return ctx->next_label++; }

/* Emit IR helper */
static void ir_emit(Cx64Unit *u, Cx64IROp op, int dst, int a, int b, int64_t imm, int label) {
    if (u->ir_count >= CX64_IR_MAX) return;
    Cx64IR *ir = &u->ir[u->ir_count++];
    ir->op       = op;
    ir->dst      = dst;
    ir->src_a    = a;
    ir->src_b    = b;
    ir->imm      = imm;
    ir->label_id = label;
    ir->is_loop  = (u->loop_depth > 0);
    ir->type     = CX64_T_I64;
}

/* ---------------------------------------------------------------
   Expression parser → IR emitter
   Returns IR virtual register holding the result
   --------------------------------------------------------------- */
static int emit_expr(Cx64CompileCtx *ctx, MiniLexer *l);

static int emit_primary(Cx64CompileCtx *ctx, MiniLexer *l) {
    Cx64Unit *u = ctx->unit;
    if (l->cur.kind == MINI_TOK_INT) {
        int64_t val = atoll(l->cur.val);
        int dst = ctx->next_slot++;
        ir_emit(u, CX64_IR_MOV, dst, -1, -1, val, -1);
        mini_next(l);
        return dst;
    }
    if (l->cur.kind == MINI_TOK_FLOAT) {
        /* treat as int for now */
        int64_t val = (int64_t)atof(l->cur.val);
        int dst = ctx->next_slot++;
        ir_emit(u, CX64_IR_MOV, dst, -1, -1, val, -1);
        mini_next(l);
        return dst;
    }
    if (l->cur.kind == MINI_TOK_STRING) {
        /* String literal — store pointer as int64 */
        char *s = strdup(l->cur.val);
        int dst = ctx->next_slot++;
        ir_emit(u, CX64_IR_MOV, dst, -1, -1, (int64_t)(uintptr_t)s, -1);
        mini_next(l);
        return dst;
    }
    if (l->cur.kind == MINI_TOK_IDENT) {
        int vi = ctx_find_var(ctx, l->cur.val);
        mini_next(l);
        if (vi < 0) return 0;
        if (ctx->vars[vi].is_const) {
            int dst = ctx->next_slot++;
            ir_emit(u, CX64_IR_MOV, dst, -1, -1, ctx->vars[vi].const_val, -1);
            return dst;
        }
        return ctx->vars[vi].slot;
    }
    if (l->cur.kind == MINI_TOK_LPAREN) {
        mini_next(l);
        int r = emit_expr(ctx, l);
        if (l->cur.kind == MINI_TOK_RPAREN) mini_next(l);
        return r;
    }
    if (l->cur.kind == MINI_TOK_MINUS) {
        mini_next(l);
        int r = emit_primary(ctx, l);
        int dst = ctx->next_slot++;
        ir_emit(u, CX64_IR_NEG, dst, r, -1, 0, -1);
        return dst;
    }
    return 0;
}

static int emit_expr_mul(Cx64CompileCtx *ctx, MiniLexer *l) {
    int left = emit_primary(ctx, l);
    while (l->cur.kind == MINI_TOK_STAR   ||
           l->cur.kind == MINI_TOK_SLASH  ||
           l->cur.kind == MINI_TOK_PERCENT) {
        MiniTok op = l->cur.kind;
        mini_next(l);
        int right = emit_primary(ctx, l);
        int dst   = ctx->next_slot++;
        Cx64Unit *u = ctx->unit;
        /* First: copy left into dst */
        ir_emit(u, CX64_IR_MOV, dst, left, -1, 0, -1);
        switch (op) {
            case MINI_TOK_STAR:    ir_emit(u, CX64_IR_MUL, dst, right, -1, 0, -1); break;
            case MINI_TOK_SLASH:   ir_emit(u, CX64_IR_DIV, dst, right, -1, 0, -1); break;
            case MINI_TOK_PERCENT: ir_emit(u, CX64_IR_MOD, dst, right, -1, 0, -1); break;
            default: break;
        }
        left = dst;
    }
    return left;
}

static int emit_expr(Cx64CompileCtx *ctx, MiniLexer *l) {
    int left = emit_expr_mul(ctx, l);
    while (l->cur.kind == MINI_TOK_PLUS || l->cur.kind == MINI_TOK_MINUS) {
        MiniTok op = l->cur.kind;
        mini_next(l);
        int right = emit_expr_mul(ctx, l);
        int dst   = ctx->next_slot++;
        Cx64Unit *u = ctx->unit;
        ir_emit(u, CX64_IR_MOV, dst, left, -1, 0, -1);
        if (op == MINI_TOK_PLUS)  ir_emit(u, CX64_IR_ADD, dst, right, -1, 0, -1);
        else                       ir_emit(u, CX64_IR_SUB, dst, right, -1, 0, -1);
        left = dst;
    }
    return left;
}

/* ---------------------------------------------------------------
   Statement parser → IR emitter
   --------------------------------------------------------------- */
static void emit_stmt(Cx64CompileCtx *ctx, MiniLexer *l);

static void skip_newlines_semi(MiniLexer *l) {
    while (l->cur.kind == MINI_TOK_NEWLINE || l->cur.kind == MINI_TOK_SEMICOLON)
        mini_next(l);
}

static void emit_block_until(Cx64CompileCtx *ctx, MiniLexer *l, MiniTok stop) {
    skip_newlines_semi(l);
    while (l->cur.kind != stop && l->cur.kind != MINI_TOK_EOF) {
        emit_stmt(ctx, l);
        skip_newlines_semi(l);
    }
}

static void emit_stmt(Cx64CompileCtx *ctx, MiniLexer *l) {
    Cx64Unit *u = ctx->unit;
    skip_newlines_semi(l);

    /* # varname = expr */
    if (l->cur.kind == MINI_TOK_HASH) {
        mini_next(l);
        if (l->cur.kind != MINI_TOK_IDENT) return;
        char name[64]; strncpy(name, l->cur.val, 63); mini_next(l);
        if (l->cur.kind == MINI_TOK_EQ) {
            mini_next(l);
            int val = emit_expr(ctx, l);
            int vi  = ctx_find_var(ctx, name);
            if (vi < 0) vi = ctx_new_var(ctx, name, CX64_T_I64);
            if (vi >= 0) ir_emit(u, CX64_IR_MOV, ctx->vars[vi].slot, val, -1, 0, -1);
        } else {
            /* bare declaration */
            ctx_new_var(ctx, name, CX64_T_I64);
        }
        return;
    }

    /* pr(expr) or pr expr */
    if (l->cur.kind == MINI_TOK_PR) {
        mini_next(l);
        bool paren = (l->cur.kind == MINI_TOK_LPAREN);
        if (paren) mini_next(l);
        int val = emit_expr(ctx, l);
        if (paren && l->cur.kind == MINI_TOK_RPAREN) mini_next(l);
        /* Emit CALL printf-style */
        ir_emit(u, CX64_IR_MOV,  REG_RSI, val, -1, 0, -1);
        ir_emit(u, CX64_IR_CALL, -1, -1, -1, (int64_t)(uintptr_t)printf, -1);
        return;
    }

    /* inc(name); ... endinc; */
    if (l->cur.kind == MINI_TOK_INC) {
        mini_next(l);
        if (l->cur.kind == MINI_TOK_LPAREN) {
            mini_next(l);
            if (l->cur.kind != MINI_TOK_RPAREN) mini_next(l); /* skip name */
            if (l->cur.kind == MINI_TOK_RPAREN) mini_next(l);
        }
        skip_newlines_semi(l);
        emit_block_until(ctx, l, MINI_TOK_ENDINC);
        if (l->cur.kind == MINI_TOK_ENDINC) mini_next(l);
        if (l->cur.kind == MINI_TOK_SEMICOLON) mini_next(l);
        return;
    }

    /* for varname range(start, end) do; ... endinc; */
    if (l->cur.kind == MINI_TOK_FOR) {
        mini_next(l);
        char loop_var[64] = {0};
        if (l->cur.kind == MINI_TOK_IDENT) {
            strncpy(loop_var, l->cur.val, 63); mini_next(l);
        }
        int64_t range_start = 0, range_end = 10;
        if (l->cur.kind == MINI_TOK_RANGE) {
            mini_next(l);
            if (l->cur.kind == MINI_TOK_LPAREN) mini_next(l);
            if (l->cur.kind == MINI_TOK_INT) { range_start = atoll(l->cur.val); mini_next(l); }
            if (l->cur.kind == MINI_TOK_COMMA) mini_next(l);
            if (l->cur.kind == MINI_TOK_INT) { range_end   = atoll(l->cur.val); mini_next(l); }
            if (l->cur.kind == MINI_TOK_RPAREN) mini_next(l);
        }
        while (l->cur.kind == MINI_TOK_DO || l->cur.kind == MINI_TOK_SEMICOLON ||
               l->cur.kind == MINI_TOK_NEWLINE) mini_next(l);

        /* Emit loop IR:
         *   MOV loop_var, range_start
         *   LOOP_BEGIN
         *   CMP loop_var, range_end
         *   JGE loop_end_label
         *   ... body ...
         *   INC loop_var
         *   JMP loop_begin_label
         *   LOOP_END
         *   LABEL loop_end_label
         */
        int vi = ctx_find_var(ctx, loop_var);
        if (vi < 0) vi = ctx_new_var(ctx, loop_var, CX64_T_I64);
        int slot = (vi >= 0) ? ctx->vars[vi].slot : ctx->next_slot++;

        int lbl_begin = ctx_new_label(ctx);
        int lbl_end   = ctx_new_label(ctx);
        ctx->loop_end_label[ctx->loop_depth]  = lbl_end;
        ctx->loop_cont_label[ctx->loop_depth] = lbl_begin;
        ctx->loop_depth++;
        u->loop_depth++;

        /* Init counter */
        ir_emit(u, CX64_IR_MOV, slot, -1, -1, range_start, -1);
        /* Mark loop begin */
        ir_emit(u, CX64_IR_LOOP_BEGIN, -1, -1, -1, 0, -1);
        ir_emit(u, CX64_IR_LABEL, -1, -1, -1, 0, lbl_begin);
        /* Condition: if slot >= range_end → exit */
        int end_reg = ctx->next_slot++;
        ir_emit(u, CX64_IR_MOV, end_reg, -1, -1, range_end, -1);
        ir_emit(u, CX64_IR_CMP, slot, end_reg, -1, 0, -1);
        ir_emit(u, CX64_IR_JGE, -1, -1, -1, 0, lbl_end);

        /* Body */
        emit_block_until(ctx, l, MINI_TOK_ENDINC);
        if (l->cur.kind == MINI_TOK_ENDINC) mini_next(l);
        if (l->cur.kind == MINI_TOK_SEMICOLON) mini_next(l);

        /* Increment + loop back */
        ir_emit(u, CX64_IR_INC, slot, -1, -1, 0, -1);
        ir_emit(u, CX64_IR_JMP, -1, -1, -1, 0, lbl_begin);
        ir_emit(u, CX64_IR_LOOP_END, -1, -1, -1, 0, -1);
        ir_emit(u, CX64_IR_LABEL, -1, -1, -1, 0, lbl_end);

        ctx->loop_depth--;
        u->loop_depth--;
        return;
    }

    /* dev name(params); ... return expr endinc; */
    if (l->cur.kind == MINI_TOK_DEV) {
        mini_next(l);
        char fname[64] = {0};
        if (l->cur.kind == MINI_TOK_IDENT) { strncpy(fname,l->cur.val,63); mini_next(l); }
        /* Skip params */
        if (l->cur.kind == MINI_TOK_LPAREN) {
            int depth = 1; mini_next(l);
            while (depth > 0 && l->cur.kind != MINI_TOK_EOF) {
                if (l->cur.kind == MINI_TOK_LPAREN) depth++;
                if (l->cur.kind == MINI_TOK_RPAREN) depth--;
                mini_next(l);
            }
        }
        skip_newlines_semi(l);
        ir_emit(u, CX64_IR_FUNC_BEGIN, -1,-1,-1,0,-1);
        emit_block_until(ctx, l, MINI_TOK_ENDINC);
        if (l->cur.kind == MINI_TOK_ENDINC) mini_next(l);
        if (l->cur.kind == MINI_TOK_SEMICOLON) mini_next(l);
        ir_emit(u, CX64_IR_FUNC_END, -1,-1,-1,0,-1);
        ir_emit(u, CX64_IR_RET, -1,-1,-1,0,-1);
        return;
    }

    /* return expr */
    if (l->cur.kind == MINI_TOK_RETURN) {
        mini_next(l);
        int r = emit_expr(ctx, l);
        ir_emit(u, CX64_IR_MOV, REG_RAX, r, -1, 0, -1);
        ir_emit(u, CX64_IR_RET_VAL, -1,-1,-1,0,-1);
        return;
    }

    /* Expression statement: ident = expr  or  bare expr */
    if (l->cur.kind == MINI_TOK_IDENT) {
        char name[64]; strncpy(name, l->cur.val, 63); mini_next(l);
        if (l->cur.kind == MINI_TOK_EQ) {
            mini_next(l);
            int val = emit_expr(ctx, l);
            int vi  = ctx_find_var(ctx, name);
            if (vi < 0) vi = ctx_new_var(ctx, name, CX64_T_I64);
            if (vi >= 0) ir_emit(u, CX64_IR_MOV, ctx->vars[vi].slot, val, -1, 0, -1);
        }
        /* else: bare ident, ignore */
        return;
    }

    /* Skip unknown token */
    mini_next(l);
}

/* ---------------------------------------------------------------
   Fallback JIT — pakai C printf untuk output saat ASM tidak tersedia
   --------------------------------------------------------------- */
struct FallbackCtx {
    int64_t vars[512];
    int     var_count;
};

/* Fast loop runner — eksekusi loop count kali, makin banyak makin cepat */
static void run_native_loop(void (*fn)(void), int count) {
    for (int i = 0; i < count; i++) fn();
}

/* ---------------------------------------------------------------
   Main compile + run pipeline
   --------------------------------------------------------------- */
static int read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f); rewind(f);
    *out = (char *)malloc(sz + 1);
    fread(*out, 1, sz, f); (*out)[sz] = '\0';
    fclose(f);
    return (int)sz;
}

static int do_run(const char *src_path, int cores, int bench, int dump_ir) {
    char *src = nullptr;
    if (read_file(src_path, &src) < 0) {
        fprintf(stderr, "[CX64] Cannot read '%s'\n", src_path);
        return 1;
    }

    printf("[CX64] Compiling '%s' → x86_64 (%d cores, unroll=%d)\n",
           src_path, cores, CX64_LOOP_UNROLL);

    /* Set up compile context */
    Cx64Unit unit;
    memset(&unit, 0, sizeof(unit));

    Cx64CompileCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.unit = &unit;

    /* Allocate code buffer */
    Cx64CodeBuf *code = cx64_buf_create(0);
    if (!code) { fprintf(stderr, "[CX64] OOM\n"); free(src); return 1; }
    ctx.code  = code;
    unit.code = code;

    /* Lex + parse → IR */
    MiniLexer lex;
    memset(&lex, 0, sizeof(lex));
    lex.src  = src;
    lex.len  = strlen(src);
    lex.line = 1;
    mini_next(&lex); /* prime */

    int64_t t_parse0 = (int64_t)clock();
    while (lex.cur.kind != MINI_TOK_EOF)
        emit_stmt(&ctx, &lex);
    ir_emit(&unit, CX64_IR_RET, -1,-1,-1,0,-1);
    int64_t t_parse1 = (int64_t)clock();

    printf("[CX64] Parse→IR: %d instructions (%.3f ms)\n",
           unit.ir_count,
           (double)(t_parse1 - t_parse0) / CLOCKS_PER_SEC * 1000.0);

    if (dump_ir) {
        printf("\n--- IR Dump (%d instrs) ---\n", unit.ir_count);
        static const char *op_names[] = {
            "MOV","LOAD","STORE","LEA","PUSH","POP",
            "ADD","SUB","MUL","DIV","MOD","NEG","INC","DEC",
            "SHL","SHR","AND","OR","XOR","NOT",
            "FADD","FSUB","FMUL","FDIV","FNEG","FSQRT","I2F","F2I",
            "CMP","FCMP","SETEQ","SETNE","SETLT","SETLE","SETGT","SETGE",
            "JMP","JE","JNE","JL","JLE","JG","JGE","CALL","RET","RETV",
            "LOOP_BEG","LOOP_END","LOOP_CTR","SYSCALL","NOP","LABEL",
            "FUNC_BEG","FUNC_END"
        };
        for (int i = 0; i < unit.ir_count && i < 200; i++) {
            const Cx64IR *ir = &unit.ir[i];
            int op = (int)ir->op;
            const char *opn = (op >= 0 && op < (int)(sizeof(op_names)/sizeof(*op_names))) ?
                               op_names[op] : "???";
            if (ir->label_id >= 0)
                printf("  [%04d] %-10s dst=%d a=%d b=%d imm=%lld label=L%d\n",
                       i, opn, ir->dst, ir->src_a, ir->src_b,
                       (long long)ir->imm, ir->label_id);
            else
                printf("  [%04d] %-10s dst=%d a=%d b=%d imm=%lld\n",
                       i, opn, ir->dst, ir->src_a, ir->src_b, (long long)ir->imm);
        }
        if (unit.ir_count > 200) printf("  ... (%d more)\n", unit.ir_count - 200);
        printf("---\n\n");
    }

    /* Optimize */
    Cx64OptFlags opt = cx64_opt_default();
    if (bench) opt.unroll_factor = CX64_LOOP_UNROLL;

    int64_t t_opt0 = (int64_t)clock();
    cx64_optimize(&unit, &opt);
    int64_t t_opt1 = (int64_t)clock();
    printf("[CX64] Optimize: %.3f ms (%d IR instrs after)\n",
           (double)(t_opt1 - t_opt0) / CLOCKS_PER_SEC * 1000.0,
           unit.ir_count);

    /* Emit native x86_64 */
    int64_t t_emit0 = (int64_t)clock();

    /* Build code: prologue + loop body + epilogue */
    cx64_emit_prologue(code, 256 + ctx.next_slot * 8);

    /* === Emit each IR to native x86_64 === */
    /* Track label positions (offset in code->buf) */
    size_t label_map[CX64_LABEL_MAX] = {0};
    struct PJump { size_t patch_at; int lbl; };
    static PJump pj[8192];
    int pj_count = 0;

    for (int i = 0; i < unit.ir_count; i++) {
        const Cx64IR *ir = &unit.ir[i];
        int d = ir->dst, a = ir->src_a;
        int pd = (d>=0&&d<CX64_VAR_MAX&&unit.vars[d].in_reg>=0)?unit.vars[d].in_reg:REG_RAX;
        int pa = (a>=0&&a<CX64_VAR_MAX&&unit.vars[a].in_reg>=0)?unit.vars[a].in_reg:REG_RCX;

        switch (ir->op) {
            case CX64_IR_MOV:
                cx64_emit_mov_reg_imm64(code, pd, ir->imm);
                break;
            case CX64_IR_ADD:
                cx64_emit_add_reg_imm32(code, pd, (int32_t)ir->imm);
                break;
            case CX64_IR_INC:
                cx64_emit_inc_reg(code, pd);
                break;
            case CX64_IR_DEC:
                cx64_emit_dec_reg(code, pd);
                break;
            case CX64_IR_CMP:
                cx64_emit_cmp_reg_imm32(code, pd, (int32_t)ir->imm);
                break;
            case CX64_IR_JMP:
                if (ir->label_id >= 0) {
                    pj[pj_count++] = { code->used + 1, ir->label_id };
                    cx64_emit_jmp_rel32(code, 0);
                }
                break;
            case CX64_IR_JGE:
            case CX64_IR_JG:
                if (ir->label_id >= 0) {
                    pj[pj_count++] = { code->used + 2, ir->label_id };
                    cx64_emit_jl_rel32(code, 0); /* inverse: skip if NOT < → JL to continue */
                }
                break;
            case CX64_IR_JL:
                if (ir->label_id >= 0) {
                    pj[pj_count++] = { code->used + 2, ir->label_id };
                    cx64_emit_jl_rel32(code, 0);
                }
                break;
            case CX64_IR_LABEL:
                if (ir->label_id >= 0 && ir->label_id < CX64_LABEL_MAX)
                    label_map[ir->label_id] = code->used;
                break;
            case CX64_IR_CALL:
                cx64_emit_call_abs(code, (void *)(uintptr_t)ir->imm);
                break;
            case CX64_IR_RET:
            case CX64_IR_RET_VAL:
            case CX64_IR_FUNC_END:
                cx64_emit_epilogue(code);
                i = unit.ir_count; /* done */
                break;
            case CX64_IR_LOOP_BEGIN:
            case CX64_IR_LOOP_END:
            case CX64_IR_LOOP_CTR:
            case CX64_IR_FUNC_BEGIN:
            case CX64_IR_NOP:
                /* No emission */
                break;
            default:
                break;
        }
    }

    /* Patch all pending jumps */
    for (int j = 0; j < pj_count; j++) {
        int lbl = pj[j].lbl;
        if (lbl >= 0 && lbl < CX64_LABEL_MAX && label_map[lbl] > 0)
            cx64_patch_jump(code, pj[j].patch_at, label_map[lbl]);
    }

    int64_t t_emit1 = (int64_t)clock();
    printf("[CX64] Emit:     %.3f ms (%zu bytes native code)\n",
           (double)(t_emit1 - t_emit0) / CLOCKS_PER_SEC * 1000.0,
           code->used);

    /* Flush icache */
    cx64_asm_cache_flush(code->buf, code->used);
    cx64_asm_mfence();

    /* Execute */
    auto entry = (void(*)())code->buf;
    int64_t t_exec0 = (int64_t)clock();

#ifdef CX64_NO_ASM
    /* NO_ASM mode: buffer not executable — show what would run */
    printf("[CX64] Emit complete: %zu bytes of x86_64 machine code generated\n", code->used);
    printf("[CX64] (NO_ASM mode: native exec disabled — use NASM for real execution)\n");
    if (bench) {
        printf("[CX64] Bench mode: %d cores × %d runs/burst = %d total executions\n",
               cores, CX64_LOOP_UNROLL, cores * CX64_LOOP_UNROLL);
        printf("[CX64] Optimizer stats above show what was applied to the IR\n");
    }
    (void)entry;
#else
    if (bench) {
        /* Benchmark mode: run 12x per 0.1s burst on each of 4 cores */
        Cx64Compiler *compiler = cx64_compiler_create(cores);
        if (compiler) {
            for (int core = 0; core < cores; core++) {
                compiler->tasks[core].entry    = entry;
                compiler->tasks[core].code     = code;
                compiler->tasks[core].done     = 1;
                compiler->tasks[core].exec_count = 0;
            }
            cx64_compile_and_run(compiler, nullptr);
            cx64_dump_stats(compiler);
            cx64_compiler_destroy(compiler);
        }
    } else {
        cx64_asm_call_native((void *)entry);
    }
#endif

    int64_t t_exec1 = (int64_t)clock();
    printf("[CX64] Execute:  %.3f ms\n",
           (double)(t_exec1 - t_exec0) / CLOCKS_PER_SEC * 1000.0);

    cx64_buf_destroy(code);
    free(src);
    return 0;
}

/* ---------------------------------------------------------------
   main() — cx64 command-line interface
   --------------------------------------------------------------- */
static void print_usage() {
    printf("cx64 — McBL# CompilerX64  (x86_64 native, 4-core JIT)\n\n");
    printf("Usage:\n");
    printf("  cx64 run   <file.cbl>           compile + run (1-shot)\n");
    printf("  cx64 bench <file.cbl>           compile + run 12x/0.1s burst\n");
    printf("  cx64 ir    <file.cbl>           compile + dump IR\n");
    printf("  cx64 build <file.cbl> -o <out>  compile to native binary\n");
    printf("  cx64 cores <n> <file.cbl>       run on N cores (1-4)\n\n");
    printf("Features:\n");
    printf("  - Direct x86_64 machine code emission (no C intermediate)\n");
    printf("  - 4-core parallel JIT via pthread\n");
    printf("  - Loop unrolling: %d runs per 0.1s burst\n", CX64_LOOP_UNROLL);
    printf("  - Optimizations: constant fold, DCE, strength reduce,\n");
    printf("                   loop unroll, register alloc, peephole\n");
    printf("  - ASM core: cx64_asm.asm (RDTSC, cache flush, burst loop)\n");
    printf("  - Static typing: size known at compile time\n");
    printf("  - Zero GC: pure refcount memory management\n\n");
}

int main(int argc, char **argv) {
    if (argc < 3) { print_usage(); return 0; }

    const char *cmd  = argv[1];
    int   cores      = 4;
    int   bench      = 0;
    int   dump_ir    = 0;
    const char *file = nullptr;

    if (strcmp(cmd, "run") == 0) {
        file = argv[2];
    } else if (strcmp(cmd, "bench") == 0) {
        file = argv[2]; bench = 1;
    } else if (strcmp(cmd, "ir") == 0) {
        file = argv[2]; dump_ir = 1;
    } else if (strcmp(cmd, "cores") == 0 && argc >= 4) {
        cores = atoi(argv[2]);
        file  = argv[3];
        if (cores < 1) cores = 1;
        if (cores > 4) cores = 4;
    } else if (strcmp(cmd, "build") == 0) {
        /* TODO: write binary to file */
        file = argv[2];
        printf("[CX64] build mode: compile to binary (ELF/PE output planned)\n");
    } else {
        file = argv[2];
    }

    if (!file) { print_usage(); return 1; }
    return do_run(file, cores, bench, dump_ir);
}
