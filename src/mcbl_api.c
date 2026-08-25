/*
 * mcbl_api.c — McBL# Script API Implementation
 * ===============================================
 * Embedded scripting engine + HTTP REST API server.
 * No GC — pure refcount memory.
 */
#include "mcbl_api.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "symbols.h"
#include "bytecode.h"
#include "codegen.h"
#include "mdk.h"
#include "memory.h"
#include "mcbl_str.h"
#include "mcbl_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>

#if defined(_WIN32) || defined(__MINGW32__)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #define SOCKET int
  #define INVALID_SOCKET (-1)
  #define closesocket close
#endif

/* ----------------------------------------------------------------
   Output capture buffer
   ---------------------------------------------------------------- */
#define CAPTURE_BUF_SZ (1024 * 1024)   /* 1 MB capture buffer */

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
    int     capturing;
} CaptureBuf;

static void cap_write(CaptureBuf *c, const char *s) {
    if (!c || !c->capturing || !s) return;
    size_t sl = strlen(s);
    if (c->len + sl + 1 >= c->cap) {
        c->cap = (c->len + sl + 1) * 2;
        c->buf = (char *)realloc(c->buf, c->cap);
        if (!c->buf) return;
    }
    memcpy(c->buf + c->len, s, sl);
    c->len += sl;
    c->buf[c->len] = '\0';
}

/* ----------------------------------------------------------------
   McblVM structure
   ---------------------------------------------------------------- */
struct McblVM {
    SymTable  *symbols;
    MdkVM     *mdk;
    BcChunk   *chunk;
    BytecodeGen *bcgen;

    /* Config */
    int       opt_level;
    int64_t   timeout_ms;
    size_t    mem_limit_mb;
    int64_t   max_instr;
    int       sandbox_file;
    int       sandbox_net;
    int       sandbox_sys;
    int       sandbox_proc;

    /* Output I/O hooks */
    void (*output_fn)(const char *line, void *ctx);
    void *output_ctx;
    char *(*input_fn)(const char *prompt, void *ctx);
    void *input_ctx;

    /* Capture */
    CaptureBuf cap;

    /* Error state */
    int   has_error;
    char  error_msg[1024];
    int   error_line;

    /* Native function bindings */
    struct { char name[64]; McblNativeFn fn; } bindings[256];
    int binding_count;
};

/* ----------------------------------------------------------------
   McblVM Lifecycle
   ---------------------------------------------------------------- */
McblVM *mcbl_api_new_opt(int opt_level) {
    McblVM *vm = (McblVM *)calloc(1, sizeof(McblVM));
    if (!vm) return NULL;

    vm->symbols  = symtable_create();
    vm->mdk      = mdk_vm_create(vm->symbols);
    vm->bcgen    = bcgen_create();
    vm->opt_level = opt_level;
    vm->timeout_ms  = 10000;   /* 10s default */
    vm->mem_limit_mb = 256;
    vm->max_instr   = 10000000;

    vm->cap.cap = 4096;
    vm->cap.buf = (char *)calloc(1, vm->cap.cap);

    return vm;
}

McblVM *mcbl_api_new(void) { return mcbl_api_new_opt(2); }

void mcbl_api_free(McblVM *vm) {
    if (!vm) return;
    if (vm->mdk)     { mdk_vm_destroy(vm->mdk);    vm->mdk    = NULL; }
    if (vm->bcgen)   { bcgen_destroy(vm->bcgen);   vm->bcgen  = NULL; }
    if (vm->chunk)   { bc_chunk_destroy(vm->chunk); vm->chunk = NULL; }
    if (vm->symbols) { symtable_destroy(vm->symbols); vm->symbols = NULL; }
    if (vm->cap.buf) { free(vm->cap.buf); vm->cap.buf = NULL; }
    free(vm);
}

void mcbl_api_reset(McblVM *vm) {
    if (!vm) return;
    if (vm->chunk)  { bc_chunk_destroy(vm->chunk); vm->chunk = NULL; }
    if (vm->mdk)    { mdk_vm_destroy(vm->mdk); vm->mdk = mdk_vm_create(vm->symbols); }
    vm->has_error = 0; vm->error_msg[0] = '\0'; vm->error_line = 0;
    if (vm->cap.buf) { memset(vm->cap.buf, 0, vm->cap.cap); vm->cap.len = 0; }
}

/* ----------------------------------------------------------------
   Variable I/O
   ---------------------------------------------------------------- */
void mcbl_api_set_int(McblVM *vm, const char *name, long long val) {
    if (!vm || !name) return;
    symtable_insert(vm->symbols, name, SYM_VAR_INT, '#');
    symtable_set_int(vm->symbols, name, val);
}
void mcbl_api_set_float(McblVM *vm, const char *name, double val) {
    if (!vm || !name) return;
    symtable_insert(vm->symbols, name, SYM_VAR_FLOAT, '#');
    symtable_set_float(vm->symbols, name, val);
}
void mcbl_api_set_bool(McblVM *vm, const char *name, int val) {
    if (!vm || !name) return;
    symtable_insert(vm->symbols, name, SYM_VAR_INT, '#');
    symtable_set_int(vm->symbols, name, val ? 1 : 0);
}
void mcbl_api_set_string(McblVM *vm, const char *name, const char *val) {
    if (!vm || !name) return;
    symtable_insert(vm->symbols, name, SYM_VAR_STRING, '#');
    symtable_set_string(vm->symbols, name, val);
}
void mcbl_api_set_null(McblVM *vm, const char *name) {
    if (!vm || !name) return;
    symtable_insert(vm->symbols, name, SYM_VAR_INT, '#');
    symtable_set_int(vm->symbols, name, 0);
}

long long mcbl_api_get_int(McblVM *vm, const char *name) {
    if (!vm || !name) return 0;
    Symbol *s = symtable_lookup(vm->symbols, name);
    return s ? s->val.ival : 0;
}
double mcbl_api_get_float(McblVM *vm, const char *name) {
    if (!vm || !name) return 0.0;
    Symbol *s = symtable_lookup(vm->symbols, name);
    return s ? s->val.fval : 0.0;
}
int mcbl_api_get_bool(McblVM *vm, const char *name) {
    return (int)mcbl_api_get_int(vm, name);
}
const char *mcbl_api_get_string(McblVM *vm, const char *name) {
    if (!vm || !name) return "";
    Symbol *s = symtable_lookup(vm->symbols, name);
    return (s && s->val.sval) ? s->val.sval : "";
}
int mcbl_api_has_var(McblVM *vm, const char *name) {
    if (!vm || !name) return 0;
    return symtable_lookup(vm->symbols, name) != NULL;
}

/* ----------------------------------------------------------------
   Native function binding
   ---------------------------------------------------------------- */
int mcbl_api_bind_fn(McblVM *vm, const char *name, McblNativeFn fn) {
    if (!vm || !name || !fn || vm->binding_count >= 256) return -1;
    int idx = vm->binding_count++;
    strncpy(vm->bindings[idx].name, name, 63);
    vm->bindings[idx].fn = fn;
    /* Register in symbol table as a function */
    symtable_insert(vm->symbols, name, SYM_FUNC, 0);
    return idx;
}

/* ----------------------------------------------------------------
   Output capture
   ---------------------------------------------------------------- */
void mcbl_api_set_output_fn(McblVM *vm, void (*fn)(const char *line, void *ctx), void *ctx) {
    if (!vm) return;
    vm->output_fn  = fn;
    vm->output_ctx = ctx;
}

const char *mcbl_api_capture_start(McblVM *vm) {
    if (!vm) return "";
    vm->cap.len        = 0;
    vm->cap.capturing  = 1;
    if (vm->cap.buf) vm->cap.buf[0] = '\0';
    return vm->cap.buf ? vm->cap.buf : "";
}

const char *mcbl_api_capture_stop(McblVM *vm) {
    if (!vm) return "";
    vm->cap.capturing = 0;
    return vm->cap.buf ? vm->cap.buf : "";
}

/* ----------------------------------------------------------------
   Sandboxing
   ---------------------------------------------------------------- */
void mcbl_api_sandbox_deny_file(McblVM *vm) { if (vm) vm->sandbox_file = 1; }
void mcbl_api_sandbox_deny_net (McblVM *vm) { if (vm) vm->sandbox_net  = 1; }
void mcbl_api_sandbox_deny_sys (McblVM *vm) { if (vm) vm->sandbox_sys  = 1; }
void mcbl_api_sandbox_deny_proc(McblVM *vm) { if (vm) vm->sandbox_proc = 1; }
void mcbl_api_set_timeout_ms   (McblVM *vm, int64_t ms)  { if (vm) vm->timeout_ms = ms; }
void mcbl_api_set_mem_limit_mb (McblVM *vm, size_t mb)   { if (vm) vm->mem_limit_mb = mb; }
void mcbl_api_set_max_instr    (McblVM *vm, int64_t n)   { if (vm) vm->max_instr = n; }

/* ----------------------------------------------------------------
   Error handling
   ---------------------------------------------------------------- */
int         mcbl_api_has_error  (McblVM *vm) { return vm ? vm->has_error : 1; }
const char *mcbl_api_error_msg  (McblVM *vm) { return vm ? vm->error_msg : "null vm"; }
int         mcbl_api_error_line (McblVM *vm) { return vm ? vm->error_line : 0; }
void        mcbl_api_clear_error(McblVM *vm) { if (vm) { vm->has_error=0; vm->error_msg[0]='\0'; } }

static void vm_set_error(McblVM *vm, int line, const char *fmt, ...) {
    if (!vm) return;
    vm->has_error  = 1;
    vm->error_line = line;
    va_list ap; va_start(ap, fmt);
    vsnprintf(vm->error_msg, sizeof(vm->error_msg), fmt, ap);
    va_end(ap);
}

/* ----------------------------------------------------------------
   Run source code
   ---------------------------------------------------------------- */
int mcbl_api_run_string(McblVM *vm, const char *source) {
    if (!vm || !source) { vm_set_error(vm, 0, "null vm or source"); return -1; }
    mcbl_api_clear_error(vm);

    /* Lex */
    Lexer *lex = lexer_create(source);
    if (!lex) { vm_set_error(vm, 0, "lexer OOM"); return -1; }
    if (lexer_tokenize(lex) != 0) {
        vm_set_error(vm, 0, "lexer error");
        lexer_destroy(lex); return -1;
    }

    /* Parse */
    Parser  *psr = parser_create(lex->tokens, lex->token_count);
    AstNode *ast = psr ? parser_parse(psr) : NULL;
    if (psr) { /* parser_destroy(psr); */ }
    lexer_destroy(lex);
    if (!ast) { vm_set_error(vm, 0, "parse error"); return -1; }

    /* Compile to bytecode */
    if (!vm->bcgen) vm->bcgen = bcgen_create();
    if (vm->chunk)  { bc_chunk_destroy(vm->chunk); vm->chunk = NULL; }
    bcgen_set_opt(vm->bcgen, vm->opt_level);
    if (bcgen_compile(vm->bcgen, ast) != 0) {
        vm_set_error(vm, 0, "compile error: %s", vm->bcgen->errmsg);
        ast_node_free(ast); return -1;
    }
    vm->chunk = vm->bcgen->chunk;
    ast_node_free(ast);

    /* Execute */
    if (!vm->mdk) vm->mdk = mdk_vm_create(vm->symbols);
    int r = mdk_vm_exec(vm->mdk, vm->chunk);
    if (r != 0 || vm->mdk->error) {
        vm_set_error(vm, 0, "runtime error: %s", vm->mdk->errmsg);
        return -1;
    }
    return 0;
}

int mcbl_api_run_file(McblVM *vm, const char *path) {
    if (!vm || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) { vm_set_error(vm, 0, "cannot open '%s'", path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f); rewind(f);
    char *src = (char *)malloc(sz + 1);
    if (!src) { fclose(f); return -1; }
    fread(src, 1, sz, f); src[sz] = '\0'; fclose(f);
    int r = mcbl_api_run_string(vm, src);
    free(src);
    return r;
}

int mcbl_api_run_bytes(McblVM *vm, const uint8_t *src, size_t len) {
    if (!vm || !src) return -1;
    char *s = (char *)malloc(len + 1);
    if (!s) return -1;
    memcpy(s, src, len); s[len] = '\0';
    int r = mcbl_api_run_string(vm, s);
    free(s);
    return r;
}

McblApiVal mcbl_api_eval(McblVM *vm, const char *expr) {
    McblApiVal v = {.type = MCBL_VAL_NULL};
    if (!vm || !expr) return v;
    /* Wrap expr in inc block for context */
    char code[4096];
    snprintf(code, sizeof(code),
             "inc(__eval__);\n__eval_result__ = %s\nendinc;\n", expr);
    if (mcbl_api_run_string(vm, code) == 0) {
        Symbol *s = symtable_lookup(vm->symbols, "__eval_result__");
        if (s) {
            switch (s->kind) {
                case SYM_VAR_INT:    v.type = MCBL_VAL_INT;    v.ival = s->val.ival; break;
                case SYM_VAR_FLOAT:  v.type = MCBL_VAL_FLOAT;  v.fval = s->val.fval; break;
                case SYM_VAR_STRING: v.type = MCBL_VAL_STRING;
                                     v.sval = s->val.sval ? strdup(s->val.sval) : NULL; break;
                default: break;
            }
        }
    } else {
        v.type = MCBL_VAL_ERROR;
        v.sval = strdup(mcbl_api_error_msg(vm));
    }
    return v;
}

/* ----------------------------------------------------------------
   Value helpers
   ---------------------------------------------------------------- */
McblApiVal mcbl_val_int   (long long v)      { return (McblApiVal){.type=MCBL_VAL_INT,   .ival=v}; }
McblApiVal mcbl_val_float (double v)         { return (McblApiVal){.type=MCBL_VAL_FLOAT, .fval=v}; }
McblApiVal mcbl_val_bool  (int v)            { return (McblApiVal){.type=MCBL_VAL_BOOL,  .bval=v}; }
McblApiVal mcbl_val_null  (void)             { return (McblApiVal){.type=MCBL_VAL_NULL}; }
McblApiVal mcbl_val_string(const char *s) {
    return (McblApiVal){.type=MCBL_VAL_STRING, .sval=s?strdup(s):NULL};
}
void mcbl_val_free(McblApiVal *v) {
    if (v && (v->type==MCBL_VAL_STRING||v->type==MCBL_VAL_ERROR) && v->sval)
        { free(v->sval); v->sval=NULL; }
}
char *mcbl_val_to_str(McblApiVal v) {
    char buf[64];
    switch (v.type) {
        case MCBL_VAL_INT:    snprintf(buf,sizeof(buf),"%lld",(long long)v.ival); return strdup(buf);
        case MCBL_VAL_FLOAT:  snprintf(buf,sizeof(buf),"%g",v.fval); return strdup(buf);
        case MCBL_VAL_BOOL:   return strdup(v.bval?"true":"false");
        case MCBL_VAL_STRING: return v.sval ? strdup(v.sval) : strdup("");
        case MCBL_VAL_NULL:   return strdup("null");
        case MCBL_VAL_ERROR:  return v.sval ? strdup(v.sval) : strdup("error");
        default:              return strdup("?");
    }
}

/* ----------------------------------------------------------------
   JSON helpers for API responses
   ---------------------------------------------------------------- */
char *mcbl_api_json_escape(const char *s) {
    if (!s) return strdup("\"\"");
    size_t n = strlen(s);
    char *out = (char *)malloc(n * 6 + 3);
    if (!out) return strdup("\"\"");
    size_t j = 0;
    out[j++] = '"';
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '"')       { out[j++]='\\'; out[j++]='"';  }
        else if (ch == '\\') { out[j++]='\\'; out[j++]='\\'; }
        else if (ch == '\n') { out[j++]='\\'; out[j++]='n';  }
        else if (ch == '\r') { out[j++]='\\'; out[j++]='r';  }
        else if (ch == '\t') { out[j++]='\\'; out[j++]='t';  }
        else if (ch < 0x20)  { j += (size_t)sprintf(out+j, "\\u%04x", ch); }
        else                 { out[j++] = (char)ch; }
    }
    out[j++] = '"';
    out[j]   = '\0';
    return out;
}

char *mcbl_api_json_ok(const char *result_json) {
    char *out = (char *)malloc(strlen(result_json) + 32);
    sprintf(out, "{\"ok\":true,\"result\":%s}", result_json ? result_json : "null");
    return out;
}

char *mcbl_api_json_err(const char *msg, int code) {
    char *esc = mcbl_api_json_escape(msg ? msg : "unknown error");
    char *out = (char *)malloc(strlen(esc) + 64);
    sprintf(out, "{\"ok\":false,\"error\":%s,\"code\":%d}", esc, code);
    free(esc);
    return out;
}

char *mcbl_api_json_val(McblApiVal v) {
    char buf[256];
    switch (v.type) {
        case MCBL_VAL_INT:    snprintf(buf,sizeof(buf),"%lld",(long long)v.ival); return strdup(buf);
        case MCBL_VAL_FLOAT:  snprintf(buf,sizeof(buf),"%g",v.fval); return strdup(buf);
        case MCBL_VAL_BOOL:   return strdup(v.bval?"true":"false");
        case MCBL_VAL_NULL:   return strdup("null");
        case MCBL_VAL_STRING: return mcbl_api_json_escape(v.sval ? v.sval : "");
        case MCBL_VAL_ERROR: {
            char *esc = mcbl_api_json_escape(v.sval ? v.sval : "error");
            char *out = (char *)malloc(strlen(esc)+32);
            sprintf(out,"{\"error\":%s}",esc); free(esc); return out;
        }
        default: return strdup("null");
    }
}

/* forward decl for base64 helper */
static char *api_b64_enc(const void *data, size_t len);

/* ================================================================
   HTTP REST API Server
   ================================================================ */

/* Simple HTTP request/response structures */
typedef struct {
    char method[16];
    char path[256];
    char body[1024*1024];   /* 1MB body buffer */
    size_t body_len;
    char auth[256];
} HttpReq;

typedef struct {
    int   status;
    char  content_type[64];
    char *body;             /* malloc'd */
    int   cors;
} HttpResp;

struct McblApiServer {
    McblApiServerConfig cfg;
    SOCKET              listen_fd;
    volatile int        running;
    pthread_t           accept_thread;
    McblVMPool         *vm_pool;
    int64_t             start_time;
    McblApiServerStats  stats;
    pthread_mutex_t     stats_lock;

    /* Custom routes */
    struct {
        char method[16];
        char path[256];
        McblApiHandler handler;
    } routes[64];
    int route_count;
};

/* VM Pool */
struct McblVMPool {
    McblVM     **vms;
    int          size;
    volatile int *in_use;
    pthread_mutex_t lock;
};

McblVMPool *mcbl_api_pool_new(int size) {
    McblVMPool *p = (McblVMPool *)calloc(1, sizeof(McblVMPool));
    if (!p) return NULL;
    p->vms    = (McblVM **)calloc(size, sizeof(McblVM *));
    p->in_use = (volatile int *)calloc(size, sizeof(int));
    p->size   = size;
    pthread_mutex_init(&p->lock, NULL);
    for (int i = 0; i < size; i++) p->vms[i] = mcbl_api_new();
    return p;
}

void mcbl_api_pool_free(McblVMPool *p) {
    if (!p) return;
    for (int i = 0; i < p->size; i++) mcbl_api_free(p->vms[i]);
    free((void *)p->in_use); free(p->vms);
    pthread_mutex_destroy(&p->lock);
    free(p);
}

McblVM *mcbl_api_pool_get(McblVMPool *p) {
    if (!p) return NULL;
    pthread_mutex_lock(&p->lock);
    for (int i = 0; i < p->size; i++) {
        if (!p->in_use[i]) { p->in_use[i] = 1; pthread_mutex_unlock(&p->lock); return p->vms[i]; }
    }
    pthread_mutex_unlock(&p->lock);
    return mcbl_api_new(); /* create overflow VM */
}

void mcbl_api_pool_put(McblVMPool *p, McblVM *vm) {
    if (!p || !vm) return;
    pthread_mutex_lock(&p->lock);
    for (int i = 0; i < p->size; i++) {
        if (p->vms[i] == vm) { mcbl_api_reset(vm); p->in_use[i] = 0; pthread_mutex_unlock(&p->lock); return; }
    }
    pthread_mutex_unlock(&p->lock);
    mcbl_api_free(vm); /* overflow VM — free it */
}

/* Parse simple JSON field */
static char *json_get_field(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        if (!end) return NULL;
        char *out = (char *)malloc(end - p + 1);
        memcpy(out, p, end - p);
        out[end - p] = '\0';
        return out;
    }
    /* number or other */
    const char *end = p;
    while (*end && *end != ',' && *end != '}' && *end != '\n') end++;
    char *out = (char *)malloc(end - p + 1);
    memcpy(out, p, end - p);
    out[end - p] = '\0';
    return out;
}

/* Build HTTP response string */
static char *http_response(int status, const char *ctype, const char *body, int cors) {
    static const char *status_text[] = {
        [200]="OK", [201]="Created", [400]="Bad Request",
        [401]="Unauthorized", [404]="Not Found",
        [405]="Method Not Allowed", [500]="Internal Server Error"
    };
    const char *st = (status >= 200 && status <= 500 && status_text[status])
                     ? status_text[status] : "Unknown";
    size_t bl   = body ? strlen(body) : 0;
    size_t total = bl + 512;
    char *resp  = (char *)malloc(total);
    int n = snprintf(resp, total,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n"
        "%s",
        status, st,
        ctype ? ctype : "application/json",
        bl,
        cors ? "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, GET, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Authorization\r\n" : "",
        body ? body : "");
    (void)n;
    return resp;
}

/* Handle one HTTP connection */
static void handle_connection(McblApiServer *srv, SOCKET client_fd) {
    char req_buf[2 * 1024 * 1024]; /* 2MB buffer */
    int  n = (int)recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
    if (n <= 0) { closesocket(client_fd); return; }
    req_buf[n] = '\0';

    /* Parse request line */
    char method[16]={0}, path[256]={0};
    sscanf(req_buf, "%15s %255s", method, path);

    /* Auth check */
    if (srv->cfg.auth_token && srv->cfg.auth_token[0]) {
        if (!strstr(req_buf, srv->cfg.auth_token)) {
            char *resp = http_response(401, "application/json",
                                       "{\"ok\":false,\"error\":\"Unauthorized\"}",
                                       srv->cfg.enable_cors);
            send(client_fd, resp, strlen(resp), 0);
            free(resp); closesocket(client_fd); return;
        }
    }

    /* Find body */
    const char *body = strstr(req_buf, "\r\n\r\n");
    body = body ? body + 4 : "";

    if (srv->cfg.log_requests)
        printf("[MCBL API] %s %s\n", method, path);

    pthread_mutex_lock(&srv->stats_lock);
    srv->stats.requests_total++;
    pthread_mutex_unlock(&srv->stats_lock);

    /* CORS preflight */
    if (strcmp(method, "OPTIONS") == 0) {
        char *resp = http_response(200, "application/json", "", srv->cfg.enable_cors);
        send(client_fd, resp, strlen(resp), 0);
        free(resp); closesocket(client_fd); return;
    }

    char *response_body = NULL;
    int   status = 200;

    /* ---- GET /health ---- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        response_body = strdup("{\"ok\":true,\"status\":\"healthy\",\"version\":\"2.0\"}");

    /* ---- GET /version ---- */
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/version") == 0) {
        response_body = strdup(
            "{\"ok\":true,\"version\":\"2.0\",\"language\":\"McBL#\","
            "\"features\":[\"OOP\",\"JIT\",\"MVM\",\"Math\",\"String\",\"File\",\"Net\","
            "\"Async\",\"NoGC\",\"MBLL\",\"CompilerX64\"]}");

    /* ---- GET /docs ---- */
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/docs") == 0) {
        response_body = strdup(
            "{\"ok\":true,\"endpoints\":{"
            "\"/run\":{\"method\":\"POST\",\"body\":{\"code\":\"string\",\"vars\":\"object\"}},"
            "\"/eval\":{\"method\":\"POST\",\"body\":{\"expr\":\"string\"}},"
            "\"/compile\":{\"method\":\"POST\",\"body\":{\"code\":\"string\",\"opt\":\"int 0-3\"}},"
            "\"/bench\":{\"method\":\"POST\",\"body\":{\"code\":\"string\",\"runs\":\"int\"}},"
            "\"/health\":{\"method\":\"GET\"},"
            "\"/version\":{\"method\":\"GET\"},"
            "\"/stats\":{\"method\":\"GET\"}"
            "}}");

    /* ---- GET /stats ---- */
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/stats") == 0) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"requests_total\":%lld,\"requests_ok\":%lld,"
                 "\"requests_err\":%lld,\"uptime_s\":%lld}",
                 (long long)srv->stats.requests_total,
                 (long long)srv->stats.requests_ok,
                 (long long)srv->stats.requests_err,
                 (long long)(time(NULL) - srv->start_time));
        response_body = strdup(buf);

    /* ---- POST /run ---- */
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/run") == 0) {
        char *code = json_get_field(body, "code");
        if (!code) {
            response_body = mcbl_api_json_err("missing 'code' field", 400);
            status = 400;
        } else {
            McblVM *vm = mcbl_api_pool_get(srv->vm_pool);
            if (srv->cfg.sandbox) {
                mcbl_api_sandbox_deny_file(vm);
                mcbl_api_sandbox_deny_net(vm);
                mcbl_api_sandbox_deny_sys(vm);
                mcbl_api_sandbox_deny_proc(vm);
            }
            mcbl_api_set_timeout_ms(vm, srv->cfg.timeout_ms);

            mcbl_api_capture_start(vm);
            int r = mcbl_api_run_string(vm, code);
            const char *output = mcbl_api_capture_stop(vm);

            if (r == 0) {
                char *out_esc = mcbl_api_json_escape(output);
                char *res = (char *)malloc(strlen(out_esc) + 64);
                sprintf(res, "{\"ok\":true,\"output\":%s}", out_esc);
                free(out_esc);
                response_body = res;
            } else {
                response_body = mcbl_api_json_err(mcbl_api_error_msg(vm), 500);
                status = 500;
            }
            mcbl_api_pool_put(srv->vm_pool, vm);
            free(code);
        }

    /* ---- POST /eval ---- */
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/eval") == 0) {
        char *expr = json_get_field(body, "expr");
        if (!expr) {
            response_body = mcbl_api_json_err("missing 'expr' field", 400);
            status = 400;
        } else {
            McblVM *vm = mcbl_api_pool_get(srv->vm_pool);
            McblApiVal v = mcbl_api_eval(vm, expr);
            char *vj = mcbl_api_json_val(v);
            response_body = mcbl_api_json_ok(vj);
            free(vj); mcbl_val_free(&v);
            mcbl_api_pool_put(srv->vm_pool, vm);
            free(expr);
        }

    /* ---- POST /compile ---- */
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/compile") == 0) {
        char *code = json_get_field(body, "code");
        char *opt_s = json_get_field(body, "opt");
        if (!code) {
            response_body = mcbl_api_json_err("missing 'code'", 400); status = 400;
        } else {
            int opt = opt_s ? atoi(opt_s) : 2;
            McblVM *vm = mcbl_api_new_opt(opt);
            size_t bc_len = 0;
            uint8_t *bc = mcbl_api_compile(vm, code, &bc_len);
            if (!bc) {
                response_body = mcbl_api_json_err(mcbl_api_error_msg(vm), 500);
                status = 500;
            } else {
                char *b64 = api_b64_enc(bc, bc_len);
                char *res = (char *)malloc(strlen(b64) + 128);
                sprintf(res, "{\"ok\":true,\"bytecode\":\"%s\",\"size\":%zu}", b64, bc_len);
                free(b64); free(bc);
                response_body = res;
            }
            mcbl_api_free(vm); free(code); if (opt_s) free(opt_s);
        }

    /* ---- POST /bench ---- */
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/bench") == 0) {
        char *code = json_get_field(body, "code");
        char *runs_s = json_get_field(body, "runs");
        if (!code) {
            response_body = mcbl_api_json_err("missing 'code'", 400); status = 400;
        } else {
            int runs = runs_s ? atoi(runs_s) : 100;
            McblVM *vm = mcbl_api_pool_get(srv->vm_pool);
            int64_t t0 = (int64_t)(clock());
            for (int i = 0; i < runs; i++) {
                mcbl_api_reset(vm);
                mcbl_api_run_string(vm, code);
            }
            int64_t t1 = (int64_t)(clock());
            double ms = (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0;
            char res[256];
            snprintf(res, sizeof(res),
                     "{\"ok\":true,\"runs\":%d,\"total_ms\":%.3f,\"avg_ms\":%.6f}",
                     runs, ms, ms / runs);
            response_body = strdup(res);
            mcbl_api_pool_put(srv->vm_pool, vm);
            free(code); if (runs_s) free(runs_s);
        }

    /* ---- Custom routes ---- */
    } else {
        int found = 0;
        for (int i = 0; i < srv->route_count; i++) {
            if (strcmp(srv->routes[i].method, method) == 0 &&
                strcmp(srv->routes[i].path,   path)   == 0) {
                response_body = srv->routes[i].handler(srv, body, strlen(body));
                found = 1; break;
            }
        }
        if (!found) {
            response_body = mcbl_api_json_err("not found", 404);
            status = 404;
        }
    }

    /* Update stats */
    pthread_mutex_lock(&srv->stats_lock);
    if (status == 200) srv->stats.requests_ok++;
    else               srv->stats.requests_err++;
    pthread_mutex_unlock(&srv->stats_lock);

    char *resp = http_response(status,
                               "application/json; charset=utf-8",
                               response_body,
                               srv->cfg.enable_cors);
    send(client_fd, resp, strlen(resp), 0);
    free(resp);
    if (response_body) free(response_body);
    closesocket(client_fd);
}

/* Accept thread */
static void *accept_loop(void *arg) {
    McblApiServer *srv = (McblApiServer *)arg;
    while (srv->running) {
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);
        SOCKET client = accept(srv->listen_fd, (struct sockaddr *)&client_addr, &clen);
        if (client == INVALID_SOCKET) {
            if (!srv->running) break;
            continue;
        }
        handle_connection(srv, client);
    }
    return NULL;
}

McblApiServerConfig mcbl_api_server_default_config(void) {
    McblApiServerConfig cfg = {0};
    cfg.port         = 8080;
    cfg.workers      = 4;
    cfg.max_req_kb   = 1024;
    cfg.timeout_ms   = 5000;
    cfg.vm_pool_size = 8;
    cfg.enable_cors  = 1;
    cfg.sandbox      = 0;
    cfg.auth_token   = NULL;
    cfg.bind_addr    = "0.0.0.0";
    cfg.opt_level    = 2;
    cfg.log_requests = 1;
    return cfg;
}

McblApiServer *mcbl_api_server_new(McblApiServerConfig cfg) {
    McblApiServer *srv = (McblApiServer *)calloc(1, sizeof(McblApiServer));
    if (!srv) return NULL;
    srv->cfg        = cfg;
    srv->vm_pool    = mcbl_api_pool_new(cfg.vm_pool_size);
    srv->start_time = (int64_t)time(NULL);
    pthread_mutex_init(&srv->stats_lock, NULL);

#if defined(_WIN32) || defined(__MINGW32__)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd == INVALID_SOCKET) { free(srv); return NULL; }

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)cfg.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[MCBL API] Cannot bind port %d\n", cfg.port);
        closesocket(srv->listen_fd); free(srv); return NULL;
    }
    listen(srv->listen_fd, 128);
    printf("[MCBL API] Server ready on http://%s:%d\n", cfg.bind_addr, cfg.port);
    printf("[MCBL API] Endpoints: /run /eval /compile /bench /health /version /stats /docs\n");
    return srv;
}

int mcbl_api_server_run(McblApiServer *srv) {
    if (!srv) return -1;
    srv->running = 1;
    accept_loop(srv);   /* blocking */
    return 0;
}

int mcbl_api_server_start(McblApiServer *srv) {
    if (!srv) return -1;
    srv->running = 1;
    pthread_create(&srv->accept_thread, NULL, accept_loop, srv);
    return 0;
}

void mcbl_api_server_stop(McblApiServer *srv) {
    if (!srv) return;
    srv->running = 0;
    closesocket(srv->listen_fd);
}

void mcbl_api_server_free(McblApiServer *srv) {
    if (!srv) return;
    mcbl_api_server_stop(srv);
    if (srv->vm_pool) mcbl_api_pool_free(srv->vm_pool);
    pthread_mutex_destroy(&srv->stats_lock);
    free(srv);
}

void mcbl_api_server_add_route(McblApiServer *srv, const char *method,
                                const char *path, McblApiHandler handler) {
    if (!srv || srv->route_count >= 64) return;
    int i = srv->route_count++;
    strncpy(srv->routes[i].method,  method, 15);
    strncpy(srv->routes[i].path,    path, 255);
    srv->routes[i].handler = handler;
}

McblApiServerStats mcbl_api_server_stats(McblApiServer *srv) {
    if (!srv) return (McblApiServerStats){0};
    pthread_mutex_lock(&srv->stats_lock);
    McblApiServerStats s = srv->stats;
    pthread_mutex_unlock(&srv->stats_lock);
    s.uptime_s = (int64_t)(time(NULL) - srv->start_time);
    return s;
}

/* Compile to bytecode stub */
uint8_t *mcbl_api_compile(McblVM *vm, const char *source, size_t *out_len) {
    if (!vm || !source) { if (out_len) *out_len = 0; return NULL; }
    mcbl_api_clear_error(vm);
    Lexer *lex = lexer_create(source);
    if (!lex) return NULL;
    lexer_tokenize(lex);
    Parser  *psr2 = parser_create(lex->tokens, lex->token_count);
    AstNode *ast = psr2 ? parser_parse(psr2) : NULL;
    lexer_destroy(lex);
    if (!ast) return NULL;
    if (!vm->bcgen) vm->bcgen = bcgen_create();
    bcgen_compile(vm->bcgen, ast);
    ast_node_free(ast);
    if (!vm->bcgen->chunk) return NULL;
    /* Serialize chunk to bytes — simplified */
    BcChunk *c = vm->bcgen->chunk;
    size_t sz = c->count * sizeof(BcInstr);
    uint8_t *out = (uint8_t *)malloc(sz);
    if (!out) return NULL;
    memcpy(out, c->instrs, sz);
    if (out_len) *out_len = sz;
    return out;
}

/* Python/JS binding stubs */
const char *mcbl_py_run(const char *code, const char *vars_json) {
    static char result[1024 * 1024];
    McblVM *vm = mcbl_api_new();
    if (!vm) return "{\"error\":\"OOM\"}";
    /* TODO: parse vars_json and set variables */
    mcbl_api_capture_start(vm);
    int r = mcbl_api_run_string(vm, code);
    const char *out = mcbl_api_capture_stop(vm);
    if (r == 0) {
        snprintf(result, sizeof(result), "{\"ok\":true,\"output\":\"%s\"}", out);
    } else {
        snprintf(result, sizeof(result), "{\"ok\":false,\"error\":\"%s\"}", mcbl_api_error_msg(vm));
    }
    mcbl_api_free(vm);
    return result;
}
const char *mcbl_js_run (const char *code, const char *vars_json) { return mcbl_py_run(code, vars_json); }
const char *mcbl_lua_run(const char *code, const char *vars_json) { return mcbl_py_run(code, vars_json); }

/* crypto base64 stub (needed by /compile endpoint) */
static char *api_b64_enc(const void *data, size_t len) {
    static const char b64t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = ((len+2)/3)*4;
    char *out = (char *)malloc(out_len+1);
    if (!out) return NULL;
    const unsigned char *in = (const unsigned char *)data;
    size_t i=0, j=0;
    while (i < len) {
        uint32_t o0=i<len?in[i++]:0, o1=i<len?in[i++]:0, o2=i<len?in[i++]:0;
        uint32_t t=(o0<<16)|(o1<<8)|o2;
        out[j++]=b64t[(t>>18)&63]; out[j++]=b64t[(t>>12)&63];
        out[j++]=b64t[(t>>6)&63];  out[j++]=b64t[t&63];
    }
    for (size_t p=0;p<(3-len%3)%3;p++) out[out_len-1-p]='=';
    out[out_len]='\0';
    return out;
}
