// Boot, engine setup and the request loop. The browser talks to this kernel
// through a mailbox in guest memory (see MAILBOX below) and the serial port.
#include "shim.h"

typedef struct { u32 d1; u16 d2, d3; u8 d4[8]; } GUID;
typedef struct { void *p; DWORD n; } SDATA;

static const GUID CLSID_FlexTalk = {0xFFE78E00, 0xC69C, 0x11CE, {0xAE, 0xC1, 0x00, 0x00, 0xC0, 0xE9, 0xCB, 0x87}};
static const GUID IID_IUnknown = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_IClassFactory = {1, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_IAudio = {0xf546b340, 0xc743, 0x11cd, {0x80, 0xe5, 0x00, 0xaa, 0x00, 0x3e, 0x4b, 0x50}};
static const GUID IID_IAudioDest = {0x2ec34da0, 0xc743, 0x11cd, {0x80, 0xe5, 0x00, 0xaa, 0x00, 0x3e, 0x4b, 0x50}};
static const GUID IID_ITTSEnumW = {0x6B837B20, 0x4A47, 0x101B, {0x93, 0x1A, 0x00, 0xAA, 0x00, 0x47, 0xBA, 0x4F}};
static const GUID IID_ITTSBufNotifySink = {0xe4963d40, 0xc743, 0x11cd, {0x80, 0xe5, 0x00, 0xaa, 0x00, 0x3e, 0x4b, 0x50}};

static int guid_eq(const GUID *a, const GUID *b) { return !memcmp(a, b, 16); }

// ------------------------------------------------------------ audio destination
typedef struct AudioDest {
    void **vt_audio;   // IAudio*
    void **vt_dest;    // IAudioDest*
    LONG refs;
    u8 *data;
    u32 len, cap;
    void **sink;       // IAudioDestNotifySink*
    int started;
    u16 wfx[9];
} AudioDest;

#define AD_FROM_DEST(p) ((AudioDest *)((u8 *)(p) - 4))
typedef HRESULT(STDCALL *Fn1)(void *);
typedef HRESULT(STDCALL *FnW)(void *, u32);
typedef HRESULT(STDCALL *Fn2)(void *, u32, u32);

static void sink_call0(AudioDest *a, int idx) { if (a->sink) ((Fn1)(*(void ***)a->sink)[idx])(a->sink); }
static void sink_call1(AudioDest *a, int idx, u32 x) { if (a->sink) ((FnW)(*(void ***)a->sink)[idx])(a->sink, x); }
static void sink_call2(AudioDest *a, int idx, u32 x, u32 y) { if (a->sink) ((Fn2)(*(void ***)a->sink)[idx])(a->sink, x, y); }

static HRESULT STDCALL ad_qi(AudioDest *a, const GUID *r, void **out) {
    if (guid_eq(r, &IID_IUnknown) || guid_eq(r, &IID_IAudio)) { *out = &a->vt_audio; a->refs++; return 0; }
    if (guid_eq(r, &IID_IAudioDest)) { *out = &a->vt_dest; a->refs++; return 0; }
    *out = 0;
    return 0x80004002;
}
static u32 STDCALL ad_addref(AudioDest *a) { return ++a->refs; }
static u32 STDCALL ad_release(AudioDest *a) { return --a->refs; }
static HRESULT STDCALL ad_ok0(AudioDest *a) { return 0; }
static HRESULT STDCALL ad_levelget(AudioDest *a, DWORD *d) { *d = 0xFFFF; return 0; }
static HRESULT STDCALL ad_levelset(AudioDest *a, DWORD d) { return 0; }
static HRESULT STDCALL ad_passnotify(AudioDest *a, void *p, GUID iid) { a->sink = p; return 0; }
static HRESULT STDCALL ad_posnget(AudioDest *a, u64 *q) { *q = a->len; return 0; }
static HRESULT STDCALL ad_unclaim(AudioDest *a) {
    if (a->started) { a->started = 0; sink_call1(a, 3, 0); }  // AudioStop(0)
    return 0;
}
static HRESULT STDCALL ad_start(AudioDest *a) {
    if (!a->started) { a->started = 1; sink_call0(a, 4); }  // AudioStart
    return 0;
}
static HRESULT STDCALL ad_totime(AudioDest *a, u64 *q, u32 *ft) { ft[0] = 0; ft[1] = 0; return 0; }
static HRESULT STDCALL ad_wfget(AudioDest *a, SDATA *s) {
    s->p = kmalloc(18);
    memcpy(s->p, a->wfx, 18);
    s->n = 18;
    return 0;
}
static HRESULT STDCALL ad_wfset(AudioDest *a, SDATA s) {
    const u16 *w = s.p;
    if (w[0] != 1 || w[1] != 1 || w[7] != 16) return 0x80040202;
    memcpy(a->wfx, s.p, s.n < 18 ? s.n : 18);
    return 0;
}
static void *g_vt_audio[] = {ad_qi, ad_addref, ad_release, ad_ok0 /*Flush*/, ad_levelget, ad_levelset, ad_passnotify,
                             ad_posnget, ad_ok0 /*Claim*/, ad_unclaim, ad_start, ad_ok0 /*Stop*/, ad_posnget /*TotalGet*/,
                             ad_totime, ad_wfget, ad_wfset};

static HRESULT STDCALL dd_qi(void *t, const GUID *r, void **o) { return ad_qi(AD_FROM_DEST(t), r, o); }
static u32 STDCALL dd_addref(void *t) { return ad_addref(AD_FROM_DEST(t)); }
static u32 STDCALL dd_release(void *t) { return ad_release(AD_FROM_DEST(t)); }
static HRESULT STDCALL dd_freespace(void *t, DWORD *d, BOOL *eof) { *d = 1 << 20; if (eof) *eof = 0; return 0; }
static HRESULT STDCALL dd_dataset(void *t, void *p, DWORD n) {
    AudioDest *a = AD_FROM_DEST(t);
    if (a->len + n > a->cap) {
        u32 nc = (a->len + n) * 2;
        a->data = krealloc(a->data, nc);
        a->cap = nc;
    }
    memcpy(a->data + a->len, p, n);
    a->len += n;
    if (!a->started) { a->started = 1; sink_call0(a, 4); }
    return 0;
}
static HRESULT STDCALL dd_bookmark(void *t, DWORD id) { sink_call2(AD_FROM_DEST(t), 6, id, 0); return 0; }
static void *g_vt_dest[] = {dd_qi, dd_addref, dd_release, dd_freespace, dd_dataset, dd_bookmark};

// ------------------------------------------------------------ text-done sink
typedef struct { void **vt; volatile int done; } BufSink;
static HRESULT STDCALL bs_qi(BufSink *b, const GUID *r, void **o) {
    if (guid_eq(r, &IID_IUnknown) || guid_eq(r, &IID_ITTSBufNotifySink)) { *o = b; return 0; }
    *o = 0;
    return 0x80004002;
}
static u32 STDCALL bs_addref(BufSink *b) { return 2; }
static u32 STDCALL bs_release(BufSink *b) { return 1; }
static HRESULT STDCALL bs_done(BufSink *b, u64 t, DWORD f) { b->done = 1; return 0; }
static HRESULT STDCALL bs_started(BufSink *b, u64 t) { return 0; }
static HRESULT STDCALL bs_mark(BufSink *b, u64 t, DWORD m) { return 0; }
static void *g_vt_bs[] = {bs_qi, bs_addref, bs_release, bs_done, bs_started, bs_mark, bs_mark};

// ------------------------------------------------------------ engine interfaces
typedef struct { u16 lang; wchar dialect[64]; } LANGW;
typedef struct {
    GUID engine; wchar mfg[262]; wchar product[262]; GUID mode; wchar modeName[262];
    LANGW language; wchar speaker[262]; wchar style[262];
    u16 gender, age; DWORD features, interfaces, engineFeatures;
} TTSMODEINFOW;

typedef HRESULT(STDCALL *PFN_GetClassObject)(const GUID *, const GUID *, void **);
typedef HRESULT(STDCALL *PFN_CreateInstance)(void *, void *, const GUID *, void **);
typedef HRESULT(STDCALL *PFN_Next)(void *, u32, TTSMODEINFOW *, u32 *);
typedef HRESULT(STDCALL *PFN_Select)(void *, GUID, void **, void *);
typedef HRESULT(STDCALL *PFN_TextData)(void *, u32, DWORD, SDATA, void *, GUID);
typedef u32(STDCALL *PFN_Release)(void *);
#define VT(obj) (*(void ***)(obj))

static const char *FT = "Software\\AT&T\\Watson\\2.0\\FlexTalk\\2.0";
static const char *SPK = "{11260610-C84A-11ce-AEC4-0000C0E9CB87}";

static void write_config(const i32 *p) {
    char b[160];
    ksnprintf(b, sizeof b, "%s\\Directories", FT);
    reg_set_str(b, "RootDirectory", ROOT_DIR);
    ksnprintf(b, sizeof b, "%s\\Options", FT);
    reg_set_str(b, "Gender", "Male");
    reg_set_str(b, "SampleRate", "11025");
    ksnprintf(b, sizeof b, "%s\\Dictionaries", FT);
    reg_set_str(b, "StandardDictionary", "2 ft_sdict.dll");
    reg_set_str(b, "ExceptionDictionary", "0 ft_udict.dll");
    reg_set_str(b, "HomographDictionary", "1 ft_hgrap.dll");
    reg_set_str(b, "ProperNameDictionary", "3 ft_namsa.dll");
    reg_set_str(b, "LetterToSoundDictionary", "4 ft_l2s.dll");
    ksnprintf(b, sizeof b, "%s\\Speakers\\%s", FT, SPK);
    reg_set_str(b, "Speaker Name", "Keith Bell");
    reg_set_str(b, "Mode Name", "Keith Bell");
    reg_set_dword(b, "Age", 30);
    reg_set_dword(b, "Gender", 2);
    static const char *names[7] = {"Pitch", "Expression", "Speaking Rate", "Head Size", "Prominences", "Lowering", "Volume"};
    for (int i = 0; i < 7; i++) {
        i32 v = p[i] < 0 ? 0 : p[i] > 100 ? 100 : p[i];
        reg_set_dword(b, names[i], (u32)v);
    }
}

// ------------------------------------------------------------ mailbox
#define MAILBOX 0x00080000u
#define MB_TEXT 0x00081000u
typedef struct {
    u32 magic;        // 'VXMB'
    volatile u32 state;  // 0 boot, 1 ready, 2 request, 3 busy, 4 done, 5 error
    u32 seq;
    i32 params[8];    // pitch, expression, rate, head, prominences, lowering, volume
    u32 text_len;     // UTF-16 units at MB_TEXT
    u32 pcm_addr, pcm_len;  // 16-bit mono 11025 Hz samples
    u32 err;
    char msg[128];
    u32 rd_addr, rd_size;   // ramdisk written into guest memory by the host page
    u32 mem_size;           // guest RAM size in bytes
} Mailbox;
static Mailbox *const g_mb = (Mailbox *)MAILBOX;

static void *g_enum;
static GUID g_mode;
static AudioDest g_ad;

static int engine_init(void) {
    i32 defaults[7] = {27, 59, 33, 46, 17, 40, 100};
    write_config(defaults);
    u32 h = shim_LoadLibraryA("FlexTalk.dll");
    if (!h) return 0;
    PFN_GetClassObject gco = shim_GetProcAddress(h, "DllGetClassObject");
    if (!gco) return 0;
    void *cf = 0;
    if (gco(&CLSID_FlexTalk, &IID_IClassFactory, &cf) || !cf) { kputs("no class factory\n"); return 0; }
    if (((PFN_CreateInstance)VT(cf)[3])(cf, 0, &IID_ITTSEnumW, &g_enum) || !g_enum) { kputs("no enum\n"); return 0; }
    static TTSMODEINFOW mi;
    u32 got = 0;
    int found = 0;
    while (((PFN_Next)VT(g_enum)[3])(g_enum, 1, &mi, &got) == 0 && got == 1) {
        char nm[64];
        int i = 0;
        for (; i < 63 && mi.speaker[i]; i++) nm[i] = (char)mi.speaker[i];
        nm[i] = 0;
        kprintf("mode: %s\n", nm);
        if (!stricmp(nm, "Keith Bell")) { g_mode = mi.mode; found = 1; }
    }
    g_ad.vt_audio = g_vt_audio;
    g_ad.vt_dest = g_vt_dest;
    g_ad.refs = 1;
    static const u16 wfx[9] = {1, 1, 11025, 0, 22050, 0, 2, 16, 0};
    memcpy(g_ad.wfx, wfx, 18);
    return found;
}

static int speak(const i32 *params, const wchar *text, u32 n, const char **err) {
    write_config(params);
    g_ad.len = 0;
    g_ad.started = 0;
    g_ad.sink = 0;
    void *central = 0;
    HRESULT hr = ((PFN_Select)VT(g_enum)[7])(g_enum, g_mode, &central, &g_ad.vt_audio);
    if (hr || !central) { *err = "Select failed"; return 0; }
    static BufSink bs = {g_vt_bs, 0};
    bs.done = 0;
    wchar *t = kmalloc((n + 1) * 2);
    memcpy(t, text, n * 2);
    t[n] = 0;
    SDATA sd = {t, (n + 1) * 2};
    hr = ((PFN_TextData)VT(central)[7])(central, 0, 1, sd, &bs, IID_ITTSBufNotifySink);
    if (hr) { *err = "TextData failed"; ((PFN_Release)VT(central)[2])(central); kfree(t); return 0; }
    u32 last = 0xFFFFFFFF, quiet = 0;
    for (u32 iter = 0; iter < 20000000; iter++) {
        pump_messages();
        yield();
        if (g_ad.len != last) { last = g_ad.len; quiet = 0; } else quiet++;
        if (bs.done && !g_ad.started && quiet > 50) break;
    }
    ((Fn1)VT(central)[11])(central);  // AudioReset
    ((PFN_Release)VT(central)[2])(central);
    for (int i = 0; i < 200; i++) { pump_messages(); yield(); }
    kfree(t);
    return 1;
}

void kmain(u32 magic, u32 mbinfo) {
    kputs("\nvoxtalk guest kernel\n");
    if (magic != 0x2BADB002) panic("not booted by multiboot");
    // The host page writes the engine ramdisk into guest memory after boot.
    memset(g_mb, 0, sizeof(Mailbox));
    g_mb->magic = 0x424D5856;
    g_mb->state = 0;
    kputs("BOOT\n");
    while (g_mb->state != 6) __asm__ volatile("pause");
    u32 mem_top = g_mb->mem_size;
    u32 heap_lo = (g_mb->rd_addr + g_mb->rd_size + 0xFFFF) & ~0xFFFFu;
    heap_init(heap_lo, mem_top - 0x10000);
    kprintf("memory %p, ramdisk %p+%u, heap from %p\n", mem_top, g_mb->rd_addr, g_mb->rd_size, heap_lo);
    threads_init();
    rd_init((u8 *)g_mb->rd_addr, g_mb->rd_size);
    (void)mbinfo;
    if (!engine_init()) {
        g_mb->state = 5;
        strcpy(g_mb->msg, "engine init failed");
        kputs("ERROR engine init failed\n");
        for (;;) __asm__ volatile("pause");
    }
    g_mb->state = 1;
    kputs("READY\n");
    for (;;) {
        if (g_mb->state != 2) { __asm__ volatile("pause"); continue; }
        g_mb->state = 3;
        const char *err = "";
        int ok = speak(g_mb->params, (const wchar *)MB_TEXT, g_mb->text_len, &err);
        g_mb->pcm_addr = (u32)g_ad.data;
        g_mb->pcm_len = ok ? g_ad.len / 2 : 0;
        if (!ok) strcpy(g_mb->msg, err);
        g_mb->seq++;
        g_mb->state = ok ? 4 : 5;
        kputs(ok ? "DONE\n" : "ERROR\n");
    }
}
