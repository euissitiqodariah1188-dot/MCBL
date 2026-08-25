#ifndef MCBL_SYMBOLS_H
#define MCBL_SYMBOLS_H

#include <stddef.h>

/* -----------------------------------------------------------------------
   McBL# Symbol Table  –  scoped hash-map  (v2.0 — UPGRADED)
   Supports: variables, functions, classes, interfaces, enums, macros.
   ----------------------------------------------------------------------- */

typedef enum {
    SYM_VAR_INT,
    SYM_VAR_FLOAT,
    SYM_VAR_STRING,
    SYM_VAR_BOOL,
    SYM_VAR_BYTE,
    SYM_VAR_LONG,
    SYM_VAR_DOUBLE,
    SYM_VAR_CHAR,
    SYM_VAR_ORGATE,     /* @  OR-gate */
    SYM_VAR_REGISTER,   /* $  register-backed */
    SYM_VAR_CONSTEXPR,  /* ^  compile-time constant */
    SYM_VAR_AUTO,       /* auto-inferred */
    SYM_VAR_ARRAY,      /* $array */
    SYM_VAR_MAP,
    SYM_VAR_SET,
    SYM_VAR_TUPLE,
    SYM_VAR_OBJECT,     /* class instance */
    SYM_VAR_PTR,        /* pointer */
    SYM_VAR_ATOMIC,     /* atomic variable */
    SYM_VAR_CHAN,        /* channel */
    SYM_FUNC,           /* dev function */
    SYM_ASYNC_FUNC,     /* async dev */
    SYM_INC,            /* inc class */
    SYM_CLASS,          /* class declaration */
    SYM_CLASS_INT,      /* classInt inner class */
    SYM_INTERFACE,      /* interface */
    SYM_STRUCT,         /* struct */
    SYM_ENUM,           /* enum */
    SYM_ENUM_VARIANT,   /* enum variant */
    SYM_MACRO,          /* macro */
    SYM_CONST,          /* const alias */
    SYM_EXTERN_FILE,    /* externFile */
    SYM_CLL_LIB,        /* include<lib.cll> */
    SYM_TYPE_ALIAS,     /* type Name = ... */
    SYM_LABEL           /* jump label for goto (internal) */
} SymKind;

/* Type info for static typing */
typedef struct {
    char        *name;       /* "int" "float" "MyClass" etc. */
    size_t       size;       /* sizeof in bytes (0 = unknown) */
    int          is_nullable;
    int          is_array;
    int          is_ptr;
    char        *elem_type;  /* for array/pointer element type */
} TypeInfo;

typedef struct Symbol {
    char       *name;
    SymKind     kind;
    char        sigil;       /* '#' '@' '$' '^' or 0 */
    TypeInfo    type;

    /* runtime value */
    union {
        long long   ival;
        double      fval;
        char       *sval;
        int         bval;
        void       *pval;    /* pointer/object */
    } val;

    /* for functions/class/inc: AST/bytecode reference */
    int          decl_index;
    int          func_addr;
    int          bc_addr;

    /* for arrays */
    void        *array_data;
    size_t       array_len;

    /* access modifier */
    int          access;     /* 0=pub 1=priv 2=prot */
    int          is_static;
    int          is_final;

    struct Symbol *next;
} Symbol;

#define SYMTABLE_BUCKETS 256  /* doubled for better perf */

typedef struct SymScope SymScope;
struct SymScope {
    Symbol    *buckets[SYMTABLE_BUCKETS];
    SymScope  *parent;
    char       scope_name[64]; /* for debug */
};

typedef struct {
    SymScope *current;
    int       depth;          /* current nesting depth */
    int       total_symbols;  /* count for diagnostics */
} SymTable;

/* Lifecycle */
SymTable *symtable_create(void);
void      symtable_destroy(SymTable *st);

/* Scope management */
void      symtable_push_scope(SymTable *st);
void      symtable_push_named_scope(SymTable *st, const char *name);
void      symtable_pop_scope(SymTable *st);

/* Insert (0=ok, -1=alloc fail, 1=already defined in current scope) */
int       symtable_insert(SymTable *st, const char *name,
                           SymKind kind, char sigil);

/* Insert with type info */
int       symtable_insert_typed(SymTable *st, const char *name,
                                 SymKind kind, char sigil,
                                 const char *type_name);

/* Lookup (searches from current scope outward) */
Symbol   *symtable_lookup(SymTable *st, const char *name);

/* Lookup only in current scope */
Symbol   *symtable_lookup_local(SymTable *st, const char *name);

/* Update value helpers */
void      symtable_set_int   (SymTable *st, const char *name, long long v);
void      symtable_set_float (SymTable *st, const char *name, double    v);
void      symtable_set_string(SymTable *st, const char *name, const char *v);
void      symtable_set_bool  (SymTable *st, const char *name, int       v);
void      symtable_set_ptr   (SymTable *st, const char *name, void     *v);

/* Type checking helpers */
int       symtable_type_compatible(const Symbol *sym, const char *type_name);
int       symtable_is_numeric     (const Symbol *sym);

/* Debug dump */
void      symtable_dump(const SymTable *st);

#endif /* MCBL_SYMBOLS_H */
