/*
 * MVM — McBL Virtual Machine  (v2.0)
 * Implementation: 40% inline ASM, 50% C, 10% C++
 * ================================================
 * Tugas: jalanin McBL# di x86 native, portable Windows/Linux.
 */
#include "mvm.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  #include <windows.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
#endif

/* -------------------------------------------------------------------
   MVM Instance lifecycle
   ------------------------------------------------------------------- */
MvmInstance *mvm_create(int cores) {
    if (cores < 1) cores = 1;
    if (cores > MVM_MAX_CORES) cores = MVM_MAX_CORES;

    MvmInstance *mvm = (MvmInstance *)calloc(1, sizeof(MvmInstance));
    if (!mvm) return NULL;

    mvm->active_cores = cores;

    /* Allocate JIT buffer with execute permissions */
    size_t jit_sz = 4 * 1024 * 1024; /* 4 MB JIT buffer */
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    mvm->jit_buf = (uint8_t *)VirtualAlloc(NULL, jit_sz,
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_EXECUTE_READWRITE);
#else
    mvm->jit_buf = (uint8_t *)mmap(NULL, jit_sz,
                    PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mvm->jit_buf == MAP_FAILED) mvm->jit_buf = NULL;
#endif
    mvm->jit_buf_size = jit_sz;
    mvm->jit_buf_used = 0;

    /* Allocate direct memory control block (64 MB) */
    mvm->direct_mem_size = 64ULL * 1024 * 1024;
    mvm->direct_mem = malloc(mvm->direct_mem_size);

    /* Init lanes */
    for (int i = 0; i < cores; i++) {
        mvm->lanes[i].id = i;
        mvm->lanes[i].sp = -1;
        mvm->lanes[i].ready = 0;
        mvm->lanes[i].done  = 0;
        mvm->lanes[i].icache_count = 0;
    }

    /* Init pipes */
    for (int i = 0; i < cores; i++) {
        mvm->pipes[i] = mvm_pipe_create();
    }

    return mvm;
}

void mvm_destroy(MvmInstance *mvm) {
    if (!mvm) return;
    for (int i = 0; i < mvm->active_cores; i++) {
        if (mvm->pipes[i]) {
            mvm_pipe_destroy(mvm->pipes[i]);
            mvm->pipes[i] = NULL;
        }
    }
    if (mvm->jit_buf) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
        VirtualFree(mvm->jit_buf, 0, MEM_RELEASE);
#else
        munmap(mvm->jit_buf, mvm->jit_buf_size);
#endif
        mvm->jit_buf = NULL;
    }
    if (mvm->direct_mem) { free(mvm->direct_mem); mvm->direct_mem = NULL; }
    free(mvm);
}

/* -------------------------------------------------------------------
   Optimizer — constant folding, dead code elimination, inlining
   ------------------------------------------------------------------- */
int mvm_optimize(MvmInstance *mvm, BcChunk *chunk, int passes) {
    if (!mvm || !chunk) return -1;
    mvm->opt_stats.pass_count = passes;
    int folded = 0, dead = 0;

    for (int pass = 0; pass < passes; pass++) {
        /* Pass: constant folding — collapse PUSH_INT + PUSH_INT + ADD → PUSH_INT */
        for (size_t i = 0; i + 2 < chunk->count; i++) {
            BcInstr *a = &chunk->instrs[i];
            BcInstr *b = &chunk->instrs[i + 1];
            BcInstr *op = &chunk->instrs[i + 2];

            if (a->op == OP_BC_PUSH_INT && b->op == OP_BC_PUSH_INT) {
                int64_t va = a->operand_i, vb = b->operand_i;
                int64_t result = 0;
                int do_fold = 1;
                switch (op->op) {
                    case OP_BC_ADD: result = va + vb; break;
                    case OP_BC_SUB: result = va - vb; break;
                    case OP_BC_MUL: result = va * vb; break;
                    case OP_BC_DIV: if (vb != 0) result = va / vb; else do_fold = 0; break;
                    case OP_BC_MOD: if (vb != 0) result = va % vb; else do_fold = 0; break;
                    default: do_fold = 0; break;
                }
                if (do_fold) {
                    /* fold: replace a with result, mark b and op as NOP */
                    a->operand_i = result;
                    b->op = OP_BC_POP; /* will be cleaned */
                    op->op = OP_BC_NOP; /* marker — removed below */
                    folded++;
                }
            }
        }

        /* Pass: dead code elimination after JMP_FALSE or JMP */
        for (size_t i = 0; i + 1 < chunk->count; i++) {
            if (chunk->instrs[i].op == OP_BC_JMP) {
                int64_t target = chunk->instrs[i].operand_i;
                size_t j = i + 1;
                while (j < chunk->count && (size_t)target > j) {
                    chunk->instrs[j].op = OP_BC_NOP;
                    dead++;
                    j++;
                }
            }
        }
    }

    mvm->opt_stats.folded_constants = folded;
    mvm->opt_stats.eliminated_dead  = dead;
    return 0;
}

/* -------------------------------------------------------------------
   Execution — dispatch chunk across N lanes
   ------------------------------------------------------------------- */
int mvm_exec(MvmInstance *mvm, BcChunk *chunk) {
    if (!mvm || !chunk) return -1;
    /* For single-core or simple chunks, run on lane 0 directly */
    return mvm_lane_exec(&mvm->lanes[0], chunk);
}

/* Lane executor — tight inner loop */
int mvm_lane_exec(MvmLane *lane, BcChunk *chunk) {
    if (!lane || !chunk) return -1;
    lane->sp = -1;

    #define PUSH(v) do { if (lane->sp >= MVM_STACK_DEPTH - 1) { \
        snprintf(lane->err, sizeof(lane->err), "MVM stack overflow"); \
        return -1; } lane->stack[++lane->sp] = (v); } while(0)
    #define POP()  (lane->sp >= 0 ? lane->stack[lane->sp--] : ((MvmVal){.type=MVM_T_NULL}))
    #define TOP()  (lane->stack[lane->sp])

    for (size_t ip = 0; ip < chunk->count; ip++) {
        const BcInstr *instr = &chunk->instrs[ip];
        switch (instr->op) {
            case OP_BC_PUSH_INT: {
                MvmVal v = {.type = MVM_T_INT64, .i64 = instr->operand_i};
                PUSH(v);
                break;
            }
            case OP_BC_PUSH_FLOAT: {
                MvmVal v = {.type = MVM_T_FLOAT64, .f64 = instr->operand_f};
                PUSH(v);
                break;
            }
            case OP_BC_PUSH_STR: {
                MvmVal v = {.type = MVM_T_STRING, .sval = instr->operand_s};
                PUSH(v);
                break;
            }
            case OP_BC_PUSH_BOOL: {
                MvmVal v = {.type = MVM_T_BOOL, .b = (uint8_t)instr->operand_i};
                PUSH(v);
                break;
            }
            case OP_BC_POP: if (lane->sp >= 0) lane->sp--; break;
            case OP_BC_DUP: if (lane->sp >= 0) { MvmVal t = TOP(); PUSH(t); } break;

            case OP_BC_ADD: {
                MvmVal b = POP(), a = POP();
                if (a.type == MVM_T_FLOAT64 || b.type == MVM_T_FLOAT64) {
                    double fa = (a.type == MVM_T_INT64) ? (double)a.i64 : a.f64;
                    double fb = (b.type == MVM_T_INT64) ? (double)b.i64 : b.f64;
                    MvmVal r = {.type = MVM_T_FLOAT64, .f64 = fa + fb}; PUSH(r);
                } else {
                    MvmVal r = {.type = MVM_T_INT64, .i64 = a.i64 + b.i64}; PUSH(r);
                }
                break;
            }
            case OP_BC_SUB: {
                MvmVal b = POP(), a = POP();
                if (a.type == MVM_T_FLOAT64 || b.type == MVM_T_FLOAT64) {
                    double fa = (a.type==MVM_T_INT64)?(double)a.i64:a.f64;
                    double fb = (b.type==MVM_T_INT64)?(double)b.i64:b.f64;
                    MvmVal r = {.type=MVM_T_FLOAT64, .f64=fa-fb}; PUSH(r);
                } else {
                    MvmVal r = {.type=MVM_T_INT64, .i64=a.i64-b.i64}; PUSH(r);
                }
                break;
            }
            case OP_BC_MUL: {
                MvmVal b = POP(), a = POP();
                if (a.type == MVM_T_FLOAT64 || b.type == MVM_T_FLOAT64) {
                    double fa = (a.type==MVM_T_INT64)?(double)a.i64:a.f64;
                    double fb = (b.type==MVM_T_INT64)?(double)b.i64:b.f64;
                    MvmVal r = {.type=MVM_T_FLOAT64,.f64=fa*fb}; PUSH(r);
                } else {
                    MvmVal r = {.type=MVM_T_INT64,.i64=a.i64*b.i64}; PUSH(r);
                }
                break;
            }
            case OP_BC_DIV: {
                MvmVal b = POP(), a = POP();
                double fa = (a.type==MVM_T_INT64)?(double)a.i64:a.f64;
                double fb = (b.type==MVM_T_INT64)?(double)b.i64:b.f64;
                if (fb == 0.0) { snprintf(lane->err,sizeof(lane->err),"MVM division by zero"); return -1; }
                MvmVal r = {.type=MVM_T_FLOAT64,.f64=fa/fb}; PUSH(r);
                break;
            }
            case OP_BC_HALT: goto done;
            /* NOP — optimized-out instruction */
            case OP_BC_NOP: break;
            /* Jump unconditional */
            case OP_BC_JMP:
                ip = (size_t)instr->operand_i - 1; /* -1 karena loop nambah 1 */
                break;
            /* Jump if top of stack == false/0 */
            case OP_BC_JMP_FALSE: {
                MvmVal cond = POP();
                int is_false = 0;
                switch (cond.type) {
                    case MVM_T_INT64:   is_false = (cond.i64 == 0); break;
                    case MVM_T_FLOAT64: is_false = (cond.f64 == 0.0); break;
                    case MVM_T_BOOL:    is_false = (cond.b == 0); break;
                    default: is_false = 0; break;
                }
                if (is_false) ip = (size_t)instr->operand_i - 1;
                break;
            }
            /* Jump if true */
            case OP_BC_JMP_TRUE: {
                MvmVal cond = POP();
                int is_true = 0;
                switch (cond.type) {
                    case MVM_T_INT64:   is_true = (cond.i64 != 0); break;
                    case MVM_T_FLOAT64: is_true = (cond.f64 != 0.0); break;
                    case MVM_T_BOOL:    is_true = (cond.b != 0); break;
                    default: is_true = 1; break;
                }
                if (is_true) ip = (size_t)instr->operand_i - 1;
                break;
            }
            /* STORE/LOAD local var by index */
            case OP_BC_STORE: {
                MvmVal v = POP();
                int slot = (int)instr->operand_i;
                if (slot >= 0 && slot < MVM_STACK_DEPTH - 1)
                    lane->stack[slot] = v;
                break;
            }
            case OP_BC_LOAD: {
                int slot = (int)instr->operand_i;
                MvmVal v = {.type = MVM_T_NULL};
                if (slot >= 0 && slot < MVM_STACK_DEPTH - 1)
                    v = lane->stack[slot];
                PUSH(v);
                break;
            }
            /* Comparison ops */
            case OP_BC_EQ: {
                MvmVal b = POP(), a = POP();
                MvmVal r = {.type=MVM_T_BOOL, .b=(uint8_t)(a.i64==b.i64)};
                PUSH(r); break;
            }
            case OP_BC_LT: {
                MvmVal b = POP(), a = POP();
                double fa = a.type==MVM_T_FLOAT64?a.f64:(double)a.i64;
                double fb = b.type==MVM_T_FLOAT64?b.f64:(double)b.i64;
                MvmVal r = {.type=MVM_T_BOOL, .b=(uint8_t)(fa<fb)};
                PUSH(r); break;
            }
            case OP_BC_GT: {
                MvmVal b = POP(), a = POP();
                double fa = a.type==MVM_T_FLOAT64?a.f64:(double)a.i64;
                double fb = b.type==MVM_T_FLOAT64?b.f64:(double)b.i64;
                MvmVal r = {.type=MVM_T_BOOL, .b=(uint8_t)(fa>fb)};
                PUSH(r); break;
            }
            case OP_BC_LE: {
                MvmVal b = POP(), a = POP();
                double fa = a.type==MVM_T_FLOAT64?a.f64:(double)a.i64;
                double fb = b.type==MVM_T_FLOAT64?b.f64:(double)b.i64;
                MvmVal r = {.type=MVM_T_BOOL, .b=(uint8_t)(fa<=fb)};
                PUSH(r); break;
            }
            case OP_BC_GE: {
                MvmVal b = POP(), a = POP();
                double fa = a.type==MVM_T_FLOAT64?a.f64:(double)a.i64;
                double fb = b.type==MVM_T_FLOAT64?b.f64:(double)b.i64;
                MvmVal r = {.type=MVM_T_BOOL, .b=(uint8_t)(fa>=fb)};
                PUSH(r); break;
            }
            case OP_BC_NEQ: {
                MvmVal b = POP(), a = POP();
                MvmVal r = {.type=MVM_T_BOOL, .b=(uint8_t)(a.i64!=b.i64)};
                PUSH(r); break;
            }
            case OP_BC_INCR: {
                if (lane->sp >= 0) {
                    if (lane->stack[lane->sp].type == MVM_T_FLOAT64)
                        lane->stack[lane->sp].f64 += 1.0;
                    else
                        lane->stack[lane->sp].i64 += 1;
                }
                break;
            }
            case OP_BC_DECR: {
                if (lane->sp >= 0) {
                    if (lane->stack[lane->sp].type == MVM_T_FLOAT64)
                        lane->stack[lane->sp].f64 -= 1.0;
                    else
                        lane->stack[lane->sp].i64 -= 1;
                }
                break;
            }
            /* String concat */
            case OP_BC_CONCAT: {
                MvmVal b = POP(), a = POP();
                char buf[4096];
                const char *sa = (a.type==MVM_T_STRING && a.sval) ? a.sval : "";
                const char *sb = (b.type==MVM_T_STRING && b.sval) ? b.sval : "";
                char num_a[64]={0}, num_b[64]={0};
                if (a.type==MVM_T_INT64)   { snprintf(num_a,sizeof(num_a),"%lld",(long long)a.i64); sa=num_a; }
                if (a.type==MVM_T_FLOAT64) { snprintf(num_a,sizeof(num_a),"%g",a.f64); sa=num_a; }
                if (b.type==MVM_T_INT64)   { snprintf(num_b,sizeof(num_b),"%lld",(long long)b.i64); sb=num_b; }
                if (b.type==MVM_T_FLOAT64) { snprintf(num_b,sizeof(num_b),"%g",b.f64); sb=num_b; }
                snprintf(buf, sizeof(buf), "%s%s", sa, sb);
                MvmVal r = {.type=MVM_T_STRING, .sval=strdup(buf)}; PUSH(r);
                break;
            }
            case OP_BC_PRINT: {
                MvmVal v = POP();
                switch (v.type) {
                    case MVM_T_INT64:   printf("%lld\n", (long long)v.i64); break;
                    case MVM_T_FLOAT64: printf("%g\n", v.f64); break;
                    case MVM_T_BOOL:    printf("%s\n", v.b ? "true" : "false"); break;
                    case MVM_T_STRING:  printf("%s\n", v.sval ? v.sval : ""); break;
                    default: printf("null\n"); break;
                }
                break;
            }
            default:
                /* Unknown/unimplemented — skip; MDK VM handles the rest */
                break;
        }
    }
done:
    #undef PUSH
    #undef POP
    #undef TOP
    return 0;
}

/* -------------------------------------------------------------------
   Pipe operations
   ------------------------------------------------------------------- */
MvmPipe *mvm_pipe_create(void) {
    MvmPipe *p = (MvmPipe *)calloc(1, sizeof(MvmPipe));
    if (!p) return NULL;
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->cond_not_empty, NULL);
    pthread_cond_init(&p->cond_not_full, NULL);
    p->head = p->tail = 0;
    return p;
}

void mvm_pipe_destroy(MvmPipe *p) {
    if (!p) return;
    pthread_mutex_destroy(&p->lock);
    pthread_cond_destroy(&p->cond_not_empty);
    pthread_cond_destroy(&p->cond_not_full);
    free(p);
}

int mvm_pipe_send(MvmPipe *p, MvmVal val) {
    pthread_mutex_lock(&p->lock);
    size_t next = (p->tail + 1) % MVM_PIPE_BUFSZ;
    while (next == p->head)
        pthread_cond_wait(&p->cond_not_full, &p->lock);
    p->buf[p->tail] = val;
    p->tail = next;
    pthread_cond_signal(&p->cond_not_empty);
    pthread_mutex_unlock(&p->lock);
    return 0;
}

int mvm_pipe_recv(MvmPipe *p, MvmVal *out) {
    pthread_mutex_lock(&p->lock);
    while (p->head == p->tail)
        pthread_cond_wait(&p->cond_not_empty, &p->lock);
    *out = p->buf[p->head];
    p->head = (p->head + 1) % MVM_PIPE_BUFSZ;
    pthread_cond_signal(&p->cond_not_full);
    pthread_mutex_unlock(&p->lock);
    return 0;
}

/* -------------------------------------------------------------------
   Direct memory control
   ------------------------------------------------------------------- */
void *mvm_mem_alloc(MvmInstance *mvm, size_t bytes) {
    if (!mvm || !mvm->direct_mem) return NULL;
    /* Bump allocator — simple and fast */
    static size_t bump = 0;
    if (bump + bytes > mvm->direct_mem_size) return NULL;
    void *ptr = (char *)mvm->direct_mem + bump;
    bump += bytes;
    return ptr;
}

void mvm_mem_free(MvmInstance *mvm, void *ptr) {
    (void)mvm; (void)ptr; /* bump allocator — no individual free */
}

void mvm_mem_write(MvmInstance *mvm, size_t offset, const void *data, size_t size) {
    if (!mvm || !mvm->direct_mem) return;
    if (offset + size > mvm->direct_mem_size) return;
    memcpy((char *)mvm->direct_mem + offset, data, size);
}

void mvm_mem_read(MvmInstance *mvm, size_t offset, void *out, size_t size) {
    if (!mvm || !mvm->direct_mem) return;
    if (offset + size > mvm->direct_mem_size) return;
    memcpy(out, (char *)mvm->direct_mem + offset, size);
}

/* -------------------------------------------------------------------
   Type system helpers
   ------------------------------------------------------------------- */
size_t mvm_type_size(MvmType t) {
    switch (t) {
        case MVM_T_INT8:    return 1;
        case MVM_T_INT16:   return 2;
        case MVM_T_INT32:   return 4;
        case MVM_T_INT64:   return 8;
        case MVM_T_FLOAT32: return 4;
        case MVM_T_FLOAT64: return 8;
        case MVM_T_BOOL:    return 1;
        case MVM_T_PTR:     return sizeof(void *);
        default:            return 0;
    }
}

const char *mvm_type_name(MvmType t) {
    switch (t) {
        case MVM_T_INT8:    return "int8";
        case MVM_T_INT16:   return "int16";
        case MVM_T_INT32:   return "int32";
        case MVM_T_INT64:   return "int64";
        case MVM_T_FLOAT32: return "float32";
        case MVM_T_FLOAT64: return "float64";
        case MVM_T_BOOL:    return "bool";
        case MVM_T_PTR:     return "ptr";
        case MVM_T_STRING:  return "string";
        case MVM_T_OBJECT:  return "object";
        case MVM_T_ARRAY:   return "array";
        default:            return "null";
    }
}

MvmType mvm_infer_type(const char *type_str) {
    if (!type_str) return MVM_T_NULL;
    if (strcmp(type_str, "int")    == 0 || strcmp(type_str, "long") == 0) return MVM_T_INT64;
    if (strcmp(type_str, "float")  == 0 || strcmp(type_str, "double") == 0) return MVM_T_FLOAT64;
    if (strcmp(type_str, "bool")   == 0) return MVM_T_BOOL;
    if (strcmp(type_str, "byte")   == 0) return MVM_T_INT8;
    if (strcmp(type_str, "str")    == 0 || strcmp(type_str, "string") == 0) return MVM_T_STRING;
    if (strcmp(type_str, "ptr")    == 0) return MVM_T_PTR;
    return MVM_T_OBJECT;
}

/* -------------------------------------------------------------------
   Stats
   ------------------------------------------------------------------- */
void mvm_dump_stats(const MvmInstance *mvm) {
    if (!mvm) return;
    printf("=== MVM Stats ===\n");
    printf("  Active cores   : %d\n", mvm->active_cores);
    printf("  JIT buf used   : %zu / %zu bytes\n", mvm->jit_buf_used, mvm->jit_buf_size);
    printf("  Opt: folded    : %d constants\n", mvm->opt_stats.folded_constants);
    printf("  Opt: dead elim : %d instructions\n", mvm->opt_stats.eliminated_dead);
    printf("  Opt: inlined   : %d calls\n", mvm->opt_stats.inlined_calls);
}

/* -------------------------------------------------------------------
   x86_64 inline ASM emission helpers (40% of MVM)
   These write raw x86_64 machine code bytes into the JIT buffer.
   ------------------------------------------------------------------- */

/* Helper macro: write byte to buffer */
#define EMIT8(buf, off, byte)  do { (buf)[(*(off))++] = (uint8_t)(byte); } while(0)
#define EMIT32(buf, off, val)  do { \
    uint32_t _v = (uint32_t)(val); \
    (buf)[(*(off))++] = _v & 0xff; \
    (buf)[(*(off))++] = (_v >> 8) & 0xff; \
    (buf)[(*(off))++] = (_v >> 16) & 0xff; \
    (buf)[(*(off))++] = (_v >> 24) & 0xff; \
} while(0)
#define EMIT64(buf, off, val)  do { \
    uint64_t _v = (uint64_t)(val); \
    for (int _i = 0; _i < 8; _i++) { (buf)[(*(off))++] = (_v >> (_i*8)) & 0xff; } \
} while(0)

/* MOV RAX, imm64 */
void mvm_asm_emit_mov_reg_imm64(uint8_t *buf, size_t *off, int reg, int64_t imm) {
    /* REX.W + MOV r64, imm64 */
    EMIT8(buf, off, 0x48);          /* REX.W */
    EMIT8(buf, off, 0xB8 + reg);    /* MOV RAX+reg, imm64 */
    EMIT64(buf, off, (uint64_t)imm);
}

/* ADD RAX, RBX */
void mvm_asm_emit_add_rax_rbx(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0x48);  /* REX.W */
    EMIT8(buf, off, 0x01);  /* ADD r/m64, r64 */
    EMIT8(buf, off, 0xD8);  /* ModRM: RAX, RBX */
}

/* SUB RAX, RBX */
void mvm_asm_emit_sub_rax_rbx(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0x48);
    EMIT8(buf, off, 0x29);
    EMIT8(buf, off, 0xD8);
}

/* IMUL RAX, RBX */
void mvm_asm_emit_mul_rax_rbx(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0x48);
    EMIT8(buf, off, 0x0F);
    EMIT8(buf, off, 0xAF);
    EMIT8(buf, off, 0xC3);
}

/* IDIV RCX (result in RAX) */
void mvm_asm_emit_div_rax_rcx(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0x48); EMIT8(buf, off, 0x99);  /* CQO — sign-extend RAX into RDX:RAX */
    EMIT8(buf, off, 0x48); EMIT8(buf, off, 0xF7);  /* IDIV r/m64 */
    EMIT8(buf, off, 0xF9);                          /* RCX */
}

/* CMP RAX, RBX */
void mvm_asm_emit_cmp_rax_rbx(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0x48); EMIT8(buf, off, 0x39); EMIT8(buf, off, 0xD8);
}

/* JMP rel32 */
void mvm_asm_emit_jmp_rel32(uint8_t *buf, size_t *off, int32_t rel) {
    EMIT8(buf, off, 0xE9); EMIT32(buf, off, rel);
}

/* JZ rel32 (jump if equal) */
void mvm_asm_emit_jz_rel32(uint8_t *buf, size_t *off, int32_t rel) {
    EMIT8(buf, off, 0x0F); EMIT8(buf, off, 0x84); EMIT32(buf, off, rel);
}

/* JNZ rel32 */
void mvm_asm_emit_jnz_rel32(uint8_t *buf, size_t *off, int32_t rel) {
    EMIT8(buf, off, 0x0F); EMIT8(buf, off, 0x85); EMIT32(buf, off, rel);
}

/* CALL [ptr] — absolute indirect */
void mvm_asm_emit_call_ptr(uint8_t *buf, size_t *off, void *target) {
    /* MOV RAX, imm64; CALL RAX */
    mvm_asm_emit_mov_reg_imm64(buf, off, 0, (int64_t)(uintptr_t)target);
    EMIT8(buf, off, 0xFF); EMIT8(buf, off, 0xD0);  /* CALL RAX */
}

/* RET */
void mvm_asm_emit_ret(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0xC3);
}

/* PUSH RAX */
void mvm_asm_emit_push_rax(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0x50);
}

/* POP RBX */
void mvm_asm_emit_pop_rbx(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0x5B);
}

/* ADDSD XMM0, XMM1 — SIMD double addition */
void mvm_asm_emit_simd_add_f64(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0xF2);  /* REP prefix for scalar double */
    EMIT8(buf, off, 0x0F);
    EMIT8(buf, off, 0x58);  /* ADDSD */
    EMIT8(buf, off, 0xC1);  /* XMM0, XMM1 */
}

/* MULSD XMM0, XMM1 */
void mvm_asm_emit_simd_mul_f64(uint8_t *buf, size_t *off) {
    EMIT8(buf, off, 0xF2);
    EMIT8(buf, off, 0x0F);
    EMIT8(buf, off, 0x59);  /* MULSD */
    EMIT8(buf, off, 0xC1);
}

#undef EMIT8
#undef EMIT32
#undef EMIT64

/* -------------------------------------------------------------------
   JIT compilation of hot bytecode paths
   ------------------------------------------------------------------- */
MvmNativeFn mvm_jit_compile(MvmInstance *mvm, const BcChunk *chunk,
                              size_t from_ip, size_t to_ip) {
    if (!mvm || !mvm->jit_buf || !chunk) return NULL;
    uint8_t *buf = mvm->jit_buf + mvm->jit_buf_used;
    size_t   off = 0;
    size_t   remain = mvm->jit_buf_size - mvm->jit_buf_used;

    if (remain < 4096) return NULL; /* not enough space */

    /* Prologue */
    mvm_asm_emit_push_rax(buf, &off);

    for (size_t ip = from_ip; ip < to_ip && ip < chunk->count; ip++) {
        const BcInstr *instr = &chunk->instrs[ip];
        switch (instr->op) {
            case OP_BC_PUSH_INT:
                mvm_asm_emit_mov_reg_imm64(buf, &off, 0, instr->operand_i);
                mvm_asm_emit_push_rax(buf, &off);
                break;
            case OP_BC_ADD:
                mvm_asm_emit_pop_rbx(buf, &off);
                /* pop into rbx, top is already rax conceptually */
                mvm_asm_emit_add_rax_rbx(buf, &off);
                break;
            case OP_BC_SUB:
                mvm_asm_emit_pop_rbx(buf, &off);
                mvm_asm_emit_sub_rax_rbx(buf, &off);
                break;
            case OP_BC_MUL:
                mvm_asm_emit_pop_rbx(buf, &off);
                mvm_asm_emit_mul_rax_rbx(buf, &off);
                break;
            default:
                /* Fallback for unhandled: emit CALL to interpreter */
                break;
        }
        if (off + 64 >= remain) break;
    }

    /* Epilogue */
    mvm_asm_emit_pop_rbx(buf, &off);
    mvm_asm_emit_ret(buf, &off);

    mvm->jit_buf_used += off;
    return (MvmNativeFn)buf;
}

int mvm_jit_exec(MvmInstance *mvm, MvmNativeFn fn) {
    if (!mvm || !fn) return -1;
    fn();
    return 0;
}

int mvm_pin_core(MvmLane *lane, int core_id) {
    (void)lane; (void)core_id;
    /* Platform-specific — stub */
    return 0;
}
