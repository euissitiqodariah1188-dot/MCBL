#ifndef MCBL_TIME_H
#define MCBL_TIME_H
/*
 * McBL# time.* — Time and Date
 * ==============================
 * time.now()         → unix timestamp ms
 * time.now_ns()      → unix timestamp nanoseconds
 * time.sleep(ms)     → sleep milliseconds
 * time.date()        → "YYYY-MM-DD" string
 * time.datetime()    → "YYYY-MM-DD HH:MM:SS" string
 * time.since(t)      → ms elapsed since timestamp t
 * time.bench_start() → start benchmark timer
 * time.bench_end(t)  → print elapsed since t
 * time.format(ts, fmt) → format timestamp
 */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int64_t  mcbl_time_now    (void);         /* ms */
int64_t  mcbl_time_now_ns (void);         /* nanoseconds */
void     mcbl_time_sleep  (int64_t ms);
char    *mcbl_time_date   (void);         /* "YYYY-MM-DD" */
char    *mcbl_time_datetime(void);        /* "YYYY-MM-DD HH:MM:SS" */
int64_t  mcbl_time_since  (int64_t t);    /* ms elapsed */
int64_t  mcbl_time_bench_start(void);
void     mcbl_time_bench_end(int64_t t, const char *label);

/* High-resolution timer for profiling */
int64_t  mcbl_time_tsc(void);            /* CPU tick count (RDTSC) */
double   mcbl_time_tsc_to_ns(int64_t tsc_count);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_TIME_H */
