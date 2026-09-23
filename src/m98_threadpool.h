/* Win98-backed subset of the Vista threadpool work-object API.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 */
#ifndef M98_THREADPOOL_H
#define M98_THREADPOOL_H

#include <windows.h>

/* These Win32 types are present in the cross headers even when targeting
 * Win98. The functions are exported via the KernelEx KERNEL32 API table. */
BOOL m98_tp_initialize(void);
void m98_tp_process_detach(BOOL process_terminating);

PTP_WORK WINAPI m98_CreateThreadpoolWork(PTP_WORK_CALLBACK callback,
                                         PVOID context,
                                         PTP_CALLBACK_ENVIRON environment);
void WINAPI m98_SubmitThreadpoolWork(PTP_WORK work);
void WINAPI m98_WaitForThreadpoolWorkCallbacks(PTP_WORK work,
                                                BOOL cancel_pending);
void WINAPI m98_CloseThreadpoolWork(PTP_WORK work);
void WINAPI m98_FreeLibraryWhenCallbackReturns(PTP_CALLBACK_INSTANCE instance,
                                                HMODULE module);
void WINAPI m98_LeaveCriticalSectionWhenCallbackReturns(PTP_CALLBACK_INSTANCE instance,
                                                        PCRITICAL_SECTION section);
void WINAPI m98_ReleaseMutexWhenCallbackReturns(PTP_CALLBACK_INSTANCE instance,
                                                HANDLE mutex);
void WINAPI m98_ReleaseSemaphoreWhenCallbackReturns(PTP_CALLBACK_INSTANCE instance,
                                                    HANDLE semaphore, DWORD count);
void WINAPI m98_SetEventWhenCallbackReturns(PTP_CALLBACK_INSTANCE instance,
                                            HANDLE event);
void WINAPI m98_DisassociateCurrentThreadFromCallback(PTP_CALLBACK_INSTANCE instance);
BOOL WINAPI m98_CallbackMayRunLong(PTP_CALLBACK_INSTANCE instance);
BOOL WINAPI m98_TrySubmitThreadpoolCallback(PTP_SIMPLE_CALLBACK callback,
                                            PVOID context,
                                            PTP_CALLBACK_ENVIRON environment);

#endif
