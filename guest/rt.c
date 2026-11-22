#include "kernel.h"

// ---------------------------------------------------------------- serial
static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(port)); }
static inline u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }

void serial_putc(char c) {
    for (int i = 0; i < 100000 && !(inb(0x3FD) & 0x20); i++) {}
    outb(0x3F8, (u8)c);
}
void kputs(const char *s) { while (*s) serial_putc(*s++); }

// ---------------------------------------------------------------- strings
void *memset(void *d, int c, size_t n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, size_t n) {
    u8 *a = d; const u8 *b = s;
    if (((u32)a | (u32)b | n) % 4 == 0) { u32 *x = d; const u32 *y = s; n /= 4; while (n--) *x++ = *y++; return d; }
    while (n--) *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    u8 *a = d; const u8 *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const u8 *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (u8)*a - (u8)*b; }
char ktolower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
char ktoupper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int stricmp(const char *a, const char *b) {
    while (*a && ktolower(*a) == ktolower(*b)) a++, b++;
    return (u8)ktolower(*a) - (u8)ktolower(*b);
}
int strnicmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        int d = (u8)ktolower(*a) - (u8)ktolower(*b);
        if (d || !*a) return d;
    }
    return 0;
}

// ---------------------------------------------------------------- printf
static int fmt_num(char *out, int n, int pos, u32 v, int base, int upper, int width, char pad, int neg) {
    char tmp[16]; int i = 0;
    const char *dg = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { tmp[i++] = dg[v % base]; v /= base; } while (v);
    if (neg) tmp[i++] = '-';
    while (i < width) tmp[i++] = pad;
    while (i) { if (pos < n - 1) out[pos] = tmp[--i]; else --i; pos++; }
    return pos;
}
int kvsnprintf(char *out, int n, const char *f, __builtin_va_list ap) {
    int pos = 0;
    for (; *f; f++) {
        if (*f != '%') { if (pos < n - 1) out[pos] = *f; pos++; continue; }
        f++;
        char pad = ' '; int width = 0, left = 0;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l' || *f == 'h') f++;
        switch (*f) {
        case 'd': case 'i': { i32 v = __builtin_va_arg(ap, i32); pos = fmt_num(out, n, pos, v < 0 ? -v : v, 10, 0, width, pad, v < 0); break; }
        case 'u': pos = fmt_num(out, n, pos, __builtin_va_arg(ap, u32), 10, 0, width, pad, 0); break;
        case 'x': pos = fmt_num(out, n, pos, __builtin_va_arg(ap, u32), 16, 0, width, pad, 0); break;
        case 'X': pos = fmt_num(out, n, pos, __builtin_va_arg(ap, u32), 16, 1, width, pad, 0); break;
        case 'p': pos = fmt_num(out, n, pos, __builtin_va_arg(ap, u32), 16, 0, 8, '0', 0); break;
        case 'c': { char c = (char)__builtin_va_arg(ap, int); if (pos < n - 1) out[pos] = c; pos++; break; }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            if (!left) for (int i = len; i < width; i++) { if (pos < n - 1) out[pos] = ' '; pos++; }
            while (*s) { if (pos < n - 1) out[pos] = *s; pos++; s++; }
            if (left) for (int i = len; i < width; i++) { if (pos < n - 1) out[pos] = ' '; pos++; }
            break;
        }
        case '%': if (pos < n - 1) out[pos] = '%'; pos++; break;
        default: if (pos < n - 1) out[pos] = *f; pos++; break;
        }
    }
    if (n > 0) out[pos < n - 1 ? pos : n - 1] = 0;
    return pos;
}
int ksnprintf(char *buf, int n, const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = kvsnprintf(buf, n, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
void kprintf(const char *fmt, ...) {
    char buf[512];
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    __builtin_va_end(ap);
    kputs(buf);
}
void panic(const char *msg) {
    kprintf("PANIC %s\n", msg);
    for (;;) __asm__ volatile("cli; hlt");
}

// ---------------------------------------------------------------- heap
// Address-ordered free list, first fit, coalescing. 16-byte headers.
typedef struct Blk { u32 size; u32 used; struct Blk *next; u32 pad; } Blk;
static Blk *g_free;
static u32 g_heap_lo, g_heap_hi;

void heap_init(u32 start, u32 end) {
    start = (start + 15) & ~15u;
    g_heap_lo = start; g_heap_hi = end;
    g_free = (Blk *)start;
    g_free->size = (end - start) & ~15u;
    g_free->used = 0;
    g_free->next = 0;
}

void *kmalloc(size_t n) {
    u32 need = ((n + 15) & ~15u) + sizeof(Blk);
    Blk **pp = &g_free;
    for (Blk *b = g_free; b; pp = &b->next, b = b->next) {
        if (b->size < need) continue;
        if (b->size - need >= 64) {  // split
            Blk *rest = (Blk *)((u8 *)b + need);
            rest->size = b->size - need; rest->used = 0; rest->next = b->next;
            b->size = need;
            *pp = rest;
        } else {
            *pp = b->next;
        }
        b->used = 0xA110C8ED;
        b->next = 0;
        return b + 1;
    }
    kprintf("kmalloc failed for %u bytes\n", n);
    return 0;
}
void *kcalloc(size_t n) { void *p = kmalloc(n); if (p) memset(p, 0, n); return p; }
size_t ksize(void *p) { return p ? ((Blk *)p - 1)->size - sizeof(Blk) : 0; }

void kfree(void *p) {
    if (!p) return;
    Blk *b = (Blk *)p - 1;
    if (b->used != 0xA110C8ED) { kprintf("kfree: bad pointer %p\n", p); return; }
    b->used = 0;
    // insert in address order
    Blk *prev = 0, *cur = g_free;
    while (cur && cur < b) { prev = cur; cur = cur->next; }
    b->next = cur;
    if (prev) prev->next = b; else g_free = b;
    // coalesce with next
    if (cur && (u8 *)b + b->size == (u8 *)cur) { b->size += cur->size; b->next = cur->next; }
    // coalesce with prev
    if (prev && (u8 *)prev + prev->size == (u8 *)b) { prev->size += b->size; prev->next = b->next; }
}

void *krealloc(void *p, size_t n) {
    if (!p) return kmalloc(n);
    size_t old = ksize(p);
    if (n <= old) return p;
    void *q = kmalloc(n);
    if (!q) return 0;
    memcpy(q, p, old);
    kfree(p);
    return q;
}

void *kmalloc_aligned(size_t n, size_t align, void **raw) {
    u8 *r = kmalloc(n + align);
    if (!r) return 0;
    *raw = r;
    return (void *)(((u32)r + align - 1) & ~(align - 1));
}

// gcc may emit calls to these for big stack frames / 64-bit maths
void ___chkstk_ms(void) {}
