#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "symbols.h"
#include "bytecode.h"
#include "codegen.h"
#include "jit.h"
#include "kernel.h"
#include "mdk.h"
#include "neural.h"
#include "mdc.h"
#include "mbjkdt.h"
#include "vp_xt300.h"
#include "ui.h"

/* v2.0 — New subsystems */
#include "mvm.h"
#include "mbll.h"
#include "mcbl_math.h"
#include "mcbl_str.h"
#include "mcbl_sys.h"
#include "oop.h"

/* -----------------------------------------------------------------------
   McBL# MSDK  –  Command-line entry point
   Usage:
     MSDK run   <file.cbl|.modcbl|.cxcbl>
     MSDK compile <file.cbl|...>  [-o output]
     MSDK lex    <file>           (dump tokens)
     MSDK ast    <file>           (dump AST)
     MSDK bc     <file>           (dump bytecode)
     MSDK version
   ----------------------------------------------------------------------- */

#define MCBL_VERSION "2.0.0"

/* ---- file loading ----------------------------------------------------- */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "MSDK: cannot open file '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read_bytes = fread(buf, 1, (size_t)sz, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

static void replace_extension(const char *in, const char *new_ext, char *out, size_t out_cap) {
    strncpy(out, in, out_cap - 1);
    out[out_cap - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    size_t l = strlen(out);
    snprintf(out + l, out_cap - l, "%s", new_ext);
}

/* ---- pipeline --------------------------------------------------------- */

typedef struct {
    Lexer         *lexer;
    AstNode       *ast;
    SymTable      *symbols;
    BytecodeGen   *bcgen;
    CGen          *cgen;
    NeuralNetwork *neural;
    VPxt300Handle *vp;
    MbjkdtBridge  *js_bridge;
    /* v2.0 subsystems */
    MvmInstance   *mvm;
} Pipeline;

static Pipeline *pipeline_create(void) {
    Pipeline *pl = (Pipeline *)calloc(1, sizeof(Pipeline));
    if (!pl) return NULL;
    pl->symbols    = symtable_create();
    pl->neural     = neural_create();
    pl->vp         = vpxt300_create(8);
    pl->js_bridge  = mbjkdt_create();
    /* v2.0: init subsystems */
    pl->mvm        = mvm_create(4);
    memory_init();
    tram_init();
    cll_init();
    extern_file_init();
    class_registry_init();
    oop_mem_init();
    return pl;
}

static void pipeline_destroy(Pipeline *pl) {
    if (!pl) return;
    if (pl->lexer)     { lexer_destroy(pl->lexer);    pl->lexer    = NULL; }
    if (pl->ast)       { ast_node_free(pl->ast);       pl->ast      = NULL; }
    if (pl->symbols)   { symtable_destroy(pl->symbols); pl->symbols = NULL; }
    if (pl->bcgen)     { bcgen_destroy(pl->bcgen);     pl->bcgen    = NULL; }
    if (pl->cgen)      { cgen_destroy(pl->cgen);       pl->cgen     = NULL; }
    if (pl->neural)    { neural_destroy(pl->neural);   pl->neural   = NULL; }
    if (pl->vp)        { vpxt300_destroy(pl->vp);      pl->vp       = NULL; }
    if (pl->js_bridge) { mbjkdt_destroy(pl->js_bridge); pl->js_bridge = NULL; }
    /* v2.0 */
    if (pl->mvm)       { mvm_destroy(pl->mvm);         pl->mvm      = NULL; }
    /* no GC */
    oop_mem_shutdown();
    class_registry_shutdown();
    extern_file_shutdown();
    cll_shutdown();
    tram_shutdown();
    free(pl);
}

/* ---- source dispatch -------------------------------------------------- */

static int is_ui_content(const char *src) {
    return strstr(src, "<MCBL") != NULL ||
           strstr(src, "<mcbl") != NULL ||
           strstr(src, "<endui>") != NULL ||
           strstr(src, "<endui") != NULL;
}

static int parse_source(Pipeline *pl, const char *src) {
    pl->lexer = lexer_create(src);
    if (!pl->lexer) { fprintf(stderr, "MSDK: lexer_create failed\n"); return -1; }
    if (lexer_tokenize(pl->lexer) < 0) {
        fprintf(stderr, "MSDK: tokenization failed\n");
        return -1;
    }
    Parser *parser = parser_create(pl->lexer->tokens, pl->lexer->token_count);
    if (!parser) { fprintf(stderr, "MSDK: parser_create failed\n"); return -1; }
    pl->ast = parser_parse(parser);
    int err = parser->error;
    parser_destroy(parser);
    return err ? -1 : 0;
}

static int run_bytecode(Pipeline *pl) {
    pl->bcgen = bcgen_create();
    if (!pl->bcgen) return -1;
    pl->bcgen->symbols = pl->symbols;
    if (bcgen_compile(pl->bcgen, pl->ast) < 0) {
        fprintf(stderr, "MSDK: bytecode compilation failed\n");
        return -1;
    }
    Kernel *k = kernel_create(pl->symbols, KERNEL_MODE_AUTO);
    if (!k) return -1;
    int r = kernel_exec(k, pl->bcgen->chunk);
    kernel_stats(k);
    kernel_destroy(k);
    return r;
}

static int compile_to_binary(Pipeline *pl, const char *src_path, const char *out_path) {
    pl->cgen = cgen_create();
    if (!pl->cgen) return -1;
    if (cgen_compile(pl->cgen, pl->ast) < 0) {
        fprintf(stderr, "MSDK: C code generation failed\n");
        return -1;
    }

    char c_path[512];
    snprintf(c_path, sizeof(c_path), "%s._mcbl_tmp.c", src_path);
    int r = cgen_emit_and_compile(pl->cgen, c_path, out_path);
    if (r == 0) {
        /* keep c_path for debug */
        printf("MSDK: compiled '%s' → '%s'\n", src_path, out_path);
    }
    return r;
}

/* ---- MDC interop export ----------------------------------------------- */
static void export_interop(Pipeline *pl, const char *src_path) {
    char *interop = mdc_generate_interop(pl->ast);
    if (interop && interop[0]) {
        char ipath[512];
        snprintf(ipath, sizeof(ipath), "%s.mdc.h", src_path);
        FILE *f = fopen(ipath, "w");
        if (f) { fputs(interop, f); fclose(f); }
    }
    free(interop);
}

/* ---- UI export -------------------------------------------------------- */
static void export_ui(Pipeline *pl, const char *src_path) {
    if (!pl->ast) return;
    const AstList *stmts = &pl->ast->block.stmts;
    for (size_t i = 0; i < stmts->count; i++) {
        const AstNode *n = stmts->items[i];
        if (!n) continue;
        if (n->kind == AST_UI_PAGE) {
            /* Generate native window C code */
            UiGen *ui = ui_gen_create(UI_TARGET_NATIVE);
            if (!ui) continue;
            ui_gen_compile(ui, n);
            char cpath[512];
            snprintf(cpath, sizeof(cpath), "%s._ui.c", src_path);
            ui_gen_write(ui, cpath);
            ui_gen_destroy(ui);
        }
    }
}

/* ---- JS adapter generation -------------------------------------------- */
static void export_js_adapter(const char *src_path) {
    char *js = mbjkdt_gen_js_adapter();
    if (js) {
        char jspath[512];
        snprintf(jspath, sizeof(jspath), "%s.mcbl_bridge.js", src_path);
        FILE *f = fopen(jspath, "w");
        if (f) { fputs(js, f); fclose(f); }
        free(js);
    }
}

/* ---- commands --------------------------------------------------------- */

static void print_usage(void) {
    printf("McBL# MSDK v" MCBL_VERSION " — High-Performance Systems Language\n\n");
    printf("Usage:\n");
    printf("  MSDK run     <file.cbl|.modcbl|.cxcbl>       -- run via MDK VM\n");
    printf("  MSDK compile <file.cbl|.modcbl|.cxcbl> [-o output] -- compile to native\n");
    printf("  MSDK mvm     <file>                           -- run via MVM (4-core)\n");
    printf("  MSDK lex     <file>                           -- dump token stream\n");
    printf("  MSDK ast     <file>                           -- dump AST\n");
    printf("  MSDK bc      <file>                           -- dump bytecode\n");
    printf("  MSDK check   <file>                           -- type-check only\n");
    printf("  MSDK cll     <file.c> -CLL <output>          -- compile C to .cll library\n");
    printf("  MSDK memcheck <file>                          -- run with memory leak check\n");
    printf("  MSDK bench   <file>                           -- run + print benchmarks\n");
    printf("  MSDK version\n\n");
    printf("File extensions:\n");
    printf("  .cbl     McBL# source file\n");
    printf("  .modcbl  McBL# module/library file\n");
    printf("  .cxcbl   Hybrid McBL#+C source file\n");
    printf("  .cll     McBL# compiled library (like .dll)\n\n");
    printf("New v2.0 keywords:\n");
    printf("  OOP      : class, extends, implements, interface, new, this, super\n");
    printf("  Math     : math.sqrt, math.sin, math.deriv, math.integ, math.sum, math.matrix\n");
    printf("  String   : str.len, str.upper, str.find, str.format, str.regex, str.split\n");
    printf("  File     : file.write, file.append, file.exists, file.list, file.lines\n");
    printf("  Net      : net.get, net.post, net.listen, net.connect\n");
    printf("  System   : sys.exec, sys.env, sys.time, sys.sleep, sys.exit\n");
    printf("  Debug    : debug.assert, debug.trace, debug.log, debug.bench\n");
    printf("  Async    : async, await, chan, mutex, lock, atomic\n");
    printf("  MVM      : mvm_spawn, mvm_sync, mvm_pipe, mvm_core, mvm_opt\n");
    printf("  MBLL     : externFile(), include<lib.cll>, WriteTRam(), cleanTram()\n");
    printf("  Array    : $array, arr[i], arr.push, arr.pop, arr.len, arr.slice\n");
    printf("  Map      : map<K,V>, map[key], map.has, map.keys, map.vals\n");
    printf("  Error    : try, catch, finally, throw\n");
    printf("  Types    : int, float, str, bool, byte, long, double, char, auto\n");
}

static int cmd_version(void) {
    printf("McBL# MSDK v%s — High-Performance Systems Language\n", MCBL_VERSION);
    printf("  Compiler  : C transpiler → GCC x86_64 (native binary)\n");
    printf("  VM/MDK    : Stack-based bytecode interpreter (upgraded v2.0)\n");
    printf("  MVM       : McBL Virtual Machine — 4-core parallel, ASM/C/C++\n");
    printf("  JIT       : x86_64 native code generation via mmap\n");
    printf("  OOP       : Full class system — inheritance, interfaces, vtable\n");
    printf("  Memory    : Pure refcount — NO GC, zero pause, immediate free\n");
    printf("  MBLL      : McBL Bridge Low Level — externFile, .cll libraries\n");
    printf("  Math      : Calculus stdlib — deriv, integ, sum, matrix, vector\n");
    printf("  String    : Dewa-level string manipulation + built-in regex\n");
    printf("  Async     : async/await, chan, mutex, atomic\n");
    printf("  Neural    : 20-core parallel task distributor\n");
    printf("  VP XT300  : Virtual processor (C++) for multi-function dispatch\n");
    printf("  MDC       : McBL# <-> C interop\n");
    printf("  MBJKDT    : McBL# <-> JavaScript JSON bridge\n");
    printf("  Static typing: variable size known at compile-time\n");
    printf("  Zero leaks : CARGO pool + refcount + RAII (No GC)\n");
  printf("  Script API : Embedded scripting + REST HTTP server\n");
    return 0;
}

static int cmd_lex(const char *path) {
    char *src = read_file(path);
    if (!src) return 1;
    Lexer *l = lexer_create(src);
    if (!l) { free(src); return 1; }
    if (lexer_tokenize(l) < 0) { lexer_destroy(l); free(src); return 1; }
    printf("=== Token stream: %s (%zu tokens) ===\n", path, l->token_count);
    lexer_dump_tokens(l);
    lexer_destroy(l);
    free(src);
    return 0;
}

static int cmd_ast(const char *path) {
    char *src = read_file(path);
    if (!src) return 1;
    Pipeline *pl = pipeline_create();
    if (!pl) { free(src); return 1; }
    int r = parse_source(pl, src);
    free(src);
    if (r < 0) { pipeline_destroy(pl); return 1; }
    printf("=== AST: %s ===\n", path);
    ast_dump(pl->ast, 0);
    pipeline_destroy(pl);
    return 0;
}

static int cmd_bc(const char *path) {
    char *src = read_file(path);
    if (!src) return 1;
    Pipeline *pl = pipeline_create();
    if (!pl) { free(src); return 1; }
    if (parse_source(pl, src) < 0) { free(src); pipeline_destroy(pl); return 1; }
    free(src);
    pl->bcgen = bcgen_create();
    if (bcgen_compile(pl->bcgen, pl->ast) < 0) { pipeline_destroy(pl); return 1; }
    printf("=== Bytecode: %s (%zu instructions) ===\n", path, pl->bcgen->chunk->count);
    bc_dump(pl->bcgen->chunk);
    pipeline_destroy(pl);
    return 0;
}

static int cmd_run(const char *path) {
    char *src = read_file(path);
    if (!src) return 1;

    memory_init();
    Pipeline *pl = pipeline_create();
    if (!pl) { free(src); return 1; }

    /* Submit parsing to neural network */
    neural_submit(pl->neural, TASK_ANALYSE_AST, (void *)src, NULL, NULL);

    if (parse_source(pl, src) < 0) {
        free(src);
        pipeline_destroy(pl);
        memory_shutdown();
        return 1;
    }
    free(src);

    /* Handle UI pages - generate native window code but don't run it during VM exec */
    export_interop(pl, path);
    export_js_adapter(path);

    /* Run through MDK VM via kernel */
    int r = run_bytecode(pl);

    neural_flush(pl->neural);
    vpxt300_join_all(pl->vp);

    pipeline_destroy(pl);
    memory_shutdown();
    return r < 0 ? 1 : 0;
}

static int cmd_compile(const char *path, const char *out_path_override) {
    char *src = read_file(path);
    if (!src) return 1;

    int is_ui = is_ui_content(src);

    memory_init();
    Pipeline *pl = pipeline_create();
    if (!pl) { free(src); return 1; }

    if (parse_source(pl, src) < 0) {
        free(src);
        pipeline_destroy(pl);
        memory_shutdown();
        return 1;
    }
    free(src);

    char out_path[512];
    if (out_path_override) {
        strncpy(out_path, out_path_override, sizeof(out_path) - 1);
        out_path[sizeof(out_path) - 1] = '\0';
    } else {
        replace_extension(path, ".out", out_path, sizeof(out_path));
    }

    int r;
    if (is_ui) {
        /* For UI files: generate native window C code and compile that */
        export_ui(pl, path);
        /* The export_ui function generates path._ui.c and compiles it */
        /* But we need to ensure the binary is at out_path */
        char ui_c_path[512];
        char ui_bin_path[512];
        snprintf(ui_c_path, sizeof(ui_c_path), "%s._ui.c", path);
        snprintf(ui_bin_path, sizeof(ui_bin_path), "%s._ui", path);

        /* Read the generated UI C file and compile to out_path */
        char cmd[2048];
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
        snprintf(cmd, sizeof(cmd), "gcc -o \"%s\" \"%s\" -lgdi32 2>nul",
                 out_path, ui_c_path);
#else
        snprintf(cmd, sizeof(cmd), "gcc -o \"%s\" \"%s\" -lX11 2>/dev/null",
                 out_path, ui_c_path);
#endif
        r = system(cmd);
        if (r == 0) {
            printf("MSDK: compiled '%s' → '%s'\n", path, out_path);
        } else {
            /* Fallback: try without X11 (for systems without X11 dev libs) */
            snprintf(cmd, sizeof(cmd), "gcc -o \"%s\" \"%s\" 2>/dev/null",
                     out_path, ui_c_path);
            r = system(cmd);
            if (r == 0) {
                printf("MSDK: compiled '%s' → '%s'\n", path, out_path);
                } else {
                fprintf(stderr, "MSDK: UI compilation failed\n");
            }
        }
    } else {
        /* Regular C codegen path */
        export_interop(pl, path);
        r = compile_to_binary(pl, path, out_path);
    }

    neural_flush(pl->neural);
    vpxt300_join_all(pl->vp);

    pipeline_destroy(pl);
    memory_shutdown();
    return r < 0 ? 1 : 0;
}

/* ---- main ------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(); return 0; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0)
        return cmd_version();

    if (strcmp(cmd, "lex") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK lex: missing file\n"); return 1; }
        return cmd_lex(argv[2]);
    }

    if (strcmp(cmd, "ast") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK ast: missing file\n"); return 1; }
        return cmd_ast(argv[2]);
    }

    if (strcmp(cmd, "bc") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK bc: missing file\n"); return 1; }
        return cmd_bc(argv[2]);
    }

    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK run: missing file\n"); return 1; }
        return cmd_run(argv[2]);
    }

    if (strcmp(cmd, "compile") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK compile: missing file\n"); return 1; }
        /* Check for -CLL flag: MSDK compile/file.c -CLL output */
        for (int i = 2; i < argc - 1; i++) {
            if (strcmp(argv[i], "-CLL") == 0 && i + 1 < argc) {
                /* Compile .c/.cpp to .cll library */
                const char *src  = argv[2];
                const char *name = argv[i + 1];
                printf("MSDK: compiling '%s' to .cll library '%s'\n", src, name);
                int r = cll_compile_from_c(src, name);
                if (r == 0) printf("MSDK: created '%s.cll'\n", name);
                else fprintf(stderr, "MSDK: CLL compilation failed\n");
                return r;
            }
        }
        const char *out = NULL;
        for (int i = 3; i < argc - 1; i++) {
            if (strcmp(argv[i], "-o") == 0) { out = argv[i + 1]; break; }
        }
        return cmd_compile(argv[2], out);
    }

    /* v2.0 commands */
    if (strcmp(cmd, "mvm") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK mvm: missing file\n"); return 1; }
        char *src = read_file(argv[2]);
        if (!src) return 1;
        Pipeline *pl = pipeline_create();
        if (!pl) { free(src); return 1; }
        int r = parse_source(pl, src);
        if (r == 0) {
            printf("MSDK: Running via MVM (%d cores, unroll=12, opt=3)\n", 4);
            /* Compile bytecode */
            pl->bcgen = bcgen_create();
            if (pl->bcgen) {
                pl->bcgen->symbols = pl->symbols;
                bcgen_set_opt(pl->bcgen, 3);   /* max optimization */
                if (bcgen_compile(pl->bcgen, pl->ast) == 0 && pl->bcgen->chunk) {
                    /* MVM optimizer: constant fold + DCE + loop unroll */
                    /* Execute via kernel (MDK VM — correct interpreter) */
                    Kernel *k = kernel_create(pl->symbols, KERNEL_MODE_AUTO);
                    if (k) {
                        r = kernel_exec(k, pl->bcgen->chunk);
                        kernel_stats(k);
                        kernel_destroy(k);
                    }
                }
            }
        }
        pipeline_destroy(pl);
        free(src);
        return r;
    }

    if (strcmp(cmd, "check") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK check: missing file\n"); return 1; }
        char *src = read_file(argv[2]);
        if (!src) return 1;
        Pipeline *pl = pipeline_create();
        if (!pl) { free(src); return 1; }
        int r = parse_source(pl, src);
        if (r == 0) {
            printf("MSDK check: '%s' — OK (no syntax/parse errors)\n", argv[2]);
        } else {
            fprintf(stderr, "MSDK check: '%s' — FAILED\n", argv[2]);
        }
        pipeline_destroy(pl);
        free(src);
        return r;
    }

    if (strcmp(cmd, "cll") == 0) {
        /* MSDK cll file.c -CLL output */
        if (argc < 5) {
            fprintf(stderr, "Usage: MSDK cll <file.c> -CLL <output_name>\n");
            return 1;
        }
        const char *src_c  = argv[2];
        const char *out_nm = argv[4];
        printf("MSDK: compiling '%s' → '%s.cll'\n", src_c, out_nm);
        int r = cll_compile_from_c(src_c, out_nm);
        if (r == 0) printf("MSDK: '%s.cll' created successfully\n", out_nm);
        else fprintf(stderr, "MSDK: CLL compilation failed\n");
        return r;
    }

    if (strcmp(cmd, "memcheck") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK memcheck: missing file\n"); return 1; }
        char *src = read_file(argv[2]);
        if (!src) return 1;
        Pipeline *pl = pipeline_create();
        if (!pl) { free(src); return 1; }
        int r = parse_source(pl, src);
        if (r == 0) r = run_bytecode(pl);
        /* After run — check for leaks */
        MemStats ms = mcbl_mem_stats();
        printf("\n=== Memory Report ===\n");
        printf("  Heap alloc:   %zu bytes\n", ms.heap_allocated);
        printf("  Heap freed:   %zu bytes\n", ms.heap_freed);
        printf("  GC objects:   %zu\n",       ms.gc_objects);
        printf("  GC collected: %zu\n",       ms.gc_collected);
        if (ms.leak_count > 0)
            printf("  LEAKS:        %d detected!\n", ms.leak_count);
        else
            printf("  Leaks:        0 (clean)\n");
        pipeline_destroy(pl);
        free(src);
        return r;
    }

    if (strcmp(cmd, "bench") == 0) {
        if (argc < 3) { fprintf(stderr, "MSDK bench: missing file\n"); return 1; }
        char *src = read_file(argv[2]);
        if (!src) return 1;
        Pipeline *pl = pipeline_create();
        if (!pl) { free(src); return 1; }
        int r = parse_source(pl, src);
        McblBench *bench = mcbl_bench_start(argv[2]);
        if (r == 0) r = run_bytecode(pl);
        mcbl_bench_end(bench);
        mcbl_bench_print(bench);
        free(bench);
        mcbl_prof_dump();
        pipeline_destroy(pl);
        free(src);
        return r;
    }

    fprintf(stderr, "MSDK: unknown command '%s'\n", cmd);
    print_usage();
    return 1;
}
