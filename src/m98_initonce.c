/*
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Independent Win98 implementation of the one-time initialization contract.
 * Sources analyzed, not copied:
 * Wine df15af3652511150490934682202d45af892f887 dlls/ntdll/sync.c,
 *   RtlRunOnceBeginInitialize/Complete/ExecuteOnce; dlls/kernelbase/sync.c.
 * ReactOS 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8 sdk/lib/rtl/runonce.c
 *   and modules/rostests/apitests/kernel32/InitOnce.c.
 * Both upstream runtimes use NT keyed-event waiter lists. Win98 has no such
 * API: waiters yield with Sleep(1) and recheck the interlocked state instead.
 * No allocation, process-global registry, NT import, or DllMain work is needed.
 *
 * The published pointer-sized ABI reserves the low two context bits. Tags are
 * 0 uninitialized, 1 synchronous initialization, 2 completed, 3 asynchronous
 * initialization. Every access is interlocked so publication also carries a
 * memory barrier. Synchronous Begin/Complete and ExecuteOnce share this state.
 * Flag validation follows the Microsoft API contract and native host probes;
 * the cited Wine/ReactOS revisions accept some flags which native rejects.
 * See docs/INITONCE_PORT.md for contracts, tests, and the guest evidence boundary.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include "m98_initonce.h"

typedef char m98_once_must_be_one_word[(sizeof(INIT_ONCE) == sizeof(LONG)) ? 1 : -1];

static LONG m98_once_read(LPINIT_ONCE once)
{
    return InterlockedCompareExchange((volatile LONG *)&once->Ptr, 0, 0);
}

static BOOL m98_once_error(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

VOID WINAPI m98_InitOnceInitialize(PINIT_ONCE once)
{
    /* The caller must own the storage exclusively during initialization. */
    InterlockedExchange((volatile LONG *)&once->Ptr, 0);
}

BOOL WINAPI m98_InitOnceBeginInitialize(LPINIT_ONCE once, DWORD flags,
                                        PBOOL pending, LPVOID *context)
{
    LONG current, tag;
    BOOL asynchronous;
    if (!once || !pending || (flags & ~(INIT_ONCE_CHECK_ONLY | INIT_ONCE_ASYNC)) ||
        flags == (INIT_ONCE_CHECK_ONLY | INIT_ONCE_ASYNC))
        return m98_once_error(ERROR_INVALID_PARAMETER);

    asynchronous = (flags & INIT_ONCE_ASYNC) != 0;
    for (;;) {
        current = m98_once_read(once);
        tag = current & 3;
        if (tag == 2) {
            if (context) *context = (PVOID)(ULONG_PTR)(current & ~3L);
            *pending = FALSE;
            return TRUE;
        }
        /* Failed checks leave both output parameters untouched. */
        if (flags & INIT_ONCE_CHECK_ONLY)
            return m98_once_error(ERROR_GEN_FAILURE);
        if (!tag) {
            LONG desired = asynchronous ? 3 : 1;
            if (InterlockedCompareExchange((volatile LONG *)&once->Ptr,
                                           desired, 0) != 0)
                continue;
            *pending = TRUE;
            return TRUE;
        }
        if ((tag == 3) != asynchronous)
            return m98_once_error(ERROR_INVALID_PARAMETER);
        if (asynchronous) {
            *pending = TRUE;
            return TRUE;
        }
        /* A yielding wait is necessary on Win98's single-CPU scheduler;
         * a busy spin could otherwise starve the initializing thread. */
        Sleep(1);
    }
}

BOOL WINAPI m98_InitOnceComplete(LPINIT_ONCE once, DWORD flags, LPVOID context)
{
    LONG current, desired;
    BOOL failed = (flags & INIT_ONCE_INIT_FAILED) != 0;
    BOOL asynchronous = (flags & INIT_ONCE_ASYNC) != 0;
    if (!once || (flags & ~(INIT_ONCE_INIT_FAILED | INIT_ONCE_ASYNC)) ||
        ((ULONG_PTR)context & 3) || (failed && (asynchronous || context)))
        return m98_once_error(ERROR_INVALID_PARAMETER);

    desired = failed ? 0 : (LONG)((ULONG_PTR)context | 2);
    for (;;) {
        current = m98_once_read(once);
        if ((current & 3) != 1 && (current & 3) != 3)
            return m98_once_error(ERROR_GEN_FAILURE);
        if (((current & 3) == 3) != asynchronous)
            return m98_once_error(ERROR_INVALID_PARAMETER);
        if (InterlockedCompareExchange((volatile LONG *)&once->Ptr,
                                       desired, current) == current)
            return TRUE;
        /* A contender that already observed async pending lost its CAS.
         * Native Windows distinguishes that race from a later Complete
         * which first observes the completed object (ERROR_GEN_FAILURE). */
        if (asynchronous) return m98_once_error(ERROR_ALREADY_EXISTS);
    }
}

BOOL WINAPI m98_InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN callback,
                                    PVOID parameter, LPVOID *context)
{
    BOOL pending;
    DWORD callback_error;
    if (!once || !callback) return m98_once_error(ERROR_INVALID_PARAMETER);
    if (!m98_InitOnceBeginInitialize(once, 0, &pending, context)) return FALSE;
    if (!pending) return TRUE;
    /* Pass the caller's optional context pointer through unchanged. The
     * callback is allowed to test for NULL and set its own LastError. */
    if (!callback(once, parameter, context)) {
        callback_error = GetLastError();
        m98_InitOnceComplete(once, INIT_ONCE_INIT_FAILED, NULL);
        SetLastError(callback_error);
        return FALSE;
    }
    return m98_InitOnceComplete(once, 0, context ? *context : NULL);
}
