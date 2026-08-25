#ifndef MCBL_STR_H
#define MCBL_STR_H

/*
 * McBL# String Standard Library  (v2.0)
 * ========================================
 * Manipulasi string level dewa.
 * Syntax di McBL#:
 *   str.len("hello")          => 5
 *   str.upper("hello")        => "HELLO"
 *   str.sub("hello", 1, 3)    => "ell"
 *   str.format("{} + {} = {}", 1, 2, 3)  => "1 + 2 = 3"
 *   str.regex("hello123", "[0-9]+")       => "123"
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* McBL string type — reference counted, always null-terminated */
typedef struct {
    char    *buf;       /* heap allocated */
    size_t   len;       /* chars (excluding null) */
    size_t   cap;       /* allocated capacity */
    int      refs;      /* reference count */
} McblStr;

/* Create/destroy */
McblStr *mcbl_str_new    (const char *s);
McblStr *mcbl_str_empty  (void);
McblStr *mcbl_str_dup    (const McblStr *s);
void     mcbl_str_free   (McblStr *s);
void     mcbl_str_ref    (McblStr *s);   /* increment refcount */
void     mcbl_str_unref  (McblStr *s);   /* decrement + free if 0 */

/* Raw C string in/out */
const char *mcbl_str_cstr(const McblStr *s);

/* -------------------------------------------------------------------
   Core string operations  (str.xxx keywords)
   ------------------------------------------------------------------- */

/* str.len(s) — character count (UTF-8 aware) */
size_t    mcbl_str_len    (const McblStr *s);
size_t    mcbl_str_len_bytes(const McblStr *s);  /* byte count */

/* str.upper / str.lower */
McblStr  *mcbl_str_upper  (const McblStr *s);
McblStr  *mcbl_str_lower  (const McblStr *s);

/* str.trim — strip leading/trailing whitespace */
McblStr  *mcbl_str_trim   (const McblStr *s);
McblStr  *mcbl_str_ltrim  (const McblStr *s);
McblStr  *mcbl_str_rtrim  (const McblStr *s);

/* str.find(s, sub, from) — returns index or -1 */
int64_t   mcbl_str_find   (const McblStr *s, const McblStr *sub,
                            int64_t from);
int64_t   mcbl_str_rfind  (const McblStr *s, const McblStr *sub);

/* str.replace(s, old, new) — replace all occurrences */
McblStr  *mcbl_str_replace(const McblStr *s,
                            const McblStr *old_sub,
                            const McblStr *new_sub);

/* str.sub(s, start, end) — substring [start, end) */
McblStr  *mcbl_str_sub    (const McblStr *s, int64_t start, int64_t end);

/* str.char(s, idx) — get single char */
int       mcbl_str_char_at(const McblStr *s, int64_t idx);

/* str.starts / str.ends */
int       mcbl_str_starts (const McblStr *s, const McblStr *prefix);
int       mcbl_str_ends   (const McblStr *s, const McblStr *suffix);

/* str.contains */
int       mcbl_str_contains(const McblStr *s, const McblStr *sub);

/* str.count(s, sub) — count non-overlapping occurrences */
size_t    mcbl_str_count  (const McblStr *s, const McblStr *sub);

/* str.split(s, delim) — returns null-terminated array */
McblStr **mcbl_str_split  (const McblStr *s, const McblStr *delim,
                            size_t *out_count);
void      mcbl_str_split_free(McblStr **parts, size_t count);

/* str.join(sep, parts, n) */
McblStr  *mcbl_str_join   (const McblStr *sep,
                            McblStr **parts, size_t count);

/* str.repeat(s, n) */
McblStr  *mcbl_str_repeat (const McblStr *s, size_t n);

/* str.rev(s) */
McblStr  *mcbl_str_rev    (const McblStr *s);

/* str.pad(s, width, padchar, side)  side: 'l'=left 'r'=right 'c'=center */
McblStr  *mcbl_str_pad    (const McblStr *s, size_t width,
                            int padchar, char side);

/* str.format("{} {} {}", args...)
   Supports: {} int/float/string, {:d} decimal, {:f} float, {:x} hex */
McblStr  *mcbl_str_format (const char *fmt, ...);
McblStr  *mcbl_str_format_v(const char *fmt, void **args,
                             const char **types, int argc);

/* str.toInt / str.toFloat */
int64_t   mcbl_str_to_int  (const McblStr *s, int base);
double    mcbl_str_to_float(const McblStr *s);
int       mcbl_str_is_numeric(const McblStr *s);

/* str.encode / str.decode */
typedef enum {
    MCBL_ENC_BASE64,
    MCBL_ENC_HEX,
    MCBL_ENC_URL,
    MCBL_ENC_HTML
} McblEncoding;

McblStr  *mcbl_str_encode (const McblStr *s, McblEncoding enc);
McblStr  *mcbl_str_decode (const McblStr *s, McblEncoding enc);

/* str.bytes(s) — raw byte array */
uint8_t  *mcbl_str_bytes  (const McblStr *s, size_t *out_len);
McblStr  *mcbl_str_from_bytes(const uint8_t *bytes, size_t len);

/* -------------------------------------------------------------------
   Regex  (str.regex / str.match)
   Lightweight POSIX-compatible regex engine built-in
   ------------------------------------------------------------------- */

typedef struct McblRegex McblRegex;

/* Compile a pattern */
McblRegex *mcbl_regex_compile(const char *pattern);
void       mcbl_regex_free   (McblRegex *re);

/* str.match(s, pattern) — returns array of matched groups */
typedef struct {
    McblStr **groups;
    size_t    count;
    int64_t   match_start;
    int64_t   match_end;
} McblRegexMatch;

McblRegexMatch *mcbl_regex_match(McblRegex *re, const McblStr *s);
void            mcbl_regex_match_free(McblRegexMatch *m);

/* str.regex(s, pattern) — returns first full match */
McblStr *mcbl_str_regex_first(const McblStr *s, const char *pattern);

/* Find all matches */
McblStr **mcbl_str_regex_all(const McblStr *s, const char *pattern,
                              size_t *out_count);

/* Replace using regex */
McblStr  *mcbl_str_regex_replace(const McblStr *s,
                                  const char *pattern,
                                  const McblStr *replacement);

/* str.split by regex */
McblStr **mcbl_str_regex_split(const McblStr *s, const char *pattern,
                                size_t *out_count);

/* -------------------------------------------------------------------
   Conversion helpers
   ------------------------------------------------------------------- */
McblStr *mcbl_str_from_int  (int64_t val);
McblStr *mcbl_str_from_float(double val, int precision);
McblStr *mcbl_str_from_bool (int val);
McblStr *mcbl_str_from_char (int c);

/* Compare */
int      mcbl_str_eq  (const McblStr *a, const McblStr *b);
int      mcbl_str_cmp (const McblStr *a, const McblStr *b); /* <0,0,>0 */
int      mcbl_str_ieq (const McblStr *a, const McblStr *b); /* case-insensitive */

/* Concat */
McblStr *mcbl_str_concat(const McblStr *a, const McblStr *b);
void     mcbl_str_append(McblStr *s, const McblStr *suffix);
void     mcbl_str_append_c(McblStr *s, const char *suffix);

/* -------------------------------------------------------------------
   String Builder — efficient construction
   ------------------------------------------------------------------- */
typedef struct {
    McblStr *buf;
} McblStrBuf;

McblStrBuf *mcbl_sb_new    (void);
void        mcbl_sb_free   (McblStrBuf *sb);
void        mcbl_sb_append (McblStrBuf *sb, const McblStr *s);
void        mcbl_sb_append_c(McblStrBuf *sb, const char *s);
void        mcbl_sb_append_int(McblStrBuf *sb, int64_t v);
void        mcbl_sb_append_float(McblStrBuf *sb, double v, int prec);
McblStr    *mcbl_sb_build  (McblStrBuf *sb);

#ifdef __cplusplus
}
#endif

#endif /* MCBL_STR_H */
