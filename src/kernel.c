#include "kernel.h"
#include "mdk.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
   McBL# Kernel implementation
   ----------------------------------------------------------------------- */

Kernel *kernel_create(SymTable *symbols, KernelMode mode) {
    Kernel *k = (Kernel *)mcbl_calloc(1, sizeof(Kernel));
    k->mode    = mode;
    k->symbols = symbols;
    k->jit     = jit_create(symbols);
    return k;
}

void kernel_destroy(Kernel *k) {
    if (!k) return;
    jit_destroy(k->jit);
    k->jit = NULL;
    mcbl_free((void **)&k);
}

/* Execution hot-path counter (per chunk, tracked via chunk address) */
#define EXEC_MAP_SIZE 256

typedef struct {
    const BcChunk *chunk;
    int            count;
    int            jit_region;  /* -1 = not yet JIT-compiled */
} ExecEntry;

static ExecEntry exec_map[EXEC_MAP_SIZE];
static int       exec_map_inited = 0;

static ExecEntry *exec_map_get(const BcChunk *chunk) {
    if (!exec_map_inited) {
        memset(exec_map, 0, sizeof(exec_map));
        for (int i = 0; i < EXEC_MAP_SIZE; i++) exec_map[i].jit_region = -1;
        exec_map_inited = 1;
    }
    unsigned int h = (unsigned int)((size_t)chunk >> 4) % EXEC_MAP_SIZE;
    /* linear probe */
    for (int i = 0; i < EXEC_MAP_SIZE; i++) {
        int idx = (h + i) % EXEC_MAP_SIZE;
        if (!exec_map[idx].chunk) {
            exec_map[idx].chunk      = chunk;
            exec_map[idx].count      = 0;
            exec_map[idx].jit_region = -1;
            return &exec_map[idx];
        }
        if (exec_map[idx].chunk == chunk) return &exec_map[idx];
    }
    return NULL; /* full — fallback to entry 0 */
}

int kernel_exec(Kernel *k, BcChunk *chunk) {
    if (!k || !chunk) return -1;
    k->exec_count++;

    ExecEntry *entry = exec_map_get(chunk);

    /* Determine execution mode */
    int use_jit = 0;
    if (k->mode == KERNEL_MODE_JIT) {
        use_jit = 1;
    } else if (k->mode == KERNEL_MODE_AUTO && entry) {
        entry->count++;
        if (entry->count >= JIT_THRESHOLD) use_jit = 1;
    }

    if (use_jit && entry && entry->jit_region < 0) {
        /* First time JIT threshold crossed — compile */
        entry->jit_region = jit_compile_chunk(k->jit, chunk);
    }

    if (use_jit && entry && entry->jit_region >= 0) {
        /* Execute JIT-compiled native code */
        jit_exec(k->jit, entry->jit_region);
        k->jit_count++;
        return 0;
    }

    /* Fall back to MDK bytecode VM */
    MdkVM *vm = mdk_vm_create(k->symbols);
    if (!vm) return -1;
    int r = mdk_vm_exec(vm, chunk);
    mdk_vm_destroy(vm);
    k->vm_count++;
    return r;
}

void kernel_stats(const Kernel *k) {
    if (!k) return;
    printf("McBL# Kernel stats:\n");
    printf("  Total dispatches : %d\n", k->exec_count);
    printf("  VM dispatches    : %d\n", k->vm_count);
    printf("  JIT dispatches   : %d\n", k->jit_count);
}
