// FlexTalk engine host: loads the 1997-era SAPI engine in-process without any
// installation, redirecting its registry access into a private app hive.
#pragma once
#include <string>
#include <vector>

struct SpeakerParams {
    int pitch = 27, expression = 59, rate = 33, headSize = 46, prominences = 17, lowering = 40, volume = 100;
};

class FlexEngine {
public:
    // root: directory holding EngineDLLs\, EngineData\, UserData\.
    // stateDir: writable directory for the private registry hive.
    bool init(const std::wstring &root, const std::wstring &stateDir, std::wstring &err);
    // Synthesize text to 16-bit mono PCM at 11025 Hz.
    bool speak(const SpeakerParams &p, const std::wstring &text, std::vector<short> &pcm, std::wstring &err);
    void shutdown();
    ~FlexEngine() { shutdown(); }

private:
    void writeConfig(const SpeakerParams &p);
    void *factory_ = nullptr;  // IClassFactory*
    void *enum_ = nullptr;     // ITTSEnumW*
    unsigned char mode_[16]{};
    std::wstring root_, hivePath_;
};
