#ifndef MCBL_MDK_H
#define MCBL_MDK_H

#include "bytecode.h"
#include "symbols.h"
#include "memory.h"
#include <stdint.h>
#include <pthread.h>

/* -----------------------------------------------------------------------
   McBL# MDK  –  McBL Development Kit / Virtual Machine  (v2.0 — UPGRADED)
   Stack-based bytecode interpreter.
   New in v2.0: OOP dispatch, async/await, error handling, array/map,
                math stdlib, debug hooks, MVM integration.
   ----------------------------------------------------------------------- */

#define MDK_STACK_SIZE     8192   /* doubled */
#define MDK_CALLSTACK_SIZE 512    /* deeper for OOP */
#define MDK_TRY_STACK_SIZE 64
#define MDK_ASYNC_POOL     32

typedef enum {
    MDK_VAL_NULL,
    MDK_VAL_INT,
    MDK_VAL_FLOAT,
    MDK_VAL_STRING,
    MDK_VAL_BOOL,
    MDK_VAL_OBJECT,   /* class instance */
    MDK_VAL_ARRAY,
    MDK_VAL_MAP,
    MDK_VAL_SET,
    MDK_VAL_FUNC,     /* function reference */
    MDK_VAL_CHAN,     /* channel */
    MDK_VAL_ERROR,    /* error/exception value */
} MdkValType;

/* Forward decl */
struct MdkArray;
struct MdkMap;

typedef struct {
    MdkValType type;
    union {
        int64_t  ival;
        double   fval;
        char    *sval;   /* heap-owned */
        int      bval;
        void    *obj;    /* McblObject* */
        struct MdkArray *arr;
        struct MdkMap   *map;
        int      func_id; /* bytecode function index */
        void    *chan;    /* McblSocket* or channel ptr */
    };
} MdkValue;

/* Dynamic array */
typedef struct MdkArray {
    MdkValue *items;
    size_t    count;
    size_t    cap;
    int       refs;
} MdkArray;

/* Dynamic map (hash map) */
typedef struct MdkMapEntry {
    char    *key;
    MdkValue val;
    struct MdkMapEntry *next;
} MdkMapEntry;

typedef struct MdkMap {
    MdkMapEntry **buckets;
    size_t        bucket_count;
    size_t        item_count;
    int           refs;
} MdkMap;

/* Call frame */
typedef struct {
    size_t  return_ip;
    size_t  base_sp;
    char   *func_name;
    char   *class_name;   /* for method calls */
    void   *this_obj;     /* McblObject* for method */
    int     is_async;
} CallFrame;

/* Try frame */
typedef struct {
    size_t  catch_ip;
    size_t  finally_ip;
    size_t  sp_save;
    int     active;
} TryFrame;

/* Async task */
typedef struct {
    pthread_t    thread;
    BcChunk     *chunk;
    size_t       start_ip;
    MdkValue     result;
    int          done;
    int          error;
    char         errmsg[256];
} AsyncTask;

typedef struct MdkVM {
    MdkValue   stack[MDK_STACK_SIZE];
    int        sp;

    CallFrame  call_stack[MDK_CALLSTACK_SIZE];
    int        call_sp;

    TryFrame   try_stack[MDK_TRY_STACK_SIZE];
    int        try_sp;

    SymTable  *symbols;

    /* CPU registers */
    int64_t    reg_EAX, reg_EBX, reg_ECX, reg_EDX;
    int64_t    reg_ESI, reg_EDI;  /* extra regs v2 */

    /* CARGO memory pool */
    int        cargo_pool;

    /* TRam block */
    int        tram_block;

    /* last input */
    char      *last_input;

    /* OR-gate pending count */
    int        orgate_pending;

    /* current object context (for method calls) */
    void      *this_obj;   /* McblObject* */
    char       current_class[128];
    char       current_method[128];

    /* Async tasks */
    AsyncTask  async_tasks[MDK_ASYNC_POOL];
    int        async_count;

    /* Debug hooks */
    void      (*on_instr)(struct MdkVM *vm, size_t ip);
    void      (*on_call )(struct MdkVM *vm, const char *func);
    void      (*on_ret  )(struct MdkVM *vm, MdkValue retval);
    void      (*on_error)(struct MdkVM *vm, const char *msg);
    int        debug_mode;
    size_t     step_break_ip;  /* break at this IP */

    /* Error state */
    MdkValue   current_error;  /* active exception */
    int        error;
    char       errmsg[512];
} MdkVM;

/* ---- lifecycle ---- */
MdkVM *mdk_vm_create(SymTable *symbols);
void   mdk_vm_destroy(MdkVM *vm);

/* Execute a BcChunk. Returns 0 = OK, -1 = runtime error. */
int    mdk_vm_exec(MdkVM *vm, const BcChunk *chunk);

/* Execute from a specific IP */
int    mdk_vm_exec_from(MdkVM *vm, const BcChunk *chunk, size_t start_ip);

/* ---- Value helpers ---- */
void       mdk_push(MdkVM *vm, MdkValue v);
MdkValue   mdk_pop (MdkVM *vm);
MdkValue   mdk_peek(MdkVM *vm, int offset);
void       mdk_value_free(MdkValue *v);
MdkValue   mdk_value_dup (const MdkValue *v);

MdkValue   mdk_mkint  (int64_t i);
MdkValue   mdk_mkfloat(double f);
MdkValue   mdk_mkstr  (const char *s);
MdkValue   mdk_mkbool (int b);
MdkValue   mdk_mknull (void);
MdkValue   mdk_mkobj  (void *obj);
MdkValue   mdk_mkarr  (MdkArray *arr);
MdkValue   mdk_mkmap  (MdkMap *map);
MdkValue   mdk_mkerr  (const char *msg);

const char *mdk_val_to_str(const MdkValue *v, char *buf, size_t buf_size);
int         mdk_val_truthy(const MdkValue *v);
int         mdk_val_eq    (const MdkValue *a, const MdkValue *b);

/* ---- Array helpers ---- */
MdkArray *mdk_arr_new  (size_t cap);
void      mdk_arr_free (MdkArray *arr);
void      mdk_arr_push (MdkArray *arr, MdkValue v);
MdkValue  mdk_arr_pop  (MdkArray *arr);
MdkValue  mdk_arr_get  (MdkArray *arr, int64_t idx);
void      mdk_arr_set  (MdkArray *arr, int64_t idx, MdkValue v);
MdkArray *mdk_arr_slice(MdkArray *arr, int64_t start, int64_t end);
void      mdk_arr_ref  (MdkArray *arr);
void      mdk_arr_unref(MdkArray *arr);

/* ---- Map helpers ---- */
MdkMap  *mdk_map_new  (void);
void     mdk_map_free (MdkMap *map);
void     mdk_map_set  (MdkMap *map, const char *key, MdkValue val);
MdkValue mdk_map_get  (MdkMap *map, const char *key);
int      mdk_map_has  (MdkMap *map, const char *key);
void     mdk_map_del  (MdkMap *map, const char *key);
MdkArray *mdk_map_keys(MdkMap *map);
MdkArray *mdk_map_vals(MdkMap *map);
void     mdk_map_ref  (MdkMap *map);
void     mdk_map_unref(MdkMap *map);

/* ---- Async helpers ---- */
int       mdk_async_spawn(MdkVM *vm, const BcChunk *chunk, size_t start_ip);
int       mdk_async_await(MdkVM *vm, int task_id, MdkValue *out);
int       mdk_async_done (MdkVM *vm, int task_id);

/* ---- Error handling helpers ---- */
void      mdk_throw(MdkVM *vm, const char *msg);
void      mdk_throw_val(MdkVM *vm, MdkValue err);
int       mdk_in_try_block(const MdkVM *vm);
void      mdk_unwind_to_catch(MdkVM *vm);

/* ---- Debug ---- */
void      mdk_set_debug(MdkVM *vm, int enabled);
void      mdk_set_breakpoint(MdkVM *vm, size_t ip);
void      mdk_step(MdkVM *vm);
void      mdk_dump_stack(const MdkVM *vm);

#endif /* MCBL_MDK_H */
