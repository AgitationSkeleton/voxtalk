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
        float g = v * 127.0f;
        if (std::fabs(g - std::round(g)) < 1e-3f) {  // already 8-bit (e.g. original game sounds)
            v = std::max(-128.0f, std::min(127.0f, std::round(g))) / 127.0f;
            continue;
        }
        float d = (urand(s) - urand(s)) * 0.5f;
        float q = std::round(v * 127.0f + d);
        q = std::max(-128.0f, std::min(127.0f, q));
        v = q / 127.0f;
    }
}

std::vector<float> pitchTrack(const Buf &x, int hop, double sr, double fmin, double fmax) {
    const int minLag = (int)(sr / fmax), maxLag = (int)(sr / fmin) + 1;
    const int W = maxLag * 2;
    size_t frames = x.size() / hop + 1;
    std::vector<float> f0(frames, 0.0f);
    // energy gate relative to the loudest frame
    std::vector<double> en(frames, 0.0);
    double emax = 0;
    for (size_t k = 0; k < frames; k++) {
        long c = (long)(k * hop);
        double e = 0;
        for (long n = c - W / 2; n < c + W / 2; n++)
            if (n >= 0 && n < (long)x.size()) e += (double)x[n] * x[n];
        en[k] = e;
        emax = std::max(emax, e);
    }
    std::vector<double> r(maxLag + 2);
    for (size_t k = 0; k < frames; k++) {
        if (en[k] < emax * 0.003) continue;
        long c = (long)(k * hop), a = c - W / 2;
        double best = 0;
        for (int l = minLag; l <= maxLag; l++) {
            double num = 0, e1 = 0, e2 = 0;
            for (int n = 0; n < W - l; n++) {
                long i = a + n, j = a + n + l;
                if (i < 0 || j >= (long)x.size()) continue;
                num += (double)x[i] * x[j];
                e1 += (double)x[i] * x[i];
                e2 += (double)x[j] * x[j];
            }
            r[l] = (e1 > 0 && e2 > 0) ? num / std::sqrt(e1 * e2) : 0;
            best = std::max(best, r[l]);
        }
        if (best < 0.55) continue;
        // first local peak close to the best (avoids octave-down errors)
        int pick = -1;
        for (int l = minLag + 1; l < maxLag; l++)
            if (r[l] >= r[l - 1] && r[l] >= r[l + 1] && r[l] >= 0.88 * best) { pick = l; break; }
        if (pick < 0) continue;
        double a0 = r[pick - 1], b0 = r[pick], c0 = r[pick + 1];
        double den = a0 - 2 * b0 + c0, off = std::fabs(den) > 1e-12 ? 0.5 * (a0 - c0) / den : 0;
        f0[k] = (float)(sr / (pick + std::max(-0.5, std::min(0.5, off))));
    }
    // median-of-5 smoothing of voiced runs
    std::vector<float> sm = f0;
    for (size_t k = 0; k < frames; k++) {
        if (f0[k] <= 0) continue;
        float v[5]; int n = 0;
        for (long j = (long)k - 2; j <= (long)k + 2; j++)
            if (j >= 0 && j < (long)frames && f0[j] > 0) v[n++] = f0[j];
        std::sort(v, v + n);
        sm[k] = v[n / 2];
    }
    // bridge short dropouts inside voiced speech (up to 4 frames) so a vowel is never split
    for (size_t k = 1; k < frames; k++) {
        if (sm[k] > 0 || sm[k - 1] <= 0) continue;
        size_t j = k;
        while (j < frames && sm[j] <= 0) j++;
        if (j < frames && j - k <= 4) {
            for (size_t i = k; i < j; i++) {
                float f = (float)(i - k + 1) / (j - k + 1);
                sm[i] = sm[k - 1] * (1 - f) + sm[j] * f;
            }
        }
        k = j;
    }
    return sm;
}

Buf psola(const Buf &x, const std::vector<float> &track, int hop, double sr, const std::vector<float> &ratio) {
    const long N = (long)x.size();
    auto periodAt = [&](long n) -> double {
        long k = std::max(0L, std::min((long)track.size() - 1, n / hop));
        return track[k] > 0 ? sr / track[k] : 0.0;
    };
    auto ratioAt = [&](long n) -> double {
        long k = std::max(0L, std::min((long)ratio.size() - 1, n / hop));
        return ratio[k];
    };
    // 1. analysis pitch marks on each glottal pulse: peaks of a low-passed copy,
    //    which has one dominant peak per period (keeps marks on the same phase)
    Buf lp = x;
    {
        double w = 2 * 3.14159265358979 * 900.0 / sr, cw = std::cos(w), al = std::sin(w) / (2 * 0.7071), a0 = 1 + al;
        double b0 = (1 - cw) / 2 / a0, b1 = (1 - cw) / a0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
        double z1 = 0, z2 = 0;
        for (auto &v : lp) { double o = b0 * v + z1; z1 = b1 * v - a1 * o + z2; z2 = b0 * v - a2 * o; v = (float)o; }
        // zero-phase: run backwards too
        z1 = z2 = 0;
        for (long i = N - 1; i >= 0; i--) { double v = lp[i], o = b0 * v + z1; z1 = b1 * v - a1 * o + z2; z2 = b0 * v - a2 * o; lp[i] = (float)o; }
    }
    std::vector<long> marks;
    long n = 0;
    while (n < N) {
        double T = periodAt(n);
        if (T <= 0) { n += hop; continue; }
        if (marks.empty() || n - marks.back() > 2 * T) {  // start of a voiced run
            long best = n;
            for (long i = n; i < std::min(N, n + (long)T); i++) if (lp[i] > lp[best]) best = i;
            marks.push_back(best);
            n = best + (long)T;
            continue;
        }
        long lo = std::max(0L, n - (long)(T * 0.2)), hi = std::min(N - 1, n + (long)(T * 0.2));
        long best = lo;
        for (long i = lo; i <= hi; i++) if (lp[i] > lp[best]) best = i;
        marks.push_back(best);
        n = best + (long)std::max(8.0, T);
    }
    if (marks.size() < 3) return x;

    // 2. overlap-add pitch-synchronous grains at the new spacing. Hann grains two
    //    periods long placed every T/r sum to r, so each grain is scaled by 1/r.
    Buf out(N, 0.0f), mask(N, 0.0f);
    size_t m = 0;
    auto localT = [&](size_t i) {
        double T = periodAt(marks[i]);
        if (T <= 0 && i > 0) T = (double)(marks[i] - marks[i - 1]);
        return std::max(8.0, T);
    };
    while (m < marks.size()) {
        // a voiced run: consecutive marks closer than twice the local period
        size_t e = m;
        while (e + 1 < marks.size() && marks[e + 1] - marks[e] < 2 * localT(e) + 2) e++;
        if (e - m >= 2) {
            long r0 = std::max(0L, marks[m] - (long)localT(m)), r1 = std::min(N - 1, marks[e] + (long)localT(e));
            for (long i = r0; i <= r1; i++) mask[i] = 1.0f;
            double ts = (double)marks[m];
            while (ts <= marks[e]) {
                size_t k = m;  // nearest analysis mark
                while (k < e && std::fabs(marks[k + 1] - ts) < std::fabs(marks[k] - ts)) k++;
                long mk = marks[k];
                double Tn = k < e ? marks[k + 1] - mk : 1e9, Tp = k > m ? mk - marks[k - 1] : 1e9;
                double Ta = std::min(Tn, Tp);  // shorter neighbour period: no oversized grains
                double rr = std::max(0.5, std::min(2.0, ratioAt((long)ts)));
                long L = (long)Ta;
                float gain = (float)(1.0 / rr);
                for (long i = -L; i <= L; i++) {
                    long src = mk + i, dst = (long)std::lround(ts) + i;
                    if (src < 0 || src >= N || dst < 0 || dst >= N) continue;
                    float w = 0.5f * (1.0f + (float)std::cos(3.14159265358979 * i / (L + 1)));
                    out[dst] += x[src] * w * gain;
                }
                ts += Ta / rr;
            }
        }
        m = e + 1;
    }
    // 3. voiced runs use the grains, everything else the original; ~6 ms cross-fades
    const long S = 64;
    Buf ms(N, 0.0f);
    double acc = 0;
    for (long i = 0; i < N + S / 2; i++) {
        if (i < N) acc += mask[i];
        if (i - S >= 0) acc -= mask[i - S];
        long c = i - S / 2;
        if (c >= 0 && c < N) ms[c] = (float)(acc / S);
    }
    Buf y(N);
    for (long i = 0; i < N; i++) y[i] = out[i] * ms[i] + x[i] * (1 - ms[i]);
    return y;
}
}  // namespace dsp
