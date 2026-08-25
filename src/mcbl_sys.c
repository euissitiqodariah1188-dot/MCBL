/* McBL# System Standard Library — v2.0 */
#include "mcbl_sys.h"
#include "mcbl_str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  #include <windows.h>
  #include <process.h>
  #define POPEN  _popen
  #define PCLOSE _pclose
  #define SLEEP_MS(ms) Sleep(ms)
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <dirent.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #define POPEN  popen
  #define PCLOSE pclose
  #define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

/* ---- Profiler (simple) ---- */
#define MAX_PROF_ENTRIES 256
static McblProfEntry g_prof[MAX_PROF_ENTRIES];
static int g_prof_count = 0;

/* ---- File ops ---- */
McblStr *mcbl_file_read(const char *path) {
    if (!path) return mcbl_str_empty();
    FILE *f = fopen(path, "rb");
    if (!f) return mcbl_str_empty();
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = (char *)malloc(size + 1);
    if (!buf) { fclose(f); return mcbl_str_empty(); }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    McblStr *r = mcbl_str_new(buf);
    free(buf);
    return r;
}
int mcbl_file_write(const char *path, const McblStr *content) {
    if (!path || !content) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(content->buf, 1, content->len, f);
    fclose(f);
    return 0;
}
int mcbl_file_append(const char *path, const McblStr *content) {
    if (!path || !content) return -1;
    FILE *f = fopen(path, "ab");
    if (!f) return -1;
    fwrite(content->buf, 1, content->len, f);
    fclose(f);
    return 0;
}
int mcbl_file_delete(const char *path) {
    if (!path) return -1;
    return remove(path);
}
int mcbl_file_exists(const char *path) {
    if (!path) return 0;
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}
int64_t mcbl_file_size(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    int64_t sz = (int64_t)ftell(f);
    fclose(f);
    return sz;
}
McblStr **mcbl_file_list(const char *dir_path, size_t *out_count) {
    if (!dir_path || !out_count) { if (out_count) *out_count = 0; return NULL; }
    *out_count = 0;
    McblStr **arr = (McblStr **)calloc(1024, sizeof(McblStr *));
    if (!arr) return NULL;
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
    DIR *d = opendir(dir_path);
    if (!d) { free(arr); return NULL; }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *out_count < 1024) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        arr[(*out_count)++] = mcbl_str_new(ent->d_name);
    }
    closedir(d);
#else
    /* Windows glob */
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            arr[(*out_count)++] = mcbl_str_new(fd.cFileName);
        } while (FindNextFileA(h, &fd) && *out_count < 1024);
        FindClose(h);
    }
#endif
    return arr;
}
int mcbl_file_mkdir(const char *path) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    return CreateDirectoryA(path, NULL) ? 0 : -1;
#else
    return mkdir(path, 0755);
#endif
}
int mcbl_file_copy(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    return 0;
}
int mcbl_file_move(const char *src, const char *dst) { return rename(src, dst); }
McblStr **mcbl_file_lines(const char *path, size_t *out_count) {
    if (!path || !out_count) { if (out_count) *out_count = 0; return NULL; }
    McblStr *content = mcbl_file_read(path);
    McblStr *newline  = mcbl_str_new("\n");
    McblStr **lines   = mcbl_str_split(content, newline, out_count);
    mcbl_str_free(content);
    mcbl_str_free(newline);
    return lines;
}

/* ---- Network ---- */
HttpResponse *mcbl_net_get(const char *url) {
    if (!url) return NULL;
    HttpResponse *r = (HttpResponse *)calloc(1, sizeof(HttpResponse));
    if (!r) return NULL;
    /* Use curl if available; otherwise stub */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "curl -sS \"%s\" 2>/dev/null", url);
    FILE *f = POPEN(cmd, "r");
    if (!f) {
        r->status_code = -1;
        r->body = mcbl_str_empty();
        r->headers = mcbl_str_empty();
        return r;
    }
    McblStrBuf *sb = mcbl_sb_new();
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) mcbl_sb_append_c(sb, buf);
    PCLOSE(f);
    r->status_code = 200;
    r->body    = mcbl_sb_build(sb);
    r->headers = mcbl_str_empty();
    mcbl_sb_free(sb);
    return r;
}
HttpResponse *mcbl_net_post(const char *url, const McblStr *body, const char *content_type) {
    if (!url) return NULL;
    HttpResponse *r = (HttpResponse *)calloc(1, sizeof(HttpResponse));
    if (!r) return NULL;
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "curl -sS -X POST -H \"Content-Type: %s\" -d \"%s\" \"%s\" 2>/dev/null",
             content_type ? content_type : "application/json",
             body ? body->buf : "",
             url);
    FILE *f = POPEN(cmd, "r");
    if (!f) {
        r->status_code = -1;
        r->body = mcbl_str_empty();
        r->headers = mcbl_str_empty();
        return r;
    }
    McblStrBuf *sb = mcbl_sb_new();
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) mcbl_sb_append_c(sb, buf);
    PCLOSE(f);
    r->status_code = 200;
    r->body    = mcbl_sb_build(sb);
    r->headers = mcbl_str_empty();
    mcbl_sb_free(sb);
    return r;
}
void mcbl_http_free(HttpResponse *r) {
    if (!r) return;
    mcbl_str_free(r->body);
    mcbl_str_free(r->headers);
    free(r);
}

/* TCP socket — minimal POSIX/Winsock wrapper */
struct McblSocket {
    int fd;
    char host[256];
    int  port;
};
McblSocket *mcbl_net_listen(int port) {
    McblSocket *s = (McblSocket *)calloc(1, sizeof(McblSocket));
    if (!s) return NULL;
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
    s->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->fd < 0) { free(s); return NULL; }
    int opt = 1;
    setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->fd); free(s); return NULL;
    }
    listen(s->fd, 5);
#endif
    s->port = port;
    return s;
}
McblSocket *mcbl_net_connect(const char *host, int port) {
    McblSocket *s = (McblSocket *)calloc(1, sizeof(McblSocket));
    if (!s) return NULL;
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
    struct hostent *he = gethostbyname(host);
    if (!he) { free(s); return NULL; }
    s->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->fd < 0) { free(s); return NULL; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    if (connect(s->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->fd); free(s); return NULL;
    }
#endif
    strncpy(s->host, host, sizeof(s->host) - 1);
    s->port = port;
    return s;
}
int mcbl_net_send(McblSocket *s, const McblStr *data) {
    if (!s || !data) return -1;
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
    return (int)send(s->fd, data->buf, data->len, 0);
#else
    return -1;
#endif
}
McblStr *mcbl_net_recv(McblSocket *s, size_t max_bytes) {
    if (!s) return mcbl_str_empty();
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
    char *buf = (char *)malloc(max_bytes + 1);
    if (!buf) return mcbl_str_empty();
    ssize_t n = recv(s->fd, buf, max_bytes, 0);
    if (n < 0) { free(buf); return mcbl_str_empty(); }
    buf[n] = '\0';
    McblStr *r = mcbl_str_new(buf);
    free(buf);
    return r;
#else
    return mcbl_str_empty();
#endif
}
void mcbl_net_close(McblSocket *s) {
    if (!s) return;
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
    close(s->fd);
#endif
    free(s);
}

/* ---- System ops ---- */
McblStr *mcbl_sys_exec(const char *cmd) {
    if (!cmd) return mcbl_str_empty();
    FILE *f = POPEN(cmd, "r");
    if (!f) return mcbl_str_empty();
    McblStrBuf *sb = mcbl_sb_new();
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) mcbl_sb_append_c(sb, buf);
    PCLOSE(f);
    McblStr *r = mcbl_sb_build(sb);
    mcbl_sb_free(sb);
    return r;
}
McblStr *mcbl_sys_env(const char *var) {
    const char *val = getenv(var);
    return mcbl_str_new(val ? val : "");
}
int64_t mcbl_sys_time(void) {
    struct timespec ts;
#if defined(_WIN32)
    /* Fallback */
    return (int64_t)time(NULL) * 1000;
#else
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}
void mcbl_sys_sleep(double seconds) {
    SLEEP_MS((unsigned int)(seconds * 1000));
}
void mcbl_sys_exit(int code) { exit(code); }
McblStr **mcbl_sys_args(int *out_count) {
    /* Args captured at startup — stub returns empty */
    if (out_count) *out_count = 0;
    return NULL;
}
int64_t mcbl_sys_pid(void) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}
McblStr *mcbl_sys_mem(void) {
    char buf[256];
    snprintf(buf, sizeof(buf), "pid=%lld", (long long)mcbl_sys_pid());
    return mcbl_str_new(buf);
}
McblStr *mcbl_sys_cpu(void) { return mcbl_str_new("x86_64"); }

/* ---- Debug ---- */
void mcbl_debug_assert(int cond, const char *msg, const char *file, int line) {
    if (!cond) {
        fprintf(stderr, "[ASSERT FAIL] %s at %s:%d\n", msg ? msg : "(no message)", file, line);
        abort();
    }
}
void mcbl_debug_trace(const char *msg, const char *file, int line) {
    fprintf(stderr, "[TRACE] %s:%d — %s\n", file, line, msg ? msg : "");
}
void mcbl_debug_log(const char *level, const char *msg) {
    fprintf(stderr, "[%s] %s\n", level ? level : "LOG", msg ? msg : "");
}
void mcbl_debug_watch(const char *name, const void *val, const char *type) {
    (void)val; (void)type;
    fprintf(stderr, "[WATCH] %s\n", name ? name : "?");
}
void mcbl_debug_break(void) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    DebugBreak();
#else
    __builtin_trap();
#endif
}
void mcbl_debug_dump(const void *ptr, size_t len) {
    const unsigned char *p = (const unsigned char *)ptr;
    for (size_t i = 0; i < len; i++) {
        fprintf(stderr, "%02x ", p[i]);
        if ((i+1) % 16 == 0) fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

McblBench *mcbl_bench_start(const char *label) {
    McblBench *b = (McblBench *)calloc(1, sizeof(McblBench));
    if (!b) return NULL;
    if (label) strncpy(b->label, label, sizeof(b->label) - 1);
    b->start_ns = mcbl_sys_time() * 1000000LL; /* ms → ns approx */
    return b;
}
void mcbl_bench_end(McblBench *b) {
    if (!b) return;
    b->end_ns     = mcbl_sys_time() * 1000000LL;
    b->elapsed_ns = b->end_ns - b->start_ns;
}
void mcbl_bench_print(const McblBench *b) {
    if (!b) return;
    printf("[BENCH] %s: %.3f ms\n", b->label, (double)b->elapsed_ns / 1e6);
}

void mcbl_prof_begin(const char *func) {
    if (!func || g_prof_count >= MAX_PROF_ENTRIES) return;
    for (int i = 0; i < g_prof_count; i++) {
        if (strcmp(g_prof[i].func, func) == 0) {
            g_prof[i]._start_tmp = mcbl_sys_time();
            return;
        }
    }
    McblProfEntry *e = &g_prof[g_prof_count++];
    strncpy(e->func, func, sizeof(e->func) - 1);
    e->calls    = 0;
    e->total_ns = 0;
    e->min_ns   = INT64_MAX;
    e->max_ns   = 0;
}
void mcbl_prof_end(const char *func) {
    if (!func) return;
    int64_t now = mcbl_sys_time();
    for (int i = 0; i < g_prof_count; i++) {
        if (strcmp(g_prof[i].func, func) == 0) {
            int64_t elapsed = now - g_prof[i]._start_tmp;
            g_prof[i].calls++;
            g_prof[i].total_ns += elapsed;
            if (elapsed < g_prof[i].min_ns) g_prof[i].min_ns = elapsed;
            if (elapsed > g_prof[i].max_ns) g_prof[i].max_ns = elapsed;
            return;
        }
    }
}
void mcbl_prof_dump(void) {
    printf("=== Profiler (%d entries) ===\n", g_prof_count);
    for (int i = 0; i < g_prof_count; i++) {
        McblProfEntry *e = &g_prof[i];
        printf("  %-30s calls=%lld avg=%.3fms min=%.3fms max=%.3fms\n",
               e->func, (long long)e->calls,
               e->calls ? (double)e->total_ns / e->calls / 1e6 : 0.0,
               (double)e->min_ns / 1e6, (double)e->max_ns / 1e6);
    }
}
void mcbl_prof_reset(void) { memset(g_prof, 0, sizeof(g_prof)); g_prof_count = 0; }
