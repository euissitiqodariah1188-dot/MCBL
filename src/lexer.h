#ifndef MCBL_LEXER_H
#define MCBL_LEXER_H

#include <stddef.h>

/* -----------------------------------------------------------------------
   McBL# Lexer  –  Tokenizer layer  (v2.0 — UPGRADED)
   Converts raw source text into a flat token stream consumed by the parser.
   ----------------------------------------------------------------------- */

typedef enum {
    /* Literals */
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL,
    TOK_STRING_LITERAL,
    TOK_BOOL_LITERAL,
    TOK_IDENT,

    /* Core keywords */
    TOK_INC,          /* inc  */
    TOK_ENDINC,       /* endinc */
    TOK_PR,           /* pr   */
    TOK_IF,           /* if   */
    TOK_ELSE,         /* else */
    TOK_ELSEIF,       /* elseif */
    TOK_DO,           /* do   */
    TOK_FOR,          /* for  */
    TOK_RANGE,        /* range */
    TOK_LOOP,         /* loop */
    TOK_WHILE,        /* while */
    TOK_BREAK,        /* break */
    TOK_RETURN,       /* return */
    TOK_CONTINUE,     /* continue */
    TOK_WAIT,         /* wait */
    TOK_WAIT_FOR,     /* wait for */
    TOK_USE,          /* use   */
    TOK_RESPONSE,     /* response */
    TOK_INPUTXT,      /* inputxt */
    TOK_DEV,          /* dev   */
    TOK_IMPORT,       /* import */
    TOK_THREAD,       /* thread */
    TOK_CONST,        /* const */
    TOK_CLICKED,      /* clicked */
    TOK_SEND,         /* send  */
    TOK_READFILE,     /* readfile */
    TOK_CREATEWINDOW, /* createWindow */

    /* Variable sigils */
    TOK_HASH,         /* #  – standard var */
    TOK_AT,           /* @  – OR-gate var  */
    TOK_DOLLAR,       /* $  – register var */
    TOK_CARET,        /* ^  – constant-expr var */

    /* Memory / CPU keywords */
    TOK_CARGO_M,      /* CARGO_m      */
    TOK_CLEAR_CARGO,  /* CLEAR_cargo  */
    TOK_AUTOCLEAR,    /* AUTOCLEAR_cargo */
    TOK_USING_CPU,    /* using(cpu)   */

    /* Assembly-level keywords */
    TOK_MOV,          /* MOV  */
    TOK_EAX,          /* EAX  */

    /* String operations */
    TOK_STRING_TYPE,  /* string: */
    TOK_TOKEN_STRING, /* TOKENstring */
    TOK_SPLIT_STRING, /* splitstring */
    TOK_COMBINE_STRING,/* combinestring */

    /* Interop */
    TOK_EXTERN_M,     /* extern m */
    TOK_EXTERN_C,     /* extern c */

    /* Data / send */
    TOK_DATA_OPEN,    /* { (data block open) */
    TOK_DATA_CLOSE,   /* } */
    TOK_BRACKET_OPEN, /* [ */
    TOK_BRACKET_CLOSE,/* ] */

    /* Operators & punctuation */
    TOK_ASSIGN,       /* = */
    TOK_EQ_EQ,        /* == */
    TOK_CHAIN_EQ,     /* == chained */
    TOK_PLUS,         /* + */
    TOK_MINUS,        /* - */
    TOK_STAR,         /* * */
    TOK_SLASH,        /* / */
    TOK_LT,           /* < */
    TOK_GT,           /* > */
    TOK_LE,           /* <= */
    TOK_GE,           /* >= */
    TOK_NEQ,          /* != */
    TOK_DOT,          /* . */
    TOK_COMMA,        /* , */
    TOK_SEMICOLON,    /* ; */
    TOK_COLON,        /* : */
    TOK_LPAREN,       /* ( */
    TOK_RPAREN,       /* ) */
    TOK_HASH_PP,      /* # preprocessor */
    TOK_UNDERSCORE,   /* _ */
    TOK_BANG,         /* ! */
    TOK_AMP,          /* & */
    TOK_PIPE,         /* | */
    TOK_PERCENT,      /* % */
    TOK_POWER,        /* ** power/exponent */
    TOK_LSHIFT,       /* << left shift */
    TOK_RSHIFT,       /* >> right shift */
    TOK_BITAND,       /* & bitwise AND  */
    TOK_BITOR,        /* | bitwise OR   */
    TOK_BITXOR,       /* ^ bitwise XOR  */
    TOK_BITNOT,       /* ~ bitwise NOT  */
    TOK_PLUS_ASSIGN,  /* += */
    TOK_MINUS_ASSIGN, /* -= */
    TOK_MUL_ASSIGN,   /* *= */
    TOK_DIV_ASSIGN,   /* /= */

    /* UI Markup tokens */
    TOK_UI_OPEN,
    TOK_UI_CLOSE,
    TOK_UI_TAG,
    TOK_UI_ENDUI,
    TOK_UI_SCRIPT,
    TOK_UI_ENDSC,
    TOK_UI_HEAD,
    TOK_UI_BUTTON,
    TOK_UI_DIV,
    TOK_UI_DIV_CLOSE,
    TOK_UI_COLOR,
    TOK_UI_GRADIENT,
    TOK_UI_INPUT,
    TOK_UI_PNG,
    TOK_OPEN_PAGE,

    /* ============================================================
       NEW v2.0 KEYWORDS
       ============================================================ */

    /* OOP */
    TOK_CLASS,        /* class       */
    TOK_NEW,          /* new         */
    TOK_THIS,         /* this        */
    TOK_EXTENDS,      /* extends     */
    TOK_INTERFACE,    /* interface   */
    TOK_IMPLEMENTS,   /* implements  */
    TOK_PUB,          /* pub         (public)  */
    TOK_PRIV,         /* priv        (private) */
    TOK_PROT,         /* prot        (protected) */
    TOK_STATIC,       /* static      */
    TOK_ABSTRACT,     /* abstract    */
    TOK_FINAL,        /* final       */
    TOK_OVERRIDE,     /* override    */
    TOK_VIRTUAL,      /* virtual     */
    TOK_INSTANCEOF,   /* instanceof  */
    TOK_SUPER,        /* super       */

    /* Type system */
    TOK_TYPE_INT,     /* int         */
    TOK_TYPE_FLOAT,   /* float       */
    TOK_TYPE_STRING,  /* str         */
    TOK_TYPE_BOOL2,   /* bool        */
    TOK_TYPE_VOID,    /* void        */
    TOK_TYPE_BYTE,    /* byte        */
    TOK_TYPE_LONG,    /* long        */
    TOK_TYPE_DOUBLE,  /* double      */
    TOK_TYPE_CHAR,    /* char        */
    TOK_TYPE_AUTO,    /* auto        (type inference) */
    TOK_NULLABLE,     /* nullable    */
    TOK_AS,           /* as          (type cast) */

    /* Data structures */
    TOK_ARRAY_KW,     /* array       */
    TOK_MAP_KW,       /* map         */
    TOK_SET_KW,       /* set         */
    TOK_TUPLE_KW,     /* tuple       */
    TOK_STRUCT_KW,    /* struct      */
    TOK_ENUM_KW,      /* enum        */

    /* Error handling */
    TOK_TRY,          /* try         */
    TOK_CATCH,        /* catch       */
    TOK_FINALLY,      /* finally     */
    TOK_THROW,        /* throw       */
    TOK_RAISES,       /* raises      */

    /* MVM — McBL Virtual Machine */
    TOK_MVM_SPAWN,    /* mvm_spawn   */
    TOK_MVM_SYNC,     /* mvm_sync    */
    TOK_MVM_PIPE,     /* mvm_pipe    */
    TOK_MVM_KILL,     /* mvm_kill    */
    TOK_MVM_CORE,     /* mvm_core(n) */
    TOK_MVM_OPT,      /* mvm_opt     */
    TOK_MVM_NATIVE,   /* mvm_native  */
    TOK_MVM_INLINE,   /* mvm_inline  (inline asm block) */

    /* MBLL — McBL Bridge Low Level */
    TOK_EXTERN_FILE,  /* externFile(filename)  */
    TOK_INCLUDE_LIB,  /* include<file.cll>     */
    TOK_WRITE_TRAM,   /* WriteTRam(mb)         */
    TOK_CLEAN_TRAM,   /* cleanTram()           */

    /* New variable/data kinds */
    TOK_ARRAY_DECL,   /* $array      */
    TOK_POINTER_OPEN, /* >>          */
    TOK_POINTER_CLOSE,/* <<          */
    TOK_FUNC_KW,      /* func        */
    TOK_CLASS_INT,    /* classInt    */
    TOK_SUBCLASS,     /* subclass    */

    /* Math / Calculus keywords */
    TOK_MATH_ABS,     /* math.abs    */
    TOK_MATH_SQRT,    /* math.sqrt   */
    TOK_MATH_POW,     /* math.pow    */
    TOK_MATH_LOG,     /* math.log    */
    TOK_MATH_LN,      /* math.ln     */
    TOK_MATH_EXP,     /* math.exp    */
    TOK_MATH_SIN,     /* math.sin    */
    TOK_MATH_COS,     /* math.cos    */
    TOK_MATH_TAN,     /* math.tan    */
    TOK_MATH_ASIN,    /* math.asin   */
    TOK_MATH_ACOS,    /* math.acos   */
    TOK_MATH_ATAN,    /* math.atan   */
    TOK_MATH_ATAN2,   /* math.atan2  */
    TOK_MATH_FLOOR,   /* math.floor  */
    TOK_MATH_CEIL,    /* math.ceil   */
    TOK_MATH_ROUND,   /* math.round  */
    TOK_MATH_MIN,     /* math.min    */
    TOK_MATH_MAX,     /* math.max    */
    TOK_MATH_CLAMP,   /* math.clamp  */
    TOK_MATH_LERP,    /* math.lerp   */
    TOK_MATH_MOD,     /* math.mod    */
    TOK_MATH_GCD,     /* math.gcd    */
    TOK_MATH_LCM,     /* math.lcm    */
    TOK_MATH_FACT,    /* math.fact   */
    TOK_MATH_FRAC,    /* math.frac   */
    TOK_MATH_PI,      /* math.PI     */
    TOK_MATH_E,       /* math.E      */
    TOK_MATH_INF,     /* math.INF    */
    TOK_MATH_NAN,     /* math.NAN    */
    TOK_MATH_DERIV,   /* math.deriv  (numerical derivative) */
    TOK_MATH_INTEG,   /* math.integ  (numerical integration/Riemann) */
    TOK_MATH_LIMIT,   /* math.limit  */
    TOK_MATH_SUM,     /* math.sum    */
    TOK_MATH_PROD,    /* math.prod   */
    TOK_MATH_MATRIX,  /* math.matrix */
    TOK_MATH_DOT,     /* math.dot    (dot product) */
    TOK_MATH_CROSS,   /* math.cross  (cross product) */
    TOK_MATH_NORM,    /* math.norm   (vector norm) */
    TOK_MATH_BITWISE, /* math.bitwise */
    TOK_MATH_RAND,    /* math.rand   */
    TOK_MATH_SEED,    /* math.seed   */

    /* String manipulation */
    TOK_STR_LEN,      /* str.len     */
    TOK_STR_UPPER,    /* str.upper   */
    TOK_STR_LOWER,    /* str.lower   */
    TOK_STR_TRIM,     /* str.trim    */
    TOK_STR_FIND,     /* str.find    */
    TOK_STR_REPLACE,  /* str.replace */
    TOK_STR_SUB,      /* str.sub     (substring) */
    TOK_STR_STARTS,   /* str.starts  */
    TOK_STR_ENDS,     /* str.ends    */
    TOK_STR_CONTAINS, /* str.contains*/
    TOK_STR_SPLIT2,   /* str.split   */
    TOK_STR_JOIN,     /* str.join    */
    TOK_STR_REPEAT,   /* str.repeat  */
    TOK_STR_REV,      /* str.rev     (reverse) */
    TOK_STR_FORMAT,   /* str.format  */
    TOK_STR_PAD,      /* str.pad     */
    TOK_STR_COUNT,    /* str.count   */
    TOK_STR_ENCODE,   /* str.encode  (base64/hex/utf8) */
    TOK_STR_DECODE,   /* str.decode  */
    TOK_STR_REGEX,    /* str.regex   */
    TOK_STR_MATCH,    /* str.match   */
    TOK_STR_TOINT,    /* str.toInt   */
    TOK_STR_TOFLOAT,  /* str.toFloat */
    TOK_STR_BYTES,    /* str.bytes   (raw byte array) */
    TOK_STR_CHAR,     /* str.char    (get char at idx) */

    /* File system */
    TOK_FILE_WRITE,   /* file.write  */
    TOK_FILE_APPEND,  /* file.append */
    TOK_FILE_DELETE,  /* file.delete */
    TOK_FILE_EXISTS,  /* file.exists */
    TOK_FILE_SIZE,    /* file.size   */
    TOK_FILE_LIST,    /* file.list   (list directory) */
    TOK_FILE_MKDIR,   /* file.mkdir  */
    TOK_FILE_COPY,    /* file.copy   */
    TOK_FILE_MOVE,    /* file.move   */
    TOK_FILE_LINES,   /* file.lines  (read as array of lines) */

    /* Network */
    TOK_NET_GET,      /* net.get     (HTTP GET) */
    TOK_NET_POST,     /* net.post    */
    TOK_NET_LISTEN,   /* net.listen  (TCP server) */
    TOK_NET_CONNECT,  /* net.connect */
    TOK_NET_SEND,     /* net.send    */
    TOK_NET_RECV,     /* net.recv    */
    TOK_NET_CLOSE,    /* net.close   */

    /* System / OS */
    TOK_SYS_EXEC,     /* sys.exec    (run shell command) */
    TOK_SYS_ENV,      /* sys.env     (get env var) */
    TOK_SYS_TIME,     /* sys.time    (unix timestamp) */
    TOK_SYS_SLEEP,    /* sys.sleep   */
    TOK_SYS_EXIT,     /* sys.exit    */
    TOK_SYS_ARGS,     /* sys.args    (CLI arguments) */
    TOK_SYS_PID,      /* sys.pid     */
    TOK_SYS_MEM,      /* sys.mem     (memory usage info) */
    TOK_SYS_CPU,      /* sys.cpu     (CPU info) */

    /* Debug */
    TOK_DEBUG_ASSERT, /* debug.assert*/
    TOK_DEBUG_TRACE,  /* debug.trace */
    TOK_DEBUG_LOG,    /* debug.log   */
    TOK_DEBUG_WATCH,  /* debug.watch */
    TOK_DEBUG_BREAK,  /* debug.break */
    TOK_DEBUG_DUMP,   /* debug.dump  */
    TOK_DEBUG_BENCH,  /* debug.bench (benchmark block) */
    TOK_DEBUG_PROF,   /* debug.prof  (profiling) */

    /* Async / concurrency */
    TOK_ASYNC,        /* async       */
    TOK_AWAIT,        /* await       */
    TOK_CHAN,         /* chan        (channel) */
    TOK_MUTEX,        /* mutex       */
    TOK_LOCK,         /* lock        */
    TOK_UNLOCK,       /* unlock      */
    TOK_ATOMIC,       /* atomic      */

    /* Compile-time / meta */
    TOK_MACRO,        /* macro       */
    TOK_COMPTIME,     /* comptime    */
    TOK_TYPEOF,       /* typeof      */
    TOK_SIZEOF,       /* sizeof      */
    TOK_ALIGNOF,      /* alignof     */
    TOK_INLINE_KW,    /* inline      */
    TOK_NOINLINE,     /* noinline    */
    TOK_PACKED,       /* packed      */
    TOK_ALIGN,        /* align       */
    TOK_SECTION,      /* section     */
    TOK_LINK,         /* link        */
    TOK_EXPORT,       /* export      */
    TOK_NORETURN,     /* noreturn    */

    /* CLL library system */
    TOK_CLL_COMPILE,  /* MSDK compile/file.c -CLL */

    /* Special */
    TOK_NEWLINE,
    TOK_EOF,
    TOK_UNKNOWN
} TokenKind;

typedef struct {
    TokenKind  kind;
    char      *value;   /* heap-allocated text, NULL for operators */
    int        line;
    int        col;
} Token;

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
    int         line;
    int         col;
    Token      *tokens;
    size_t      token_count;
    size_t      token_cap;
} Lexer;

/* Lifecycle */
Lexer *lexer_create(const char *source);
void   lexer_destroy(Lexer *l);

/* Run – fills l->tokens; returns 0 on success, -1 on error */
int    lexer_tokenize(Lexer *l);

/* Utility */
const char *token_kind_name(TokenKind k);
void        lexer_dump_tokens(const Lexer *l);

#endif /* MCBL_LEXER_H */
