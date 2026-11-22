#include "pack.h"
#include <windows.h>
#include <shlobj.h>
#include <vector>
#include <cstring>
#include <cstdio>

// Minimal Compression API declarations (cabinet.dll, Windows 8+), loaded at runtime.
typedef void *DECOMPRESSOR_HANDLE;
typedef BOOL(WINAPI *PFN_CreateDecompressor)(DWORD, void *, DECOMPRESSOR_HANDLE *);
typedef BOOL(WINAPI *PFN_Decompress)(DECOMPRESSOR_HANDLE, const void *, SIZE_T, void *, SIZE_T, SIZE_T *);
typedef BOOL(WINAPI *PFN_CloseDecompressor)(DECOMPRESSOR_HANDLE);
static const DWORD COMPRESS_ALGORITHM_LZMS_ = 5;
static const DWORD COMPRESS_RAW_ = 1 << 29;

static bool mkdirs(const std::wstring &p) {
    if (CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    size_t s = p.find_last_of(L"\\/");
    if (s == std::wstring::npos) return false;
    if (!mkdirs(p.substr(0, s))) return false;
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool exists(const std::wstring &p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void removeTree(const std::wstring &dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// Delete leftover private registry hives from earlier runs that are no longer in use.
static void sweepStaleHives(const std::wstring &stateDir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((stateDir + L"\\regoverlay-*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do DeleteFileW((stateDir + L"\\" + fd.cFileName).c_str());  // fails harmlessly while in use
    while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool ensureEngineFiles(std::wstring &engineDir, std::wstring &stateDir, std::wstring &err) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(101), (LPCWSTR)RT_RCDATA);
    if (!r) { err = L"Engine payload missing from executable."; return false; }
    DWORD sz = SizeofResource(nullptr, r);
    const unsigned char *pak = (const unsigned char *)LockResource(LoadResource(nullptr, r));
    if (!pak || sz < 12 || memcmp(pak, "VXPK1\0\0\0", 8)) { err = L"Engine payload is damaged."; return false; }

    unsigned long long id = 1469598103934665603ULL;
    for (DWORD i = 0; i < sz; i++) { id ^= pak[i]; id *= 1099511628211ULL; }

    wchar_t *la = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &la))) { err = L"No LocalAppData folder."; return false; }
    std::wstring base = std::wstring(la) + L"\\voxtalk";
    CoTaskMemFree(la);
    stateDir = base;
    wchar_t idw[32];
    swprintf(idw, 32, L"%016llx", id);
    engineDir = base + L"\\engine-" + idw;
    if (!mkdirs(base)) { err = L"Cannot create " + base; return false; }
    sweepStaleHives(base);
    if (exists(engineDir + L"\\.complete")) return true;

    // Remove older engine versions, then unpack into a staging folder.
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((base + L"\\engine-*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                // Only remove a version no other instance is using: renaming the
                // folder fails while any of its files are open.
                std::wstring old = base + L"\\" + fd.cFileName;
                std::wstring trash = base + L"\\old-" + fd.cFileName;
                if (MoveFileW(old.c_str(), trash.c_str())) removeTree(trash);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    HMODULE cab = LoadLibraryW(L"cabinet.dll");
    auto pCreate = cab ? (PFN_CreateDecompressor)GetProcAddress(cab, "CreateDecompressor") : nullptr;
    auto pDecomp = cab ? (PFN_Decompress)GetProcAddress(cab, "Decompress") : nullptr;
    auto pClose = cab ? (PFN_CloseDecompressor)GetProcAddress(cab, "CloseDecompressor") : nullptr;
    if (!pCreate || !pDecomp || !pClose) { err = L"Windows Compression API unavailable (Windows 8 or later required)."; return false; }

    std::wstring stage = engineDir + L".partial";
    removeTree(stage);
    if (!mkdirs(stage)) { err = L"Cannot create " + stage; return false; }
    DWORD off = 8;
    auto rd32 = [&](DWORD &v) { if (off + 4 > sz) return false; memcpy(&v, pak + off, 4); off += 4; return true; };
    DWORD count;
    if (!rd32(count)) { err = L"Engine payload is damaged."; return false; }
    bool ok = true;
    for (DWORD i = 0; i < count && ok; i++) {
        DWORD plen, orig, comp;
        if (!rd32(plen) || off + plen > sz) { ok = false; break; }
        std::string rel((const char *)pak + off, plen);
        off += plen;
        if (!rd32(orig) || !rd32(comp) || off + comp > sz) { ok = false; break; }
        std::vector<unsigned char> buf(orig);
        if (orig) {
            DECOMPRESSOR_HANDLE dh;
            if (!pCreate(COMPRESS_ALGORITHM_LZMS_, nullptr, &dh)) { ok = false; break; }
            SIZE_T got = 0;
            BOOL d = pDecomp(dh, pak + off, comp, buf.data(), orig, &got);
            pClose(dh);
            if (!d || got != orig) { ok = false; break; }
        }
        off += comp;
        int wn = MultiByteToWideChar(CP_UTF8, 0, rel.c_str(), -1, nullptr, 0);
        std::wstring wrel(wn - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, rel.c_str(), -1, &wrel[0], wn);
        if (wrel.find(L"..") != std::wstring::npos) { ok = false; break; }
        std::wstring dst = stage + L"\\" + wrel;
        mkdirs(dst.substr(0, dst.find_last_of(L'\\')));
        HANDLE h = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { ok = false; break; }
        DWORD wr = 0;
        if (orig) WriteFile(h, buf.data(), orig, &wr, nullptr);
        CloseHandle(h);
        if (wr != orig) ok = false;
    }
    if (!ok) { removeTree(stage); err = L"Unpacking the engine failed."; return false; }
    HANDLE m = CreateFileW((stage + L"\\.complete").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (m != INVALID_HANDLE_VALUE) CloseHandle(m);
    if (!MoveFileW(stage.c_str(), engineDir.c_str())) {
        if (exists(engineDir + L"\\.complete")) { removeTree(stage); return true; }  // another instance won the race
        removeTree(stage);
        err = L"Cannot finalise " + engineDir;
        return false;
    }
    return true;
}
