// Minimal PE (DLL) loader: map, relocate, bind imports to shims, run DllMain.
#include "kernel.h"

Module *g_modules;

static u32 rd32(const u8 *p) { return *(const u32 *)p; }
static u16 rd16(const u8 *p) { return *(const u16 *)p; }

static const char *basename_of(const char *p) {
    const char *b = p;
    for (; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    return b;
}

Module *module_by_name(const char *name) {
    const char *b = basename_of(name);
    for (Module *m = g_modules; m; m = m->next) {
        if (!stricmp(m->name, b)) return m;
        // allow name without ".dll"
        size_t l = strlen(b);
        if (strlen(m->name) == l + 4 && !strnicmp(m->name, b, l) && !stricmp(m->name + l, ".dll")) return m;
    }
    return 0;
}

Module *module_by_base(u32 base) {
    for (Module *m = g_modules; m; m = m->next) if ((u32)m->base == base) return m;
    return 0;
}

void *pe_export(Module *m, const char *name) {
    if (m->is_shim) return shim_lookup(m->name, name);
    u8 *b = m->base;
    u32 nt = rd32(b + 0x3C);
    u32 dir = rd32(b + nt + 24 + 96), dsize = rd32(b + nt + 24 + 100);
    if (!dir) return 0;
    u8 *ed = b + dir;
    u32 nnames = rd32(ed + 24), funcs = rd32(ed + 28), names = rd32(ed + 32), ords = rd32(ed + 36), base_ord = rd32(ed + 16);
    if ((u32)name < 0x10000) {  // by ordinal
        u32 idx = (u32)name - base_ord;
        return b + rd32(b + funcs + idx * 4);
    }
    for (u32 i = 0; i < nnames; i++) {
        if (!strcmp((char *)(b + rd32(b + names + i * 4)), name)) {
            u16 o = rd16(b + ords + i * 2);
            u32 rva = rd32(b + funcs + o * 4);
            if (rva >= dir && rva < dir + dsize) return 0;  // forwarder: not used here
            return b + rva;
        }
    }
    return 0;
}

static int strstr_i(const char *h, const char *n) {
    size_t ln = strlen(n);
    for (; *h; h++) if (!strnicmp(h, n, ln)) return 1;
    return 0;
}

static Module *shim_module(const char *name) {
    Module *m = module_by_name(name);
    if (m) return m;
    m = kcalloc(sizeof(Module));
    strcpy(m->name, basename_of(name));
    for (char *c = m->name; *c; c++) *c = ktolower(*c);
    if (!strstr_i(m->name, ".dll")) strcpy(m->name + strlen(m->name), ".dll");
    m->is_shim = 1;
    m->base = kcalloc(4096);  // fake image with MZ/PE signature
    m->base[0] = 'M'; m->base[1] = 'Z';
    *(u32 *)(m->base + 0x3C) = 0x80;
    memcpy(m->base + 0x80, "PE\0\0", 4);
    m->next = g_modules;
    g_modules = m;
    return m;
}

Module *module_for_system(const char *name) { return shim_module(name); }

typedef BOOL(STDCALL *DllMainFn)(u32, u32, void *);

Module *pe_load(const char *name) {
    Module *m = module_by_name(name);
    if (m) return m;
    char path[200];
    ksnprintf(path, sizeof path, "EngineDLLs\\%s", basename_of(name));
    RdFile *f = rd_find(path);
    if (!f) {
        // system DLLs are provided by shims
        static const char *sys[] = {"kernel32", "user32", "gdi32", "advapi32", "ole32", "mpr", "comctl32", "ntdll", "oleaut32", 0};
        for (int i = 0; sys[i]; i++)
            if (!strnicmp(basename_of(name), sys[i], strlen(sys[i]))) return shim_module(name);
        kprintf("pe_load: %s not found\n", name);
        return 0;
    }
    u8 *img = f->data;
    u32 nt = rd32(img + 0x3C);
    u16 nsec = rd16(img + nt + 6), optsz = rd16(img + nt + 20);
    u8 *opt = img + nt + 24;
    u32 entry = rd32(opt + 16), ibase = rd32(opt + 28), isize = rd32(opt + 56), hsize = rd32(opt + 60);
    void *raw;
    u8 *b = kmalloc_aligned(isize, 0x10000, &raw);
    if (!b) panic("pe_load: out of memory");
    memset(b, 0, isize);
    memcpy(b, img, hsize);
    u8 *sec = opt + optsz;
    for (int i = 0; i < nsec; i++, sec += 40) {
        u32 vsz = rd32(sec + 8), va = rd32(sec + 12), rsz = rd32(sec + 16), rptr = rd32(sec + 20);
        u32 n = rsz < vsz || !vsz ? rsz : vsz;
        if (n && rptr) memcpy(b + va, img + rptr, n);
    }
    // relocations
    u32 delta = (u32)b - ibase;
    u32 rdir = rd32(opt + 96 + 5 * 8), rsize = rd32(opt + 96 + 5 * 8 + 4);
    if (delta && rdir) {
        u8 *p = b + rdir, *end = p + rsize;
        while (p < end) {
            u32 page = rd32(p), bs = rd32(p + 4);
            if (!bs) break;
            for (u32 k = 8; k < bs; k += 2) {
                u16 e = rd16(p + k);
                if ((e >> 12) == 3) *(u32 *)(b + page + (e & 0xFFF)) += delta;
            }
            p += bs;
        }
    }
    m = kcalloc(sizeof(Module));
    strcpy(m->name, basename_of(name));
    m->base = b;
    m->size = isize;
    m->entry = entry ? (u32)b + entry : 0;
    m->next = g_modules;
    g_modules = m;
    // imports
    u32 idir = rd32(opt + 96 + 8);
    if (idir) {
        for (u8 *d = b + idir; rd32(d + 12); d += 20) {
            const char *dll = (char *)(b + rd32(d + 12));
            Module *dm = pe_load(dll);
            u32 ilt = rd32(d) ? rd32(d) : rd32(d + 16), iat = rd32(d + 16);
            for (u32 k = 0;; k++) {
                u32 t = rd32(b + ilt + k * 4);
                if (!t) break;
                void *fn = 0;
                const char *fname = "(ordinal)";
                if (t & 0x80000000u) fn = dm ? pe_export(dm, (const char *)(t & 0xFFFF)) : 0;
                else { fname = (char *)(b + t + 2); fn = dm ? pe_export(dm, fname) : 0; }
                if (!fn) { kprintf("unresolved import %s!%s\n", dll, fname); fn = shim_lookup("missing", fname); }
                *(u32 *)(b + iat + k * 4) = (u32)fn;
            }
        }
    }
    kprintf("loaded %s at %p\n", m->name, b);
    if (m->entry) {
        BOOL ok = ((DllMainFn)m->entry)((u32)b, 1, 0);
        if (!ok) kprintf("DllMain(%s) failed\n", m->name);
    }
    return m;
}

void modules_thread_attach(int attach) {
    for (Module *m = g_modules; m; m = m->next)
        if (!m->is_shim && m->entry) ((DllMainFn)m->entry)((u32)m->base, attach ? 2 : 3, 0);
}
