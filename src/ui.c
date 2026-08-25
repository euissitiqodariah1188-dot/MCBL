#include "ui.h"
#include "memory.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* -----------------------------------------------------------------------
   McBL# UI Markup Processor implementation
   ----------------------------------------------------------------------- */

#define UI_INIT_CAP 32768

UiGen *ui_gen_create(UiTarget target) {
    UiGen *g = (UiGen *)mcbl_calloc(1, sizeof(UiGen));
    g->output = (char *)mcbl_malloc(UI_INIT_CAP);
    g->output[0] = '\0';
    g->cap    = UI_INIT_CAP;
    g->len    = 0;
    g->target = target;
    g->indent = 0;
    return g;
}

void ui_gen_destroy(UiGen *g) {
    if (!g) return;
    mcbl_free((void **)&g->output);
    mcbl_free((void **)&g);
}

static void ui_grow(UiGen *g, size_t extra) {
    while (g->len + extra + 1 >= g->cap) {
        g->cap   *= 2;
        g->output = (char *)mcbl_realloc(g->output, g->cap);
    }
}

static void ui_write(UiGen *g, const char *fmt, ...) {
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    ui_grow(g, (size_t)n + 1);
    memcpy(g->output + g->len, tmp, (size_t)n);
    g->len += (size_t)n;
    g->output[g->len] = '\0';
}

static void ui_indent_write(UiGen *g, const char *fmt, ...) {
    for (int i = 0; i < g->indent; i++) ui_write(g, "  ");
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    ui_write(g, "%s", tmp);
}

static void ui_node_html(UiGen *g, const AstNode *n);

/* Compile McBL# script block to inline JS */
static void ui_script_to_js(UiGen *g, const AstList *script) {
    ui_write(g, "<script>\n");
    for (size_t i = 0; i < script->count; i++) {
        const AstNode *n = script->items[i];
        if (!n) continue;
        switch (n->kind) {
            case AST_CONST_STMT:
                /* const btn = id → const btn = document.getElementById('id') */
                if (n->var.name && n->var.value && n->var.value->kind == AST_EXPR_IDENT) {
                    ui_indent_write(g, "const %s = document.getElementById('%s');\n",
                                    n->var.name, n->var.value->sval ? n->var.value->sval : "");
                }
                break;
            case AST_IF_STMT:
                /* if btn clicked do; openpage(X) */
                if (n->if_stmt.cond) {
                    const AstNode *cond = n->if_stmt.cond;
                    /* clicked event */
                    if (cond->kind == AST_EXPR_BINOP &&
                        cond->binop.right &&
                        cond->binop.right->kind == AST_EXPR_IDENT &&
                        cond->binop.right->sval &&
                        strcmp(cond->binop.right->sval, "clicked") == 0) {
                        const AstNode *btn = cond->binop.left;
                        const char *btn_name = (btn && btn->kind == AST_EXPR_IDENT) ? btn->sval : "element";
                        ui_indent_write(g, "document.getElementById('%s').addEventListener('click', function() {\n", btn_name);
                        g->indent++;
                        for (size_t j = 0; j < n->if_stmt.then_body.count; j++) {
                            const AstNode *s = n->if_stmt.then_body.items[j];
                            if (!s) continue;
                            if (s->kind == AST_OPEN_PAGE_STMT) {
                                ui_indent_write(g, "window.location.href = '%s.html';\n",
                                                s->call.target ? s->call.target : "");
                            } else if (s->kind == AST_PR_STMT && s->call.args.count > 0) {
                                const AstNode *arg = s->call.args.items[0];
                                if (arg && arg->kind == AST_EXPR_STRING)
                                    ui_indent_write(g, "console.log('%s');\n", arg->sval ? arg->sval : "");
                            }
                        }
                        g->indent--;
                        ui_indent_write(g, "});\n");
                    } else {
                        /* plain if */
                        ui_indent_write(g, "if (");
                        /* simplified: just emit condition as JS comment */
                        ui_write(g, "true /* McBL# condition */ ");
                        ui_write(g, ") {\n");
                        g->indent++;
                        for (size_t j = 0; j < n->if_stmt.then_body.count; j++)
                            ui_node_html(g, n->if_stmt.then_body.items[j]);
                        g->indent--;
                        ui_indent_write(g, "}\n");
                    }
                }
                break;
            case AST_OPEN_PAGE_STMT:
                ui_indent_write(g, "window.location.href = '%s.html';\n",
                                n->call.target ? n->call.target : "");
                break;
            case AST_PR_STMT:
                if (n->call.args.count > 0) {
                    const AstNode *arg = n->call.args.items[0];
                    if (arg && arg->kind == AST_EXPR_STRING)
                        ui_indent_write(g, "console.log(\"%s\");\n", arg->sval ? arg->sval : "");
                }
                break;
            default:
                break;
        }
    }
    ui_write(g, "</script>\n");
}

static void ui_node_html(UiGen *g, const AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_UI_PAGE: {
            /* <PAGE_NAME> becomes the HTML page wrapper */
            ui_write(g, "<!DOCTYPE html>\n<html lang=\"en\">\n");
            ui_write(g, "<head><meta charset=\"UTF-8\">\n");
            ui_write(g, "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n");
            ui_write(g, "<title>%s</title>\n", n->ui.name ? n->ui.name : "McBL# App");
            ui_write(g, "<style>body{font-family:sans-serif;margin:0;padding:20px;}</style>\n");
            ui_write(g, "</head>\n<body>\n");
            g->indent++;
            for (size_t i = 0; i < n->ui.children.count; i++)
                ui_node_html(g, n->ui.children.items[i]);
            if (n->ui.script.count > 0)
                ui_script_to_js(g, &n->ui.script);
            g->indent--;
            ui_write(g, "</body>\n</html>\n");
            break;
        }

        case AST_UI_HEAD:
            /* <head> section already written in page; skip here */
            for (size_t i = 0; i < n->ui.children.count; i++)
                ui_node_html(g, n->ui.children.items[i]);
            break;

        case AST_UI_DIV:
            ui_indent_write(g, "<div");
            if (n->ui.name && n->ui.name[0])
                ui_write(g, " id=\"%s\"", n->ui.name);
            ui_write(g, ">\n");
            g->indent++;
            for (size_t i = 0; i < n->ui.children.count; i++)
                ui_node_html(g, n->ui.children.items[i]);
            g->indent--;
            ui_indent_write(g, "</div>\n");
            break;

        case AST_UI_BUTTON:
            ui_indent_write(g, "<button id=\"%s\" type=\"button\">%s</button>\n",
                            n->ui.attr_name ? n->ui.attr_name : "btn",
                            n->ui.attr_name ? n->ui.attr_name : "Click");
            break;

        case AST_UI_INPUT:
            ui_indent_write(g, "<input id=\"%s\" type=\"text\" name=\"%s\" />\n",
                            n->ui.attr_name ? n->ui.attr_name : "input",
                            n->ui.attr_name ? n->ui.attr_name : "input");
            break;

        case AST_UI_COLOR:
            ui_indent_write(g, "<style>body { background-color: #%s; }</style>\n",
                            n->ui.color_val ? n->ui.color_val : "ffffff");
            break;

        case AST_UI_GRADIENT:
            ui_indent_write(g, "<style>body { background: linear-gradient(135deg, #%s, #%s); }</style>\n",
                            n->ui.color_val ? n->ui.color_val : "ffffff",
                            n->ui.color_val ? n->ui.color_val : "000000");
            break;

        case AST_UI_PNG:
            ui_indent_write(g, "<img src=\"%s\" alt=\"image\" />\n",
                            n->ui.png_path ? n->ui.png_path : "");
            break;

        case AST_UI_SCRIPT:
            if (n->ui.script.count > 0)
                ui_script_to_js(g, &n->ui.script);
            break;

        case AST_PR_STMT:
            /* pr inside UI = <p> text </p> */
            if (n->call.args.count > 0) {
                const AstNode *arg = n->call.args.items[0];
                if (arg && arg->kind == AST_EXPR_STRING)
                    ui_indent_write(g, "<p>%s</p>\n", arg->sval ? arg->sval : "");
            }
            break;

        default:
            break;
    }
}

static void collect_ui_text(UiGen *g, const AstNode *node) {
    if (!node) return;
    switch (node->kind) {
        case AST_UI_PAGE:
        case AST_UI_HEAD:
        case AST_UI_DIV:
        case AST_UI_BUTTON:
        case AST_UI_INPUT:
        case AST_UI_COLOR:
        case AST_UI_GRADIENT:
        case AST_UI_SCRIPT:
        case AST_UI_PNG:
            for (size_t i = 0; i < node->ui.children.count; i++)
                collect_ui_text(g, node->ui.children.items[i]);
            break;
        case AST_EXPR_STRING:
            if (node->sval && node->sval[0])
                ui_write(g, "    blen += snprintf(body+blen, sizeof(body)-blen, \"%%s\\n\", \"%s\");\n", node->sval);
            break;
        case AST_EXPR_IDENT:
            if (node->sval && node->sval[0])
                ui_write(g, "    blen += snprintf(body+blen, sizeof(body)-blen, \"%%s \", \"%s\");\n", node->sval);
            break;
        case AST_EXPR_BINOP:
            collect_ui_text(g, node->binop.left);
            collect_ui_text(g, node->binop.right);
            break;
        case AST_PR_STMT:
        case AST_CALL_STMT:
        case AST_EXPR_CALL:
            for (size_t i = 0; i < node->call.args.count; i++)
                collect_ui_text(g, node->call.args.items[i]);
            break;
        case AST_IF_STMT:
            for (size_t i = 0; i < node->if_stmt.then_body.count; i++)
                collect_ui_text(g, node->if_stmt.then_body.items[i]);
            for (size_t i = 0; i < node->if_stmt.else_body.count; i++)
                collect_ui_text(g, node->if_stmt.else_body.items[i]);
            break;
        case AST_CONST_STMT:
            if (node->var.name)
                ui_write(g, "    blen += snprintf(body+blen, sizeof(body)-blen, \"%%s \", \"%s\");\n", node->var.name);
            break;
        case AST_OPEN_PAGE_STMT:
            if (node->call.target)
                ui_write(g, "    blen += snprintf(body+blen, sizeof(body)-blen, \"[%%s]\\n\", \"%s\");\n", node->call.target);
            break;
        default:
            if (node->sval && node->sval[0])
                ui_write(g, "    blen += snprintf(body+blen, sizeof(body)-blen, \"%%s \", \"%s\");\n", node->sval);
            break;
    }
}

int ui_gen_compile(UiGen *g, const AstNode *ui_node) {
    if (!g || !ui_node) return -1;

    if (g->target == UI_TARGET_HTML) {
        ui_node_html(g, ui_node);
    } else {
        /* Native target: emit C code that opens a native window */
        const char *raw_name = (ui_node->kind == AST_UI_PAGE && ui_node->ui.name)
                               ? ui_node->ui.name : "McBLApp";

        /* Sanitize name for C identifier */
        char safe_name[128] = {0};
        for (int i = 0; raw_name[i] && i < 127; i++) {
            char c = raw_name[i];
            if (isalnum((unsigned char)c) || c == '_') safe_name[i] = c;
            else safe_name[i] = '_';
        }
        if (!safe_name[0]) { safe_name[0] = 'M'; safe_name[1] = 0; }

        ui_write(g, "/* McBL# Native UI for: %s */\n", raw_name);
        ui_write(g, "#include <stdio.h>\n");
        ui_write(g, "#include <string.h>\n");
        ui_write(g, "#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)\n");
        ui_write(g, "  #include <windows.h>\n");
        ui_write(g, "  static void mcbl_show_window(const char *title, const char *body) {\n");
        ui_write(g, "    MessageBoxA(NULL, body, title, MB_OK | MB_ICONINFORMATION);\n");
        ui_write(g, "  }\n");
        ui_write(g, "#else\n");
        ui_write(g, "  #include <X11/Xlib.h>\n");
        ui_write(g, "  #include <time.h>\n");
        ui_write(g, "  static void mcbl_show_window(const char *title, const char *body) {\n");
        ui_write(g, "    Display *d = XOpenDisplay(NULL);\n");
        ui_write(g, "    if (!d) { fprintf(stderr, \"McBL# UI: cannot open X11 display\\n\"); return; }\n");
        ui_write(g, "    int s = DefaultScreen(d);\n");
        ui_write(g, "    Window w = XCreateSimpleWindow(d, RootWindow(d,s), 10,10, 400,300, 1,\n");
        ui_write(g, "                                   BlackPixel(d,s), WhitePixel(d,s));\n");
        ui_write(g, "    XSelectInput(d, w, ExposureMask | KeyPressMask);\n");
        ui_write(g, "    XMapWindow(d, w);\n");
        ui_write(g, "    GC gc = DefaultGC(d, s);\n");
        ui_write(g, "    XStoreName(d, w, title);\n");
        ui_write(g, "    XDrawString(d, w, gc, 20, 30, body, (int)strlen(body));\n");
        ui_write(g, "    XFlush(d);\n");
        ui_write(g, "    for (int i = 0; i < 100; i++) { XFlush(d);\n");
        ui_write(g, "      struct timespec ts = {0, 100000000}; nanosleep(&ts, NULL); }\n");
        ui_write(g, "    XCloseDisplay(d);\n");
        ui_write(g, "  }\n");
        ui_write(g, "#endif\n\n");

        ui_write(g, "int main(void) {\n");
        ui_write(g, "    char body[4096] = {0};\n");
        ui_write(g, "    int blen = 0;\n");

        /* Recursively collect all text from UI tree */
        if (ui_node->kind == AST_UI_PAGE) {
            for (size_t i = 0; i < ui_node->ui.children.count; i++) {
                const AstNode *child = ui_node->ui.children.items[i];
                if (!child) continue;
                /* Collect text from UI subtree */
                collect_ui_text(g, child);
            }
        }
        ui_write(g, "    mcbl_show_window(\"%s\", body);\n", raw_name);
        ui_write(g, "    return 0;\n");
        ui_write(g, "}\n");
    }
    return 0;
}

int ui_gen_write(UiGen *g, const char *path) {
    if (!g || !path) return -1;
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "McBL# UI: cannot write to %s\n", path); return -1; }
    fwrite(g->output, 1, g->len, f);
    fclose(f);
    return 0;
}
