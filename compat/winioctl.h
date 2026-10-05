#pragma once
#define CTL_CODE(t, f, m, a) (((t) << 16) | ((a) << 14) | ((f) << 2) | (m))
#define METHOD_BUFFERED 0
#define METHOD_IN_DIRECT 1
#define METHOD_OUT_DIRECT 2
#define METHOD_NEITHER 3
#define FILE_ANY_ACCESS 0
#define FILE_READ_ACCESS 1
#define FILE_WRITE_ACCESS 2
#define FILE_READ_DATA 1
#define FILE_WRITE_DATA 2
#define FILE_DEVICE_UNKNOWN 0x22
// Real floppy access (fdrawcmd.sys) does not exist on Android.
inline BOOL DeviceIoControl(HANDLE, DWORD, void*, DWORD, void*, DWORD, DWORD*, void*) { return FALSE; }
