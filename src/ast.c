#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
   McBL# AST node implementation
   ----------------------------------------------------------------------- */

#define AST_LIST_INIT_CAP 8

AstNode *ast_node_new(AstKind kind, int line) {
    AstNode *n = (AstNode *)calloc(1, sizeof(AstNode));
    if (!n) return NULL;
    n->kind = kind;
    n->line = line;
    return n;
}

void ast_list_init(AstList *list) {
    if (!list) return;
    list->items = NULL;
    list->count = 0;
    list->cap   = 0;
}

int ast_list_push(AstList *list, AstNode *node) {
    if (!list) return -1;
    if (list->count >= list->cap) {
        size_t new_cap = list->cap == 0 ? AST_LIST_INIT_CAP : list->cap * 2;
        AstNode **tmp = (AstNode **)realloc(list->items, sizeof(AstNode *) * new_cap);
        if (!tmp) return -1;
        list->items = tmp;
        list->cap   = new_cap;
    }
    list->items[list->count++] = node;
    return 0;
}

void ast_list_free(AstList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        ast_node_free(list->items[i]);
        list->items[i] = NULL;
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap   = 0;
}

void ast_node_free(AstNode *node) {
    if (!node) return;

    switch (node->kind) {
        case AST_EXPR_STRING:
        case AST_EXPR_IDENT:
            free(node->sval);
            node->sval = NULL;
            break;

        case AST_EXPR_BINOP:
            ast_node_free(node->binop.left);
            ast_node_free(node->binop.right);
            node->binop.left  = NULL;
            node->binop.right = NULL;
            break;

        case AST_EXPR_CALL:
            free(node->call.target);
            node->call.target = NULL;
            ast_list_free(&node->call.args);
            break;

        case AST_INC_DECL:
            free(node->inc.name);
            node->inc.name = NULL;
            ast_list_free(&node->inc.body);
            break;

        case AST_DEV_DECL:
            free(node->dev.name);
            node->dev.name = NULL;
            ast_list_free(&node->dev.params);
            ast_list_free(&node->dev.body);
            break;

        case AST_VAR_DECL:
        case AST_ORGATE_DECL:
        case AST_REG_VAR_DECL:
        case AST_CONSTEXPR_DECL:
            free(node->var.name);
            node->var.name = NULL;
            ast_node_free(node->var.value);
            node->var.value = NULL;
            break;

        case AST_ASSIGN:
            free(node->assign.name);
            node->assign.name = NULL;
            ast_node_free(node->assign.value);
            node->assign.value = NULL;
            break;

        case AST_IF_STMT:
            ast_node_free(node->if_stmt.cond);
            node->if_stmt.cond = NULL;
            ast_list_free(&node->if_stmt.then_body);
            ast_list_free(&node->if_stmt.elseif_conds);
            ast_list_free(&node->if_stmt.else_body);
            break;

        case AST_FOR_STMT:
            free(node->for_stmt.var_name);
            node->for_stmt.var_name = NULL;
            ast_node_free(node->for_stmt.start);
            ast_node_free(node->for_stmt.end);
            ast_node_free(node->for_stmt.step);
            node->for_stmt.start = NULL;
            node->for_stmt.end   = NULL;
            node->for_stmt.step  = NULL;
            ast_list_free(&node->for_stmt.body);
            break;

        case AST_LOOP_STMT:
        case AST_WHILE_STMT:
            ast_node_free(node->loop_stmt.cond);
            node->loop_stmt.cond = NULL;
            ast_list_free(&node->loop_stmt.body);
            break;

        case AST_PR_STMT:
        case AST_USE_STMT:
        case AST_WAIT_FOR_STMT:
        case AST_INPUTXT_STMT:
        case AST_IMPORT_STMT:
        case AST_CALL_STMT:
        case AST_OPEN_PAGE_STMT:
            free(node->call.target);
            node->call.target = NULL;
            ast_list_free(&node->call.args);
            break;

        case AST_THREAD_STMT:
            ast_list_free(&node->thread.body);
            break;

        case AST_SEND_STMT:
            free(node->send.data_name);
            free(node->send.dest);
            node->send.data_name = NULL;
            node->send.dest      = NULL;
            break;

        case AST_DATA_BLOCK:
            free(node->data.name);
            node->data.name = NULL;
            ast_node_free(node->data.value);
            node->data.value = NULL;
            break;

        case AST_MOV_STMT:
            free(node->mov.reg);
            node->mov.reg = NULL;
            ast_node_free(node->mov.value);
            node->mov.value = NULL;
            break;

        case AST_SPLIT_STRING_STMT:
        case AST_COMBINE_STRING_STMT:
            ast_node_free(node->str_op.str);
            ast_node_free(node->str_op.delim);
            free(node->str_op.result);
            node->str_op.str    = NULL;
            node->str_op.delim  = NULL;
            node->str_op.result = NULL;
            break;

        case AST_UI_PAGE:
        case AST_UI_HEAD:
        case AST_UI_DIV:
        case AST_UI_BUTTON:
        case AST_UI_INPUT:
        case AST_UI_COLOR:
        case AST_UI_GRADIENT:
        case AST_UI_SCRIPT:
        case AST_UI_PNG:
            free(node->ui.name);
            free(node->ui.attr_name);
            free(node->ui.color_val);
            free(node->ui.png_path);
            node->ui.name      = NULL;
            node->ui.attr_name = NULL;
            node->ui.color_val = NULL;
            node->ui.png_path  = NULL;
            ast_list_free(&node->ui.children);
            ast_list_free(&node->ui.script);
            break;

        case AST_EXTERN_M_BLOCK:
        case AST_EXTERN_C_BLOCK:
            free(node->ext.raw_code);
            node->ext.raw_code = NULL;
            break;

        case AST_BLOCK:
        case AST_PROGRAM:
            ast_list_free(&node->block.stmts);
            break;

        case AST_RETURN_STMT:
            ast_node_free(node->ret.value);
            node->ret.value = NULL;
            break;

        case AST_CONST_STMT:
            free(node->var.name);
            node->var.name = NULL;
            ast_node_free(node->var.value);
            node->var.value = NULL;
            break;

        default:
            break;
    }
    free(node);
}

static const char *ast_kind_name(AstKind k) {
    switch (k) {
        case AST_PROGRAM:           return "PROGRAM";
        case AST_INC_DECL:          return "INC_DECL";
        case AST_PR_STMT:           return "PR";
        case AST_VAR_DECL:          return "VAR_DECL";
        case AST_ORGATE_DECL:       return "ORGATE";
        case AST_REG_VAR_DECL:      return "REG_VAR";
        case AST_CONSTEXPR_DECL:    return "CONSTEXPR";
        case AST_ASSIGN:            return "ASSIGN";
        case AST_IF_STMT:           return "IF";
        case AST_FOR_STMT:          return "FOR";
        case AST_LOOP_STMT:         return "LOOP";
        case AST_WHILE_STMT:        return "WHILE";
        case AST_BREAK_STMT:        return "BREAK";
        case AST_RETURN_STMT:       return "RETURN";
        case AST_CONTINUE_STMT:     return "CONTINUE";
        case AST_USE_STMT:          return "USE";
        case AST_WAIT_FOR_STMT:     return "WAIT_FOR";
        case AST_RESPONSE_STMT:     return "RESPONSE";
        case AST_INPUTXT_STMT:      return "INPUTXT";
        case AST_DEV_DECL:          return "DEV";
        case AST_CALL_STMT:         return "CALL";
        case AST_THREAD_STMT:       return "THREAD";
        case AST_IMPORT_STMT:       return "IMPORT";
        case AST_SEND_STMT:         return "SEND";
        case AST_READFILE_STMT:     return "READFILE";
        case AST_CREATEWINDOW_STMT: return "CREATEWINDOW";
        case AST_CARGO_M_STMT:      return "CARGO_m";
        case AST_CLEAR_CARGO_STMT:  return "CLEAR_cargo";
        case AST_AUTOCLEAR_STMT:    return "AUTOCLEAR";
        case AST_USING_CPU_STMT:    return "USING_CPU";
        case AST_MOV_STMT:          return "MOV";
        case AST_SPLIT_STRING_STMT: return "SPLIT_STRING";
        case AST_COMBINE_STRING_STMT: return "COMBINE_STRING";
        case AST_DATA_BLOCK:        return "DATA_BLOCK";
        case AST_EXTERN_M_BLOCK:    return "EXTERN_M";
        case AST_EXTERN_C_BLOCK:    return "EXTERN_C";
        case AST_OPEN_PAGE_STMT:    return "OPEN_PAGE";
        case AST_EXPR_INT:          return "INT";
        case AST_EXPR_FLOAT:        return "FLOAT";
        case AST_EXPR_STRING:       return "STRING";
        case AST_EXPR_BOOL:         return "BOOL";
        case AST_EXPR_IDENT:        return "IDENT";
        case AST_EXPR_BINOP:        return "BINOP";
        case AST_EXPR_UNOP:         return "UNOP";
        case AST_EXPR_CALL:         return "CALL_EXPR";
        case AST_EXPR_CHAIN_COND:   return "CHAIN_COND";
        case AST_EXPR_INDEX:        return "INDEX";
        case AST_EXPR_MEMBER:       return "MEMBER";
        case AST_UI_PAGE:           return "UI_PAGE";
        case AST_UI_HEAD:           return "UI_HEAD";
        case AST_UI_DIV:            return "UI_DIV";
        case AST_UI_BUTTON:         return "UI_BUTTON";
        case AST_UI_INPUT:          return "UI_INPUT";
        case AST_UI_COLOR:          return "UI_COLOR";
        case AST_UI_GRADIENT:       return "UI_GRADIENT";
        case AST_UI_SCRIPT:         return "UI_SCRIPT";
        case AST_UI_PNG:            return "UI_PNG";
        case AST_CONST_STMT:        return "CONST";
        case AST_BLOCK:             return "BLOCK";
        case AST_NOP:               return "NOP";
        default:                    return "?";
    }
}

void ast_dump(const AstNode *node, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; i++) printf("  ");
    printf("%s", ast_kind_name(node->kind));

    switch (node->kind) {
        case AST_EXPR_INT:   printf("  %lld", node->ival);  break;
        case AST_EXPR_FLOAT: printf("  %f",   node->fval);  break;
        case AST_EXPR_STRING:
        case AST_EXPR_IDENT: printf("  \"%s\"", node->sval ? node->sval : ""); break;
        case AST_EXPR_BOOL:  printf("  %s", node->bval ? "true" : "false"); break;
        case AST_INC_DECL:   printf("  name=%s", node->inc.name ? node->inc.name : ""); break;
        case AST_DEV_DECL:   printf("  name=%s", node->dev.name ? node->dev.name : ""); break;
        case AST_VAR_DECL:
        case AST_ORGATE_DECL:
        case AST_REG_VAR_DECL:
        case AST_CONSTEXPR_DECL:
            printf("  %c%s", node->var.sigil, node->var.name ? node->var.name : "");
            break;
        default: break;
    }
    printf("\n");

    /* recurse into children */
    switch (node->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < node->block.stmts.count; i++)
                ast_dump(node->block.stmts.items[i], indent + 1);
            break;
        case AST_INC_DECL:
            for (size_t i = 0; i < node->inc.body.count; i++)
                ast_dump(node->inc.body.items[i], indent + 1);
            break;
        case AST_DEV_DECL:
            for (size_t i = 0; i < node->dev.body.count; i++)
                ast_dump(node->dev.body.items[i], indent + 1);
            break;
        case AST_VAR_DECL:
        case AST_ORGATE_DECL:
        case AST_REG_VAR_DECL:
        case AST_CONSTEXPR_DECL:
            ast_dump(node->var.value, indent + 1);
            break;
        case AST_ASSIGN:
            ast_dump(node->assign.value, indent + 1);
            break;
        case AST_IF_STMT:
            ast_dump(node->if_stmt.cond, indent + 1);
            for (size_t i = 0; i < node->if_stmt.then_body.count; i++)
                ast_dump(node->if_stmt.then_body.items[i], indent + 1);
            for (size_t i = 0; i < node->if_stmt.else_body.count; i++)
                ast_dump(node->if_stmt.else_body.items[i], indent + 1);
            break;
        case AST_EXPR_BINOP:
            ast_dump(node->binop.left,  indent + 1);
            ast_dump(node->binop.right, indent + 1);
            break;
        case AST_PR_STMT:
        case AST_CALL_STMT:
        case AST_EXPR_CALL:
            for (size_t i = 0; i < node->call.args.count; i++)
                ast_dump(node->call.args.items[i], indent + 1);
            break;
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
                ast_dump(node->ui.children.items[i], indent + 1);
            break;
        default: break;
    }
}
