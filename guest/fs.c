// RAM disk holding the FlexTalk engine files (multiboot module "VXRD").
#include "kernel.h"

static RdFile *g_files;

static void norm(const char *in, char *out, int n) {
    int j = 0;
    for (int i = 0; in[i] && j < n - 1; i++) {
        char c = in[i] == '/' ? '\\' : in[i];
        if (c == '\\' && j > 0 && out[j - 1] == '\\') continue;  // collapse doubles
        out[j++] = c;
    }
    out[j] = 0;
}

const char *rd_relpath(const char *path) {
    static char buf[260];
    char tmp[260];
    norm(path, tmp, sizeof tmp);
    const char *p = tmp;
    size_t rl = strlen(ROOT_DIR);
    if (!strnicmp(p, ROOT_DIR, rl) && (p[rl] == '\\' || p[rl] == 0)) p += rl;
    while (*p == '\\') p++;
    if (p[0] == '.' && p[1] == '\\') p += 2;
    strcpy(buf, p);
    return buf;
}

void rd_init(u8 *blob, u32 size) {
    if (size < 8 || memcmp(blob, "VXRD", 4)) panic("bad ramdisk");
    u32 count = *(u32 *)(blob + 4);
    u32 off = 8;
    for (u32 i = 0; i < count; i++) {
        u32 plen = *(u32 *)(blob + off); off += 4;
        RdFile *f = kcalloc(sizeof(RdFile));
        memcpy(f->path, blob + off, plen < 159 ? plen : 159);
        off += (plen + 3) & ~3u;
        f->size = f->cap = *(u32 *)(blob + off); off += 4;
        f->data = blob + off;  // in place (module memory stays reserved)
        off += (f->size + 3) & ~3u;
        f->next = g_files;
        g_files = f;
    }
    kprintf("ramdisk: %u files\n", count);
}

RdFile *rd_find(const char *path) {
    const char *rel = rd_relpath(path);
    for (RdFile *f = g_files; f; f = f->next)
        if (!stricmp(f->path, rel)) return f;
    return 0;
}

RdFile *rd_create(const char *path) {
    RdFile *f = rd_find(path);
    if (f) { f->size = 0; return f; }
    f = kcalloc(sizeof(RdFile));
    strcpy(f->path, rd_relpath(path));
    f->cap = 4096;
    f->data = kcalloc(f->cap);
    f->next = g_files;
    g_files = f;
    return f;
}

RdFile *rd_first(void) { return g_files; }
