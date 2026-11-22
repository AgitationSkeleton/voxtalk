// In-memory registry for the engine's configuration.
#include "kernel.h"
#include "shim.h"

typedef struct Value { char name[64]; u32 type; u8 *data; u32 len; struct Value *next; } Value;
typedef struct Node { char name[64]; struct Node *child, *sibling; Value *values; } Node;

static Node g_roots[4];  // HKCR, HKCU, HKLM, HKU

static Node *root_of(u32 h) {
    if (h >= 0x80000000u && h <= 0x80000003u) return &g_roots[h - 0x80000000u];
    return (Node *)h;
}

static Node *child(Node *n, const char *name, int create) {
    for (Node *c = n->child; c; c = c->sibling) if (!stricmp(c->name, name)) return c;
    if (!create) return 0;
    Node *c = kcalloc(sizeof(Node));
    strcpy(c->name, name);
    Node **tail = &n->child;
    while (*tail) tail = &(*tail)->sibling;
    *tail = c;
    return c;
}

static Node *walk(Node *n, const char *path, int create) {
    char part[64];
    while (n && path && *path) {
        int i = 0;
        while (*path && *path != '\\' && i < 63) part[i++] = *path++;
        part[i] = 0;
        while (*path == '\\') path++;
        if (i) n = child(n, part, create);
    }
    return n;
}

static Value *value(Node *n, const char *name, int create) {
    const char *nm = name ? name : "";
    for (Value *v = n->values; v; v = v->next) if (!stricmp(v->name, nm)) return v;
    if (!create) return 0;
    Value *v = kcalloc(sizeof(Value));
    strcpy(v->name, nm);
    Value **tail = &n->values;  // keep creation order, like Windows' enumeration
    while (*tail) tail = &(*tail)->next;
    *tail = v;
    return v;
}

static void set_value(Node *n, const char *name, u32 type, const void *data, u32 len) {
    Value *v = value(n, name, 1);
    if (v->data) kfree(v->data);
    v->data = kmalloc(len ? len : 1);
    memcpy(v->data, data, len);
    v->len = len;
    v->type = type;
}

void reg_set_str(const char *path, const char *name, const char *val) {
    set_value(walk(&g_roots[2], path, 1), name, 1, val, strlen(val) + 1);
}
void reg_set_dword(const char *path, const char *name, u32 val) {
    set_value(walk(&g_roots[2], path, 1), name, 4, &val, 4);
}

#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_ITEMS 259

SHIM(LONG, RegOpenKeyExA, (u32 h, const char *sub, DWORD opt, DWORD sam, u32 *out)) {
    Node *n = walk(root_of(h), sub, 0);
    if (!n) return ERROR_FILE_NOT_FOUND;
    *out = (u32)n;
    return ERROR_SUCCESS;
}
SHIM(LONG, RegOpenKeyA, (u32 h, const char *sub, u32 *out)) { return shim_RegOpenKeyExA(h, sub, 0, 0, out); }
SHIM(LONG, RegCreateKeyExA, (u32 h, const char *sub, DWORD res, char *cls, DWORD opt, DWORD sam, void *sa, u32 *out, DWORD *disp)) {
    Node *n = walk(root_of(h), sub, 1);
    *out = (u32)n;
    if (disp) *disp = 2;
    return ERROR_SUCCESS;
}
SHIM(LONG, RegCreateKeyA, (u32 h, const char *sub, u32 *out)) { return shim_RegCreateKeyExA(h, sub, 0, 0, 0, 0, 0, out, 0); }
SHIM(LONG, RegCloseKey, (u32 h)) { return ERROR_SUCCESS; }
SHIM(LONG, RegFlushKey, (u32 h)) { return ERROR_SUCCESS; }

SHIM(LONG, RegQueryValueExA, (u32 h, const char *name, DWORD *res, DWORD *type, u8 *data, DWORD *cb)) {
    Value *v = value(root_of(h), name, 0);
    if (!v) return ERROR_FILE_NOT_FOUND;
    if (type) *type = v->type;
    if (!data) { if (cb) *cb = v->len; return ERROR_SUCCESS; }
    if (!cb || *cb < v->len) { if (cb) *cb = v->len; return ERROR_MORE_DATA; }
    memcpy(data, v->data, v->len);
    *cb = v->len;
    return ERROR_SUCCESS;
}
SHIM(LONG, RegSetValueExA, (u32 h, const char *name, DWORD res, DWORD type, const u8 *data, DWORD cb)) {
    set_value(root_of(h), name, type, data, cb);
    return ERROR_SUCCESS;
}
SHIM(LONG, RegDeleteValueA, (u32 h, const char *name)) {
    Node *n = root_of(h);
    for (Value **pv = &n->values; *pv; pv = &(*pv)->next)
        if (!stricmp((*pv)->name, name ? name : "")) { *pv = (*pv)->next; return ERROR_SUCCESS; }
    return ERROR_FILE_NOT_FOUND;
}
SHIM(LONG, RegDeleteKeyA, (u32 h, const char *sub)) {
    Node *n = root_of(h);
    for (Node **pc = &n->child; *pc; pc = &(*pc)->sibling)
        if (!stricmp((*pc)->name, sub)) { *pc = (*pc)->sibling; return ERROR_SUCCESS; }
    return ERROR_FILE_NOT_FOUND;
}
SHIM(LONG, RegEnumKeyA, (u32 h, DWORD idx, char *name, DWORD cch)) {
    Node *c = root_of(h)->child;
    for (DWORD i = 0; c && i < idx; i++) c = c->sibling;
    if (!c) return ERROR_NO_MORE_ITEMS;
    if (strlen(c->name) + 1 > cch) return ERROR_MORE_DATA;
    strcpy(name, c->name);
    return ERROR_SUCCESS;
}
SHIM(LONG, RegEnumValueA, (u32 h, DWORD idx, char *name, DWORD *cchName, DWORD *res, DWORD *type, u8 *data, DWORD *cb)) {
    Value *v = root_of(h)->values;
    for (DWORD i = 0; v && i < idx; i++) v = v->next;
    if (!v) return ERROR_NO_MORE_ITEMS;
    if (strlen(v->name) + 1 > *cchName) return ERROR_MORE_DATA;
    strcpy(name, v->name);
    *cchName = strlen(v->name);
    if (type) *type = v->type;
    if (data && cb) {
        if (*cb < v->len) { *cb = v->len; return ERROR_MORE_DATA; }
        memcpy(data, v->data, v->len);
    }
    if (cb) *cb = v->len;
    return ERROR_SUCCESS;
}
SHIM(LONG, RegQueryInfoKeyA, (u32 h, char *cls, DWORD *ccls, DWORD *res, DWORD *nsub, DWORD *maxsub, DWORD *maxcls,
                              DWORD *nval, DWORD *maxvname, DWORD *maxvlen, DWORD *sec, void *ft)) {
    Node *n = root_of(h);
    DWORD ns = 0, ms = 0, nv = 0, mvn = 0, mvl = 0;
    for (Node *c = n->child; c; c = c->sibling) { ns++; if (strlen(c->name) > ms) ms = strlen(c->name); }
    for (Value *v = n->values; v; v = v->next) { nv++; if (strlen(v->name) > mvn) mvn = strlen(v->name); if (v->len > mvl) mvl = v->len; }
    if (ccls) *ccls = 0;
    if (cls) *cls = 0;
    if (nsub) *nsub = ns;
    if (maxsub) *maxsub = ms;
    if (maxcls) *maxcls = 0;
    if (nval) *nval = nv;
    if (maxvname) *maxvname = mvn;
    if (maxvlen) *maxvlen = mvl;
    if (sec) *sec = 0;
    if (ft) memset(ft, 0, 8);
    return ERROR_SUCCESS;
}
