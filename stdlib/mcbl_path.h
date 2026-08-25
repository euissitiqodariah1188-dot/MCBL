#ifndef MCBL_PATH_H
#define MCBL_PATH_H
/*
 * McBL# path.* — File Path Manipulation
 * ========================================
 * path.join(a, b)     → "a/b"
 * path.dir(p)         → parent directory
 * path.base(p)        → filename
 * path.ext(p)         → extension
 * path.stem(p)        → filename without extension
 * path.abs(p)         → absolute path
 * path.exists(p)      → 1 if path exists
 * path.is_file(p)     → 1 if regular file
 * path.is_dir(p)      → 1 if directory
 * path.expand(p)      → expand ~ and env vars
 * path.glob(pat)      → array of matching paths
 */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

char  *mcbl_path_join   (const char *a, const char *b);
char  *mcbl_path_dir    (const char *p);
char  *mcbl_path_base   (const char *p);
char  *mcbl_path_ext    (const char *p);
char  *mcbl_path_stem   (const char *p);
char  *mcbl_path_abs    (const char *p);
int    mcbl_path_exists (const char *p);
int    mcbl_path_is_file(const char *p);
int    mcbl_path_is_dir (const char *p);
char  *mcbl_path_expand (const char *p);
char **mcbl_path_glob   (const char *pattern, int *out_count);
void   mcbl_path_free_list(char **paths, int count);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_PATH_H */
