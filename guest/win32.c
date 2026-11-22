// Win32 API subset used by the FlexTalk engine DLLs.
#include "shim.h"

static void set_err(u32 e) { g_current->last_error = e; }

// ============================================================ process / env
SHIM(DWORD, GetVersion, (void)) { return 0x05650004; }  // Windows NT 4.0 build 1381
SHIM(char *, GetCommandLineA, (void)) { static char c[] = "voxtalk.exe"; return c; }
SHIM(char *, GetEnvironmentStrings, (void)) { static char e[] = "VOXTALK=1\0"; return e; }
SHIM(wchar *, GetEnvironmentStringsW, (void)) { static wchar e[] = {'V', '=', '1', 0, 0}; return e; }
SHIM(BOOL, FreeEnvironmentStringsA, (char *p)) { return 1; }
SHIM(BOOL, FreeEnvironmentStringsW, (wchar *p)) { return 1; }
SHIM(BOOL, SetEnvironmentVariableA, (const char *n, const char *v)) { return 1; }
SHIM(void, GetStartupInfoA, (u8 *si)) { memset(si, 0, 68); *(u32 *)si = 68; }
SHIM(HANDLE, GetStdHandle, (DWORD n)) { return n == (DWORD)-10 ? 0x10 : n == (DWORD)-11 ? 0x14 : 0x18; }
SHIM(BOOL, SetStdHandle, (DWORD n, HANDLE h)) { return 1; }
SHIM(u32, SetHandleCount, (u32 n)) { return n; }
SHIM(u32, GetACP, (void)) { return 1252; }
SHIM(u32, GetOEMCP, (void)) { return 437; }
SHIM(BOOL, GetCPInfo, (u32 cp, u8 *info)) { memset(info, 0, 20); *(u32 *)info = 1; info[4] = '?'; return 1; }
SHIM(int, GetLocaleInfoA, (u32 lcid, u32 type, char *buf, int n)) { return 0; }
SHIM(int, GetLocaleInfoW, (u32 lcid, u32 type, wchar *buf, int n)) { return 0; }
SHIM(DWORD, GetLastError, (void)) { return g_current->last_error; }
SHIM(void, SetLastError, (DWORD e)) { g_current->last_error = e; }
SHIM(BOOL, IsBadReadPtr, (const void *p, u32 n)) { return 0; }
SHIM(BOOL, IsBadWritePtr, (void *p, u32 n)) { return 0; }
SHIM(BOOL, IsBadCodePtr, (void *p)) { return 0; }
SHIM(void, OutputDebugStringA, (const char *s)) { kprintf("[dbg] %s", s); }
SHIM(void *, SetUnhandledExceptionFilter, (void *f)) { return 0; }
// ---- structured exception handling (enough for MSVC C++ try/catch) ----
typedef struct ExRec { u32 code, flags; struct ExRec *rec; void *addr; u32 n; u32 info[15]; } ExRec;
typedef struct ExReg { struct ExReg *next; u32(CDECL *handler)(ExRec *, struct ExReg *, void *, void *); } ExReg;
static u8 g_ctx[0x2CC];

SHIM(void, RaiseException, (DWORD code, DWORD flags, DWORD n, const DWORD *args)) {
    ExRec er;
    memset(&er, 0, sizeof er);
    er.code = code;
    kprintf("RaiseException %x from %p\n", code, __builtin_return_address(0));
    er.flags = flags & 1;
    er.addr = __builtin_return_address(0);
    er.n = n > 15 ? 15 : n;
    for (u32 i = 0; i < er.n; i++) er.info[i] = args[i];
    ExReg *r = (ExReg *)((u32 *)g_current->teb)[0];
    while ((u32)r != 0xFFFFFFFF && r) {
        void *disp = 0;
        u32 rc = r->handler(&er, r, g_ctx, &disp);
        if (rc == 0) return;  // ExceptionContinueExecution
        r = r->next;
    }
    kprintf("unhandled exception %x\n", code);
    panic("unhandled exception");
}
SHIM(void, RtlUnwind, (void *target, void *ip, ExRec *rec, void *ret)) {
    ExRec local;
    if (rec) local = *rec;
    else { memset(&local, 0, sizeof local); local.code = 0xC0000027; }
    local.flags |= 2;  // EXCEPTION_UNWINDING
    if (!target) local.flags |= 4;  // EXCEPTION_EXIT_UNWIND
    u32 *teb = (u32 *)g_current->teb;
    ExReg *r = (ExReg *)teb[0];
    while ((u32)r != 0xFFFFFFFF && r && r != (ExReg *)target) {
        void *disp = 0;
        r->handler(&local, r, g_ctx, &disp);
        r = r->next;
        teb[0] = (u32)r;
    }
}
SHIM(void, ExitProcess, (u32 code)) {
    kprintf("ExitProcess %u from %p\n", code, __builtin_return_address(0));
    u32 *bp = __builtin_frame_address(0);
    for (int i = 0; i < 12 && bp; i++) { kprintf("  frame ret %p\n", bp[1]); bp = (u32 *)bp[0]; }
    panic("exit");
}
SHIM(BOOL, TerminateProcess, (HANDLE h, u32 code)) { kprintf("TerminateProcess %u\n", code); panic("terminate"); return 0; }
SHIM(HANDLE, GetCurrentProcess, (void)) { return 0xFFFFFFFF; }
SHIM(DWORD, GetCurrentProcessId, (void)) { return 0x10; }
SHIM(DWORD, GetCurrentThreadId, (void)) { return g_current->id; }

// ============================================================ strings / locale
SHIM(int, MultiByteToWideChar, (u32 cp, DWORD fl, const char *s, int n, wchar *d, int dn)) {
    if (n < 0) n = strlen(s) + 1;
    if (!dn) return n;
    int m = n < dn ? n : dn;
    for (int i = 0; i < m; i++) d[i] = (u8)s[i];
    return m;
}
SHIM(int, WideCharToMultiByte, (u32 cp, DWORD fl, const wchar *s, int n, char *d, int dn, const char *def, BOOL *used)) {
    if (n < 0) { n = 0; while (s[n]) n++; n++; }
    if (used) *used = 0;
    if (!dn) return n;
    int m = n < dn ? n : dn;
    for (int i = 0; i < m; i++) d[i] = s[i] < 256 ? (char)s[i] : '?';
    return m;
}
SHIM(char *, lstrcpyA, (char *d, const char *s)) { return strcpy(d, s); }
SHIM(char *, lstrcatA, (char *d, const char *s)) { strcpy(d + strlen(d), s); return d; }
SHIM(int, lstrlenA, (const char *s)) { return s ? strlen(s) : 0; }

static int cmp_chars(u32 a, u32 b, int ic) {
    if (ic) { if (a < 128) a = ktolower(a); if (b < 128) b = ktolower(b); }
    return (int)a - (int)b;
}
SHIM(int, CompareStringA, (u32 lcid, DWORD fl, const char *a, int na, const char *b, int nb)) {
    if (na < 0) na = strlen(a);
    if (nb < 0) nb = strlen(b);
    int n = na < nb ? na : nb;
    for (int i = 0; i < n; i++) {
        int d = cmp_chars((u8)a[i], (u8)b[i], fl & 1);
        if (d) return d < 0 ? 1 : 3;
    }
    return na < nb ? 1 : na > nb ? 3 : 2;
}
SHIM(int, CompareStringW, (u32 lcid, DWORD fl, const wchar *a, int na, const wchar *b, int nb)) {
    if (na < 0) { na = 0; while (a[na]) na++; }
    if (nb < 0) { nb = 0; while (b[nb]) nb++; }
    int n = na < nb ? na : nb;
    for (int i = 0; i < n; i++) {
        int d = cmp_chars(a[i], b[i], fl & 1);
        if (d) return d < 0 ? 1 : 3;
    }
    return na < nb ? 1 : na > nb ? 3 : 2;
}
SHIM(int, LCMapStringA, (u32 lcid, DWORD fl, const char *s, int n, char *d, int dn)) {
    if (n < 0) n = strlen(s) + 1;
    if (!dn) return n;
    for (int i = 0; i < n && i < dn; i++)
        d[i] = (fl & 0x100) ? ktolower(s[i]) : (fl & 0x200) ? ktoupper(s[i]) : s[i];
    return n < dn ? n : dn;
}
SHIM(int, LCMapStringW, (u32 lcid, DWORD fl, const wchar *s, int n, wchar *d, int dn)) {
    if (n < 0) { n = 0; while (s[n]) n++; n++; }
    if (!dn) return n;
    for (int i = 0; i < n && i < dn; i++) {
        wchar c = s[i];
        if (c < 128) c = (fl & 0x100) ? ktolower(c) : (fl & 0x200) ? ktoupper(c) : c;
        d[i] = c;
    }
    return n < dn ? n : dn;
}
static u16 ctype1(u32 c) {
    u16 t = 0;
    if (c >= 'A' && c <= 'Z') t |= 0x001 | 0x100;  // C1_UPPER | C1_ALPHA
    if (c >= 'a' && c <= 'z') t |= 0x002 | 0x100;
    if (c >= '0' && c <= '9') t |= 0x004;
    if (c == ' ' || (c >= 9 && c <= 13)) t |= 0x008;
    if (c == ' ' || c == '\t') t |= 0x040;  // blank
    if (c < 32 || c == 127) t |= 0x020;
    if ((c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~')) t |= 0x010;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) t |= 0x080;
    return t;
}
SHIM(BOOL, GetStringTypeA, (u32 lcid, DWORD type, const char *s, int n, u16 *out)) {
    if (n < 0) n = strlen(s) + 1;
    for (int i = 0; i < n; i++) out[i] = ctype1((u8)s[i]);
    return 1;
}
SHIM(BOOL, GetStringTypeW, (DWORD type, const wchar *s, int n, u16 *out)) {
    if (n < 0) { n = 0; while (s[n]) n++; n++; }
    for (int i = 0; i < n; i++) out[i] = s[i] < 128 ? ctype1(s[i]) : 0x100;
    return 1;
}

// ============================================================ memory
SHIM(HANDLE, HeapCreate, (DWORD o, u32 init, u32 max)) { return 0x00440000; }
SHIM(BOOL, HeapDestroy, (HANDLE h)) { return 1; }
SHIM(void *, HeapAlloc, (HANDLE h, DWORD fl, u32 n)) { return (fl & 8) ? kcalloc(n) : kmalloc(n); }
SHIM(BOOL, HeapFree, (HANDLE h, DWORD fl, void *p)) { kfree(p); return 1; }
SHIM(void *, HeapReAlloc, (HANDLE h, DWORD fl, void *p, u32 n)) {
    u32 old = ksize(p);
    void *q = krealloc(p, n);
    if (q && (fl & 8) && n > old) memset((u8 *)q + old, 0, n - old);
    return q;
}
SHIM(u32, HeapSize, (HANDLE h, DWORD fl, void *p)) { return ksize(p); }
SHIM(HANDLE, GlobalAlloc, (u32 fl, u32 n)) { return (HANDLE)kcalloc(n); }
SHIM(HANDLE, GlobalFree, (HANDLE h)) { kfree((void *)h); return 0; }

// VirtualAlloc: reserve = allocate zeroed, 64K-aligned; commit inside a region = no-op
typedef struct VReg { u32 base, size; void *raw; struct VReg *next; } VReg;
static VReg *g_vregs;
SHIM(void *, VirtualAlloc, (void *addr, u32 size, DWORD type, DWORD prot)) {
    if (addr) {
        for (VReg *r = g_vregs; r; r = r->next)
            if ((u32)addr >= r->base && (u32)addr + size <= r->base + r->size) return addr;
        kprintf("VirtualAlloc at %p outside any region\n", addr);
        return 0;
    }
    size = (size + 0xFFF) & ~0xFFFu;
    void *raw;
    u8 *p = kmalloc_aligned(size, 0x10000, &raw);
    if (!p) return 0;
    memset(p, 0, size);
    VReg *r = kcalloc(sizeof(VReg));
    r->base = (u32)p; r->size = size; r->raw = raw; r->next = g_vregs; g_vregs = r;
    return p;
}
SHIM(BOOL, VirtualFree, (void *addr, u32 size, DWORD type)) {
    if (type & 0x8000) {  // MEM_RELEASE
        for (VReg **pr = &g_vregs; *pr; pr = &(*pr)->next)
            if ((*pr)->base == (u32)addr) { VReg *r = *pr; *pr = r->next; kfree(r->raw); kfree(r); return 1; }
    }
    return 1;
}

// ============================================================ TLS / sync
static u32 g_tls_used[2];
SHIM(DWORD, TlsAlloc, (void)) {
    for (int i = 0; i < 64; i++)
        if (!(g_tls_used[i / 32] & (1u << (i % 32)))) { g_tls_used[i / 32] |= 1u << (i % 32); return i; }
    return 0xFFFFFFFF;
}
SHIM(BOOL, TlsFree, (DWORD i)) { if (i < 64) g_tls_used[i / 32] &= ~(1u << (i % 32)); return 1; }
SHIM(void *, TlsGetValue, (DWORD i)) { set_err(0); return i < 64 ? (void *)g_current->tls[i] : 0; }
SHIM(BOOL, TlsSetValue, (DWORD i, void *v)) { if (i >= 64) return 0; g_current->tls[i] = (u32)v; return 1; }

typedef struct { void *dbg; LONG lock; LONG rec; u32 owner; u32 sem; u32 spin; } CS;
SHIM(void, InitializeCriticalSection, (CS *c)) { memset(c, 0, sizeof *c); c->lock = -1; }
SHIM(void, DeleteCriticalSection, (CS *c)) {}
SHIM(void, EnterCriticalSection, (CS *c)) {
    while (c->owner && c->owner != g_current->id) yield();
    c->owner = g_current->id;
    c->rec++;
}
SHIM(void, LeaveCriticalSection, (CS *c)) { if (--c->rec <= 0) { c->rec = 0; c->owner = 0; } }
SHIM(LONG, InterlockedIncrement, (LONG *p)) { return ++*p; }
SHIM(LONG, InterlockedDecrement, (LONG *p)) { return --*p; }
SHIM(LONG, InterlockedExchange, (LONG *p, LONG v)) { LONG o = *p; *p = v; return o; }

typedef struct { u32 kind; int manual, state; } Event;
typedef struct { u32 kind; LONG count, max; int refs; } Sem;
typedef struct { u32 kind; Thread *t; } ThreadObj;

// ---- named kernel objects (the engine shares loaded data via named sections) ----
typedef struct Named { char name[200]; Obj *obj; struct Named *next; } Named;
static Named *g_named;
static Obj *named_find(const char *name, u32 kind) {
    if (!name) return 0;
    for (Named *n = g_named; n; n = n->next)
        if (n->obj->kind == kind && !stricmp(n->name, name)) return n->obj;
    return 0;
}
static void named_add(const char *name, Obj *o) {
    if (!name) return;
    Named *n = kcalloc(sizeof(Named));
    strcpy(n->name, name);
    n->obj = o;
    n->next = g_named;
    g_named = n;
}
static void named_remove(Obj *o) {
    for (Named **p = &g_named; *p; p = &(*p)->next)
        if ((*p)->obj == o) { Named *n = *p; *p = n->next; kfree(n); return; }
}

SHIM(HANDLE, CreateEventA, (void *sa, BOOL manual, BOOL initial, const char *name)) {
    Event *e = kcalloc(sizeof(Event));
    e->kind = K_EVENT; e->manual = manual; e->state = initial;
    return (HANDLE)e;
}
SHIM(BOOL, SetEvent, (HANDLE h)) { ((Event *)h)->state = 1; return 1; }
SHIM(BOOL, ResetEvent, (HANDLE h)) { ((Event *)h)->state = 0; return 1; }
SHIM(HANDLE, CreateSemaphoreA, (void *sa, LONG init, LONG max, const char *name)) {
    Sem *s = (Sem *)named_find(name, K_SEM);
    if (s) { s->refs++; set_err(183); return (HANDLE)s; }  // ERROR_ALREADY_EXISTS
    s = kcalloc(sizeof(Sem));
    s->kind = K_SEM; s->count = init; s->max = max; s->refs = 1;
    named_add(name, (Obj *)s);
    set_err(0);
    return (HANDLE)s;
}
SHIM(HANDLE, OpenSemaphoreA, (DWORD acc, BOOL inh, const char *name)) {
    Sem *s = (Sem *)named_find(name, K_SEM);
    if (!s) { set_err(2); return 0; }
    s->refs++;
    return (HANDLE)s;
}
SHIM(BOOL, ReleaseSemaphore, (HANDLE h, LONG n, LONG *prev)) {
    Sem *s = (Sem *)h;
    if (prev) *prev = s->count;
    s->count += n;
    if (s->count > s->max) s->count = s->max;
    return 1;
}

static int is_signaled(HANDLE h, int consume) {
    Obj *o = (Obj *)h;
    if (!h || h == INVALID_HANDLE_VALUE) return 1;
    switch (o->kind) {
    case K_EVENT: { Event *e = (Event *)o; if (!e->state) return 0; if (consume && !e->manual) e->state = 0; return 1; }
    case K_SEM: { Sem *s = (Sem *)o; if (s->count <= 0) return 0; if (consume) s->count--; return 1; }
    case K_THREAD: return ((ThreadObj *)o)->t->exited;
    default: return 1;
    }
}
SHIM(DWORD, WaitForSingleObject, (HANDLE h, DWORD ms)) {
    u32 t0 = ticks();
    for (;;) {
        if (is_signaled(h, 1)) return 0;
        if (ms != 0xFFFFFFFF && ticks() - t0 >= ms) return 0x102;
        yield();
    }
}
SHIM(DWORD, WaitForMultipleObjects, (DWORD n, const HANDLE *hs, BOOL all, DWORD ms)) {
    u32 t0 = ticks();
    for (;;) {
        if (all) {
            DWORD i;
            for (i = 0; i < n; i++) if (!is_signaled(hs[i], 0)) break;
            if (i == n) { for (i = 0; i < n; i++) is_signaled(hs[i], 1); return 0; }
        } else {
            for (DWORD i = 0; i < n; i++) if (is_signaled(hs[i], 1)) return i;
        }
        if (ms != 0xFFFFFFFF && ticks() - t0 >= ms) return 0x102;
        yield();
    }
}
SHIM(void, Sleep, (DWORD ms)) { u32 t0 = ticks(); do yield(); while (ticks() - t0 < ms); }

SHIM(HANDLE, CreateThread, (void *sa, u32 stack, u32 (STDCALL *fn)(void *), void *param, DWORD flags, DWORD *tid)) {
    ThreadObj *o = kcalloc(sizeof(ThreadObj));
    o->kind = K_THREAD;
    o->t = thread_create(fn, param, (flags & 4) != 0);
    if (tid) *tid = o->t->id;
    return (HANDLE)o;
}
SHIM(DWORD, ResumeThread, (HANDLE h)) {
    Thread *t = ((ThreadObj *)h)->t;
    DWORD prev = t->suspended;
    t->suspended = 0;
    return prev;
}

// ============================================================ files
typedef struct { u32 kind; RdFile *f; u32 pos; } FileObj;
typedef struct { u32 kind; RdFile *f; u8 *mem; u32 size; int refs, views; } MapObj;
typedef struct View { u8 *addr; MapObj *m; struct View *next; } View;
static View *g_views;
typedef struct { u32 kind; } FindObj;

SHIM(HANDLE, CreateFileA, (const char *name, DWORD acc, DWORD share, void *sa, DWORD disp, DWORD attr, HANDLE tmpl)) {
    RdFile *f = rd_find(name);
    int write = (acc & 0x40000000) != 0;
    if (disp == 2 || (disp == 4 && !f) || (disp == 1 && !f)) {  // CREATE_ALWAYS / OPEN_ALWAYS / CREATE_NEW
        f = rd_create(name);
    } else if (disp == 5 && f) {  // TRUNCATE_EXISTING
        f->size = 0;
    }
    if (!f) { kprintf("CreateFileA: not found %s\n", name); set_err(2); return INVALID_HANDLE_VALUE; }
    (void)write;
    FileObj *o = kcalloc(sizeof(FileObj));
    o->kind = K_FILE; o->f = f; o->pos = 0;
    set_err(0);
    return (HANDLE)o;
}
SHIM(BOOL, ReadFile, (HANDLE h, void *buf, DWORD n, DWORD *got, void *ov)) {
    FileObj *o = (FileObj *)h;
    if (h < 0x100) { if (got) *got = 0; return 1; }
    u32 avail = o->pos < o->f->size ? o->f->size - o->pos : 0;
    if (n > avail) n = avail;
    memcpy(buf, o->f->data + o->pos, n);
    o->pos += n;
    if (got) *got = n;
    return 1;
}
SHIM(BOOL, WriteFile, (HANDLE h, const void *buf, DWORD n, DWORD *wr, void *ov)) {
    if (h < 0x100) {  // std handles -> serial log
        const char *s = buf;
        for (DWORD i = 0; i < n; i++) serial_putc(s[i]);
        if (wr) *wr = n;
        return 1;
    }
    FileObj *o = (FileObj *)h;
    RdFile *f = o->f;
    if (o->pos + n > f->cap) {
        u32 nc = (o->pos + n) * 2;
        u8 *d = kcalloc(nc);
        memcpy(d, f->data, f->size);
        f->data = d;
        f->cap = nc;
    }
    memcpy(f->data + o->pos, buf, n);
    o->pos += n;
    if (o->pos > f->size) f->size = o->pos;
    if (wr) *wr = n;
    return 1;
}
SHIM(DWORD, SetFilePointer, (HANDLE h, LONG lo, LONG *hi, DWORD method)) {
    FileObj *o = (FileObj *)h;
    if (h < 0x100) return 0;
    LONG base = method == 0 ? 0 : method == 1 ? (LONG)o->pos : (LONG)o->f->size;
    LONG np = base + lo;
    if (np < 0) { set_err(131); return 0xFFFFFFFF; }
    o->pos = np;
    if (hi) *hi = 0;
    return o->pos;
}
SHIM(BOOL, SetEndOfFile, (HANDLE h)) { FileObj *o = (FileObj *)h; o->f->size = o->pos; return 1; }
SHIM(BOOL, FlushFileBuffers, (HANDLE h)) { return 1; }
SHIM(DWORD, GetFileType, (HANDLE h)) { return h < 0x100 ? 2 : 1; }
static void map_maybe_destroy(MapObj *m) {
    if (m->refs > 0 || m->views > 0) return;
    named_remove((Obj *)m);
    if (m->mem) kfree(m->mem);
    m->kind = 0;
    kfree(m);
}
SHIM(HANDLE, CreateFileMappingA, (HANDLE hf, void *sa, DWORD prot, DWORD hi, DWORD lo, const char *name)) {
    MapObj *m = (MapObj *)named_find(name, K_MAP);
    if (m) { m->refs++; set_err(183); return (HANDLE)m; }  // existing section is shared
    m = kcalloc(sizeof(MapObj));
    m->kind = K_MAP;
    m->refs = 1;
    if (hf == INVALID_HANDLE_VALUE || !hf) { m->size = lo; m->mem = kcalloc(lo ? lo : 1); }
    else { m->f = ((FileObj *)hf)->f; m->size = m->f->size; }
    named_add(name, (Obj *)m);
    set_err(0);
    return (HANDLE)m;
}
SHIM(HANDLE, OpenFileMappingA, (DWORD acc, BOOL inh, const char *name)) {
    MapObj *m = (MapObj *)named_find(name, K_MAP);
    if (!m) { set_err(2); return 0; }
    m->refs++;
    return (HANDLE)m;
}
SHIM(void *, MapViewOfFile, (HANDLE h, DWORD acc, DWORD hi, DWORD lo, u32 n)) {
    MapObj *m = (MapObj *)h;
    u8 *p = (m->f ? m->f->data : m->mem) + lo;
    View *v = kcalloc(sizeof(View));
    v->addr = p; v->m = m; v->next = g_views; g_views = v;
    m->views++;
    return p;
}
SHIM(BOOL, UnmapViewOfFile, (void *p)) {
    for (View **pv = &g_views; *pv; pv = &(*pv)->next) {
        if ((*pv)->addr == p) {
            View *v = *pv;
            *pv = v->next;
            v->m->views--;
            map_maybe_destroy(v->m);
            kfree(v);
            return 1;
        }
    }
    return 0;
}
SHIM(BOOL, CloseHandle, (HANDLE h)) {
    if (h < 0x100 || h == INVALID_HANDLE_VALUE) return 1;
    Obj *o = (Obj *)h;
    if (o->kind == K_MAP) { MapObj *m = (MapObj *)o; m->refs--; map_maybe_destroy(m); }
    else if (o->kind == K_SEM) { Sem *s = (Sem *)o; if (--s->refs <= 0) named_remove(o); }
    return 1;
}

static int wild(const char *p, const char *s) {
    if (!*p) return !*s;
    if (*p == '*') return wild(p + 1, s) || (*s && wild(p, s + 1));
    if (*s && (*p == '?' || ktolower(*p) == ktolower(*s))) return wild(p + 1, s + 1);
    return 0;
}
SHIM(HANDLE, FindFirstFileA, (const char *pattern, u8 *fd)) {
    char pat[260];
    strcpy(pat, rd_relpath(pattern));
    for (RdFile *f = rd_first(); f; f = f->next) {
        if (wild(pat, f->path)) {
            memset(fd, 0, 320);
            *(u32 *)fd = 0x80;
            *(u32 *)(fd + 32) = f->size;
            const char *b = f->path;
            for (const char *p = f->path; *p; p++) if (*p == '\\') b = p + 1;
            strcpy((char *)fd + 44, b);
            FindObj *o = kcalloc(sizeof(FindObj));
            o->kind = K_FIND;
            return (HANDLE)o;
        }
    }
    set_err(2);
    return INVALID_HANDLE_VALUE;
}
SHIM(BOOL, FindClose, (HANDLE h)) { return 1; }
SHIM(BOOL, CreateDirectoryA, (const char *p, void *sa)) { return 1; }
SHIM(BOOL, CopyFileA, (const char *from, const char *to, BOOL failIfExists)) {
    RdFile *s = rd_find(from);
    if (!s) { set_err(2); return 0; }
    if (failIfExists && rd_find(to)) { set_err(80); return 0; }
    RdFile *d = rd_create(to);
    d->data = kmalloc(s->size + 1);
    d->cap = s->size + 1;
    memcpy(d->data, s->data, s->size);
    d->size = s->size;
    return 1;
}
SHIM(DWORD, GetFullPathNameA, (const char *p, DWORD n, char *buf, char **file)) {
    char tmp[260];
    if (p[0] && p[1] == ':') strcpy(tmp, p);
    else ksnprintf(tmp, sizeof tmp, "%s\\%s", ROOT_DIR, p);
    DWORD l = strlen(tmp);
    if (l + 1 > n) return l + 1;
    strcpy(buf, tmp);
    if (file) { *file = buf; for (char *c = buf; *c; c++) if (*c == '\\') *file = c + 1; }
    return l;
}
SHIM(DWORD, GetCurrentDirectoryA, (DWORD n, char *buf)) {
    DWORD l = strlen(ROOT_DIR);
    if (n < l + 1) return l + 1;
    strcpy(buf, ROOT_DIR);
    return l;
}
SHIM(u32, GetDriveTypeA, (const char *root)) { return 3; }

// ============================================================ modules
Module *module_for_system(const char *name);
static Module *g_exe;
SHIM(u32, GetModuleHandleA, (const char *name)) {
    if (!name) {
        if (!g_exe) g_exe = module_for_system("voxtalk.exe");
        return (u32)g_exe->base;
    }
    Module *m = module_by_name(name);
    if (!m) {
        static const char *sys[] = {"kernel32", "user32", "gdi32", "advapi32", "ole32", "mpr", "comctl32", "ntdll", 0};
        for (int i = 0; sys[i]; i++) if (!strnicmp(name, sys[i], strlen(sys[i]))) return (u32)module_for_system(name)->base;
    }
    if (!m) set_err(126);
    return m ? (u32)m->base : 0;
}
SHIM(DWORD, GetModuleFileNameA, (u32 h, char *buf, DWORD n)) {
    char tmp[260];
    Module *m = h ? module_by_base(h) : 0;
    if (!m || m->is_shim) ksnprintf(tmp, sizeof tmp, "%s\\voxtalk.exe", ROOT_DIR);
    else ksnprintf(tmp, sizeof tmp, "%s\\EngineDLLs\\%s", ROOT_DIR, m->name);
    DWORD l = strlen(tmp);
    if (l >= n) l = n - 1;
    memcpy(buf, tmp, l);
    buf[l] = 0;
    return l;
}
SHIM(u32, LoadLibraryA, (const char *name)) {
    Module *m = pe_load(name);
    if (!m) { set_err(126); return 0; }
    return (u32)m->base;
}
SHIM(BOOL, FreeLibrary, (u32 h)) { return 1; }
SHIM(void *, GetProcAddress, (u32 h, const char *name)) {
    Module *m = module_by_base(h);
    void *p = m ? pe_export(m, name) : 0;
    if (!p) { kprintf("GetProcAddress: %s not found\n", (u32)name < 0x10000 ? "(ordinal)" : name); set_err(127); }
    return p;
}

// ============================================================ time
SHIM(DWORD, GetTickCount, (void)) { return ticks(); }
static void fixed_time(u16 *st) {  // 1998-11-19 12:00:00 (Half-Life's release week)
    st[0] = 1998; st[1] = 11; st[2] = 4; st[3] = 19; st[4] = 12; st[5] = 0; st[6] = 0; st[7] = 0;
}
SHIM(void, GetLocalTime, (u16 *st)) { fixed_time(st); }
SHIM(void, GetSystemTime, (u16 *st)) { fixed_time(st); }
SHIM(DWORD, GetTimeZoneInformation, (u8 *tz)) { memset(tz, 0, 172); return 0; }
SHIM(BOOL, FileTimeToLocalFileTime, (const u32 *a, u32 *b)) { b[0] = a[0]; b[1] = a[1]; return 1; }
SHIM(BOOL, FileTimeToSystemTime, (const u32 *ft, u16 *st)) { fixed_time(st); return 1; }
SHIM(BOOL, SystemTimeToFileTime, (const u16 *st, u32 *ft)) { ft[0] = 0; ft[1] = 0x01BE0000; return 1; }

// ============================================================ user32 (windows & messages)
typedef u32(STDCALL *WndProc)(u32, u32, u32, u32);
typedef struct Cls { char name[64]; WndProc proc; int extra; struct Cls *next; } Cls;
typedef struct { u32 kind; WndProc proc; u8 extra[64]; u32 userdata; int alive; u32 id; } Wnd;
static Cls *g_classes;
typedef struct { u32 hwnd, msg, wp, lp; } Msg;
static Msg g_queue[1024];
static int g_qhead, g_qtail;

SHIM(u16, RegisterClassA, (const u32 *wc)) {
    Cls *c = kcalloc(sizeof(Cls));
    const char *name = (const char *)wc[9];
    if ((u32)name < 0x10000) ksnprintf(c->name, 64, "#%u", (u32)name);
    else strcpy(c->name, name);
    c->proc = (WndProc)wc[1];
    c->extra = wc[3];
    c->next = g_classes;
    g_classes = c;
    return 0xC000 + ((u32)c & 0xFFF);
}
SHIM(BOOL, UnregisterClassA, (const char *name, u32 inst)) { return 1; }
SHIM(u32, CreateWindowExA, (DWORD ex, const char *cls, const char *title, DWORD style, int x, int y, int w, int h,
                            u32 parent, u32 menu, u32 inst, void *param)) {
    Cls *c = g_classes;
    for (; c; c = c->next) if ((u32)cls >= 0x10000 && !stricmp(c->name, cls)) break;
    if (!c) { kprintf("CreateWindowExA: unknown class\n"); return 0; }
    Wnd *wnd = kcalloc(sizeof(Wnd));
    wnd->kind = K_WND; wnd->proc = c->proc; wnd->alive = 1; wnd->id = menu;
    u32 cs[12] = {(u32)param, inst, menu, parent, (u32)h, (u32)w, (u32)y, (u32)x, style, (u32)title, (u32)cls, ex};
    if (!wnd->proc((u32)wnd, 0x81, 0, (u32)cs)) return 0;  // WM_NCCREATE
    if ((i32)wnd->proc((u32)wnd, 0x01, 0, (u32)cs) == -1) return 0;  // WM_CREATE
    return (u32)wnd;
}
SHIM(BOOL, DestroyWindow, (u32 h)) {
    Wnd *w = (Wnd *)h;
    if (!w || !w->alive) return 0;
    w->proc(h, 0x02, 0, 0);  // WM_DESTROY
    w->proc(h, 0x82, 0, 0);  // WM_NCDESTROY
    w->alive = 0;
    return 1;
}
SHIM(u32, DefWindowProcA, (u32 h, u32 msg, u32 wp, u32 lp)) { return msg == 0x81 ? 1 : 0; }
SHIM(BOOL, IsWindow, (u32 h)) { return h && ((Wnd *)h)->kind == K_WND && ((Wnd *)h)->alive; }
SHIM(LONG, GetWindowLongA, (u32 h, int idx)) {
    Wnd *w = (Wnd *)h;
    if (idx == -4) return (LONG)w->proc;
    if (idx == -21) return w->userdata;
    if (idx == -12) return w->id;
    if (idx >= 0 && idx + 4 <= 64) return *(LONG *)(w->extra + idx);
    return 0;
}
SHIM(LONG, SetWindowLongA, (u32 h, int idx, LONG v)) {
    Wnd *w = (Wnd *)h;
    LONG old = shim_GetWindowLongA(h, idx);
    if (idx == -4) w->proc = (WndProc)v;
    else if (idx == -21) w->userdata = v;
    else if (idx >= 0 && idx + 4 <= 64) *(LONG *)(w->extra + idx) = v;
    return old;
}
SHIM(BOOL, PostMessageA, (u32 h, u32 msg, u32 wp, u32 lp)) {
    int nt = (g_qtail + 1) % 1024;
    if (nt == g_qhead) return 0;
    g_queue[g_qtail] = (Msg){h, msg, wp, lp};
    g_qtail = nt;
    return 1;
}
SHIM(u32, SendMessageA, (u32 h, u32 msg, u32 wp, u32 lp)) {
    if (!shim_IsWindow(h)) return 0;
    return ((Wnd *)h)->proc(h, msg, wp, lp);
}
SHIM(BOOL, SetMessageQueue, (int n)) { return 1; }
void pump_messages(void) {
    while (g_qhead != g_qtail) {
        Msg m = g_queue[g_qhead];
        g_qhead = (g_qhead + 1) % 1024;
        if (shim_IsWindow(m.hwnd)) ((Wnd *)m.hwnd)->proc(m.hwnd, m.msg, m.wp, m.lp);
    }
}
SHIM(char *, CharLowerA, (char *s)) {
    if ((u32)s < 0x10000) return (char *)(u32)ktolower((char)(u32)s);
    for (char *p = s; *p; p++) *p = ktolower(*p);
    return s;
}
// ---- PE resources (string tables are used for the dictionary file names) ----
static int res_name_eq(u8 *rsrc, u32 entry_name, const char *name) {
    if ((u32)name < 0x10000) return !(entry_name & 0x80000000u) && entry_name == (u32)name;
    if (name[0] == '#') {  // "#123" means numeric id
        u32 v = 0;
        for (const char *p = name + 1; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
        return !(entry_name & 0x80000000u) && entry_name == v;
    }
    if (!(entry_name & 0x80000000u)) return 0;
    u16 *s = (u16 *)(rsrc + (entry_name & 0x7FFFFFFF));
    u32 len = s[0];
    if (strlen(name) != len) return 0;
    for (u32 i = 0; i < len; i++) if (ktolower((char)s[1 + i]) != ktolower(name[i])) return 0;
    return 1;
}
static u8 *res_dir_find(u8 *rsrc, u8 *dir, const char *name) {
    u16 nn = *(u16 *)(dir + 12), ni = *(u16 *)(dir + 14);
    u32 *e = (u32 *)(dir + 16);
    for (u32 i = 0; i < (u32)nn + ni; i++, e += 2) {
        if (name == (const char *)-1 || res_name_eq(rsrc, e[0], name)) return rsrc + (e[1] & 0x7FFFFFFF);
    }
    return 0;
}
// returns the IMAGE_RESOURCE_DATA_ENTRY (RVA, size, ...) or 0
static u32 *res_find(Module *m, const char *type, const char *name) {
    if (!m || m->is_shim) return 0;
    u8 *b = m->base;
    u32 nt = *(u32 *)(b + 0x3C);
    u32 rva = *(u32 *)(b + nt + 24 + 96 + 2 * 8);
    if (!rva) return 0;
    u8 *rsrc = b + rva;
    u8 *d1 = res_dir_find(rsrc, rsrc, type);
    if (!d1) return 0;
    u8 *d2 = res_dir_find(rsrc, d1, name);
    if (!d2) return 0;
    u8 *d3 = res_dir_find(rsrc, d2, (const char *)-1);  // first language
    return (u32 *)d3;
}
SHIM(int, LoadStringA, (u32 inst, u32 id, char *buf, int n)) {
    if (n > 0) buf[0] = 0;
    u32 *de = res_find(module_by_base(inst), (const char *)6, (const char *)((id >> 4) + 1));
    if (!de) return 0;
    u16 *p = (u16 *)(module_by_base(inst)->base + de[0]);
    for (u32 i = 0; i < (id & 15); i++) p += 1 + p[0];
    int len = p[0];
    if (len > n - 1) len = n - 1;
    for (int i = 0; i < len; i++) buf[i] = (char)p[1 + i];
    if (n > 0) buf[len] = 0;
    return len;
}
SHIM(u32, FindResourceA, (u32 mod, const char *name, const char *type)) {
    Module *m = module_by_base(mod);
    return (u32)res_find(m, type, name);
}
SHIM(u32, LoadResource, (u32 mod, u32 res)) {
    Module *m = module_by_base(mod);
    return (m && res) ? (u32)(m->base + ((u32 *)res)[0]) : 0;
}
SHIM(void *, LockResource, (u32 h)) { return (void *)h; }
SHIM(DWORD, SizeofResource, (u32 mod, u32 res)) { return res ? ((u32 *)res)[1] : 0; }
SHIM(BOOL, FreeResource, (u32 h)) { return 0; }
SHIM(int, MessageBoxA, (u32 h, const char *text, const char *cap, u32 type)) {
    kprintf("[MessageBox] %s: %s\n", cap ? cap : "", text ? text : "");
    return 1;
}
SHIM(int, wvsprintfA, (char *buf, const char *fmt, __builtin_va_list ap)) { return kvsnprintf(buf, 1024, fmt, ap); }
SHIM_CDECL(int, wsprintfA, (char *buf, const char *fmt, ...)) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = kvsnprintf(buf, 1024, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

// ============================================================ mpr / ole32
SHIM(DWORD, WNetGetUserA, (const char *res, char *user, DWORD *n)) {
    if (*n < 5) { *n = 5; return 234; }
    strcpy(user, "user");
    return 0;
}
SHIM(HRESULT, CoInitialize, (void *p)) { return 0; }
SHIM(void, CoUninitialize, (void)) {}
SHIM(DWORD, CoBuildVersion, (void)) { return 0x00170000; }
SHIM(void *, CoTaskMemAlloc, (u32 n)) { return kmalloc(n ? n : 1); }
SHIM(void, CoTaskMemFree, (void *p)) { kfree(p); }
SHIM(HRESULT, CoCreateInstance, (const void *clsid, void *outer, DWORD ctx, const void *iid, void **out)) {
    if (out) *out = 0;
    return 0x80040154;  // REGDB_E_CLASSNOTREG
}
SHIM(int, StringFromGUID2, (const u8 *g, wchar *out, int n)) {
    char tmp[40];
    const u32 d1 = *(const u32 *)g;
    const u16 d2 = *(const u16 *)(g + 4), d3 = *(const u16 *)(g + 6);
    ksnprintf(tmp, sizeof tmp, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", d1, d2, d3,
              g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    if (n < 39) return 0;
    for (int i = 0; i < 39; i++) out[i] = (u8)tmp[i];
    return 39;
}
static int hexv(wchar c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
SHIM(HRESULT, CLSIDFromString, (const wchar *s, u8 *g)) {
    // {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
    static const int pos[16] = {1, 3, 5, 7, 10, 12, 15, 17, 20, 22, 25, 27, 29, 31, 33, 35};
    u8 b[16];
    for (int i = 0; i < 16; i++) {
        int hi = hexv(s[pos[i]]), lo = hexv(s[pos[i] + 1]);
        if (hi < 0 || lo < 0) return 0x800401F3;
        b[i] = (u8)(hi * 16 + lo);
    }
    *(u32 *)g = (u32)b[0] << 24 | (u32)b[1] << 16 | (u32)b[2] << 8 | b[3];
    *(u16 *)(g + 4) = (u16)(b[4] << 8 | b[5]);
    *(u16 *)(g + 6) = (u16)(b[6] << 8 | b[7]);
    memcpy(g + 8, b + 8, 8);
    return 0;
}
