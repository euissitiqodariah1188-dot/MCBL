#ifndef MCBL_MATH_H
#define MCBL_MATH_H

/*
 * McBL# Math Standard Library  (v2.0)
 * =====================================
 * Keyword matematika level kalkulus.
 * Syntax di McBL#:
 *   math.abs(-5)
 *   math.sqrt(16)
 *   math.deriv((x) => x*x, 3.0, 0.001)
 *   math.integ((x) => math.sin(x), 0.0, math.PI, 1000)
 *   math.sum(i, 1, 100, i*i)
 *   math.matrix(2, 2, {1,0,0,1})
 *   math.dot(v1, v2)
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */
#define MCBL_PI        3.14159265358979323846
#define MCBL_E         2.71828182845904523536
#define MCBL_TAU       6.28318530717958647692
#define MCBL_PHI       1.61803398874989484820   /* golden ratio */
#define MCBL_SQRT2     1.41421356237309504880
#define MCBL_LN2       0.69314718055994530941
#define MCBL_INF       (1.0 / 0.0)
#define MCBL_NAN       (0.0 / 0.0)
#define MCBL_EPSILON   1e-9                      /* default dx */

/* -------------------------------------------------------------------
   Scalar math
   ------------------------------------------------------------------- */
double   mcbl_abs    (double x);
double   mcbl_sqrt   (double x);
double   mcbl_cbrt   (double x);
double   mcbl_pow    (double base, double exp);
double   mcbl_log    (double x, double base);   /* log_base(x)  */
double   mcbl_log2   (double x);
double   mcbl_log10  (double x);
double   mcbl_ln     (double x);                /* natural log  */
double   mcbl_exp    (double x);
double   mcbl_sin    (double x);
double   mcbl_cos    (double x);
double   mcbl_tan    (double x);
double   mcbl_asin   (double x);
double   mcbl_acos   (double x);
double   mcbl_atan   (double x);
double   mcbl_atan2  (double y, double x);
double   mcbl_sinh   (double x);
double   mcbl_cosh   (double x);
double   mcbl_tanh   (double x);
double   mcbl_floor  (double x);
double   mcbl_ceil   (double x);
double   mcbl_round  (double x);
double   mcbl_trunc  (double x);
double   mcbl_frac   (double x);               /* fractional part */
double   mcbl_sign   (double x);               /* -1, 0, 1 */
double   mcbl_min    (double a, double b);
double   mcbl_max    (double a, double b);
double   mcbl_clamp  (double x, double lo, double hi);
double   mcbl_lerp   (double a, double b, double t);
double   mcbl_map    (double x, double a, double b, double c, double d);
double   mcbl_mod_f  (double x, double m);     /* floating-point mod */

/* Integer math */
int64_t  mcbl_gcd    (int64_t a, int64_t b);
int64_t  mcbl_lcm    (int64_t a, int64_t b);
int64_t  mcbl_fact   (int64_t n);              /* n!   */
int64_t  mcbl_comb   (int64_t n, int64_t k);  /* C(n,k) */
int64_t  mcbl_perm   (int64_t n, int64_t k);  /* P(n,k) */
int64_t  mcbl_isqrt  (int64_t n);             /* integer sqrt */
int      mcbl_isprime(int64_t n);
int      mcbl_popcount(uint64_t x);            /* count set bits */
uint64_t mcbl_next_pow2(uint64_t x);

/* Random */
void     mcbl_seed   (uint64_t seed);
double   mcbl_rand   (void);                   /* [0.0, 1.0) */
double   mcbl_rand_range(double lo, double hi);
int64_t  mcbl_rand_int(int64_t lo, int64_t hi);
void     mcbl_shuffle(void *arr, size_t n, size_t elem_size);

/* -------------------------------------------------------------------
   Calculus — numerical methods
   ------------------------------------------------------------------- */

/* Function pointer type for calculus operations */
typedef double (*McblFn)(double x, void *ctx);

/*
 * math.deriv(f, x, dx)
 *   Numerical derivative using central difference:
 *   f'(x) ≈ (f(x+dx) - f(x-dx)) / (2*dx)
 */
double mcbl_deriv(McblFn f, void *ctx, double x, double dx);

/*
 * math.deriv2(f, x, dx)
 *   Second derivative:
 *   f''(x) ≈ (f(x+dx) - 2*f(x) + f(x-dx)) / dx²
 */
double mcbl_deriv2(McblFn f, void *ctx, double x, double dx);

/*
 * math.integ(f, a, b, n)
 *   Numerical integration using Simpson's 1/3 rule (n must be even):
 *   ∫[a,b] f(x) dx
 */
double mcbl_integ(McblFn f, void *ctx, double a, double b, int64_t n);

/*
 * math.integ_gauss(f, a, b)
 *   Gaussian quadrature (5-point Legendre) — higher precision
 */
double mcbl_integ_gauss(McblFn f, void *ctx, double a, double b);

/*
 * math.limit(f, x, eps)
 *   Numerical limit:  lim_{x→c} f(x)
 */
double mcbl_limit(McblFn f, void *ctx, double x, double eps);

/*
 * math.sum(start, end, f(i))  —  Σ f(i) for i in [start, end]
 */
double mcbl_sum_range(int64_t start, int64_t end, McblFn f, void *ctx);

/*
 * math.prod(start, end, f(i))  —  Π f(i)
 */
double mcbl_prod_range(int64_t start, int64_t end, McblFn f, void *ctx);

/*
 * Newton-Raphson root finding: find x where f(x)=0
 */
double mcbl_newton(McblFn f, McblFn df, void *ctx,
                   double x0, double eps, int max_iter);

/*
 * Bisection method: find root in [a,b]
 */
double mcbl_bisect(McblFn f, void *ctx,
                   double a, double b, double eps, int max_iter);

/* -------------------------------------------------------------------
   Vectors (double arrays)
   ------------------------------------------------------------------- */
typedef struct {
    double  *data;
    size_t   n;
} McblVec;

McblVec *mcbl_vec_new (size_t n);
void     mcbl_vec_free(McblVec *v);
McblVec *mcbl_vec_from(const double *vals, size_t n);

double   mcbl_vec_dot  (const McblVec *a, const McblVec *b);
McblVec *mcbl_vec_cross(const McblVec *a, const McblVec *b); /* 3D only */
double   mcbl_vec_norm (const McblVec *v);                   /* Euclidean */
McblVec *mcbl_vec_normalize(const McblVec *v);
McblVec *mcbl_vec_add  (const McblVec *a, const McblVec *b);
McblVec *mcbl_vec_sub  (const McblVec *a, const McblVec *b);
McblVec *mcbl_vec_scale(const McblVec *v, double s);
double   mcbl_vec_angle(const McblVec *a, const McblVec *b); /* in radians */

/* -------------------------------------------------------------------
   Matrices — row-major
   ------------------------------------------------------------------- */
typedef struct {
    double *data;   /* rows * cols doubles */
    size_t  rows;
    size_t  cols;
} McblMat;

McblMat *mcbl_mat_new  (size_t rows, size_t cols);
void     mcbl_mat_free (McblMat *m);
McblMat *mcbl_mat_eye  (size_t n);               /* identity          */
McblMat *mcbl_mat_from (size_t rows, size_t cols, const double *vals);
void     mcbl_mat_set  (McblMat *m, size_t r, size_t c, double v);
double   mcbl_mat_get  (const McblMat *m, size_t r, size_t c);
McblMat *mcbl_mat_add  (const McblMat *a, const McblMat *b);
McblMat *mcbl_mat_sub  (const McblMat *a, const McblMat *b);
McblMat *mcbl_mat_mul  (const McblMat *a, const McblMat *b);
McblMat *mcbl_mat_scale(const McblMat *m, double s);
McblMat *mcbl_mat_trans(const McblMat *m);       /* transpose         */
double   mcbl_mat_det  (const McblMat *m);       /* determinant       */
McblMat *mcbl_mat_inv  (const McblMat *m);       /* inverse           */
double   mcbl_mat_trace(const McblMat *m);       /* trace             */
int      mcbl_mat_eq   (const McblMat *a, const McblMat *b, double eps);

/* Solve Ax = b via Gaussian elimination */
McblVec *mcbl_mat_solve(const McblMat *A, const McblVec *b);

/* -------------------------------------------------------------------
   Bitwise operations (as math ops)
   ------------------------------------------------------------------- */
int64_t  mcbl_bit_and (int64_t a, int64_t b);
int64_t  mcbl_bit_or  (int64_t a, int64_t b);
int64_t  mcbl_bit_xor (int64_t a, int64_t b);
int64_t  mcbl_bit_not (int64_t a);
int64_t  mcbl_bit_shl (int64_t a, int b);
int64_t  mcbl_bit_shr (int64_t a, int b);
int64_t  mcbl_bit_rol (int64_t a, int b);  /* rotate left  */
int64_t  mcbl_bit_ror (int64_t a, int b);  /* rotate right */

/* -------------------------------------------------------------------
   Statistics
   ------------------------------------------------------------------- */
double mcbl_stat_mean    (const double *data, size_t n);
double mcbl_stat_variance(const double *data, size_t n);
double mcbl_stat_stddev  (const double *data, size_t n);
double mcbl_stat_median  (double *data, size_t n);  /* sorts in-place */
double mcbl_stat_mode    (const double *data, size_t n);
void   mcbl_stat_minmax  (const double *data, size_t n,
                          double *out_min, double *out_max);

#ifdef __cplusplus
}
#endif

#endif /* MCBL_MATH_H */
