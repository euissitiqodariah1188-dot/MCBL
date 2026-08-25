#ifndef MCBL_MBJKDT_H
#define MCBL_MBJKDT_H

#include "mdk.h"
#include <stddef.h>

/* -----------------------------------------------------------------------
   McBL# MBJKDT  –  McBL Backend JavaScript Kit Developer Tools
   Provides bidirectional JSON communication between McBL# and JavaScript.

   McBL# → JavaScript:  mbjkdt_send_json(json_string)
   JavaScript → McBL#:  mbjkdt_recv_json()  returns JSON string

   Internally uses a file-based or pipe-based IPC channel.
   ----------------------------------------------------------------------- */

#define MBJKDT_IPC_PATH  "/tmp/mcbl_js_ipc"
#define MBJKDT_PIPE_M2J  "/tmp/mcbl_m2j.json"   /* McBL → JS  */
#define MBJKDT_PIPE_J2M  "/tmp/mcbl_j2m.json"   /* JS   → McBL */

typedef struct {
    char  *last_sent;     /* last JSON sent to JS   */
    char  *last_received; /* last JSON from JS      */
    int    connected;
} MbjkdtBridge;

MbjkdtBridge *mbjkdt_create(void);
void          mbjkdt_destroy(MbjkdtBridge *b);

/* Send a McBL# value to JavaScript as JSON */
int  mbjkdt_send_value(MbjkdtBridge *b, const MdkValue *v);

/* Send raw JSON string to JavaScript */
int  mbjkdt_send_json(MbjkdtBridge *b, const char *json);

/* Receive JSON from JavaScript (reads from IPC channel) */
char *mbjkdt_recv_json(MbjkdtBridge *b);

/* Convert a McBL# value to JSON */
char *mbjkdt_value_to_json(const MdkValue *v);

/* Parse JSON to McBL# value */
MdkValue mbjkdt_json_to_value(const char *json);

/* Generate a JavaScript adapter script that connects to McBL# */
char *mbjkdt_gen_js_adapter(void);

#endif /* MCBL_MBJKDT_H */
