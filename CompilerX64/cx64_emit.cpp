/*
 * cx64_emit.cpp — x86_64 Machine Code Emitter
 * =============================================
 * Emit raw x86_64 bytes ke JIT code buffer.
 * Semua function inline untuk kecepatan maksimal.
 * Ini adalah "backend" compiler — terima IR, output bytes.
 */
#include "cx64_defs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#if defined(_WIN32) || defined(__MINGW32__)
  #include <windows.h>
  #define MEM_RWX(sz)  VirtualAlloc(NULL,(sz),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE)
  #define MEM_FREE(p)  VirtualFree((p),0,MEM_RELEASE)
#elif defined(CX64_NO_ASM)
  /* No ASM mode: use malloc (not executable, for struct/emit only) */
  #include <cstdlib>
  #define MEM_RWX(sz)  malloc(sz)
  #define MEM_FREE(p)  free(p)
#else
  #include <sys/mman.h>
  #define MEM_RWX(sz)  mmap(NULL,(sz),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)
  #define MEM_FREE(p)  munmap((p),CX64_CODE_BUF_SZ)
#endif

/* ---------------------------------------------------------------
   Code buffer management
   --------------------------------------------------------------- */
Cx64CodeBuf *cx64_buf_create(int core_id) {
    auto *b = new Cx64CodeBuf{};
    b->buf     = (uint8_t *)MEM_RWX(CX64_CODE_BUF_SZ);
    b->size    = CX64_CODE_BUF_SZ;
    b->used    = 0;
    b->core_id = (uint32_t)core_id;
    if (!b->buf) { delete b; return nullptr; }
    return b;
}

void cx64_buf_destroy(Cx64CodeBuf *b) {
    if (!b) return;
    if (b->buf) MEM_FREE(b->buf);
    delete b;
}

void cx64_buf_reset(Cx64CodeBuf *b) {
    if (b) b->used = 0;
}

static inline void emit8(Cx64CodeBuf *b, uint8_t byte) {
    if (b->used < b->size) b->buf[b->used++] = byte;
}
static inline void emit16(Cx64CodeBuf *b, uint16_t v) {
    emit8(b, v & 0xff); emit8(b, (v>>8) & 0xff);
}
static inline void emit32(Cx64CodeBuf *b, uint32_t v) {
    emit8(b, v & 0xff); emit8(b, (v>>8)&0xff);
    emit8(b, (v>>16)&0xff); emit8(b, (v>>24)&0xff);
}
static inline void emit64(Cx64CodeBuf *b, uint64_t v) {
    for (int i = 0; i < 8; i++) emit8(b, (v >> (i*8)) & 0xff);
}

/* REX prefix helper */
static inline uint8_t rex(bool w, bool r, bool x, bool rb) {
    return 0x40 | (w?8:0) | (r?4:0) | (x?2:0) | (rb?1:0);
}

/* ModRM byte */
static inline uint8_t modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return (mod<<6) | ((reg&7)<<3) | (rm&7);
}

/* SIB byte */
static inline uint8_t sib(uint8_t scale, uint8_t index, uint8_t base) {
    return (scale<<6) | ((index&7)<<3) | (base&7);
}

/* ---------------------------------------------------------------
   Function prologue / epilogue
   --------------------------------------------------------------- */
void cx64_emit_prologue(Cx64CodeBuf *b, int frame_size) {
    /* PUSH RBP */
    emit8(b, 0x55);
    /* MOV RBP, RSP */
    emit8(b, 0x48); emit8(b, 0x89); emit8(b, 0xE5);
    /* PUSH callee-saved regs */
    emit8(b, 0x53);             /* PUSH RBX */
    emit8(b, 0x41); emit8(b, 0x54); /* PUSH R12 */
    emit8(b, 0x41); emit8(b, 0x55); /* PUSH R13 */
    emit8(b, 0x41); emit8(b, 0x56); /* PUSH R14 */
    emit8(b, 0x41); emit8(b, 0x57); /* PUSH R15 */
    /* SUB RSP, frame_size (align to 16) */
    int aligned = (frame_size + 15) & ~15;
    if (aligned > 0) {
        emit8(b, 0x48); emit8(b, 0x81); emit8(b, 0xEC);
        emit32(b, (uint32_t)aligned);
    }
}

void cx64_emit_epilogue(Cx64CodeBuf *b) {
    /* ADD RSP, frame_size (matching prologue) */
    /* (simplified: restore from RBP) */
    emit8(b, 0x41); emit8(b, 0x5F); /* POP R15 */
    emit8(b, 0x41); emit8(b, 0x5E); /* POP R14 */
    emit8(b, 0x41); emit8(b, 0x5D); /* POP R13 */
    emit8(b, 0x41); emit8(b, 0x5C); /* POP R12 */
    emit8(b, 0x5B);             /* POP RBX */
    /* MOV RSP, RBP */
    emit8(b, 0x48); emit8(b, 0x89); emit8(b, 0xEC);
    /* POP RBP */
    emit8(b, 0x5D);
    /* RET */
    emit8(b, 0xC3);
}

/* ---------------------------------------------------------------
   MOV reg, imm64
   --------------------------------------------------------------- */
void cx64_emit_mov_reg_imm64(Cx64CodeBuf *b, int reg, int64_t imm) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0xB8 | (reg & 7));
    emit64(b, (uint64_t)imm);
}

/* MOV reg, reg */
void cx64_emit_mov_reg_reg(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, rex(true, dst > 7, false, src > 7));
    emit8(b, 0x89);
    emit8(b, modrm(3, src & 7, dst & 7));
}

/* MOV [RBP - offset], reg (spill to stack) */
void cx64_emit_spill(Cx64CodeBuf *b, int reg, int rbp_offset) {
    emit8(b, rex(true, reg > 7, false, false));
    emit8(b, 0x89);
    emit8(b, modrm(2, reg & 7, 5)); /* RBP-relative */
    emit32(b, (uint32_t)(-rbp_offset));
}

/* MOV reg, [RBP - offset] (load from stack) */
void cx64_emit_load(Cx64CodeBuf *b, int reg, int rbp_offset) {
    emit8(b, rex(true, reg > 7, false, false));
    emit8(b, 0x8B);
    emit8(b, modrm(2, reg & 7, 5));
    emit32(b, (uint32_t)(-rbp_offset));
}

/* ---------------------------------------------------------------
   Arithmetic — integer
   --------------------------------------------------------------- */
void cx64_emit_add_reg_reg(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, rex(true, src > 7, false, dst > 7));
    emit8(b, 0x01);
    emit8(b, modrm(3, src & 7, dst & 7));
}

void cx64_emit_add_reg_imm32(Cx64CodeBuf *b, int reg, int32_t imm) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0x81);
    emit8(b, modrm(3, 0, reg & 7));
    emit32(b, (uint32_t)imm);
}

void cx64_emit_sub_reg_reg(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, rex(true, src > 7, false, dst > 7));
    emit8(b, 0x29);
    emit8(b, modrm(3, src & 7, dst & 7));
}

void cx64_emit_sub_reg_imm32(Cx64CodeBuf *b, int reg, int32_t imm) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0x81);
    emit8(b, modrm(3, 5, reg & 7)); /* opcode /5 = SUB */
    emit32(b, (uint32_t)imm);
}

void cx64_emit_imul_reg_reg(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, rex(true, dst > 7, false, src > 7));
    emit8(b, 0x0F); emit8(b, 0xAF);
    emit8(b, modrm(3, dst & 7, src & 7));
}

void cx64_emit_idiv_rcx(Cx64CodeBuf *b) {
    /* CQO: sign-extend RAX into RDX:RAX */
    emit8(b, 0x48); emit8(b, 0x99);
    /* IDIV RCX */
    emit8(b, 0x48); emit8(b, 0xF7); emit8(b, 0xF9);
}

void cx64_emit_inc_reg(Cx64CodeBuf *b, int reg) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0xFF);
    emit8(b, modrm(3, 0, reg & 7));
}

void cx64_emit_dec_reg(Cx64CodeBuf *b, int reg) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0xFF);
    emit8(b, modrm(3, 1, reg & 7));
}

void cx64_emit_neg_reg(Cx64CodeBuf *b, int reg) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0xF7);
    emit8(b, modrm(3, 3, reg & 7));
}

void cx64_emit_shl_reg_imm8(Cx64CodeBuf *b, int reg, uint8_t count) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0xC1);
    emit8(b, modrm(3, 4, reg & 7));
    emit8(b, count);
}

void cx64_emit_shr_reg_imm8(Cx64CodeBuf *b, int reg, uint8_t count) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0xC1);
    emit8(b, modrm(3, 5, reg & 7));
    emit8(b, count);
}

void cx64_emit_and_reg_reg(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, rex(true, src > 7, false, dst > 7));
    emit8(b, 0x21);
    emit8(b, modrm(3, src & 7, dst & 7));
}

void cx64_emit_or_reg_reg(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, rex(true, src > 7, false, dst > 7));
    emit8(b, 0x09);
    emit8(b, modrm(3, src & 7, dst & 7));
}

void cx64_emit_xor_reg_reg(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, rex(true, dst > 7, false, src > 7));
    emit8(b, 0x33);
    emit8(b, modrm(3, dst & 7, src & 7));
}

/* ---------------------------------------------------------------
   Floating point — SSE2
   --------------------------------------------------------------- */
void cx64_emit_movsd_xmm_mem(Cx64CodeBuf *b, int xmm, int rbp_off) {
    emit8(b, 0xF2); emit8(b, 0x0F); emit8(b, 0x10);
    emit8(b, modrm(2, xmm & 7, 5));
    emit32(b, (uint32_t)(-rbp_off));
}

void cx64_emit_movsd_mem_xmm(Cx64CodeBuf *b, int rbp_off, int xmm) {
    emit8(b, 0xF2); emit8(b, 0x0F); emit8(b, 0x11);
    emit8(b, modrm(2, xmm & 7, 5));
    emit32(b, (uint32_t)(-rbp_off));
}

void cx64_emit_addsd(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, 0xF2); emit8(b, 0x0F); emit8(b, 0x58);
    emit8(b, modrm(3, dst & 7, src & 7));
}

void cx64_emit_subsd(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, 0xF2); emit8(b, 0x0F); emit8(b, 0x5C);
    emit8(b, modrm(3, dst & 7, src & 7));
}

void cx64_emit_mulsd(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, 0xF2); emit8(b, 0x0F); emit8(b, 0x59);
    emit8(b, modrm(3, dst & 7, src & 7));
}

void cx64_emit_divsd(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, 0xF2); emit8(b, 0x0F); emit8(b, 0x5E);
    emit8(b, modrm(3, dst & 7, src & 7));
}

void cx64_emit_sqrtsd(Cx64CodeBuf *b, int dst, int src) {
    emit8(b, 0xF2); emit8(b, 0x0F); emit8(b, 0x51);
    emit8(b, modrm(3, dst & 7, src & 7));
}

/* CVTSI2SD xmm, reg — int64 → double */
void cx64_emit_cvtsi2sd(Cx64CodeBuf *b, int xmm, int reg) {
    emit8(b, 0xF2);
    emit8(b, rex(true, xmm > 7, false, reg > 7));
    emit8(b, 0x0F); emit8(b, 0x2A);
    emit8(b, modrm(3, xmm & 7, reg & 7));
}

/* CVTTSD2SI reg, xmm — double → int64 (truncate) */
void cx64_emit_cvttsd2si(Cx64CodeBuf *b, int reg, int xmm) {
    emit8(b, 0xF2);
    emit8(b, rex(true, reg > 7, false, xmm > 7));
    emit8(b, 0x0F); emit8(b, 0x2C);
    emit8(b, modrm(3, reg & 7, xmm & 7));
}

/* ---------------------------------------------------------------
   Comparison + conditional set
   --------------------------------------------------------------- */
void cx64_emit_cmp_reg_reg(Cx64CodeBuf *b, int a, int bb_reg) {
    emit8(b, rex(true, bb_reg > 7, false, a > 7));
    emit8(b, 0x39);
    emit8(b, modrm(3, bb_reg & 7, a & 7));
}

void cx64_emit_cmp_reg_imm32(Cx64CodeBuf *b, int reg, int32_t imm) {
    emit8(b, rex(true, false, false, reg > 7));
    emit8(b, 0x81);
    emit8(b, modrm(3, 7, reg & 7));
    emit32(b, (uint32_t)imm);
}

void cx64_emit_test_reg_reg(Cx64CodeBuf *b, int a, int bb_reg) {
    emit8(b, rex(true, bb_reg > 7, false, a > 7));
    emit8(b, 0x85);
    emit8(b, modrm(3, bb_reg & 7, a & 7));
}

/* SETcc AL — set byte by condition */
void cx64_emit_sete (Cx64CodeBuf *b) { emit8(b,0x0F); emit8(b,0x94); emit8(b,0xC0); }
void cx64_emit_setne(Cx64CodeBuf *b) { emit8(b,0x0F); emit8(b,0x95); emit8(b,0xC0); }
void cx64_emit_setl (Cx64CodeBuf *b) { emit8(b,0x0F); emit8(b,0x9C); emit8(b,0xC0); }
void cx64_emit_setle(Cx64CodeBuf *b) { emit8(b,0x0F); emit8(b,0x9E); emit8(b,0xC0); }
void cx64_emit_setg (Cx64CodeBuf *b) { emit8(b,0x0F); emit8(b,0x9F); emit8(b,0xC0); }
void cx64_emit_setge(Cx64CodeBuf *b) { emit8(b,0x0F); emit8(b,0x9D); emit8(b,0xC0); }

/* ---------------------------------------------------------------
   Jumps
   --------------------------------------------------------------- */
/* JMP rel32 */
void cx64_emit_jmp_rel32(Cx64CodeBuf *b, int32_t rel) {
    emit8(b, 0xE9); emit32(b, (uint32_t)rel);
}

/* JE rel32 */
void cx64_emit_je_rel32(Cx64CodeBuf *b, int32_t rel) {
    emit8(b, 0x0F); emit8(b, 0x84); emit32(b, (uint32_t)rel);
}

/* JNE rel32 */
void cx64_emit_jne_rel32(Cx64CodeBuf *b, int32_t rel) {
    emit8(b, 0x0F); emit8(b, 0x85); emit32(b, (uint32_t)rel);
}

/* JL rel32 */
void cx64_emit_jl_rel32(Cx64CodeBuf *b, int32_t rel) {
    emit8(b, 0x0F); emit8(b, 0x8C); emit32(b, (uint32_t)rel);
}

/* JLE rel32 */
void cx64_emit_jle_rel32(Cx64CodeBuf *b, int32_t rel) {
    emit8(b, 0x0F); emit8(b, 0x8E); emit32(b, (uint32_t)rel);
}

/* JG rel32 */
void cx64_emit_jg_rel32(Cx64CodeBuf *b, int32_t rel) {
    emit8(b, 0x0F); emit8(b, 0x8F); emit32(b, (uint32_t)rel);
}

/* JGE rel32 */
void cx64_emit_jge_rel32(Cx64CodeBuf *b, int32_t rel) {
    emit8(b, 0x0F); emit8(b, 0x8D); emit32(b, (uint32_t)rel);
}

/* Patch jump target — fill in rel32 at offset patch_pos */
void cx64_patch_jump(Cx64CodeBuf *b, size_t patch_pos, size_t target_pos) {
    if (patch_pos + 4 > b->used) return;
    int32_t rel = (int32_t)((int64_t)target_pos - (int64_t)(patch_pos + 4));
    uint8_t *p  = b->buf + patch_pos;
    p[0] = rel & 0xff;
    p[1] = (rel >> 8)  & 0xff;
    p[2] = (rel >> 16) & 0xff;
    p[3] = (rel >> 24) & 0xff;
}

/* ---------------------------------------------------------------
   CALL + RET
   --------------------------------------------------------------- */
void cx64_emit_call_reg(Cx64CodeBuf *b, int reg) {
    if (reg > 7) emit8(b, 0x41);
    emit8(b, 0xFF);
    emit8(b, modrm(3, 2, reg & 7));
}

void cx64_emit_call_abs(Cx64CodeBuf *b, void *fn) {
    /* MOV R11, imm64; CALL R11 */
    emit8(b, rex(true, false, false, true)); /* REX.W + REX.B for R11 */
    emit8(b, 0xB8 | (REG_R11 & 7));
    emit64(b, (uint64_t)(uintptr_t)fn);
    /* CALL R11 */
    emit8(b, 0x41); emit8(b, 0xFF); emit8(b, 0xD3);
}

void cx64_emit_ret(Cx64CodeBuf *b) {
    emit8(b, 0xC3);
}

void cx64_emit_nop(Cx64CodeBuf *b) {
    emit8(b, 0x90);
}

/* ---------------------------------------------------------------
   System call (Linux)
   --------------------------------------------------------------- */
void cx64_emit_syscall(Cx64CodeBuf *b) {
    emit8(b, 0x0F); emit8(b, 0x05);
}

/* ---------------------------------------------------------------
   Print helper — emit call to printf(fmt, val)
   --------------------------------------------------------------- */
void cx64_emit_print_int(Cx64CodeBuf *b, int64_t val) {
    static const char *fmt = "%lld\n";
    /* MOV RDI, fmt_ptr; MOV RSI, val; XOR EAX, EAX; CALL printf */
#ifndef _WIN32
    cx64_emit_mov_reg_imm64(b, REG_RDI, (int64_t)(uintptr_t)fmt);
    cx64_emit_mov_reg_imm64(b, REG_RSI, val);
    emit8(b, 0x31); emit8(b, 0xC0); /* XOR EAX, EAX */
    cx64_emit_call_abs(b, (void *)printf);
#endif
}

/* ---------------------------------------------------------------
   Loop unroll emit — given body_start, body_end (positions in buf),
   duplicate the body N times (cx64_opt_default().unroll_factor)
   --------------------------------------------------------------- */
void cx64_emit_unrolled_loop(Cx64CodeBuf *b, size_t body_start,
                              size_t body_end, int unroll_count,
                              int ctr_reg) {
    size_t body_len = body_end - body_start;
    if (body_len == 0 || unroll_count <= 1) return;
    if (b->used + body_len * (size_t)(unroll_count - 1) >= b->size) return;

    /* Duplicate body (unroll_count - 1) more times */
    for (int i = 1; i < unroll_count; i++) {
        memcpy(b->buf + b->used, b->buf + body_start, body_len);
        b->used += body_len;
        /* Adjust counter: DEC ctr_reg after each unrolled copy */
        cx64_emit_dec_reg(b, ctr_reg);
    }
}

/* ---------------------------------------------------------------
   Dump emitted bytes (debug)
   --------------------------------------------------------------- */
void cx64_buf_dump(const Cx64CodeBuf *b, size_t max_bytes) {
    if (!b || !b->buf) return;
    size_t n = b->used < max_bytes ? b->used : max_bytes;
    printf("[CX64 CodeBuf core=%u used=%zu]\n", b->core_id, b->used);
    for (size_t i = 0; i < n; i++) {
        if (i % 16 == 0) printf("  %04zx: ", i);
        printf("%02x ", b->buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (n % 16 != 0) printf("\n");
}
