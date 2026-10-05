// Replacement for the Win32 file API, used by the FD-502's wd1793.cpp and the
// IDE interface unchanged. A file is a disk or hard drive image the
// JavaScript host holds; it is opened by name, and reads and writes go to the
// host through the imports below. Keeping file contents out of WebAssembly
// memory is what lets a snapshot be a copy of that memory: it stays small,
// and restoring one never rolls back a disk.
#include <Windows.h>
#include <time.h>

#define HOST_IMPORT(name) __attribute__((import_module("vcca"), import_name(#name)))
extern "C" {
HOST_IMPORT(file_open) int host_file_open(const char* name, int nameLen, int write);
HOST_IMPORT(file_size) unsigned int host_file_size(int id);
HOST_IMPORT(file_read) unsigned int host_file_read(int id, unsigned int pos, void* buf, unsigned int n);
HOST_IMPORT(file_write) unsigned int host_file_write(int id, unsigned int pos, const void* buf, unsigned int n);
HOST_IMPORT(file_close) void host_file_close(int id);
}

namespace
{
	struct VHandle
	{
		int id;
		size_t pos = 0;
	};
}

HANDLE CreateFile(const char* name, DWORD access, DWORD, void*, DWORD, DWORD, HANDLE)
{
	if (!name) return INVALID_HANDLE_VALUE;
	int id = host_file_open(name, (int)strlen(name), (access & GENERIC_WRITE) ? 1 : 0);
	if (id < 0) return INVALID_HANDLE_VALUE;
	auto h = new VHandle;
	h->id = id;
	return h;
}

BOOL ReadFile(HANDLE handle, void* buf, DWORD n, DWORD* got, void*)
{
	auto h = static_cast<VHandle*>(handle);
	if (!h || handle == INVALID_HANDLE_VALUE) { if (got) *got = 0; return FALSE; }
	unsigned int count = host_file_read(h->id, (unsigned int)h->pos, buf, n);
	h->pos += count;
	if (got) *got = count;
	return TRUE;
}

BOOL WriteFile(HANDLE handle, const void* buf, DWORD n, DWORD* put, void*)
{
	auto h = static_cast<VHandle*>(handle);
	if (!h || handle == INVALID_HANDLE_VALUE) { if (put) *put = 0; return FALSE; }
	unsigned int count = host_file_write(h->id, (unsigned int)h->pos, buf, n);
	h->pos += count;
	if (put) *put = count;
	return count == n;
}

DWORD SetFilePointer(HANDLE handle, LONG dist, LONG*, DWORD method)
{
	auto h = static_cast<VHandle*>(handle);
	if (!h || handle == INVALID_HANDLE_VALUE)
		return INVALID_SET_FILE_POINTER;
	long base = method == FILE_BEGIN ? 0 : method == FILE_CURRENT ? (long)h->pos : (long)host_file_size(h->id);
	long pos = base + dist;
	if (pos < 0)
		return INVALID_SET_FILE_POINTER;
	h->pos = (size_t)pos;
	return (DWORD)pos;
}

DWORD GetFileSize(HANDLE handle, DWORD* high)
{
	if (high) *high = 0;
	auto h = static_cast<VHandle*>(handle);
	if (!h || handle == INVALID_HANDLE_VALUE)
		return 0;
	return (DWORD)host_file_size(h->id);
}

BOOL CloseHandle(HANDLE handle)
{
	if (handle && handle != INVALID_HANDLE_VALUE) {
		auto h = static_cast<VHandle*>(handle);
		host_file_close(h->id);
		delete h;
	}
	return TRUE;
}

BOOL FlushFileBuffers(HANDLE) { return TRUE; }

BOOL QueryPerformanceFrequency(LARGE_INTEGER* f)
{
	f->QuadPart = 1000000000LL;
	return TRUE;
}

BOOL QueryPerformanceCounter(LARGE_INTEGER* c)
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	c->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
	return TRUE;
}

DWORD timeGetTime()
{
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (DWORD)(c.QuadPart / 1000000);
}

void GetLocalTime(SYSTEMTIME* t)
{
	time_t now = time(nullptr);
	struct tm lt;
	localtime_r(&now, &lt);
	t->wYear = (WORD)(lt.tm_year + 1900);
	t->wMonth = (WORD)(lt.tm_mon + 1);
	t->wDayOfWeek = (WORD)lt.tm_wday;
	t->wDay = (WORD)lt.tm_mday;
	t->wHour = (WORD)lt.tm_hour;
	t->wMinute = (WORD)lt.tm_min;
	t->wSecond = (WORD)lt.tm_sec;
	t->wMilliseconds = 0;
}
