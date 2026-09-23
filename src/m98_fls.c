/* Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 * Clean-room Win98 FLS bridge. Pinned Wine/ReactOS behavior is cited in
 * docs/FLS_PORT_IMPLEMENTATION.md; no upstream implementation code is copied.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "m98_fls.h"

#define M98_FLS_LIMIT 128
#define M98_FREE 0
#define M98_ACTIVE 1
#define M98_RETIRING 2
#define M98_LIVE 1
#define M98_DYING 2
#define M98_DEAD 3
#define M98_THREAD_RECORD 1
#define M98_FIBER_RECORD 2
#define M98_RETIRE_INDEX 1
#define M98_RUNDOWN_RECORD 2

typedef BOOL (WINAPI *M98_IS_THREAD_A_FIBER)(VOID);

typedef struct m98_slot {
    DWORD state;
    DWORD generation;
    DWORD callbacks;
    BOOL retirement_done;
    M98_FLS_CALLBACK callback;
    PVOID callback_allocation;
} M98_SLOT;

typedef struct m98_thread_context M98_THREAD_CONTEXT;
typedef struct m98_cleanup M98_CLEANUP;
typedef struct m98_record {
    struct m98_record *next;
    DWORD kind;
    DWORD state;
    DWORD references; /* list owner plus in-flight FlsFree callbacks */
    DWORD serial;
    LPVOID native_fiber;
    LPFIBER_START_ROUTINE start;
    BOOL pending_native_delete;
    DWORD generations[M98_FLS_LIMIT];
    PVOID values[M98_FLS_LIMIT];
} M98_RECORD;

struct m98_thread_context {
    M98_RECORD *thread_record;
    BOOL exiting;
    M98_CLEANUP *cleanup;
};

/* Stack-owned continuation frames. Routed thread termination drains the same
 * frames before abandoning their C call stacks. Nested exit never returns to
 * the interrupted caller. The TLS owner distinguishes thread incarnations;
 * no global thread-id lookup can recover an old thread's abandoned stack. */
struct m98_cleanup {
    M98_CLEANUP *next;
    M98_CLEANUP *all_next;
    M98_THREAD_CONTEXT *context;
    LPVOID native_fiber;
    DWORD kind, index, generation;
    M98_FLS_CALLBACK callback;
    PVOID callback_owner;
    M98_RECORD *record;
    BOOL callback_ok, wait_references;
    BOOL callback_reserved;
    DWORD callback_index, callback_generation;
};

typedef struct m98_callback_work {
    M98_FLS_CALLBACK callback;
    PVOID owner;
    PVOID value;
} M98_CALLBACK_WORK;

typedef struct m98_start_context {
    LPTHREAD_START_ROUTINE start;
    LPVOID parameter;
} M98_START_CONTEXT;

static CRITICAL_SECTION m98_lock;
static volatile LONG m98_init_state;
static DWORD m98_tls_index = TLS_OUT_OF_INDEXES;
static M98_IS_THREAD_A_FIBER m98_native_is_fiber;
static M98_SLOT m98_slots[M98_FLS_LIMIT];
static M98_RECORD *m98_records;
static M98_CLEANUP *m98_cleanups;
static DWORD m98_next_serial;

static BOOL m98_ready(void)
{
    LONG state = InterlockedCompareExchange(&m98_init_state, 1, 0);
    if (state == 0) {
        HMODULE kernel;
        InitializeCriticalSection(&m98_lock);
        m98_tls_index = TlsAlloc();
        if (m98_tls_index == TLS_OUT_OF_INDEXES) {
            DeleteCriticalSection(&m98_lock);
            InterlockedExchange(&m98_init_state, -1);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        kernel = GetModuleHandleA("KERNEL32.DLL");
        if (kernel)
            m98_native_is_fiber = (M98_IS_THREAD_A_FIBER)
                GetProcAddress(kernel, "IsThreadAFiber");
        m98_next_serial = 1;
        InterlockedExchange(&m98_init_state, 2);
        return TRUE;
    }
    while (state == 1) {
        Sleep(0);
        state = m98_init_state;
    }
    if (state == 2) return TRUE;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
}

static BOOL m98_is_current_fiber(void)
{
    if (m98_native_is_fiber) return m98_native_is_fiber();
    /* Directly installed Win98 SE: raw GetCurrentFiber is zero before
     * conversion. Modern hosts use IsThreadAFiber above instead. */
    return GetCurrentFiber() != NULL;
}

static M98_RECORD *m98_new_record(DWORD kind)
{
    M98_RECORD *record = (M98_RECORD *)HeapAlloc(GetProcessHeap(),
                                                 HEAP_ZERO_MEMORY,
                                                 sizeof(*record));
    if (!record) SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    else {
        record->kind = kind;
        record->state = M98_LIVE;
        record->references = 1;
    }
    return record;
}

static void m98_link_record_locked(M98_RECORD *record)
{
    record->serial = m98_next_serial++;
    record->next = m98_records;
    m98_records = record;
}

static void m98_unlink_record_locked(M98_RECORD *record)
{
    M98_RECORD **link = &m98_records;
    while (*link && *link != record) link = &(*link)->next;
    if (*link) *link = record->next;
    record->next = NULL;
}

static M98_RECORD *m98_find_fiber_locked(LPVOID fiber, BOOL live_only)
{
    M98_RECORD *record;
    for (record = m98_records; record; record = record->next)
        if (record->kind == M98_FIBER_RECORD &&
            record->native_fiber == fiber &&
            (!live_only || record->state == M98_LIVE)) return record;
    return NULL;
}

static M98_THREAD_CONTEXT *m98_current_context(BOOL create)
{
    M98_THREAD_CONTEXT *context;
    context = (M98_THREAD_CONTEXT *)TlsGetValue(m98_tls_index);
    if (context || !create) return context;
    context = (M98_THREAD_CONTEXT *)HeapAlloc(GetProcessHeap(),
                                              HEAP_ZERO_MEMORY,
                                              sizeof(*context));
    if (!context) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (!TlsSetValue(m98_tls_index, context)) {
        HeapFree(GetProcessHeap(), 0, context);
        return NULL;
    }
    return context;
}

static M98_RECORD *m98_current_record_locked(M98_THREAD_CONTEXT *context,
                                              BOOL native_fiber,
                                              LPVOID fiber)
{
    if (native_fiber) return m98_find_fiber_locked(fiber, TRUE);
    if (!context || context->exiting) return NULL;
    return context->thread_record && context->thread_record->state == M98_LIVE
               ? context->thread_record : NULL;
}

static BOOL m98_callback_mapped(M98_FLS_CALLBACK callback, PVOID owner)
{
    MEMORY_BASIC_INFORMATION info;
    if (!callback) return TRUE;
    if (VirtualQuery((LPCVOID)callback, &info, sizeof(info)) != sizeof(info))
        return FALSE;
    return info.State == MEM_COMMIT && info.AllocationBase == owner;
}

static BOOL m98_run_callback(const M98_CALLBACK_WORK *work)
{
    if (!work->callback || !work->value) return TRUE;
    if (!m98_callback_mapped(work->callback, work->owner)) {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }
    work->callback(work->value);
    return TRUE;
}

static void m98_finalize_record(M98_RECORD *record)
{
    BOOL native_delete = record->pending_native_delete;
    LPVOID fiber = record->native_fiber;
    HeapFree(GetProcessHeap(), 0, record);
    if (native_delete) DeleteFiber(fiber);
}

static void m98_link_cleanup_locked(M98_CLEANUP *operation)
{
    operation->native_fiber = m98_is_current_fiber() ? GetCurrentFiber() : NULL;
    operation->next = operation->context->cleanup;
    operation->context->cleanup = operation;
    operation->all_next = m98_cleanups;
    m98_cleanups = operation;
}

static void m98_unlink_cleanup_locked(M98_CLEANUP *operation)
{
    M98_CLEANUP **link = &operation->context->cleanup;
    while (*link && *link != operation) link = &(*link)->next;
    if (*link) *link = operation->next;
    link = &m98_cleanups;
    while (*link && *link != operation) link = &(*link)->all_next;
    if (*link) *link = operation->all_next;
}

static void m98_release_callback_record(M98_CLEANUP *operation)
{
    M98_RECORD *record = operation->record;
    BOOL finalize = FALSE;
    if (!record) return;
    EnterCriticalSection(&m98_lock);
    operation->record = NULL; /* exactly once, including recursive termination */
    finalize = --record->references == 0 && record->state == M98_DEAD;
    LeaveCriticalSection(&m98_lock);
    if (finalize) m98_finalize_record(record);
}

static void m98_finish_slot_locked(DWORD index)
{
    M98_SLOT *slot = &m98_slots[index];
    slot->state = M98_FREE;
    slot->callback = NULL;
    slot->callback_allocation = NULL;
    slot->retirement_done = FALSE;
    if (++slot->generation == 0) slot->generation = 1;
}

/* A detached rundown value is still callback work for this generation. It
 * must stay reserved across the unlocked call, otherwise a concurrent free
 * could return (and its owner could unload) before the callback even starts. */
static void m98_reserve_callback_locked(M98_CLEANUP *operation, DWORD index)
{
    operation->callback_reserved = TRUE;
    operation->callback_index = index;
    operation->callback_generation = m98_slots[index].generation;
    ++m98_slots[index].callbacks;
}

static void m98_release_callback_locked(M98_CLEANUP *operation)
{
    M98_SLOT *slot;
    if (!operation->callback_reserved) return;
    slot = &m98_slots[operation->callback_index];
    operation->callback_reserved = FALSE; /* normal return or abandonment */
    --slot->callbacks;
    if (!slot->callbacks && slot->retirement_done)
        m98_finish_slot_locked(operation->callback_index);
}

static DWORD m98_local_callbacks_locked(M98_CLEANUP *operation)
{
    M98_CLEANUP *frame;
    DWORD count = 0;
    for (frame = operation->context->cleanup; frame; frame = frame->next)
        if (frame->callback_reserved &&
            frame->callback_index == operation->index &&
            frame->callback_generation == operation->generation) ++count;
    return count;
}

static void m98_finish_retirement(M98_CLEANUP *operation)
{
    DWORD index = operation->index;
    for (;;) {
        M98_CALLBACK_WORK work;
        M98_RECORD *record = NULL;
        /* Normal callback return and abandoned callback return release the
         * same reference. Clear the continuation before calling any finalizer. */
        m98_release_callback_record(operation);
        EnterCriticalSection(&m98_lock);
        m98_release_callback_locked(operation);
        for (M98_RECORD *candidate = m98_records; candidate; candidate = candidate->next) {
            if (candidate->state != M98_DEAD &&
                candidate->generations[index] == operation->generation &&
                candidate->values[index]) { record = candidate; break; }
        }
        if (!record) {
            if (m98_slots[index].callbacks > m98_local_callbacks_locked(operation)) {
                LeaveCriticalSection(&m98_lock);
                Sleep(1);
                continue;
            }
            /* A callback can free its own index. Waiting on that suspended
             * caller would deadlock. Complete its retirement but hold the
             * generation until that local callback returns or is abandoned. */
            m98_slots[index].retirement_done = TRUE;
            if (!m98_slots[index].callbacks) m98_finish_slot_locked(index);
            m98_unlink_cleanup_locked(operation);
            LeaveCriticalSection(&m98_lock);
            return;
        }
        work.callback = operation->callback;
        work.owner = operation->callback_owner;
        work.value = record->values[index];
        record->values[index] = NULL;
        record->generations[index] = 0;
        ++record->references;
        operation->record = record;
        if (work.callback) m98_reserve_callback_locked(operation, index);
        LeaveCriticalSection(&m98_lock);
        if (!m98_run_callback(&work)) operation->callback_ok = FALSE;
    }
}

static void m98_finish_rundown(M98_CLEANUP *operation)
{
    M98_RECORD *record = operation->record;
    BOOL finalize;
    EnterCriticalSection(&m98_lock);
    m98_release_callback_locked(operation);
    while (operation->index < M98_FLS_LIMIT) {
        DWORD index = operation->index++;
        M98_CALLBACK_WORK work = { NULL, NULL, NULL };
        if (record->values[index] &&
            m98_slots[index].state != M98_FREE &&
            record->generations[index] == m98_slots[index].generation) {
            work.callback = m98_slots[index].callback;
            work.owner = m98_slots[index].callback_allocation;
            work.value = record->values[index];
            if (work.callback) m98_reserve_callback_locked(operation, index);
        }
        record->values[index] = NULL;
        record->generations[index] = 0;
        /* Detach only the callback about to run. A previous callback can free
         * and reuse another slot; caching all callbacks would invoke an old
         * generation after FlsFree had already returned. DYING records stay
         * linked so FlsFree can synchronously drain their remaining values. */
        LeaveCriticalSection(&m98_lock);
        m98_run_callback(&work);
        EnterCriticalSection(&m98_lock);
        m98_release_callback_locked(operation);
    }
    /* All locally interrupted retirement frames have been drained first.
     * Remaining references therefore belong to foreign callbacks. */
    while (operation->wait_references && record->references > 1) {
        LeaveCriticalSection(&m98_lock);
        Sleep(1);
        EnterCriticalSection(&m98_lock);
    }
    m98_unlink_record_locked(record);
    record->state = M98_DEAD;
    finalize = --record->references == 0;
    operation->record = NULL;
    m98_unlink_cleanup_locked(operation);
    LeaveCriticalSection(&m98_lock);
    if (finalize) m98_finalize_record(record);
}

static void m98_finish_cleanup(M98_CLEANUP *operation)
{
    if (operation->kind == M98_RETIRE_INDEX) m98_finish_retirement(operation);
    else m98_finish_rundown(operation);
}

static void m98_rundown_record(M98_RECORD *record, BOOL native_delete,
                               BOOL wait_references, M98_THREAD_CONTEXT *context)
{
    M98_CLEANUP operation = { 0 };
    EnterCriticalSection(&m98_lock);
    if (record->state != M98_LIVE) {
        LeaveCriticalSection(&m98_lock);
        return;
    }
    record->state = M98_DYING;
    record->pending_native_delete = native_delete;
    operation.kind = M98_RUNDOWN_RECORD;
    operation.record = record;
    operation.index = 1;
    operation.context = context;
    operation.wait_references = wait_references;
    m98_link_cleanup_locked(&operation);
    /* A nested exit can finish this record without returning to this frame. */
    if (context->thread_record == record) context->thread_record = NULL;
    LeaveCriticalSection(&m98_lock);
    m98_finish_rundown(&operation);
}

DWORD WINAPI m98_FlsAlloc(M98_FLS_CALLBACK callback)
{
    DWORD index;
    MEMORY_BASIC_INFORMATION info;
    if (!m98_ready()) return FLS_OUT_OF_INDEXES;
    if (callback &&
        (VirtualQuery((LPCVOID)callback, &info, sizeof(info)) != sizeof(info)
         || info.State != MEM_COMMIT)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FLS_OUT_OF_INDEXES;
    }
    EnterCriticalSection(&m98_lock);
    for (index = 1; index < M98_FLS_LIMIT; ++index)
        if (m98_slots[index].state == M98_FREE) break;
    if (index < M98_FLS_LIMIT) {
        M98_SLOT *slot = &m98_slots[index];
        if (!slot->generation) slot->generation = 1;
        slot->state = M98_ACTIVE;
        slot->callback = callback;
        slot->callback_allocation = callback ? info.AllocationBase : NULL;
    }
    LeaveCriticalSection(&m98_lock);
    if (index == M98_FLS_LIMIT) {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FLS_OUT_OF_INDEXES;
    }
    return index;
}

BOOL WINAPI m98_FlsFree(DWORD index)
{
    M98_CLEANUP operation = { 0 };
    M98_THREAD_CONTEXT *context;
    if (!m98_ready()) return FALSE;
    if (!index || index >= M98_FLS_LIMIT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    context = m98_current_context(TRUE);
    if (!context) return FALSE;
    EnterCriticalSection(&m98_lock);
    if (m98_slots[index].state != M98_ACTIVE) {
        LeaveCriticalSection(&m98_lock);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    m98_slots[index].state = M98_RETIRING;
    operation.context = context;
    operation.kind = M98_RETIRE_INDEX;
    operation.index = index;
    operation.generation = m98_slots[index].generation;
    operation.callback = m98_slots[index].callback;
    operation.callback_owner = m98_slots[index].callback_allocation;
    operation.callback_ok = TRUE;
    m98_link_cleanup_locked(&operation);
    LeaveCriticalSection(&m98_lock);
    m98_finish_retirement(&operation);
    if (!operation.callback_ok) SetLastError(ERROR_INVALID_ADDRESS);
    return operation.callback_ok;
}

PVOID WINAPI m98_FlsGetValue(DWORD index)
{
    M98_THREAD_CONTEXT *context;
    M98_RECORD *record;
    LPVOID fiber;
    BOOL native_fiber;
    PVOID value = NULL;
    if (!m98_ready()) return NULL;
    if (!index || index >= M98_FLS_LIMIT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    native_fiber = m98_is_current_fiber();
    fiber = native_fiber ? GetCurrentFiber() : NULL;
    context = m98_current_context(FALSE);
    EnterCriticalSection(&m98_lock);
    if (m98_slots[index].state != M98_ACTIVE) {
        LeaveCriticalSection(&m98_lock);
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    record = m98_current_record_locked(context, native_fiber, fiber);
    if (native_fiber && !record) {
        LeaveCriticalSection(&m98_lock);
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    if (record && record->generations[index] == m98_slots[index].generation)
        value = record->values[index];
    LeaveCriticalSection(&m98_lock);
    SetLastError(ERROR_SUCCESS); /* valid empty slot is distinguishable */
    return value;
}

BOOL WINAPI m98_FlsSetValue(DWORD index, PVOID value)
{
    M98_THREAD_CONTEXT *context;
    M98_RECORD *record;
    BOOL native_fiber;
    LPVOID fiber;
    if (!m98_ready()) return FALSE;
    if (!index || index >= M98_FLS_LIMIT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    native_fiber = m98_is_current_fiber();
    fiber = native_fiber ? GetCurrentFiber() : NULL;
    context = m98_current_context(TRUE);
    if (!context) return FALSE;
    EnterCriticalSection(&m98_lock);
    if (m98_slots[index].state != M98_ACTIVE || context->exiting) {
        LeaveCriticalSection(&m98_lock);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    record = m98_current_record_locked(context, native_fiber, fiber);
    if (!record && !native_fiber) {
        record = m98_new_record(M98_THREAD_RECORD);
        if (record) {
            context->thread_record = record;
            m98_link_record_locked(record);
        }
    }
    if (!record) {
        LeaveCriticalSection(&m98_lock);
        if (native_fiber) SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    record->values[index] = value;
    record->generations[index] = m98_slots[index].generation;
    LeaveCriticalSection(&m98_lock);
    return TRUE;
}

LPVOID WINAPI m98_ConvertThreadToFiber(LPVOID parameter)
{
    M98_THREAD_CONTEXT *context;
    M98_RECORD *fiber_record, *old;
    LPVOID fiber;
    DWORD index;
    BOOL free_old = FALSE;
    if (!m98_ready()) return NULL;
    context = m98_current_context(TRUE);
    if (!context) return NULL;
    if (context->exiting) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    fiber_record = m98_new_record(M98_FIBER_RECORD);
    if (!fiber_record) return NULL;
    fiber = ConvertThreadToFiber(parameter);
    if (!fiber) {
        HeapFree(GetProcessHeap(), 0, fiber_record);
        return NULL;
    }
    fiber_record->native_fiber = fiber;
    EnterCriticalSection(&m98_lock);
    old = context->thread_record;
    if (old) {
        for (index = 1; index < M98_FLS_LIMIT; ++index) {
            fiber_record->values[index] = old->values[index];
            fiber_record->generations[index] = old->generations[index];
            old->values[index] = NULL;
            old->generations[index] = 0;
        }
        context->thread_record = NULL;
        m98_unlink_record_locked(old);
        old->state = M98_DEAD;
        free_old = --old->references == 0;
    }
    m98_link_record_locked(fiber_record);
    LeaveCriticalSection(&m98_lock);
    /* A concurrent FlsFree may drop its last reference immediately after
     * unlock. Never read old again unless this path owns final destruction. */
    if (free_old) HeapFree(GetProcessHeap(), 0, old);
    return fiber;
}

LPVOID WINAPI m98_ConvertThreadToFiberEx(LPVOID parameter, DWORD flags)
{
    if (flags) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return NULL;
    }
    return m98_ConvertThreadToFiber(parameter);
}

static VOID WINAPI m98_fiber_start(LPVOID parameter)
{
    LPVOID fiber = GetCurrentFiber();
    LPFIBER_START_ROUTINE start = NULL;
    M98_RECORD *record;
    EnterCriticalSection(&m98_lock);
    record = m98_find_fiber_locked(fiber, TRUE);
    if (record) start = record->start;
    LeaveCriticalSection(&m98_lock);
    if (!start) m98_ExitThread(ERROR_INVALID_HANDLE);
    start(parameter); /* Native GetFiberData still returns the original param. */
    m98_DeleteFiber(fiber); /* Returning from a fiber exits its thread. */
    m98_ExitThread(0); /* Only reachable after a deferred pathological delete. */
}

LPVOID WINAPI m98_CreateFiber(SIZE_T stack, LPFIBER_START_ROUTINE start,
                              LPVOID parameter)
{
    M98_RECORD *record;
    LPVOID fiber;
    if (!m98_ready()) return NULL;
    if (!start) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    record = m98_new_record(M98_FIBER_RECORD);
    if (!record) return NULL;
    record->start = start;
    fiber = CreateFiber(stack, m98_fiber_start, parameter);
    if (!fiber) {
        HeapFree(GetProcessHeap(), 0, record);
        return NULL;
    }
    record->native_fiber = fiber;
    EnterCriticalSection(&m98_lock);
    m98_link_record_locked(record);
    LeaveCriticalSection(&m98_lock);
    return fiber;
}

LPVOID WINAPI m98_CreateFiberEx(SIZE_T commit, SIZE_T reserve, DWORD flags,
                                LPFIBER_START_ROUTINE start, LPVOID parameter)
{
    if (flags || (commit && reserve && commit != reserve)) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return NULL;
    }
    return m98_CreateFiber(commit ? commit : reserve, start, parameter);
}

VOID WINAPI m98_SwitchToFiber(LPVOID fiber)
{
    SwitchToFiber(fiber);
}

VOID WINAPI m98_DeleteFiber(LPVOID fiber)
{
    M98_RECORD *record;
    M98_CLEANUP *cleanup;
    M98_THREAD_CONTEXT *context;
    BOOL current;
    if (!fiber) return;
    if (!m98_ready()) {
        DeleteFiber(fiber);
        return;
    }
    current = m98_is_current_fiber() && GetCurrentFiber() == fiber;
    if (current) {
        /* Retire interrupted FlsFree frames before waiting for references to
         * this fiber, otherwise the callback waits on its own reference. */
        m98_fls_rundown_current_thread();
        DeleteFiber(fiber);
        return;
    }
    context = m98_current_context(TRUE);
    if (!context) return; /* allocation error preserved; tracked fiber intact */
    EnterCriticalSection(&m98_lock);
    /* A suspended callback has a continuation on this fiber's native stack.
     * Deleting it from a different fiber would leave a dangling continuation.
     * Remote-fiber abandonment needs a separate ownership protocol; preserve
     * the fiber and report the unsupported busy state instead of freeing it. */
    for (cleanup = m98_cleanups; cleanup; cleanup = cleanup->all_next) {
        if (cleanup->native_fiber == fiber) {
            LeaveCriticalSection(&m98_lock);
            SetLastError(ERROR_BUSY);
            return;
        }
    }
    record = m98_find_fiber_locked(fiber, TRUE);
    if (!record) {
        M98_RECORD *dying = m98_find_fiber_locked(fiber, FALSE);
        LeaveCriticalSection(&m98_lock);
        if (dying) return; /* Reentrant deletion of the same instance. */
        DeleteFiber(fiber); /* Untracked native fiber has no bridge values. */
        return;
    }
    LeaveCriticalSection(&m98_lock);
    m98_rundown_record(record, TRUE, FALSE, context);
}

VOID m98_fls_rundown_current_thread(void)
{
    M98_THREAD_CONTEXT *context;
    M98_RECORD *fiber_record = NULL;
    LPVOID fiber;
    if (m98_init_state != 2) return;
    context = m98_current_context(FALSE);
    if (!context) return;
    context->exiting = TRUE;
    /* Stack continuations remain valid until native termination. A callback
     * can recursively request exit while this loop calls remaining callbacks:
     * that invocation resumes the same top frame and then never returns. */
    while (context->cleanup) m98_finish_cleanup(context->cleanup);
    fiber = m98_is_current_fiber() ? GetCurrentFiber() : NULL;
    if (fiber) {
        EnterCriticalSection(&m98_lock);
        fiber_record = m98_find_fiber_locked(fiber, TRUE);
        LeaveCriticalSection(&m98_lock);
    }
    if (fiber_record) m98_rundown_record(fiber_record, FALSE, TRUE, context);
    if (context->thread_record) {
        m98_rundown_record(context->thread_record, FALSE, TRUE, context);
    }
    TlsSetValue(m98_tls_index, NULL);
    HeapFree(GetProcessHeap(), 0, context);
}

static DWORD WINAPI m98_thread_start(LPVOID parameter)
{
    M98_START_CONTEXT *context = (M98_START_CONTEXT *)parameter;
    LPTHREAD_START_ROUTINE start = context->start;
    LPVOID user_parameter = context->parameter;
    DWORD code;
    HeapFree(GetProcessHeap(), 0, context);
    code = start(user_parameter);
    m98_fls_rundown_current_thread();
    return code;
}

HANDLE WINAPI m98_CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack,
                               LPTHREAD_START_ROUTINE start, LPVOID parameter,
                               DWORD flags, LPDWORD thread_id)
{
    M98_START_CONTEXT *context;
    HANDLE thread;
    DWORD local_thread_id, error;
    if (!start) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    context = (M98_START_CONTEXT *)HeapAlloc(GetProcessHeap(), 0,
                                              sizeof(*context));
    if (!context) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    context->start = start;
    context->parameter = parameter;
    thread = CreateThread(attributes, stack, m98_thread_start, context,
                          flags, thread_id ? thread_id : &local_thread_id);
    if (!thread) {
        error = GetLastError();
        HeapFree(GetProcessHeap(), 0, context);
        SetLastError(error);
    }
    return thread;
}

VOID WINAPI m98_ExitThread(DWORD code)
{
    m98_fls_rundown_current_thread();
    ExitThread(code);
}

VOID WINAPI m98_FreeLibraryAndExitThread(HMODULE module, DWORD code)
{
    /* Wine kernelbase/thread.c::FreeLibraryAndExitThread (df15af3) and
     * ReactOS kernel32/client/loader.c (9dc3ca8) both unload before exit.
     * The API provider must itself remain pinned while this code executes. */
    FreeLibrary(module);
    m98_ExitThread(code);
}
