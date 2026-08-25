#include "mdc.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* -----------------------------------------------------------------------
   McBL# MDC implementation
   Generates C glue code enabling McBL# ↔ C interoperability.
   ----------------------------------------------------------------------- */

#define GLUE_BUF_SIZE 16384

static void glue_append(char *buf, size_t *pos, size_t cap, const char *fmt, ...) {
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    size_t l = strlen(tmp);
    if (*pos + l < cap) {
        memcpy(buf + *pos, tmp, l);
        *pos += l;
        buf[*pos] = '\0';
    }
}

char *mdc_gen_c_glue(const AstNode *extern_block) {
    if (!extern_block) return mcbl_strdup("");
    char *buf = (char *)mcbl_malloc(GLUE_BUF_SIZE);
    size_t pos = 0;
    buf[0] = '\0';

    glue_append(buf, &pos, GLUE_BUF_SIZE,
        "/* McBL# MDC - extern m glue (McBL -> C) */\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "/* McBL value type for interop */\n"
        "typedef struct {\n"
        "    int   type; /* 0=int 1=float 2=str 3=bool */\n"
        "    union { long long ival; double fval; char *sval; int bval; };\n"
        "} McblInteropValue;\n"
        "\n");

    /* If raw_code is present, wrap it in an extern C block */
    if (extern_block->kind == AST_EXTERN_M_BLOCK && extern_block->ext.raw_code) {
        glue_append(buf, &pos, GLUE_BUF_SIZE,
            "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
            "%s\n"
            "#ifdef __cplusplus\n}\n#endif\n",
            extern_block->ext.raw_code);
    }

    glue_append(buf, &pos, GLUE_BUF_SIZE,
        "\n/* End MDC extern m glue */\n");

    return buf;
}

char *mdc_gen_mcbl_wrappers(const AstNode *extern_block) {
    if (!extern_block) return mcbl_strdup("");
    char *buf = (char *)mcbl_malloc(GLUE_BUF_SIZE);
    size_t pos = 0;
    buf[0] = '\0';

    glue_append(buf, &pos, GLUE_BUF_SIZE,
        "/* McBL# MDC - extern c wrapper (C -> McBL) */\n"
        "/* These functions allow C code to invoke McBL# inc blocks */\n"
        "\n"
        "void mcbl_invoke_inc(const char *inc_name);\n"
        "void mcbl_invoke_dev(const char *dev_name, ...);\n"
        "void mcbl_set_var_int(const char *name, long long val);\n"
        "void mcbl_set_var_str(const char *name, const char *val);\n"
        "long long mcbl_get_var_int(const char *name);\n"
        "const char *mcbl_get_var_str(const char *name);\n"
        "\n");

    if (extern_block->kind == AST_EXTERN_C_BLOCK && extern_block->ext.raw_code) {
        glue_append(buf, &pos, GLUE_BUF_SIZE,
            "/* user extern c code */\n%s\n",
            extern_block->ext.raw_code);
    }

    return buf;
}

char *mdc_generate_interop(const AstNode *program) {
    if (!program) return mcbl_strdup("");
    char *result = (char *)mcbl_malloc(GLUE_BUF_SIZE * 4);
    size_t pos = 0;
    result[0] = '\0';

    size_t rsize = GLUE_BUF_SIZE * 4;

    /* Walk the AST for extern blocks */
    const AstList *stmts = NULL;
    if (program->kind == AST_PROGRAM || program->kind == AST_BLOCK)
        stmts = &program->block.stmts;

    if (!stmts) return result;

    for (size_t i = 0; i < stmts->count; i++) {
        const AstNode *n = stmts->items[i];
        if (!n) continue;

        if (n->kind == AST_EXTERN_M_BLOCK) {
            char *glue = mdc_gen_c_glue(n);
            size_t gl = strlen(glue);
            if (pos + gl < rsize) { memcpy(result + pos, glue, gl); pos += gl; result[pos] = '\0'; }
            mcbl_free((void **)&glue);
        } else if (n->kind == AST_EXTERN_C_BLOCK) {
            char *wrap = mdc_gen_mcbl_wrappers(n);
            size_t wl = strlen(wrap);
            if (pos + wl < rsize) { memcpy(result + pos, wrap, wl); pos += wl; result[pos] = '\0'; }
            mcbl_free((void **)&wrap);
        } else if (n->kind == AST_INC_DECL) {
            /* Recurse into inc blocks */
            for (size_t j = 0; j < n->inc.body.count; j++) {
                const AstNode *m = n->inc.body.items[j];
                if (!m) continue;
                if (m->kind == AST_EXTERN_M_BLOCK) {
                    char *glue = mdc_gen_c_glue(m);
                    size_t gl = strlen(glue);
                    if (pos + gl < rsize) { memcpy(result + pos, glue, gl); pos += gl; result[pos] = '\0'; }
                    mcbl_free((void **)&glue);
                } else if (m->kind == AST_EXTERN_C_BLOCK) {
                    char *wrap = mdc_gen_mcbl_wrappers(m);
                    size_t wl = strlen(wrap);
                    if (pos + wl < rsize) { memcpy(result + pos, wrap, wl); pos += wl; result[pos] = '\0'; }
                    mcbl_free((void **)&wrap);
                }
            }
        }
    }

    return result;
}
