#ifndef MCBL_JIT_H
#define MCBL_JIT_H

#include "bytecode.h"
#include "symbols.h"
#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
   McBL# JIT Compiler
   Converts hot BcChunk segments to native x86_64 machine code using
   mmap-based executable memory pages.  Invoked by the MDK VM when a
   chunk's execution counter exceeds JIT_THRESHOLD.
   ----------------------------------------------------------------------- */

#define JIT_THRESHOLD     16        /* executions before JIT kicks in      */
#define JIT_EXEC_MEM_SIZE (1 << 20) /* 1 MiB per JIT region                */

typedef void (*JitFn)(void);

typedef struct {
    uint8_t  *mem;      /* executable memory page (mmap/VirtualAlloc)     */
    size_t    mem_size;
    size_t    used;
    JitFn     entry;    /* entry point into compiled native code           */
    int       compiled; /* 1 = successfully compiled                       */
} JitRegion;

typedef struct {
    JitRegion *regions;
    int        region_count;
    int        region_cap;
    SymTable  *symbols;     /* shared symbol table for value lookup        */
} JitCompiler;

JitCompiler *jit_create(SymTable *symbols);
void         jit_destroy(JitCompiler *jc);

/* Attempt to JIT-compile a bytecode chunk.
   Returns the JitRegion index (>=0) on success, -1 on failure. */
int          jit_compile_chunk(JitCompiler *jc, const BcChunk *chunk);

/* Execute a previously compiled region */
void         jit_exec(JitCompiler *jc, int region_id);

#endif /* MCBL_JIT_H */
