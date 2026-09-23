/* Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#ifndef M98_INITONCE_H
#define M98_INITONCE_H

#include <windows.h>

VOID WINAPI m98_InitOnceInitialize(PINIT_ONCE once);
BOOL WINAPI m98_InitOnceBeginInitialize(LPINIT_ONCE once, DWORD flags,
                                        PBOOL pending, LPVOID *context);
BOOL WINAPI m98_InitOnceComplete(LPINIT_ONCE once, DWORD flags, LPVOID context);
BOOL WINAPI m98_InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN callback,
                                    PVOID parameter, LPVOID *context);

#endif
