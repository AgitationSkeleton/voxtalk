#include "engine.h"
#include "sapi4.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Registry redirection. FlexTalk reads its configuration from
// HKLM/HKCU\Software\AT&T\... and Software\Voice\.... We patch the import
// tables of the engine DLLs so those paths resolve inside a private,
// process-local app hive (RegLoadAppKey) instead of the real registry.
// ---------------------------------------------------------------------------
static HKEY g_overlay = nullptr;

static bool redirectable(HKEY k, LPCSTR sub) {
    if (!g_overlay || !sub) return false;
    if (k != HKEY_LOCAL_MACHINE && k != HKEY_CURRENT_USER) return false;
    return !_strnicmp(sub, "Software\\AT&T", 13) || !_strnicmp(sub, "Software\\Voice", 14);
}

static LONG WINAPI hk_RegOpenKeyExA(HKEY k, LPCSTR s, DWORD o, REGSAM r, PHKEY p) {
    if (redirectable(k, s)) return RegOpenKeyExA(g_overlay, s, 0, r, p);
    return RegOpenKeyExA(k, s, o, r, p);
}
static LONG WINAPI hk_RegOpenKeyA(HKEY k, LPCSTR s, PHKEY p) {
    if (redirectable(k, s)) return RegOpenKeyExA(g_overlay, s, 0, KEY_ALL_ACCESS, p);
    return RegOpenKeyA(k, s, p);
}
static LONG WINAPI hk_RegCreateKeyA(HKEY k, LPCSTR s, PHKEY p) {
    if (redirectable(k, s)) return RegCreateKeyExA(g_overlay, s, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, p, nullptr);
    return RegCreateKeyA(k, s, p);
}
static LONG WINAPI hk_RegCreateKeyExA(HKEY k, LPCSTR s, DWORD res, LPSTR cls, DWORD opt, REGSAM sam,
                                      LPSECURITY_ATTRIBUTES sa, PHKEY p, LPDWORD disp) {
    if (redirectable(k, s)) return RegCreateKeyExA(g_overlay, s, 0, cls, 0, sam, nullptr, p, disp);
    return RegCreateKeyExA(k, s, res, cls, opt, sam, sa, p, disp);
}
static LONG WINAPI hk_RegDeleteKeyA(HKEY k, LPCSTR s) {
    if (redirectable(k, s)) return RegDeleteKeyA(g_overlay, s);
    return RegDeleteKeyA(k, s);
}

static void patchImports(HMODULE m) {
    auto base = (BYTE *)m;
    auto nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp((char *)(base + imp->Name), "ADVAPI32.dll")) continue;
        auto thunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        auto orig = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        for (; orig->u1.AddressOfData; orig++, thunk++) {
            if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            const char *nm = (char *)((IMAGE_IMPORT_BY_NAME *)(base + orig->u1.AddressOfData))->Name;
            void *rep = nullptr;
            if (!strcmp(nm, "RegOpenKeyExA")) rep = (void *)hk_RegOpenKeyExA;
            else if (!strcmp(nm, "RegOpenKeyA")) rep = (void *)hk_RegOpenKeyA;
            else if (!strcmp(nm, "RegCreateKeyA")) rep = (void *)hk_RegCreateKeyA;
            else if (!strcmp(nm, "RegCreateKeyExA")) rep = (void *)hk_RegCreateKeyExA;
            else if (!strcmp(nm, "RegDeleteKeyA")) rep = (void *)hk_RegDeleteKeyA;
            if (!rep) continue;
            DWORD old;
            VirtualProtect(&thunk->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            thunk->u1.Function = (ULONG_PTR)rep;
            VirtualProtect(&thunk->u1.Function, sizeof(void *), old, &old);
        }
    }
}

// ---------------------------------------------------------------------------
// In-memory SAPI audio destination
// ---------------------------------------------------------------------------
namespace {
struct MemAudioDest final : IAudio, IAudioDest {
    virtual ~MemAudioDest() = default;
    LONG ref = 1;
    std::vector<BYTE> data;
    WAVEFORMATEX wfx{WAVE_FORMAT_PCM, 1, 11025, 22050, 2, 16, 0};
    IAudioDestNotifySink *sink = nullptr;
    bool started = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void **p) override {
        if (r == IID_IUnknown || r == IID_IAudio_) { *p = (IAudio *)this; AddRef(); return S_OK; }
        if (r == IID_IAudioDest_) { *p = (IAudioDest *)this; AddRef(); return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Flush() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LevelGet(DWORD *d) override { *d = 0xffff; return S_OK; }
    HRESULT STDMETHODCALLTYPE LevelSet(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PassNotify(PVOID p, IID) override { sink = (IAudioDestNotifySink *)p; return S_OK; }
    HRESULT STDMETHODCALLTYPE PosnGet(PQWORD q) override { *q = data.size(); return S_OK; }
    HRESULT STDMETHODCALLTYPE Claim() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UnClaim() override {
        if (sink && started) { started = false; sink->AudioStop(0); }
        started = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Start() override {
        if (!started) { started = true; if (sink) sink->AudioStart(); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stop() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TotalGet(PQWORD q) override { *q = data.size(); return S_OK; }
    HRESULT STDMETHODCALLTYPE ToFileTime(PQWORD, FILETIME *f) override { GetSystemTimeAsFileTime(f); return S_OK; }
    HRESULT STDMETHODCALLTYPE WaveFormatGet(PSDATA s) override {
        s->pData = CoTaskMemAlloc(sizeof(wfx));
        if (!s->pData) return E_OUTOFMEMORY;
        memcpy(s->pData, &wfx, sizeof(wfx));
        s->dwSize = sizeof(wfx);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE WaveFormatSet(SDATA s) override {
        WAVEFORMATEX w{};
        memcpy(&w, s.pData, s.dwSize < sizeof(w) ? s.dwSize : sizeof(w));
        if (w.wFormatTag != WAVE_FORMAT_PCM || w.nChannels != 1 || w.wBitsPerSample != 16) return 0x80040202;
        wfx = w;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FreeSpace(DWORD *d, BOOL *eof) override { *d = 1 << 20; if (eof) *eof = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE DataSet(PVOID p, DWORD n) override {
        data.insert(data.end(), (BYTE *)p, (BYTE *)p + n);
        if (!started) { started = true; if (sink) sink->AudioStart(); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BookMark(DWORD id) override {
        if (sink) sink->BookMark(id, FALSE);
        return S_OK;
    }
};

struct DoneSink : ITTSBufNotifySink {
    volatile bool done = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void **p) override {
        if (r == IID_IUnknown || r == IID_ITTSBufNotifySink_) { *p = this; return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE TextDataDone(QWORD, DWORD) override { done = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE TextDataStarted(QWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BookMark(QWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE WordPosition(QWORD, DWORD) override { return S_OK; }
};
}  // namespace

// ---------------------------------------------------------------------------
static const char *FT = "Software\\AT&T\\Watson\\2.0\\FlexTalk\\2.0";
static const char *SPEAKER_GUID = "{11260610-C84A-11ce-AEC4-0000C0E9CB87}";

static void regStr(const char *sub, const char *name, const std::string &v) {
    HKEY k;
    if (RegCreateKeyExA(g_overlay, sub, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &k, nullptr)) return;
    RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)v.c_str(), (DWORD)v.size() + 1);
    RegCloseKey(k);
}
static void regDw(const char *sub, const char *name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExA(g_overlay, sub, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &k, nullptr)) return;
    RegSetValueExA(k, name, 0, REG_DWORD, (const BYTE *)&v, 4);
    RegCloseKey(k);
}
static std::string narrow(const std::wstring &w) {
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
static int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

void FlexEngine::writeConfig(const SpeakerParams &p) {
    char b[256];
    snprintf(b, sizeof b, "%s\\Directories", FT);
    regStr(b, "RootDirectory", narrow(root_));
    snprintf(b, sizeof b, "%s\\Options", FT);
    regStr(b, "Gender", "Male");
    regStr(b, "SampleRate", "11025");
    snprintf(b, sizeof b, "%s\\Dictionaries", FT);
    regStr(b, "StandardDictionary", "2 ft_sdict.dll");
    regStr(b, "ExceptionDictionary", "0 ft_udict.dll");
    regStr(b, "HomographDictionary", "1 ft_hgrap.dll");
    regStr(b, "ProperNameDictionary", "3 ft_namsa.dll");
    regStr(b, "LetterToSoundDictionary", "4 ft_l2s.dll");
    snprintf(b, sizeof b, "%s\\Speakers\\%s", FT, SPEAKER_GUID);
    regStr(b, "Speaker Name", "Keith Bell");
    regStr(b, "Mode Name", "Keith Bell");
    regDw(b, "Age", 30);
    regDw(b, "Gender", 2);
    regDw(b, "Pitch", clamp100(p.pitch));
    regDw(b, "Expression", clamp100(p.expression));
    regDw(b, "Speaking Rate", clamp100(p.rate));
    regDw(b, "Head Size", clamp100(p.headSize));
    regDw(b, "Prominences", clamp100(p.prominences));
    regDw(b, "Lowering", clamp100(p.lowering));
    regDw(b, "Volume", clamp100(p.volume));
}

bool FlexEngine::init(const std::wstring &root, const std::wstring &stateDir, std::wstring &err) {
    root_ = root;
    // Private registry hive (per process so several instances can coexist).
    wchar_t hv[MAX_PATH];
    swprintf(hv, MAX_PATH, L"%ls\\regoverlay-%lu.dat", stateDir.c_str(), GetCurrentProcessId());
    hivePath_ = hv;
    DeleteFileW(hivePath_.c_str());
    LONG rc = RegLoadAppKeyW(hivePath_.c_str(), &g_overlay, KEY_ALL_ACCESS, 0, 0);
    if (rc != ERROR_SUCCESS) { err = L"Could not create the private registry hive."; return false; }

    std::wstring dlls = root + L"\\EngineDLLs";
    SetDllDirectoryW(dlls.c_str());
    HMODULE ft = LoadLibraryW((dlls + L"\\FlexTalk.dll").c_str());
    if (!ft) { err = L"Could not load FlexTalk.dll."; return false; }
    patchImports(ft);
    for (const wchar_t *d : {L"ft_sdict.dll", L"ft_udict.dll", L"ft_hgrap.dll", L"ft_namsa.dll", L"ft_l2s.dll"}) {
        HMODULE m = LoadLibraryW((dlls + L"\\" + d).c_str());
        if (!m) { err = std::wstring(L"Could not load ") + d; return false; }
        patchImports(m);
    }
    writeConfig(SpeakerParams());

    auto gco = (HRESULT(WINAPI *)(REFCLSID, REFIID, void **))GetProcAddress(ft, "DllGetClassObject");
    if (!gco) { err = L"FlexTalk.dll has no DllGetClassObject."; return false; }
    IClassFactory *cf = nullptr;
    if (FAILED(gco(CLSID_FlexTalk, IID_IClassFactory, (void **)&cf))) { err = L"FlexTalk class factory unavailable."; return false; }
    factory_ = cf;
    ITTSEnumW *en = nullptr;
    if (FAILED(cf->CreateInstance(nullptr, IID_ITTSEnumW_, (void **)&en))) { err = L"FlexTalk engine enumerator unavailable."; return false; }
    enum_ = en;
    TTSMODEINFOW mi;
    ULONG got = 0;
    bool found = false;
    while (en->Next(1, &mi, &got) == S_OK && got == 1) {
        if (!_wcsicmp(mi.szSpeaker, L"Keith Bell")) { memcpy(mode_, &mi.gModeID, 16); found = true; }
    }
    if (!found) { err = L"FlexTalk voice not found."; return false; }
    return true;
}

bool FlexEngine::speak(const SpeakerParams &p, const std::wstring &text, std::vector<short> &pcm, std::wstring &err) {
    pcm.clear();
    if (!enum_) { err = L"Engine not initialised."; return false; }
    writeConfig(p);
    auto en = (ITTSEnumW *)enum_;
    GUID mode;
    memcpy(&mode, mode_, 16);
    MemAudioDest *ad = new MemAudioDest;
    ITTSCentralW *c = nullptr;
    HRESULT hr = en->Select(mode, &c, (IAudio *)ad);
    if (FAILED(hr) || !c) { ad->Release(); err = L"FlexTalk could not select the voice."; return false; }
    DoneSink ds;
    std::wstring t = text;
    SDATA sd{(PVOID)t.c_str(), (DWORD)((t.size() + 1) * sizeof(wchar_t))};
    hr = c->TextData(CHARSET_TEXT, TTSDATAFLAG_TAGGED, sd, &ds, IID_ITTSBufNotifySink_);
    if (FAILED(hr)) { c->Release(); ad->Release(); err = L"FlexTalk rejected the text."; return false; }
    DWORD t0 = GetTickCount(), lastChange = t0;
    size_t lastSz = 0;
    bool ok = false;
    while (GetTickCount() - t0 < 120000) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (ad->data.size() != lastSz) { lastSz = ad->data.size(); lastChange = GetTickCount(); }
        if (ds.done && !ad->started && GetTickCount() - lastChange > 20) { ok = true; break; }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
    }
    c->AudioReset();
    c->Release();
    pcm.resize(ad->data.size() / 2);
    if (!pcm.empty()) memcpy(pcm.data(), ad->data.data(), pcm.size() * 2);
    ad->Release();
    if (!ok) { err = L"FlexTalk timed out."; return false; }
    return true;
}

void FlexEngine::shutdown() {
    if (enum_) { ((ITTSEnumW *)enum_)->Release(); enum_ = nullptr; }
    if (factory_) { ((IClassFactory *)factory_)->Release(); factory_ = nullptr; }
    if (g_overlay) {
        RegCloseKey(g_overlay);
        g_overlay = nullptr;
        // The hive file is released once the last handle closes.
        for (const wchar_t *suf : {L"", L".LOG1", L".LOG2"}) DeleteFileW((hivePath_ + suf).c_str());
    }
}
