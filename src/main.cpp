// voxtalk - FlexTalk-based text-to-speech in the style of Half-Life's VOX announcer.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <deque>
#include "engine.h"
#include "voice.h"
#include "audio_out.h"
#include "pack.h"
#include "resource.h"

#define VOXTALK_VERSION L"1.2.0"

// ===========================================================================
// Shared helpers
// ===========================================================================
static std::wstring extLower(const std::wstring &p) {
    size_t d = p.find_last_of(L'.');
    size_t s = p.find_last_of(L"\\/");
    if (d == std::wstring::npos || (s != std::wstring::npos && d < s)) return L"";
    std::wstring e = p.substr(d);
    for (auto &c : e) c = (wchar_t)towlower(c);
    return e;
}

static bool saveWords(FlexEngine &eng, const VoiceSettings &vs, const OutputOptions &oo, FileFormat fmt,
                      const std::wstring &text, const std::wstring &dir, int &count, std::wstring &err) {
    count = 0;
    VoiceSettings single = vs;
    single.style = Style::Words;
    auto words = splitWords(text);
    if (words.empty()) { err = L"No words to export."; return false; }
    for (auto &w : words) {
        std::vector<float> buf;
        if (!render(eng, single, oo, w.first, buf, err)) return false;
        std::wstring path = dir + L"\\" + (w.second.empty() ? L"word" : w.second) + (fmt == FileFormat::Mp3 ? L".mp3" : L".wav");
        if (!writeAudioFile(path, fmt, buf, oo.sampleRate, oo.eightBit, err)) return false;
        count++;
    }
    return true;
}

// ===========================================================================
// Command line
// ===========================================================================
static void cliOut(const std::wstring &s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w;
    if (h && h != INVALID_HANDLE_VALUE) {
        if (GetFileType(h) == FILE_TYPE_CHAR) WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &w, nullptr);
        else {
            int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
            std::string u(n, 0);
            WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &u[0], n, nullptr, nullptr);
            WriteFile(h, u.data(), (DWORD)u.size(), &w, nullptr);
        }
    }
}

static const wchar_t *USAGE =
    L"voxtalk " VOXTALK_VERSION L" - FlexTalk text-to-speech in the style of Half-Life's VOX\n"
    L"\n"
    L"usage: voxtalk [options] \"text to speak\"\n"
    L"       voxtalk            (no arguments: open the window)\n"
    L"\n"
    L"output:\n"
    L"  -o, --out FILE        write FILE (.wav or .mp3; format taken from the extension)\n"
    L"  --split DIR           write one file per word into DIR (like the game's sound/vox folder)\n"
    L"  --play                play the result\n"
    L"  -r, --rate HZ         11025 (default) or 44100\n"
    L"  -f, --format FMT      wav or mp3 (for --split, or to override the extension)\n"
    L"  --8bit / --16bit      8-bit Half-Life style quantisation on (default) or off\n"
    L"\n"
    L"voice (defaults match Half-Life's VOX):\n"
    L"  --no-game-timing      don't give known words the length they have in the game\n"
    L"  --no-game-pitch       don't give known words the pitch contour they have in the game\n"
    L"  --style STYLE       words (each word spoken separately, as in-game) or sentence\n"
    L"  --pitch N             FlexTalk pitch 0-100        --expression N   intonation 0-100\n"
    L"  --speaking-rate N     FlexTalk rate 0-100         --head-size N    vocal tract size 0-100\n"
    L"  --prominences N       accent strength 0-100       --lowering N     declination 0-100\n"
    L"\n"
    L"effects:\n"
    L"  --speed PCT           playback speed in percent (default 91.875)\n"
    L"  --echo PCT            echo amount, 100 = Half-Life VOX echo (default), 0 = off\n"
    L"  --echo-delay MS       echo delay (default 137)\n"
    L"  --low-cut HZ          high-pass filter frequency (default 160), 0 = off\n"
    L"\n"
    L"text syntax (word-by-word style):\n"
    L"  Half-Life sentence modifiers work: word(s15 e80) plays 15-80 % of a word,\n"
    L"  a standalone (e90) applies to the following words; p = pitch %, v = volume %,\n"
    L"  t = time compression %. Sound effects by name: bizwarn bloop buzwarn dadeda\n"
    L"  deeoo doop woop endgame (original Half-Life sounds by Valve).\n"
    L"\n"
    L"examples:\n"
    L"  voxtalk -o flag.wav \"buzwarn your team has the(s15 e80) enemy flag\"\n"
    L"  voxtalk -o hello.wav \"attention, all personnel.\"\n"
    L"  voxtalk -r 44100 -o warn.mp3 \"warning. power level critical.\"\n"
    L"  voxtalk --split words \"one two three\"\n";

static int runCli(int argc, wchar_t **argv) {
    VoiceSettings vs = voxDefaults();
    OutputOptions oo;
    std::wstring out, split, text, fmtArg;
    bool play = false;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        auto next = [&](std::wstring &v) -> bool {
            if (i + 1 >= argc) { cliOut(L"error: " + a + L" needs a value\n"); return false; }
            v = argv[++i];
            return true;
        };
        std::wstring v;
        if (a == L"-h" || a == L"--help" || a == L"/?") { cliOut(USAGE); return 0; }
        else if (a == L"--version") { cliOut(L"voxtalk " VOXTALK_VERSION L"\n"); return 0; }
        else if (a == L"-o" || a == L"--out") { if (!next(out)) return 2; }
        else if (a == L"--split") { if (!next(split)) return 2; }
        else if (a == L"--play") play = true;
        else if (a == L"-r" || a == L"--rate") { if (!next(v)) return 2; oo.sampleRate = _wtoi(v.c_str()); }
        else if (a == L"-f" || a == L"--format") { if (!next(fmtArg)) return 2; }
        else if (a == L"--8bit") oo.eightBit = true;
        else if (a == L"--16bit") oo.eightBit = false;
        else if (a == L"--no-game-timing") vs.gameTiming = false;
        else if (a == L"--no-game-pitch") vs.gamePitch = false;
        else if (a == L"--style") {
            if (!next(v)) return 2;
            if (v == L"words") vs.style = Style::Words;
            else if (v == L"sentence") vs.style = Style::Sentence;
            else { cliOut(L"error: --style must be words or sentence\n"); return 2; }
        }
        else if (a == L"--pitch") { if (!next(v)) return 2; vs.engine.pitch = _wtoi(v.c_str()); }
        else if (a == L"--expression") { if (!next(v)) return 2; vs.engine.expression = _wtoi(v.c_str()); }
        else if (a == L"--speaking-rate") { if (!next(v)) return 2; vs.engine.rate = _wtoi(v.c_str()); }
        else if (a == L"--head-size") { if (!next(v)) return 2; vs.engine.headSize = _wtoi(v.c_str()); }
        else if (a == L"--prominences") { if (!next(v)) return 2; vs.engine.prominences = _wtoi(v.c_str()); }
        else if (a == L"--lowering") { if (!next(v)) return 2; vs.engine.lowering = _wtoi(v.c_str()); }
        else if (a == L"--speed") { if (!next(v)) return 2; vs.speed = _wtof(v.c_str()) / 100.0; }
        else if (a == L"--echo") { if (!next(v)) return 2; vs.echoAmount = _wtof(v.c_str()) / 100.0; }
        else if (a == L"--echo-delay") { if (!next(v)) return 2; vs.echoDelayMs = _wtof(v.c_str()); }
        else if (a == L"--low-cut") { if (!next(v)) return 2; vs.lowCutHz = _wtof(v.c_str()); }
        else if (!a.empty() && a[0] == L'-' && a.size() > 1) { cliOut(L"error: unknown option " + a + L"\n"); return 2; }
        else { if (!text.empty()) text += L' '; text += a; }
    }
    if (oo.sampleRate != 11025 && oo.sampleRate != 44100) { cliOut(L"error: --rate must be 11025 or 44100\n"); return 2; }
    if (vs.speed < 0.25 || vs.speed > 4) { cliOut(L"error: --speed out of range\n"); return 2; }
    if (text.empty()) { cliOut(L"error: no text given (see --help)\n"); return 2; }
    if (out.empty() && split.empty() && !play) { cliOut(L"error: nothing to do; give -o FILE, --split DIR or --play\n"); return 2; }

    std::wstring engineDir, stateDir, err;
    if (!ensureEngineFiles(engineDir, stateDir, err)) { cliOut(L"error: " + err + L"\n"); return 1; }
    setSoundDirectory(engineDir + L"\\Sounds");
    FlexEngine eng;
    if (!eng.init(engineDir, stateDir, err)) { cliOut(L"error: " + err + L"\n"); return 1; }

    if (!split.empty()) {
        FileFormat ff = (fmtArg == L"mp3") ? FileFormat::Mp3 : FileFormat::Wav;
        CreateDirectoryW(split.c_str(), nullptr);
        int n = 0;
        if (!saveWords(eng, vs, oo, ff, text, split, n, err)) { cliOut(L"error: " + err + L"\n"); return 1; }
        cliOut(L"wrote " + std::to_wstring(n) + L" word files to " + split + L"\n");
    }
    if (!out.empty() || play) {
        std::vector<float> buf;
        if (!render(eng, vs, oo, text, buf, err)) { cliOut(L"error: " + err + L"\n"); return 1; }
        if (!out.empty()) {
            FileFormat ff = extLower(out) == L".mp3" ? FileFormat::Mp3 : FileFormat::Wav;
            if (fmtArg == L"mp3") ff = FileFormat::Mp3;
            if (fmtArg == L"wav") ff = FileFormat::Wav;
            if (!writeAudioFile(out, ff, buf, oo.sampleRate, oo.eightBit, err)) { cliOut(L"error: " + err + L"\n"); return 1; }
            cliOut(L"wrote " + out + L"\n");
        }
        if (play) {
            std::vector<unsigned char> wav = makeWav(buf, oo.sampleRate, false);
            PlaySoundW((LPCWSTR)wav.data(), nullptr, SND_MEMORY | SND_SYNC | SND_NODEFAULT);
        }
    }
    return 0;
}

// ===========================================================================
// GUI
// ===========================================================================
enum {
    IDC_TEXT = 1000, IDC_STYLE, IDC_RATE, IDC_FORMAT, IDC_8BIT, IDC_PLAY, IDC_STOP, IDC_SAVE,
    IDC_SAVEWORDS, IDC_RESET, IDC_STATUS, IDC_SOUND, IDC_INSERT, IDC_TIMING, IDC_SLIDER0 = 1100, IDC_VALUE0 = 1200, IDC_LABEL0 = 1300
};
#define WM_APP_READY (WM_APP + 1)
#define WM_APP_DONE (WM_APP + 2)

struct SliderDef { const wchar_t *label; int lo, hi; };
enum { S_PITCH, S_EXPR, S_RATE, S_HEAD, S_SPEED, S_ECHO, S_DELAY, S_LOWCUT, S_COUNT };
static const SliderDef SLIDERS[S_COUNT] = {
    {L"Pitch", 0, 100},           {L"Expression", 0, 100},   {L"Speaking rate", 0, 100},
    {L"Head size", 0, 100},       {L"Playback speed", 600, 1400}, {L"Echo amount", 0, 200},
    {L"Echo delay", 20, 500},     {L"Low cut", 0, 600},
};

enum class Action { Play, SaveFile, SaveWords };
struct Job {
    Action action;
    VoiceSettings vs;
    OutputOptions oo;
    FileFormat fmt;
    std::wstring text, path;
};
struct JobResult {
    Action action;
    bool ok;
    std::wstring err, path;
    std::vector<float> audio;
    int rate = 11025, words = 0;
};

static HWND g_wnd, g_text, g_style, g_rate, g_format, g_8bit, g_status, g_play, g_save, g_savewords, g_stop, g_reset, g_sound, g_timing;
static HWND g_sl[S_COUNT], g_val[S_COUNT];
static HFONT g_font;
static double g_scale = 1.0;
static HANDLE g_jobEvent;
static CRITICAL_SECTION g_cs;
static std::deque<Job> g_jobs;
static bool g_ready = false, g_busy = false;
static std::wstring g_initErr;

static int S(int v) { return (int)(v * g_scale + 0.5); }

static DWORD WINAPI workerMain(void *) {
    CoInitialize(nullptr);
    FlexEngine eng;
    std::wstring engineDir, stateDir, err;
    bool ok = ensureEngineFiles(engineDir, stateDir, err) && eng.init(engineDir, stateDir, err);
    if (ok) setSoundDirectory(engineDir + L"\\Sounds");
    g_initErr = err;
    PostMessageW(g_wnd, WM_APP_READY, ok, 0);
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &g_jobEvent, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0 + 1) {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) { eng.shutdown(); CoUninitialize(); return 0; }
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            continue;
        }
        for (;;) {
            Job job;
            EnterCriticalSection(&g_cs);
            bool have = !g_jobs.empty();
            if (have) { job = g_jobs.front(); g_jobs.pop_front(); }
            LeaveCriticalSection(&g_cs);
            if (!have) break;
            auto *res = new JobResult;
            res->action = job.action;
            res->path = job.path;
            res->rate = job.oo.sampleRate;
            if (!ok) { res->ok = false; res->err = g_initErr; }
            else if (job.action == Action::SaveWords) {
                res->ok = saveWords(eng, job.vs, job.oo, job.fmt, job.text, job.path, res->words, res->err);
            } else {
                res->ok = render(eng, job.vs, job.oo, job.text, res->audio, res->err);
                if (res->ok && job.action == Action::SaveFile)
                    res->ok = writeAudioFile(job.path, job.fmt, res->audio, job.oo.sampleRate, job.oo.eightBit, res->err);
            }
            PostMessageW(g_wnd, WM_APP_DONE, 0, (LPARAM)res);
        }
    }
}

static void setStatus(const std::wstring &s) { SetWindowTextW(g_status, s.c_str()); }

static void updateValueLabel(int i) {
    int v = (int)SendMessageW(g_sl[i], TBM_GETPOS, 0, 0);
    wchar_t b[64];
    switch (i) {
    case S_SPEED: swprintf(b, 64, L"%.1f %%", v / 10.0); break;
    case S_ECHO: swprintf(b, 64, L"%d %%", v); break;
    case S_DELAY: swprintf(b, 64, L"%d ms", v); break;
    case S_LOWCUT: if (v == 0) wcscpy(b, L"off"); else swprintf(b, 64, L"%d Hz", v); break;
    default: swprintf(b, 64, L"%d", v);
    }
    SetWindowTextW(g_val[i], b);
}

static void setSlider(int i, int v) {
    SendMessageW(g_sl[i], TBM_SETPOS, TRUE, v);
    updateValueLabel(i);
}

static void loadDefaults() {
    const VoiceSettings s = voxDefaults();
    setSlider(S_PITCH, s.engine.pitch);
    setSlider(S_EXPR, s.engine.expression);
    setSlider(S_RATE, s.engine.rate);
    setSlider(S_HEAD, s.engine.headSize);
    setSlider(S_SPEED, (int)(s.speed * 1000 + 0.5));
    setSlider(S_ECHO, (int)(s.echoAmount * 100 + 0.5));
    setSlider(S_DELAY, (int)(s.echoDelayMs + 0.5));
    setSlider(S_LOWCUT, (int)(s.lowCutHz + 0.5));
    SendMessageW(g_style, CB_SETCURSEL, s.style == Style::Words ? 1 : 0, 0);
    SendMessageW(g_timing, BM_SETCHECK, s.gameTiming ? BST_CHECKED : BST_UNCHECKED, 0);
}

static VoiceSettings currentSettings() {
    VoiceSettings s = voxDefaults();
    auto pos = [](int i) { return (int)SendMessageW(g_sl[i], TBM_GETPOS, 0, 0); };
    s.engine.pitch = pos(S_PITCH);
    s.engine.expression = pos(S_EXPR);
    s.engine.rate = pos(S_RATE);
    s.engine.headSize = pos(S_HEAD);
    int sp = pos(S_SPEED);
    // Keep the exact 11025/12000 ratio when the slider sits on the preset value.
    if (sp != (int)(s.speed * 1000 + 0.5)) s.speed = sp / 1000.0;
    s.echoAmount = pos(S_ECHO) / 100.0;
    int d = pos(S_DELAY);
    if (d != (int)(s.echoDelayMs + 0.5)) s.echoDelayMs = d;
    s.lowCutHz = pos(S_LOWCUT);
    s.style = SendMessageW(g_style, CB_GETCURSEL, 0, 0) == 1 ? Style::Words : Style::Sentence;
    s.gameTiming = s.gamePitch = SendMessageW(g_timing, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return s;
}

static OutputOptions currentOutput() {
    OutputOptions o;
    o.sampleRate = SendMessageW(g_rate, CB_GETCURSEL, 0, 0) == 1 ? 44100 : 11025;
    o.eightBit = SendMessageW(g_8bit, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return o;
}

static FileFormat currentFormat() { return SendMessageW(g_format, CB_GETCURSEL, 0, 0) == 1 ? FileFormat::Mp3 : FileFormat::Wav; }

static std::wstring currentText() {
    int n = GetWindowTextLengthW(g_text);
    std::wstring t(n + 1, 0);
    GetWindowTextW(g_text, &t[0], n + 1);
    t.resize(n);
    for (auto &c : t) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    return t;
}

static void setBusy(bool b) {
    g_busy = b;
    BOOL en = !b && g_ready;
    EnableWindow(g_play, en);
    EnableWindow(g_save, en);
    EnableWindow(g_savewords, en);
}

static void submit(Action a, const std::wstring &path) {
    std::wstring t = currentText();
    bool blank = true;
    for (wchar_t c : t) if (!iswspace(c)) blank = false;
    if (blank) { setStatus(L"Type something to say first."); return; }
    Job j;
    j.action = a;
    j.vs = currentSettings();
    j.oo = currentOutput();
    j.fmt = currentFormat();
    j.text = t;
    j.path = path;
    EnterCriticalSection(&g_cs);
    g_jobs.push_back(j);
    LeaveCriticalSection(&g_cs);
    SetEvent(g_jobEvent);
    setBusy(true);
    setStatus(a == Action::SaveWords ? L"Rendering words..." : L"Rendering...");
}

static std::wstring pickSaveFile() {
    bool mp3 = currentFormat() == FileFormat::Mp3;
    wchar_t file[MAX_PATH];
    wcsncpy(file, suggestFileName(currentText()).c_str(), MAX_PATH - 1);
    file[MAX_PATH - 1] = 0;
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = g_wnd;
    of.lpstrFilter = mp3 ? L"MP3 audio (*.mp3)\0*.mp3\0" : L"WAV audio (*.wav)\0*.wav\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = mp3 ? L"mp3" : L"wav";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&of) ? std::wstring(file) : std::wstring();
}

static std::wstring pickFolder() {
    std::wstring r;
    IFileOpenDialog *d = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
        DWORD o;
        d->GetOptions(&o);
        d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        d->SetTitle(L"Folder for the word files");
        if (SUCCEEDED(d->Show(g_wnd))) {
            IShellItem *it;
            if (SUCCEEDED(d->GetResult(&it))) {
                wchar_t *p;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { r = p; CoTaskMemFree(p); }
                it->Release();
            }
        }
        d->Release();
    }
    return r;
}

static HWND mk(const wchar_t *cls, const wchar_t *txt, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_wnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void group(const wchar_t *t, int x, int y, int w, int h) { mk(L"BUTTON", t, BS_GROUPBOX, x, y, w, h, -1); }

static void buildUi() {
    const int W = 600;
    mk(L"STATIC", L"Text:", 0, 12, 10, 200, 18, -1);
    g_text = mk(L"EDIT", L"Attention, all personnel. The test is now complete.",
                ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 12, 30, W - 24, 70, IDC_TEXT, WS_EX_CLIENTEDGE);

    mk(L"STATIC", L"Style:", 0, 12, 114, 50, 20, -1);
    g_style = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 64, 110, 220, 200, IDC_STYLE);
    SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"Whole sentence");
    SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"Word by word (in-game)");
    mk(L"STATIC", L"Sound:", 0, 306, 114, 46, 20, -1);
    g_sound = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 354, 110, 140, 240, IDC_SOUND);
    for (auto &n : soundEffectNames()) SendMessageW(g_sound, CB_ADDSTRING, 0, (LPARAM)n.c_str());
    SendMessageW(g_sound, CB_SETCURSEL, 0, 0);
    mk(L"BUTTON", L"&Insert", WS_TABSTOP, 500, 109, 88, 26, IDC_INSERT);

    int y = 146;
    group(L"FlexTalk voice", 12, y, W - 24, 132);
    group(L"Effects", 12, y + 140, W - 24, 132);
    for (int i = 0; i < S_COUNT; i++) {
        int yy = i < 4 ? y + 20 + i * 27 : y + 160 + (i - 4) * 27;
        mk(L"STATIC", SLIDERS[i].label, 0, 26, yy + 4, 120, 20, IDC_LABEL0 + i);
        g_sl[i] = mk(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 150, yy, W - 260, 26, IDC_SLIDER0 + i);
        SendMessageW(g_sl[i], TBM_SETRANGE, TRUE, MAKELPARAM(SLIDERS[i].lo, SLIDERS[i].hi));
        g_val[i] = mk(L"STATIC", L"", SS_RIGHT, W - 104, yy + 4, 80, 20, IDC_VALUE0 + i);
    }
    y += 282;
    g_reset = mk(L"BUTTON", L"Reset to VOX defaults", WS_TABSTOP, 12, y, 180, 26, IDC_RESET);
    g_timing = mk(L"BUTTON", L"Match known words to the game (length and pitch)", BS_AUTOCHECKBOX | WS_TABSTOP, 210, y + 3,
                  370, 22, IDC_TIMING);

    y += 36;
    group(L"Output", 12, y, W - 24, 56);
    mk(L"STATIC", L"Sample rate:", 0, 26, y + 25, 80, 20, -1);
    g_rate = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 108, y + 21, 100, 200, IDC_RATE);
    SendMessageW(g_rate, CB_ADDSTRING, 0, (LPARAM)L"11025 Hz");
    SendMessageW(g_rate, CB_ADDSTRING, 0, (LPARAM)L"44100 Hz");
    SendMessageW(g_rate, CB_SETCURSEL, 0, 0);
    mk(L"STATIC", L"Format:", 0, 224, y + 25, 50, 20, -1);
    g_format = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 276, y + 21, 80, 200, IDC_FORMAT);
    SendMessageW(g_format, CB_ADDSTRING, 0, (LPARAM)L"WAV");
    SendMessageW(g_format, CB_ADDSTRING, 0, (LPARAM)L"MP3");
    SendMessageW(g_format, CB_SETCURSEL, 0, 0);
    g_8bit = mk(L"BUTTON", L"8-bit (as in the game files)", BS_AUTOCHECKBOX | WS_TABSTOP, 372, y + 23, 210, 22, IDC_8BIT);
    SendMessageW(g_8bit, BM_SETCHECK, BST_CHECKED, 0);

    y += 68;
    g_play = mk(L"BUTTON", L"&Speak", BS_DEFPUSHBUTTON | WS_TABSTOP, 12, y, 110, 30, IDC_PLAY);
    g_stop = mk(L"BUTTON", L"S&top", WS_TABSTOP, 130, y, 80, 30, IDC_STOP);
    g_save = mk(L"BUTTON", L"Save &as...", WS_TABSTOP, W - 12 - 260, y, 120, 30, IDC_SAVE);
    g_savewords = mk(L"BUTTON", L"Save &words...", WS_TABSTOP, W - 12 - 132, y, 132, 30, IDC_SAVEWORDS);
    y += 40;
    g_status = mk(L"STATIC", L"Starting the FlexTalk engine...", SS_LEFTNOWORDWRAP, 12, y, W - 24, 20, IDC_STATUS);
    loadDefaults();
    setBusy(false);
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_HSCROLL:
        for (int i = 0; i < S_COUNT; i++) if ((HWND)lp == g_sl[i]) updateValueLabel(i);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_RESET) loadDefaults();
        else if (id == IDC_SOUND && HIWORD(wp) == CBN_SELCHANGE) {
            // Preview the chosen sound effect.
            int i = (int)SendMessageW(g_sound, CB_GETCURSEL, 0, 0);
            std::vector<float> b;
            if (i >= 0 && i < (int)soundEffectNames().size() && soundEffect(soundEffectNames()[i], b)) playAudio(b, 11025);
        }
        else if (id == IDC_INSERT) {
            int i = (int)SendMessageW(g_sound, CB_GETCURSEL, 0, 0);
            if (i >= 0 && i < (int)soundEffectNames().size()) {
                std::wstring ins = soundEffectNames()[i] + L" ";
                DWORD a = 0, b = 0;
                SendMessageW(g_text, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
                int len = GetWindowTextLengthW(g_text);
                if (a > 0 && a <= (DWORD)len) {
                    std::wstring t(len + 1, 0);
                    GetWindowTextW(g_text, &t[0], len + 1);
                    if (!iswspace(t[a - 1])) ins = L" " + ins;
                }
                SendMessageW(g_text, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
                SetFocus(g_text);
            }
        }
        else if (id == IDC_PLAY && g_ready && !g_busy) submit(Action::Play, L"");
        else if (id == IDC_STOP) stopAudio();
        else if (id == IDC_SAVE && g_ready && !g_busy) {
            std::wstring p = pickSaveFile();
            if (!p.empty()) submit(Action::SaveFile, p);
        } else if (id == IDC_SAVEWORDS && g_ready && !g_busy) {
            std::wstring p = pickFolder();
            if (!p.empty()) submit(Action::SaveWords, p);
        }
        return 0;
    }
    case WM_APP_READY:
        g_ready = wp != 0;
        setStatus(g_ready ? L"Ready." : L"Engine failed to start: " + g_initErr);
        setBusy(false);
        return 0;
    case WM_APP_DONE: {
        JobResult *r = (JobResult *)lp;
        if (!r->ok) setStatus(L"Error: " + r->err);
        else if (r->action == Action::Play) {
            playAudio(r->audio, r->rate);
            wchar_t b[96];
            swprintf(b, 96, L"Playing %.2f s.", r->audio.size() / (double)r->rate);
            setStatus(b);
        } else if (r->action == Action::SaveFile) setStatus(L"Saved " + r->path);
        else setStatus(L"Saved " + std::to_wstring(r->words) + L" word files to " + r->path);
        delete r;
        setBusy(false);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_DESTROY:
        stopAudio();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static int runGui(HINSTANCE inst, int show) {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HDC dc = GetDC(nullptr);
    g_scale = GetDeviceCaps(dc, LOGPIXELSY) / 96.0;
    ReleaseDC(nullptr, dc);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"voxtalk";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);
    RECT rc{0, 0, S(600), S(632)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    g_wnd = CreateWindowExW(0, L"voxtalk", L"voxtalk", style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                            rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    buildUi();
    ShowWindow(g_wnd, show);

    InitializeCriticalSection(&g_cs);
    g_jobEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    DWORD tid;
    HANDLE th = CreateThread(nullptr, 0, workerMain, nullptr, 0, &tid);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    PostThreadMessageW(tid, WM_QUIT, 0, 0);
    WaitForSingleObject(th, 5000);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        // Command-line mode: borrow the parent console for output.
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            HANDLE out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN) SetStdHandle(STD_OUTPUT_HANDLE, out);
            cliOut(L"\n");
        }
        CoInitialize(nullptr);
        int rc = runCli(argc, argv);
        CoUninitialize();
        return rc;
    }
    return runGui(inst, show);
}
