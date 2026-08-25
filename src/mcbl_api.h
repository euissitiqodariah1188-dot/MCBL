#ifndef MCBL_API_H
#define MCBL_API_H

/*
 * McBL# Script API  v2.0
 * ========================
 * Embed McBL# sebagai scripting engine di dalam program C/C++.
 * Juga provide HTTP REST API server biar script bisa dijalankan
 * via HTTP request (kayak curl, fetch, requests).
 *
 * Cara pakai embedded:
 *
 *   McblVM *vm = mcbl_api_new();
 *   mcbl_api_set_global_int(vm, "x", 42);
 *   mcbl_api_run_string(vm, "inc(main); pr(x * 2) endinc;");
 *   long long r = mcbl_api_get_global_int(vm, "result");
 *   mcbl_api_free(vm);
 *
 * Cara pakai REST API:
 *
 *   McblApiServer *srv = mcbl_api_server_new(8080);
 *   mcbl_api_server_run(srv);   // blocking
 *   // Endpoints:
 *   //   POST /run      body: { "code": "...", "vars": {...} }
 *   //   POST /compile  body: { "code": "...", "opt": 2 }
 *   //   GET  /health
 *   //   GET  /version
 *   //   POST /eval     body: { "expr": "2 + 2" }  → { "result": 4 }
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
   McblVM — embedded scripting context
   ---------------------------------------------------------------- */
typedef struct McblVM McblVM;

typedef enum {
    MCBL_VAL_NULL = 0,
    MCBL_VAL_INT,
    MCBL_VAL_FLOAT,
    MCBL_VAL_BOOL,
    MCBL_VAL_STRING,
    MCBL_VAL_ARRAY,
    MCBL_VAL_MAP,
    MCBL_VAL_ERROR
} McblApiType;

typedef struct {
    McblApiType type;
    union {
        long long   ival;
        double      fval;
        int         bval;
        char       *sval;   /* heap-owned */
        void       *pval;   /* array/map handle */
    };
} McblApiVal;

/* ---- Lifecycle ---- */
McblVM  *mcbl_api_new     (void);           /* create VM instance */
McblVM  *mcbl_api_new_opt (int opt_level);  /* 0=none, 1=basic, 2=full, 3=aggressive */
void     mcbl_api_free    (McblVM *vm);     /* destroy + free all */
void     mcbl_api_reset   (McblVM *vm);     /* reset state, keep config */

/* ---- Run source code ---- */
int      mcbl_api_run_string (McblVM *vm, const char *source);
int      mcbl_api_run_file   (McblVM *vm, const char *path);
int      mcbl_api_run_bytes  (McblVM *vm, const uint8_t *src, size_t len);

/* ---- Eval single expression → value ---- */
McblApiVal mcbl_api_eval   (McblVM *vm, const char *expr);

/* ---- Compile only (no run) — returns bytecode ---- */
uint8_t  *mcbl_api_compile  (McblVM *vm, const char *source, size_t *out_len);
int       mcbl_api_run_bc   (McblVM *vm, const uint8_t *bc, size_t len);

/* ---- Global variable I/O ---- */
void      mcbl_api_set_int   (McblVM *vm, const char *name, long long val);
void      mcbl_api_set_float (McblVM *vm, const char *name, double val);
void      mcbl_api_set_bool  (McblVM *vm, const char *name, int val);
void      mcbl_api_set_string(McblVM *vm, const char *name, const char *val);
void      mcbl_api_set_null  (McblVM *vm, const char *name);
void      mcbl_api_set_val   (McblVM *vm, const char *name, McblApiVal val);

long long   mcbl_api_get_int   (McblVM *vm, const char *name);
double      mcbl_api_get_float (McblVM *vm, const char *name);
int         mcbl_api_get_bool  (McblVM *vm, const char *name);
const char *mcbl_api_get_string(McblVM *vm, const char *name);
McblApiVal  mcbl_api_get_val   (McblVM *vm, const char *name);
int         mcbl_api_has_var   (McblVM *vm, const char *name);

/* ---- Native function binding ---- */
/* Register C function callable from McBL# as: dev funcname(args) */
typedef McblApiVal (*McblNativeFn)(McblVM *vm, McblApiVal *args, int argc);

int  mcbl_api_bind_fn   (McblVM *vm, const char *name, McblNativeFn fn);
int  mcbl_api_bind_const(McblVM *vm, const char *name, McblApiVal val);

/* ---- Output capture ---- */
void  mcbl_api_set_output_fn (McblVM *vm, void (*fn)(const char *line, void *ctx), void *ctx);
void  mcbl_api_set_input_fn  (McblVM *vm, char *(*fn)(const char *prompt, void *ctx), void *ctx);

/* Capture pr() output to buffer */
const char *mcbl_api_capture_start(McblVM *vm);  /* returns buffer ptr */
const char *mcbl_api_capture_stop (McblVM *vm);  /* returns full output */

/* ---- Error handling ---- */
int         mcbl_api_has_error  (McblVM *vm);
const char *mcbl_api_error_msg  (McblVM *vm);
int         mcbl_api_error_line (McblVM *vm);
void        mcbl_api_clear_error(McblVM *vm);

/* ---- Sandboxing ---- */
void  mcbl_api_sandbox_deny_file  (McblVM *vm);  /* block file.*  */
void  mcbl_api_sandbox_deny_net   (McblVM *vm);  /* block net.*   */
void  mcbl_api_sandbox_deny_sys   (McblVM *vm);  /* block sys.*   */
void  mcbl_api_sandbox_deny_proc  (McblVM *vm);  /* block proc.*  */
void  mcbl_api_set_timeout_ms     (McblVM *vm, int64_t ms);  /* execution timeout */
void  mcbl_api_set_mem_limit_mb   (McblVM *vm, size_t mb);   /* memory limit */
void  mcbl_api_set_max_instr      (McblVM *vm, int64_t n);   /* instruction limit */

/* ---- Multi-VM pool ---- */
typedef struct McblVMPool McblVMPool;
McblVMPool *mcbl_api_pool_new  (int size);      /* create N VMs */
void        mcbl_api_pool_free (McblVMPool *p);
McblVM     *mcbl_api_pool_get  (McblVMPool *p); /* borrow VM */
void        mcbl_api_pool_put  (McblVMPool *p, McblVM *vm); /* return VM */

/* ---- Script API value helpers ---- */
McblApiVal mcbl_val_int   (long long v);
McblApiVal mcbl_val_float (double v);
McblApiVal mcbl_val_bool  (int v);
McblApiVal mcbl_val_string(const char *s);
McblApiVal mcbl_val_null  (void);
void       mcbl_val_free  (McblApiVal *v);
char      *mcbl_val_to_str(McblApiVal v);    /* malloc'd string representation */

/* ================================================================
   McBL# REST API Server
   ================================================================
   HTTP server yang serve McBL# endpoints.
   Bisa dipakai sebagai:
     - Serverless function runner
     - Script execution API
     - Live REPL over HTTP

   Endpoints:
     POST /run      → run McBL# code
     POST /eval     → eval expression → JSON result
     POST /compile  → compile to bytecode → base64
     GET  /health   → { "status": "ok", "version": "2.0" }
     GET  /version  → full version info JSON
     GET  /docs     → API documentation
     POST /bench    → benchmark mode (cx64 style)
     WS   /repl     → WebSocket REPL session
   ================================================================ */

typedef struct McblApiServer McblApiServer;

typedef struct {
    int         port;          /* default 8080 */
    int         workers;       /* thread pool size, default 4 */
    int         max_req_kb;    /* max request body, default 1024 KB */
    int         timeout_ms;    /* per-request timeout, default 5000 */
    int         vm_pool_size;  /* VM pool size, default 8 */
    int         enable_cors;   /* CORS headers, default 1 */
    int         sandbox;       /* 1 = deny file/net/sys/proc by default */
    const char *auth_token;    /* NULL = no auth, else Bearer token */
    const char *bind_addr;     /* default "0.0.0.0" */
    int         opt_level;     /* compiler opt level, default 2 */
    int         log_requests;  /* print each request, default 1 */
} McblApiServerConfig;

McblApiServerConfig mcbl_api_server_default_config(void);

McblApiServer *mcbl_api_server_new    (McblApiServerConfig cfg);
int            mcbl_api_server_run    (McblApiServer *srv);   /* blocking */
int            mcbl_api_server_start  (McblApiServer *srv);   /* background thread */
void           mcbl_api_server_stop   (McblApiServer *srv);
void           mcbl_api_server_free   (McblApiServer *srv);

/* Register custom endpoint: POST /custom/<name> */
typedef char *(*McblApiHandler)(McblApiServer *srv, const char *body, size_t body_len);
void mcbl_api_server_add_route(McblApiServer *srv, const char *method,
                                const char *path, McblApiHandler handler);

/* Stats */
typedef struct {
    int64_t requests_total;
    int64_t requests_ok;
    int64_t requests_err;
    int64_t avg_latency_ms;
    int64_t uptime_s;
} McblApiServerStats;

McblApiServerStats mcbl_api_server_stats(McblApiServer *srv);

/* ================================================================
   JSON API helpers — build JSON responses
   ================================================================ */
char *mcbl_api_json_ok    (const char *result_json);
char *mcbl_api_json_err   (const char *msg, int code);
char *mcbl_api_json_val   (McblApiVal v);
char *mcbl_api_json_escape(const char *s);   /* escape for JSON string */

/* ================================================================
   C embedding convenience macros
   ================================================================ */
#define MCBL_RUN(vm, code)          mcbl_api_run_string((vm), (code))
#define MCBL_SET_INT(vm, name, val) mcbl_api_set_int((vm),(name),(val))
#define MCBL_SET_STR(vm, name, val) mcbl_api_set_string((vm),(name),(val))
#define MCBL_GET_INT(vm, name)      mcbl_api_get_int((vm),(name))
#define MCBL_GET_STR(vm, name)      mcbl_api_get_string((vm),(name))
#define MCBL_EVAL_INT(vm, expr)     mcbl_api_eval((vm),(expr)).ival
#define MCBL_EVAL_STR(vm, expr)     mcbl_api_eval((vm),(expr)).sval

/* ================================================================
   Python/JS binding helpers (for extension modules)
   ================================================================ */
/* These functions allow McBL# to be called from Python via ctypes
   or from Node.js via ffi-napi.
   
   Python example:
     import ctypes
     lib = ctypes.CDLL("./libmcbl.so")
     lib.mcbl_py_run.restype = ctypes.c_char_p
     result = lib.mcbl_py_run(b"inc(x); #y = 2 + 2 endinc;", b"")
     
   JavaScript example:
     const ffi = require('ffi-napi')
     const lib = ffi.Library('./libmcbl', {
       mcbl_js_run: ['string', ['string', 'string']]
     })
*/
const char *mcbl_py_run   (const char *code, const char *vars_json);
const char *mcbl_js_run   (const char *code, const char *vars_json);
const char *mcbl_lua_run  (const char *code, const char *vars_json);

#ifdef __cplusplus
}
#endif

#endif /* MCBL_API_H */
