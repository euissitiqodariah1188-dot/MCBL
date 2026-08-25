#ifndef MCBL_FMT_H
#define MCBL_FMT_H
/*
 * McBL# fmt.* — String Formatting
 * ==================================
 * fmt.sprintf(fmt, ...)       → formatted string (malloc'd)
 * fmt.int(v, base)            → int to string
 * fmt.float(v, prec)          → float to string
 * fmt.pad(s, width, ch, side) → pad string
 * fmt.table(rows, cols, data) → ASCII table string
 * fmt.hex(v)                  → hex string
 * fmt.bin(v)                  → binary string
 * fmt.si(v)                   → SI prefix (1234 → "1.23K")
 * fmt.bytes(n)                → bytes string (1234 → "1.2 KB")
 * fmt.duration(ns)            → duration string ("1.23ms")
 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

char *mcbl_fmt_sprintf(const char *fmt, ...);
char *mcbl_fmt_int    (long long v, int base);
char *mcbl_fmt_float  (double v, int prec);
char *mcbl_fmt_pad    (const char *s, int width, char ch, char side);
char *mcbl_fmt_table  (const char **rows, int nrow, int ncol);
char *mcbl_fmt_hex    (long long v);
char *mcbl_fmt_bin    (long long v);
char *mcbl_fmt_si     (double v);
char *mcbl_fmt_bytes  (int64_t n);
char *mcbl_fmt_duration(int64_t ns);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_FMT_H */
