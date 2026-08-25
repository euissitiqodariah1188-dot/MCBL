#ifndef MCBL_VP_XT300_H
#define MCBL_VP_XT300_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
   McBL# Virtual Processor XT300  (C++)
   Handles function dispatch, multi-function concurrency, and keeps
   McBL# responsive under thousands of simultaneous loops/processes.
   ----------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to the XT300 processor instance */
typedef struct VPxt300Handle VPxt300Handle;

/* Create / destroy */
VPxt300Handle *vpxt300_create(int max_concurrent);
void           vpxt300_destroy(VPxt300Handle *vp);

/* Submit a function for execution.
   func_id : arbitrary unique identifier for this function
   args    : serialised argument bytes
   arg_len : byte count of args
   Returns a task handle (>= 0) or -1 on error. */
int  vpxt300_submit(VPxt300Handle *vp,
                    const char    *func_name,
                    const void    *args,
                    size_t         arg_len);

/* Wait for a specific task to finish.  Returns 0 = done, -1 = error. */
int  vpxt300_join(VPxt300Handle *vp, int task_id);

/* Wait for ALL pending tasks */
void vpxt300_join_all(VPxt300Handle *vp);

/* Check if a task is still running */
int  vpxt300_running(VPxt300Handle *vp, int task_id);

/* Retrieve the output of a completed task (heap-allocated; caller frees) */
void *vpxt300_result(VPxt300Handle *vp, int task_id, size_t *out_len);

/* Print processor statistics */
void vpxt300_stats(const VPxt300Handle *vp);

#ifdef __cplusplus
}
#endif

#endif /* MCBL_VP_XT300_H */
