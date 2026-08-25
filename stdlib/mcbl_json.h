#ifndef MCBL_JSON_H
#define MCBL_JSON_H
/*
 * McBL# json.* — JSON Parse and Emit
 * =====================================
 * json.parse(str)        → McblJsonNode*
 * json.stringify(node)   → char* (malloc'd)
 * json.get(node, key)    → McblJsonNode*
 * json.get_int(node,key) → long long
 * json.get_str(node,key) → const char*
 * json.get_flt(node,key) → double
 * json.get_arr(node,key) → McblJsonNode* (array)
 * json.arr_len(node)     → int
 * json.arr_get(node,i)   → McblJsonNode*
 * json.new_obj()         → empty object node
 * json.new_arr()         → empty array node
 * json.set(obj,key,val)  → set key in object
 * json.push(arr,val)     → push to array
 * json.free(node)        → free entire tree
 */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MCBL_JSON_NULL,
    MCBL_JSON_BOOL,
    MCBL_JSON_INT,
    MCBL_JSON_FLOAT,
    MCBL_JSON_STRING,
    MCBL_JSON_ARRAY,
    MCBL_JSON_OBJECT
} McblJsonType;

typedef struct McblJsonNode McblJsonNode;
struct McblJsonNode {
    McblJsonType type;
    union {
        int          bval;
        long long    ival;
        double       fval;
        char        *sval;
        struct {
            McblJsonNode **items;
            int           count;
        } arr;
        struct {
            char        **keys;
            McblJsonNode **vals;
            int           count;
        } obj;
    };
};

McblJsonNode *mcbl_json_parse  (const char *str);
char         *mcbl_json_str    (McblJsonNode *node);   /* stringify */
char         *mcbl_json_pretty (McblJsonNode *node, int indent);
void          mcbl_json_free   (McblJsonNode *node);

McblJsonNode *mcbl_json_get    (McblJsonNode *node, const char *key);
long long     mcbl_json_int    (McblJsonNode *node, const char *key);
double        mcbl_json_flt    (McblJsonNode *node, const char *key);
const char   *mcbl_json_cstr   (McblJsonNode *node, const char *key);
int           mcbl_json_bool   (McblJsonNode *node, const char *key);

McblJsonNode *mcbl_json_arr_get(McblJsonNode *arr, int idx);
int           mcbl_json_arr_len(McblJsonNode *arr);

McblJsonNode *mcbl_json_new_obj(void);
McblJsonNode *mcbl_json_new_arr(void);
McblJsonNode *mcbl_json_new_int(long long v);
McblJsonNode *mcbl_json_new_flt(double v);
McblJsonNode *mcbl_json_new_str(const char *s);
McblJsonNode *mcbl_json_new_bool(int v);
McblJsonNode *mcbl_json_new_null(void);

void mcbl_json_obj_set(McblJsonNode *obj, const char *key, McblJsonNode *val);
void mcbl_json_arr_push(McblJsonNode *arr, McblJsonNode *val);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_JSON_H */
