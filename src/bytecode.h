#ifndef MCBL_BYTECODE_H
#define MCBL_BYTECODE_H

#include <stddef.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
   McBL# Bytecode  –  compact instruction set executed by the MDK VM
   v2.0 — UPGRADED: OOP, math, array, error handling, async, MVM
   ----------------------------------------------------------------------- */

typedef enum {
    /* Stack ops */
    OP_BC_PUSH_INT,
    OP_BC_PUSH_FLOAT,
    OP_BC_PUSH_STR,
    OP_BC_PUSH_BOOL,
    OP_BC_PUSH_NULL,
    OP_BC_POP,
    OP_BC_DUP,
    OP_BC_SWAP,

    /* Variables */
    OP_BC_LOAD,
    OP_BC_STORE,
    OP_BC_LOAD_GLOBAL,
    OP_BC_STORE_GLOBAL,
    OP_BC_STORE_ORGATE,
    OP_BC_LOAD_FIELD,        /* obj.field → stack */
    OP_BC_STORE_FIELD,       /* stack → obj.field */
    OP_BC_LOAD_STATIC,       /* ClassName.field   */
    OP_BC_STORE_STATIC,
    OP_BC_LOAD_TYPED,        /* typed variable    */

    /* Arithmetic */
    OP_BC_ADD,
    OP_BC_SUB,
    OP_BC_MUL,
    OP_BC_DIV,
    OP_BC_MOD,
    OP_BC_NEG,
    OP_BC_POWER,             /* ** exponentiation */
    OP_BC_ABS,
    OP_BC_INCR,              /* ++ */
    OP_BC_DECR,              /* -- */
    OP_BC_ADD_ASSIGN,        /* += */
    OP_BC_SUB_ASSIGN,        /* -= */
    OP_BC_MUL_ASSIGN,        /* *= */
    OP_BC_DIV_ASSIGN,        /* /= */

    /* Bitwise */
    OP_BC_LSHIFT,
    OP_BC_RSHIFT,
    OP_BC_BITAND,
    OP_BC_BITOR,
    OP_BC_BITXOR,
    OP_BC_BITNOT,

    /* Comparison */
    OP_BC_EQ,
    OP_BC_NEQ,
    OP_BC_LT,
    OP_BC_GT,
    OP_BC_LE,
    OP_BC_GE,
    OP_BC_INSTANCEOF,        /* type check */

    /* Logic */
    OP_BC_AND,
    OP_BC_OR,
    OP_BC_NOT,

    /* String */
    OP_BC_CONCAT,
    OP_BC_SPLIT,
    OP_BC_COMBINE,
    OP_BC_STR_LEN,
    OP_BC_STR_UPPER,
    OP_BC_STR_LOWER,
    OP_BC_STR_TRIM,
    OP_BC_STR_FIND,
    OP_BC_STR_REPLACE,
    OP_BC_STR_SUB,
    OP_BC_STR_STARTS,
    OP_BC_STR_ENDS,
    OP_BC_STR_CONTAINS,
    OP_BC_STR_SPLIT2,
    OP_BC_STR_JOIN,
    OP_BC_STR_REPEAT,
    OP_BC_STR_REV,
    OP_BC_STR_FORMAT,
    OP_BC_STR_PAD,
    OP_BC_STR_COUNT,
    OP_BC_STR_ENCODE,
    OP_BC_STR_DECODE,
    OP_BC_STR_REGEX,
    OP_BC_STR_MATCH,
    OP_BC_STR_TOINT,
    OP_BC_STR_TOFLOAT,
    OP_BC_STR_BYTES,
    OP_BC_STR_CHAR,

    /* Math stdlib */
    OP_BC_MATH_ABS,
    OP_BC_MATH_SQRT,
    OP_BC_MATH_POW,
    OP_BC_MATH_LOG,
    OP_BC_MATH_LN,
    OP_BC_MATH_EXP,
    OP_BC_MATH_SIN,
    OP_BC_MATH_COS,
    OP_BC_MATH_TAN,
    OP_BC_MATH_ASIN,
    OP_BC_MATH_ACOS,
    OP_BC_MATH_ATAN,
    OP_BC_MATH_ATAN2,
    OP_BC_MATH_FLOOR,
    OP_BC_MATH_CEIL,
    OP_BC_MATH_ROUND,
    OP_BC_MATH_MIN,
    OP_BC_MATH_MAX,
    OP_BC_MATH_CLAMP,
    OP_BC_MATH_LERP,
    OP_BC_MATH_GCD,
    OP_BC_MATH_LCM,
    OP_BC_MATH_FACT,
    OP_BC_MATH_RAND,
    OP_BC_MATH_SEED,
    OP_BC_MATH_PI,
    OP_BC_MATH_E,
    OP_BC_MATH_DERIV,        /* numerical derivative */
    OP_BC_MATH_INTEG,        /* numerical integration */
    OP_BC_MATH_SUM,          /* summation */
    OP_BC_MATH_PROD,         /* product */
    OP_BC_MATH_MATRIX_NEW,
    OP_BC_MATH_MATRIX_GET,
    OP_BC_MATH_MATRIX_SET,
    OP_BC_MATH_MATRIX_MUL,
    OP_BC_MATH_MATRIX_TRANS,
    OP_BC_MATH_DOT,
    OP_BC_MATH_CROSS,
    OP_BC_MATH_NORM,

    /* OOP */
    OP_BC_NEW_OBJ,           /* create instance of class */
    OP_BC_CALL_METHOD,       /* obj.method(args)         */
    OP_BC_CALL_SUPER,        /* super(args)              */
    OP_BC_GET_THIS,          /* push this onto stack     */
    OP_BC_CLASS_DEF,         /* define class in table    */
    OP_BC_FIELD_SET,
    OP_BC_FIELD_GET,

    /* Array/Map/Set */
    OP_BC_ARRAY_NEW,         /* new array of n elements  */
    OP_BC_ARRAY_GET,
    OP_BC_ARRAY_SET,
    OP_BC_ARRAY_PUSH,
    OP_BC_ARRAY_POP,
    OP_BC_ARRAY_LEN,
    OP_BC_ARRAY_SLICE,
    OP_BC_MAP_NEW,
    OP_BC_MAP_GET,
    OP_BC_MAP_SET,
    OP_BC_MAP_HAS,
    OP_BC_MAP_KEYS,
    OP_BC_MAP_VALS,
    OP_BC_MAP_DEL,
    OP_BC_SET_NEW,
    OP_BC_SET_ADD,
    OP_BC_SET_HAS,
    OP_BC_SET_DEL,

    /* Error handling */
    OP_BC_TRY_BEGIN,         /* push try frame           */
    OP_BC_TRY_END,           /* pop try frame            */
    OP_BC_THROW,             /* throw top of stack       */
    OP_BC_CATCH,             /* catch clause entry       */
    OP_BC_FINALLY,           /* finally entry            */

    /* Type ops */
    OP_BC_CAST,              /* type cast                */
    OP_BC_TYPEOF,
    OP_BC_SIZEOF,

    /* Control flow */
    OP_BC_JMP,
    OP_BC_JMP_FALSE,
    OP_BC_JMP_TRUE,
    OP_BC_CALL,
    OP_BC_CALL_NATIVE,
    OP_BC_RET,
    OP_BC_RET_VAL,
    OP_BC_BREAK,
    OP_BC_CONTINUE,
    OP_BC_TERNARY,

    /* I/O */
    OP_BC_PRINT,
    OP_BC_INPUT,

    /* Memory */
    OP_BC_CARGO_CREATE,
    OP_BC_CARGO_ALLOC,
    OP_BC_CARGO_CLEAR,
    OP_BC_CARGO_FREE,
    OP_BC_TRAM_ALLOC,        /* WriteTRam(mb)            */
    OP_BC_TRAM_CLEAN,        /* cleanTram()              */

    /* Thread / Async */
    OP_BC_THREAD_SPAWN,
    OP_BC_THREAD_JOIN,
    OP_BC_ASYNC_CALL,        /* call async dev           */
    OP_BC_AWAIT,             /* await async result       */
    OP_BC_CHAN_SEND,
    OP_BC_CHAN_RECV,
    OP_BC_MUTEX_LOCK,
    OP_BC_MUTEX_UNLOCK,
    OP_BC_ATOMIC_LOAD,
    OP_BC_ATOMIC_STORE,
    OP_BC_ATOMIC_CAS,        /* compare and swap         */

    /* MVM — McBL Virtual Machine cores */
    OP_BC_MVM_SPAWN,         /* spawn worker on N cores  */
    OP_BC_MVM_SYNC,          /* barrier sync             */
    OP_BC_MVM_PIPE,          /* send data through pipe   */
    OP_BC_MVM_KILL,          /* kill worker              */
    OP_BC_MVM_OPT,           /* optimizer hint           */

    /* CPU */
    OP_BC_MOV_REG,
    OP_BC_USING_CPU,

    /* INC / USE */
    OP_BC_INC_ENTER,
    OP_BC_INC_EXIT,
    OP_BC_USE,
    OP_BC_WAIT_FOR,
    OP_BC_RESPONSE,

    /* File */
    OP_BC_READFILE,
    OP_BC_FILE_WRITE,
    OP_BC_FILE_APPEND,
    OP_BC_FILE_DELETE,
    OP_BC_FILE_EXISTS,
    OP_BC_FILE_SIZE,
    OP_BC_FILE_LIST,
    OP_BC_FILE_MKDIR,
    OP_BC_FILE_COPY,
    OP_BC_FILE_MOVE,
    OP_BC_FILE_LINES,
    OP_BC_SEND,
    OP_BC_CREATEWIN,
    OP_BC_OPENPAGE,

    /* Network */
    OP_BC_NET_GET,
    OP_BC_NET_POST,
    OP_BC_NET_LISTEN,
    OP_BC_NET_CONNECT,
    OP_BC_NET_SEND,
    OP_BC_NET_RECV,
    OP_BC_NET_CLOSE,

    /* System */
    OP_BC_SYS_EXEC,
    OP_BC_SYS_ENV,
    OP_BC_SYS_TIME,
    OP_BC_SYS_SLEEP,
    OP_BC_SYS_EXIT,
    OP_BC_SYS_ARGS,
    OP_BC_SYS_PID,
    OP_BC_SYS_MEM,
    OP_BC_SYS_CPU,

    /* Debug */
    OP_BC_DEBUG_ASSERT,
    OP_BC_DEBUG_TRACE,
    OP_BC_DEBUG_LOG,
    OP_BC_DEBUG_WATCH,
    OP_BC_DEBUG_BREAK,
    OP_BC_DEBUG_DUMP,
    OP_BC_DEBUG_BENCH_START,
    OP_BC_DEBUG_BENCH_END,

    /* Extern interop */
    OP_BC_EXTERN_M,
    OP_BC_EXTERN_C,
    OP_BC_EXTERN_FILE,       /* externFile(name.c)       */
    OP_BC_INCLUDE_LIB,       /* include<lib.cll>         */

    /* Halt */
    OP_BC_NOP,
    OP_BC_HALT
} BytecodeOp;

/* A single bytecode instruction */
typedef struct {
    BytecodeOp  op;
    int64_t     operand_i;
    double      operand_f;
    char       *operand_s;
} BcInstr;

typedef struct {
    BcInstr  *instrs;
    size_t    count;
    size_t    cap;

    /* constant string table */
    char    **strings;
    size_t    str_count;
    size_t    str_cap;
} BcChunk;

/* Lifecycle */
BcChunk *bc_chunk_create(void);
void     bc_chunk_destroy(BcChunk *chunk);

/* Emission helpers */
int      bc_emit(BcChunk *c, BytecodeOp op);
int      bc_emit_i(BcChunk *c, BytecodeOp op, int64_t operand);
int      bc_emit_f(BcChunk *c, BytecodeOp op, double operand);
int      bc_emit_s(BcChunk *c, BytecodeOp op, const char *s);

/* String table — interns a string, returns its index */
int      bc_intern_string(BcChunk *c, const char *s);

/* Patch a jump instruction's target */
void     bc_patch_jump(BcChunk *c, size_t instr_idx);

/* Debug dump */
void     bc_dump(const BcChunk *c);

#endif /* MCBL_BYTECODE_H */
