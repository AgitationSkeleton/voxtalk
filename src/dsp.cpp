#include "dsp.h"
#include <cmath>
#include <algorithm>

namespace dsp {
static const double PI = 3.14159265358979323846;

static double besselI0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 40; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
    return s;
}

// Windowed-sinc interpolation at fractional input positions.
static Buf interpolate(const Buf &x, size_t nout, double step, double cutoff) {
    const int half = 24;
    const double beta = 8.0;
    const double scale = std::min(1.0, cutoff);
    const int halfw = (int)std::ceil(half / scale);
    const double i0b = besselI0(beta);
    const int TAB = 4096;
    std::vector<double> win(TAB + 2);
    for (int i = 0; i <= TAB + 1; i++) {
        double u = std::min(1.0, (double)i / TAB);
        win[i] = besselI0(beta * std::sqrt(std::max(0.0, 1 - u * u))) / i0b;
    }
    Buf y(nout);
    for (size_t n = 0; n < nout; n++) {
        double t = n * step;
        long c = (long)std::floor(t);
        double acc = 0;
        for (long i = c - halfw + 1; i <= c + halfw; i++) {
            if (i < 0 || i >= (long)x.size()) continue;
            double d = t - i;
            double u = std::fabs(d) / halfw;
            if (u >= 1) continue;
            double fi = u * TAB;
            int k = (int)fi;
            double w = win[k] + (win[k + 1] - win[k]) * (fi - k);
            double a = scale * d;
            double s = std::fabs(a) < 1e-9 ? scale : scale * std::sin(PI * a) / (PI * a);
            acc += x[i] * s * w;
        }
        y[n] = (float)acc;
    }
    return y;
}

Buf resample(const Buf &x, int up, int down) {
    if (up == down || x.empty()) return x;
    size_t nout = (size_t)((double)x.size() * up / down);
    double ratio = (double)up / down;
    return interpolate(x, nout, 1.0 / ratio, std::min(1.0, ratio) * 0.97);
}

Buf stretch(const Buf &x, double factor) {
    if (std::fabs(factor - 1.0) < 1e-6 || x.empty()) return x;
    size_t nout = (size_t)(x.size() * factor);
    return interpolate(x, nout, 1.0 / factor, std::min(1.0, factor) * 0.97);
}

struct Biquad {
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    float run(float in) {
        double out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return (float)out;
    }
};

static Biquad hpSection(double fc, double sr, double q) {
    double w = 2 * PI * fc / sr, cw = std::cos(w), al = std::sin(w) / (2 * q);
    double a0 = 1 + al;
    Biquad b;
    b.b0 = (1 + cw) / 2 / a0; b.b1 = -(1 + cw) / a0; b.b2 = (1 + cw) / 2 / a0;
    b.a1 = -2 * cw / a0; b.a2 = (1 - al) / a0;
    return b;
}

void highpass(Buf &x, double fc, double sr, int order) {
    if (fc <= 0) return;
    std::vector<Biquad> secs;
    if (order >= 4) { secs.push_back(hpSection(fc, sr, 0.5412)); secs.push_back(hpSection(fc, sr, 1.3066)); }
    else secs.push_back(hpSection(fc, sr, 0.7071));
    for (auto &s : secs) for (auto &v : x) v = s.run(v);
}

void feedbackEcho(Buf &x, int delay, const std::vector<float> &taps, double amount) {
    if (amount <= 0 || delay <= 0 || taps.empty()) return;
    double g = 0;
    for (float t : taps) g += std::fabs(t) * amount;
    // Number of repeats until the loop has decayed by ~60 dB.
    int repeats = g < 0.999 ? (int)std::ceil(-60.0 / (20 * std::log10(std::max(g, 1e-3)))) : 40;
    repeats = std::min(repeats, 40);
    size_t n0 = x.size();
    x.resize(n0 + (size_t)(delay + taps.size()) * repeats, 0.0f);
    for (size_t n = delay; n < x.size(); n++) {
        double acc = 0;
        for (size_t k = 0; k < taps.size(); k++) {
            if (n >= delay + k) acc += taps[k] * x[n - delay - k];
        }
        x[n] += (float)(amount * acc);
    }
}

static unsigned xs(unsigned &s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static float urand(unsigned &s) { return (xs(s) >> 8) * (1.0f / 16777216.0f); }

void normalize(Buf &x, double peak) {
    float m = 0;
    for (float v : x) m = std::max(m, std::fabs(v));
    if (m < 1e-9f) return;
    float g = (float)(peak / m);
    for (auto &v : x) v *= g;
}

void trim(Buf &x, double thresholdDb, int padStart, int padEnd) {
    if (x.empty()) return;
    // short-window envelope
    const int W = 64;
    float peak = 0;
    for (float v : x) peak = std::max(peak, std::fabs(v));
    if (peak <= 0) { x.clear(); return; }
    float thr = (float)(peak * std::pow(10, thresholdDb / 20));
    long first = -1, last = -1;
    double acc = 0;
    std::vector<float> env(x.size());
    for (size_t i = 0; i < x.size(); i++) {
        acc += std::fabs(x[i]);
        if (i >= W) acc -= std::fabs(x[i - W]);
        env[i] = (float)(acc / W);
    }
    for (size_t i = 0; i < x.size(); i++) if (env[i] > thr) { first = (long)i - W; break; }
    for (long i = (long)x.size() - 1; i >= 0; i--) if (env[i] > thr) { last = i; break; }
    if (first < 0) first = 0;
    if (last < 0) { x.clear(); return; }
    long a = std::max(0L, first - padStart);
    long b = std::min((long)x.size(), last + 1 + padEnd);
    x = Buf(x.begin() + a, x.begin() + b);
}

void quantize8(Buf &x, unsigned seed) {
    unsigned s = seed ? seed : 12345u;
    for (auto &v : x) {
        float d = (urand(s) - urand(s)) * 0.5f;
        float q = std::round(v * 127.0f + d);
        q = std::max(-128.0f, std::min(127.0f, q));
        v = q / 127.0f;
    }
}
}  // namespace dsp
