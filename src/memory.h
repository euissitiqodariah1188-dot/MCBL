#ifndef MCBL_MEMORY_H
#define MCBL_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
   McBL# Memory Manager  (v2.0 — UPGRADED)
   CARGO_m / CLEAR_cargo / AUTOCLEAR_cargo / WriteTRam / cleanTram
   + GC integration, no memory leaks.
   ----------------------------------------------------------------------- */

#define CARGO_MAX_POOLS   64
#define ARENA_MAX         32    /* general-purpose arenas */

/* ---- Cargo Pool ---- */
typedef struct {
    void        *base;
    size_t       capacity;
    size_t       used;
    int          autoclear;
    unsigned int id;
    char         name[64];   /* optional debug label */
} CargoPool;

typedef struct {
    CargoPool pools[CARGO_MAX_POOLS];
    int       pool_count;
    size_t    total_allocated;
    size_t    total_freed;
} MemoryManager;

MemoryManager *mcbl_mm(void);

void  memory_init(void);
void  memory_shutdown(void);

/* CARGO_m(size) — allocates a cargo pool, returns pool id or -1 */
int   cargo_create(size_t size, int autoclear);

/* Named cargo pool for debugging */
int   cargo_create_named(size_t size, int autoclear, const char *name);

/* Allocate from a specific cargo pool */
void *cargo_alloc(int pool_id, size_t bytes);

/* Allocate aligned memory from cargo pool (for SIMD) */
void *cargo_alloc_aligned(int pool_id, size_t bytes, size_t align);

/* Free individual allocation */
void  cargo_free_ptr(int pool_id, void *ptr);

/* CLEAR_cargo */
void  cargo_clear(int pool_id);

/* Destroy a pool completely */
void  cargo_destroy(int pool_id);

/* Get pool usage stats */
void  cargo_stats(int pool_id, size_t *used, size_t *cap);

/* ---- General arenas ---- */
typedef struct {
    void   *base;
    size_t  capacity;
    size_t  used;
    int     active;
} MemArena;

int   arena_create(size_t size);
void *arena_alloc (int id, size_t bytes);
void  arena_reset (int id);
void  arena_free  (int id);

/* ---- Safe global wrappers (terminate on OOM) ---- */
void *mcbl_malloc (size_t size);
void *mcbl_calloc (size_t n, size_t size);
void *mcbl_realloc(void *ptr, size_t new_size);
void  mcbl_free   (void **ptr);   /* nullifies *ptr after free */

/* Aligned allocation */
void *mcbl_malloc_aligned(size_t size, size_t align);
void  mcbl_free_aligned  (void *ptr);

/* String helpers */
char *mcbl_strdup (const char *s);
char *mcbl_strndup(const char *s, size_t n);

/* ---- GC interface (used by OOP system) ---- */
void  mcbl_gc_init     (void);
void  mcbl_gc_shutdown (void);
void  mcbl_gc_register (void *ptr, void (*finalizer)(void *));
void  mcbl_gc_unregister(void *ptr);
void  mcbl_gc_collect  (void);
void  mcbl_gc_set_threshold(size_t bytes);
size_t mcbl_gc_total_alive(void);

/* ---- Memory diagnostics ---- */
typedef struct {
    size_t   heap_allocated;
    size_t   heap_freed;
    size_t   cargo_allocated;
    size_t   cargo_freed;
    size_t   gc_objects;
    size_t   gc_collected;
    int      leak_count;
} MemStats;

MemStats mcbl_mem_stats(void);
void     mcbl_mem_dump  (void);     /* print diagnostics to stderr */
int      mcbl_mem_check (void);     /* returns 0 if no leaks */

/* ---- Fence / guard pages (debug builds) ---- */
#ifdef MCBL_DEBUG
void *mcbl_malloc_guarded(size_t size);  /* adds guard pages */
#else
#define mcbl_malloc_guarded(s) mcbl_malloc(s)
#endif

#endif /* MCBL_MEMORY_H */
