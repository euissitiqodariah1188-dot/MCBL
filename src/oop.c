/* McBL# OOP System — v2.0 implementation */
#include "oop.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Class Registry ---- */
static size_t g_live_objects = 0;
static ClassRegistry g_class_reg;
static int           g_class_reg_init = 0;

/* ---- GC state ---- */



ClassRegistry *class_registry(void) { return &g_class_reg; }

void class_registry_init(void) {
    if (g_class_reg_init) return;
    memset(&g_class_reg, 0, sizeof(g_class_reg));
    g_class_reg_init = 1;
}
void class_registry_shutdown(void) {
    oop_mem_shutdown();
    if (!g_class_reg_init) return;
    /* Free class descriptors */
    for (int i = 0; i < g_class_reg.count; i++) {
        McblClass *cls = g_class_reg.classes[i];
        if (!cls) continue;
        free(cls->name);
        if (cls->fields) {
            for (int j = 0; j < cls->field_count; j++) {
                free(cls->fields[j].name);
                free(cls->fields[j].type_name);
            }
            free(cls->fields);
        }
        if (cls->methods) {
            for (int j = 0; j < cls->method_count; j++) {
                free(cls->methods[j].name);
                free(cls->methods[j].ret_type);
            }
            free(cls->methods);
        }
        if (cls->static_storage) free(cls->static_storage);
        free(cls);
    }
    g_class_reg_init = 0;
}

int class_register(McblClass *cls) {
    if (!g_class_reg_init) class_registry_init();
    if (!cls || g_class_reg.count >= CLASS_REG_MAX) return -1;
    /* Check duplicate */
    if (class_lookup(cls->name)) {
        fprintf(stderr, "OOP: class '%s' already registered\n", cls->name);
        return -1;
    }
    g_class_reg.classes[g_class_reg.count++] = cls;
    return 0;
}

McblClass *class_lookup(const char *name) {
    if (!name || !g_class_reg_init) return NULL;
    for (int i = 0; i < g_class_reg.count; i++) {
        if (g_class_reg.classes[i] && strcmp(g_class_reg.classes[i]->name, name) == 0)
            return g_class_reg.classes[i];
    }
    return NULL;
}

int class_implements(const McblClass *cls, const McblInterface *iface) {
    if (!cls || !iface) return 0;
    for (int i = 0; i < cls->iface_count; i++) {
        if (strcmp(cls->ifaces[i]->name, iface->name) == 0) return 1;
    }
    /* Check base class too */
    if (cls->base) return class_implements(cls->base, iface);
    return 0;
}

int obj_instanceof(const McblObject *obj, const McblClass *cls) {
    if (!obj || !cls || !obj->hdr || !obj->hdr->cls) return 0;
    McblClass *c = obj->hdr->cls;
    while (c) {
        if (strcmp(c->name, cls->name) == 0) return 1;
        c = c->base;
    }
    return 0;
}

/* ---- Object lifecycle ---- */
McblObject *obj_new(McblClass *cls) {
    if (!cls) return NULL;

    size_t total = sizeof(ObjHeader) + cls->instance_size + 64; /* +64 padding */
    char *raw = (char *)calloc(1, total);
    if (!raw) return NULL;

    McblObject *obj = (McblObject *)malloc(sizeof(McblObject));
    if (!obj) { free(raw); return NULL; }

    ObjHeader *hdr = (ObjHeader *)raw;
    hdr->cls        = cls;
    hdr->refs       = 1;
    hdr->flags      = 0;
    hdr->generation = 0;

    obj->hdr  = hdr;
    obj->data = raw + sizeof(ObjHeader);

    g_live_objects++;

    return obj;
}

void obj_ref(McblObject *obj) { if (obj && obj->hdr) obj->hdr->refs++; }

void obj_unref(McblObject *obj) {
    if (!obj || !obj->hdr) return;
    obj->hdr->refs--;
    if (obj->hdr->refs <= 0) {
        /* Run destructor if exists — immediate, no GC pause */
        MethodDesc *dtor = obj_lookup_method(obj, "deinit");
        if (dtor && dtor->fn_ptr) {
            typedef void (*DtorFn)(McblObject *);
            ((DtorFn)dtor->fn_ptr)(obj);
        }
        free(obj->hdr);
        free(obj);
        if (g_live_objects > 0) g_live_objects--;
    }
}

/* Field layout: we use a simple name→offset map stored in FieldDesc.offset */
static FieldDesc *find_field(McblObject *obj, const char *name) {
    if (!obj || !obj->hdr || !obj->hdr->cls || !name) return NULL;
    McblClass *c = obj->hdr->cls;
    while (c) {
        for (int i = 0; i < c->field_count; i++) {
            if (strcmp(c->fields[i].name, name) == 0) return &c->fields[i];
        }
        c = c->base;
    }
    return NULL;
}

void *obj_field_ptr(McblObject *obj, const char *name) {
    FieldDesc *f = find_field(obj, name);
    if (!f) return NULL;
    return (char *)obj->data + f->offset;
}
void obj_field_set_int(McblObject *obj, const char *name, int64_t val) {
    void *p = obj_field_ptr(obj, name);
    if (p) *(int64_t *)p = val;
}
void obj_field_set_float(McblObject *obj, const char *name, double val) {
    void *p = obj_field_ptr(obj, name);
    if (p) *(double *)p = val;
}
void obj_field_set_str(McblObject *obj, const char *name, const char *val) {
    FieldDesc *f = find_field(obj, name);
    if (!f) return;
    char **strptr = (char **)((char *)obj->data + f->offset);
    free(*strptr);
    *strptr = val ? strdup(val) : NULL;
}
void obj_field_set_obj(McblObject *obj, const char *name, McblObject *val) {
    void *p = obj_field_ptr(obj, name);
    if (p) *(McblObject **)p = val;
    if (val) obj_ref(val);
}
int64_t obj_field_get_int(McblObject *obj, const char *name) {
    void *p = obj_field_ptr(obj, name);
    return p ? *(int64_t *)p : 0;
}
double obj_field_get_float(McblObject *obj, const char *name) {
    void *p = obj_field_ptr(obj, name);
    return p ? *(double *)p : 0.0;
}
char *obj_field_get_str(McblObject *obj, const char *name) {
    void *p = obj_field_ptr(obj, name);
    return p ? *(char **)p : NULL;
}
McblObject *obj_field_get_obj(McblObject *obj, const char *name) {
    void *p = obj_field_ptr(obj, name);
    return p ? *(McblObject **)p : NULL;
}

/* ---- vtable dispatch ---- */
MethodDesc *obj_lookup_method(McblObject *obj, const char *name) {
    if (!obj || !obj->hdr || !obj->hdr->cls) return NULL;
    McblClass *c = obj->hdr->cls;
    while (c) {
        /* Search vtable first (virtual dispatch) */
        for (int i = 0; i < c->vtable.count; i++) {
            if (c->vtable.entries[i] && strcmp(c->vtable.entries[i]->name, name) == 0)
                return c->vtable.entries[i];
        }
        /* Then non-virtual methods */
        for (int i = 0; i < c->method_count; i++) {
            if (strcmp(c->methods[i].name, name) == 0)
                return &c->methods[i];
        }
        c = c->base;
    }
    return NULL;
}

int obj_call_method(void *vm, McblObject *obj, const char *method_name,
                    void **args, int argc) {
    (void)vm; (void)args; (void)argc;
    MethodDesc *m = obj_lookup_method(obj, method_name);
    if (!m) {
        fprintf(stderr, "OOP: method '%s' not found on '%s'\n",
                method_name, obj->hdr->cls->name);
        return -1;
    }
    if (m->fn_ptr) {
        /* Native method — call directly */
        typedef int (*MethodFn)(McblObject *, void **, int);
        return ((MethodFn)m->fn_ptr)(obj, args, argc);
    }
    /* Bytecode method — will be dispatched by MDK VM via bc_addr */
    return m->bc_addr;
}

/* ---- Memory management — pure refcount, NO GC ---- */

void oop_mem_init(void) {
    g_live_objects = 0;
}
void oop_mem_shutdown(void) {
    if (g_live_objects > 0)
        fprintf(stderr, "OOP: %zu objects still alive at shutdown\n", g_live_objects);
}
size_t oop_mem_alive(void) { return g_live_objects; }

/* ---- classInt support ---- */
McblClass *classint_new(const char *name) {
    McblClass *cls = (McblClass *)calloc(1, sizeof(McblClass));
    if (!cls) return NULL;
    cls->name = strdup(name);
    return cls;
}
void classint_add_field(McblClass *cls, const char *fname,
                         const char *type_name, int access) {
    if (!cls || !fname) return;
    cls->fields = (FieldDesc *)realloc(cls->fields,
                   (cls->field_count + 1) * sizeof(FieldDesc));
    if (!cls->fields) return;
    FieldDesc *f = &cls->fields[cls->field_count++];
    memset(f, 0, sizeof(FieldDesc));
    f->name      = strdup(fname);
    f->type_name = type_name ? strdup(type_name) : strdup("auto");
    f->access    = access;
    /* Compute offset: simple bump */
    size_t prev_off = (cls->field_count > 1) ?
        cls->fields[cls->field_count - 2].offset +
        cls->fields[cls->field_count - 2].size : 0;
    f->offset = prev_off;
    f->size   = 8; /* uniform 8 bytes per field for simplicity */
    cls->instance_size = f->offset + f->size;
}
void classint_add_method(McblClass *cls, MethodDesc *m) {
    if (!cls || !m) return;
    cls->methods = (MethodDesc *)realloc(cls->methods,
                    (cls->method_count + 1) * sizeof(MethodDesc));
    if (!cls->methods) return;
    memcpy(&cls->methods[cls->method_count++], m, sizeof(MethodDesc));
    /* Add to vtable if virtual */
    if (m->is_virtual && cls->vtable.count < VTABLE_MAX)
        cls->vtable.entries[cls->vtable.count++] = &cls->methods[cls->method_count - 1];
}
McblObject *classint_activate(McblClass *cls) {
    if (!cls) return NULL;
    McblObject *obj = obj_new(cls);
    class_register(cls);
    return obj;
}
int classint_share_field(McblClass *from, McblClass *to, const char *field_name) {
    /* Allow cross-classInt variable access by storing a pointer in 'to' */
    if (!from || !to || !field_name) return -1;
    FieldDesc *f = NULL;
    for (int i = 0; i < from->field_count; i++) {
        if (strcmp(from->fields[i].name, field_name) == 0) { f = &from->fields[i]; break; }
    }
    if (!f) return -1;
    classint_add_field(to, field_name, f->type_name, f->access);
    return 0;
}

/* ---- Interface system ---- */
McblInterface *iface_new(const char *name) {
    McblInterface *iface = (McblInterface *)calloc(1, sizeof(McblInterface));
    if (!iface) return NULL;
    iface->name = strdup(name);
    return iface;
}
void iface_add_method(McblInterface *iface, const char *method_name, const char *ret_type) {
    if (!iface) return;
    iface->methods = (MethodDesc *)realloc(iface->methods,
                      (iface->method_count + 1) * sizeof(MethodDesc));
    if (!iface->methods) return;
    MethodDesc *m = &iface->methods[iface->method_count++];
    memset(m, 0, sizeof(MethodDesc));
    m->name     = strdup(method_name);
    m->ret_type = ret_type ? strdup(ret_type) : strdup("void");
}
int iface_validate(const McblClass *cls, const McblInterface *iface) {
    if (!cls || !iface) return 0;
    for (int i = 0; i < iface->method_count; i++) {
        const char *mname = iface->methods[i].name;
        int found = 0;
        McblClass *c = (McblClass *)cls;
        while (c && !found) {
            for (int j = 0; j < c->method_count; j++) {
                if (strcmp(c->methods[j].name, mname) == 0) { found = 1; break; }
            }
            c = c->base;
        }
        if (!found) {
            fprintf(stderr, "OOP: class '%s' missing method '%s' from interface '%s'\n",
                    cls->name, mname, iface->name);
            return 0;
        }
    }
    return 1;
}
