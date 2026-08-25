#ifndef MCBL_SORT_H
#define MCBL_SORT_H
/*
 * McBL# sort.* — Sorting Algorithms
 * =====================================
 * sort.quick(arr, n, sz, cmp)    → quicksort in-place
 * sort.merge(arr, n, sz, cmp)    → mergesort (stable)
 * sort.heap(arr, n, sz, cmp)     → heapsort
 * sort.radix_int(arr, n)         → radix sort for int64[]
 * sort.binary_search(arr,n,sz,cmp,key) → index or -1
 * sort.ints(arr, n)              → sort int64 array asc
 * sort.floats(arr, n)            → sort double array asc
 * sort.strings(arr, n)           → sort char* array
 * sort.is_sorted(arr,n,sz,cmp)   → check if sorted
 * sort.reverse(arr, n, sz)       → reverse in-place
 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef int (*McblCmpFn)(const void *, const void *);

void  mcbl_sort_quick  (void *arr, int n, size_t sz, McblCmpFn cmp);
void  mcbl_sort_merge  (void *arr, int n, size_t sz, McblCmpFn cmp);
void  mcbl_sort_heap   (void *arr, int n, size_t sz, McblCmpFn cmp);
void  mcbl_sort_radix  (int64_t *arr, int n);
int   mcbl_sort_bsearch(const void *arr, int n, size_t sz, McblCmpFn cmp,
                         const void *key);
void  mcbl_sort_ints   (int64_t *arr, int n);
void  mcbl_sort_floats (double  *arr, int n);
void  mcbl_sort_strings(char   **arr, int n);
int   mcbl_sort_is_sorted(const void *arr, int n, size_t sz, McblCmpFn cmp);
void  mcbl_sort_reverse(void *arr, int n, size_t sz);

/* Built-in comparators */
int mcbl_cmp_int_asc (const void *a, const void *b);
int mcbl_cmp_int_desc(const void *a, const void *b);
int mcbl_cmp_flt_asc (const void *a, const void *b);
int mcbl_cmp_str_asc (const void *a, const void *b);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_SORT_H */
