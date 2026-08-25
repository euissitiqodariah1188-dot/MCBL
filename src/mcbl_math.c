/* McBL# Math Standard Library — v2.0 implementation */
#include "mcbl_math.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static uint64_t g_rng_state = 12345678901234567ULL;

/* ---- Scalar ---- */
double mcbl_abs   (double x) { return fabs(x); }
double mcbl_sqrt  (double x) { return sqrt(x); }
double mcbl_cbrt  (double x) { return cbrt(x); }
double mcbl_pow   (double b, double e) { return pow(b, e); }
double mcbl_log   (double x, double base) { return log(x) / log(base); }
double mcbl_log2  (double x) { return log2(x); }
double mcbl_log10 (double x) { return log10(x); }
double mcbl_ln    (double x) { return log(x); }
double mcbl_exp   (double x) { return exp(x); }
double mcbl_sin   (double x) { return sin(x); }
double mcbl_cos   (double x) { return cos(x); }
double mcbl_tan   (double x) { return tan(x); }
double mcbl_asin  (double x) { return asin(x); }
double mcbl_acos  (double x) { return acos(x); }
double mcbl_atan  (double x) { return atan(x); }
double mcbl_atan2 (double y, double x) { return atan2(y, x); }
double mcbl_sinh  (double x) { return sinh(x); }
double mcbl_cosh  (double x) { return cosh(x); }
double mcbl_tanh  (double x) { return tanh(x); }
double mcbl_floor (double x) { return floor(x); }
double mcbl_ceil  (double x) { return ceil(x); }
double mcbl_round (double x) { return round(x); }
double mcbl_trunc (double x) { return trunc(x); }
double mcbl_frac  (double x) { return x - trunc(x); }
double mcbl_sign  (double x) { return (x > 0) ? 1.0 : (x < 0) ? -1.0 : 0.0; }
double mcbl_min   (double a, double b) { return a < b ? a : b; }
double mcbl_max   (double a, double b) { return a > b ? a : b; }
double mcbl_clamp (double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
double mcbl_lerp  (double a, double b, double t) { return a + t * (b - a); }
double mcbl_map   (double x, double a, double b, double c, double d) {
    if (b == a) return c;
    return c + (x - a) / (b - a) * (d - c);
}
double mcbl_mod_f (double x, double m) { return fmod(x, m); }

/* ---- Integer math ---- */
int64_t mcbl_gcd(int64_t a, int64_t b) {
    a = a < 0 ? -a : a; b = b < 0 ? -b : b;
    while (b) { int64_t t = b; b = a % b; a = t; }
    return a;
}
int64_t mcbl_lcm(int64_t a, int64_t b) {
    int64_t g = mcbl_gcd(a, b);
    return g ? (a / g) * b : 0;
}
int64_t mcbl_fact(int64_t n) {
    if (n < 0) return -1;
    int64_t r = 1;
    for (int64_t i = 2; i <= n; i++) r *= i;
    return r;
}
int64_t mcbl_comb(int64_t n, int64_t k) {
    if (k < 0 || k > n) return 0;
    if (k == 0 || k == n) return 1;
    int64_t r = 1;
    for (int64_t i = 0; i < k; i++) { r *= (n - i); r /= (i + 1); }
    return r;
}
int64_t mcbl_perm(int64_t n, int64_t k) {
    if (k < 0 || k > n) return 0;
    int64_t r = 1;
    for (int64_t i = n - k + 1; i <= n; i++) r *= i;
    return r;
}
int64_t mcbl_isqrt(int64_t n) {
    if (n < 0) return -1;
    int64_t x = (int64_t)sqrt((double)n);
    while (x * x > n) x--;
    while ((x+1)*(x+1) <= n) x++;
    return x;
}
int mcbl_isprime(int64_t n) {
    if (n < 2) return 0;
    if (n == 2) return 1;
    if (n % 2 == 0) return 0;
    for (int64_t i = 3; i * i <= n; i += 2) if (n % i == 0) return 0;
    return 1;
}
int mcbl_popcount(uint64_t x) {
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
}
uint64_t mcbl_next_pow2(uint64_t x) {
    x--;
    x |= x >> 1; x |= x >> 2; x |= x >> 4;
    x |= x >> 8; x |= x >> 16; x |= x >> 32;
    return x + 1;
}

/* ---- Random (xorshift64) ---- */
void mcbl_seed(uint64_t seed) { g_rng_state = seed ? seed : 1; }
double mcbl_rand(void) {
    g_rng_state ^= g_rng_state << 13;
    g_rng_state ^= g_rng_state >> 7;
    g_rng_state ^= g_rng_state << 17;
    return (double)(g_rng_state >> 11) / (double)(1ULL << 53);
}
double mcbl_rand_range(double lo, double hi) { return lo + mcbl_rand() * (hi - lo); }
int64_t mcbl_rand_int(int64_t lo, int64_t hi) {
    return lo + (int64_t)(mcbl_rand() * (double)(hi - lo + 1));
}
void mcbl_shuffle(void *arr, size_t n, size_t esz) {
    char *a = (char *)arr;
    char *tmp = (char *)malloc(esz);
    if (!tmp) return;
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)(mcbl_rand() * (double)(i + 1));
        if (i == j) continue;
        memcpy(tmp,       a + i*esz, esz);
        memcpy(a + i*esz, a + j*esz, esz);
        memcpy(a + j*esz, tmp,       esz);
    }
    free(tmp);
}

/* ---- Calculus ---- */
double mcbl_deriv(McblFn f, void *ctx, double x, double dx) {
    if (dx <= 0) dx = MCBL_EPSILON;
    return (f(x + dx, ctx) - f(x - dx, ctx)) / (2.0 * dx);
}
double mcbl_deriv2(McblFn f, void *ctx, double x, double dx) {
    if (dx <= 0) dx = MCBL_EPSILON;
    return (f(x + dx, ctx) - 2.0*f(x, ctx) + f(x - dx, ctx)) / (dx * dx);
}
double mcbl_integ(McblFn f, void *ctx, double a, double b, int64_t n) {
    if (n <= 0) n = 1000;
    if (n % 2 != 0) n++;
    double h = (b - a) / (double)n;
    double sum = f(a, ctx) + f(b, ctx);
    for (int64_t i = 1; i < n; i++) {
        double x = a + i * h;
        sum += (i % 2 == 0) ? 2.0 * f(x, ctx) : 4.0 * f(x, ctx);
    }
    return sum * h / 3.0;
}
double mcbl_integ_gauss(McblFn f, void *ctx, double a, double b) {
    /* 5-point Gauss-Legendre */
    static const double pts[] = {0.0, 0.538469310, -0.538469310, 0.906179846, -0.906179846};
    static const double wts[] = {0.568888889, 0.478628671,  0.478628671, 0.236926885,  0.236926885};
    double mid = (a + b) / 2.0, half = (b - a) / 2.0;
    double sum = 0.0;
    for (int i = 0; i < 5; i++) sum += wts[i] * f(mid + half * pts[i], ctx);
    return half * sum;
}
double mcbl_limit(McblFn f, void *ctx, double x, double eps) {
    if (eps <= 0) eps = 1e-7;
    return (f(x + eps, ctx) + f(x - eps, ctx)) / 2.0;
}
double mcbl_sum_range(int64_t start, int64_t end, McblFn f, void *ctx) {
    double sum = 0.0;
    for (int64_t i = start; i <= end; i++) sum += f((double)i, ctx);
    return sum;
}
double mcbl_prod_range(int64_t start, int64_t end, McblFn f, void *ctx) {
    double prod = 1.0;
    for (int64_t i = start; i <= end; i++) prod *= f((double)i, ctx);
    return prod;
}
double mcbl_newton(McblFn f, McblFn df, void *ctx, double x0, double eps, int max_iter) {
    double x = x0;
    for (int i = 0; i < max_iter; i++) {
        double fx = f(x, ctx), dfx = df(x, ctx);
        if (fabs(dfx) < 1e-15) break;
        double x1 = x - fx / dfx;
        if (fabs(x1 - x) < eps) return x1;
        x = x1;
    }
    return x;
}
double mcbl_bisect(McblFn f, void *ctx, double a, double b, double eps, int max_iter) {
    for (int i = 0; i < max_iter; i++) {
        double mid = (a + b) / 2.0;
        double fm = f(mid, ctx);
        if (fabs(fm) < eps || (b - a) / 2.0 < eps) return mid;
        if (fm * f(a, ctx) > 0) a = mid; else b = mid;
    }
    return (a + b) / 2.0;
}

/* ---- Vectors ---- */
McblVec *mcbl_vec_new(size_t n) {
    McblVec *v = (McblVec *)malloc(sizeof(McblVec));
    if (!v) return NULL;
    v->data = (double *)calloc(n, sizeof(double));
    v->n    = n;
    return v;
}
void mcbl_vec_free(McblVec *v) {
    if (!v) return; free(v->data); free(v);
}
McblVec *mcbl_vec_from(const double *vals, size_t n) {
    McblVec *v = mcbl_vec_new(n);
    if (v) memcpy(v->data, vals, n * sizeof(double));
    return v;
}
double mcbl_vec_dot(const McblVec *a, const McblVec *b) {
    if (!a || !b || a->n != b->n) return 0;
    double s = 0;
    for (size_t i = 0; i < a->n; i++) s += a->data[i] * b->data[i];
    return s;
}
double mcbl_vec_norm(const McblVec *v) {
    if (!v) return 0;
    return sqrt(mcbl_vec_dot(v, v));
}
McblVec *mcbl_vec_cross(const McblVec *a, const McblVec *b) {
    if (!a || !b || a->n != 3 || b->n != 3) return NULL;
    double vals[3] = {
        a->data[1]*b->data[2] - a->data[2]*b->data[1],
        a->data[2]*b->data[0] - a->data[0]*b->data[2],
        a->data[0]*b->data[1] - a->data[1]*b->data[0]
    };
    return mcbl_vec_from(vals, 3);
}
McblVec *mcbl_vec_normalize(const McblVec *v) {
    double n = mcbl_vec_norm(v);
    if (n < 1e-15) return mcbl_vec_from(v->data, v->n);
    McblVec *r = mcbl_vec_new(v->n);
    if (!r) return NULL;
    for (size_t i = 0; i < v->n; i++) r->data[i] = v->data[i] / n;
    return r;
}
McblVec *mcbl_vec_add(const McblVec *a, const McblVec *b) {
    if (!a || !b || a->n != b->n) return NULL;
    McblVec *r = mcbl_vec_new(a->n);
    if (!r) return NULL;
    for (size_t i = 0; i < a->n; i++) r->data[i] = a->data[i] + b->data[i];
    return r;
}
McblVec *mcbl_vec_sub(const McblVec *a, const McblVec *b) {
    if (!a || !b || a->n != b->n) return NULL;
    McblVec *r = mcbl_vec_new(a->n);
    if (!r) return NULL;
    for (size_t i = 0; i < a->n; i++) r->data[i] = a->data[i] - b->data[i];
    return r;
}
McblVec *mcbl_vec_scale(const McblVec *v, double s) {
    if (!v) return NULL;
    McblVec *r = mcbl_vec_new(v->n);
    if (!r) return NULL;
    for (size_t i = 0; i < v->n; i++) r->data[i] = v->data[i] * s;
    return r;
}
double mcbl_vec_angle(const McblVec *a, const McblVec *b) {
    double d = mcbl_vec_dot(a, b);
    double n = mcbl_vec_norm(a) * mcbl_vec_norm(b);
    if (n < 1e-15) return 0;
    return acos(mcbl_clamp(d / n, -1.0, 1.0));
}

/* ---- Matrices ---- */
McblMat *mcbl_mat_new(size_t rows, size_t cols) {
    McblMat *m = (McblMat *)malloc(sizeof(McblMat));
    if (!m) return NULL;
    m->data = (double *)calloc(rows * cols, sizeof(double));
    m->rows = rows; m->cols = cols;
    return m;
}
void mcbl_mat_free(McblMat *m) { if (!m) return; free(m->data); free(m); }
McblMat *mcbl_mat_eye(size_t n) {
    McblMat *m = mcbl_mat_new(n, n);
    if (!m) return NULL;
    for (size_t i = 0; i < n; i++) m->data[i * n + i] = 1.0;
    return m;
}
McblMat *mcbl_mat_from(size_t rows, size_t cols, const double *vals) {
    McblMat *m = mcbl_mat_new(rows, cols);
    if (m) memcpy(m->data, vals, rows * cols * sizeof(double));
    return m;
}
void   mcbl_mat_set(McblMat *m, size_t r, size_t c, double v) { m->data[r*m->cols+c] = v; }
double mcbl_mat_get(const McblMat *m, size_t r, size_t c) { return m->data[r*m->cols+c]; }
McblMat *mcbl_mat_trans(const McblMat *m) {
    McblMat *t = mcbl_mat_new(m->cols, m->rows);
    if (!t) return NULL;
    for (size_t i = 0; i < m->rows; i++)
        for (size_t j = 0; j < m->cols; j++)
            t->data[j * m->rows + i] = m->data[i * m->cols + j];
    return t;
}
McblMat *mcbl_mat_mul(const McblMat *a, const McblMat *b) {
    if (!a || !b || a->cols != b->rows) return NULL;
    McblMat *r = mcbl_mat_new(a->rows, b->cols);
    if (!r) return NULL;
    for (size_t i = 0; i < a->rows; i++)
        for (size_t k = 0; k < a->cols; k++) {
            double aik = a->data[i*a->cols+k];
            for (size_t j = 0; j < b->cols; j++)
                r->data[i*b->cols+j] += aik * b->data[k*b->cols+j];
        }
    return r;
}
double mcbl_mat_trace(const McblMat *m) {
    if (!m || m->rows != m->cols) return 0;
    double t = 0;
    for (size_t i = 0; i < m->rows; i++) t += m->data[i*m->cols+i];
    return t;
}

/* ---- Bitwise ---- */
int64_t mcbl_bit_and(int64_t a, int64_t b) { return a & b; }
int64_t mcbl_bit_or (int64_t a, int64_t b) { return a | b; }
int64_t mcbl_bit_xor(int64_t a, int64_t b) { return a ^ b; }
int64_t mcbl_bit_not(int64_t a)             { return ~a; }
int64_t mcbl_bit_shl(int64_t a, int b)      { return a << b; }
int64_t mcbl_bit_shr(int64_t a, int b)      { return a >> b; }
int64_t mcbl_bit_rol(int64_t a, int b) { b &= 63; return (a << b) | ((uint64_t)a >> (64-b)); }
int64_t mcbl_bit_ror(int64_t a, int b) { b &= 63; return ((uint64_t)a >> b) | (a << (64-b)); }

/* ---- Statistics ---- */
double mcbl_stat_mean(const double *data, size_t n) {
    if (!n) return 0;
    double s = 0; for (size_t i = 0; i < n; i++) s += data[i];
    return s / (double)n;
}
double mcbl_stat_variance(const double *data, size_t n) {
    if (n < 2) return 0;
    double m = mcbl_stat_mean(data, n), s = 0;
    for (size_t i = 0; i < n; i++) { double d = data[i]-m; s += d*d; }
    return s / (double)(n-1);
}
double mcbl_stat_stddev(const double *data, size_t n) { return sqrt(mcbl_stat_variance(data, n)); }
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return da < db ? -1 : da > db ? 1 : 0;
}
double mcbl_stat_median(double *data, size_t n) {
    if (!n) return 0;
    qsort(data, n, sizeof(double), cmp_double);
    return n % 2 ? data[n/2] : (data[n/2-1] + data[n/2]) / 2.0;
}
double mcbl_stat_mode(const double *data, size_t n) {
    if (!n) return 0;
    double mode = data[0]; size_t max_c = 1;
    for (size_t i = 0; i < n; i++) {
        size_t c = 0;
        for (size_t j = 0; j < n; j++) if (data[j] == data[i]) c++;
        if (c > max_c) { max_c = c; mode = data[i]; }
    }
    return mode;
}
void mcbl_stat_minmax(const double *data, size_t n, double *out_min, double *out_max) {
    if (!n) { *out_min = *out_max = 0; return; }
    *out_min = *out_max = data[0];
    for (size_t i = 1; i < n; i++) {
        if (data[i] < *out_min) *out_min = data[i];
        if (data[i] > *out_max) *out_max = data[i];
    }
}
