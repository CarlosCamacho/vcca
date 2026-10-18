// Minimal Win32 compatibility shim for building the VCC core outside Windows.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef long LONG;
typedef int INT;
typedef char CHAR;
typedef void* HANDLE;
typedef void* HWND;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* HMENU;
typedef void* HDC;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef const char* LPCTSTR;
typedef intptr_t LPARAM;
typedef uintptr_t WPARAM;
typedef intptr_t LRESULT;
typedef DWORD* LPDWORD;
typedef long* PLONG;
typedef int64_t LONGLONG;
typedef union { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
struct _GUID { uint32_t a; uint16_t b, c; uint8_t d[8]; };
typedef _GUID GUID;
typedef struct { int dummy; } CRITICAL_SECTION;

#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define WINAPI
#define CALLBACK
#define __fastcall
#define _inline static inline
#define __declspec(x)
#define CW_USEDEFAULT ((int)0x80000000)
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define CREATE_ALWAYS 2
#define CREATE_NEW 1
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define MB_OK 0
#define MB_ICONERROR 0
#define MB_ICONWARNING 0
#define MB_ICONEXCLAMATION 0
#define MB_YESNO 0
#define MB_SETFOREGROUND 0
#define IDYES 6
#define LOWORD(l) ((WORD)((uintptr_t)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((uintptr_t)(l) >> 16) & 0xffff))
#define ZeroMemory(p, n) memset((p), 0, (n))
#define _stricmp strcasecmp
#define strcpy_s(d, n, s) strncpy((d), (s), (n))
#define sprintf_s snprintf

#include <strings.h>

inline void InitializeCriticalSection(CRITICAL_SECTION*) {}
inline void DeleteCriticalSection(CRITICAL_SECTION*) {}
inline void EnterCriticalSection(CRITICAL_SECTION*) {}
inline void LeaveCriticalSection(CRITICAL_SECTION*) {}
inline int MessageBox(HWND, const char* text, const char* caption, UINT) { fprintf(stderr, "[%s] %s\n", caption ? caption : "", text ? text : ""); return 0; }
#define MessageBoxA MessageBox
inline void OutputDebugString(const char* s) { fputs(s, stderr); }
inline DWORD GetLastError() { return 0; }
inline void Sleep(DWORD) {}
inline LRESULT SendMessage(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline BOOL PostMessage(HWND, UINT, WPARAM, LPARAM) { return 0; }

#define FILE_SHARE_READ 1
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define MB_TASKMODAL 0
#define MB_TOPMOST 0
inline BOOL FreeConsole() { return TRUE; }
inline BOOL AllocConsole() { return TRUE; }
inline HANDLE GetStdHandle(DWORD) { return nullptr; }
inline BOOL SetConsoleTitle(const char*) { return TRUE; }
BOOL QueryPerformanceFrequency(LARGE_INTEGER* f);
BOOL QueryPerformanceCounter(LARGE_INTEGER* c);
DWORD timeGetTime();

typedef unsigned short USHORT;
typedef unsigned char UCHAR;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
void GetLocalTime(SYSTEMTIME* t);
#define wsprintf sprintf

typedef void* PVOID;
typedef BYTE* PBYTE;
#define MEM_COMMIT 0x1000
#define MEM_RELEASE 0x8000
#define PAGE_READWRITE 4
inline void* VirtualAlloc(void*, size_t n, DWORD, DWORD) { return calloc(1, n); }
inline BOOL VirtualFree(void* p, size_t, DWORD) { free(p); return TRUE; }

// In-memory file system used in place of Win32 file handles (see vfs.cpp)
HANDLE CreateFile(const char* name, DWORD access, DWORD share, void* sec, DWORD disp, DWORD flags, HANDLE tmpl);
BOOL ReadFile(HANDLE h, void* buf, DWORD n, DWORD* got, void* ov);
BOOL WriteFile(HANDLE h, const void* buf, DWORD n, DWORD* put, void* ov);
DWORD SetFilePointer(HANDLE h, LONG dist, LONG* high, DWORD method);
DWORD GetFileSize(HANDLE h, DWORD* high);
BOOL CloseHandle(HANDLE h);
BOOL FlushFileBuffers(HANDLE h);
#define CreateFileA CreateFile
