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
#define M98_TP_MAX_WORKERS 500

typedef struct m98_tp_work m98_tp_work;
typedef struct m98_tp_callback_instance {
    DWORD magic;
    DWORD thread_id;
    m98_tp_work *work;
    BOOL associated;
    BOOL may_run_long;
    PCRITICAL_SECTION deferred_section;
    HANDLE deferred_mutex, deferred_semaphore, deferred_event;
    DWORD deferred_semaphore_count;
    HMODULE deferred_library;
} m98_tp_callback_instance;

struct m98_tp_work {
    DWORD magic;
    m98_tp_work *next;
    PTP_WORK_CALLBACK callback;
    PTP_SIMPLE_CALLBACK simple_callback;
    PTP_SIMPLE_CALLBACK finalization;
    PVOID context;
    HANDLE idle_event; /* pending + associated callbacks == 0 */
    DWORD pending;
    DWORD running;
    ULONGLONG references; /* caller ownership + one per outstanding callback */
    BOOL queued;
    BOOL closed;
    BOOL runs_long;
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
static DWORD m98_tp_busy;
static HANDLE m98_tp_growth_event;
static BOOL m98_tp_monitor_started, m98_tp_retry_growth;
static BOOL m98_tp_initialized, m98_tp_shutdown;

static BOOL m98_tp_grow_workers(DWORD target);

/* A control thread retries failed growth even when every callback is blocked.
 * This is also a shared scheduling foundation for later timer/wait objects.
 * It never invokes application callbacks and is not counted as a pool worker. */
static DWORD WINAPI m98_tp_monitor(void *unused)
{
    (void)unused;
    for (;;) {
        WaitForSingleObject(m98_tp_growth_event, 200);
        EnterCriticalSection(&m98_tp_lock);
        if (m98_tp_shutdown) {
            LeaveCriticalSection(&m98_tp_lock);
            return 0;
        }
        if (m98_tp_retry_growth) {
            if (m98_tp_busy < m98_tp_workers ||
                (m98_tp_workers < M98_TP_MAX_WORKERS &&
                 m98_tp_grow_workers(m98_tp_workers + 1)))
                m98_tp_retry_growth = FALSE;
        }
        LeaveCriticalSection(&m98_tp_lock);
    }
}

static m98_tp_callback_instance *m98_tp_instance(PTP_CALLBACK_INSTANCE opaque)
{
    m98_tp_callback_instance *instance = (m98_tp_callback_instance *)opaque;
    if (!instance || instance->magic != M98_TP_INSTANCE_MAGIC ||
        instance->thread_id != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return instance;
}

/* Source review: pinned Wine ntdll/threadpool.c tp_object_execute_callbacks
 * and ReactOS rtl/threadpool.c use one deferred resource of each kind, after
 * finalization, and stop later cleanup on a failed native handle operation.
 * Independent Win98 implementation: use original KERNEL32 handle functions. */
static void m98_tp_cleanup(m98_tp_callback_instance *instance)
{
    if (instance->deferred_section)
        LeaveCriticalSection(instance->deferred_section);
    if (instance->deferred_mutex && !ReleaseMutex(instance->deferred_mutex)) return;
    if (instance->deferred_semaphore &&
        !ReleaseSemaphore(instance->deferred_semaphore,
                          instance->deferred_semaphore_count, NULL)) return;
    if (instance->deferred_event && !SetEvent(instance->deferred_event)) return;
    if (instance->deferred_library) FreeLibrary(instance->deferred_library);
}

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
        ++m98_tp_busy;
        if (work->pending) {
            m98_tp_queue_push(work);
            ReleaseSemaphore(m98_tp_semaphore, 1, NULL);
        }
        LeaveCriticalSection(&m98_tp_lock);

        instance.magic = M98_TP_INSTANCE_MAGIC;
        instance.thread_id = GetCurrentThreadId();
        instance.work = work;
        instance.associated = TRUE;
        instance.may_run_long = FALSE;
        instance.deferred_section = NULL;
        instance.deferred_mutex = NULL;
        instance.deferred_semaphore = NULL;
        instance.deferred_event = NULL;
        instance.deferred_semaphore_count = 0;
        instance.deferred_library = NULL;
        if (work->runs_long) m98_CallbackMayRunLong((PTP_CALLBACK_INSTANCE)&instance);
        if (work->simple_callback)
            work->simple_callback((PTP_CALLBACK_INSTANCE)&instance, work->context);
        else
            work->callback((PTP_CALLBACK_INSTANCE)&instance, work->context,
                           (PTP_WORK)work);
        if (work->finalization)
            work->finalization((PTP_CALLBACK_INSTANCE)&instance, work->context);
        m98_tp_cleanup(&instance);
        instance.magic = 0;

        EnterCriticalSection(&m98_tp_lock);
        if (instance.associated) --work->running;
        --m98_tp_busy;
        --work->references;
        if (!work->pending && !work->running) SetEvent(work->idle_event);
        destroy = work->closed && !work->references;
        LeaveCriticalSection(&m98_tp_lock);
        if (destroy) m98_tp_destroy_work(work);
    }
}

static BOOL m98_tp_grow_workers(DWORD target)
{
    DWORD error = 0;
    while (m98_tp_workers < target) {
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
    if (m98_tp_workers < target) {
        SetLastError(error ? error : ERROR_GEN_FAILURE);
        return FALSE;
    }
    return TRUE;
}

static BOOL m98_tp_ensure_workers(void)
{
    if (!m98_tp_monitor_started) {
        DWORD thread_id;
        HANDLE thread = CreateThread(NULL, 0, m98_tp_monitor, NULL, 0, &thread_id);
        if (!thread) return FALSE;
        CloseHandle(thread);
        m98_tp_monitor_started = TRUE;
    }
    m98_tp_grow_workers(M98_TP_WORKER_COUNT);
    return m98_tp_workers != 0;
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
    m98_tp_growth_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!m98_tp_growth_event) {
        DWORD error = GetLastError();
        CloseHandle(m98_tp_semaphore);
        DeleteCriticalSection(&m98_tp_lock);
        SetLastError(error ? error : ERROR_GEN_FAILURE);
        return FALSE;
    }
    m98_tp_first = m98_tp_last = NULL;
    m98_tp_workers = 0;
    m98_tp_busy = 0;
    m98_tp_monitor_started = m98_tp_retry_growth = FALSE;
    m98_tp_shutdown = FALSE;
    m98_tp_initialized = TRUE;
    return TRUE;
}

void m98_tp_process_detach(BOOL process_terminating)
{
    DWORD i;
    /* ExitProcess has already terminated the other threads. A killed worker
     * may own this lock forever; the OS reclaims process resources, so do not
     * acquire locks or signal workers from process-termination DllMain. */
    if (process_terminating || !m98_tp_initialized) return;
    /* DllMain holds the loader lock: never wait for callback threads here.
     * KernelEx keeps this API library loaded for the process lifetime. */
    EnterCriticalSection(&m98_tp_lock);
    m98_tp_shutdown = TRUE;
    LeaveCriticalSection(&m98_tp_lock);
    for (i = 0; i < m98_tp_workers; ++i)
        ReleaseSemaphore(m98_tp_semaphore, 1, NULL);
    SetEvent(m98_tp_growth_event);
}

static PTP_WORK m98_tp_create(PTP_WORK_CALLBACK callback,
                              PTP_SIMPLE_CALLBACK simple_callback,
                              PVOID context, PTP_CALLBACK_ENVIRON environment)
{
    m98_tp_work *work;
    if (!callback && !simple_callback) {
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
    work->simple_callback = simple_callback;
    work->context = context;
    work->finalization = environment ? environment->FinalizationCallback : NULL;
    work->runs_long = environment && (environment->u.Flags & 1);
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

PTP_WORK WINAPI m98_CreateThreadpoolWork(PTP_WORK_CALLBACK callback,
                                         PVOID context,
                                         PTP_CALLBACK_ENVIRON environment)
{
    return m98_tp_create(callback, NULL, context, environment);
}

BOOL WINAPI m98_TrySubmitThreadpoolCallback(PTP_SIMPLE_CALLBACK callback,
                                            PVOID context,
                                            PTP_CALLBACK_ENVIRON environment)
{
    PTP_WORK work = m98_tp_create(NULL, callback, context, environment);
    if (!work) return FALSE;
    m98_SubmitThreadpoolWork(work);
    m98_CloseThreadpoolWork(work);
    return TRUE;
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
    m98_tp_callback_instance *instance = m98_tp_instance(opaque);
    if (!instance || !module) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return;
    }
    /* Windows/Wine keep one deferred library per callback instance. */
    if (!instance->deferred_library)
        instance->deferred_library = module;
}

void WINAPI m98_LeaveCriticalSectionWhenCallbackReturns(PTP_CALLBACK_INSTANCE opaque,
                                                        PCRITICAL_SECTION section)
{
    m98_tp_callback_instance *instance = m98_tp_instance(opaque);
    if (instance && !instance->deferred_section) instance->deferred_section = section;
}

void WINAPI m98_ReleaseMutexWhenCallbackReturns(PTP_CALLBACK_INSTANCE opaque, HANDLE mutex)
{
    m98_tp_callback_instance *instance = m98_tp_instance(opaque);
    if (instance && !instance->deferred_mutex) instance->deferred_mutex = mutex;
}

void WINAPI m98_ReleaseSemaphoreWhenCallbackReturns(PTP_CALLBACK_INSTANCE opaque,
                                                    HANDLE semaphore, DWORD count)
{
    m98_tp_callback_instance *instance = m98_tp_instance(opaque);
    if (instance && !instance->deferred_semaphore) {
        instance->deferred_semaphore = semaphore;
        instance->deferred_semaphore_count = count;
    }
}

void WINAPI m98_SetEventWhenCallbackReturns(PTP_CALLBACK_INSTANCE opaque, HANDLE event)
{
    m98_tp_callback_instance *instance = m98_tp_instance(opaque);
    if (instance && !instance->deferred_event) instance->deferred_event = event;
}

/* Disassociation releases waiters but does not drop the callback's reference.
 * Native Windows and both reviewed upstreams retain the object through actual
 * callback return, finalization and resource cleanup. */
void WINAPI m98_DisassociateCurrentThreadFromCallback(PTP_CALLBACK_INSTANCE opaque)
{
    m98_tp_callback_instance *instance = m98_tp_instance(opaque);
    m98_tp_work *work;
    if (!instance || !instance->associated) return;
    work = instance->work;
    EnterCriticalSection(&m98_tp_lock);
    instance->associated = FALSE;
    --work->running;
    if (!work->pending && !work->running) SetEvent(work->idle_event);
    LeaveCriticalSection(&m98_tp_lock);
}

BOOL WINAPI m98_CallbackMayRunLong(PTP_CALLBACK_INSTANCE opaque)
{
    m98_tp_callback_instance *instance = m98_tp_instance(opaque);
    BOOL available = TRUE;
    if (!instance) return FALSE;
    if (instance->may_run_long) return TRUE;
    EnterCriticalSection(&m98_tp_lock);
    if (m98_tp_busy >= m98_tp_workers) {
        if (m98_tp_workers < M98_TP_MAX_WORKERS)
            available = m98_tp_grow_workers(m98_tp_workers + 1);
        else {
            SetLastError(ERROR_MAX_THRDS_REACHED);
            available = FALSE;
        }
        if (!available) m98_tp_retry_growth = TRUE;
    }
    instance->may_run_long = TRUE;
    LeaveCriticalSection(&m98_tp_lock);
    return available;
}
