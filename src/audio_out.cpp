#include "audio_out.h"
#include <windows.h>
#include <mmsystem.h>
#include <lame/lame.h>
#include <cmath>
#include <cstring>

static void put32(std::vector<unsigned char> &b, unsigned v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 255); }
static void put16(std::vector<unsigned char> &b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }

static short toS16(float v) {
    float s = std::round(v * 32767.0f);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return (short)s;
}

std::vector<unsigned char> makeWav(const std::vector<float> &x, int rate, bool eightBit) {
    bool u8 = eightBit && rate == 11025;
    unsigned bps = u8 ? 1 : 2;
    unsigned dataLen = (unsigned)x.size() * bps;
    std::vector<unsigned char> b;
    b.reserve(44 + dataLen + 1);
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    put32(b, 36 + dataLen + (dataLen & 1));
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(b, 16);
    put16(b, 1);
    put16(b, 1);
    put32(b, rate);
    put32(b, rate * bps);
    put16(b, bps);
    put16(b, bps * 8);
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    put32(b, dataLen);
    for (float v : x) {
        if (u8) {
            float q = std::round(v * 127.0f) + 128.0f;
            if (q < 0) q = 0;
            if (q > 255) q = 255;
            b.push_back((unsigned char)q);
        } else {
            put16(b, (unsigned short)toS16(v));
        }
    }
    if (dataLen & 1) b.push_back(0);
    return b;
}

bool makeMp3(const std::vector<float> &x, int rate, std::vector<unsigned char> &out, std::wstring &err) {
    lame_global_flags *gf = lame_init();
    if (!gf) { err = L"MP3 encoder initialisation failed."; return false; }
    lame_set_in_samplerate(gf, rate);
    lame_set_out_samplerate(gf, rate);
    lame_set_num_channels(gf, 1);
    lame_set_mode(gf, MONO);
    lame_set_brate(gf, rate <= 11025 ? 32 : 128);
    lame_set_quality(gf, 2);
    lame_set_write_id3tag_automatic(gf, 0);
    if (lame_init_params(gf) < 0) { lame_close(gf); err = L"MP3 encoder rejected the settings."; return false; }
    std::vector<short> pcm(x.size());
    for (size_t i = 0; i < x.size(); i++) pcm[i] = toS16(x[i]);
    std::vector<unsigned char> buf((size_t)(1.25 * pcm.size()) + 7200);
    int n = lame_encode_buffer(gf, pcm.data(), nullptr, (int)pcm.size(), buf.data(), (int)buf.size());
    if (n < 0) { lame_close(gf); err = L"MP3 encoding failed."; return false; }
    out.assign(buf.begin(), buf.begin() + n);
    unsigned char tail[7200];
    int m = lame_encode_flush(gf, tail, sizeof tail);
    if (m > 0) out.insert(out.end(), tail, tail + m);
    lame_close(gf);
    return true;
}

bool writeAudioFile(const std::wstring &path, FileFormat fmt, const std::vector<float> &x, int rate, bool eightBit,
                    std::wstring &err) {
    std::vector<unsigned char> data;
    if (fmt == FileFormat::Mp3) {
        if (!makeMp3(x, rate, data, err)) return false;
    } else {
        data = makeWav(x, rate, eightBit);
    }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = L"Cannot write " + path; return false; }
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    if (!ok || wr != data.size()) { err = L"Write failed: " + path; return false; }
    return true;
}

static std::vector<unsigned char> g_playing;

void playAudio(const std::vector<float> &x, int rate) {
    PlaySoundW(nullptr, nullptr, 0);
    g_playing = makeWav(x, rate, false);
    PlaySoundW((LPCWSTR)g_playing.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void stopAudio() { PlaySoundW(nullptr, nullptr, 0); }
