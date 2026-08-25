#include "bytecode.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
   McBL# Bytecode implementation
   ----------------------------------------------------------------------- */

#define BC_INIT_CAP 256
#define BC_STR_INIT_CAP 32

BcChunk *bc_chunk_create(void) {
    BcChunk *c = (BcChunk *)mcbl_calloc(1, sizeof(BcChunk));
    c->instrs = (BcInstr *)mcbl_malloc(sizeof(BcInstr) * BC_INIT_CAP);
    c->cap    = BC_INIT_CAP;
    c->count  = 0;
    c->strings = (char **)mcbl_malloc(sizeof(char *) * BC_STR_INIT_CAP);
    c->str_cap  = BC_STR_INIT_CAP;
    c->str_count = 0;
    return c;
}

void bc_chunk_destroy(BcChunk *c) {
    if (!c) return;
    for (size_t i = 0; i < c->count; i++) {
        mcbl_free((void **)&c->instrs[i].operand_s);
    }
    mcbl_free((void **)&c->instrs);
    for (size_t i = 0; i < c->str_count; i++) {
        mcbl_free((void **)&c->strings[i]);
    }
    mcbl_free((void **)&c->strings);
    mcbl_free((void **)&c);
}

static int bc_grow(BcChunk *c) {
    if (c->count < c->cap) return 0;
    size_t new_cap = c->cap * 2;
    BcInstr *tmp = (BcInstr *)mcbl_realloc(c->instrs, sizeof(BcInstr) * new_cap);
    c->instrs = tmp;
    c->cap = new_cap;
    return 0;
}

static int bc_emit_raw(BcChunk *c, BytecodeOp op, int64_t oi, double of, const char *os) {
    bc_grow(c);
    BcInstr *ins = &c->instrs[c->count];
    ins->op       = op;
    ins->operand_i = oi;
    ins->operand_f = of;
    ins->operand_s = os ? mcbl_strdup(os) : NULL;
    c->count++;
    return (int)(c->count - 1);
}

int bc_emit(BcChunk *c, BytecodeOp op) {
    return bc_emit_raw(c, op, 0, 0.0, NULL);
}

int bc_emit_i(BcChunk *c, BytecodeOp op, int64_t operand) {
    return bc_emit_raw(c, op, operand, 0.0, NULL);
}

int bc_emit_f(BcChunk *c, BytecodeOp op, double operand) {
    return bc_emit_raw(c, op, 0, operand, NULL);
}

int bc_emit_s(BcChunk *c, BytecodeOp op, const char *s) {
    return bc_emit_raw(c, op, 0, 0.0, s);
}

int bc_intern_string(BcChunk *c, const char *s) {
    /* check for existing */
    for (size_t i = 0; i < c->str_count; i++) {
        if (strcmp(c->strings[i], s) == 0) return (int)i;
    }
    if (c->str_count >= c->str_cap) {
        size_t new_cap = c->str_cap * 2;
        c->strings = (char **)mcbl_realloc(c->strings, sizeof(char *) * new_cap);
        c->str_cap = new_cap;
    }
    c->strings[c->str_count] = mcbl_strdup(s);
    return (int)(c->str_count++);
}

void bc_patch_jump(BcChunk *c, size_t instr_idx) {
    if (instr_idx >= c->count) return;
    c->instrs[instr_idx].operand_i = (int64_t)c->count;
}

void bc_dump(const BcChunk *c) {
        static const char *OPNAMES[] = {
        "PUSH_INT", "PUSH_FLOAT", "PUSH_STR", "PUSH_BOOL", "PUSH_NULL", "POP", "DUP", "SWAP",
        "LOAD", "STORE", "LOAD_GLOBAL", "STORE_GLOBAL", "STORE_ORGATE", "LOAD_FIELD", "STORE_FIELD", "LOAD_STATIC",
        "STORE_STATIC", "LOAD_TYPED", "ADD", "SUB", "MUL", "DIV", "MOD", "NEG",
        "POWER", "ABS", "INCR", "DECR", "ADD_ASSIGN", "SUB_ASSIGN", "MUL_ASSIGN", "DIV_ASSIGN",
        "LSHIFT", "RSHIFT", "BITAND", "BITOR", "BITXOR", "BITNOT", "EQ", "NEQ",
        "LT", "GT", "LE", "GE", "INSTANCEOF", "AND", "OR", "NOT",
        "CONCAT", "SPLIT", "COMBINE", "STR_LEN", "STR_UPPER", "STR_LOWER", "STR_TRIM", "STR_FIND",
        "STR_REPLACE", "STR_SUB", "STR_STARTS", "STR_ENDS", "STR_CONTAINS", "STR_SPLIT2", "STR_JOIN", "STR_REPEAT",
        "STR_REV", "STR_FORMAT", "STR_PAD", "STR_COUNT", "STR_ENCODE", "STR_DECODE", "STR_REGEX", "STR_MATCH",
        "STR_TOINT", "STR_TOFLOAT", "STR_BYTES", "STR_CHAR", "MATH_ABS", "MATH_SQRT", "MATH_POW", "MATH_LOG",
        "MATH_LN", "MATH_EXP", "MATH_SIN", "MATH_COS", "MATH_TAN", "MATH_ASIN", "MATH_ACOS", "MATH_ATAN",
        "MATH_ATAN2", "MATH_FLOOR", "MATH_CEIL", "MATH_ROUND", "MATH_MIN", "MATH_MAX", "MATH_CLAMP", "MATH_LERP",
        "MATH_GCD", "MATH_LCM", "MATH_FACT", "MATH_RAND", "MATH_SEED", "MATH_PI", "MATH_E", "MATH_DERIV",
        "MATH_INTEG", "MATH_SUM", "MATH_PROD", "MATH_MATRIX_NEW", "MATH_MATRIX_GET", "MATH_MATRIX_SET", "MATH_MATRIX_MUL", "MATH_MATRIX_TRANS",
        "MATH_DOT", "MATH_CROSS", "MATH_NORM", "NEW_OBJ", "CALL_METHOD", "CALL_SUPER", "GET_THIS", "CLASS_DEF",
        "FIELD_SET", "FIELD_GET", "ARRAY_NEW", "ARRAY_GET", "ARRAY_SET", "ARRAY_PUSH", "ARRAY_POP", "ARRAY_LEN",
        "ARRAY_SLICE", "MAP_NEW", "MAP_GET", "MAP_SET", "MAP_HAS", "MAP_KEYS", "MAP_VALS", "MAP_DEL",
        "SET_NEW", "SET_ADD", "SET_HAS", "SET_DEL", "TRY_BEGIN", "TRY_END", "THROW", "CATCH",
        "FINALLY", "CAST", "TYPEOF", "SIZEOF", "JMP", "JMP_FALSE", "JMP_TRUE", "CALL",
        "CALL_NATIVE", "RET", "RET_VAL", "BREAK", "CONTINUE", "TERNARY", "PRINT", "INPUT",
        "CARGO_CREATE", "CARGO_ALLOC", "CARGO_CLEAR", "CARGO_FREE", "TRAM_ALLOC", "TRAM_CLEAN", "THREAD_SPAWN", "THREAD_JOIN",
        "ASYNC_CALL", "AWAIT", "CHAN_SEND", "CHAN_RECV", "MUTEX_LOCK", "MUTEX_UNLOCK", "ATOMIC_LOAD", "ATOMIC_STORE",
        "ATOMIC_CAS", "MVM_SPAWN", "MVM_SYNC", "MVM_PIPE", "MVM_KILL", "MVM_OPT", "MOV_REG", "USING_CPU",
        "INC_ENTER", "INC_EXIT", "USE", "WAIT_FOR", "RESPONSE", "READFILE", "FILE_WRITE", "FILE_APPEND",
        "FILE_DELETE", "FILE_EXISTS", "FILE_SIZE", "FILE_LIST", "FILE_MKDIR", "FILE_COPY", "FILE_MOVE", "FILE_LINES",
        "SEND", "CREATEWIN", "OPENPAGE", "NET_GET", "NET_POST", "NET_LISTEN", "NET_CONNECT", "NET_SEND",
        "NET_RECV", "NET_CLOSE", "SYS_EXEC", "SYS_ENV", "SYS_TIME", "SYS_SLEEP", "SYS_EXIT", "SYS_ARGS",
        "SYS_PID", "SYS_MEM", "SYS_CPU", "DEBUG_ASSERT", "DEBUG_TRACE", "DEBUG_LOG", "DEBUG_WATCH", "DEBUG_BREAK",
        "DEBUG_DUMP", "DEBUG_BENCH_START", "DEBUG_BENCH_END", "EXTERN_M", "EXTERN_C", "EXTERN_FILE", "INCLUDE_LIB", "NOP",
        "HALT",
    };
    static const int OPNAMES_COUNT = 233;
    for (size_t i = 0; i < c->count; i++) {
        const BcInstr *ins = &c->instrs[i];
        const char *name = (ins->op < sizeof(OPNAMES)/sizeof(OPNAMES[0]))
                           ? OPNAMES[ins->op] : "??";
        printf("[%4zu] %-16s", i, name);
        if (ins->operand_s) printf("  s=\"%s\"", ins->operand_s);
        else if (ins->operand_i) printf("  i=%lld", (long long)ins->operand_i);
        else if (ins->operand_f) printf("  f=%g", ins->operand_f);
        printf("\n");
    }
}
