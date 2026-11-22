// Cooperative threads with per-thread TEBs (FS segment), as Win32 code expects.
#include "kernel.h"

// ---- GDT: null, code, data, fs ----
static u32 g_gdt[8] __attribute__((aligned(16)));
static struct __attribute__((packed)) { u16 limit; u32 base; } g_gdtr;

static void gdt_entry(int i, u32 base, u32 limit, u8 access, u8 flags) {
    u32 lo = (limit & 0xFFFF) | ((base & 0xFFFF) << 16);
    u32 hi = ((base >> 16) & 0xFF) | ((u32)access << 8) | (((limit >> 16) & 0xF) << 16) | ((u32)flags << 20) | (base & 0xFF000000);
    g_gdt[i * 2] = lo;
    g_gdt[i * 2 + 1] = hi;
}

void set_fs_base(u32 base) {
    gdt_entry(3, base, 0xFFFFF, 0x92, 0xC);
    __asm__ volatile("movw $0x18, %%ax; movw %%ax, %%fs" ::: "eax", "memory");
}

static void gdt_init(void) {
    gdt_entry(0, 0, 0, 0, 0);
    gdt_entry(1, 0, 0xFFFFF, 0x9A, 0xC);  // code
    gdt_entry(2, 0, 0xFFFFF, 0x92, 0xC);  // data
    gdt_entry(3, 0, 0xFFFFF, 0x92, 0xC);  // fs (TEB)
    g_gdtr.limit = sizeof(g_gdt) - 1;
    g_gdtr.base = (u32)g_gdt;
    __asm__ volatile(
        "lgdt %0\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n movw %%ax, %%es\n movw %%ax, %%ss\n movw %%ax, %%gs\n"
        "movw $0x18, %%ax\n movw %%ax, %%fs\n"
        :: "m"(g_gdtr) : "eax", "memory");
}

static void fpu_init(void) {
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1u << 2);  // EM off
    cr0 |= (1u << 1) | (1u << 5);  // MP, NE
    __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));
    __asm__ volatile("fninit");
    u16 cw = 0x027F;  // 53-bit precision like the MSVC runtime
    __asm__ volatile("fldcw %0" ::"m"(cw));
}

// ---- scheduler ----
Thread *g_current;
static Thread *g_threads;
static u32 g_next_tid = 0x100;
static u32 g_ticks;
static u8 g_peb[4096] __attribute__((aligned(16)));

u32 ticks(void) { return g_ticks; }

static u8 *make_teb(Thread *t) {
    u8 *teb = kcalloc(4096);
    u32 *w = (u32 *)teb;
    w[0] = 0xFFFFFFFF;                       // SEH chain end
    w[1] = (u32)(t->stack + t->stack_size);  // stack base (top)
    w[2] = (u32)t->stack;                    // stack limit
    w[6] = (u32)teb;                         // self
    w[8] = 0x10;                             // process id
    w[9] = t->id;                            // thread id
    w[12] = (u32)g_peb;                      // PEB
    return teb;
}

// switch_to(&old->esp, new_esp): saves callee-saved regs, swaps stacks
extern void switch_stack(u32 *save_esp, u32 new_esp);
__asm__(
    ".globl _switch_stack\n"
    "_switch_stack:\n"
    "  movl 4(%esp), %eax\n"
    "  movl 8(%esp), %edx\n"
    "  pushl %ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n"
    "  movl %esp, (%eax)\n"
    "  movl %edx, %esp\n"
    "  popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n"
    "  ret\n");

static void thread_entry(void) {
    Thread *t = g_current;
    modules_thread_attach(1);
    u32 rc = t->start(t->param);
    modules_thread_attach(0);
    t->exit_code = rc;
    t->exited = 1;
    for (;;) yield();
}

void threads_init(void) {
    fpu_init();
    gdt_init();
    Thread *t = kcalloc(sizeof(Thread));
    t->id = g_next_tid++;
    t->stack = (u8 *)0x00200000;  // boot stack region is fine; limit is informational
    t->stack_size = 0x00100000;
    t->teb = make_teb(t);
    set_fs_base((u32)t->teb);
    g_current = g_threads = t;
    t->next = t;
}

Thread *thread_create(u32 (STDCALL *fn)(void *), void *param, int suspended) {
    Thread *t = kcalloc(sizeof(Thread));
    t->id = g_next_tid++;
    t->stack_size = 1024 * 1024;
    t->stack = kmalloc(t->stack_size);
    t->teb = make_teb(t);
    t->start = fn;
    t->param = param;
    t->suspended = suspended;
    u32 *sp = (u32 *)(t->stack + t->stack_size - 64);
    *--sp = 0;                    // fake return address for thread_entry
    *--sp = (u32)thread_entry;    // ret target
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;  // ebp ebx esi edi
    t->esp = (u32)sp;
    // insert into ring after current
    t->next = g_current->next;
    g_current->next = t;
    return t;
}

void yield(void) {
    g_ticks++;
    Thread *cur = g_current, *n = cur->next;
    while (n != cur && (n->exited || n->suspended)) n = n->next;
    if (n == cur) return;
    // save FPU control word implicitly shared; fine for this engine
    g_current = n;
    set_fs_base((u32)n->teb);
    switch_stack(&cur->esp, n->esp);
}
