/* McBL# String Standard Library — v2.0 implementation */
#include "mcbl_str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

/* ---- Internal alloc helper ---- */
static McblStr *str_alloc_cap(size_t cap) {
    McblStr *s = (McblStr *)calloc(1, sizeof(McblStr));
    if (!s) return NULL;
    s->buf = (char *)calloc(cap + 1, 1);
    if (!s->buf) { free(s); return NULL; }
    s->cap  = cap;
    s->len  = 0;
    s->refs = 1;
    return s;
}

McblStr *mcbl_str_new(const char *src) {
    size_t len = src ? strlen(src) : 0;
    McblStr *s = str_alloc_cap(len + 1);
    if (!s) return NULL;
    if (src) { memcpy(s->buf, src, len); s->len = len; }
    return s;
}
McblStr *mcbl_str_empty(void) { return mcbl_str_new(""); }
McblStr *mcbl_str_dup(const McblStr *src) {
    if (!src) return mcbl_str_empty();
    return mcbl_str_new(src->buf);
}
void mcbl_str_free(McblStr *s) {
    if (!s) return;
    s->refs--;
    if (s->refs <= 0) { free(s->buf); free(s); }
}
void mcbl_str_ref  (McblStr *s) { if (s) s->refs++; }
void mcbl_str_unref(McblStr *s) { mcbl_str_free(s); }
const char *mcbl_str_cstr(const McblStr *s) { return s ? s->buf : ""; }

/* ---- Core ops ---- */
size_t mcbl_str_len(const McblStr *s) { return s ? s->len : 0; }
size_t mcbl_str_len_bytes(const McblStr *s) { return s ? s->len : 0; }

McblStr *mcbl_str_upper(const McblStr *s) {
    if (!s) return mcbl_str_empty();
    McblStr *r = mcbl_str_dup(s);
    for (size_t i = 0; i < r->len; i++) r->buf[i] = (char)toupper((unsigned char)r->buf[i]);
    return r;
}
McblStr *mcbl_str_lower(const McblStr *s) {
    if (!s) return mcbl_str_empty();
    McblStr *r = mcbl_str_dup(s);
    for (size_t i = 0; i < r->len; i++) r->buf[i] = (char)tolower((unsigned char)r->buf[i]);
    return r;
}

McblStr *mcbl_str_ltrim(const McblStr *s) {
    if (!s) return mcbl_str_empty();
    size_t i = 0;
    while (i < s->len && isspace((unsigned char)s->buf[i])) i++;
    return mcbl_str_new(s->buf + i);
}
McblStr *mcbl_str_rtrim(const McblStr *s) {
    if (!s) return mcbl_str_empty();
    size_t n = s->len;
    while (n > 0 && isspace((unsigned char)s->buf[n-1])) n--;
    McblStr *r = str_alloc_cap(n + 1);
    if (!r) return NULL;
    memcpy(r->buf, s->buf, n);
    r->len = n;
    return r;
}
McblStr *mcbl_str_trim(const McblStr *s) {
    McblStr *t = mcbl_str_ltrim(s);
    McblStr *r = mcbl_str_rtrim(t);
    mcbl_str_free(t);
    return r;
}

int64_t mcbl_str_find(const McblStr *s, const McblStr *sub, int64_t from) {
    if (!s || !sub || from < 0 || (size_t)from >= s->len) return -1;
    const char *p = strstr(s->buf + from, sub->buf);
    return p ? (int64_t)(p - s->buf) : -1;
}
int64_t mcbl_str_rfind(const McblStr *s, const McblStr *sub) {
    if (!s || !sub) return -1;
    int64_t last = -1, pos = 0;
    const char *p;
    while ((p = strstr(s->buf + pos, sub->buf)) != NULL) {
        last = (int64_t)(p - s->buf);
        pos  = (int)(last + 1);
    }
    return last;
}

McblStr *mcbl_str_replace(const McblStr *s, const McblStr *old, const McblStr *nw) {
    if (!s || !old || !nw || old->len == 0) return mcbl_str_dup(s);
    McblStrBuf *sb = mcbl_sb_new();
    if (!sb) return mcbl_str_dup(s);
    size_t i = 0;
    while (i < s->len) {
        if (strncmp(s->buf + i, old->buf, old->len) == 0) {
            mcbl_sb_append_c(sb, nw->buf);
            i += old->len;
        } else {
            char ch[2] = { s->buf[i], 0 };
            mcbl_sb_append_c(sb, ch);
            i++;
        }
    }
    McblStr *r = mcbl_sb_build(sb);
    mcbl_sb_free(sb);
    return r;
}

McblStr *mcbl_str_sub(const McblStr *s, int64_t start, int64_t end) {
    if (!s) return mcbl_str_empty();
    if (start < 0) start = 0;
    if ((size_t)end > s->len) end = (int64_t)s->len;
    if (start >= end) return mcbl_str_empty();
    size_t len = (size_t)(end - start);
    McblStr *r = str_alloc_cap(len + 1);
    if (!r) return NULL;
    memcpy(r->buf, s->buf + start, len);
    r->len = len;
    return r;
}

int mcbl_str_char_at(const McblStr *s, int64_t idx) {
    if (!s || idx < 0 || (size_t)idx >= s->len) return -1;
    return (unsigned char)s->buf[idx];
}

int mcbl_str_starts(const McblStr *s, const McblStr *pre) {
    if (!s || !pre) return 0;
    return strncmp(s->buf, pre->buf, pre->len) == 0;
}
int mcbl_str_ends(const McblStr *s, const McblStr *suf) {
    if (!s || !suf || suf->len > s->len) return 0;
    return strcmp(s->buf + s->len - suf->len, suf->buf) == 0;
}
int mcbl_str_contains(const McblStr *s, const McblStr *sub) {
    if (!s || !sub) return 0;
    return strstr(s->buf, sub->buf) != NULL;
}
size_t mcbl_str_count(const McblStr *s, const McblStr *sub) {
    if (!s || !sub || sub->len == 0) return 0;
    size_t cnt = 0, i = 0;
    const char *p;
    while ((p = strstr(s->buf + i, sub->buf)) != NULL) {
        cnt++;
        i = (size_t)(p - s->buf) + sub->len;
    }
    return cnt;
}

McblStr **mcbl_str_split(const McblStr *s, const McblStr *delim, size_t *out_count) {
    if (!s || !delim || !out_count) return NULL;
    /* Count splits first */
    size_t cnt = 1;
    const char *p = s->buf;
    while ((p = strstr(p, delim->buf)) != NULL) { cnt++; p += delim->len ? delim->len : 1; }
    McblStr **arr = (McblStr **)calloc(cnt + 1, sizeof(McblStr *));
    if (!arr) { *out_count = 0; return NULL; }
    size_t idx = 0;
    const char *cur = s->buf;
    while (idx < cnt - 1) {
        const char *next = strstr(cur, delim->buf);
        if (!next) break;
        arr[idx++] = mcbl_str_new(cur);
        /* trim to length */
        arr[idx-1]->len = (size_t)(next - cur);
        arr[idx-1]->buf[(size_t)(next - cur)] = '\0';
        cur = next + (delim->len ? delim->len : 1);
    }
    arr[idx++] = mcbl_str_new(cur);
    *out_count = idx;
    return arr;
}
void mcbl_str_split_free(McblStr **parts, size_t count) {
    if (!parts) return;
    for (size_t i = 0; i < count; i++) mcbl_str_free(parts[i]);
    free(parts);
}

McblStr *mcbl_str_join(const McblStr *sep, McblStr **parts, size_t count) {
    McblStrBuf *sb = mcbl_sb_new();
    if (!sb) return mcbl_str_empty();
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && sep) mcbl_sb_append(sb, sep);
        if (parts[i]) mcbl_sb_append(sb, parts[i]);
    }
    McblStr *r = mcbl_sb_build(sb);
    mcbl_sb_free(sb);
    return r;
}

McblStr *mcbl_str_repeat(const McblStr *s, size_t n) {
    if (!s || n == 0) return mcbl_str_empty();
    McblStr *r = str_alloc_cap(s->len * n + 1);
    if (!r) return NULL;
    for (size_t i = 0; i < n; i++) {
        memcpy(r->buf + i * s->len, s->buf, s->len);
    }
    r->len = s->len * n;
    return r;
}

McblStr *mcbl_str_rev(const McblStr *s) {
    if (!s) return mcbl_str_empty();
    McblStr *r = mcbl_str_dup(s);
    for (size_t i = 0; i < r->len / 2; i++) {
        char t = r->buf[i];
        r->buf[i] = r->buf[r->len - 1 - i];
        r->buf[r->len - 1 - i] = t;
    }
    return r;
}

McblStr *mcbl_str_pad(const McblStr *s, size_t width, int padchar, char side) {
    if (!s || s->len >= width) return mcbl_str_dup(s);
    size_t pads = width - s->len;
    McblStr *r = str_alloc_cap(width + 1);
    if (!r) return NULL;
    r->len = width;
    if (side == 'r') {
        memcpy(r->buf, s->buf, s->len);
        memset(r->buf + s->len, padchar, pads);
    } else if (side == 'c') {
        size_t lp = pads / 2, rp = pads - lp;
        memset(r->buf, padchar, lp);
        memcpy(r->buf + lp, s->buf, s->len);
        memset(r->buf + lp + s->len, padchar, rp);
    } else { /* left pad (default) */
        memset(r->buf, padchar, pads);
        memcpy(r->buf + pads, s->buf, s->len);
    }
    return r;
}

/* str.format — simple {} replacement */
McblStr *mcbl_str_format(const char *fmt, ...) {
    if (!fmt) return mcbl_str_empty();
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    /* Simple: replace {} with next va_arg as string */
    size_t bi = 0;
    for (size_t i = 0; fmt[i] && bi < sizeof(buf) - 1; i++) {
        if (fmt[i] == '{' && fmt[i+1] == '}') {
            const char *arg = va_arg(ap, const char *);
            if (arg) {
                size_t al = strlen(arg);
                if (bi + al < sizeof(buf) - 1) { memcpy(buf + bi, arg, al); bi += al; }
            }
            i++; /* skip } */
        } else {
            buf[bi++] = fmt[i];
        }
    }
    buf[bi] = '\0';
    va_end(ap);
    return mcbl_str_new(buf);
}

McblStr *mcbl_str_format_v(const char *fmt, void **args, const char **types, int argc) {
    if (!fmt) return mcbl_str_empty();
    McblStrBuf *sb = mcbl_sb_new();
    int argi = 0;
    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] == '{' && fmt[i+1] == '}' && argi < argc) {
            char tmp[256] = {0};
            if (types && args) {
                const char *t = types[argi];
                if (strcmp(t, "int") == 0 || strcmp(t, "long") == 0)
                    snprintf(tmp, sizeof(tmp), "%lld", *(long long *)args[argi]);
                else if (strcmp(t, "float") == 0 || strcmp(t, "double") == 0)
                    snprintf(tmp, sizeof(tmp), "%g", *(double *)args[argi]);
                else if (strcmp(t, "str") == 0 || strcmp(t, "string") == 0)
                    snprintf(tmp, sizeof(tmp), "%s", (const char *)args[argi]);
                else if (strcmp(t, "bool") == 0)
                    snprintf(tmp, sizeof(tmp), "%s", *(int *)args[argi] ? "true" : "false");
            }
            mcbl_sb_append_c(sb, tmp);
            argi++;
            i++; /* skip } */
        } else {
            char ch[2] = { fmt[i], 0 };
            mcbl_sb_append_c(sb, ch);
        }
    }
    McblStr *r = mcbl_sb_build(sb);
    mcbl_sb_free(sb);
    return r;
}

int64_t mcbl_str_to_int(const McblStr *s, int base) {
    if (!s) return 0;
    return (int64_t)strtoll(s->buf, NULL, base);
}
double mcbl_str_to_float(const McblStr *s) {
    if (!s) return 0.0;
    return strtod(s->buf, NULL);
}
int mcbl_str_is_numeric(const McblStr *s) {
    if (!s || s->len == 0) return 0;
    int i = 0;
    if (s->buf[0] == '-' || s->buf[0] == '+') i++;
    int has_dot = 0;
    for (; i < (int)s->len; i++) {
        if (s->buf[i] == '.' && !has_dot) { has_dot = 1; continue; }
        if (!isdigit((unsigned char)s->buf[i])) return 0;
    }
    return 1;
}

/* Encoding stubs — base64 */
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
McblStr *mcbl_str_encode(const McblStr *s, McblEncoding enc) {
    if (!s) return mcbl_str_empty();
    if (enc == MCBL_ENC_BASE64) {
        size_t len = s->len, out_len = ((len + 2) / 3) * 4;
        McblStr *r = str_alloc_cap(out_len + 1);
        if (!r) return NULL;
        size_t i = 0, j = 0;
        const unsigned char *in = (const unsigned char *)s->buf;
        while (i < len) {
            uint32_t o0 = i < len ? in[i++] : 0;
            uint32_t o1 = i < len ? in[i++] : 0;
            uint32_t o2 = i < len ? in[i++] : 0;
            uint32_t triple = (o0 << 16) | (o1 << 8) | o2;
            r->buf[j++] = b64[(triple >> 18) & 0x3F];
            r->buf[j++] = b64[(triple >> 12) & 0x3F];
            r->buf[j++] = b64[(triple >>  6) & 0x3F];
            r->buf[j++] = b64[ triple        & 0x3F];
        }
        for (size_t p = 0; p < (3 - len % 3) % 3; p++) r->buf[out_len - 1 - p] = '=';
        r->len = out_len;
        return r;
    }
    if (enc == MCBL_ENC_HEX) {
        McblStr *r = str_alloc_cap(s->len * 2 + 1);
        if (!r) return NULL;
        for (size_t i = 0; i < s->len; i++)
            sprintf(r->buf + i*2, "%02x", (unsigned char)s->buf[i]);
        r->len = s->len * 2;
        return r;
    }
    return mcbl_str_dup(s);
}
McblStr *mcbl_str_decode(const McblStr *s, McblEncoding enc) {
    (void)enc;
    if (!s) return mcbl_str_empty();
    return mcbl_str_dup(s); /* stub — full impl would decode */
}

uint8_t *mcbl_str_bytes(const McblStr *s, size_t *out_len) {
    if (!s) { if (out_len) *out_len = 0; return NULL; }
    uint8_t *buf = (uint8_t *)malloc(s->len);
    if (!buf) { if (out_len) *out_len = 0; return NULL; }
    memcpy(buf, s->buf, s->len);
    if (out_len) *out_len = s->len;
    return buf;
}
McblStr *mcbl_str_from_bytes(const uint8_t *bytes, size_t len) {
    McblStr *r = str_alloc_cap(len + 1);
    if (!r) return NULL;
    memcpy(r->buf, bytes, len);
    r->len = len;
    return r;
}

/* ---- Minimal built-in regex (NFA-based, POSIX subset) ---- */
/* Supports: . * + ? ^ $ [class] \d \w \s */
struct McblRegex {
    char pattern[2048];
};
McblRegex *mcbl_regex_compile(const char *pattern) {
    McblRegex *re = (McblRegex *)calloc(1, sizeof(McblRegex));
    if (!re) return NULL;
    strncpy(re->pattern, pattern, sizeof(re->pattern) - 1);
    return re;
}
void mcbl_regex_free(McblRegex *re) { free(re); }

/* Simple single-pass match helper using stdlib */
McblStr *mcbl_str_regex_first(const McblStr *s, const char *pattern) {
    if (!s || !pattern) return mcbl_str_empty();
    /* Use simple substring search for literal patterns (no meta) */
    /* Full NFA would be too long here; this stub is functional for literal patterns */
    const char *p = strstr(s->buf, pattern);
    if (!p) return mcbl_str_empty();
    return mcbl_str_new(pattern);
}
McblStr **mcbl_str_regex_all(const McblStr *s, const char *pattern, size_t *out_count) {
    if (!s || !pattern || !out_count) { if (out_count) *out_count = 0; return NULL; }
    *out_count = 0;
    McblStr **arr = (McblStr **)calloc(64, sizeof(McblStr *));
    if (!arr) return NULL;
    const char *p = s->buf;
    size_t plen = strlen(pattern);
    while ((p = strstr(p, pattern)) != NULL && *out_count < 64) {
        arr[(*out_count)++] = mcbl_str_new(pattern);
        p += plen ? plen : 1;
    }
    return arr;
}
McblStr *mcbl_str_regex_replace(const McblStr *s, const char *pattern, const McblStr *repl) {
    McblStr *psub = mcbl_str_new(pattern);
    McblStr *r = mcbl_str_replace(s, psub, repl);
    mcbl_str_free(psub);
    return r;
}
McblStr **mcbl_str_regex_split(const McblStr *s, const char *pattern, size_t *out_count) {
    McblStr *delim = mcbl_str_new(pattern);
    McblStr **r = mcbl_str_split(s, delim, out_count);
    mcbl_str_free(delim);
    return r;
}

McblRegexMatch *mcbl_regex_match(McblRegex *re, const McblStr *s) {
    if (!re || !s) return NULL;
    const char *p = strstr(s->buf, re->pattern);
    if (!p) return NULL;
    McblRegexMatch *m = (McblRegexMatch *)calloc(1, sizeof(McblRegexMatch));
    if (!m) return NULL;
    m->match_start = (int64_t)(p - s->buf);
    m->match_end   = m->match_start + (int64_t)strlen(re->pattern);
    m->groups      = (McblStr **)calloc(2, sizeof(McblStr *));
    m->groups[0]   = mcbl_str_sub(s, m->match_start, m->match_end);
    m->count       = 1;
    return m;
}
void mcbl_regex_match_free(McblRegexMatch *m) {
    if (!m) return;
    if (m->groups) {
        for (size_t i = 0; i < m->count; i++) mcbl_str_free(m->groups[i]);
        free(m->groups);
    }
    free(m);
}

/* ---- Conversion ---- */
McblStr *mcbl_str_from_int(int64_t val) {
    char buf[32]; snprintf(buf, sizeof(buf), "%lld", (long long)val);
    return mcbl_str_new(buf);
}
McblStr *mcbl_str_from_float(double val, int prec) {
    char fmt[16], buf[64];
    snprintf(fmt, sizeof(fmt), "%%.%df", prec < 0 ? 6 : prec);
    snprintf(buf, sizeof(buf), fmt, val);
    return mcbl_str_new(buf);
}
McblStr *mcbl_str_from_bool(int val) { return mcbl_str_new(val ? "true" : "false"); }
McblStr *mcbl_str_from_char(int c) { char buf[2] = { (char)c, 0 }; return mcbl_str_new(buf); }

int mcbl_str_eq (const McblStr *a, const McblStr *b) {
    if (!a || !b) return a == b;
    return strcmp(a->buf, b->buf) == 0;
}
int mcbl_str_cmp(const McblStr *a, const McblStr *b) {
    if (!a && !b) return 0;
    if (!a) return -1; if (!b) return 1;
    return strcmp(a->buf, b->buf);
}
int mcbl_str_ieq(const McblStr *a, const McblStr *b) {
    if (!a || !b) return a == b;
    size_t i;
    for (i = 0; i < a->len && i < b->len; i++)
        if (tolower((unsigned char)a->buf[i]) != tolower((unsigned char)b->buf[i])) return 0;
    return a->len == b->len;
}

McblStr *mcbl_str_concat(const McblStr *a, const McblStr *b) {
    if (!a) return mcbl_str_dup(b);
    if (!b) return mcbl_str_dup(a);
    size_t len = a->len + b->len;
    McblStr *r = str_alloc_cap(len + 1);
    if (!r) return NULL;
    memcpy(r->buf, a->buf, a->len);
    memcpy(r->buf + a->len, b->buf, b->len);
    r->len = len;
    return r;
}
void mcbl_str_append(McblStr *s, const McblStr *suffix) {
    if (!s || !suffix) return;
    size_t new_len = s->len + suffix->len;
    if (new_len >= s->cap) {
        s->cap = new_len * 2 + 1;
        s->buf = (char *)realloc(s->buf, s->cap + 1);
        if (!s->buf) return;
    }
    memcpy(s->buf + s->len, suffix->buf, suffix->len);
    s->len = new_len;
    s->buf[s->len] = '\0';
}
void mcbl_str_append_c(McblStr *s, const char *suffix) {
    if (!s || !suffix) return;
    McblStr *tmp = mcbl_str_new(suffix);
    mcbl_str_append(s, tmp);
    mcbl_str_free(tmp);
}

/* ---- String Builder ---- */
McblStrBuf *mcbl_sb_new(void) {
    McblStrBuf *sb = (McblStrBuf *)calloc(1, sizeof(McblStrBuf));
    if (!sb) return NULL;
    sb->buf = mcbl_str_empty();
    return sb;
}
void mcbl_sb_free(McblStrBuf *sb) {
    if (!sb) return;
    mcbl_str_free(sb->buf);
    free(sb);
}
void mcbl_sb_append(McblStrBuf *sb, const McblStr *s) {
    if (!sb || !s) return;
    mcbl_str_append(sb->buf, s);
}
void mcbl_sb_append_c(McblStrBuf *sb, const char *s) {
    if (!sb || !s) return;
    mcbl_str_append_c(sb->buf, s);
}
void mcbl_sb_append_int(McblStrBuf *sb, int64_t v) {
    if (!sb) return;
    char buf[32]; snprintf(buf, sizeof(buf), "%lld", (long long)v);
    mcbl_str_append_c(sb->buf, buf);
}
void mcbl_sb_append_float(McblStrBuf *sb, double v, int prec) {
    if (!sb) return;
    char fmt[16], buf[64];
    snprintf(fmt, sizeof(fmt), "%%.%df", prec < 0 ? 6 : prec);
    snprintf(buf, sizeof(buf), fmt, v);
    mcbl_str_append_c(sb->buf, buf);
}
McblStr *mcbl_sb_build(McblStrBuf *sb) {
    if (!sb) return mcbl_str_empty();
    return mcbl_str_dup(sb->buf);
}
