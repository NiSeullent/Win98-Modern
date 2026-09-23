/*
 * Windows 98 threadpool work objects for KernelEx.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Source review: Wine df15af365251 dlls/ntdll/threadpool.c (TpAllocWork,
 * tp_object_submit/cancel/execute, TpReleaseWork and TpWaitForWork), Wine
 * dlls/kernelbase/thread.c, ReactOS 9dc3ca8720 sdk/lib/rtl/threadpool.c and
 * dll/win32/kernel32/kernel32_vista/threadpool.c. No upstream code is copied.
 * Win98 lacks the NT completion-port and condition-variable implementation;
 * this independent adapter uses original Win98 threads, a semaphore and a
 * critical section. See docs/THREADPOOL_WORK_PORT.md for its scope.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include "m98_threadpool.h"

#define M98_TP_WORK_MAGIC 0x4d395457UL /* M9TW */
#define M98_TP_INSTANCE_MAGIC 0x4d395449UL /* M9TI */
#define M98_TP_WORKER_COUNT 4

typedef struct m98_tp_work m98_tp_work;
typedef struct m98_tp_callback_instance {
    DWORD magic;
    DWORD thread_id;
    HMODULE deferred_library;
} m98_tp_callback_instance;

struct m98_tp_work {
    DWORD magic;
    m98_tp_work *next;
    PTP_WORK_CALLBACK callback;
    PTP_SIMPLE_CALLBACK finalization;
    PVOID context;
    HANDLE idle_event; /* signaled only when pending + running == 0 */
    DWORD pending;
    DWORD running;
    ULONGLONG references; /* caller ownership + one per outstanding callback */
    BOOL queued;
    BOOL closed;
};

typedef struct m98_tp_environment_v3 {
    TP_CALLBACK_ENVIRON prefix;
    TP_CALLBACK_PRIORITY priority;
    DWORD size;
} m98_tp_environment_v3;

static CRITICAL_SECTION m98_tp_lock;
static HANDLE m98_tp_semaphore;
static m98_tp_work *m98_tp_first, *m98_tp_last;
static DWORD m98_tp_workers;
static BOOL m98_tp_initialized, m98_tp_shutdown;

static void m98_tp_queue_push(m98_tp_work *work)
{
    work->next = NULL;
    work->queued = TRUE;
    if (m98_tp_last) m98_tp_last->next = work;
    else m98_tp_first = work;
    m98_tp_last = work;
}

static m98_tp_work *m98_tp_queue_pop(void)
{
    m98_tp_work *work = m98_tp_first;
    if (work) {
        m98_tp_first = work->next;
        if (!m98_tp_first) m98_tp_last = NULL;
        work->next = NULL;
        work->queued = FALSE;
    }
    return work;
}

static void m98_tp_queue_remove(m98_tp_work *work)
{
    m98_tp_work *previous = NULL, *cursor = m98_tp_first;
    while (cursor && cursor != work) {
        previous = cursor;
        cursor = cursor->next;
    }
    if (!cursor) return;
    if (previous) previous->next = work->next;
    else m98_tp_first = work->next;
    if (m98_tp_last == work) m98_tp_last = previous;
    work->next = NULL;
    work->queued = FALSE;
}

static void m98_tp_destroy_work(m98_tp_work *work)
{
    CloseHandle(work->idle_event);
    work->magic = 0;
    HeapFree(GetProcessHeap(), 0, work);
}

static DWORD WINAPI m98_tp_worker(void *unused)
{
    (void)unused;
    for (;;) {
        m98_tp_work *work;
        m98_tp_callback_instance instance;
        BOOL destroy;

        if (WaitForSingleObject(m98_tp_semaphore, INFINITE) != WAIT_OBJECT_0)
            return 0;
        EnterCriticalSection(&m98_tp_lock);
        if (m98_tp_shutdown) {
            LeaveCriticalSection(&m98_tp_lock);
            return 0;
        }
        work = m98_tp_queue_pop();
        if (!work) {
            /* Close/Wait may have canceled a queued work object's permit. */
            LeaveCriticalSection(&m98_tp_lock);
            continue;
        }
        --work->pending;
        ++work->running;
        if (work->pending) {
            m98_tp_queue_push(work);
            ReleaseSemaphore(m98_tp_semaphore, 1, NULL);
        }
        LeaveCriticalSection(&m98_tp_lock);

        instance.magic = M98_TP_INSTANCE_MAGIC;
        instance.thread_id = GetCurrentThreadId();
        instance.deferred_library = NULL;
        work->callback((PTP_CALLBACK_INSTANCE)&instance, work->context,
                       (PTP_WORK)work);
        if (work->finalization)
            work->finalization((PTP_CALLBACK_INSTANCE)&instance, work->context);
        if (instance.deferred_library)
            FreeLibrary(instance.deferred_library);
        instance.magic = 0;

        EnterCriticalSection(&m98_tp_lock);
        --work->running;
        --work->references;
        if (!work->pending && !work->running) SetEvent(work->idle_event);
        destroy = work->closed && !work->references;
        LeaveCriticalSection(&m98_tp_lock);
        if (destroy) m98_tp_destroy_work(work);
    }
}

static BOOL m98_tp_ensure_workers(void)
{
    DWORD error = 0;
    while (m98_tp_workers < M98_TP_WORKER_COUNT) {
        DWORD thread_id;
        /* Original Win9x CreateThread requires a writable lpThreadId. The
         * KernelEx CreateThread_fix supplies a dummy for NULL, but an API
         * provider can import the original function before that override is
         * active. Never rely on the fix here. */
        HANDLE thread = CreateThread(NULL, 0, m98_tp_worker, NULL, 0,
                                     &thread_id);
        if (!thread) {
            error = GetLastError();
            break;
        }
        ++m98_tp_workers;
        CloseHandle(thread);
    }
    if (!m98_tp_workers) {
        SetLastError(error ? error : ERROR_GEN_FAILURE);
        return FALSE;
    }
    return TRUE;
}

static BOOL m98_tp_supported_environment(PTP_CALLBACK_ENVIRON env)
{
    if (!env) return TRUE;
    if (env->Version != 1 && env->Version != 3) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (env->Pool || env->CleanupGroup || env->CleanupGroupCancelCallback ||
        env->RaceDll || env->ActivationContext || (env->u.Flags & ~1UL)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (env->Version == 3) {
        const m98_tp_environment_v3 *v3 = (const m98_tp_environment_v3 *)env;
        if (v3->size != sizeof(*v3) ||
            v3->priority != TP_CALLBACK_PRIORITY_NORMAL) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL m98_tp_initialize(void)
{
    InitializeCriticalSection(&m98_tp_lock);
    m98_tp_semaphore = CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
    if (!m98_tp_semaphore) {
        DWORD error = GetLastError();
        DeleteCriticalSection(&m98_tp_lock);
        SetLastError(error ? error : ERROR_GEN_FAILURE);
        return FALSE;
    }
    m98_tp_first = m98_tp_last = NULL;
    m98_tp_workers = 0;
    m98_tp_shutdown = FALSE;
    m98_tp_initialized = TRUE;
    return TRUE;
}

void m98_tp_process_detach(void)
{
    DWORD i;
    if (!m98_tp_initialized) return;
    /* DllMain holds the loader lock: never wait for callback threads here.
     * KernelEx keeps this API library loaded for the process lifetime. */
    EnterCriticalSection(&m98_tp_lock);
    m98_tp_shutdown = TRUE;
    LeaveCriticalSection(&m98_tp_lock);
    for (i = 0; i < m98_tp_workers; ++i)
        ReleaseSemaphore(m98_tp_semaphore, 1, NULL);
}

PTP_WORK WINAPI m98_CreateThreadpoolWork(PTP_WORK_CALLBACK callback,
                                         PVOID context,
                                         PTP_CALLBACK_ENVIRON environment)
{
    m98_tp_work *work;
    if (!callback) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!m98_tp_supported_environment(environment)) return NULL;
    if (!m98_tp_initialized) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    work = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*work));
    if (!work) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    work->idle_event = CreateEventA(NULL, TRUE, TRUE, NULL);
    if (!work->idle_event) {
        DWORD error = GetLastError();
        HeapFree(GetProcessHeap(), 0, work);
        SetLastError(error ? error : ERROR_GEN_FAILURE);
        return NULL;
    }
    work->magic = M98_TP_WORK_MAGIC;
    work->callback = callback;
    work->context = context;
    work->finalization = environment ? environment->FinalizationCallback : NULL;
    work->references = 1;
    EnterCriticalSection(&m98_tp_lock);
    if (!m98_tp_ensure_workers()) {
        DWORD error = GetLastError();
        LeaveCriticalSection(&m98_tp_lock);
        m98_tp_destroy_work(work);
        SetLastError(error ? error : ERROR_GEN_FAILURE);
        return NULL;
    }
    LeaveCriticalSection(&m98_tp_lock);
    return (PTP_WORK)work;
}

void WINAPI m98_SubmitThreadpoolWork(PTP_WORK opaque)
{
    m98_tp_work *work = (m98_tp_work *)opaque;
    if (!work || work->magic != M98_TP_WORK_MAGIC) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return;
    }
    EnterCriticalSection(&m98_tp_lock);
    if (work->closed || work->pending == 0xffffffffUL) {
        LeaveCriticalSection(&m98_tp_lock);
        SetLastError(ERROR_TOO_MANY_POSTS);
        return;
    }
    if (!work->pending && !work->running) ResetEvent(work->idle_event);
    ++work->pending;
    ++work->references;
    if (!work->queued) {
        m98_tp_queue_push(work);
        ReleaseSemaphore(m98_tp_semaphore, 1, NULL);
    }
    LeaveCriticalSection(&m98_tp_lock);
}

void WINAPI m98_WaitForThreadpoolWorkCallbacks(PTP_WORK opaque,
                                                BOOL cancel_pending)
{
    m98_tp_work *work = (m98_tp_work *)opaque;
    if (!work || work->magic != M98_TP_WORK_MAGIC) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return;
    }
    if (cancel_pending) {
        EnterCriticalSection(&m98_tp_lock);
        if (work->queued) m98_tp_queue_remove(work);
        work->references -= work->pending;
        work->pending = 0;
        if (!work->running) SetEvent(work->idle_event);
        LeaveCriticalSection(&m98_tp_lock);
    }
    WaitForSingleObject(work->idle_event, INFINITE);
}

void WINAPI m98_CloseThreadpoolWork(PTP_WORK opaque)
{
    m98_tp_work *work = (m98_tp_work *)opaque;
    BOOL destroy;
    if (!work || work->magic != M98_TP_WORK_MAGIC) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return;
    }
    EnterCriticalSection(&m98_tp_lock);
    if (work->closed) {
        LeaveCriticalSection(&m98_tp_lock);
        SetLastError(ERROR_INVALID_HANDLE);
        return;
    }
    work->closed = TRUE;
    /* Closing drops caller ownership, but callbacks already submitted still
     * run. WaitForThreadpoolWorkCallbacks(..., TRUE) is the cancel operation. */
    --work->references;
    destroy = !work->references;
    LeaveCriticalSection(&m98_tp_lock);
    if (destroy) m98_tp_destroy_work(work);
}

void WINAPI m98_FreeLibraryWhenCallbackReturns(PTP_CALLBACK_INSTANCE opaque,
                                                HMODULE module)
{
    m98_tp_callback_instance *instance = (m98_tp_callback_instance *)opaque;
    if (!instance || instance->magic != M98_TP_INSTANCE_MAGIC ||
        instance->thread_id != GetCurrentThreadId() || !module) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return;
    }
    /* Windows/Wine keep one deferred library per callback instance. */
    if (!instance->deferred_library)
        instance->deferred_library = module;
}
