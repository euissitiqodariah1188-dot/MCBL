#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
   McBL# Parser  –  Recursive-descent
   ----------------------------------------------------------------------- */

Parser *parser_create(const Token *tokens, size_t count) {
    Parser *p = (Parser *)malloc(sizeof(Parser));
    if (!p) return NULL;
    p->tokens = tokens;
    p->count  = count;
    p->pos    = 0;
    p->error  = 0;
    p->errmsg[0] = '\0';
    return p;
}

void parser_destroy(Parser *p) {
    if (!p) return;
    free(p);
}

/* ---- helpers ---------------------------------------------------------- */

static const Token *cur(const Parser *p) {
    if (p->pos >= p->count) return &p->tokens[p->count - 1]; /* EOF */
    return &p->tokens[p->pos];
}

static const Token *peek_at(const Parser *p, size_t off) {
    size_t idx = p->pos + off;
    if (idx >= p->count) return &p->tokens[p->count - 1];
    return &p->tokens[idx];
}

static void advance_p(Parser *p) {
    if (p->pos < p->count) p->pos++;
}

static int check(const Parser *p, TokenKind k) {
    return cur(p)->kind == k;
}

static void skip_newlines(Parser *p) {
    while (check(p, TOK_NEWLINE)) advance_p(p);
}

static void parse_error(Parser *p, const char *msg) {
    if (p->error) return;
    p->error = 1;
    const Token *t = cur(p);
    snprintf(p->errmsg, sizeof(p->errmsg),
             "McBL# ParseError at line %d col %d: %s (got '%s')",
             t->line, t->col, msg, token_kind_name(t->kind));
}

static int expect(Parser *p, TokenKind k) {
    skip_newlines(p);
    if (check(p, k)) { advance_p(p); return 1; }
    char buf[128];
    snprintf(buf, sizeof(buf), "expected '%s'", token_kind_name(k));
    parse_error(p, buf);
    return 0;
}

/* Forward declarations */
static AstNode *parse_stmt(Parser *p);
static AstNode *parse_expr(Parser *p);
static AstNode *parse_expr_primary(Parser *p);
static AstNode *parse_expr_unary(Parser *p);
static AstNode *parse_expr_mul(Parser *p);
static AstNode *parse_expr_add(Parser *p);
static AstNode *parse_expr_cmp(Parser *p);
static AstNode *parse_expr_logic(Parser *p);
static void     parse_block_into(Parser *p, AstList *list, TokenKind until1, TokenKind until2);
static AstNode *parse_inc_decl(Parser *p);
static AstNode *parse_dev_decl(Parser *p);
static AstNode *parse_if_stmt(Parser *p);
static AstNode *parse_for_stmt(Parser *p);
static AstNode *parse_loop_stmt(Parser *p);
static AstNode *parse_while_stmt(Parser *p);

/* ---- expression parsing ----------------------------------------------- */

/* v2.0: ns_call reuses AST_MATH_CALL node type */
#define AST_NS_CALL AST_MATH_CALL

static AstNode *parse_expr_primary(Parser *p) {
    skip_newlines(p);
    if (p->error) return NULL;
    const Token *t = cur(p);
    int ln = t->line;

    switch (t->kind) {
        case TOK_INT_LITERAL: {
            AstNode *n = ast_node_new(AST_EXPR_INT, ln);
            if (!n) return NULL;
            n->ival = t->value ? atoll(t->value) : 0;
            advance_p(p);
            return n;
        }
        case TOK_FLOAT_LITERAL: {
            AstNode *n = ast_node_new(AST_EXPR_FLOAT, ln);
            if (!n) return NULL;
            n->fval = t->value ? atof(t->value) : 0.0;
            advance_p(p);
            return n;
        }
        case TOK_STRING_LITERAL: {
            AstNode *n = ast_node_new(AST_EXPR_STRING, ln);
            if (!n) return NULL;
            n->sval = t->value ? strdup(t->value) : strdup("");
            if (!n->sval) { ast_node_free(n); return NULL; }
            advance_p(p);
            return n;
        }
        case TOK_BOOL_LITERAL: {
            AstNode *n = ast_node_new(AST_EXPR_BOOL, ln);
            if (!n) return NULL;
            n->bval = (t->value && strcmp(t->value, "true") == 0) ? 1 : 0;
            advance_p(p);
            return n;
        }
        case TOK_IDENT: {
            char *name = t->value ? strdup(t->value) : strdup("");
            if (!name) return NULL;
            advance_p(p);
            /* function call? */
            skip_newlines(p);
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                AstNode *n = ast_node_new(AST_EXPR_CALL, ln);
                if (!n) { free(name); return NULL; }
                n->call.target = name;
                ast_list_init(&n->call.args);
                skip_newlines(p);
                while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                    AstNode *arg = parse_expr(p);
                    if (!arg || p->error) {
                        ast_node_free(n);
                        return NULL;
                    }
                    ast_list_push(&n->call.args, arg);
                    skip_newlines(p);
                    if (check(p, TOK_COMMA)) advance_p(p);
                }
                expect(p, TOK_RPAREN);
                /* member access */
                while (check(p, TOK_DOT)) {
                    advance_p(p);
                    if (!check(p, TOK_IDENT)) { parse_error(p, "expected member name"); ast_node_free(n); return NULL; }
                    AstNode *member = ast_node_new(AST_EXPR_MEMBER, ln);
                    if (!member) { ast_node_free(n); return NULL; }
                    member->binop.left  = n;
                    member->sval = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                    advance_p(p);
                    n = member;
                }
                return n;
            }
            /* index access */
            if (check(p, TOK_BRACKET_OPEN)) {
                advance_p(p);
                AstNode *idx = parse_expr(p);
                AstNode *n = ast_node_new(AST_EXPR_INDEX, ln);
                if (!n) { free(name); ast_node_free(idx); return NULL; }
                AstNode *base = ast_node_new(AST_EXPR_IDENT, ln);
                if (!base) { free(name); ast_node_free(idx); ast_node_free(n); return NULL; }
                base->sval = name;
                n->binop.left  = base;
                n->binop.right = idx;
                expect(p, TOK_BRACKET_CLOSE);
                return n;
            }
            /* plain ident */
            AstNode *n = ast_node_new(AST_EXPR_IDENT, ln);
            if (!n) { free(name); return NULL; }
            n->sval = name;
            /* member access on ident */
            while (check(p, TOK_DOT)) {
                advance_p(p);
                if (!check(p, TOK_IDENT)) { parse_error(p, "expected member"); ast_node_free(n); return NULL; }
                AstNode *member = ast_node_new(AST_EXPR_MEMBER, ln);
                if (!member) { ast_node_free(n); return NULL; }
                member->binop.left = n;
                member->sval = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                advance_p(p);
                n = member;
            }
            return n;
        }
        case TOK_LPAREN: {
            advance_p(p);
            AstNode *inner = parse_expr(p);
            expect(p, TOK_RPAREN);
            return inner;
        }
        case TOK_BANG: {
            advance_p(p);
            AstNode *operand = parse_expr_unary(p);
            AstNode *n = ast_node_new(AST_EXPR_UNOP, ln);
            if (!n) { ast_node_free(operand); return NULL; }
            n->binop.op    = OP_NOT;
            n->binop.left  = operand;
            n->binop.right = NULL;
            return n;
        }
        case TOK_MINUS: {
            advance_p(p);
            AstNode *operand = parse_expr_unary(p);
            AstNode *n = ast_node_new(AST_EXPR_UNOP, ln);
            if (!n) { ast_node_free(operand); return NULL; }
            n->binop.op    = OP_SUB;
            n->binop.left  = operand;
            n->binop.right = NULL;
            return n;
        }
        /* EAX as expression */
        case TOK_EAX: {
            AstNode *n = ast_node_new(AST_EXPR_IDENT, ln);
            if (!n) return NULL;
            n->sval = strdup("__mcbl_eax");
            advance_p(p);
            return n;
        }
        /* Keyword-builtins that can appear as callable expressions */
        case TOK_INPUTXT:
        case TOK_READFILE:
        case TOK_CREATEWINDOW:
        case TOK_SPLIT_STRING:
        case TOK_COMBINE_STRING:
        case TOK_RESPONSE: {
            const char *fn_name = t->value ? t->value : "inputxt";
            advance_p(p);
            skip_newlines(p);
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                AstNode *n = ast_node_new(AST_EXPR_CALL, ln);
                if (!n) return NULL;
                n->call.target = strdup(fn_name);
                ast_list_init(&n->call.args);
                skip_newlines(p);
                while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                    /* For inputxt, treat unquoted identifiers as string literals */
                    if (strcmp(fn_name, "inputxt") == 0 && check(p, TOK_IDENT)) {
                        AstNode *arg = ast_node_new(AST_EXPR_STRING, ln);
                        if (!arg) { ast_node_free(n); return NULL; }
                        arg->sval = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                        ast_list_push(&n->call.args, arg);
                        advance_p(p);
                    } else {
                        AstNode *arg = parse_expr(p);
                        if (!arg || p->error) { ast_node_free(n); return NULL; }
                        ast_list_push(&n->call.args, arg);
                    }
                    skip_newlines(p);
                    if (check(p, TOK_COMMA)) advance_p(p);
                }
                expect(p, TOK_RPAREN);
                return n;
            }
            /* bare keyword as ident */
            AstNode *n = ast_node_new(AST_EXPR_IDENT, ln);
            if (!n) return NULL;
            n->sval = strdup(fn_name);
            return n;
        }
        /* ---- v2.0: namespace call tokens: math.* str.* file.* net.* sys.* debug.* ---- */
        case TOK_MATH_ABS:
        case TOK_STR_LEN:
        case TOK_FILE_WRITE:
        case TOK_NET_GET:
        case TOK_SYS_EXEC:
        case TOK_DEBUG_LOG: {
            /* Token value is "ns.method" e.g. "math.sqrt" */
            const char *full_name = t->value ? t->value : "math.unknown";
            advance_p(p);
            AstNode *n = ast_node_new(AST_NS_CALL, ln);
            if (!n) return NULL;
            /* Split "ns.method" */
            char ns_buf[64]   = {0};
            char func_buf[64] = {0};
            const char *dot = strchr(full_name, '.');
            if (dot) {
                size_t ns_len = (size_t)(dot - full_name);
                if (ns_len >= sizeof(ns_buf)) ns_len = sizeof(ns_buf) - 1;
                strncpy(ns_buf, full_name, ns_len);
                strncpy(func_buf, dot + 1, sizeof(func_buf) - 1);
            } else {
                strncpy(ns_buf, full_name, sizeof(ns_buf) - 1);
                strncpy(func_buf, "call",  sizeof(func_buf) - 1);
            }
            n->ns_call.ns   = strdup(ns_buf);
            n->ns_call.func = strdup(func_buf);
            ast_list_init(&n->ns_call.args);
            /* Parse optional argument list: (arg, arg, ...) */
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                    AstNode *arg = parse_expr(p);
                    if (arg) ast_list_push(&n->ns_call.args, arg);
                    if (check(p, TOK_COMMA)) advance_p(p);
                }
                if (check(p, TOK_RPAREN)) advance_p(p);
            }
            return n;
        }
        /* ---- v2.0: WriteTRam(mb) ---- */
        case TOK_WRITE_TRAM: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_WRITE_TRAM_STMT, ln);
            if (!n) return NULL;
            n->tram.mb = 256; /* default */
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                if (check(p, TOK_INT_LITERAL)) {
                    n->tram.mb = (long long)atoll(cur(p)->value);
                    advance_p(p);
                }
                if (check(p, TOK_RPAREN)) advance_p(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }
        /* ---- v2.0: cleanTram() ---- */
        case TOK_CLEAN_TRAM: {
            advance_p(p);
            if (check(p, TOK_LPAREN)) advance_p(p);
            if (check(p, TOK_RPAREN)) advance_p(p);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            AstNode *n = ast_node_new(AST_CLEAN_TRAM_STMT, ln);
            return n;
        }
        /* ---- v2.0: externFile(name) ---- */
        case TOK_EXTERN_FILE: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_EXTERN_FILE_STMT, ln);
            if (!n) return NULL;
            n->mbll.filename = NULL;
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                if (check(p, TOK_IDENT) || check(p, TOK_STRING_LITERAL)) {
                    n->mbll.filename = strdup(cur(p)->value ? cur(p)->value : "");
                    advance_p(p);
                }
                if (check(p, TOK_RPAREN)) advance_p(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }
        /* ---- v2.0: include<lib.cll> ---- */
        case TOK_INCLUDE_LIB: {
            const char *lib_name = t->value;
            advance_p(p);
            AstNode *n = ast_node_new(AST_INCLUDE_LIB_STMT, ln);
            if (!n) return NULL;
            n->mbll.filename = lib_name ? strdup(lib_name) : NULL;
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }
        /* ---- v2.0: func(name) ---- */
        case TOK_FUNC_KW: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_FUNC_DECL, ln);
            if (!n) return NULL;
            n->dev.name = NULL;
            ast_list_init(&n->dev.params);
            ast_list_init(&n->dev.body);
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                if (check(p, TOK_IDENT)) {
                    n->dev.name = strdup(cur(p)->value);
                    advance_p(p);
                }
                if (check(p, TOK_RPAREN)) advance_p(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            /* body follows — just parse the next statement block up to next inc/endinc */
            while (!check(p, TOK_EOF) && !check(p, TOK_INC) && !check(p, TOK_ENDINC)) {
                skip_newlines(p);
                if (check(p, TOK_EOF) || check(p, TOK_INC) || check(p, TOK_ENDINC)) break;
                AstNode *stmt = parse_stmt(p);
                if (stmt) ast_list_push(&n->dev.body, stmt);
                else break;
            }
            return n;
        }
        /* ---- v2.0: new ClassName() ---- */
        case TOK_NEW: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_NEW_EXPR, ln);
            if (!n) return NULL;
            ast_list_init(&n->new_expr.args);
            if (check(p, TOK_IDENT)) {
                n->new_expr.class_name = strdup(cur(p)->value);
                advance_p(p);
            } else {
                n->new_expr.class_name = strdup("unknown");
            }
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                    AstNode *arg = parse_expr(p);
                    if (arg) ast_list_push(&n->new_expr.args, arg);
                    if (check(p, TOK_COMMA)) advance_p(p);
                }
                if (check(p, TOK_RPAREN)) advance_p(p);
            }
            return n;
        }
        default:
            parse_error(p, "unexpected token in expression");
            return NULL;
    }
}


static AstNode *parse_expr_unary(Parser *p) {
    return parse_expr_primary(p);
}

static AstNode *parse_expr_mul(Parser *p) {
    AstNode *left = parse_expr_unary(p);
    if (!left || p->error) return left;

    skip_newlines(p);
    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT)) {
        BinOp op = check(p, TOK_STAR)   ? OP_MUL :
                   check(p, TOK_SLASH)  ? OP_DIV : OP_MOD;
        int ln = cur(p)->line;
        advance_p(p);
        AstNode *right = parse_expr_unary(p);
        if (!right || p->error) { ast_node_free(left); return NULL; }
        AstNode *n = ast_node_new(AST_EXPR_BINOP, ln);
        if (!n) { ast_node_free(left); ast_node_free(right); return NULL; }
        n->binop.op    = op;
        n->binop.left  = left;
        n->binop.right = right;
        left = n;
        skip_newlines(p);
    }
    return left;
}

static AstNode *parse_expr_add(Parser *p) {
    AstNode *left = parse_expr_mul(p);
    if (!left || p->error) return left;

    skip_newlines(p);
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        BinOp op = check(p, TOK_PLUS) ? OP_ADD : OP_SUB;
        int ln = cur(p)->line;
        advance_p(p);
        AstNode *right = parse_expr_mul(p);
        if (!right || p->error) { ast_node_free(left); return NULL; }
        AstNode *n = ast_node_new(AST_EXPR_BINOP, ln);
        if (!n) { ast_node_free(left); ast_node_free(right); return NULL; }
        n->binop.op    = op;
        n->binop.left  = left;
        n->binop.right = right;
        left = n;
        skip_newlines(p);
    }
    return left;
}

static AstNode *parse_expr_cmp(Parser *p) {
    AstNode *left = parse_expr_add(p);
    if (!left || p->error) return left;

    skip_newlines(p);
    while (check(p, TOK_LT) || check(p, TOK_GT) || check(p, TOK_LE) ||
           check(p, TOK_GE) || check(p, TOK_EQ_EQ) || check(p, TOK_NEQ)) {
        BinOp op;
        switch (cur(p)->kind) {
            case TOK_LT:   op = OP_LT;  break;
            case TOK_GT:   op = OP_GT;  break;
            case TOK_LE:   op = OP_LE;  break;
            case TOK_GE:   op = OP_GE;  break;
            case TOK_EQ_EQ:op = OP_EQ;  break;
            default:       op = OP_NEQ; break;
        }
        int ln = cur(p)->line;
        advance_p(p);
        AstNode *right = parse_expr_add(p);
        if (!right || p->error) { ast_node_free(left); return NULL; }
        AstNode *n = ast_node_new(AST_EXPR_BINOP, ln);
        if (!n) { ast_node_free(left); ast_node_free(right); return NULL; }
        n->binop.op    = op;
        n->binop.left  = left;
        n->binop.right = right;
        left = n;
        skip_newlines(p);
    }
    return left;
}

static AstNode *parse_expr_logic(Parser *p) {
    AstNode *left = parse_expr_cmp(p);
    if (!left || p->error) return left;

    skip_newlines(p);
    while (check(p, TOK_AMP) || check(p, TOK_PIPE)) {
        BinOp op = check(p, TOK_AMP) ? OP_AND : OP_OR;
        int ln = cur(p)->line;
        advance_p(p);
        AstNode *right = parse_expr_cmp(p);
        if (!right || p->error) { ast_node_free(left); return NULL; }
        AstNode *n = ast_node_new(AST_EXPR_BINOP, ln);
        if (!n) { ast_node_free(left); ast_node_free(right); return NULL; }
        n->binop.op    = op;
        n->binop.left  = left;
        n->binop.right = right;
        left = n;
        skip_newlines(p);
    }
    return left;
}

static AstNode *parse_expr(Parser *p) {
    return parse_expr_logic(p);
}

/* ---- block parsing ---------------------------------------------------- */

static void parse_block_into(Parser *p, AstList *list,
                             TokenKind until1, TokenKind until2) {
    while (!p->error) {
        skip_newlines(p);
        TokenKind k = cur(p)->kind;
        if (k == TOK_EOF || k == until1 || k == until2) break;
        /* Stop at closing brace if not expected here */
        if (k == TOK_DATA_CLOSE && until1 != TOK_DATA_CLOSE && until2 != TOK_DATA_CLOSE) break;
        /* Stop at continue if not expected here */
        if (k == TOK_CONTINUE && until1 != TOK_CONTINUE && until2 != TOK_CONTINUE) break;
        AstNode *stmt = parse_stmt(p);
        if (!stmt) break;
        ast_list_push(list, stmt);
    }
}

/* ---- statement parsing ------------------------------------------------ */

static AstNode *parse_var_decl(Parser *p, char sigil, AstKind kind) {
    int ln = cur(p)->line;
    if (!check(p, TOK_IDENT)) { parse_error(p, "expected variable name"); return NULL; }
    char *name = cur(p)->value ? strdup(cur(p)->value) : strdup("");
    if (!name) return NULL;
    advance_p(p);
    expect(p, TOK_ASSIGN);
    if (p->error) { free(name); return NULL; }
    AstNode *val = parse_expr(p);
    if (!val || p->error) { free(name); return NULL; }

    AstNode *n = ast_node_new(kind, ln);
    if (!n) { free(name); ast_node_free(val); return NULL; }
    n->var.name  = name;
    n->var.value = val;
    n->var.sigil = sigil;
    return n;
}

static AstNode *parse_inc_decl(Parser *p) {
    int ln = cur(p)->line;
    /* consume 'inc' */
    advance_p(p);
    expect(p, TOK_LPAREN);
    if (p->error) return NULL;

    /* collect inc name (everything until RPAREN) */
    char name_buf[256] = {0};
    size_t nb = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF) && nb < 255) {
        if (cur(p)->value)
            nb += (size_t)snprintf(name_buf + nb, 255 - nb, "%s ", cur(p)->value);
        advance_p(p);
    }
    /* trim trailing space */
    while (nb > 0 && name_buf[nb - 1] == ' ') name_buf[--nb] = '\0';
    expect(p, TOK_RPAREN);
    if (p->error) return NULL;

    /* optional semicolon */
    skip_newlines(p);
    if (check(p, TOK_SEMICOLON)) advance_p(p);

    AstNode *n = ast_node_new(AST_INC_DECL, ln);
    if (!n) return NULL;
    n->inc.name = strdup(name_buf);
    if (!n->inc.name) { ast_node_free(n); return NULL; }
    ast_list_init(&n->inc.body);

    parse_block_into(p, &n->inc.body, TOK_ENDINC, TOK_EOF);
    skip_newlines(p);
    if (check(p, TOK_ENDINC)) {
        advance_p(p);
        if (check(p, TOK_SEMICOLON)) advance_p(p);
    }
    return n;
}

static AstNode *parse_dev_decl(Parser *p) {
    int ln = cur(p)->line;
    advance_p(p); /* consume 'dev' */
    if (!check(p, TOK_IDENT)) { parse_error(p, "expected function name after dev"); return NULL; }
    char *name = cur(p)->value ? strdup(cur(p)->value) : strdup("");
    if (!name) return NULL;
    advance_p(p);
    expect(p, TOK_LPAREN);
    if (p->error) { free(name); return NULL; }

    AstNode *n = ast_node_new(AST_DEV_DECL, ln);
    if (!n) { free(name); return NULL; }
    n->dev.name = name;
    ast_list_init(&n->dev.params);
    ast_list_init(&n->dev.body);

    /* params */
    skip_newlines(p);
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        if (check(p, TOK_IDENT)) {
            AstNode *param = ast_node_new(AST_EXPR_IDENT, ln);
            if (!param) { ast_node_free(n); return NULL; }
            param->sval = cur(p)->value ? strdup(cur(p)->value) : strdup("");
            ast_list_push(&n->dev.params, param);
            advance_p(p);
        }
        if (check(p, TOK_COMMA)) advance_p(p);
        skip_newlines(p);
    }
    expect(p, TOK_RPAREN);
    if (p->error) { ast_node_free(n); return NULL; }

    /* body: optional braces or until next dev/inc/endinc */
    skip_newlines(p);
    int has_brace = 0;
    if (check(p, TOK_DATA_OPEN)) { advance_p(p); has_brace = 1; }

    if (has_brace)
        parse_block_into(p, &n->dev.body, TOK_DATA_CLOSE, TOK_EOF);
    else
        parse_block_into(p, &n->dev.body, TOK_DEV, TOK_ENDINC);

    if (has_brace) {
        skip_newlines(p);
        if (check(p, TOK_DATA_CLOSE)) advance_p(p);
    }
    return n;
}

static AstNode *parse_if_stmt(Parser *p) {
    int ln = cur(p)->line;
    advance_p(p); /* consume 'if' */

    /* McBL# if supports "if==else==if" chain notation via == */
    /* Simple: parse condition expression */
    AstNode *cond = parse_expr(p);
    if (!cond || p->error) return NULL;

    AstNode *n = ast_node_new(AST_IF_STMT, ln);
    if (!n) { ast_node_free(cond); return NULL; }
    n->if_stmt.cond = cond;
    ast_list_init(&n->if_stmt.then_body);
    ast_list_init(&n->if_stmt.elseif_conds);
    ast_list_init(&n->if_stmt.else_body);

    /* optional 'do' or '{' */
    skip_newlines(p);
    int has_brace = 0;
    if (check(p, TOK_DO)) { advance_p(p); if (check(p, TOK_SEMICOLON)) advance_p(p); }
    else if (check(p, TOK_DATA_OPEN)) { advance_p(p); has_brace = 1; }

    /* then body: until else/elseif/endinc/continue/data_close/EOF */
    if (has_brace) {
        parse_block_into(p, &n->if_stmt.then_body, TOK_DATA_CLOSE, TOK_DATA_CLOSE);
        skip_newlines(p);
        if (check(p, TOK_DATA_CLOSE)) advance_p(p); /* consume closing } */
    } else
        parse_block_into(p, &n->if_stmt.then_body, TOK_ELSE, TOK_ENDINC);

    /* elseif chain */
    skip_newlines(p);
    while (check(p, TOK_ELSEIF)) {
        advance_p(p);
        AstNode *ei_cond = parse_expr(p);
        if (!ei_cond || p->error) { ast_node_free(n); return NULL; }
        skip_newlines(p);
        if (check(p, TOK_DO)) { advance_p(p); if (check(p, TOK_SEMICOLON)) advance_p(p); }
        ast_list_push(&n->if_stmt.elseif_conds, ei_cond);
        AstNode *ei_body = ast_node_new(AST_BLOCK, ln);
        if (!ei_body) { ast_node_free(n); return NULL; }
        ast_list_init(&ei_body->block.stmts);
        parse_block_into(p, &ei_body->block.stmts, TOK_ELSE, TOK_ENDINC);
        if (check(p, TOK_DATA_CLOSE)) { /* let enclosing block handle } */ }
        ast_list_push(&n->if_stmt.elseif_conds, ei_body);
        skip_newlines(p);
    }

    /* else */
    skip_newlines(p);
    if (check(p, TOK_ELSE)) {
        advance_p(p);
        if (check(p, TOK_SEMICOLON)) advance_p(p);
        parse_block_into(p, &n->if_stmt.else_body, TOK_ENDINC, TOK_CONTINUE);
        if (check(p, TOK_DATA_CLOSE)) { /* let enclosing block handle } */ }
    }
    return n;
}

static AstNode *parse_for_stmt(Parser *p) {
    int ln = cur(p)->line;
    advance_p(p); /* consume 'for' */

    AstNode *n = ast_node_new(AST_FOR_STMT, ln);
    if (!n) return NULL;
    ast_list_init(&n->for_stmt.body);
    n->for_stmt.var_name = NULL;
    n->for_stmt.start    = NULL;
    n->for_stmt.end      = NULL;
    n->for_stmt.step     = NULL;

    /* for i range(start, end) */
    skip_newlines(p);
    if (check(p, TOK_IDENT)) {
        n->for_stmt.var_name = cur(p)->value ? strdup(cur(p)->value) : strdup("i");
        advance_p(p);
    } else {
        n->for_stmt.var_name = strdup("i");
    }
    if (!n->for_stmt.var_name) { ast_node_free(n); return NULL; }

    skip_newlines(p);
    if (check(p, TOK_RANGE)) {
        advance_p(p);
        expect(p, TOK_LPAREN);
        n->for_stmt.start = parse_expr(p);
        if (check(p, TOK_COMMA)) advance_p(p);
        n->for_stmt.end   = parse_expr(p);
        if (check(p, TOK_COMMA)) {
            advance_p(p);
            n->for_stmt.step = parse_expr(p);
        }
        expect(p, TOK_RPAREN);
    } else {
        /* plain for with = start, end */
        if (check(p, TOK_ASSIGN)) advance_p(p);
        n->for_stmt.start = parse_expr(p);
        if (check(p, TOK_COMMA) || check(p, TOK_SEMICOLON)) advance_p(p);
        n->for_stmt.end = parse_expr(p);
    }
    if (p->error) { ast_node_free(n); return NULL; }

    skip_newlines(p);
    if (check(p, TOK_DATA_OPEN)) advance_p(p);
    parse_block_into(p, &n->for_stmt.body, TOK_DATA_CLOSE, TOK_ENDINC);
    skip_newlines(p);
    if (check(p, TOK_DATA_CLOSE)) advance_p(p);
    return n;
}

static AstNode *parse_loop_stmt(Parser *p) {
    int ln = cur(p)->line;
    advance_p(p); /* consume 'loop' */
    AstNode *n = ast_node_new(AST_LOOP_STMT, ln);
    if (!n) return NULL;
    n->loop_stmt.cond = NULL;
    ast_list_init(&n->loop_stmt.body);

    skip_newlines(p);
    if (check(p, TOK_DATA_OPEN)) advance_p(p);
    parse_block_into(p, &n->loop_stmt.body, TOK_DATA_CLOSE, TOK_ENDINC);
    skip_newlines(p);
    if (check(p, TOK_DATA_CLOSE)) advance_p(p);
    return n;
}

static AstNode *parse_while_stmt(Parser *p) {
    int ln = cur(p)->line;
    advance_p(p);
    AstNode *cond = parse_expr(p);
    if (!cond || p->error) return NULL;
    AstNode *n = ast_node_new(AST_WHILE_STMT, ln);
    if (!n) { ast_node_free(cond); return NULL; }
    n->loop_stmt.cond = cond;
    ast_list_init(&n->loop_stmt.body);
    skip_newlines(p);
    if (check(p, TOK_DATA_OPEN)) advance_p(p);
    parse_block_into(p, &n->loop_stmt.body, TOK_DATA_CLOSE, TOK_ENDINC);
    skip_newlines(p);
    if (check(p, TOK_DATA_CLOSE)) advance_p(p);
    return n;
}

static AstNode *parse_thread_stmt(Parser *p) {
    int ln = cur(p)->line;
    advance_p(p);
    AstNode *n = ast_node_new(AST_THREAD_STMT, ln);
    if (!n) return NULL;
    ast_list_init(&n->thread.body);
    skip_newlines(p);
    if (check(p, TOK_DATA_OPEN)) advance_p(p);
    parse_block_into(p, &n->thread.body, TOK_DATA_CLOSE, TOK_ENDINC);
    skip_newlines(p);
    if (check(p, TOK_DATA_CLOSE)) advance_p(p);
    return n;
}

static char *collect_paren_content(Parser *p) {
    expect(p, TOK_LPAREN);
    if (p->error) return strdup("");
    char buf[512] = {0};
    size_t nb = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF) && nb < 511) {
        if (cur(p)->value)
            nb += (size_t)snprintf(buf + nb, 511 - nb, "%s", cur(p)->value);
        advance_p(p);
    }
    expect(p, TOK_RPAREN);
    return strdup(buf);
}

/* Parse a single statement */
static AstNode *parse_stmt(Parser *p) {
    skip_newlines(p);
    if (p->error) return NULL;
    const Token *t = cur(p);
    int ln = t->line;

    switch (t->kind) {
        /* inc declaration */
        case TOK_INC:
            return parse_inc_decl(p);

        /* dev function */
        case TOK_DEV:
            return parse_dev_decl(p);

        /* v2.0: WriteTRam, cleanTram, externFile, include<>, func sebagai statement */
        case TOK_WRITE_TRAM:
        case TOK_CLEAN_TRAM:
        case TOK_EXTERN_FILE:
        case TOK_INCLUDE_LIB:
        case TOK_FUNC_KW:
        case TOK_NEW:
        /* namespace call tokens juga bisa jadi statement (pr diabaikan returnnya) */
        case TOK_MATH_ABS:
        case TOK_STR_LEN:
        case TOK_FILE_WRITE:
        case TOK_NET_GET:
        case TOK_SYS_EXEC:
        case TOK_DEBUG_LOG: {
            AstNode *expr = parse_expr_primary(p);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return expr;
        }

        /* v2.0: $array sebagai statement */
        case TOK_ARRAY_DECL:
        case TOK_ARRAY_KW: {
            /* Sama seperti TOK_DOLLAR + array — handle langsung */
            advance_p(p);
            AstNode *node = ast_node_new(AST_ARRAY_DECL, ln);
            if (!node) return NULL;
            if (!check(p, TOK_IDENT)) {
                parse_error(p, "expected array name");
                ast_node_free(node);
                return NULL;
            }
            node->array_decl.name = strdup(cur(p)->value);
            advance_p(p);
            ast_list_init(&node->array_decl.elements);
            if (check(p, TOK_ASSIGN)) {
                advance_p(p);
                if (check(p, TOK_DATA_OPEN)) advance_p(p);
                while (!check(p, TOK_DATA_CLOSE) && !check(p, TOK_EOF)) {
                    AstNode *elem = parse_expr(p);
                    if (elem) ast_list_push(&node->array_decl.elements, elem);
                    if (check(p, TOK_COMMA)) advance_p(p);
                }
                if (check(p, TOK_DATA_CLOSE)) advance_p(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return node;
        }

        /* pr */
        case TOK_PR: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_PR_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = strdup("pr");
            if (!check(p, TOK_LPAREN)) {
                /* pr without parens: rest of line is the arg */
                AstNode *arg = parse_expr(p);
                if (arg) ast_list_push(&n->call.args, arg);
            } else {
                advance_p(p);
                skip_newlines(p);
                while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                    AstNode *arg = parse_expr(p);
                    if (!arg || p->error) { ast_node_free(n); return NULL; }
                    ast_list_push(&n->call.args, arg);
                    skip_newlines(p);
                    if (check(p, TOK_COMMA)) advance_p(p);
                }
                expect(p, TOK_RPAREN);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* variable declarations */
        case TOK_HASH: {
            advance_p(p);
            AstNode *n = parse_var_decl(p, '#', AST_VAR_DECL);
            if (n && check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }
        case TOK_AT: {
            advance_p(p);
            AstNode *n = parse_var_decl(p, '@', AST_ORGATE_DECL);
            if (n && check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }
        case TOK_DOLLAR: {
            advance_p(p);
            /* $array name = {e1, e2, ...} */
            if (check(p, TOK_ARRAY_DECL) || check(p, TOK_ARRAY_KW) ||
                (check(p, TOK_IDENT) && p->tokens[p->pos].value &&
                 strcmp(p->tokens[p->pos].value, "array") == 0)) {
                advance_p(p); /* consume 'array' */
                AstNode *node = ast_node_new(AST_ARRAY_DECL, cur(p)->line);
                if (!node) return NULL;
                if (!check(p, TOK_IDENT)) {
                    parse_error(p, "expected array name after '$array'");
                    ast_node_free(node);
                    return NULL;
                }
                node->array_decl.name = strdup(p->tokens[p->pos].value);
                advance_p(p);
                ast_list_init(&node->array_decl.elements);
                if (check(p, TOK_ASSIGN)) {
                    advance_p(p); /* consume '=' */
                    if (check(p, TOK_DATA_OPEN)) advance_p(p); /* consume '{' */
                    while (!check(p, TOK_DATA_CLOSE) && !check(p, TOK_EOF)) {
                        AstNode *elem = parse_expr(p);
                        if (elem) ast_list_push(&node->array_decl.elements, elem);
                        if (check(p, TOK_COMMA)) advance_p(p);
                    }
                    if (check(p, TOK_DATA_CLOSE)) advance_p(p); /* consume '}' */
                }
                if (check(p, TOK_SEMICOLON)) advance_p(p);
                return node;
            }
            AstNode *n = parse_var_decl(p, '$', AST_REG_VAR_DECL);
            if (n && check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }
        case TOK_CARET: {
            advance_p(p);
            AstNode *n = parse_var_decl(p, '^', AST_CONSTEXPR_DECL);
            if (n && check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* if */
        case TOK_IF:
            return parse_if_stmt(p);

        /* for */
        case TOK_FOR:
            return parse_for_stmt(p);

        /* loop */
        case TOK_LOOP:
            return parse_loop_stmt(p);

        /* while */
        case TOK_WHILE:
            return parse_while_stmt(p);

        /* break */
        case TOK_BREAK: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_BREAK_STMT, ln);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* return */
        case TOK_RETURN: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_RETURN_STMT, ln);
            if (!n) return NULL;
            n->ret.value = NULL;
            skip_newlines(p);
            if (!check(p, TOK_NEWLINE) && !check(p, TOK_SEMICOLON) &&
                !check(p, TOK_ENDINC) && !check(p, TOK_EOF))
                n->ret.value = parse_expr(p);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* continue */
        case TOK_CONTINUE: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_CONTINUE_STMT, ln);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* use */
        case TOK_USE: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_USE_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = collect_paren_content(p);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* wait for */
        case TOK_WAIT_FOR: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_WAIT_FOR_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = collect_paren_content(p);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* wait (bare) */
        case TOK_WAIT: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_CALL_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = strdup("wait");
            skip_newlines(p);
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                AstNode *arg = parse_expr(p);
                if (arg) ast_list_push(&n->call.args, arg);
                expect(p, TOK_RPAREN);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* response */
        case TOK_RESPONSE: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_RESPONSE_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = strdup("response");
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* inputxt */
        case TOK_INPUTXT: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_INPUTXT_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = strdup("inputxt");
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                skip_newlines(p);
                /* Treat unquoted identifiers as string literals for inputxt */
                if (check(p, TOK_IDENT)) {
                    AstNode *arg = ast_node_new(AST_EXPR_STRING, ln);
                    if (!arg) { ast_node_free(n); return NULL; }
                    arg->sval = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                    ast_list_push(&n->call.args, arg);
                    advance_p(p);
                } else {
                    AstNode *arg = parse_expr(p);
                    if (arg) ast_list_push(&n->call.args, arg);
                }
                skip_newlines(p);
                expect(p, TOK_RPAREN);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* thread */
        case TOK_THREAD:
            return parse_thread_stmt(p);

        /* import */
        case TOK_IMPORT: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_IMPORT_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = strdup("import");
            skip_newlines(p);
            if (check(p, TOK_STRING_LITERAL)) {
                AstNode *path = ast_node_new(AST_EXPR_STRING, ln);
                if (path) {
                    path->sval = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                    ast_list_push(&n->call.args, path);
                }
                advance_p(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* CARGO_m */
        case TOK_CARGO_M: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_CARGO_M_STMT, ln);
            if (!n) return NULL;
            n->cargo.size = 0;
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                if (check(p, TOK_INT_LITERAL)) {
                    n->cargo.size = cur(p)->value ? atoll(cur(p)->value) : 0;
                    advance_p(p);
                }
                expect(p, TOK_RPAREN);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* CLEAR_cargo */
        case TOK_CLEAR_CARGO: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_CLEAR_CARGO_STMT, ln);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* AUTOCLEAR_cargo */
        case TOK_AUTOCLEAR: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_AUTOCLEAR_STMT, ln);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* using(cpu) */
        case TOK_USING_CPU: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_USING_CPU_STMT, ln);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* MOV */
        case TOK_MOV: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_MOV_STMT, ln);
            if (!n) return NULL;
            n->mov.reg   = NULL;
            n->mov.value = NULL;
            skip_newlines(p);
            if (check(p, TOK_EAX)) {
                n->mov.reg = strdup("EAX");
                advance_p(p);
            } else if (check(p, TOK_IDENT)) {
                n->mov.reg = cur(p)->value ? strdup(cur(p)->value) : strdup("EAX");
                advance_p(p);
            }
            if (check(p, TOK_COMMA)) advance_p(p);
            n->mov.value = parse_expr(p);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* splitstring */
        case TOK_SPLIT_STRING: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_SPLIT_STRING_STMT, ln);
            if (!n) return NULL;
            n->str_op.str    = NULL;
            n->str_op.delim  = NULL;
            n->str_op.result = NULL;
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                n->str_op.str   = parse_expr(p);
                if (check(p, TOK_COMMA)) advance_p(p);
                n->str_op.delim = parse_expr(p);
                if (check(p, TOK_COMMA)) { advance_p(p);
                    if (check(p, TOK_IDENT)) {
                        n->str_op.result = cur(p)->value ? strdup(cur(p)->value) : NULL;
                        advance_p(p);
                    }
                }
                expect(p, TOK_RPAREN);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* combinestring */
        case TOK_COMBINE_STRING: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_COMBINE_STRING_STMT, ln);
            if (!n) return NULL;
            n->str_op.str    = NULL;
            n->str_op.delim  = NULL;
            n->str_op.result = NULL;
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                n->str_op.str   = parse_expr(p);
                if (check(p, TOK_COMMA)) advance_p(p);
                n->str_op.delim = parse_expr(p);
                if (check(p, TOK_COMMA)) { advance_p(p);
                    if (check(p, TOK_IDENT)) {
                        n->str_op.result = cur(p)->value ? strdup(cur(p)->value) : NULL;
                        advance_p(p);
                    }
                }
                expect(p, TOK_RPAREN);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* send{data}[dest] */
        case TOK_SEND: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_SEND_STMT, ln);
            if (!n) return NULL;
            n->send.data_name = NULL;
            n->send.dest      = NULL;
            if (check(p, TOK_DATA_OPEN)) {
                advance_p(p);
                if (check(p, TOK_IDENT)) {
                    n->send.data_name = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                    advance_p(p);
                }
                if (check(p, TOK_DATA_CLOSE)) advance_p(p);
            }
            if (check(p, TOK_BRACKET_OPEN)) {
                advance_p(p);
                char buf[256] = {0}; size_t nb = 0;
                while (!check(p, TOK_BRACKET_CLOSE) && !check(p, TOK_EOF) && nb < 255) {
                    if (cur(p)->value)
                        nb += (size_t)snprintf(buf + nb, 255 - nb, "%s", cur(p)->value);
                    advance_p(p);
                }
                n->send.dest = strdup(buf);
                if (check(p, TOK_BRACKET_CLOSE)) advance_p(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* readfile */
        case TOK_READFILE: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_READFILE_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = strdup("readfile");
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                AstNode *arg = parse_expr(p);
                if (arg) ast_list_push(&n->call.args, arg);
                expect(p, TOK_RPAREN);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* createWindow */
        case TOK_CREATEWINDOW: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_CREATEWINDOW_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = strdup("createWindow");
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                    AstNode *arg = parse_expr(p);
                    if (arg) ast_list_push(&n->call.args, arg);
                    if (check(p, TOK_COMMA)) advance_p(p);
                }
                expect(p, TOK_RPAREN);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* const */
        case TOK_CONST: {
            advance_p(p);
            /* const btn = buttonName  or  const varname  — find existing */
            AstNode *n = ast_node_new(AST_CONST_STMT, ln);
            if (!n) return NULL;
            n->var.sigil = 'c';
            n->var.name  = NULL;
            n->var.value = NULL;
            if (check(p, TOK_IDENT)) {
                n->var.name = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                advance_p(p);
            }
            if (check(p, TOK_ASSIGN)) {
                advance_p(p);
                n->var.value = parse_expr(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* openpage */
        case TOK_OPEN_PAGE: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_OPEN_PAGE_STMT, ln);
            if (!n) return NULL;
            ast_list_init(&n->call.args);
            n->call.target = collect_paren_content(p);
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* extern m / extern c */
        case TOK_EXTERN_M:
        case TOK_EXTERN_C: {
            char lang = (t->kind == TOK_EXTERN_M) ? 'm' : 'c';
            advance_p(p);
            AstNode *n = ast_node_new(lang == 'm' ? AST_EXTERN_M_BLOCK : AST_EXTERN_C_BLOCK, ln);
            if (!n) return NULL;
            n->ext.lang     = lang;
            n->ext.raw_code = NULL;
            if (check(p, TOK_DATA_OPEN)) {
                advance_p(p);
                /* collect raw text until } */
                char buf[4096] = {0}; size_t nb = 0;
                while (!check(p, TOK_DATA_CLOSE) && !check(p, TOK_EOF) && nb < 4095) {
                    if (cur(p)->value)
                        nb += (size_t)snprintf(buf + nb, 4095 - nb, "%s ", cur(p)->value);
                    advance_p(p);
                }
                n->ext.raw_code = strdup(buf);
                if (check(p, TOK_DATA_CLOSE)) advance_p(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* data block {name}[data] */
        case TOK_DATA_OPEN: {
            advance_p(p);
            AstNode *n = ast_node_new(AST_DATA_BLOCK, ln);
            if (!n) return NULL;
            n->data.name  = NULL;
            n->data.value = NULL;
            if (check(p, TOK_IDENT)) {
                n->data.name = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                advance_p(p);
            }
            if (check(p, TOK_DATA_CLOSE)) advance_p(p);
            if (check(p, TOK_BRACKET_OPEN)) {
                advance_p(p);
                n->data.value = parse_expr(p);
                if (check(p, TOK_BRACKET_CLOSE)) advance_p(p);
            }
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return n;
        }

        /* UI markup */
        case TOK_UI_OPEN:
        case TOK_UI_HEAD:
        case TOK_UI_DIV:
        case TOK_UI_BUTTON:
        case TOK_UI_INPUT:
        case TOK_UI_COLOR:
        case TOK_UI_GRADIENT:
        case TOK_UI_PNG:
        case TOK_UI_SCRIPT:
        case TOK_UI_TAG: {
            /* UI parsing is handled by ui.c; here we just package the token */
            AstNode *n = ast_node_new(AST_UI_PAGE, ln);
            if (!n) return NULL;
            n->ui.name      = cur(p)->value ? strdup(cur(p)->value) : strdup("");
            n->ui.attr_name = NULL;
            n->ui.color_val = NULL;
            n->ui.png_path  = NULL;
            ast_list_init(&n->ui.children);
            ast_list_init(&n->ui.script);
            advance_p(p);
            /* collect children until endui */
            parse_block_into(p, &n->ui.children, TOK_UI_ENDUI, TOK_EOF);
            if (check(p, TOK_UI_ENDUI)) advance_p(p);
            return n;
        }

        /* endinc — consumed by inc parser; bare occurrence is a NOP */
        case TOK_ENDINC: {
            AstNode *n = ast_node_new(AST_NOP, ln);
            advance_p(p);
            return n;
        }

        /* semicolon alone */
        case TOK_SEMICOLON: {
            advance_p(p);
            return ast_node_new(AST_NOP, ln);
        }

        /* identifier possibly followed by assignment or call */
        case TOK_IDENT: {
            const Token *next = peek_at(p, 1);
            if (next->kind == TOK_ASSIGN) {
                /* assignment */
                char *name = cur(p)->value ? strdup(cur(p)->value) : strdup("");
                if (!name) return NULL;
                advance_p(p); /* ident */
                advance_p(p); /* = */
                AstNode *val = parse_expr(p);
                if (!val || p->error) { free(name); return NULL; }
                AstNode *n = ast_node_new(AST_ASSIGN, ln);
                if (!n) { free(name); ast_node_free(val); return NULL; }
                n->assign.name  = name;
                n->assign.value = val;
                if (check(p, TOK_SEMICOLON)) advance_p(p);
                return n;
            }
            /* function call as statement */
            AstNode *expr = parse_expr(p);
            if (!expr || p->error) return NULL;
            if (check(p, TOK_SEMICOLON)) advance_p(p);
            return expr;
        }

        default:
            /* skip unknown token */
            advance_p(p);
            return ast_node_new(AST_NOP, ln);
    }
}

/* ---- top-level parse -------------------------------------------------- */

AstNode *parser_parse(Parser *p) {
    if (!p) return NULL;

    AstNode *program = ast_node_new(AST_PROGRAM, 1);
    if (!program) return NULL;
    ast_list_init(&program->block.stmts);

    parse_block_into(p, &program->block.stmts, TOK_EOF, TOK_EOF);

    if (p->error) {
        fprintf(stderr, "%s\n", p->errmsg);
        /* still return partially-built tree */
    }
    return program;
}
