#ifndef MCBL_KERNEL_H
#define MCBL_KERNEL_H

#include "bytecode.h"
#include "jit.h"
#include "symbols.h"

/* -----------------------------------------------------------------------
   McBL# Kernel
   Connects the compiler pipeline and interpreter to the CPU.
   Schedules execution: decides whether a chunk runs in the MDK VM
   (interpreter mode) or is handed to the JIT for native execution.
   ----------------------------------------------------------------------- */

typedef enum {
    KERNEL_MODE_INTERPRET,  /* always use bytecode VM             */
    KERNEL_MODE_JIT,        /* always use JIT                     */
    KERNEL_MODE_AUTO        /* auto: JIT after JIT_THRESHOLD runs */
} KernelMode;

typedef struct {
    KernelMode   mode;
    JitCompiler *jit;
    SymTable    *symbols;
    int          exec_count;   /* total executions dispatched       */
    int          jit_count;    /* how many were JIT-dispatched      */
    int          vm_count;     /* how many were VM-dispatched       */
} Kernel;

Kernel *kernel_create(SymTable *symbols, KernelMode mode);
void    kernel_destroy(Kernel *k);

/* Schedule execution of a bytecode chunk.
   Returns 0 on success, -1 on error. */
int     kernel_exec(Kernel *k, BcChunk *chunk);

/* Print kernel statistics */
void    kernel_stats(const Kernel *k);

#endif /* MCBL_KERNEL_H */
