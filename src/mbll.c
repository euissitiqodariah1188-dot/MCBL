/*
 * MBLL — McBL Bridge Low Level  (v2.0)
 */
#include "mbll.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  #include <windows.h>
  #define DL_OPEN(p)   LoadLibrary(p)
  #define DL_SYM(h,s)  GetProcAddress((HMODULE)(h), s)
  #define DL_CLOSE(h)  FreeLibrary((HMODULE)(h))
  #define DL_EXT       ".dll"
#else
  #include <dlfcn.h>
  #define DL_OPEN(p)   dlopen(p, RTLD_LAZY)
  #define DL_SYM(h,s)  dlsym(h, s)
  #define DL_CLOSE(h)  dlclose(h)
  #define DL_EXT       ".so"
#endif

/* -------------------------------------------------------------------
   TRam Manager
   ------------------------------------------------------------------- */
static TRamManager g_tram;
static int         g_tram_init = 0;

TRamManager *tram_manager(void) { return &g_tram; }

void tram_init(void) {
    if (g_tram_init) return;
    memset(&g_tram, 0, sizeof(g_tram));
    g_tram_init = 1;
}

void tram_shutdown(void) {
    if (!g_tram_init) return;
    tram_clean_all();
    for (int i = 0; i < g_tram.block_count; i++) {
        if (g_tram.blocks[i].base) {
            free(g_tram.blocks[i].base);
            g_tram.blocks[i].base = NULL;
        }
    }
    g_tram_init = 0;
}

int tram_alloc(size_t mb) {
    if (!g_tram_init) tram_init();
    if (g_tram.block_count >= TRAM_MAX_BLOCKS) return -1;
    size_t bytes = mb * TRAM_MB;
    void *base = malloc(bytes);
    if (!base) { fprintf(stderr, "TRam: failed to allocate %zu MB\n", mb); return -1; }
    memset(base, 0, bytes);
    int id = g_tram.block_count++;
    g_tram.blocks[id].base     = base;
    g_tram.blocks[id].capacity = bytes;
    g_tram.blocks[id].used     = 0;
    g_tram.blocks[id].active   = 1;
    g_tram.blocks[id].id       = (uint32_t)id;
    g_tram.total_mb            += mb;
    return id;
}

void *tram_alloc_in(int id, size_t size) {
    if (!g_tram_init || id < 0 || id >= g_tram.block_count) return NULL;
    TRamBlock *b = &g_tram.blocks[id];
    if (!b->active || b->used + size > b->capacity) {
        fprintf(stderr, "TRam[%d]: out of space (%zu/%zu)\n", id, b->used, b->capacity);
        return NULL;
    }
    void *ptr = (char *)b->base + b->used;
    b->used += size;
    return ptr;
}

void *tram_write(int id, const void *data, size_t size) {
    void *dst = tram_alloc_in(id, size);
    if (!dst) return NULL;
    memcpy(dst, data, size);
    return dst;
}

void tram_clean(int id) {
    if (!g_tram_init || id < 0 || id >= g_tram.block_count) return;
    TRamBlock *b = &g_tram.blocks[id];
    if (b->base) { memset(b->base, 0, b->capacity); b->used = 0; }
}

void tram_clean_all(void) {
    for (int i = 0; i < g_tram.block_count; i++) tram_clean(i);
}

void tram_free(int id) {
    if (!g_tram_init || id < 0 || id >= g_tram.block_count) return;
    TRamBlock *b = &g_tram.blocks[id];
    if (b->base) { free(b->base); b->base = NULL; b->active = 0; }
}

size_t tram_used(int id) {
    if (!g_tram_init || id < 0 || id >= g_tram.block_count) return 0;
    return g_tram.blocks[id].used;
}

size_t tram_remaining(int id) {
    if (!g_tram_init || id < 0 || id >= g_tram.block_count) return 0;
    TRamBlock *b = &g_tram.blocks[id];
    return b->capacity - b->used;
}

/* -------------------------------------------------------------------
   CLL Registry
   ------------------------------------------------------------------- */
static CllRegistry g_cll;
static int         g_cll_init = 0;

CllRegistry *cll_registry(void) { return &g_cll; }

void cll_init(void) {
    if (g_cll_init) return;
    memset(&g_cll, 0, sizeof(g_cll));
    g_cll_init = 1;
}

void cll_shutdown(void) {
    if (!g_cll_init) return;
    for (int i = 0; i < g_cll.lib_count; i++) {
        if (g_cll.libs[i].loaded && g_cll.libs[i].handle) {
            DL_CLOSE(g_cll.libs[i].handle);
            g_cll.libs[i].handle = NULL;
        }
    }
    g_cll_init = 0;
}

int cll_load(const char *lib_path) {
    if (!g_cll_init) cll_init();
    if (g_cll.lib_count >= CLL_MAX_LIBS) return -1;

    void *h = DL_OPEN(lib_path);
    if (!h) {
        fprintf(stderr, "MBLL: cannot load '%s'\n", lib_path);
        return -1;
    }
    int id = g_cll.lib_count++;
    CllLib *lib = &g_cll.libs[id];
    strncpy(lib->path, lib_path, sizeof(lib->path) - 1);
    lib->handle = h;
    lib->loaded = 1;
    return id;
}

void *cll_resolve(const char *sym_name) {
    if (!g_cll_init) return NULL;
    for (int i = 0; i < g_cll.lib_count; i++) {
        if (!g_cll.libs[i].loaded) continue;
        void *sym = DL_SYM(g_cll.libs[i].handle, sym_name);
        if (sym) return sym;
    }
    return NULL;
}

void cll_dump(void) {
    printf("=== CLL Registry (%d libs) ===\n", g_cll.lib_count);
    for (int i = 0; i < g_cll.lib_count; i++) {
        printf("  [%d] %s (loaded=%d, syms=%d)\n", i,
               g_cll.libs[i].path, g_cll.libs[i].loaded, g_cll.libs[i].sym_count);
    }
}

/* -------------------------------------------------------------------
   CLL Compiler
   ------------------------------------------------------------------- */
int cll_compile_from_c(const char *src_path, const char *out_name) {
    char cmd[2048];
    char out_cll[512];
    snprintf(out_cll, sizeof(out_cll), "%s.cll", out_name);

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    snprintf(cmd, sizeof(cmd),
             "gcc -O2 -shared -fPIC -o \"%s\" \"%s\" 2>nul",
             out_cll, src_path);
#else
    snprintf(cmd, sizeof(cmd),
             "gcc -O2 -shared -fPIC -o \"%s\" \"%s\" 2>/dev/null",
             out_cll, src_path);
#endif
    int r = system(cmd);
    if (r == 0) printf("MBLL: compiled '%s' → '%s'\n", src_path, out_cll);
    else fprintf(stderr, "MBLL: compilation failed for '%s'\n", src_path);
    return r;
}

int cll_inspect_exports(const char *src_path, char **out_names, int max) {
    (void)src_path; (void)out_names; (void)max;
    return 0; /* stub — would parse nm output */
}

/* -------------------------------------------------------------------
   externFile
   ------------------------------------------------------------------- */
static ExternFileTable g_ext;
static int             g_ext_init = 0;

ExternFileTable *extern_file_table(void) { return &g_ext; }

void extern_file_init(void) {
    if (g_ext_init) return;
    memset(&g_ext, 0, sizeof(g_ext));
    g_ext_init = 1;
}

void extern_file_shutdown(void) {
    if (!g_ext_init) return;
    for (int i = 0; i < g_ext.count; i++) {
        if (g_ext.entries[i].handle)
            DL_CLOSE(g_ext.entries[i].handle);
    }
    g_ext_init = 0;
}

int extern_file_load(const char *filename) {
    if (!g_ext_init) extern_file_init();
    if (g_ext.count >= EXTERN_FILE_MAX) return -1;

    /* Compile to temp .so/.dll first */
    ExternFileEntry *e = &g_ext.entries[g_ext.count];
    strncpy(e->src_path, filename, sizeof(e->src_path) - 1);
    snprintf(e->out_path, sizeof(e->out_path), "%s" DL_EXT, filename);
    snprintf(e->name, sizeof(e->name), "%s", filename);

    char cmd[2048];
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    snprintf(cmd, sizeof(cmd),
             "gcc -O2 -shared -o \"%s\" \"%s\" 2>nul",
             e->out_path, filename);
#else
    snprintf(cmd, sizeof(cmd),
             "gcc -O2 -shared -fPIC -o \"%s\" \"%s\" 2>/dev/null",
             e->out_path, filename);
#endif
    int r = system(cmd);
    if (r != 0) { fprintf(stderr, "MBLL externFile: compile failed for '%s'\n", filename); return -1; }

    e->handle   = DL_OPEN(e->out_path);
    e->compiled = 1;
    if (!e->handle) {
        fprintf(stderr, "MBLL externFile: dlopen failed for '%s'\n", e->out_path);
        return -1;
    }
    return g_ext.count++;
}

void *extern_file_resolve(const char *filename, const char *sym_name) {
    if (!g_ext_init) return NULL;
    for (int i = 0; i < g_ext.count; i++) {
        if (strcmp(g_ext.entries[i].name, filename) == 0 && g_ext.entries[i].handle)
            return DL_SYM(g_ext.entries[i].handle, sym_name);
    }
    return NULL;
}

int extern_file_call(const char *filename, const char *func,
                     void **args, int argc, void *ret_out) {
    (void)args; (void)argc; (void)ret_out;
    void *fn = extern_file_resolve(filename, func);
    if (!fn) { fprintf(stderr, "MBLL: symbol '%s' not found in '%s'\n", func, filename); return -1; }
    /* Simple no-arg call; typed dispatch handled by codegen */
    typedef void (*VoidFn)(void);
    ((VoidFn)fn)();
    return 0;
}
