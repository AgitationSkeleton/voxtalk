// voxtalk guest kernel: a tiny freestanding "mini-Windows" that hosts the
// FlexTalk engine DLLs inside an emulated x86 machine (v86) in the browser.
#pragma once

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;
typedef unsigned long long u64;
typedef long long i64;
typedef u32 size_t;
typedef u16 wchar;

#define NULL ((void *)0)
#define STDCALL __attribute__((stdcall))
#define CDECL __attribute__((cdecl))

typedef u32 DWORD;
typedef i32 LONG;
typedef i32 BOOL;
typedef u32 HANDLE;
typedef i32 HRESULT;

#define INVALID_HANDLE_VALUE 0xFFFFFFFFu

// ---- runtime (rt.c) ----
void serial_putc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...);
int kvsnprintf(char *buf, int n, const char *fmt, __builtin_va_list ap);
int ksnprintf(char *buf, int n, const char *fmt, ...);
void panic(const char *msg);

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strcpy(char *d, const char *s);
int strcmp(const char *a, const char *b);
int stricmp(const char *a, const char *b);
int strnicmp(const char *a, const char *b, size_t n);
char ktolower(char c);
char ktoupper(char c);

void heap_init(u32 start, u32 end);
void *kmalloc(size_t n);
void *kcalloc(size_t n);
void kfree(void *p);
void *krealloc(void *p, size_t n);
size_t ksize(void *p);
void *kmalloc_aligned(size_t n, size_t align, void **raw);

// ---- threads (thread.c) ----
typedef struct Thread Thread;
struct Thread {
    u32 esp;
    u32 id;
    u8 *stack;
    u32 stack_size;
    u8 *teb;
    int exited;
    u32 exit_code;
    u32 tls[64];
    u32 last_error;
    u32 (STDCALL *start)(void *);
    void *param;
    int suspended;
    Thread *next;
};
extern Thread *g_current;
void threads_init(void);
Thread *thread_create(u32 (STDCALL *fn)(void *), void *param, int suspended);
void yield(void);
u32 ticks(void);
void set_fs_base(u32 base);

// ---- ramdisk (fs.c) ----
typedef struct RdFile {
    char path[160];   // relative, backslashes, e.g. "EngineData\\AE1M11LC.INV"
    u8 *data;
    u32 size, cap;
    struct RdFile *next;
} RdFile;
void rd_init(u8 *blob, u32 size);
RdFile *rd_find(const char *path);     // path may be absolute (C:\VOXTALK\...)
RdFile *rd_create(const char *path);
const char *rd_relpath(const char *path);
RdFile *rd_first(void);
#define ROOT_DIR "C:\\VOXTALK"

// ---- PE loader (pe.c) ----
typedef struct Module {
    char name[64];
    u8 *base;
    u32 size;
    u32 entry;
    int is_shim;  // fake handle for a system DLL
    struct Module *next;
} Module;
Module *pe_load(const char *name);          // loads from ramdisk EngineDLLs\, runs DllMain
Module *module_by_name(const char *name);
Module *module_by_base(u32 base);
void *pe_export(Module *m, const char *name);
void modules_thread_attach(int attach);
extern Module *g_modules;

// ---- shim table (generated) ----
typedef struct ShimEntry { const char *dll; const char *name; void *fn; } ShimEntry;
extern const ShimEntry g_shims[];
void *shim_lookup(const char *dll, const char *name);

// ---- registry (registry.c) ----
#define HKEY_CLASSES_ROOT 0x80000000u
#define HKEY_CURRENT_USER 0x80000001u
#define HKEY_LOCAL_MACHINE 0x80000002u
#define HKEY_USERS 0x80000003u
void reg_set_str(const char *path, const char *name, const char *val);
void reg_set_dword(const char *path, const char *name, u32 val);

// ---- windows/messages (win32.c) ----
void pump_messages(void);

// ---- misc ----
void kmain(u32 magic, u32 mbinfo);
