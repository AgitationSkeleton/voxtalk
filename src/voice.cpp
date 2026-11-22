#include "voice.h"
#include "vox_timing.h"
#include "vox_pitch.h"
#include <windows.h>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <algorithm>
#include <map>

static const double SR = 11025.0;

// Loop filter of the Half-Life VOX echo, measured from the original files:
// a single feedback delay of 1510 samples (137 ms at 11025 Hz) with a gentle
// low-pass in the loop (taps at 1510, 1511, 1512, 1513 samples).
static const std::vector<float> VOX_ECHO_TAPS = {0.30f, 0.09f, 0.03f, 0.027f};

VoiceSettings voxDefaults() {
    VoiceSettings v;
    v.engine.pitch = 20;              // FlexTalk default is 27
    v.engine.expression = 55;         // FlexTalk default is 59
    v.engine.lowering = 0;            // FlexTalk default is 40
    v.speed = 11025.0 / 12000.0;      // slowed by 0.91875 (pitch and formants drop together)
    v.echoAmount = 1.0;
    v.echoDelayMs = 1510.0 / 11.025;  // 137 ms
    v.lowCutHz = 160;
    v.style = Style::Words;
    return v;
}

// ---------------------------------------------------------------------------
// Bundled VOX sound effects (original Half-Life files)
// ---------------------------------------------------------------------------
static std::wstring g_soundDir;
static std::map<std::wstring, dsp::Buf> g_soundCache;

const std::vector<std::wstring> &soundEffectNames() {
    static const std::vector<std::wstring> n = {L"bizwarn", L"bloop", L"buzwarn", L"dadeda",
                                                L"deeoo",   L"doop",  L"woop",    L"endgame"};
    return n;
}


static bool isSoundEffect(const std::wstring &w) {
    for (auto &n : soundEffectNames())
        if (!_wcsicmp(n.c_str(), w.c_str())) return true;
    return false;
}

static bool loadWav(const std::wstring &path, dsp::Buf &out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr), got = 0;
    std::vector<unsigned char> d(size);
    ReadFile(h, d.data(), size, &got, nullptr);
    CloseHandle(h);
    if (got != size || size < 12 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4)) return false;
    unsigned rate = 0, bits = 0, ch = 0;
    size_t p = 12;
    while (p + 8 <= size) {
        unsigned len;
        memcpy(&len, d.data() + p + 4, 4);
        const unsigned char *c = d.data() + p + 8;
        if (!memcmp(d.data() + p, "fmt ", 4) && len >= 16) {
            ch = c[2] | (c[3] << 8);
            memcpy(&rate, c + 4, 4);
            bits = c[14] | (c[15] << 8);
        } else if (!memcmp(d.data() + p, "data", 4) && ch) {
            size_t n = std::min<size_t>(len, size - (p + 8));
            out.clear();
            // 8-bit samples use the same scale as the 8-bit writer so they pass through unchanged.
            if (bits == 8) for (size_t i = 0; i < n; i += ch) out.push_back((c[i] - 128) / 127.0f);
            else if (bits == 16) for (size_t i = 0; i + 1 < n; i += 2 * ch) out.push_back((short)(c[i] | (c[i + 1] << 8)) / 32768.0f);
            else return false;
            if (rate == 22050) out = dsp::resample(out, 1, 2);
            else if (rate == 44100) out = dsp::resample(out, 1, 4);
            else if (rate != 11025) return false;
            return true;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

static SRWLOCK g_soundLock = SRWLOCK_INIT;  // used from the UI and render threads

void setSoundDirectory(const std::wstring &dir) {
    AcquireSRWLockExclusive(&g_soundLock);
    g_soundDir = dir;
    ReleaseSRWLockExclusive(&g_soundLock);
}

bool soundEffect(const std::wstring &name, std::vector<float> &out) {
    std::wstring key = name;
    for (auto &c : key) c = (wchar_t)towlower(c);
    AcquireSRWLockExclusive(&g_soundLock);
    auto it = g_soundCache.find(key);
    bool ok = true;
    if (it == g_soundCache.end()) {
        dsp::Buf b;
        if (g_soundDir.empty() || !loadWav(g_soundDir + L"\\" + key + L".wav", b)) ok = false;
        else it = g_soundCache.emplace(key, std::move(b)).first;
    }
    if (ok) out = it->second;
    ReleaseSRWLockExclusive(&g_soundLock);
    return ok;
}

// ---------------------------------------------------------------------------
// Text parsing: plain text plus Half-Life sentence syntax
//   word(s15 e80)  start/end the word at 15 % / 80 % of its length
//   (e90) word     a standalone group sets defaults for the following words
//   p = pitch %, v = volume %, t = time compression %
//   vox/word       directory prefixes are ignored
//   , ; :  -> short pause (_comma)   . ! ?  -> long pause (_period)
// ---------------------------------------------------------------------------
namespace {
struct WordParams { double s = 0, e = 100, p = 100, v = 100, t = 0; };
struct Item {
    enum Kind { Word, Sound, Pause } kind;
    std::wstring text;
    WordParams wp;
    double pause = 0;
    wchar_t punct = 0;  // original punctuation (sentence mode)
};
}  // namespace

static void applyModifiers(const std::wstring &grp, WordParams &wp) {
    size_t i = 0;
    while (i < grp.size()) {
        wchar_t k = (wchar_t)towlower(grp[i]);
        size_t j = i + 1;
        while (j < grp.size() && (iswdigit(grp[j]) || grp[j] == L'.')) j++;
        if (j > i + 1) {
            double v = _wtof(grp.substr(i + 1, j - i - 1).c_str());
            if (k == L's') wp.s = v;
            else if (k == L'e') wp.e = v;
            else if (k == L'p') wp.p = v;
            else if (k == L'v') wp.v = v;
            else if (k == L't') wp.t = v;
        }
        i = j > i + 1 ? j : i + 1;
    }
}

static std::vector<Item> parse(const std::wstring &text) {
    std::vector<Item> items;
    WordParams defaults;
    size_t i = 0, n = text.size();
    auto pauseFor = [](wchar_t c) -> double {
        if (c == L',' || c == L';' || c == L':') return 0.25;
        if (c == L'.' || c == L'!' || c == L'?') return 0.43;
        return 0;
    };
    while (i < n) {
        wchar_t c = text[i];
        if (iswspace(c)) { i++; continue; }
        if (c == L'(') {  // standalone modifier group -> new defaults
            size_t j = text.find(L')', i);
            if (j == std::wstring::npos) j = n;
            applyModifiers(text.substr(i + 1, j - i - 1), defaults);
            i = j + 1;
            continue;
        }
        if (pauseFor(c) > 0) {
            Item it{Item::Pause};
            it.pause = pauseFor(c);
            it.punct = c;
            items.push_back(it);
            i++;
            continue;
        }
        // word token
        size_t j = i;
        while (j < n && !iswspace(text[j]) && text[j] != L'(' && pauseFor(text[j]) == 0) j++;
        std::wstring w = text.substr(i, j - i);
        i = j;
        size_t slash = w.find_last_of(L"/\\");
        if (slash != std::wstring::npos) w = w.substr(slash + 1);
        WordParams wp = defaults;
        if (i < n && text[i] == L'(') {  // attached modifiers
            size_t k = text.find(L')', i);
            if (k == std::wstring::npos) k = n;
            applyModifiers(text.substr(i + 1, k - i - 1), wp);
            i = k + 1;
        }
        std::wstring clean;
        for (wchar_t ch : w) if (iswalnum(ch) || ch == L'\'' || ch == L'-' || ch == L'_') clean += ch;
        while (!clean.empty() && (clean.back() == L'-' || clean.back() == L'\'')) clean.pop_back();
        if (clean.empty()) continue;
        if (!_wcsicmp(clean.c_str(), L"_comma")) { Item it{Item::Pause}; it.pause = 0.25; it.punct = L','; items.push_back(it); continue; }
        if (!_wcsicmp(clean.c_str(), L"_period")) { Item it{Item::Pause}; it.pause = 0.43; it.punct = L'.'; items.push_back(it); continue; }
        Item it{isSoundEffect(clean) ? Item::Sound : Item::Word};
        if (it.kind == Item::Word) for (auto &ch : clean) if (ch == L'_') ch = L' ';
        it.text = clean;
        it.wp = wp;
        items.push_back(it);
    }
    return items;
}

// Apply Half-Life's per-word playback parameters to a rendered word.
static void applyWordParams(dsp::Buf &x, const WordParams &wp) {
    if (x.empty()) return;
    double s = std::max(0.0, std::min(100.0, wp.s)), e = std::max(s, std::min(100.0, wp.e));
    if (s > 0 || e < 100) {
        size_t a = (size_t)(x.size() * s / 100.0), b = (size_t)(x.size() * e / 100.0);
        x = dsp::Buf(x.begin() + a, x.begin() + std::max(a, b));
    }
    if (wp.t > 0 && wp.t < 100 && x.size() > 512) {
        // Time compression: drop evenly spaced short slices (cross-faded).
        const size_t slice = 220, fade = 32;
        double keep = 1.0 - wp.t / 100.0;
        dsp::Buf y;
        double acc = 0;
        bool skipped = false;
        for (size_t p = 0; p < x.size(); p += slice) {
            acc += keep;
            if (acc < 1.0 && p > 0) { skipped = true; continue; }
            acc -= 1.0;
            size_t end = std::min(x.size(), p + slice);
            size_t q = p;
            if (skipped && y.size() >= fade) {
                // overlap the start of this slice with the end of the output
                size_t base = y.size() - fade;
                for (size_t f = 0; f < fade && q < end; f++, q++) {
                    float g = (float)(f + 1) / (fade + 1);
                    y[base + f] = y[base + f] * (1 - g) + x[q] * g;
                }
            }
            for (; q < end; q++) y.push_back(x[q]);
            skipped = false;
        }
        x.swap(y);
    }
    if (wp.p > 0 && std::fabs(wp.p - 100) > 0.5) x = dsp::stretch(x, 100.0 / wp.p);
    if (std::fabs(wp.v - 100) > 0.5) for (auto &v : x) v *= (float)(wp.v / 100.0);
}

// Length of a word in Half-Life's own recordings, 0 if the game has no such word.
static size_t gameWordLength(const std::wstring &word) {
    std::string key;
    for (wchar_t c : word) {
        if (c == L' ') key += '_';
        else if (c < 128) key += (char)towlower(c);
        else return 0;
    }
    size_t lo = 0, hi = sizeof(VOX_TIMING) / sizeof(VOX_TIMING[0]);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(VOX_TIMING[mid].word, key.c_str());
        if (c == 0) return VOX_TIMING[mid].samples;
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return 0;
}

// Steer a rendered word's pitch to the contour Half-Life's own recording of that
// word has (quarter-by-quarter medians from vox_pitch.h), keeping FlexTalk's fine
// detail. Pitch/Expression settings still offset and scale the target.
static void applyGamePitch(dsp::Buf &x, const std::wstring &word, const SpeakerParams &sp) {
    std::string key;
    for (wchar_t c : word) {
        if (c == L' ') key += '_';
        else if (c < 128) key += (char)towlower(c);
        else return;
    }
    const VoxPitch *e = nullptr;
    size_t lo = 0, hi = sizeof(VOX_PITCH) / sizeof(VOX_PITCH[0]);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(VOX_PITCH[mid].word, key.c_str());
        if (c == 0) { e = &VOX_PITCH[mid]; break; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    if (!e) return;
    const int HOP = 64;
    std::vector<float> tr = dsp::pitchTrack(x, HOP, SR, 35, 250);
    long v0 = -1, v1 = -1;
    for (long k = 0; k < (long)tr.size(); k++) if (tr[k] > 0) { if (v0 < 0) v0 = k; v1 = k; }
    if (v0 < 0 || v1 - v0 < 6) return;
    // source medians per segment (semitones re 100 Hz)
    const int NS = (int)(sizeof(e->st) / sizeof(e->st[0]));
    double src[16], tgt[16];
    for (int q = 0; q < NS; q++) {
        std::vector<double> v;
        for (long k = v0; k <= v1; k++) {
            double u = (double)(k - v0) / (v1 - v0);
            if (tr[k] > 0 && u >= (double)q / NS && u <= (double)(q + 1) / NS) v.push_back(12 * std::log2(tr[k] / 100.0));
        }
        if (v.empty()) {  // segment too short to measure: borrow the previous one
            if (q == 0) return;
            src[q] = src[q - 1];
            continue;
        }
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        src[q] = v[v.size() / 2];
    }
    double mean = 0;
    for (int q = 0; q < NS; q++) { tgt[q] = e->st[q] / 5.0; mean += tgt[q] / NS; }
    double scale = std::max(0.0, sp.expression / 55.0), offset = (sp.pitch - 20) * 0.37;
    for (int q = 0; q < NS; q++) tgt[q] = mean + (tgt[q] - mean) * scale + offset;
    auto interp = [NS](const double *v, double u) {
        double pos = u * NS - 0.5;  // segment centres at (q + 0.5) / NS
        if (pos <= 0) return v[0];
        if (pos >= NS - 1) return v[NS - 1];
        int i = (int)pos;
        double f = pos - i;
        return v[i] + (v[i + 1] - v[i]) * f;
    };
    std::vector<float> ratio(tr.size(), 1.0f);
    for (long k = 0; k < (long)tr.size(); k++) {
        double u = std::max(0.0, std::min(1.0, (double)(k - v0) / (v1 - v0)));
        ratio[k] = (float)std::pow(2.0, (interp(tgt, u) - interp(src, u)) / 12.0);
    }
    x = dsp::psola(x, tr, HOP, SR, ratio);
}

static bool renderChunk(FlexEngine &eng, const VoiceSettings &vs, const std::wstring &text, bool wordMode,
                        dsp::Buf &out, std::wstring &err) {
    std::vector<short> pcm;
    // Half-Life's word recordings carry a rising-then-falling accent; FlexTalk
    // produces the same contour with an L*+H pitch accent escape.
    std::wstring spoken = wordMode ? L"\\!*L*+H " + text : text;
    if (!eng.speak(vs.engine, spoken, pcm, err)) return false;
    dsp::Buf x(pcm.size());
    for (size_t i = 0; i < pcm.size(); i++) x[i] = pcm[i] / 32768.0f;
    dsp::trim(x, -55, 32, 220);
    if (x.empty()) { out.clear(); return true; }
    if (std::fabs(vs.speed - 11025.0 / 12000.0) < 1e-6) x = dsp::resample(x, 160, 147);
    else if (vs.speed > 0.05) x = dsp::stretch(x, 1.0 / vs.speed);
    if (wordMode && vs.gamePitch) applyGamePitch(x, text, vs.engine);
    dsp::highpass(x, vs.lowCutHz, SR, 4);
    int delay = (int)std::lround(vs.echoDelayMs * SR / 1000.0);
    dsp::feedbackEcho(x, delay, VOX_ECHO_TAPS, vs.echoAmount);
    size_t target = (wordMode && vs.gameTiming) ? gameWordLength(text) : 0;
    if (target) {
        // Known word: give it exactly the length it has in the game, cutting
        // (with a short fade) or extending the echo tail as needed.
        dsp::trim(x, -60, 0, 0);
        if (x.size() > target) {
            x.resize(target);
            size_t fade = std::min<size_t>(110, target);
            for (size_t i = 0; i < fade; i++) x[target - fade + i] *= (float)(fade - i) / fade;
        } else {
            x.resize(target, 0.0f);
        }
    } else {
        // Otherwise cut the echo tail about 29 dB down, like most of the game's word files.
        dsp::trim(x, wordMode ? -29 : -60, 0, wordMode ? 0 : 110);
    }
    dsp::normalize(x, 0.95);
    out = std::move(x);
    return true;
}

std::vector<std::pair<std::wstring, std::wstring>> splitWords(const std::wstring &text) {
    std::vector<std::pair<std::wstring, std::wstring>> r;
    for (auto &it : parse(text)) {
        if (it.kind != Item::Word) continue;
        std::wstring stem;
        for (wchar_t c : it.text) {
            if (iswalnum(c)) stem += (wchar_t)towlower(c);
            else if (c == L' ' || c == L'-') stem += L'_';
        }
        r.push_back({it.text, stem});
    }
    return r;
}

std::wstring suggestFileName(const std::wstring &text, size_t maxLen) {
    std::wstring name;
    for (auto &it : parse(text)) {
        if (it.kind == Item::Pause) continue;
        std::wstring stem;
        for (wchar_t c : it.text) {
            if (iswalnum(c) && c < 128) stem += (wchar_t)towlower(c);
            else if (c == L' ' || c == L'-' || c == L'_') stem += L'_';
        }
        if (stem.empty()) continue;
        if (!name.empty() && name.size() + 1 + stem.size() > maxLen) break;
        if (name.empty() && stem.size() > maxLen) stem.resize(maxLen);
        if (!name.empty()) name += L'_';
        name += stem;
    }
    return name.empty() ? L"voxtalk" : name;
}

bool render(FlexEngine &eng, const VoiceSettings &vs, const OutputOptions &opts, const std::wstring &text,
            std::vector<float> &out, std::wstring &err) {
    out.clear();
    dsp::Buf all;
    std::vector<Item> items = parse(text);
    if (vs.style == Style::Sentence) {
        // Natural FlexTalk phrasing for runs of words; sound effects in between.
        std::wstring run;
        auto flush = [&]() -> bool {
            bool any = false;
            for (wchar_t c : run) if (iswalnum(c)) any = true;
            if (any) {
                dsp::Buf b;
                if (!renderChunk(eng, vs, run, false, b, err)) return false;
                all.insert(all.end(), b.begin(), b.end());
            }
            run.clear();
            return true;
        };
        for (auto &it : items) {
            if (it.kind == Item::Word) { if (!run.empty()) run += L' '; run += it.text; }
            else if (it.kind == Item::Pause) run += it.punct;
            else {
                if (!flush()) return false;
                dsp::Buf b;
                if (!soundEffect(it.text, b)) { err = L"Sound effect missing: " + it.text; return false; }
                all.insert(all.end(), b.begin(), b.end());
            }
        }
        if (!flush()) return false;
    } else {
        // In-game style: every word is a separate recording, played back to back,
        // with Half-Life's sentence modifiers applied.
        for (auto &it : items) {
            dsp::Buf b;
            if (it.kind == Item::Pause) {
                all.insert(all.end(), (size_t)(it.pause * SR), 0.0f);
                continue;
            }
            if (it.kind == Item::Sound) {
                if (!soundEffect(it.text, b)) { err = L"Sound effect missing: " + it.text; return false; }
            } else if (!renderChunk(eng, vs, it.text, true, b, err)) {
                return false;
            }
            applyWordParams(b, it.wp);
            all.insert(all.end(), b.begin(), b.end());
        }
    }
    if (all.empty()) { err = L"Nothing to say."; return false; }
    for (auto &v : all) v = std::max(-128.0f / 127.0f, std::min(1.0f, v));
    if (opts.eightBit) dsp::quantize8(all, 777);
    if (opts.sampleRate == 44100) all = dsp::resample(all, 4, 1);
    out = std::move(all);
    return true;
}
