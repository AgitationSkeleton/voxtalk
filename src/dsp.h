#pragma once
#include <vector>

namespace dsp {
using Buf = std::vector<float>;

// Band-limited rational resampler (output length = in * up / down).
Buf resample(const Buf &x, int up, int down);
// Arbitrary-ratio resampler (factor > 1 lengthens the signal).
Buf stretch(const Buf &x, double factor);

void highpass(Buf &x, double fc, double sr, int order);   // Butterworth, order 2 or 4
// Feedback echo y[n] = x[n] + amount * sum_k taps[k] * y[n - delay - k].
// Appends enough tail for the repeats to die away.
void feedbackEcho(Buf &x, int delay, const std::vector<float> &taps, double amount);
void normalize(Buf &x, double peak);
// Remove leading/trailing material quieter than thresholdDb below the peak.
void trim(Buf &x, double thresholdDb, int padStart, int padEnd);
// Quantise to 8-bit (with TPDF dither) and back, emulating Half-Life's 8-bit files.
void quantize8(Buf &x, unsigned seed);
}  // namespace dsp
