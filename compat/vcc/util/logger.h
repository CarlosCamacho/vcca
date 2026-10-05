#pragma once
inline void PrintLogC(const char*, ...) {}
inline void PrintLogF(const char*, ...) {}
#define DLOG_C(...) ((void)0)
#define DLOG_F(...) ((void)0)
