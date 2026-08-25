#include "jit.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  #include <windows.h>
  #include <memoryapi.h>
#else
  #include <unistd.h>
  #include <sys/mman.h>
#endif

/* -----------------------------------------------------------------------
   McBL# JIT Compiler implementation
   Emits x86_64 machine code for a subset of bytecode ops.
   Uses mmap (POSIX) or VirtualAlloc (Windows) for executable pages.
   ----------------------------------------------------------------------- */

#define JIT_REGION_INIT_CAP 4

/* x86_64 register encoding for ModR/M */
#define RAX 0
#define RCX 1
#define RDX 2
#define RSP 4
#define RBP 5
#define RSI 6
#define RDI 7

/* REX prefix for 64-bit operand size */
#define REX_W 0x48

JitCompiler *jit_create(SymTable *symbols) {
    JitCompiler *jc = (JitCompiler *)mcbl_calloc(1, sizeof(JitCompiler));
    jc->regions = (JitRegion *)mcbl_malloc(sizeof(JitRegion) * JIT_REGION_INIT_CAP);
    jc->region_cap   = JIT_REGION_INIT_CAP;
    jc->region_count = 0;
    jc->symbols      = symbols;
    return jc;
}

void jit_destroy(JitCompiler *jc) {
    if (!jc) return;
    for (int i = 0; i < jc->region_count; i++) {
        JitRegion *r = &jc->regions[i];
        if (r->mem) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
            VirtualFree(r->mem, 0, MEM_RELEASE);
#else
            if (r->mem != MAP_FAILED)
                munmap(r->mem, r->mem_size);
#endif
            r->mem = NULL;
        }
    }
    mcbl_free((void **)&jc->regions);
    mcbl_free((void **)&jc);
}

/* Simple bytecode-to-machine-code emitter.
   For demonstration: emits a minimal prologue, processes a subset of ops,
   and an epilogue.  The full VM handles ops that the JIT does not. */
static void emit_byte(JitRegion *r, uint8_t b) {
    if (r->used >= r->mem_size) return;
    r->mem[r->used++] = b;
}

static void emit_bytes(JitRegion *r, const uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n && r->used < r->mem_size; i++)
        r->mem[r->used++] = buf[i];
}

static void emit_prologue(JitRegion *r) {
    /* push rbp; mov rbp, rsp; sub rsp, 64  (stack frame + scratch space) */
    uint8_t prologue[] = {
        0x55,                                   /* push rbp              */
        REX_W, 0x89, 0xE5,                       /* mov rbp, rsp          */
        REX_W, 0x81, 0xEC, 0x40, 0x00, 0x00, 0x00 /* sub rsp, 64           */
    };
    emit_bytes(r, prologue, sizeof(prologue));
}

static void emit_epilogue(JitRegion *r) {
    /* leave; ret */
    uint8_t epilogue[] = { 0xC9, 0xC3 };
    emit_bytes(r, epilogue, sizeof(epilogue));
}

static void emit_mov_imm64(JitRegion *r, int reg, int64_t val) {
    /* REX.W + mov r64, imm64 */
    emit_byte(r, REX_W | ((reg >> 3) & 1));
    emit_byte(r, 0xB8 + (reg & 7));
    for (int i = 0; i < 8; i++)
        emit_byte(r, (uint8_t)((val >> (i * 8)) & 0xFF));
}

static void emit_add_rax_rcx(JitRegion *r) {
    /* add rax, rcx */
    uint8_t bytes[] = { REX_W, 0x01, 0xC8 };
    emit_bytes(r, bytes, 3);
}

static void emit_sub_rax_rcx(JitRegion *r) {
    /* sub rax, rcx */
    uint8_t bytes[] = { REX_W, 0x29, 0xC8 };
    emit_bytes(r, bytes, 3);
}

static void emit_imul_rax_rcx(JitRegion *r) {
    /* imul rax, rcx */
    uint8_t bytes[] = { REX_W, 0x0F, 0xAF, 0xC1 };
    emit_bytes(r, bytes, 4);
}

static void emit_xor_rax_rax(JitRegion *r) {
    /* xor rax, rax */
    uint8_t bytes[] = { REX_W, 0x31, 0xC0 };
    emit_bytes(r, bytes, 3);
}

static void emit_print_rax(JitRegion *r) {
    /* For JIT demonstration: call printf via a simpler approach.
       In production this would use a trampoline to the runtime.
       Here we emit a nop-sled to keep the code valid. */
    for (int i = 0; i < 5; i++) emit_byte(r, 0x90); /* 5 nops */
}

int jit_compile_chunk(JitCompiler *jc, const BcChunk *chunk) {
    if (!jc || !chunk) return -1;

    /* Allocate executable memory */
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    void *mem = VirtualAlloc(NULL, JIT_EXEC_MEM_SIZE,
                             MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) {
        fprintf(stderr, "McBL# JIT: VirtualAlloc failed\n");
        return -1;
    }
#else
    void *mem = mmap(NULL, JIT_EXEC_MEM_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "McBL# JIT: mmap failed\n");
        return -1;
    }
#endif

    if (jc->region_count >= jc->region_cap) {
        jc->region_cap *= 2;
        jc->regions = (JitRegion *)mcbl_realloc(jc->regions,
                                                sizeof(JitRegion) * jc->region_cap);
    }

    int id = jc->region_count++;
    JitRegion *r = &jc->regions[id];
    r->mem       = (uint8_t *)mem;
    r->mem_size  = JIT_EXEC_MEM_SIZE;
    r->used      = 0;
    r->entry     = NULL;
    r->compiled  = 0;

    emit_prologue(r);
    emit_xor_rax_rax(r);

    /* Stack pointer for simulated value stack: use [rbp-8], [rbp-16], etc. */
    int stack_depth = 0;

    for (size_t i = 0; i < chunk->count; i++) {
        const BcInstr *ins = &chunk->instrs[i];
        switch (ins->op) {
            case OP_BC_PUSH_INT:
                emit_mov_imm64(r, RAX, ins->operand_i);
                /* push to simulated stack: mov [rbp - offset], rax */
                stack_depth++;
                {
                    int off = stack_depth * 8;
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x89);
                    emit_byte(r, 0x85);
                    emit_byte(r, (uint8_t)(off & 0xFF));
                    emit_byte(r, (uint8_t)((off >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                }
                break;

            case OP_BC_ADD:
                /* pop rcx, pop rax, add, push rax */
                if (stack_depth >= 2) {
                    int off1 = stack_depth * 8;
                    int off2 = (stack_depth - 1) * 8;
                    /* mov rcx, [rbp-off1] */
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x8B);
                    emit_byte(r, 0x8D);
                    emit_byte(r, (uint8_t)(off1 & 0xFF));
                    emit_byte(r, (uint8_t)((off1 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                    /* mov rax, [rbp-off2] */
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x8B);
                    emit_byte(r, 0x85);
                    emit_byte(r, (uint8_t)(off2 & 0xFF));
                    emit_byte(r, (uint8_t)((off2 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                    emit_add_rax_rcx(r);
                    stack_depth--;
                    off2 = stack_depth * 8;
                    /* mov [rbp-off2], rax */
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x89);
                    emit_byte(r, 0x85);
                    emit_byte(r, (uint8_t)(off2 & 0xFF));
                    emit_byte(r, (uint8_t)((off2 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                }
                break;

            case OP_BC_SUB:
                if (stack_depth >= 2) {
                    int off1 = stack_depth * 8;
                    int off2 = (stack_depth - 1) * 8;
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x8B);
                    emit_byte(r, 0x8D);
                    emit_byte(r, (uint8_t)(off1 & 0xFF));
                    emit_byte(r, (uint8_t)((off1 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x8B);
                    emit_byte(r, 0x85);
                    emit_byte(r, (uint8_t)(off2 & 0xFF));
                    emit_byte(r, (uint8_t)((off2 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                    emit_sub_rax_rcx(r);
                    stack_depth--;
                    off2 = stack_depth * 8;
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x89);
                    emit_byte(r, 0x85);
                    emit_byte(r, (uint8_t)(off2 & 0xFF));
                    emit_byte(r, (uint8_t)((off2 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                }
                break;

            case OP_BC_MUL:
                if (stack_depth >= 2) {
                    int off1 = stack_depth * 8;
                    int off2 = (stack_depth - 1) * 8;
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x8B);
                    emit_byte(r, 0x8D);
                    emit_byte(r, (uint8_t)(off1 & 0xFF));
                    emit_byte(r, (uint8_t)((off1 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x8B);
                    emit_byte(r, 0x85);
                    emit_byte(r, (uint8_t)(off2 & 0xFF));
                    emit_byte(r, (uint8_t)((off2 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                    emit_imul_rax_rcx(r);
                    stack_depth--;
                    off2 = stack_depth * 8;
                    emit_byte(r, REX_W);
                    emit_byte(r, 0x89);
                    emit_byte(r, 0x85);
                    emit_byte(r, (uint8_t)(off2 & 0xFF));
                    emit_byte(r, (uint8_t)((off2 >> 8) & 0xFF));
                    emit_byte(r, 0x00);
                    emit_byte(r, 0x00);
                }
                break;

            case OP_BC_PRINT:
                emit_print_rax(r);
                break;

            case OP_BC_HALT:
                goto done;

            default:
                /* unhandled ops fall through to interpreter */
                break;
        }
    }

done:
    emit_epilogue(r);

    /* Make page executable and read-only */
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    DWORD old_protect;
    if (!VirtualProtect(r->mem, r->mem_size, PAGE_EXECUTE_READ, &old_protect)) {
        fprintf(stderr, "McBL# JIT: VirtualProtect failed\n");
        VirtualFree(r->mem, 0, MEM_RELEASE);
        r->mem = NULL;
        return -1;
    }
#else
    if (mprotect(r->mem, r->mem_size, PROT_READ | PROT_EXEC) != 0) {
        fprintf(stderr, "McBL# JIT: mprotect failed\n");
        munmap(r->mem, r->mem_size);
        r->mem = NULL;
        return -1;
    }
#endif

    r->entry    = (JitFn)r->mem;
    r->compiled = 1;
    return id;
}

void jit_exec(JitCompiler *jc, int region_id) {
    if (!jc || region_id < 0 || region_id >= jc->region_count) return;
    JitRegion *r = &jc->regions[region_id];
    if (r->compiled && r->entry) {
        r->entry();
    }
}
