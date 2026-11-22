// Build-time tool: packs a directory tree into the engine payload (LZMS).
// usage: packer out.pak rootdir
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

typedef void *COMPRESSOR_HANDLE;
typedef BOOL(WINAPI *PFN_CreateCompressor)(DWORD, void *, COMPRESSOR_HANDLE *);
typedef BOOL(WINAPI *PFN_Compress)(COMPRESSOR_HANDLE, const void *, SIZE_T, void *, SIZE_T, SIZE_T *);
typedef BOOL(WINAPI *PFN_CloseCompressor)(COMPRESSOR_HANDLE);

static void walk(const std::wstring &root, const std::wstring &rel, std::vector<std::wstring> &out) {
    WIN32_FIND_DATAW fd;
    std::wstring dir = rel.empty() ? root : root + L"\\" + rel;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> names;
    do names.push_back(fd.cFileName); while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (auto &n : names) {
        if (n == L"." || n == L"..") continue;
        std::wstring r = rel.empty() ? n : rel + L"\\" + n;
        DWORD a = GetFileAttributesW((root + L"\\" + r).c_str());
        if (a & FILE_ATTRIBUTE_DIRECTORY) walk(root, r, out);
        else out.push_back(r);
    }
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 3) { fprintf(stderr, "usage: packer out.pak rootdir\n"); return 1; }
    HMODULE cab = LoadLibraryW(L"cabinet.dll");
    auto pCreate = (PFN_CreateCompressor)GetProcAddress(cab, "CreateCompressor");
    auto pCompress = (PFN_Compress)GetProcAddress(cab, "Compress");
    auto pClose = (PFN_CloseCompressor)GetProcAddress(cab, "CloseCompressor");
    std::vector<std::wstring> files;
    walk(argv[2], L"", files);
    FILE *o = _wfopen(argv[1], L"wb");
    if (!o) return 2;
    fwrite("VXPK1\0\0\0", 1, 8, o);
    DWORD count = (DWORD)files.size();
    fwrite(&count, 4, 1, o);
    size_t tin = 0, tout = 0;
    for (auto &rel : files) {
        FILE *f = _wfopen((std::wstring(argv[2]) + L"\\" + rel).c_str(), L"rb");
        if (!f) return 3;
        std::vector<unsigned char> d;
        unsigned char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
        fclose(f);
        std::vector<unsigned char> c(d.size() + 65536);
        SIZE_T cs = 0;
        if (!d.empty()) {
            COMPRESSOR_HANDLE ch;
            if (!pCreate(5 /*LZMS*/, nullptr, &ch)) return 4;
            if (!pCompress(ch, d.data(), d.size(), c.data(), c.size(), &cs)) return 5;
            pClose(ch);
        }
        char u8[1024];
        int ul = WideCharToMultiByte(CP_UTF8, 0, rel.c_str(), -1, u8, sizeof u8, nullptr, nullptr) - 1;
        DWORD pl = ul, os = (DWORD)d.size(), csz = (DWORD)cs;
        fwrite(&pl, 4, 1, o); fwrite(u8, 1, ul, o); fwrite(&os, 4, 1, o); fwrite(&csz, 4, 1, o); fwrite(c.data(), 1, cs, o);
        tin += os; tout += cs;
        fwprintf(stderr, L"  %-36ls %8lu -> %8lu\n", rel.c_str(), os, csz);
    }
    fclose(o);
    fprintf(stderr, "packed %lu files: %zu -> %zu bytes\n", count, tin, tout);
    return 0;
}
