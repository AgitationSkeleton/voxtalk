// VOX settings and the render pipeline (engine + post-processing).
#pragma once
#include "engine.h"
#include "dsp.h"
#include <string>
#include <vector>

enum class Style { Sentence = 0, Words = 1 };

struct VoiceSettings {
    SpeakerParams engine;
    double speed = 1.0;      // playback speed factor (<1 = slower and lower, like a slowed tape)
    double echoAmount = 0;   // 1.0 = the echo measured from Half-Life's VOX files
    double echoDelayMs = 137.0;
    double lowCutHz = 0;
    Style style = Style::Sentence;
};

// Settings measured from Half-Life's sound/vox files.
VoiceSettings voxDefaults();

struct OutputOptions {
    int sampleRate = 11025;  // 11025 or 44100
    bool eightBit = true;    // Half-Life style 8-bit quantisation
};

// Render text to a float buffer at opts.sampleRate.
bool render(FlexEngine &eng, const VoiceSettings &vs, const OutputOptions &opts, const std::wstring &text,
            std::vector<float> &out, std::wstring &err);

// Split text into words for one-file-per-word export. Returns (spoken text, file stem).
std::vector<std::pair<std::wstring, std::wstring>> splitWords(const std::wstring &text);
