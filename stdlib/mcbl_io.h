#ifndef MCBL_IO_H
#define MCBL_IO_H
/*
 * McBL# io.* — Input/Output Standard Library
 * ============================================
 * io.print(val)          → print tanpa newline
 * io.println(val)        → print + newline
 * io.read()              → baca line dari stdin
 * io.read_int()          → baca int dari stdin
 * io.read_float()        → baca float dari stdin
 * io.write_file(p, s)    → tulis ke file
 * io.append_file(p, s)   → append ke file
 * io.read_file(p)        → baca semua isi file
 * io.read_lines(p)       → baca file sebagai array baris
 * io.stdin_lines()       → baca stdin sampai EOF, array of lines
 * io.flush()             → flush stdout
 * io.eprint(msg)         → print ke stderr
 * io.eprintln(msg)       → print ke stderr + newline
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void         mcbl_io_print    (const char *s);
void         mcbl_io_println  (const char *s);
void         mcbl_io_print_int(long long v);
void         mcbl_io_print_flt(double v, int prec);
char        *mcbl_io_read     (void);           /* malloc'd, caller frees */
long long    mcbl_io_read_int (void);
double       mcbl_io_read_flt (void);
int          mcbl_io_write_file(const char *path, const char *content);
int          mcbl_io_append_file(const char *path, const char *content);
char        *mcbl_io_read_file (const char *path);  /* malloc'd */
char       **mcbl_io_read_lines(const char *path, int *out_count);
void         mcbl_io_free_lines(char **lines, int count);
void         mcbl_io_flush    (void);
void         mcbl_io_eprint   (const char *s);
void         mcbl_io_eprintln (const char *s);

/* Format print */
void         mcbl_io_printf   (const char *fmt, ...);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_IO_H */
