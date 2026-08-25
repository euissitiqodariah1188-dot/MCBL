#include "symbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
   McBL# Symbol Table implementation
   ----------------------------------------------------------------------- */

static unsigned int sym_hash(const char *name) {
    unsigned int h = 5381;
    while (*name) h = ((h << 5) + h) + (unsigned char)(*name++);
    return h % SYMTABLE_BUCKETS;
}

static SymScope *scope_create(SymScope *parent) {
    SymScope *s = (SymScope *)calloc(1, sizeof(SymScope));
    if (!s) return NULL;
    s->parent = parent;
    return s;
}

static void scope_destroy(SymScope *s) {
    if (!s) return;
    for (int i = 0; i < SYMTABLE_BUCKETS; i++) {
        Symbol *sym = s->buckets[i];
        while (sym) {
            Symbol *next = sym->next;
            free(sym->name);
            sym->name = NULL;
            if (sym->kind == SYM_VAR_STRING || sym->kind == SYM_CONST) {
                free(sym->val.sval);
                sym->val.sval = NULL;
            }
            free(sym);
            sym = next;
        }
        s->buckets[i] = NULL;
    }
    free(s);
}

SymTable *symtable_create(void) {
    SymTable *st = (SymTable *)malloc(sizeof(SymTable));
    if (!st) return NULL;
    st->current = scope_create(NULL);
    if (!st->current) { free(st); return NULL; }
    return st;
}

void symtable_destroy(SymTable *st) {
    if (!st) return;
    SymScope *s = st->current;
    while (s) {
        SymScope *parent = s->parent;
        scope_destroy(s);
        s = parent;
    }
    st->current = NULL;
    free(st);
}

void symtable_push_scope(SymTable *st) {
    if (!st) return;
    SymScope *ns = scope_create(st->current);
    if (ns) st->current = ns;
}

void symtable_pop_scope(SymTable *st) {
    if (!st || !st->current) return;
    SymScope *parent = st->current->parent;
    scope_destroy(st->current);
    st->current = parent;
}

int symtable_insert(SymTable *st, const char *name, SymKind kind, char sigil) {
    if (!st || !name) return -1;
    unsigned int h = sym_hash(name);
    /* check existing in current scope */
    Symbol *existing = st->current->buckets[h];
    while (existing) {
        if (strcmp(existing->name, name) == 0) return 1; /* already defined */
        existing = existing->next;
    }
    Symbol *sym = (Symbol *)calloc(1, sizeof(Symbol));
    if (!sym) return -1;
    sym->name  = strdup(name);
    if (!sym->name) { free(sym); return -1; }
    sym->kind  = kind;
    sym->sigil = sigil;
    sym->func_addr = -1;
    sym->next  = st->current->buckets[h];
    st->current->buckets[h] = sym;
    return 0;
}

Symbol *symtable_lookup(SymTable *st, const char *name) {
    if (!st || !name) return NULL;
    unsigned int h = sym_hash(name);
    SymScope *s = st->current;
    while (s) {
        Symbol *sym = s->buckets[h];
        while (sym) {
            if (strcmp(sym->name, name) == 0) return sym;
            sym = sym->next;
        }
        s = s->parent;
    }
    return NULL;
}

void symtable_set_int(SymTable *st, const char *name, long long v) {
    Symbol *s = symtable_lookup(st, name);
    if (!s) { symtable_insert(st, name, SYM_VAR_INT, '#'); s = symtable_lookup(st, name); }
    if (s) { s->kind = SYM_VAR_INT; s->val.ival = v; }
}

void symtable_set_float(SymTable *st, const char *name, double v) {
    Symbol *s = symtable_lookup(st, name);
    if (!s) { symtable_insert(st, name, SYM_VAR_FLOAT, '#'); s = symtable_lookup(st, name); }
    if (s) { s->kind = SYM_VAR_FLOAT; s->val.fval = v; }
}

void symtable_set_string(SymTable *st, const char *name, const char *v) {
    Symbol *s = symtable_lookup(st, name);
    if (!s) { symtable_insert(st, name, SYM_VAR_STRING, '#'); s = symtable_lookup(st, name); }
    if (s) {
        s->kind = SYM_VAR_STRING;
        if (s->val.sval) { free(s->val.sval); s->val.sval = NULL; }
        s->val.sval = v ? strdup(v) : NULL;
    }
}

void symtable_set_bool(SymTable *st, const char *name, int v) {
    Symbol *s = symtable_lookup(st, name);
    if (!s) { symtable_insert(st, name, SYM_VAR_BOOL, '#'); s = symtable_lookup(st, name); }
    if (s) { s->kind = SYM_VAR_BOOL; s->val.bval = v; }
}

void symtable_set_ptr(SymTable *st, const char *name, void *v) {
    Symbol *s = symtable_lookup(st, name);
    if (!s) { symtable_insert(st, name, SYM_VAR_OBJECT, '#'); s = symtable_lookup(st, name); }
    if (s) { s->val.pval = v; }
}

void symtable_dump(const SymTable *st) {
    if (!st) return;
    const SymScope *sc = st->current;
    int depth = 0;
    while (sc) {
        printf("[Scope %d]\n", depth++);
        for (int i = 0; i < SYMTABLE_BUCKETS; i++) {
            const Symbol *sym = sc->buckets[i];
            while (sym) {
                printf("  %s (kind=%d)\n", sym->name, (int)sym->kind);
                sym = sym->next;
            }
        }
        sc = sc->parent;
    }
}
