#pragma once
#include <string>
#include <vector>

enum class FileFormat { Wav, Mp3 };

// Builds a RIFF/WAVE image. 8-bit unsigned PCM when eightBit and rate == 11025
// (the exact Half-Life format), otherwise 16-bit PCM.
std::vector<unsigned char> makeWav(const std::vector<float> &x, int rate, bool eightBit);
bool makeMp3(const std::vector<float> &x, int rate, std::vector<unsigned char> &out, std::wstring &err);
bool writeAudioFile(const std::wstring &path, FileFormat fmt, const std::vector<float> &x, int rate, bool eightBit,
                    std::wstring &err);
void playAudio(const std::vector<float> &x, int rate);
void stopAudio();
