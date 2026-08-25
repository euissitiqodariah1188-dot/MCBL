#ifndef MCBL_SYS_H
#define MCBL_SYS_H

/*
 * McBL# System Standard Library  (v2.0)
 * =========================================
 * file.xxx / net.xxx / sys.xxx / debug.xxx keywords.
 */

#include <stddef.h>
#include <stdint.h>
#include "mcbl_str.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------
   File operations  (file.xxx)
   ------------------------------------------------------------------- */
McblStr *mcbl_file_read   (const char *path);
int      mcbl_file_write  (const char *path, const McblStr *content);
int      mcbl_file_append (const char *path, const McblStr *content);
int      mcbl_file_delete (const char *path);
int      mcbl_file_exists (const char *path);
int64_t  mcbl_file_size   (const char *path);
McblStr **mcbl_file_list  (const char *dir_path, size_t *out_count);
int      mcbl_file_mkdir  (const char *path);
int      mcbl_file_copy   (const char *src, const char *dst);
int      mcbl_file_move   (const char *src, const char *dst);
McblStr **mcbl_file_lines (const char *path, size_t *out_count);

/* -------------------------------------------------------------------
   Network  (net.xxx)
   ------------------------------------------------------------------- */
typedef struct {
    int     status_code;
    McblStr *body;
    McblStr *headers;
} HttpResponse;

HttpResponse *mcbl_net_get    (const char *url);
HttpResponse *mcbl_net_post   (const char *url, const McblStr *body,
                                const char *content_type);
void          mcbl_http_free  (HttpResponse *r);

typedef struct McblSocket McblSocket;
McblSocket   *mcbl_net_listen (int port);
McblSocket   *mcbl_net_connect(const char *host, int port);
int           mcbl_net_send   (McblSocket *s, const McblStr *data);
McblStr      *mcbl_net_recv   (McblSocket *s, size_t max_bytes);
void          mcbl_net_close  (McblSocket *s);

/* -------------------------------------------------------------------
   System  (sys.xxx)
   ------------------------------------------------------------------- */
McblStr *mcbl_sys_exec  (const char *cmd);
McblStr *mcbl_sys_env   (const char *var);
int64_t  mcbl_sys_time  (void);   /* Unix timestamp ms */
void     mcbl_sys_sleep (double seconds);
void     mcbl_sys_exit  (int code);
McblStr **mcbl_sys_args (int *out_count);
int64_t  mcbl_sys_pid   (void);
McblStr *mcbl_sys_mem   (void);   /* memory usage report */
McblStr *mcbl_sys_cpu   (void);   /* CPU info */

/* -------------------------------------------------------------------
   Debug  (debug.xxx)
   ------------------------------------------------------------------- */
void     mcbl_debug_assert(int cond, const char *msg, const char *file,
                            int line);
void     mcbl_debug_trace (const char *msg, const char *file, int line);
void     mcbl_debug_log   (const char *level, const char *msg);
void     mcbl_debug_watch (const char *var_name, const void *val,
                            const char *type);
void     mcbl_debug_break (void);  /* software breakpoint */
void     mcbl_debug_dump  (const void *ptr, size_t len);

/* Benchmark block */
typedef struct {
    int64_t  start_ns;
    int64_t  end_ns;
    int64_t  elapsed_ns;
    char     label[128];
} McblBench;

McblBench *mcbl_bench_start(const char *label);
void       mcbl_bench_end  (McblBench *b);
void       mcbl_bench_print(const McblBench *b);

/* Profiler */
typedef struct {
    char     func[128];
    int64_t  calls;
    int64_t  total_ns;
    int64_t  min_ns;
    int64_t  max_ns;
    int64_t  _start_tmp; /* internal: call start timestamp */
} McblProfEntry;

void mcbl_prof_begin(const char *func);
void mcbl_prof_end  (const char *func);
void mcbl_prof_dump (void);
void mcbl_prof_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MCBL_SYS_H */
