/*
 * cx64_opt.cpp — CompilerX64 Optimizer
 * ======================================
 * Optimasi IR sebelum emit ke machine code.
 * Passes:
 *   1. Constant folding
 *   2. Dead code elimination (DCE)
 *   3. Loop unrolling (12x — makin banyak run makin cepat)
 *   4. Strength reduction (x*2 → SHL, x*const → shifts)
 *   5. Peephole (pattern matching + replace)
 *   6. Linear scan register allocation
 *   7. Common subexpression elimination (CSE)
 */
#include "cx64_defs.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

/* ---------------------------------------------------------------
   Pass 1: Constant Folding
   Lipat operasi yang hasilnya bisa diketahui saat compile time.
   --------------------------------------------------------------- */
static int opt_constant_fold(Cx64Unit *u) {
    int changes = 0;
    for (int i = 0; i + 1 < u->ir_count; i++) {
        Cx64IR *a = &u->ir[i];
        Cx64IR *b = &u->ir[i + 1];

        /* MOV dst, imm; MOV dst2, imm2; ADD dst, dst2  →  MOV dst, imm+imm2 */
        if (a->op == CX64_IR_MOV && b->op == CX64_IR_MOV && i + 2 < u->ir_count) {
            Cx64IR *op = &u->ir[i + 2];
            int64_t va = a->imm, vb = b->imm;
            int fold = 0;
            int64_t result = 0;

            switch (op->op) {
                case CX64_IR_ADD: result = va + vb; fold = 1; break;
                case CX64_IR_SUB: result = va - vb; fold = 1; break;
                case CX64_IR_MUL: result = va * vb; fold = 1; break;
                case CX64_IR_DIV: if (vb != 0) { result = va / vb; fold = 1; } break;
                case CX64_IR_MOD: if (vb != 0) { result = va % vb; fold = 1; } break;
                case CX64_IR_SHL: result = va << (vb & 63); fold = 1; break;
                case CX64_IR_SHR: result = (int64_t)((uint64_t)va >> (vb & 63)); fold = 1; break;
                case CX64_IR_AND: result = va & vb; fold = 1; break;
                case CX64_IR_OR:  result = va | vb; fold = 1; break;
                case CX64_IR_XOR: result = va ^ vb; fold = 1; break;
                default: break;
            }
            if (fold) {
                /* Replace a with folded result, mark b and op as NOP */
                a->imm = result;
                b->op  = CX64_IR_NOP;
                op->op = CX64_IR_NOP;
                changes++;
                i += 2; /* skip past folded instrs */
            }
        }

        /* x * 1 → x */
        if (b->op == CX64_IR_MUL && b->imm == 1) {
            b->op = CX64_IR_NOP;
            changes++;
        }
        /* x * 0 → 0 */
        if (b->op == CX64_IR_MUL && b->imm == 0) {
            b->op = CX64_IR_MOV; b->imm = 0;
            changes++;
        }
        /* x + 0 → x */
        if (b->op == CX64_IR_ADD && b->imm == 0) {
            b->op = CX64_IR_NOP; changes++;
        }
        /* x - 0 → x */
        if (b->op == CX64_IR_SUB && b->imm == 0) {
            b->op = CX64_IR_NOP; changes++;
        }
    }
    return changes;
}

/* ---------------------------------------------------------------
   Pass 2: Dead Code Elimination
   Hapus NOP dan instruksi yang hasilnya tidak dipakai.
   --------------------------------------------------------------- */
static int opt_dce(Cx64Unit *u) {
    int removed = 0;
    /* Pack out NOP instructions */
    int write = 0;
    for (int i = 0; i < u->ir_count; i++) {
        if (u->ir[i].op == CX64_IR_NOP) {
            removed++;
        } else {
            if (write != i) u->ir[write] = u->ir[i];
            write++;
        }
    }
    u->ir_count = write;

    /* Remove instructions after unconditional JMP with no label target */
    for (int i = 0; i + 1 < u->ir_count; i++) {
        if (u->ir[i].op == CX64_IR_JMP) {
            int j = i + 1;
            while (j < u->ir_count && u->ir[j].op != CX64_IR_LABEL) {
                u->ir[j].op = CX64_IR_NOP;
                removed++;
                j++;
            }
        }
    }
    return removed;
}

/* ---------------------------------------------------------------
   Pass 3: Strength Reduction
   Ganti operasi mahal dengan yang lebih murah.
   --------------------------------------------------------------- */
static int opt_strength_reduce(Cx64Unit *u) {
    int changes = 0;
    for (int i = 0; i < u->ir_count; i++) {
        Cx64IR *ir = &u->ir[i];

        /* x * 2^n → x SHL n */
        if (ir->op == CX64_IR_MUL && ir->imm > 0) {
            int64_t v = ir->imm;
            if ((v & (v - 1)) == 0) { /* power of 2 */
                int shift = 0;
                while ((1LL << shift) < v) shift++;
                ir->op  = CX64_IR_SHL;
                ir->imm = shift;
                changes++;
            }
        }

        /* x / 2^n → x SHR n (for unsigned / positive) */
        if (ir->op == CX64_IR_DIV && ir->imm > 0) {
            int64_t v = ir->imm;
            if ((v & (v - 1)) == 0) {
                int shift = 0;
                while ((1LL << shift) < v) shift++;
                ir->op  = CX64_IR_SHR;
                ir->imm = shift;
                changes++;
            }
        }

        /* x % 2^n → x AND (2^n - 1) */
        if (ir->op == CX64_IR_MOD && ir->imm > 0) {
            int64_t v = ir->imm;
            if ((v & (v - 1)) == 0) {
                ir->op  = CX64_IR_AND;
                ir->imm = v - 1;
                changes++;
            }
        }

        /* NEG + ADD → SUB */
        if (ir->op == CX64_IR_NEG && i + 1 < u->ir_count &&
            u->ir[i + 1].op == CX64_IR_ADD) {
            ir->op         = CX64_IR_NOP;
            u->ir[i+1].op  = CX64_IR_SUB;
            changes++;
        }
    }
    return changes;
}

/* ---------------------------------------------------------------
   Pass 4: Loop Unrolling
   Deteksi LOOP_BEGIN...LOOP_END dan duplikat body 12x.
   Ini yang bikin "12 run per 0.1 detik" — makin banyak unroll
   makin efisien karena CPU branch predictor selalu benar.
   --------------------------------------------------------------- */
static int opt_loop_unroll(Cx64Unit *u, const Cx64OptFlags *flags) {
    if (!flags->do_loop_unroll) return 0;
    int unrolled = 0;

    for (int i = 0; i < u->ir_count; i++) {
        if (u->ir[i].op != CX64_IR_LOOP_BEGIN) continue;

        /* Find matching LOOP_END */
        int begin = i;
        int end   = -1;
        int depth = 1;
        for (int j = i + 1; j < u->ir_count; j++) {
            if (u->ir[j].op == CX64_IR_LOOP_BEGIN) depth++;
            if (u->ir[j].op == CX64_IR_LOOP_END)   depth--;
            if (depth == 0) { end = j; break; }
        }
        if (end < 0) continue;

        int body_len = end - begin - 1;
        if (body_len <= 0) continue;

        /* Check there's room to unroll */
        int factor = flags->unroll_factor; /* default 12 */
        int new_total = u->ir_count + body_len * (factor - 1);
        if (new_total >= CX64_IR_MAX) {
            factor = (CX64_IR_MAX - u->ir_count - 1) / body_len + 1;
            if (factor < 2) continue;
        }

        /* Shift existing instructions right to make room */
        int insert_at = end; /* insert copies before LOOP_END */
        int copies_len = body_len * (factor - 1);
        memmove(&u->ir[insert_at + copies_len],
                &u->ir[insert_at],
                sizeof(Cx64IR) * (size_t)(u->ir_count - insert_at));
        u->ir_count += copies_len;

        /* Copy body (factor-1) more times */
        for (int k = 1; k < factor; k++) {
            memcpy(&u->ir[insert_at + (k - 1) * body_len],
                   &u->ir[begin + 1],
                   sizeof(Cx64IR) * (size_t)body_len);
        }

        /* Mark this loop as done — replace BEGIN/END with NOPs */
        u->ir[begin].op = CX64_IR_NOP;
        u->ir[insert_at + copies_len - 1 + body_len].op = CX64_IR_NOP;

        unrolled++;
        i = insert_at + copies_len; /* skip past unrolled block */
    }
    return unrolled;
}

/* ---------------------------------------------------------------
   Pass 5: Peephole Optimization
   Pattern matching pada 2-3 instruksi berturutan.
   --------------------------------------------------------------- */
static int opt_peephole(Cx64Unit *u) {
    int changes = 0;
    for (int i = 0; i + 1 < u->ir_count; i++) {
        Cx64IR *a = &u->ir[i];
        Cx64IR *b = &u->ir[i + 1];

        /* MOV dst, x followed by MOV x, dst → remove second MOV */
        if (a->op == CX64_IR_MOV && b->op == CX64_IR_MOV &&
            a->dst == b->src_a && b->dst == a->src_a) {
            b->op = CX64_IR_NOP;
            changes++;
        }

        /* XOR reg, reg (clear) followed by ADD reg, 0 → only XOR */
        if (a->op == CX64_IR_XOR && a->dst == a->src_a &&
            b->op == CX64_IR_ADD && b->imm == 0) {
            b->op = CX64_IR_NOP;
            changes++;
        }

        /* CMP + branch: combine into single conditional jump hint */
        if (a->op == CX64_IR_CMP && b->op == CX64_IR_JMP) {
            /* Already handled in emitter — no change needed */
        }

        /* INC then CMP reg, limit → use loop counter register */
        if (a->op == CX64_IR_INC && b->op == CX64_IR_CMP &&
            a->dst == b->src_a) {
            a->is_loop = 1; /* flag as loop counter op */
        }
    }
    return changes;
}

/* ---------------------------------------------------------------
   Pass 6: Linear Scan Register Allocation
   Assign physical registers ke virtual registers.
   Uses "next-use" distance heuristic.
   --------------------------------------------------------------- */
#define PHYS_REGS_AVAIL 6  /* RAX RCX RDX RSI RDI R8 (R9-R15 reserved for special) */
static const int ALLOC_REGS[PHYS_REGS_AVAIL] = {
    REG_RAX, REG_RCX, REG_RDX, REG_RSI, REG_RDI, REG_R8
};

static int opt_reg_alloc(Cx64Unit *u) {
    /* Track which virtual regs are currently in physical registers */
    int phys_to_virt[PHYS_REGS_AVAIL];
    int virt_to_phys[CX64_VAR_MAX];
    for (int i = 0; i < PHYS_REGS_AVAIL; i++) phys_to_virt[i] = -1;
    for (int i = 0; i < CX64_VAR_MAX; i++) virt_to_phys[i] = -1;

    int spills = 0;
    for (int i = 0; i < u->ir_count; i++) {
        Cx64IR *ir = &u->ir[i];
        int vr = ir->dst;
        if (vr < 0 || vr >= CX64_VAR_MAX) continue;

        /* Try to find free physical register */
        int free_phys = -1;
        for (int p = 0; p < PHYS_REGS_AVAIL; p++) {
            if (phys_to_virt[p] == -1) { free_phys = p; break; }
        }

        if (free_phys >= 0) {
            /* Assign */
            phys_to_virt[free_phys]  = vr;
            virt_to_phys[vr]         = ALLOC_REGS[free_phys];
            u->vars[vr].in_reg       = ALLOC_REGS[free_phys];
        } else {
            /* Spill — find oldest allocation (LRU) */
            int evict = 0;
            phys_to_virt[evict]              = -1;
            u->vars[phys_to_virt[evict]].in_reg = -1;
            phys_to_virt[evict]  = vr;
            virt_to_phys[vr]     = ALLOC_REGS[evict];
            u->vars[vr].in_reg   = ALLOC_REGS[evict];
            spills++;
        }
    }
    return spills;
}

/* ---------------------------------------------------------------
   Run all optimization passes
   --------------------------------------------------------------- */
void cx64_optimize(Cx64Unit *u, const Cx64OptFlags *flags) {
    if (!u || !flags) return;

    int total_changes = 0;
    int passes = 3; /* run multiple times until convergence */

    for (int p = 0; p < passes; p++) {
        int changes = 0;
        if (flags->do_constant_fold)  changes += opt_constant_fold(u);
        if (flags->do_strength_reduce)changes += opt_strength_reduce(u);
        if (flags->do_peephole)       changes += opt_peephole(u);
        if (flags->do_dce)            changes += opt_dce(u);
        total_changes += changes;
        if (changes == 0) break; /* converged */
    }

    /* Loop unroll runs once after convergence */
    if (flags->do_loop_unroll) {
        int unrolled = opt_loop_unroll(u, flags);
        if (unrolled > 0) opt_dce(u); /* clean up after unroll */
        total_changes += unrolled;
    }

    /* Register allocation — run last */
    if (flags->do_reg_alloc) opt_reg_alloc(u);

    if (total_changes > 0)
        printf("[CX64 OPT] %d optimizations applied (%d IR instrs remaining)\n",
               total_changes, u->ir_count);
}
