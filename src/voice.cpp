#include "voice.h"
#include <cmath>
#include <cwctype>
#include <algorithm>

static const double SR = 11025.0;

// Loop filter of the Half-Life VOX echo, measured from the original files:
// a single feedback delay of 1510 samples (137 ms at 11025 Hz) with a gentle
// low-pass in the loop (taps at 1510, 1511, 1512, 1513 samples).
static const std::vector<float> VOX_ECHO_TAPS = {0.30f, 0.09f, 0.03f, 0.027f};

VoiceSettings voxDefaults() {
    VoiceSettings v;
    v.engine.pitch = 19;              // FlexTalk default is 27
    v.engine.expression = 75;         // FlexTalk default is 59
    v.engine.lowering = 0;            // FlexTalk default is 40
    v.speed = 11025.0 / 12000.0;      // slowed by 0.91875 (pitch and formants drop together)
    v.echoAmount = 1.0;
    v.echoDelayMs = 1510.0 / 11.025;  // 137 ms
    v.lowCutHz = 160;
    v.style = Style::Words;
    return v;
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
    dsp::highpass(x, vs.lowCutHz, SR, 4);
    int delay = (int)std::lround(vs.echoDelayMs * SR / 1000.0);
    dsp::feedbackEcho(x, delay, VOX_ECHO_TAPS, vs.echoAmount);
    // Half-Life's word files are cut about 29 dB down the echo tail.
    dsp::trim(x, wordMode ? -29 : -60, 0, wordMode ? 0 : 110);
    dsp::normalize(x, 0.95);
    out = std::move(x);
    return true;
}

std::vector<std::pair<std::wstring, std::wstring>> splitWords(const std::wstring &text) {
    std::vector<std::pair<std::wstring, std::wstring>> r;
    std::wstring tok;
    auto flush = [&]() {
        std::wstring spoken, stem;
        for (wchar_t c : tok) {
            if (std::iswalnum(c) || c == L'\'' || c == L'-') spoken += c;
        }
        while (!spoken.empty() && (spoken.back() == L'-' || spoken.back() == L'\'')) spoken.pop_back();
        for (wchar_t c : spoken) {
            if (std::iswalnum(c)) stem += (wchar_t)std::towlower(c);
            else if (c == L'-') stem += L'_';
        }
        if (!spoken.empty()) r.push_back({spoken, stem});
        tok.clear();
    };
    for (wchar_t c : text) {
        if (std::iswspace(c)) flush();
        else tok += c;
    }
    flush();
    return r;
}

bool render(FlexEngine &eng, const VoiceSettings &vs, const OutputOptions &opts, const std::wstring &text,
            std::vector<float> &out, std::wstring &err) {
    out.clear();
    dsp::Buf all;
    if (vs.style == Style::Sentence) {
        if (!renderChunk(eng, vs, text, false, all, err)) return false;
    } else {
        // In-game style: every word is spoken on its own, as Half-Life's VOX
        // concatenates individual word recordings. Punctuation becomes the
        // same pauses as the game's _comma / _period files.
        std::wstring tok;
        auto emitPause = [&](wchar_t p) {
            double sec = 0;
            if (p == L',' || p == L';' || p == L':') sec = 0.25;
            else if (p == L'.' || p == L'!' || p == L'?') sec = 0.43;
            if (sec > 0) all.insert(all.end(), (size_t)(sec * SR), 0.0f);
        };
        auto flush = [&]() -> bool {
            if (tok.empty()) return true;
            wchar_t last = tok.back();
            std::wstring w;
            for (wchar_t c : tok) if (std::iswalnum(c) || c == L'\'' || c == L'-') w += c;
            tok.clear();
            if (!w.empty()) {
                dsp::Buf b;
                if (!renderChunk(eng, vs, w, true, b, err)) return false;
                all.insert(all.end(), b.begin(), b.end());
            }
            emitPause(last);
            return true;
        };
        for (wchar_t c : text) {
            if (std::iswspace(c)) { if (!flush()) return false; }
            else tok += c;
        }
        if (!flush()) return false;
    }
    if (all.empty()) { err = L"Nothing to say."; return false; }
    if (opts.eightBit) dsp::quantize8(all, 777);
    if (opts.sampleRate == 44100) all = dsp::resample(all, 4, 1);
    out = std::move(all);
    return true;
}
