/* cx64_asm_stub.c — C fallback when NASM not available */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

void    cx64_asm_call_native(void *fn) { typedef void(*F)(void); ((F)fn)(); }
void    cx64_asm_cache_flush(unsigned char *s, size_t l) { (void)s;(void)l; }
int64_t cx64_asm_rdtsc(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000LL+t.tv_nsec; }
int64_t cx64_asm_get_time_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000LL+t.tv_nsec; }
void    cx64_asm_sfence(void) {}
void    cx64_asm_mfence(void) {}
void    cx64_asm_lfence(void) {}
void    cx64_asm_loop_burst(void *fn, int count) { typedef void(*F)(void); F f=(F)fn; for(int i=0;i<count;i++) f(); }
int64_t cx64_asm_int_div(int64_t a, int64_t b) { return b?a/b:0; }
int64_t cx64_asm_mulhi64(int64_t a, int64_t b) { __int128 r=(__int128)a*b; return (int64_t)(r>>64); }
int     cx64_asm_clz64(uint64_t x) { return x?__builtin_clzll(x):64; }
int     cx64_asm_popcnt64(uint64_t x) { return __builtin_popcountll(x); }
double  cx64_asm_sqrt_f64(double x) { return __builtin_sqrt(x); }
double  cx64_asm_fma_f64(double a,double b,double c) { return a*b+c; }
