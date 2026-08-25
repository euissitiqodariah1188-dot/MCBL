#ifndef CX64_DEFS_H
#define CX64_DEFS_H

/*
 * CompilerX64 — McBL# Native x86_64 Compiler
 * ============================================
 * Translate McBL# bytecode langsung ke x86_64 binary machine code.
 * - 4 core CPU parallel execution
 * - JIT dengan loop unrolling (12 run per 0.1 detik per loop iteration)
 * - Super-speed direct-to-CPU emit
 * - Zero interpreter overhead — pure native binary
 *
 * Files:
 *   cx64_asm.asm   — x86_64 ASM core routines (NASM syntax)
 *   cx64_emit.cpp  — C++ x86_64 machine code emitter
 *   cx64_opt.cpp   — Optimizer (loop unrolling, DCE, CFG)
 *   cx64_jit.cpp   — JIT dispatcher (4-core + hot-path detection)
 *   cx64_core.cpp  — Main compiler pipeline
 *   cx64_defs.h    — Shared types/constants
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Machine register IDs (x86_64 calling convention) ---- */
#define REG_RAX  0
#define REG_RCX  1
#define REG_RDX  2
#define REG_RBX  3
#define REG_RSP  4
#define REG_RBP  5
#define REG_RSI  6
#define REG_RDI  7
#define REG_R8   8
#define REG_R9   9
#define REG_R10  10
#define REG_R11  11
#define REG_R12  12
#define REG_R13  13
#define REG_R14  14
#define REG_R15  15

/* XMM registers for float ops */
#define XMM0  0
#define XMM1  1
#define XMM2  2
#define XMM3  3
#define XMM4  4
#define XMM5  5
#define XMM6  6
#define XMM7  7

/* ---- CX64 JIT Code Buffer ---- */
#define CX64_CODE_BUF_SZ  (32 * 1024 * 1024)   /* 32 MB JIT buffer per core */
#define CX64_MAX_CORES    4
#define CX64_LOOP_UNROLL  12                     /* 12 iterations per 0.1s burst */
#define CX64_BURST_NS     100000000LL            /* 0.1 second in nanoseconds */

typedef struct {
    uint8_t  *buf;        /* RWX memory — code goes here */
    size_t    size;       /* allocated bytes */
    size_t    used;       /* bytes written */
    uint32_t  core_id;   /* which CPU core owns this buffer */
} Cx64CodeBuf;

/* ---- CX64 Value types — statically typed ---- */
typedef enum {
    CX64_T_I8   = 0,
    CX64_T_I16  = 1,
    CX64_T_I32  = 2,
    CX64_T_I64  = 3,
    CX64_T_F32  = 4,
    CX64_T_F64  = 5,
    CX64_T_PTR  = 6,
    CX64_T_BOOL = 7,
    CX64_T_VOID = 8
} Cx64Type;

static inline size_t cx64_type_bytes(Cx64Type t) {
    switch (t) {
        case CX64_T_I8:   return 1;
        case CX64_T_I16:  return 2;
        case CX64_T_I32:  return 4;
        case CX64_T_I64:  return 8;
        case CX64_T_F32:  return 4;
        case CX64_T_F64:  return 8;
        case CX64_T_PTR:  return 8;
        case CX64_T_BOOL: return 1;
        default:          return 0;
    }
}

/* ---- CX64 IR Instruction (between McBL# AST and native code) ---- */
typedef enum {
    /* Memory */
    CX64_IR_MOV,         /* dst = src */
    CX64_IR_LOAD,        /* dst = *ptr */
    CX64_IR_STORE,       /* *ptr = src */
    CX64_IR_LEA,         /* dst = &var */
    CX64_IR_PUSH,
    CX64_IR_POP,

    /* Arithmetic — int */
    CX64_IR_ADD,
    CX64_IR_SUB,
    CX64_IR_MUL,
    CX64_IR_DIV,
    CX64_IR_MOD,
    CX64_IR_NEG,
    CX64_IR_INC,
    CX64_IR_DEC,
    CX64_IR_SHL,
    CX64_IR_SHR,
    CX64_IR_AND,
    CX64_IR_OR,
    CX64_IR_XOR,
    CX64_IR_NOT,

    /* Arithmetic — float (SSE2) */
    CX64_IR_FADD,
    CX64_IR_FSUB,
    CX64_IR_FMUL,
    CX64_IR_FDIV,
    CX64_IR_FNEG,
    CX64_IR_FSQRT,
    CX64_IR_FCONV_I2F,   /* int → float */
    CX64_IR_FCONV_F2I,   /* float → int */

    /* Comparison */
    CX64_IR_CMP,
    CX64_IR_FCMP,
    CX64_IR_SETEQ,
    CX64_IR_SETNE,
    CX64_IR_SETLT,
    CX64_IR_SETLE,
    CX64_IR_SETGT,
    CX64_IR_SETGE,

    /* Control flow */
    CX64_IR_JMP,
    CX64_IR_JE,
    CX64_IR_JNE,
    CX64_IR_JL,
    CX64_IR_JLE,
    CX64_IR_JG,
    CX64_IR_JGE,
    CX64_IR_CALL,
    CX64_IR_RET,
    CX64_IR_RET_VAL,

    /* Loop-specific (enable unrolling) */
    CX64_IR_LOOP_BEGIN,  /* marks start of hot loop */
    CX64_IR_LOOP_END,    /* marks end, triggers unroll */
    CX64_IR_LOOP_CTR,    /* loop counter variable */

    /* System */
    CX64_IR_SYSCALL,
    CX64_IR_NOP,
    CX64_IR_LABEL,
    CX64_IR_FUNC_BEGIN,
    CX64_IR_FUNC_END,

    CX64_IR_COUNT
} Cx64IROp;

typedef struct {
    Cx64IROp  op;
    int       dst;        /* register or var slot */
    int       src_a;
    int       src_b;
    int64_t   imm;        /* immediate value */
    double    fimm;       /* float immediate */
    Cx64Type  type;       /* operand type */
    int       label_id;   /* for jumps */
    int       is_loop;    /* 1 = inside a loop → unroll eligible */
} Cx64IR;

/* ---- CX64 Compile Unit ---- */
#define CX64_IR_MAX   65536
#define CX64_LABEL_MAX 4096
#define CX64_VAR_MAX   2048

typedef struct {
    Cx64IR    ir[CX64_IR_MAX];
    int       ir_count;

    /* Label table: label_id → IR index */
    int       labels[CX64_LABEL_MAX];
    int       label_count;

    /* Variable table */
    struct {
        char      name[64];
        Cx64Type  type;
        int       stack_offset;  /* from RBP */
        int       in_reg;        /* -1 = spilled, else register id */
        int       is_live;
    } vars[CX64_VAR_MAX];
    int       var_count;
    int       stack_frame_size;

    /* Loop tracking */
    int       loop_depth;
    int       loop_begin_ir[64];    /* stack of loop begin IR indices */

    /* Code output */
    Cx64CodeBuf *code;
    int          core_id;
} Cx64Unit;

/* ---- Optimizer flags ---- */
typedef struct {
    int do_constant_fold;    /* fold compile-time constants */
    int do_dce;              /* dead code elimination */
    int do_loop_unroll;      /* unroll hot loops (12x) */
    int do_reg_alloc;        /* linear scan register allocation */
    int do_inlining;         /* inline small functions */
    int do_strength_reduce;  /* x*2 → x<<1 etc */
    int do_peephole;         /* short pattern replacement */
    int unroll_factor;       /* default 12 */
} Cx64OptFlags;

static inline Cx64OptFlags cx64_opt_default(void) {
    Cx64OptFlags f = {0};
    f.do_constant_fold  = 1;
    f.do_dce            = 1;
    f.do_loop_unroll    = 1;
    f.do_reg_alloc      = 1;
    f.do_inlining       = 1;
    f.do_strength_reduce= 1;
    f.do_peephole       = 1;
    f.unroll_factor     = CX64_LOOP_UNROLL;
    return f;
}

/* ---- JIT Task (per core) ---- */
typedef struct {
    Cx64Unit    *unit;
    Cx64CodeBuf *code;
    void        (*entry)(void);   /* compiled native entry point */
    int          core_id;
    volatile int done;
    int          error;
    char         errmsg[256];
    /* Timing stats */
    int64_t      compile_ns;
    int64_t      exec_ns;
    int64_t      exec_count;      /* how many times this chunk has run */
} Cx64JitTask;

/* ---- CX64 Compiler instance ---- */
typedef struct {
    Cx64JitTask  tasks[CX64_MAX_CORES];
    Cx64CodeBuf  code_bufs[CX64_MAX_CORES];
    Cx64OptFlags opt;
    int          active_cores;
    /* Stats */
    int64_t      total_instrs_emitted;
    int64_t      total_loops_unrolled;
    int64_t      total_native_runs;
} Cx64Compiler;

/* ---- Forward declarations ---- */
Cx64Compiler *cx64_compiler_create(int cores);
void          cx64_compiler_destroy(Cx64Compiler *c);

/* Main compile + run pipeline */
int           cx64_compile_and_run(Cx64Compiler *c, const void *mcbl_ast);

/* Dump stats */
void          cx64_dump_stats(const Cx64Compiler *c);

/* ASM runtime functions (defined in cx64_asm.asm) */
extern void   cx64_asm_call_native(void *fn_ptr);
extern void   cx64_asm_cache_flush(uint8_t *start, size_t len);
extern int64_t cx64_asm_rdtsc(void);
extern void   cx64_asm_sfence(void);
extern void   cx64_asm_mfence(void);
extern void   cx64_asm_lfence(void);
extern int64_t cx64_asm_get_time_ns(void);

#ifdef __cplusplus
}
#endif

#endif /* CX64_DEFS_H */
