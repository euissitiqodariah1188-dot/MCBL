#ifndef MCBL_PROC_H
#define MCBL_PROC_H
/*
 * McBL# proc.* — Process Management
 * =====================================
 * proc.run(cmd)          → exit code
 * proc.output(cmd)       → stdout as string
 * proc.spawn(cmd)        → pid (background)
 * proc.wait(pid)         → wait for process
 * proc.kill(pid, sig)    → send signal
 * proc.pid()             → current process id
 * proc.ppid()            → parent process id
 * proc.env(var)          → get environment variable
 * proc.setenv(var, val)  → set environment variable
 * proc.args()            → argv as array
 * proc.exit(code)        → exit process
 */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int      mcbl_proc_run    (const char *cmd);
char    *mcbl_proc_output (const char *cmd);
int64_t  mcbl_proc_spawn  (const char *cmd);
int      mcbl_proc_wait   (int64_t pid);
int      mcbl_proc_kill   (int64_t pid, int sig);
int64_t  mcbl_proc_pid    (void);
int64_t  mcbl_proc_ppid   (void);
char    *mcbl_proc_env    (const char *var);
int      mcbl_proc_setenv (const char *var, const char *val);
char   **mcbl_proc_args   (int *out_count);
void     mcbl_proc_exit   (int code);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_PROC_H */
