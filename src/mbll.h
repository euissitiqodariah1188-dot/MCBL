#ifndef MCBL_MBLL_H
#define MCBL_MBLL_H

/*
 * MBLL — McBL# Bridge Low Level  (v2.0)
 * =========================================
 * Bridge untuk connect ke bahasa C dan C++.
 * Juga mengatur sistem .cll library.
 *
 * Cara pakai:
 *   externFile(nama_file.c)     — link file C/C++ yang ada di folder sama
 *   include<file.cll>           — pakai library .cll
 *   WriteTRam(1000)             — pinjam 1000 MB RAM untuk tulis kode
 *   cleanTram()                 — bersihin TRam kalo udah penuh
 *
 * Compile file C/C++ jadi .cll:
 *   MSDK compile/nama_file.c -CLL nama_output
 */

#include <stddef.h>
#include <stdint.h>
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------
   TRam — Temporary RAM arena for complex projects
   (WriteTRam / cleanTram)
   ------------------------------------------------------------------- */
#define TRAM_MAX_BLOCKS   64
#define TRAM_MB           (1024ULL * 1024ULL)

typedef struct {
    void    *base;          /* mmap'd / malloc'd region */
    size_t   capacity;      /* bytes total              */
    size_t   used;          /* bytes consumed           */
    int      active;        /* 1 = allocated, 0 = free  */
    uint32_t id;
} TRamBlock;

typedef struct {
    TRamBlock blocks[TRAM_MAX_BLOCKS];
    int       block_count;
    size_t    total_mb;
} TRamManager;

/* Global TRam instance */
TRamManager *tram_manager(void);
void         tram_init(void);
void         tram_shutdown(void);

/* WriteTRam(mb) — borrow mb megabytes of RAM */
int          tram_alloc(size_t mb);   /* returns block id or -1 */
void        *tram_write(int id, const void *data, size_t size);
void        *tram_alloc_in(int id, size_t size);
void         tram_clean(int id);      /* cleanTram() for one block */
void         tram_clean_all(void);    /* clean all blocks */
void         tram_free(int id);       /* release the block */
size_t       tram_used(int id);
size_t       tram_remaining(int id);

/* -------------------------------------------------------------------
   .cll Library System
   ------------------------------------------------------------------- */
#define CLL_MAX_LIBS   128
#define CLL_MAX_SYMS   4096

typedef struct {
    char    *name;          /* symbol (function) name  */
    void    *ptr;           /* resolved function pointer */
    char    *sig;           /* C signature string       */
} CllSym;

typedef struct {
    char     path[512];     /* path to .cll file        */
    void    *handle;        /* dlopen / LoadLibrary handle */
    CllSym   syms[256];
    int      sym_count;
    char     name[128];     /* logical library name      */
    int      loaded;
} CllLib;

typedef struct {
    CllLib   libs[CLL_MAX_LIBS];
    int      lib_count;
    CllSym   all_syms[CLL_MAX_SYMS];
    int      sym_count;
} CllRegistry;

CllRegistry *cll_registry(void);
void         cll_init(void);
void         cll_shutdown(void);

/* include<file.cll> — load a .cll library */
int          cll_load(const char *lib_path);

/* Look up a symbol from loaded .cll libs */
void        *cll_resolve(const char *sym_name);

/* List all loaded libs and their symbols */
void         cll_dump(void);

/* -------------------------------------------------------------------
   externFile — link a raw .c or .cpp file at runtime
   (Compiles it to a temp .so/.dll and loads it)
   ------------------------------------------------------------------- */
typedef struct {
    char     src_path[512];   /* original .c/.cpp file path */
    char     out_path[512];   /* compiled .so/.dll path     */
    void    *handle;          /* dynamic library handle     */
    char     name[256];       /* logical name               */
    int      compiled;
} ExternFileEntry;

#define EXTERN_FILE_MAX 64

typedef struct {
    ExternFileEntry entries[EXTERN_FILE_MAX];
    int             count;
} ExternFileTable;

ExternFileTable *extern_file_table(void);
void             extern_file_init(void);
void             extern_file_shutdown(void);

/* externFile(filename.c) — compile + link at runtime */
int              extern_file_load(const char *filename);

/* Get a symbol from an externFile'd file */
void            *extern_file_resolve(const char *filename,
                                     const char *sym_name);

/* Call a C function from externFile by name with MVM values */
typedef struct {
    void    *fn_ptr;
    int      argc;
    char    *arg_types[16];   /* "int" "float" "string" etc. */
    char    *ret_type;
} CCallDesc;

int extern_file_call(const char *filename, const char *func,
                     void **args, int argc, void *ret_out);

/* -------------------------------------------------------------------
   CLL Compiler — MSDK compile/file.c -CLL output
   ------------------------------------------------------------------- */

/* Compile a .c or .cpp file into a .cll (shared library) */
int cll_compile_from_c(const char *src_path, const char *out_name);

/* Inspect what functions a .c file exports (pre-compile analysis) */
int cll_inspect_exports(const char *src_path, char **out_names, int max);

#ifdef __cplusplus
}
#endif

#endif /* MCBL_MBLL_H */
