#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -----------------------------------------------------------------------
   McBL# Lexer implementation
   ----------------------------------------------------------------------- */

#define TOKENS_INIT_CAP 256

static char *strndup_safe(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

Lexer *lexer_create(const char *source) {
    if (!source) return NULL;
    Lexer *l = (Lexer *)malloc(sizeof(Lexer));
    if (!l) return NULL;
    l->src         = source;
    l->pos         = 0;
    l->len         = strlen(source);
    l->line        = 1;
    l->col         = 1;
    l->token_count = 0;
    l->token_cap   = TOKENS_INIT_CAP;
    l->tokens      = (Token *)malloc(sizeof(Token) * l->token_cap);
    if (!l->tokens) {
        free(l);
        return NULL;
    }
    return l;
}

void lexer_destroy(Lexer *l) {
    if (!l) return;
    for (size_t i = 0; i < l->token_count; i++) {
        free(l->tokens[i].value);
        l->tokens[i].value = NULL;
    }
    free(l->tokens);
    l->tokens = NULL;
    free(l);
}

static int push_token(Lexer *l, TokenKind kind, const char *val, int line, int col) {
    if (l->token_count >= l->token_cap) {
        size_t new_cap = l->token_cap * 2;
        Token *tmp = (Token *)realloc(l->tokens, sizeof(Token) * new_cap);
        if (!tmp) return -1;
        l->tokens    = tmp;
        l->token_cap = new_cap;
    }
    l->tokens[l->token_count].kind  = kind;
    l->tokens[l->token_count].value = val ? strdup(val) : NULL;
    l->tokens[l->token_count].line  = line;
    l->tokens[l->token_count].col   = col;
    l->token_count++;
    return 0;
}

static char peek(const Lexer *l) {
    if (l->pos >= l->len) return '\0';
    return l->src[l->pos];
}

static char peek_ahead(const Lexer *l, size_t off) {
    if (l->pos + off >= l->len) return '\0';
    return l->src[l->pos + off];
}

static char advance(Lexer *l) {
    if (l->pos >= l->len) return '\0';
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else           { l->col++; }
    return c;
}

static void skip_whitespace_no_newline(Lexer *l) {
    while (l->pos < l->len && (peek(l) == ' ' || peek(l) == '\t' || peek(l) == '\r'))
        advance(l);
}

static void skip_line_comment(Lexer *l) {
    while (l->pos < l->len && peek(l) != '\n') advance(l);
}

static void skip_block_comment(Lexer *l) {
    while (l->pos + 1 < l->len) {
        if (peek(l) == '*' && peek_ahead(l, 1) == '/') {
            advance(l); advance(l);
            return;
        }
        advance(l);
    }
}

/* Match keyword table */
typedef struct { const char *kw; TokenKind kind; } KWEntry;

static const KWEntry KEYWORDS[] = {
    /* ---- Core (unchanged) ---- */
    {"inc",             TOK_INC},
    {"endinc",          TOK_ENDINC},
    {"pr",              TOK_PR},
    {"if",              TOK_IF},
    {"elseif",          TOK_ELSEIF},
    {"else",            TOK_ELSE},
    {"do",              TOK_DO},
    {"for",             TOK_FOR},
    {"range",           TOK_RANGE},
    {"loop",            TOK_LOOP},
    {"while",           TOK_WHILE},
    {"break",           TOK_BREAK},
    {"return",          TOK_RETURN},
    {"continue",        TOK_CONTINUE},
    {"wait",            TOK_WAIT},
    {"use",             TOK_USE},
    {"response",        TOK_RESPONSE},
    {"inputxt",         TOK_INPUTXT},
    {"dev",             TOK_DEV},
    {"import",          TOK_IMPORT},
    {"thread",          TOK_THREAD},
    {"const",           TOK_CONST},
    {"clicked",         TOK_CLICKED},
    {"send",            TOK_SEND},
    {"readfile",        TOK_READFILE},
    {"createWindow",    TOK_CREATEWINDOW},
    {"CARGO_m",         TOK_CARGO_M},
    {"CLEAR_cargo",     TOK_CLEAR_CARGO},
    {"AUTOCLEAR_cargo", TOK_AUTOCLEAR},
    {"MOV",             TOK_MOV},
    {"EAX",             TOK_EAX},
    {"TOKENstring",     TOK_TOKEN_STRING},
    {"splitstring",     TOK_SPLIT_STRING},
    {"combinestring",   TOK_COMBINE_STRING},
    {"openpage",        TOK_OPEN_PAGE},
    {"string",          TOK_STRING_TYPE},
    /* ---- NEW v2.0: OOP ---- */
    {"class",           TOK_CLASS},
    {"new",             TOK_NEW},
    {"this",            TOK_THIS},
    {"extends",         TOK_EXTENDS},
    {"interface",       TOK_INTERFACE},
    {"implements",      TOK_IMPLEMENTS},
    {"pub",             TOK_PUB},
    {"priv",            TOK_PRIV},
    {"prot",            TOK_PROT},
    {"static",          TOK_STATIC},
    {"abstract",        TOK_ABSTRACT},
    {"final",           TOK_FINAL},
    {"override",        TOK_OVERRIDE},
    {"virtual",         TOK_VIRTUAL},
    {"instanceof",      TOK_INSTANCEOF},
    {"super",           TOK_SUPER},
    /* ---- NEW v2.0: Types ---- */
    {"int",             TOK_TYPE_INT},
    {"float",           TOK_TYPE_FLOAT},
    {"str",             TOK_TYPE_STRING},
    {"bool",            TOK_TYPE_BOOL2},
    {"void",            TOK_TYPE_VOID},
    {"byte",            TOK_TYPE_BYTE},
    {"long",            TOK_TYPE_LONG},
    {"double",          TOK_TYPE_DOUBLE},
    {"char",            TOK_TYPE_CHAR},
    {"auto",            TOK_TYPE_AUTO},
    {"nullable",        TOK_NULLABLE},
    {"as",              TOK_AS},
    /* ---- NEW v2.0: Data structures ---- */
    {"array",           TOK_ARRAY_KW},
    {"map",             TOK_MAP_KW},
    {"set",             TOK_SET_KW},
    {"tuple",           TOK_TUPLE_KW},
    {"struct",          TOK_STRUCT_KW},
    {"enum",            TOK_ENUM_KW},
    /* ---- NEW v2.0: Error handling ---- */
    {"try",             TOK_TRY},
    {"catch",           TOK_CATCH},
    {"finally",         TOK_FINALLY},
    {"throw",           TOK_THROW},
    {"raises",          TOK_RAISES},
    /* ---- NEW v2.0: MVM ---- */
    {"mvm_spawn",       TOK_MVM_SPAWN},
    {"mvm_sync",        TOK_MVM_SYNC},
    {"mvm_pipe",        TOK_MVM_PIPE},
    {"mvm_kill",        TOK_MVM_KILL},
    {"mvm_core",        TOK_MVM_CORE},
    {"mvm_opt",         TOK_MVM_OPT},
    {"mvm_native",      TOK_MVM_NATIVE},
    {"mvm_inline",      TOK_MVM_INLINE},
    /* ---- NEW v2.0: MBLL ---- */
    {"externFile",      TOK_EXTERN_FILE},
    {"WriteTRam",       TOK_WRITE_TRAM},
    {"cleanTram",       TOK_CLEAN_TRAM},
    {"func",            TOK_FUNC_KW},
    {"classInt",        TOK_CLASS_INT},
    {"subclass",        TOK_SUBCLASS},
    /* ---- NEW v2.0: Async ---- */
    {"async",           TOK_ASYNC},
    {"await",           TOK_AWAIT},
    {"chan",            TOK_CHAN},
    {"mutex",           TOK_MUTEX},
    {"lock",            TOK_LOCK},
    {"unlock",          TOK_UNLOCK},
    {"atomic",          TOK_ATOMIC},
    /* ---- NEW v2.0: Compile-time / meta ---- */
    {"macro",           TOK_MACRO},
    {"comptime",        TOK_COMPTIME},
    {"typeof",          TOK_TYPEOF},
    {"sizeof",          TOK_SIZEOF},
    {"alignof",         TOK_ALIGNOF},
    {"inline",          TOK_INLINE_KW},
    {"noinline",        TOK_NOINLINE},
    {"packed",          TOK_PACKED},
    {"export",          TOK_EXPORT},
    {"noreturn",        TOK_NORETURN},
    {NULL, TOK_UNKNOWN}
};

static TokenKind match_keyword(const char *word) {
    for (int i = 0; KEYWORDS[i].kw != NULL; i++) {
        if (strcmp(word, KEYWORDS[i].kw) == 0)
            return KEYWORDS[i].kind;
    }
    return TOK_IDENT;
}

static int lex_string(Lexer *l) {
    int sline = l->line, scol = l->col;
    advance(l); /* consume opening quote */
    size_t start = l->pos;
    while (l->pos < l->len && peek(l) != '"') {
        if (peek(l) == '\\') advance(l); /* skip escape */
        advance(l);
    }
    char *val = strndup_safe(l->src + start, l->pos - start);
    if (!val) return -1;
    if (peek(l) == '"') advance(l); /* consume closing quote */
    int r = push_token(l, TOK_STRING_LITERAL, val, sline, scol);
    free(val);
    return r;
}

static int lex_number(Lexer *l) {
    int sline = l->line, scol = l->col;
    size_t start = l->pos;
    int is_float = 0;
    while (l->pos < l->len && isdigit((unsigned char)peek(l))) advance(l);
    if (peek(l) == '.' && isdigit((unsigned char)peek_ahead(l, 1))) {
        is_float = 1;
        advance(l);
        while (l->pos < l->len && isdigit((unsigned char)peek(l))) advance(l);
    }
    char *val = strndup_safe(l->src + start, l->pos - start);
    if (!val) return -1;
    int r = push_token(l, is_float ? TOK_FLOAT_LITERAL : TOK_INT_LITERAL, val, sline, scol);
    free(val);
    return r;
}

static int lex_ident_or_keyword(Lexer *l) {
    int sline = l->line, scol = l->col;
    size_t start = l->pos;
    while (l->pos < l->len &&
           (isalnum((unsigned char)peek(l)) || peek(l) == '_'))
        advance(l);
    char *word = strndup_safe(l->src + start, l->pos - start);
    if (!word) return -1;

    /* special two-word combos: "wait for", "extern m", "extern c", "using cpu" */
    TokenKind kind = match_keyword(word);

    if (kind == TOK_WAIT) {
        /* peek for " for" */
        size_t saved_pos = l->pos;
        int saved_line = l->line, saved_col = l->col;
        skip_whitespace_no_newline(l);
        if (l->pos + 3 <= l->len && strncmp(l->src + l->pos, "for", 3) == 0 &&
            !isalnum((unsigned char)l->src[l->pos + 3])) {
            l->pos += 3; l->col += 3;
            kind = TOK_WAIT_FOR;
        } else {
            l->pos = saved_pos; l->line = saved_line; l->col = saved_col;
        }
    } else if (kind == TOK_IDENT && strcmp(word, "extern") == 0) {
        size_t saved_pos = l->pos;
        int saved_line = l->line, saved_col = l->col;
        skip_whitespace_no_newline(l);
        if (peek(l) == 'm' && !isalnum((unsigned char)peek_ahead(l, 1))) {
            advance(l);
            kind = TOK_EXTERN_M;
        } else if (peek(l) == 'c' && !isalnum((unsigned char)peek_ahead(l, 1))) {
            advance(l);
            kind = TOK_EXTERN_C;
        } else {
            l->pos = saved_pos; l->line = saved_line; l->col = saved_col;
        }
    } else if (kind == TOK_USING_CPU) {
        /* 'using' followed by (cpu) – scan past the parens */
        skip_whitespace_no_newline(l);
        if (peek(l) == '(') {
            advance(l);
            skip_whitespace_no_newline(l);
            if (l->pos + 3 <= l->len && strncmp(l->src + l->pos, "cpu", 3) == 0)
                l->pos += 3;
            skip_whitespace_no_newline(l);
            if (peek(l) == ')') advance(l);
            kind = TOK_USING_CPU;
        }
    } else if (kind == TOK_STRING_TYPE) {
        /* consume the colon if present: "string:" */
        if (peek(l) == ':') advance(l);
    }

    /* ---- v2.0: dot-namespace keywords: math.xxx, str.xxx, file.xxx, net.xxx, sys.xxx, debug.xxx ---- */
    if ((kind == TOK_IDENT || kind == TOK_TYPE_STRING || kind == TOK_SYS_EXEC ||
         kind == TOK_NET_GET || kind == TOK_FILE_WRITE || kind == TOK_DEBUG_LOG ||
         kind == TOK_MATH_ABS) && peek(l) == '.') {
        /* Check if word is a known namespace */
        const char *ns = NULL;
        if (strcmp(word, "math")  == 0) ns = "math";
        else if (strcmp(word, "str")   == 0) ns = "str";
        else if (strcmp(word, "file")  == 0) ns = "file";
        else if (strcmp(word, "net")   == 0) ns = "net";
        else if (strcmp(word, "sys")   == 0) ns = "sys";
        else if (strcmp(word, "debug") == 0) ns = "debug";

        if (ns) {
            /* consume the dot */
            advance(l);
            /* read the method name */
            size_t m_start = l->pos;
            while (l->pos < l->len &&
                   (isalnum((unsigned char)peek(l)) || peek(l) == '_'))
                advance(l);
            size_t m_len = l->pos - m_start;
            if (m_len > 0) {
                /* Build "ns.method" token value */
                char *full = (char *)malloc(strlen(ns) + 1 + m_len + 1);
                if (full) {
                    strcpy(full, ns);
                    strcat(full, ".");
                    strncat(full, l->src + m_start, m_len);
                    /* Determine token kind based on namespace */
                    TokenKind ns_kind = TOK_IDENT; /* fallback; parser identifies */
                    if (strcmp(ns, "math")  == 0) ns_kind = TOK_MATH_ABS;   /* generic math call marker */
                    else if (strcmp(ns, "str")   == 0) ns_kind = TOK_STR_LEN;
                    else if (strcmp(ns, "file")  == 0) ns_kind = TOK_FILE_WRITE;
                    else if (strcmp(ns, "net")   == 0) ns_kind = TOK_NET_GET;
                    else if (strcmp(ns, "sys")   == 0) ns_kind = TOK_SYS_EXEC;
                    else if (strcmp(ns, "debug") == 0) ns_kind = TOK_DEBUG_LOG;
                    int r2 = push_token(l, ns_kind, full, sline, scol);
                    free(full);
                    free(word);
                    return r2;
                }
            }
        }
    }

    /* ---- v2.0: $array declaration ---- */
    if (kind == TOK_DOLLAR) {
        /* look for "array" keyword right after $ */
        size_t saved = l->pos;
        int sl2 = l->line, sc2 = l->col;
        skip_whitespace_no_newline(l);
        if (l->pos + 5 <= l->len && strncmp(l->src + l->pos, "array", 5) == 0 &&
            !isalnum((unsigned char)l->src[l->pos + 5])) {
            l->pos += 5; l->col += 5;
            free(word);
            return push_token(l, TOK_ARRAY_DECL, "$array", sline, scol);
        }
        l->pos = saved; l->line = sl2; l->col = sc2;
    }

    /* bool literals */
    if (kind == TOK_IDENT && (strcmp(word, "true") == 0 || strcmp(word, "false") == 0))
        kind = TOK_BOOL_LITERAL;

    /* null literal */
    if (kind == TOK_IDENT && strcmp(word, "null") == 0)
        kind = TOK_IDENT; /* parser checks for "null" string value */

    int r = push_token(l, kind, word, sline, scol);
    free(word);
    return r;
}

/* UI tag lexer  <…> */
static int lex_ui_tag(Lexer *l) {
    int sline = l->line, scol = l->col;
    advance(l); /* consume '<' */
    size_t start = l->pos;
    while (l->pos < l->len && peek(l) != '>') advance(l);
    char *inner = strndup_safe(l->src + start, l->pos - start);
    if (!inner) return -1;
    if (peek(l) == '>') advance(l);

    TokenKind kind = TOK_UI_TAG;
    if (strncasecmp(inner, "endui", 5) == 0 || strncasecmp(inner, "end ui", 6) == 0)
        kind = TOK_UI_ENDUI;
    else if (strncasecmp(inner, "endsc", 5) == 0)
        kind = TOK_UI_ENDSC;
    else if (strncasecmp(inner, "script #", 8) == 0)
        kind = TOK_UI_SCRIPT;
    else if (strncasecmp(inner, "head", 4) == 0)
        kind = TOK_UI_HEAD;
    else if (strncasecmp(inner, "button", 6) == 0)
        kind = TOK_UI_BUTTON;
    else if (strncasecmp(inner, "div>h>", 6) == 0 || strncasecmp(inner, "div", 3) == 0)
        kind = TOK_UI_DIV;
    else if (strncasecmp(inner, "h<div", 5) == 0)
        kind = TOK_UI_DIV_CLOSE;
    else if (strncasecmp(inner, "color", 5) == 0)
        kind = TOK_UI_COLOR;
    else if (strncasecmp(inner, "gradient", 8) == 0)
        kind = TOK_UI_GRADIENT;
    else if (strncasecmp(inner, "input", 5) == 0)
        kind = TOK_UI_INPUT;
    else if (strncasecmp(inner, "pngdocs", 7) == 0)
        kind = TOK_UI_PNG;
    else if (inner[0] == '/' || inner[0] == '\\')
        kind = TOK_UI_CLOSE;
    else if (strncasecmp(inner, "MCBL", 4) == 0)
        kind = TOK_UI_OPEN;

    int r = push_token(l, kind, inner, sline, scol);
    free(inner);
    return r;
}

int lexer_tokenize(Lexer *l) {
    if (!l) return -1;

    while (l->pos < l->len) {
        skip_whitespace_no_newline(l);
        if (l->pos >= l->len) break;

        char c = peek(l);
        int ln = l->line, co = l->col;

        /* newline */
        if (c == '\n') { advance(l); push_token(l, TOK_NEWLINE, NULL, ln, co); continue; }

        /* line comment // */
        if (c == '/' && peek_ahead(l, 1) == '/') {
            advance(l); advance(l); skip_line_comment(l); continue;
        }
        /* block comment: slash-star ... star-slash */
        if (c == '/' && peek_ahead(l, 1) == '*') {
            advance(l); advance(l); skip_block_comment(l); continue;
        }

        /* string literal */
        if (c == '"') { if (lex_string(l) < 0) return -1; continue; }

        /* number */
        if (isdigit((unsigned char)c)) { if (lex_number(l) < 0) return -1; continue; }

        /* identifier / keyword */
        if (isalpha((unsigned char)c) || c == '_') {
            if (lex_ident_or_keyword(l) < 0) return -1;
            continue;
        }

        /* UI tag: only if < is followed by a letter (tag name) */
        if (c == '<' && isalpha((unsigned char)peek_ahead(l, 1))) {
            if (lex_ui_tag(l) < 0) return -1; continue;
        }

        /* sigils */
        if (c == '#') { advance(l); push_token(l, TOK_HASH, NULL, ln, co); continue; }
        if (c == '@') { advance(l); push_token(l, TOK_AT, NULL, ln, co); continue; }
        if (c == '$') { advance(l); push_token(l, TOK_DOLLAR, NULL, ln, co); continue; }
        if (c == '^') { advance(l); push_token(l, TOK_CARET, NULL, ln, co); continue; }

        /* two-char operators */
        if (c == '=' && peek_ahead(l, 1) == '=') {
            advance(l); advance(l);
            push_token(l, TOK_EQ_EQ, NULL, ln, co); continue;
        }
        if (c == '<' && peek_ahead(l, 1) == '=') {
            advance(l); advance(l);
            push_token(l, TOK_LE, NULL, ln, co); continue;
        }
        if (c == '>' && peek_ahead(l, 1) == '=') {
            advance(l); advance(l);
            push_token(l, TOK_GE, NULL, ln, co); continue;
        }
        if (c == '!' && peek_ahead(l, 1) == '=') {
            advance(l); advance(l);
            push_token(l, TOK_NEQ, NULL, ln, co); continue;
        }

        /* ---- compound operators (check before single-char) ---- */
        /* ** power */
        if (c == '*' && peek_ahead(l, 1) == '*') {
            advance(l); advance(l);
            push_token(l, TOK_POWER, NULL, ln, co); continue;
        }
        /* += -= *= /= */
        if (c == '+' && peek_ahead(l, 1) == '=') {
            advance(l); advance(l);
            push_token(l, TOK_PLUS_ASSIGN, NULL, ln, co); continue;
        }
        if (c == '-' && peek_ahead(l, 1) == '=') {
            advance(l); advance(l);
            push_token(l, TOK_MINUS_ASSIGN, NULL, ln, co); continue;
        }
        if (c == '*' && peek_ahead(l, 1) == '=') {
            advance(l); advance(l);
            push_token(l, TOK_MUL_ASSIGN, NULL, ln, co); continue;
        }
        if (c == '/' && peek_ahead(l, 1) == '=') {
            advance(l); advance(l);
            push_token(l, TOK_DIV_ASSIGN, NULL, ln, co); continue;
        }
        /* << >> (pointer open/close AND bit shift — context resolved in parser) */
        if (c == '<' && peek_ahead(l, 1) == '<') {
            advance(l); advance(l);
            push_token(l, TOK_LSHIFT, NULL, ln, co); continue;
        }
        if (c == '>' && peek_ahead(l, 1) == '>') {
            advance(l); advance(l);
            push_token(l, TOK_RSHIFT, NULL, ln, co); continue;
        }
        /* ^  (bitwise XOR — distinct from caret sigil only when not at expr start) */
        if (c == '^' && peek_ahead(l, 1) != ' ' && peek_ahead(l, 1) != '\0') {
            /* If previous meaningful token was an operator/open, it's sigil (^var).
               Otherwise it's XOR. Parser handles disambiguation. */
            advance(l);
            push_token(l, TOK_CARET, NULL, ln, co); continue;
        }
        /* include< ... > — MBLL library include */
        if (c == 'i' && strncmp(l->src + l->pos, "include<", 8) == 0) {
            /* advance past 'include<' */
            for (int ii = 0; ii < 8; ii++) advance(l);
            size_t lib_start = l->pos;
            while (l->pos < l->len && peek(l) != '>') advance(l);
            size_t lib_end = l->pos;
            if (peek(l) == '>') advance(l); /* consume '>' */
            char *lib_name = strndup_safe(l->src + lib_start, lib_end - lib_start);
            push_token(l, TOK_INCLUDE_LIB, lib_name, ln, co);
            free(lib_name);
            continue;
        }

        /* single-char operators */
        switch (c) {
            case '=': advance(l); push_token(l, TOK_ASSIGN, NULL, ln, co); break;
            case '+': advance(l); push_token(l, TOK_PLUS,   NULL, ln, co); break;
            case '-': advance(l); push_token(l, TOK_MINUS,  NULL, ln, co); break;
            case '*': advance(l); push_token(l, TOK_STAR,   NULL, ln, co); break;
            case '/': advance(l); push_token(l, TOK_SLASH,  NULL, ln, co); break;
            case '>': advance(l); push_token(l, TOK_GT,     NULL, ln, co); break;
            case '<': advance(l); push_token(l, TOK_LT,     NULL, ln, co); break;
            case '.': advance(l); push_token(l, TOK_DOT,    NULL, ln, co); break;
            case ',': advance(l); push_token(l, TOK_COMMA,  NULL, ln, co); break;
            case ';': advance(l); push_token(l, TOK_SEMICOLON, NULL, ln, co); break;
            case ':': advance(l); push_token(l, TOK_COLON,  NULL, ln, co); break;
            case '(': advance(l); push_token(l, TOK_LPAREN, NULL, ln, co); break;
            case ')': advance(l); push_token(l, TOK_RPAREN, NULL, ln, co); break;
            case '{': advance(l); push_token(l, TOK_DATA_OPEN,  NULL, ln, co); break;
            case '}': advance(l); push_token(l, TOK_DATA_CLOSE, NULL, ln, co); break;
            case '[': advance(l); push_token(l, TOK_BRACKET_OPEN,  NULL, ln, co); break;
            case ']': advance(l); push_token(l, TOK_BRACKET_CLOSE, NULL, ln, co); break;
            case '!': advance(l); push_token(l, TOK_BANG,   NULL, ln, co); break;
            case '&': advance(l); push_token(l, TOK_BITAND, NULL, ln, co); break;
            case '|': advance(l); push_token(l, TOK_BITOR,  NULL, ln, co); break;
            case '%': advance(l); push_token(l, TOK_PERCENT,NULL, ln, co); break;
            case '_': advance(l); push_token(l, TOK_UNDERSCORE, NULL, ln, co); break;
            case '~': advance(l); push_token(l, TOK_BITNOT, NULL, ln, co); break;
            default:
                advance(l);
                push_token(l, TOK_UNKNOWN, NULL, ln, co);
                break;
        }
    }
    push_token(l, TOK_EOF, NULL, l->line, l->col);
    return 0;
}

const char *token_kind_name(TokenKind k) {
    switch (k) {
        case TOK_INT_LITERAL:    return "INT";
        case TOK_FLOAT_LITERAL:  return "FLOAT";
        case TOK_STRING_LITERAL: return "STRING";
        case TOK_BOOL_LITERAL:   return "BOOL";
        case TOK_IDENT:          return "IDENT";
        case TOK_INC:            return "inc";
        case TOK_ENDINC:         return "endinc";
        case TOK_PR:             return "pr";
        case TOK_IF:             return "if";
        case TOK_ELSEIF:         return "elseif";
        case TOK_ELSE:           return "else";
        case TOK_DO:             return "do";
        case TOK_FOR:            return "for";
        case TOK_RANGE:          return "range";
        case TOK_LOOP:           return "loop";
        case TOK_WHILE:          return "while";
        case TOK_BREAK:          return "break";
        case TOK_RETURN:         return "return";
        case TOK_CONTINUE:       return "continue";
        case TOK_WAIT:           return "wait";
        case TOK_WAIT_FOR:       return "wait for";
        case TOK_USE:            return "use";
        case TOK_RESPONSE:       return "response";
        case TOK_INPUTXT:        return "inputxt";
        case TOK_DEV:            return "dev";
        case TOK_IMPORT:         return "import";
        case TOK_THREAD:         return "thread";
        case TOK_CONST:          return "const";
        case TOK_CLICKED:        return "clicked";
        case TOK_SEND:           return "send";
        case TOK_READFILE:       return "readfile";
        case TOK_CREATEWINDOW:   return "createWindow";
        case TOK_HASH:           return "#";
        case TOK_AT:             return "@";
        case TOK_DOLLAR:         return "$";
        case TOK_CARET:          return "^";
        case TOK_CARGO_M:        return "CARGO_m";
        case TOK_CLEAR_CARGO:    return "CLEAR_cargo";
        case TOK_AUTOCLEAR:      return "AUTOCLEAR_cargo";
        case TOK_USING_CPU:      return "using(cpu)";
        case TOK_MOV:            return "MOV";
        case TOK_EAX:            return "EAX";
        case TOK_STRING_TYPE:    return "string:";
        case TOK_TOKEN_STRING:   return "TOKENstring";
        case TOK_SPLIT_STRING:   return "splitstring";
        case TOK_COMBINE_STRING: return "combinestring";
        case TOK_EXTERN_M:       return "extern m";
        case TOK_EXTERN_C:       return "extern c";
        case TOK_ASSIGN:         return "=";
        case TOK_EQ_EQ:          return "==";
        case TOK_PLUS:           return "+";
        case TOK_MINUS:          return "-";
        case TOK_STAR:           return "*";
        case TOK_SLASH:          return "/";
        case TOK_LT:             return "<";
        case TOK_GT:             return ">";
        case TOK_LE:             return "<=";
        case TOK_GE:             return ">=";
        case TOK_NEQ:            return "!=";
        case TOK_DOT:            return ".";
        case TOK_COMMA:          return ",";
        case TOK_SEMICOLON:      return ";";
        case TOK_COLON:          return ":";
        case TOK_LPAREN:         return "(";
        case TOK_RPAREN:         return ")";
        case TOK_DATA_OPEN:      return "{";
        case TOK_DATA_CLOSE:     return "}";
        case TOK_BRACKET_OPEN:   return "[";
        case TOK_BRACKET_CLOSE:  return "]";
        case TOK_BANG:           return "!";
        case TOK_AMP:            return "&";
        case TOK_PIPE:           return "|";
        case TOK_PERCENT:        return "%";
        case TOK_UI_OPEN:        return "<UI_OPEN>";
        case TOK_UI_CLOSE:       return "<UI_CLOSE>";
        case TOK_UI_TAG:         return "<UI_TAG>";
        case TOK_UI_ENDUI:       return "<endui>";
        case TOK_UI_SCRIPT:      return "<script#>";
        case TOK_UI_ENDSC:       return "<endsc>";
        case TOK_UI_HEAD:        return "<head>";
        case TOK_UI_BUTTON:      return "<button>";
        case TOK_UI_DIV:         return "<div>";
        case TOK_UI_DIV_CLOSE:   return "</div>";
        case TOK_UI_COLOR:       return "<color>";
        case TOK_UI_GRADIENT:    return "<gradient>";
        case TOK_UI_INPUT:       return "<input>";
        case TOK_UI_PNG:         return "<pngdocs>";
        case TOK_OPEN_PAGE:      return "openpage";
        /* v2.0 OOP */
        case TOK_CLASS:          return "class";
        case TOK_NEW:            return "new";
        case TOK_THIS:           return "this";
        case TOK_EXTENDS:        return "extends";
        case TOK_INTERFACE:      return "interface";
        case TOK_IMPLEMENTS:     return "implements";
        case TOK_PUB:            return "pub";
        case TOK_PRIV:           return "priv";
        case TOK_PROT:           return "prot";
        case TOK_STATIC:         return "static";
        case TOK_ABSTRACT:       return "abstract";
        case TOK_FINAL:          return "final";
        case TOK_OVERRIDE:       return "override";
        case TOK_VIRTUAL:        return "virtual";
        case TOK_INSTANCEOF:     return "instanceof";
        case TOK_SUPER:          return "super";
        /* v2.0 Types */
        case TOK_TYPE_INT:       return "int";
        case TOK_TYPE_FLOAT:     return "float";
        case TOK_TYPE_STRING:    return "str";
        case TOK_TYPE_BOOL2:     return "bool";
        case TOK_TYPE_VOID:      return "void";
        case TOK_TYPE_BYTE:      return "byte";
        case TOK_TYPE_LONG:      return "long";
        case TOK_TYPE_DOUBLE:    return "double";
        case TOK_TYPE_CHAR:      return "char";
        case TOK_TYPE_AUTO:      return "auto";
        case TOK_NULLABLE:       return "nullable";
        case TOK_AS:             return "as";
        /* v2.0 Data structures */
        case TOK_ARRAY_KW:       return "array";
        case TOK_MAP_KW:         return "map";
        case TOK_SET_KW:         return "set";
        case TOK_TUPLE_KW:       return "tuple";
        case TOK_STRUCT_KW:      return "struct";
        case TOK_ENUM_KW:        return "enum";
        /* v2.0 Error handling */
        case TOK_TRY:            return "try";
        case TOK_CATCH:          return "catch";
        case TOK_FINALLY:        return "finally";
        case TOK_THROW:          return "throw";
        case TOK_RAISES:         return "raises";
        /* v2.0 MVM */
        case TOK_MVM_SPAWN:      return "mvm_spawn";
        case TOK_MVM_SYNC:       return "mvm_sync";
        case TOK_MVM_PIPE:       return "mvm_pipe";
        case TOK_MVM_KILL:       return "mvm_kill";
        case TOK_MVM_CORE:       return "mvm_core";
        case TOK_MVM_OPT:        return "mvm_opt";
        case TOK_MVM_NATIVE:     return "mvm_native";
        case TOK_MVM_INLINE:     return "mvm_inline";
        /* v2.0 MBLL */
        case TOK_EXTERN_FILE:    return "externFile";
        case TOK_INCLUDE_LIB:    return "include<>";
        case TOK_WRITE_TRAM:     return "WriteTRam";
        case TOK_CLEAN_TRAM:     return "cleanTram";
        case TOK_FUNC_KW:        return "func";
        case TOK_CLASS_INT:      return "classInt";
        case TOK_SUBCLASS:       return "subclass";
        /* v2.0 Operators */
        case TOK_POWER:          return "**";
        case TOK_LSHIFT:         return "<<";
        case TOK_RSHIFT:         return ">>";
        case TOK_BITAND:         return "&";
        case TOK_BITOR:          return "|";
        case TOK_BITXOR:         return "^";
        case TOK_BITNOT:         return "~";
        case TOK_PLUS_ASSIGN:    return "+=";
        case TOK_MINUS_ASSIGN:   return "-=";
        case TOK_MUL_ASSIGN:     return "*=";
        case TOK_DIV_ASSIGN:     return "/=";
        case TOK_ARRAY_DECL:     return "$array";
        case TOK_POINTER_OPEN:   return ">>";
        case TOK_POINTER_CLOSE:  return "<<";
        /* v2.0 Async */
        case TOK_ASYNC:          return "async";
        case TOK_AWAIT:          return "await";
        case TOK_CHAN:           return "chan";
        case TOK_MUTEX:          return "mutex";
        case TOK_LOCK:           return "lock";
        case TOK_UNLOCK:         return "unlock";
        case TOK_ATOMIC:         return "atomic";
        /* v2.0 Meta */
        case TOK_MACRO:          return "macro";
        case TOK_COMPTIME:       return "comptime";
        case TOK_TYPEOF:         return "typeof";
        case TOK_SIZEOF:         return "sizeof";
        case TOK_ALIGNOF:        return "alignof";
        case TOK_INLINE_KW:      return "inline";
        case TOK_NOINLINE:       return "noinline";
        case TOK_PACKED:         return "packed";
        case TOK_EXPORT:         return "export";
        case TOK_NORETURN:       return "noreturn";
        /* v2.0 math/str/file/net/sys/debug namespace markers */
        case TOK_MATH_ABS:       return "math.*";
        case TOK_STR_LEN:        return "str.*";
        case TOK_FILE_WRITE:     return "file.*";
        case TOK_NET_GET:        return "net.*";
        case TOK_SYS_EXEC:       return "sys.*";
        case TOK_DEBUG_LOG:      return "debug.*";
        case TOK_NEWLINE:        return "NEWLINE";
        case TOK_EOF:            return "EOF";
        default:                 return "UNKNOWN";
    }
}

void lexer_dump_tokens(const Lexer *l) {
    for (size_t i = 0; i < l->token_count; i++) {
        const Token *t = &l->tokens[i];
        if (t->kind == TOK_NEWLINE) continue;
        printf("[%3zu] %-20s  val=%-20s  line=%d col=%d\n",
               i, token_kind_name(t->kind),
               t->value ? t->value : "(null)",
               t->line, t->col);
    }
}
