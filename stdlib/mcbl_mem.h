#ifndef MCBL_MEM_H
#define MCBL_MEM_H
/*
 * McBL# mem.* — Memory Management (No GC, pure manual/refcount)
 * ==============================================================
 * Zero garbage collector — memory freed immediately on last reference.
 *
 * mem.alloc(size)        → allocate bytes, return ptr
 * mem.calloc(n, size)    → zero-initialize allocation
 * mem.realloc(ptr, size) → resize allocation
 * mem.free(ptr)          → free immediately (NO GC delay)
 * mem.copy(dst, src, n)  → memcpy
 * mem.move(dst, src, n)  → memmove
 * mem.set(ptr, val, n)   → memset
 * mem.eq(a, b, n)        → memcmp == 0
 * mem.arena_new(size)    → create arena allocator
 * mem.arena_alloc(a,n)   → alloc from arena
 * mem.arena_reset(a)     → reset arena (free all at once)
 * mem.arena_free(a)      → destroy arena
 * mem.pool_new(esize,cap)→ create pool allocator
 * mem.pool_get(pool)     → get element from pool
 * mem.pool_put(pool,ptr) → return element to pool
 * mem.stats()            → print memory usage stats
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic allocation — wraps malloc/calloc/realloc/free */
void    *mcbl_mem_alloc  (size_t size);
void    *mcbl_mem_calloc (size_t n, size_t size);
void    *mcbl_mem_realloc(void *ptr, size_t size);
void     mcbl_mem_free   (void **pptr);   /* zeroes *pptr after free */

/* Memory ops */
void    *mcbl_mem_copy(void *dst, const void *src, size_t n);
void    *mcbl_mem_move(void *dst, const void *src, size_t n);
void    *mcbl_mem_set (void *dst, int val, size_t n);
int      mcbl_mem_eq  (const void *a, const void *b, size_t n);

/* Aligned allocation (for SIMD) */
void    *mcbl_mem_alloc_aligned(size_t size, size_t align);
void     mcbl_mem_free_aligned (void *ptr);

/* Arena allocator — alloc fast, free all at once */
typedef struct McblArena McblArena;
McblArena *mcbl_mem_arena_new  (size_t capacity);
void      *mcbl_mem_arena_alloc(McblArena *a, size_t size);
void      *mcbl_mem_arena_alloc_aligned(McblArena *a, size_t size, size_t align);
size_t     mcbl_mem_arena_used (McblArena *a);
size_t     mcbl_mem_arena_cap  (McblArena *a);
void       mcbl_mem_arena_reset(McblArena *a);    /* free all at once */
void       mcbl_mem_arena_free (McblArena *a);    /* destroy arena */

/* Object pool — pre-allocated fixed-size elements */
typedef struct McblPool McblPool;
McblPool  *mcbl_mem_pool_new(size_t elem_size, int capacity);
void      *mcbl_mem_pool_get (McblPool *pool);     /* get one element */
void       mcbl_mem_pool_put (McblPool *pool, void *ptr); /* return element */
int        mcbl_mem_pool_free_count(McblPool *pool);
void       mcbl_mem_pool_destroy(McblPool *pool);

/* Reference counting helpers */
typedef struct {
    int   refs;
    void  (*destructor)(void *self);
} McblRefObj;

void  *mcbl_rc_new  (size_t size, void (*destructor)(void *));
void  *mcbl_rc_ref  (void *obj);
void   mcbl_rc_unref(void **pobj);   /* frees when refs == 0 */
int    mcbl_rc_count(void *obj);

/* Stats */
typedef struct {
    size_t total_allocated;
    size_t total_freed;
    size_t current_live;
    size_t peak_live;
    int    alloc_count;
    int    free_count;
    int    leak_count;   /* always 0 with proper usage */
} McblMemStats;

McblMemStats mcbl_mem_stats(void);
void         mcbl_mem_print_stats(void);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_MEM_H */
