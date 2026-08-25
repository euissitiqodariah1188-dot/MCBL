#ifndef MCBL_AST_H
#define MCBL_AST_H

#include <stddef.h>

/* -----------------------------------------------------------------------
   McBL# Abstract Syntax Tree  (v2.0 — UPGRADED)
   All node types used by the parser and consumed by codegen / interpreter.
   ----------------------------------------------------------------------- */

typedef enum {
    /* Top-level */
    AST_PROGRAM,

    /* Class/module (inc … endinc) */
    AST_INC_DECL,

    /* Statements */
    AST_PR_STMT,
    AST_VAR_DECL,
    AST_ORGATE_DECL,
    AST_REG_VAR_DECL,
    AST_CONSTEXPR_DECL,
    AST_ASSIGN,
    AST_COMPOUND_ASSIGN,    /* += -= *= /= */
    AST_IF_STMT,
    AST_FOR_STMT,
    AST_LOOP_STMT,
    AST_WHILE_STMT,
    AST_BREAK_STMT,
    AST_RETURN_STMT,
    AST_CONTINUE_STMT,
    AST_USE_STMT,
    AST_WAIT_FOR_STMT,
    AST_RESPONSE_STMT,
    AST_INPUTXT_STMT,
    AST_DEV_DECL,
    AST_CALL_STMT,
    AST_THREAD_STMT,
    AST_IMPORT_STMT,
    AST_SEND_STMT,
    AST_READFILE_STMT,
    AST_CREATEWINDOW_STMT,
    AST_CARGO_M_STMT,
    AST_CLEAR_CARGO_STMT,
    AST_AUTOCLEAR_STMT,
    AST_USING_CPU_STMT,
    AST_MOV_STMT,
    AST_SPLIT_STRING_STMT,
    AST_COMBINE_STRING_STMT,
    AST_DATA_BLOCK,
    AST_EXTERN_M_BLOCK,
    AST_EXTERN_C_BLOCK,
    AST_OPEN_PAGE_STMT,

    /* ============================================================
       NEW v2.0 NODES
       ============================================================ */

    /* OOP */
    AST_CLASS_DECL,         /* class Name [extends Base] [implements I,...] { } */
    AST_CLASS_INT_DECL,     /* classInt(name) { } — inner class */
    AST_SUBCLASS_DECL,      /* subclass(name) inside classInt */
    AST_NEW_EXPR,           /* new ClassName(args)     */
    AST_MEMBER_ACCESS,      /* obj.field               */
    AST_METHOD_CALL,        /* obj.method(args)        */
    AST_THIS_EXPR,          /* this                    */
    AST_SUPER_EXPR,         /* super(args)             */
    AST_INSTANCEOF_EXPR,    /* x instanceof T          */
    AST_FIELD_DECL,         /* pub/priv field : type   */
    AST_METHOD_DECL,        /* pub/priv dev method ... */
    AST_CONSTRUCTOR_DECL,   /* init(args) { }          */
    AST_DESTRUCTOR_DECL,    /* deinit() { }            */

    /* Typed variable declarations */
    AST_TYPED_VAR_DECL,     /* int #x = 5              */
    AST_AUTO_VAR_DECL,      /* auto #x = expr          */

    /* New data structures */
    AST_ARRAY_DECL,         /* $array name = {v1,v2,...} */
    AST_MAP_DECL,           /* map<K,V> name           */
    AST_SET_DECL,           /* set<T> name             */
    AST_TUPLE_DECL,         /* tuple(a,b,c) name       */
    AST_STRUCT_DECL,        /* struct Name { fields }  */
    AST_ENUM_DECL,          /* enum Name { A,B,C }     */
    AST_ARRAY_INDEX,        /* arr[idx]                */
    AST_ARRAY_SLICE,        /* arr[start:end]          */
    AST_ARRAY_PUSH,         /* arr.push(v)             */
    AST_ARRAY_POP,          /* arr.pop()               */
    AST_ARRAY_LEN,          /* arr.len                 */
    AST_MAP_GET,            /* map[key]                */
    AST_MAP_SET,            /* map[key] = val          */
    AST_MAP_HAS,            /* map.has(key)            */
    AST_MAP_KEYS,           /* map.keys()              */
    AST_MAP_VALS,           /* map.vals()              */

    /* Pointer / reference */
    AST_POINTER_DECL,       /* >>name = code<<         */
    AST_DEREF,              /* *ptr                    */
    AST_ADDR_OF,            /* &var                    */
    AST_RAW_PTR,            /* raw pointer type        */

    /* Error handling */
    AST_TRY_STMT,           /* try { } catch(e) { } finally { } */
    AST_THROW_STMT,         /* throw expr              */

    /* MVM */
    AST_MVM_SPAWN_STMT,     /* mvm_spawn(n, block)     */
    AST_MVM_SYNC_STMT,      /* mvm_sync(handles)       */
    AST_MVM_PIPE_STMT,      /* mvm_pipe(chan, val)      */
    AST_MVM_CORE_STMT,      /* mvm_core(n) { }         */
    AST_MVM_OPT_STMT,       /* mvm_opt { }             */
    AST_MVM_NATIVE_STMT,    /* mvm_native asm { }      */
    AST_MVM_INLINE_STMT,    /* mvm_inline { asm }      */

    /* MBLL — low level bridge */
    AST_EXTERN_FILE_STMT,   /* externFile(name.c)      */
    AST_INCLUDE_LIB_STMT,   /* include<lib.cll>        */
    AST_WRITE_TRAM_STMT,    /* WriteTRam(mb)           */
    AST_CLEAN_TRAM_STMT,    /* cleanTram()             */
    AST_FUNC_DECL,          /* func(name) { }          */

    /* Math nodes */
    AST_MATH_CALL,          /* math.xxx(args)          */
    AST_MATH_DERIV,         /* math.deriv(f, x, dx)    */
    AST_MATH_INTEG,         /* math.integ(f, a, b, n)  */
    AST_MATH_MATRIX,        /* math.matrix(rows,cols,data) */
    AST_MATH_SUM,           /* math.sum(var, start, end, expr) */
    AST_MATH_PROD,          /* math.prod(var, start, end, expr)*/

    /* String ops */
    AST_STR_CALL,           /* str.xxx(args)           */

    /* File ops */
    AST_FILE_CALL,          /* file.xxx(args)          */

    /* Network ops */
    AST_NET_CALL,           /* net.xxx(args)           */

    /* System ops */
    AST_SYS_CALL,           /* sys.xxx(args)           */

    /* Debug ops */
    AST_DEBUG_CALL,         /* debug.xxx(args)         */

    /* Async */
    AST_ASYNC_DECL,         /* async dev name(args) {} */
    AST_AWAIT_EXPR,         /* await expr              */
    AST_CHAN_DECL,           /* chan<T> name            */
    AST_MUTEX_DECL,         /* mutex name              */
    AST_LOCK_STMT,          /* lock(m) { }             */
    AST_ATOMIC_DECL,        /* atomic #x = val         */

    /* Compile-time / meta */
    AST_MACRO_DECL,         /* macro name(args) { }    */
    AST_COMPTIME_BLOCK,     /* comptime { }            */
    AST_TYPEOF_EXPR,        /* typeof(expr)            */
    AST_SIZEOF_EXPR,        /* sizeof(type_or_expr)    */
    AST_CAST_EXPR,          /* expr as Type            */

    /* Expressions */
    AST_EXPR_INT,
    AST_EXPR_FLOAT,
    AST_EXPR_STRING,
    AST_EXPR_BOOL,
    AST_EXPR_IDENT,
    AST_EXPR_BINOP,
    AST_EXPR_UNOP,
    AST_EXPR_CALL,
    AST_EXPR_CHAIN_COND,
    AST_EXPR_INDEX,
    AST_EXPR_MEMBER,
    AST_EXPR_TERNARY,       /* cond ? a : b            */
    AST_EXPR_NULL,          /* null literal            */
    AST_EXPR_RANGE,         /* a..b range literal      */
    AST_EXPR_LAMBDA,        /* (args) => expr          */

    /* UI markup */
    AST_UI_PAGE,
    AST_UI_HEAD,
    AST_UI_DIV,
    AST_UI_BUTTON,
    AST_UI_INPUT,
    AST_UI_COLOR,
    AST_UI_GRADIENT,
    AST_UI_SCRIPT,
    AST_UI_PNG,

    /* const var */
    AST_CONST_STMT,

    AST_BLOCK,
    AST_NOP
} AstKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR, OP_NOT,
    OP_CHAIN_AND, OP_CHAIN_OR,
    OP_CONCAT,
    OP_POWER,
    OP_LSHIFT, OP_RSHIFT,
    OP_BITAND, OP_BITOR, OP_BITXOR, OP_BITNOT
} BinOp;

/* Access modifier */
typedef enum {
    ACCESS_PUBLIC = 0,
    ACCESS_PRIVATE,
    ACCESS_PROTECTED,
    ACCESS_STATIC
} AccessMod;

struct AstNode;
typedef struct AstNode AstNode;

typedef struct {
    AstNode **items;
    size_t    count;
    size_t    cap;
} AstList;

struct AstNode {
    AstKind kind;
    int     line;

    union {
        /* Literals */
        long long   ival;
        double      fval;
        char       *sval;
        int         bval;

        /* Binary / unary ops */
        struct {
            BinOp    op;
            AstNode *left;
            AstNode *right;
        } binop;

        /* inc declaration */
        struct {
            char    *name;
            AstList  body;
        } inc;

        /* class declaration */
        struct {
            char       *name;
            char       *base;          /* extends */
            char      **ifaces;        /* implements list */
            int         iface_count;
            int         is_abstract;
            int         is_final;
            AstList     body;          /* fields + methods */
        } class_decl;

        /* classInt inner class */
        struct {
            char    *name;
            AstList  body;
        } class_int;

        /* field declaration */
        struct {
            char       *name;
            char       *type_name;
            AccessMod   access;
            int         is_static;
            AstNode    *default_val;
        } field;

        /* dev / method function */
        struct {
            char       *name;
            AstList     params;
            AstList     body;
            char       *return_type;
            AccessMod   access;
            int         is_static;
            int         is_abstract;
            int         is_async;
            int         is_override;
            int         is_virtual;
            int         is_inline;
        } dev;

        /* async dev */
        struct {
            char    *name;
            AstList  params;
            AstList  body;
            char    *return_type;
        } async_dev;

        /* Variable declarations */
        struct {
            char    *name;
            AstNode *value;
            char    *type_name;   /* NULL = infer */
            char     sigil;
        } var;

        /* Assignment */
        struct {
            char    *name;
            AstNode *value;
            char     op_char;    /* 0=plain, '+'='+=', etc. */
        } assign;

        /* if */
        struct {
            AstNode *cond;
            AstList  then_body;
            AstList  elseif_conds;
            AstList  else_body;
        } if_stmt;

        /* for */
        struct {
            char    *var_name;
            AstNode *start;
            AstNode *end;
            AstNode *step;
            AstList  body;
        } for_stmt;

        /* loop / while */
        struct {
            AstNode *cond;
            AstList  body;
        } loop_stmt;

        /* call / pr / use / wait_for / inputxt / import */
        struct {
            char    *target;
            AstList  args;
        } call;

        /* thread */
        struct {
            AstList body;
        } thread;

        /* send */
        struct {
            char *data_name;
            char *dest;
        } send;

        /* data block */
        struct {
            char    *name;
            AstNode *value;
        } data;

        /* cargo / memory */
        struct {
            long long size;
        } cargo;

        /* MOV */
        struct {
            char    *reg;
            AstNode *value;
        } mov;

        /* string ops */
        struct {
            AstNode *str;
            AstNode *delim;
            char    *result;
        } str_op;

        /* try/catch/finally */
        struct {
            AstList  try_body;
            char    *catch_var;
            AstList  catch_body;
            AstList  finally_body;
        } try_stmt;

        /* array declaration */
        struct {
            char    *name;
            AstList  elements;
            char    *elem_type;
        } array_decl;

        /* map / set */
        struct {
            char    *name;
            char    *key_type;
            char    *val_type;
            AstList  pairs_k;
            AstList  pairs_v;
        } map_decl;

        /* struct */
        struct {
            char    *name;
            AstList  fields;
        } struct_decl;

        /* enum */
        struct {
            char    *name;
            char   **variants;
            int      variant_count;
        } enum_decl;

        /* pointer */
        struct {
            char    *name;
            AstList  code_body;
        } pointer_decl;

        /* new expression */
        struct {
            char    *class_name;
            AstList  args;
        } new_expr;

        /* cast */
        struct {
            AstNode *expr;
            char    *type_name;
        } cast;

        /* ternary */
        struct {
            AstNode *cond;
            AstNode *then_val;
            AstNode *else_val;
        } ternary;

        /* lambda */
        struct {
            AstList params;
            AstNode *body_expr;
        } lambda;

        /* math call / str call / file call / net call / sys call / debug call */
        struct {
            char    *ns;          /* "math" "str" "file" "net" "sys" "debug" */
            char    *func;        /* "abs" "sqrt" etc. */
            AstList  args;
        } ns_call;

        /* math.deriv */
        struct {
            AstNode *func_expr;   /* lambda or dev name */
            AstNode *x_val;
            AstNode *dx_val;      /* step, optional */
        } deriv;

        /* math.integ */
        struct {
            AstNode *func_expr;
            AstNode *a_val;
            AstNode *b_val;
            AstNode *n_val;       /* steps, optional */
        } integ;

        /* math.sum / math.prod */
        struct {
            char    *var_name;
            AstNode *start;
            AstNode *end;
            AstNode *expr;
        } sigma;

        /* MVM */
        struct {
            int      cores;
            AstList  body;
            char    *chan_name;
            AstNode *value;
        } mvm;

        /* MBLL */
        struct {
            char *filename;
        } mbll;

        /* WriteTRam */
        struct {
            long long mb;
        } tram;

        /* macro */
        struct {
            char    *name;
            AstList  params;
            AstList  body;
        } macro;

        /* UI nodes */
        struct {
            char    *name;
            char    *attr_name;
            char    *color_val;
            char    *png_path;
            AstList  children;
            AstList  script;
        } ui;

        /* extern blocks */
        struct {
            char *raw_code;
            char  lang;
        } ext;

        /* generic block */
        struct {
            AstList stmts;
        } block;

        /* return */
        struct {
            AstNode *value;
        } ret;

        /* throw */
        struct {
            AstNode *value;
        } throw_stmt;

        /* await */
        struct {
            AstNode *expr;
        } await;

        /* sizeof / typeof / alignof */
        struct {
            AstNode *expr;      /* may be NULL if type_name given */
            char    *type_name;
        } meta;

        /* chan/mutex */
        struct {
            char    *name;
            char    *elem_type;
        } chan;

        /* lock */
        struct {
            char    *mutex_name;
            AstList  body;
        } lock;
    };
};

/* ---- lifecycle ---- */
AstNode *ast_node_new(AstKind kind, int line);
void     ast_node_free(AstNode *node);

/* ---- list helpers ---- */
void ast_list_init(AstList *list);
int  ast_list_push(AstList *list, AstNode *node);
void ast_list_free(AstList *list);

/* ---- debug ---- */
void ast_dump(const AstNode *node, int indent);

#endif /* MCBL_AST_H */
