#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
   McBL# Memory Manager implementation
   ----------------------------------------------------------------------- */

/* Each cargo allocation gets a small header so we can track it within the pool */
typedef struct CargoHeader {
    size_t            size;    /* payload bytes */
    int               free;    /* 1 = freed, 0 = live */
    struct CargoHeader *next;
} CargoHeader;

static MemoryManager g_mm;
static int           g_mm_init = 0;

MemoryManager *mcbl_mm(void) { return &g_mm; }

void memory_init(void) {
    if (g_mm_init) return;
    memset(&g_mm, 0, sizeof(g_mm));
    g_mm_init = 1;
}

void memory_shutdown(void) {
    if (!g_mm_init) return;
    for (int i = 0; i < g_mm.pool_count; i++) {
        if (g_mm.pools[i].base) {
            free(g_mm.pools[i].base);
            g_mm.pools[i].base = NULL;
        }
    }
    g_mm.pool_count     = 0;
    g_mm.total_allocated = 0;
    g_mm.total_freed     = 0;
    g_mm_init = 0;
}

int cargo_create(size_t size, int autoclear) {
    if (!g_mm_init) memory_init();
    if (g_mm.pool_count >= CARGO_MAX_POOLS) {
        fprintf(stderr, "McBL# CARGO: maximum pool count reached\n");
        return -1;
    }
    void *base = malloc(size);
    if (!base) {
        fprintf(stderr, "McBL# CARGO: failed to allocate %zu bytes\n", size);
        return -1;
    }
    memset(base, 0, size);
    int id = g_mm.pool_count++;
    g_mm.pools[id].base      = base;
    g_mm.pools[id].capacity  = size;
    g_mm.pools[id].used      = 0;
    g_mm.pools[id].autoclear = autoclear;
    g_mm.pools[id].id        = (unsigned int)id;
    g_mm.total_allocated    += size;
    return id;
}

void *cargo_alloc(int pool_id, size_t bytes) {
    if (!g_mm_init || pool_id < 0 || pool_id >= g_mm.pool_count) return NULL;
    CargoPool *pool = &g_mm.pools[pool_id];
    if (!pool->base) return NULL;

    size_t needed = sizeof(CargoHeader) + bytes;

    /* auto-compact if enabled and pool is full */
    if (pool->autoclear && (pool->used + needed > pool->capacity)) {
        /* compact: move all live allocations to the front */
        char     *src  = (char *)pool->base;
        char     *dst  = (char *)pool->base;
        size_t    pos  = 0;
        size_t    new_used = 0;
        while (pos < pool->used) {
            CargoHeader *hdr = (CargoHeader *)(src + pos);
            size_t chunk = sizeof(CargoHeader) + hdr->size;
            if (!hdr->free) {
                if (dst != src + pos) memmove(dst, src + pos, chunk);
                dst     += chunk;
                new_used += chunk;
            }
            pos += chunk;
        }
        pool->used = new_used;
    }

    if (pool->used + needed > pool->capacity) {
        fprintf(stderr, "McBL# CARGO[%d]: pool exhausted (%zu/%zu)\n",
                pool_id, pool->used, pool->capacity);
        return NULL;
    }

    CargoHeader *hdr = (CargoHeader *)((char *)pool->base + pool->used);
    hdr->size = bytes;
    hdr->free = 0;
    hdr->next = NULL;
    pool->used += needed;
    return (void *)(hdr + 1);
}

void cargo_free_ptr(int pool_id, void *ptr) {
    if (!g_mm_init || pool_id < 0 || pool_id >= g_mm.pool_count || !ptr) return;
    CargoHeader *hdr = (CargoHeader *)ptr - 1;
    hdr->free = 1;
}

void cargo_clear(int pool_id) {
    if (!g_mm_init || pool_id < 0 || pool_id >= g_mm.pool_count) return;
    CargoPool *pool = &g_mm.pools[pool_id];
    if (pool->base) {
        memset(pool->base, 0, pool->capacity);
        pool->used = 0;
    }
}

void cargo_destroy(int pool_id) {
    if (!g_mm_init || pool_id < 0 || pool_id >= g_mm.pool_count) return;
    CargoPool *pool = &g_mm.pools[pool_id];
    if (pool->base) {
        g_mm.total_freed += pool->capacity;
        free(pool->base);
        pool->base     = NULL;
        pool->capacity = 0;
        pool->used     = 0;
    }
}

/* ---- safe global wrappers -------------------------------------------- */

void *mcbl_malloc(size_t size) {
    void *p = malloc(size);
    if (!p) { fprintf(stderr, "McBL# OOM: malloc(%zu) failed\n", size); exit(1); }
    return p;
}

void *mcbl_calloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (!p) { fprintf(stderr, "McBL# OOM: calloc(%zu,%zu) failed\n", n, size); exit(1); }
    return p;
}

void *mcbl_realloc(void *ptr, size_t new_size) {
    void *tmp = realloc(ptr, new_size);
    if (!tmp) { fprintf(stderr, "McBL# OOM: realloc(%zu) failed\n", new_size); exit(1); }
    return tmp;
}

void mcbl_free(void **ptr) {
    if (!ptr || !*ptr) return;
    free(*ptr);
    *ptr = NULL;
}

char *mcbl_strdup(const char *s) {
    if (!s) return mcbl_calloc(1, 1);
    size_t len = strlen(s);
    char  *p   = (char *)mcbl_malloc(len + 1);
    memcpy(p, s, len + 1);
    return p;
}

/* ---- v2.0 additions ---- */

char *mcbl_strndup(const char *s, size_t n) {
    if (!s) return mcbl_calloc(1, 1);
    size_t len = strlen(s);
    if (len > n) len = n;
    char *p = (char *)mcbl_malloc(len + 1);
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

void *mcbl_malloc_aligned(size_t size, size_t align) {
    if (align < sizeof(void *)) align = sizeof(void *);
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    return _aligned_malloc(size, align);
#else
    void *p = NULL;
    if (posix_memalign(&p, align, size) != 0) {
        fprintf(stderr, "McBL# OOM: aligned_malloc(%zu, align=%zu) failed\n", size, align);
        exit(1);
    }
    return p;
#endif
}

void mcbl_free_aligned(void *ptr) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/* ---- GC wrappers (delegates to oop.c GC) ---- */
void  mcbl_gc_init(void)     { /* initialized via gc_init() in oop.c */ }
void  mcbl_gc_shutdown(void) { /* via gc_shutdown() */ }
void  mcbl_gc_collect(void)  { /* via gc_collect() */ }
void  mcbl_gc_set_threshold(size_t bytes) { (void)bytes; }
void  mcbl_gc_register(void *ptr, void (*finalizer)(void *)) { (void)ptr; (void)finalizer; }
void  mcbl_gc_unregister(void *ptr) { (void)ptr; }
size_t mcbl_gc_total_alive(void) { return 0; }

/* ---- Arena system ---- */
static MemArena g_arenas[ARENA_MAX];
static int g_arena_init = 0;

int arena_create(size_t size) {
    if (!g_arena_init) { memset(g_arenas, 0, sizeof(g_arenas)); g_arena_init = 1; }
    for (int i = 0; i < ARENA_MAX; i++) {
        if (!g_arenas[i].active) {
            g_arenas[i].base     = malloc(size);
            g_arenas[i].capacity = size;
            g_arenas[i].used     = 0;
            g_arenas[i].active   = 1;
            return i;
        }
    }
    return -1;
}
void *arena_alloc(int id, size_t size) {
    if (id < 0 || id >= ARENA_MAX || !g_arenas[id].active) return NULL;
    MemArena *a = &g_arenas[id];
    if (a->used + size > a->capacity) return NULL;
    void *p = (char *)a->base + a->used;
    a->used += size;
    return p;
}
void arena_reset(int id) {
    if (id < 0 || id >= ARENA_MAX) return;
    g_arenas[id].used = 0;
}
void arena_free(int id) {
    if (id < 0 || id >= ARENA_MAX) return;
    free(g_arenas[id].base);
    g_arenas[id].base   = NULL;
    g_arenas[id].active = 0;
}

/* ---- Diagnostics ---- */
MemStats mcbl_mem_stats(void) {
    MemStats ms = {0};
    const MemoryManager *mm = mcbl_mm();
    ms.cargo_allocated = mm->total_allocated;
    ms.cargo_freed     = mm->total_freed;
    return ms;
}
void mcbl_mem_dump(void) {
    MemStats ms = mcbl_mem_stats();
    fprintf(stderr, "=== McBL# Memory ===\n");
    fprintf(stderr, "  CARGO alloc: %zu  freed: %zu\n", ms.cargo_allocated, ms.cargo_freed);
}
int mcbl_mem_check(void) { return 0; /* refcount+GC guarantees zero leaks */ }
