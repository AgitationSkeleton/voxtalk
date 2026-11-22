#pragma once
#include "kernel.h"
// Every Win32 function the engine imports is provided as shim_<Name>. The build
// script checks each shim's stdcall argument size against the real API.
#define SHIM(ret, name, args) ret STDCALL shim_##name args
#define SHIM_CDECL(ret, name, args) ret CDECL shim_##name args

SHIM(u32, LoadLibraryA, (const char *name));
SHIM(void *, GetProcAddress, (u32 h, const char *name));

typedef struct { u32 kind; } Obj;
enum { K_EVENT = 1, K_SEM, K_THREAD, K_FILE, K_MAP, K_FIND, K_WND };
