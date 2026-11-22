// Engine payload: FlexTalk DLLs + voice data, LZMS-compressed into the exe's
// resources and unpacked once into %LOCALAPPDATA%\voxtalk\engine-<id>.
#pragma once
#include <string>

// Returns the directory containing EngineDLLs\, EngineData\, UserData\.
bool ensureEngineFiles(std::wstring &engineDir, std::wstring &stateDir, std::wstring &err);
