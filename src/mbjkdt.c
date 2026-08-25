#include "mbjkdt.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
   McBL# MBJKDT implementation
   ----------------------------------------------------------------------- */

MbjkdtBridge *mbjkdt_create(void) {
    MbjkdtBridge *b = (MbjkdtBridge *)mcbl_calloc(1, sizeof(MbjkdtBridge));
    b->last_sent     = NULL;
    b->last_received = NULL;
    b->connected     = 0;
    return b;
}

void mbjkdt_destroy(MbjkdtBridge *b) {
    if (!b) return;
    mcbl_free((void **)&b->last_sent);
    mcbl_free((void **)&b->last_received);
    mcbl_free((void **)&b);
}

char *mbjkdt_value_to_json(const MdkValue *v) {
    if (!v) return mcbl_strdup("null");
    char buf[512] = {0};
    switch (v->type) {
        case MDK_VAL_INT:
            snprintf(buf, sizeof(buf), "{\"type\":\"int\",\"value\":%lld}", (long long)v->ival);
            break;
        case MDK_VAL_FLOAT:
            snprintf(buf, sizeof(buf), "{\"type\":\"float\",\"value\":%g}", v->fval);
            break;
        case MDK_VAL_BOOL:
            snprintf(buf, sizeof(buf), "{\"type\":\"bool\",\"value\":%s}",
                     v->bval ? "true" : "false");
            break;
        case MDK_VAL_STRING:
            /* Basic JSON string escaping */
            snprintf(buf, sizeof(buf), "{\"type\":\"string\",\"value\":\"");
            size_t pos = strlen(buf);
            const char *s = v->sval ? v->sval : "";
            while (*s && pos < sizeof(buf) - 5) {
                if (*s == '"') { buf[pos++] = '\\'; buf[pos++] = '"'; }
                else if (*s == '\\') { buf[pos++] = '\\'; buf[pos++] = '\\'; }
                else if (*s == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n'; }
                else if (*s == '\t') { buf[pos++] = '\\'; buf[pos++] = 't'; }
                else buf[pos++] = *s;
                s++;
            }
            buf[pos++] = '"'; buf[pos++] = '}'; buf[pos] = '\0';
            break;
        case MDK_VAL_NULL:
        default:
            snprintf(buf, sizeof(buf), "null");
            break;
    }
    return mcbl_strdup(buf);
}

MdkValue mbjkdt_json_to_value(const char *json) {
    if (!json) return mdk_mknull();
    /* Simple JSON parser for {"type":"X","value":Y} */
    if (strstr(json, "\"type\":\"int\"")) {
        const char *p = strstr(json, "\"value\":");
        if (p) { long long v = atoll(p + 8); return mdk_mkint(v); }
    }
    if (strstr(json, "\"type\":\"float\"")) {
        const char *p = strstr(json, "\"value\":");
        if (p) { double v = atof(p + 8); return mdk_mkfloat(v); }
    }
    if (strstr(json, "\"type\":\"bool\"")) {
        return mdk_mkbool(strstr(json, "true") ? 1 : 0);
    }
    if (strstr(json, "\"type\":\"string\"")) {
        const char *p = strstr(json, "\"value\":\"");
        if (p) {
            p += 9;
            char buf[1024] = {0}; size_t pos = 0;
            while (*p && *p != '"' && pos < sizeof(buf) - 1) {
                if (*p == '\\' && *(p+1)) { p++; buf[pos++] = *p; }
                else buf[pos++] = *p;
                p++;
            }
            return mdk_mkstr(buf);
        }
    }
    if (strcmp(json, "null") == 0) return mdk_mknull();
    /* fallback: treat as raw string */
    return mdk_mkstr(json);
}

int mbjkdt_send_json(MbjkdtBridge *b, const char *json) {
    if (!b || !json) return -1;
    /* Write JSON to the M→JS pipe file */
    FILE *f = fopen(MBJKDT_PIPE_M2J, "w");
    if (!f) return -1;
    fputs(json, f);
    fputc('\n', f);
    fclose(f);

    mcbl_free((void **)&b->last_sent);
    b->last_sent = mcbl_strdup(json);
    b->connected = 1;
    return 0;
}

int mbjkdt_send_value(MbjkdtBridge *b, const MdkValue *v) {
    if (!b || !v) return -1;
    char *json = mbjkdt_value_to_json(v);
    int r = mbjkdt_send_json(b, json);
    mcbl_free((void **)&json);
    return r;
}

char *mbjkdt_recv_json(MbjkdtBridge *b) {
    if (!b) return NULL;
    /* Read JSON from the JS→M pipe file */
    FILE *f = fopen(MBJKDT_PIPE_J2M, "r");
    if (!f) return NULL;
    char buf[4096] = {0};
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[r] = '\0';

    /* Trim trailing newline */
    size_t l = strlen(buf);
    while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r')) buf[--l] = '\0';

    mcbl_free((void **)&b->last_received);
    b->last_received = mcbl_strdup(buf);
    return b->last_received;
}

char *mbjkdt_gen_js_adapter(void) {
    static const char JS_ADAPTER[] =
        "// McBL# MBJKDT JavaScript Adapter\n"
        "// Provides bidirectional JSON communication with McBL#\n"
        "\n"
        "const fs   = require('fs');\n"
        "const path = require('path');\n"
        "\n"
        "const M2J_PIPE = '/tmp/mcbl_m2j.json';\n"
        "const J2M_PIPE = '/tmp/mcbl_j2m.json';\n"
        "\n"
        "class McblBridge {\n"
        "    constructor() { this.connected = false; }\n"
        "\n"
        "    connect() { this.connected = true; }\n"
        "    disconnect() { this.connected = false; }\n"
        "\n"
        "    // Send JSON to McBL#\n"
        "    send(data) {\n"
        "        fs.writeFileSync(J2M_PIPE, JSON.stringify(data));\n"
        "    }\n"
        "\n"
        "    // Receive JSON from McBL#\n"
        "    receive() {\n"
        "        try {\n"
        "            const raw = fs.readFileSync(M2J_PIPE, 'utf8');\n"
        "            return JSON.parse(raw.trim());\n"
        "        } catch (e) { return null; }\n"
        "    }\n"
        "\n"
        "    // Watch for new messages from McBL#\n"
        "    watch(callback) {\n"
        "        fs.watchFile(M2J_PIPE, { interval: 100 }, () => {\n"
        "            const msg = this.receive();\n"
        "            if (msg) callback(msg);\n"
        "        });\n"
        "    }\n"
        "}\n"
        "\n"
        "module.exports = { McblBridge };\n";
    return mcbl_strdup(JS_ADAPTER);
}
