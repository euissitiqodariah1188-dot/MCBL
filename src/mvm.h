#ifndef MCBL_MVM_H
#define MCBL_MVM_H

/*
 * MVM — McBL Virtual Machine
 * =============================================================
 * Tugas: jalanin McBL# di x86 native, portable ke Windows.
 * Mix: 40% ASM inline, 50% C, 10% C++
 *
 * Fitur:
 *   - 4 jalur eksekusi super cepat (pipeline)
 *   - Multi-core: distribusi ke 4 core CPU via pthread
 *   - Optimizer otomatis
 *   - Direct x86 binary codegen
 *   - Direct memory control
 *   - Static typing (ukuran variable diketahui saat compile)
 *   - Dekat dengan hardware
 */

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "bytecode.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------
   MVM Core Configuration
   ------------------------------------------------------------------- */
#define MVM_MAX_CORES      4       /* default 4 core CPU lanes          */
#define MVM_PIPE_BUFSZ     4096    /* inter-core pipe buffer size        */
#define MVM_INSTR_CACHE    256     /* hot instruction cache size         */
#define MVM_OPT_PASSES     3       /* default optimizer passes           */
#define MVM_STACK_DEPTH    8192    /* native stack depth per core        */

/* -------------------------------------------------------------------
   MVM Value — statically typed, size known at compile time
   ------------------------------------------------------------------- */
typedef enum {
    MVM_T_INT8,    /* 1 byte */
    MVM_T_INT16,   /* 2 bytes */
    MVM_T_INT32,   /* 4 bytes */
    MVM_T_INT64,   /* 8 bytes */
    MVM_T_FLOAT32, /* 4 bytes */
    MVM_T_FLOAT64, /* 8 bytes */
    MVM_T_BOOL,    /* 1 byte */
    MVM_T_PTR,     /* pointer-sized */
    MVM_T_STRING,
    MVM_T_OBJECT,
    MVM_T_ARRAY,
    MVM_T_NULL
} MvmType;

/* Static type descriptor — size known at compile time */
typedef struct {
    MvmType     base;
    size_t      size;       /* sizeof in bytes */
    const char *name;       /* type name string */
} MvmTypeDesc;

typedef struct {
    MvmType  type;
    union {
        int8_t   i8;
        int16_t  i16;
        int32_t  i32;
        int64_t  i64;
        float    f32;
        double   f64;
        uint8_t  b;
        void    *ptr;
        char    *sval;
    };
} MvmVal;

/* -------------------------------------------------------------------
   MVM Pipeline — 4 super-fast execution lanes
   ------------------------------------------------------------------- */
typedef struct {
    int         id;             /* lane 0–3 */
    pthread_t   thread;
    BcChunk    *chunk;          /* bytecode to execute */
    MvmVal      stack[MVM_STACK_DEPTH];
    int         sp;
    volatile int ready;         /* 1 = waiting for work */
    volatile int done;          /* 1 = finished */
    int         result_code;
    char        err[256];

    /* hot instruction cache */
    size_t      icache_ip[MVM_INSTR_CACHE];
    void       *icache_fn[MVM_INSTR_CACHE];
    int         icache_count;
} MvmLane;

/* -------------------------------------------------------------------
   MVM Pipe — inter-core communication channel
   ------------------------------------------------------------------- */
typedef struct {
    MvmVal          buf[MVM_PIPE_BUFSZ];
    volatile size_t head;
    volatile size_t tail;
    pthread_mutex_t lock;
    pthread_cond_t  cond_not_empty;
    pthread_cond_t  cond_not_full;
} MvmPipe;

/* -------------------------------------------------------------------
   MVM Optimizer — static analysis + constant folding + DCE
   ------------------------------------------------------------------- */
typedef struct {
    int pass_count;
    int folded_constants;   /* stats */
    int eliminated_dead;
    int inlined_calls;
    int vectorized_loops;
} MvmOptStats;

/* -------------------------------------------------------------------
   MVM Instance
   ------------------------------------------------------------------- */
typedef struct MvmInstance {
    MvmLane      lanes[MVM_MAX_CORES];
    int          active_cores;     /* how many cores to use */
    MvmPipe     *pipes[MVM_MAX_CORES];
    MvmOptStats  opt_stats;

    /* Native x86 JIT output buffer */
    uint8_t     *jit_buf;
    size_t       jit_buf_size;
    size_t       jit_buf_used;

    /* Direct memory control block */
    void        *direct_mem;
    size_t       direct_mem_size;

    int          error;
    char         errmsg[512];
} MvmInstance;

/* -------------------------------------------------------------------
   MVM API
   ------------------------------------------------------------------- */

/* Create MVM with N cores (1–4) */
MvmInstance *mvm_create(int cores);
void         mvm_destroy(MvmInstance *mvm);

/* Execute bytecode chunk on N cores, dispatching work across lanes */
int          mvm_exec(MvmInstance *mvm, BcChunk *chunk);

/* Execute a single lane (called by worker thread) */
int          mvm_lane_exec(MvmLane *lane, BcChunk *chunk);

/* Optimizer: analyze chunk and optimize in-place */
int          mvm_optimize(MvmInstance *mvm, BcChunk *chunk, int passes);

/* JIT: compile hot bytecode to native x86_64 and run */
typedef void (*MvmNativeFn)(void);
MvmNativeFn  mvm_jit_compile(MvmInstance *mvm, const BcChunk *chunk,
                              size_t from_ip, size_t to_ip);
int          mvm_jit_exec(MvmInstance *mvm, MvmNativeFn fn);

/* Pipe operations */
MvmPipe     *mvm_pipe_create(void);
void         mvm_pipe_destroy(MvmPipe *p);
int          mvm_pipe_send(MvmPipe *p, MvmVal val);
int          mvm_pipe_recv(MvmPipe *p, MvmVal *out);

/* Direct memory control */
void        *mvm_mem_alloc(MvmInstance *mvm, size_t bytes);
void         mvm_mem_free(MvmInstance *mvm, void *ptr);
void         mvm_mem_write(MvmInstance *mvm, size_t offset,
                           const void *data, size_t size);
void         mvm_mem_read(MvmInstance *mvm, size_t offset,
                          void *out, size_t size);

/* Core affinity — pin lane to physical core */
int          mvm_pin_core(MvmLane *lane, int core_id);

/* Stats / debug */
void         mvm_dump_stats(const MvmInstance *mvm);

/* -------------------------------------------------------------------
   Static type system helpers — determine size at compile time
   ------------------------------------------------------------------- */
size_t       mvm_type_size(MvmType t);
const char  *mvm_type_name(MvmType t);
MvmType      mvm_infer_type(const char *type_str);

/* Inline x86 ASM helpers (40% of MVM guts live here) */
/* These emit raw x86_64 bytes into the JIT buffer     */
void mvm_asm_emit_mov_reg_imm64(uint8_t *buf, size_t *off,
                                 int reg, int64_t imm);
void mvm_asm_emit_add_rax_rbx  (uint8_t *buf, size_t *off);
void mvm_asm_emit_sub_rax_rbx  (uint8_t *buf, size_t *off);
void mvm_asm_emit_mul_rax_rbx  (uint8_t *buf, size_t *off);
void mvm_asm_emit_div_rax_rcx  (uint8_t *buf, size_t *off);
void mvm_asm_emit_cmp_rax_rbx  (uint8_t *buf, size_t *off);
void mvm_asm_emit_jmp_rel32    (uint8_t *buf, size_t *off, int32_t rel);
void mvm_asm_emit_jz_rel32     (uint8_t *buf, size_t *off, int32_t rel);
void mvm_asm_emit_jnz_rel32    (uint8_t *buf, size_t *off, int32_t rel);
void mvm_asm_emit_call_ptr     (uint8_t *buf, size_t *off, void *target);
void mvm_asm_emit_ret          (uint8_t *buf, size_t *off);
void mvm_asm_emit_push_rax     (uint8_t *buf, size_t *off);
void mvm_asm_emit_pop_rbx      (uint8_t *buf, size_t *off);
void mvm_asm_emit_simd_add_f64 (uint8_t *buf, size_t *off); /* SIMD float */
void mvm_asm_emit_simd_mul_f64 (uint8_t *buf, size_t *off);

#ifdef __cplusplus
}
#endif

#endif /* MCBL_MVM_H */
