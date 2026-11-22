// Minimal SAPI 4 era interface declarations needed to drive the FlexTalk
// engine in-process (written from the public interface layouts).
#pragma once
#include <windows.h>
#include <objbase.h>

typedef unsigned __int64 QWORD, *PQWORD;

struct SDATA { PVOID pData; DWORD dwSize; };
typedef SDATA *PSDATA;

enum VOICECHARSET { CHARSET_TEXT = 0, CHARSET_IPAPHONETIC = 1, CHARSET_ENGINEPHONETIC = 2 };

#define VT_NAMELEN 262
#define VT_LANGLEN 64

struct LANGUAGEW { LANGID LanguageID; WCHAR szDialect[VT_LANGLEN]; };

struct TTSMODEINFOW {
    GUID      gEngineID;
    WCHAR     szMfgName[VT_NAMELEN];
    WCHAR     szProductName[VT_NAMELEN];
    GUID      gModeID;
    WCHAR     szModeName[VT_NAMELEN];
    LANGUAGEW language;
    WCHAR     szSpeaker[VT_NAMELEN];
    WCHAR     szStyle[VT_NAMELEN];
    WORD      wGender;
    WORD      wAge;
    DWORD     dwFeatures;
    DWORD     dwInterfaces;
    DWORD     dwEngineFeatures;
};

struct TTSMOUTH { BYTE b[8]; };

#define TTSDATAFLAG_TAGGED 1

struct IAudio : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Flush() = 0;
    virtual HRESULT STDMETHODCALLTYPE LevelGet(DWORD *) = 0;
    virtual HRESULT STDMETHODCALLTYPE LevelSet(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE PassNotify(PVOID, IID) = 0;
    virtual HRESULT STDMETHODCALLTYPE PosnGet(PQWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE Claim() = 0;
    virtual HRESULT STDMETHODCALLTYPE UnClaim() = 0;
    virtual HRESULT STDMETHODCALLTYPE Start() = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
    virtual HRESULT STDMETHODCALLTYPE TotalGet(PQWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE ToFileTime(PQWORD, FILETIME *) = 0;
    virtual HRESULT STDMETHODCALLTYPE WaveFormatGet(PSDATA) = 0;
    virtual HRESULT STDMETHODCALLTYPE WaveFormatSet(SDATA) = 0;
};

struct IAudioDest : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE FreeSpace(DWORD *, BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE DataSet(PVOID, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE BookMark(DWORD) = 0;
};

struct IAudioDestNotifySink : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AudioStop(WORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE AudioStart() = 0;
    virtual HRESULT STDMETHODCALLTYPE FreeSpace(DWORD, BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE BookMark(DWORD, BOOL) = 0;
};

struct ITTSCentralW : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Inject(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE ModeGet(TTSMODEINFOW *) = 0;
    virtual HRESULT STDMETHODCALLTYPE Phoneme(VOICECHARSET, DWORD, SDATA, PSDATA) = 0;
    virtual HRESULT STDMETHODCALLTYPE PosnGet(PQWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE TextData(VOICECHARSET, DWORD, SDATA, PVOID, IID) = 0;
    virtual HRESULT STDMETHODCALLTYPE ToFileTime(PQWORD, FILETIME *) = 0;
    virtual HRESULT STDMETHODCALLTYPE AudioPause() = 0;
    virtual HRESULT STDMETHODCALLTYPE AudioResume() = 0;
    virtual HRESULT STDMETHODCALLTYPE AudioReset() = 0;
    virtual HRESULT STDMETHODCALLTYPE Register(PVOID, IID, DWORD *) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnRegister(DWORD) = 0;
};

struct ITTSEnumW : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Next(ULONG, TTSMODEINFOW *, ULONG *) = 0;
    virtual HRESULT STDMETHODCALLTYPE Skip(ULONG) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
    virtual HRESULT STDMETHODCALLTYPE Clone(ITTSEnumW **) = 0;
    virtual HRESULT STDMETHODCALLTYPE Select(GUID, ITTSCentralW **, LPUNKNOWN) = 0;
};

struct ITTSAttributesW : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE PitchGet(WORD *) = 0;
    virtual HRESULT STDMETHODCALLTYPE PitchSet(WORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE RealTimeGet(DWORD *) = 0;
    virtual HRESULT STDMETHODCALLTYPE RealTimeSet(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE SpeedGet(DWORD *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SpeedSet(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE VolumeGet(DWORD *) = 0;
    virtual HRESULT STDMETHODCALLTYPE VolumeSet(DWORD) = 0;
};

struct ITTSBufNotifySink : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE TextDataDone(QWORD, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE TextDataStarted(QWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE BookMark(QWORD, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE WordPosition(QWORD, DWORD) = 0;
};

// {F546B340-C743-11cd-80E5-00AA003E4B50}
static const IID IID_IAudio_ = {0xf546b340, 0xc743, 0x11cd, {0x80, 0xe5, 0x00, 0xaa, 0x00, 0x3e, 0x4b, 0x50}};
// {2EC34DA0-C743-11cd-80E5-00AA003E4B50}
static const IID IID_IAudioDest_ = {0x2ec34da0, 0xc743, 0x11cd, {0x80, 0xe5, 0x00, 0xaa, 0x00, 0x3e, 0x4b, 0x50}};
// {ACB08C00-C743-11cd-80E5-00AA003E4B50}
static const IID IID_IAudioDestNotifySink_ = {0xacb08c00, 0xc743, 0x11cd, {0x80, 0xe5, 0x00, 0xaa, 0x00, 0x3e, 0x4b, 0x50}};
static const IID IID_ITTSCentralW_ = {0x28016060, 0x4A47, 0x101B, {0x93, 0x1A, 0x00, 0xAA, 0x00, 0x47, 0xBA, 0x4F}};
static const IID IID_ITTSEnumW_ = {0x6B837B20, 0x4A47, 0x101B, {0x93, 0x1A, 0x00, 0xAA, 0x00, 0x47, 0xBA, 0x4F}};
static const IID IID_ITTSAttributesW_ = {0x1287A280, 0x4A47, 0x101B, {0x93, 0x1A, 0x00, 0xAA, 0x00, 0x47, 0xBA, 0x4F}};
// {E4963D40-C743-11cd-80E5-00AA003E4B50}
static const IID IID_ITTSBufNotifySink_ = {0xe4963d40, 0xc743, 0x11cd, {0x80, 0xe5, 0x00, 0xaa, 0x00, 0x3e, 0x4b, 0x50}};

// FlexTalk engine class {FFE78E00-C69C-11CE-AEC1-0000C0E9CB87}
static const CLSID CLSID_FlexTalk = {0xFFE78E00, 0xC69C, 0x11CE, {0xAE, 0xC1, 0x00, 0x00, 0xC0, 0xE9, 0xCB, 0x87}};
