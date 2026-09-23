/* Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#ifndef M98_FLS_H
#define M98_FLS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef FLS_OUT_OF_INDEXES
#define FLS_OUT_OF_INDEXES 0xffffffffUL
#endif

typedef VOID (WINAPI *M98_FLS_CALLBACK)(PVOID);

DWORD WINAPI m98_FlsAlloc(M98_FLS_CALLBACK callback);
BOOL WINAPI m98_FlsFree(DWORD index);
PVOID WINAPI m98_FlsGetValue(DWORD index);
BOOL WINAPI m98_FlsSetValue(DWORD index, PVOID value);

LPVOID WINAPI m98_CreateFiber(SIZE_T stack, LPFIBER_START_ROUTINE start,
                              LPVOID parameter);
LPVOID WINAPI m98_CreateFiberEx(SIZE_T commit, SIZE_T reserve, DWORD flags,
                                LPFIBER_START_ROUTINE start, LPVOID parameter);
LPVOID WINAPI m98_ConvertThreadToFiber(LPVOID parameter);
LPVOID WINAPI m98_ConvertThreadToFiberEx(LPVOID parameter, DWORD flags);
VOID WINAPI m98_SwitchToFiber(LPVOID fiber);
VOID WINAPI m98_DeleteFiber(LPVOID fiber);

HANDLE WINAPI m98_CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack,
                               LPTHREAD_START_ROUTINE start, LPVOID parameter,
                               DWORD flags, LPDWORD thread_id);
VOID WINAPI m98_ExitThread(DWORD code);
VOID WINAPI m98_FreeLibraryAndExitThread(HMODULE module, DWORD code);

/* Explicit user-context teardown for a future KernelEx native thread hook.
 * Never invoke it from DllMain; callbacks may load libraries or reenter FLS. */
VOID m98_fls_rundown_current_thread(void);

#endif
