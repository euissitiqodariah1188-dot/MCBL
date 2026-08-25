#ifndef MCBL_TEST_H
#define MCBL_TEST_H
/*
 * McBL# test.* — Unit Testing Framework
 * ========================================
 * test.assert(cond, msg)       → assert condition
 * test.eq(a, b)                → assert a == b
 * test.neq(a, b)               → assert a != b
 * test.lt(a, b)                → assert a < b
 * test.near(a, b, eps)         → assert |a-b| < eps
 * test.run(name, fn)           → run a test case
 * test.suite(name)             → start a test suite
 * test.report()                → print results
 * test.bench(name, fn, iters)  → benchmark a function
 */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Test result tracking */
typedef struct {
    int passed;
    int failed;
    int skipped;
    double total_ms;
} McblTestResult;

void mcbl_test_assert(int cond,  const char *msg, const char *file, int line);
void mcbl_test_eq_int(long long a, long long b, const char *file, int line);
void mcbl_test_eq_str(const char *a, const char *b, const char *file, int line);
void mcbl_test_eq_flt(double a, double b, double eps, const char *file, int line);
void mcbl_test_neq   (int cond,  const char *msg, const char *file, int line);
void mcbl_test_near  (double a, double b, double eps, const char *file, int line);

void mcbl_test_run   (const char *name, void (*fn)(void));
void mcbl_test_suite (const char *name);
void mcbl_test_report(void);
void mcbl_test_bench (const char *name, void (*fn)(void), int iters);
void mcbl_test_skip  (const char *name, const char *reason);

McblTestResult mcbl_test_get_results(void);

/* Macros for convenience */
#define MCBL_ASSERT(cond, msg)    mcbl_test_assert((cond), (msg), __FILE__, __LINE__)
#define MCBL_EQ_INT(a, b)         mcbl_test_eq_int((a),(b),__FILE__,__LINE__)
#define MCBL_EQ_STR(a, b)         mcbl_test_eq_str((a),(b),__FILE__,__LINE__)
#define MCBL_EQ_FLT(a, b, eps)    mcbl_test_eq_flt((a),(b),(eps),__FILE__,__LINE__)
#define MCBL_NEAR(a, b, eps)       mcbl_test_near((a),(b),(eps),__FILE__,__LINE__)

#ifdef __cplusplus
}
#endif
#endif /* MCBL_TEST_H */
