#ifndef MCBL_OOP_H
#define MCBL_OOP_H

/*
 * McBL# OOP System  (v2.0)
 * ===========================
 * Full OOP: class, inheritance, interface, method dispatch.
 * Zero memory leak by design — semua object pake refcount + GC.
 *
 * Syntax McBL#:
 *
 *   class Animal {
 *     pub #name = ""
 *     pub dev speak() {
 *       pr("...")
 *     }
 *   }
 *
 *   class Dog extends Animal {
 *     override dev speak() {
 *       pr("Woof!")
 *     }
 *   }
 *
 *   #d = new Dog()
 *   d.name = "Rex"
 *   d.speak()
 *
 * classInt / subclass syntax (from spec):
 *
 *   inc(impl);
 *     classInt(varClass);
 *       #hello = "world"
 *     classInt(printClass);
 *       pr(hello)
 *     printClass()
 *   endinc;
 */

#include <stddef.h>
#include <stdint.h>
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------
   Type descriptors
   ------------------------------------------------------------------- */
typedef struct McblClass McblClass;
typedef struct McblObject McblObject;
typedef struct McblInterface McblInterface;

/* Field descriptor */
typedef struct {
    char       *name;
    char       *type_name;
    int         access;      /* 0=pub 1=priv 2=prot */
    int         is_static;
    size_t      offset;      /* byte offset in object layout */
    size_t      size;        /* byte size */
} FieldDesc;

/* Method descriptor */
typedef struct {
    char        *name;
    int          access;
    int          is_static;
    int          is_abstract;
    int          is_virtual;
    int          is_override;
    int          is_async;
    int          is_inline;
    /* Function pointer — runtime dispatch */
    void        *fn_ptr;
    /* Bytecode address (for VM path) */
    int          bc_addr;
    char        *ret_type;
    char       **param_types;
    int          param_count;
} MethodDesc;

/* vtable — virtual method table, one per class */
#define VTABLE_MAX 256
typedef struct {
    MethodDesc *entries[VTABLE_MAX];
    int         count;
} VTable;

/* Interface descriptor */
struct McblInterface {
    char         *name;
    MethodDesc   *methods;
    int           method_count;
    McblInterface *next;
};

/* Class descriptor */
struct McblClass {
    char         *name;
    McblClass    *base;              /* superclass or NULL */
    McblInterface **ifaces;
    int           iface_count;
    int           is_abstract;
    int           is_final;

    FieldDesc    *fields;
    int           field_count;
    size_t        instance_size;     /* total object size in bytes */

    MethodDesc   *methods;
    int           method_count;
    VTable        vtable;

    /* classInt inner classes (linked list) */
    McblClass    *inner_classes;
    McblClass    *inner_next;

    /* static field storage */
    void         *static_storage;
    size_t        static_size;
};

/* Object header (lives before user data in every heap object) */
typedef struct {
    McblClass   *cls;            /* pointer to class descriptor */
    int          refs;           /* reference count */
    uint32_t     flags;          /* GC / debug flags */
    uint32_t     generation;     /* GC generation */
} ObjHeader;

/* Object handle — points to user data (after header) */
struct McblObject {
    ObjHeader   *hdr;
    void        *data;           /* field data */
};

/* -------------------------------------------------------------------
   Class registry
   ------------------------------------------------------------------- */
#define CLASS_REG_MAX 4096

typedef struct {
    McblClass *classes[CLASS_REG_MAX];
    int        count;
} ClassRegistry;

ClassRegistry *class_registry(void);
void           class_registry_init(void);
void           class_registry_shutdown(void);

/* Register a class definition */
int            class_register(McblClass *cls);

/* Look up by name */
McblClass     *class_lookup(const char *name);

/* Check if cls implements iface */
int            class_implements(const McblClass *cls,
                                 const McblInterface *iface);

/* Check if obj is instance of cls or subclass */
int            obj_instanceof(const McblObject *obj,
                               const McblClass *cls);

/* -------------------------------------------------------------------
   Object lifecycle
   ------------------------------------------------------------------- */

/* Allocate new instance of cls */
McblObject *obj_new      (McblClass *cls);

/* Reference counting */
void        obj_ref      (McblObject *obj);
void        obj_unref    (McblObject *obj);  /* frees when refs=0 */

/* Field access */
void       *obj_field_ptr(McblObject *obj, const char *field_name);
void        obj_field_set_int   (McblObject *obj, const char *name,
                                  int64_t val);
void        obj_field_set_float (McblObject *obj, const char *name,
                                  double val);
void        obj_field_set_str   (McblObject *obj, const char *name,
                                  const char *val);
void        obj_field_set_obj   (McblObject *obj, const char *name,
                                  McblObject *val);
int64_t     obj_field_get_int   (McblObject *obj, const char *name);
double      obj_field_get_float (McblObject *obj, const char *name);
char       *obj_field_get_str   (McblObject *obj, const char *name);
McblObject *obj_field_get_obj   (McblObject *obj, const char *name);

/* Method dispatch — looks up vtable */
MethodDesc *obj_lookup_method(McblObject *obj, const char *method_name);

/* Call a method through vtable */
typedef struct MdkVM MdkVM; /* forward decl from mdk.h */
int         obj_call_method(void *vm, McblObject *obj,
                             const char *method_name,
                             void **args, int argc);

/* -------------------------------------------------------------------
   Memory management — pure reference counting, NO garbage collector.
   Objects freed immediately when refs == 0. Zero GC pauses.
   ------------------------------------------------------------------- */
void  oop_mem_init    (void);
void  oop_mem_shutdown(void);
size_t oop_mem_alive  (void);   /* count of live objects */

/* -------------------------------------------------------------------
   classInt / subclass support
   ------------------------------------------------------------------- */

/* classInt creates an inner named class inside an inc block */
McblClass *classint_new    (const char *name);
void       classint_add_field (McblClass *cls, const char *fname,
                                const char *type_name, int access);
void       classint_add_method(McblClass *cls, MethodDesc *m);
McblObject *classint_activate (McblClass *cls); /* instantiate and run */

/* Cross-class variable access (classInt can access other classInt vars) */
int classint_share_field(McblClass *from, McblClass *to,
                          const char *field_name);

/* -------------------------------------------------------------------
   Interface system
   ------------------------------------------------------------------- */
McblInterface *iface_new    (const char *name);
void           iface_add_method(McblInterface *iface, const char *method_name,
                                 const char *ret_type);
int            iface_validate(const McblClass *cls,
                               const McblInterface *iface); /* 1=ok 0=missing */

#ifdef __cplusplus
}
#endif

#endif /* MCBL_OOP_H */
