#ifndef MCBL_COLLECTIONS_H
#define MCBL_COLLECTIONS_H
/*
 * McBL# Collections Standard Library
 * =====================================
 * arr.*   — dynamic array
 * map.*   — hash map (string key)
 * set.*   — hash set
 * deque.* — double-ended queue
 * stack.* — stack
 * queue.* — FIFO queue
 * heap.*  — min/max heap
 * list.*  — linked list
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Dynamic Array (arr.*) ---- */
typedef struct {
    void   *data;
    size_t  elem_size;
    int     count;
    int     capacity;
} McblArr;

McblArr *mcbl_arr_new    (size_t elem_size, int init_cap);
void     mcbl_arr_free   (McblArr *a);
void     mcbl_arr_push   (McblArr *a, const void *elem);
void    *mcbl_arr_pop    (McblArr *a);
void    *mcbl_arr_get    (McblArr *a, int idx);
void     mcbl_arr_set    (McblArr *a, int idx, const void *elem);
void     mcbl_arr_insert (McblArr *a, int idx, const void *elem);
void     mcbl_arr_remove (McblArr *a, int idx);
void     mcbl_arr_clear  (McblArr *a);
int      mcbl_arr_len    (McblArr *a);
void    *mcbl_arr_slice  (McblArr *a, int start, int end, int *out_len);
McblArr *mcbl_arr_concat (McblArr *a, McblArr *b);
void     mcbl_arr_reverse(McblArr *a);
int      mcbl_arr_find   (McblArr *a, const void *elem,
                          int (*eq)(const void *, const void *));
McblArr *mcbl_arr_filter (McblArr *a, int (*pred)(const void *));
McblArr *mcbl_arr_map_arr(McblArr *a, void *(*fn)(const void *), size_t out_elem_size);
void    *mcbl_arr_reduce (McblArr *a, void *init,
                          void *(*fn)(void *acc, const void *elem));

/* ---- Hash Map (map.*) ---- */
typedef struct McblMap McblMap;

McblMap *mcbl_map_new    (void);
void     mcbl_map_free   (McblMap *m);
void     mcbl_map_set    (McblMap *m, const char *key, const void *val, size_t val_size);
void    *mcbl_map_get    (McblMap *m, const char *key);
int      mcbl_map_has    (McblMap *m, const char *key);
void     mcbl_map_del    (McblMap *m, const char *key);
int      mcbl_map_len    (McblMap *m);
char   **mcbl_map_keys   (McblMap *m, int *out_count);
void   **mcbl_map_vals   (McblMap *m, int *out_count);
void     mcbl_map_clear  (McblMap *m);
void     mcbl_map_each   (McblMap *m, void (*fn)(const char *key, void *val));

/* Typed helpers */
void     mcbl_map_set_int(McblMap *m, const char *key, long long val);
void     mcbl_map_set_flt(McblMap *m, const char *key, double val);
void     mcbl_map_set_str(McblMap *m, const char *key, const char *val);
long long mcbl_map_get_int(McblMap *m, const char *key);
double    mcbl_map_get_flt(McblMap *m, const char *key);
const char *mcbl_map_get_str(McblMap *m, const char *key);

/* ---- Hash Set (set.*) ---- */
typedef struct McblSet McblSet;
McblSet *mcbl_set_new  (void);
void     mcbl_set_free (McblSet *s);
void     mcbl_set_add  (McblSet *s, const char *key);
int      mcbl_set_has  (McblSet *s, const char *key);
void     mcbl_set_del  (McblSet *s, const char *key);
int      mcbl_set_len  (McblSet *s);
char   **mcbl_set_items(McblSet *s, int *out_count);
McblSet *mcbl_set_union(McblSet *a, McblSet *b);
McblSet *mcbl_set_inter(McblSet *a, McblSet *b);
McblSet *mcbl_set_diff (McblSet *a, McblSet *b);

/* ---- Stack ---- */
typedef struct McblStack McblStack;
McblStack *mcbl_stack_new  (size_t elem_size);
void       mcbl_stack_free (McblStack *s);
void       mcbl_stack_push (McblStack *s, const void *elem);
void      *mcbl_stack_pop  (McblStack *s);
void      *mcbl_stack_peek (McblStack *s);
int        mcbl_stack_empty(McblStack *s);
int        mcbl_stack_len  (McblStack *s);

/* ---- Queue (FIFO) ---- */
typedef struct McblQueue McblQueue;
McblQueue *mcbl_queue_new    (size_t elem_size, int cap);
void       mcbl_queue_free   (McblQueue *q);
void       mcbl_queue_push   (McblQueue *q, const void *elem);
void      *mcbl_queue_pop    (McblQueue *q);
void      *mcbl_queue_front  (McblQueue *q);
int        mcbl_queue_empty  (McblQueue *q);
int        mcbl_queue_len    (McblQueue *q);

/* ---- Min Heap ---- */
typedef struct McblHeap McblHeap;
McblHeap *mcbl_heap_new (size_t elem_size, int (*cmp)(const void *, const void *));
void      mcbl_heap_free(McblHeap *h);
void      mcbl_heap_push(McblHeap *h, const void *elem);
void     *mcbl_heap_pop (McblHeap *h);
void     *mcbl_heap_peek(McblHeap *h);
int       mcbl_heap_len (McblHeap *h);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_COLLECTIONS_H */
