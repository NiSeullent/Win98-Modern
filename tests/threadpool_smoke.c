/* Direct API-table test: lifecycle, parallel callbacks, cancel and deferred unload. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98_threadpool.h"

#ifdef M98_STATIC
__declspec(dllimport) PTP_WORK WINAPI CreateThreadpoolWork(
    PTP_WORK_CALLBACK, PVOID, PTP_CALLBACK_ENVIRON);
__declspec(dllimport) void WINAPI SubmitThreadpoolWork(PTP_WORK);
__declspec(dllimport) void WINAPI WaitForThreadpoolWorkCallbacks(PTP_WORK, BOOL);
__declspec(dllimport) void WINAPI CloseThreadpoolWork(PTP_WORK);
__declspec(dllimport) void WINAPI FreeLibraryWhenCallbackReturns(
    PTP_CALLBACK_INSTANCE, HMODULE);
#endif

typedef const m98_api_table *(*get_table_fn)(void);
typedef PTP_WORK (WINAPI *create_fn)(PTP_WORK_CALLBACK, PVOID, PTP_CALLBACK_ENVIRON);
typedef void (WINAPI *submit_fn)(PTP_WORK);
typedef void (WINAPI *wait_fn)(PTP_WORK, BOOL);
typedef void (WINAPI *close_fn)(PTP_WORK);
typedef void (WINAPI *free_fn)(PTP_CALLBACK_INSTANCE, HMODULE);

typedef struct api_ops {
    create_fn create;
    submit_fn submit;
    wait_fn wait;
    close_fn close;
    free_fn free_library;
} api_ops;

typedef struct gate_state {
    volatile LONG started, completed;
    LONG target;
    HANDLE started_event, release_event, completed_event;
    HMODULE deferred_library;
    free_fn free_library;
} gate_state;

typedef struct wait_state {
    wait_fn wait;
    PTP_WORK work;
    HANDLE done;
} wait_state;

typedef struct environment_v3 {
    TP_CALLBACK_ENVIRON prefix;
    TP_CALLBACK_PRIORITY priority;
    DWORD size;
} environment_v3;

static volatile LONG finalization_count;

static void say(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, NULL);
}

static void say_hex(DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char result[11] = "0x00000000";
    int i;
    for (i = 0; i < 8; ++i)
        result[9 - i] = digits[(value >> (i * 4)) & 15];
    say(result);
}

static void fail(const char *message)
{
    say("FAIL: ");
    say(message);
    say("\r\n");
    ExitProcess(1);
}

#ifndef M98_STATIC
static BOOL same(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void *find_api(const m98_api_table *table, const char *name)
{
    int i;
    for (i = 0; i < table->named_apis_count; ++i)
        if (same(table->named_apis[i].name, name))
            return (void *)(ULONG_PTR)table->named_apis[i].addr;
    return NULL;
}
#endif

static void gate_open(gate_state *state, LONG target)
{
    state->started = state->completed = 0;
    state->target = target;
    state->deferred_library = NULL;
    state->free_library = NULL;
    state->started_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    state->release_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    state->completed_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!state->started_event || !state->release_event || !state->completed_event)
        fail("gate event allocation");
}

static void gate_close(gate_state *state)
{
    CloseHandle(state->started_event);
    CloseHandle(state->release_event);
    CloseHandle(state->completed_event);
}

static VOID CALLBACK gate_callback(PTP_CALLBACK_INSTANCE instance, PVOID opaque,
                                   PTP_WORK work)
{
    gate_state *state = (gate_state *)opaque;
    (void)work;
    if (InterlockedIncrement(&state->started) == state->target)
        SetEvent(state->started_event);
    WaitForSingleObject(state->release_event, 10000);
    if (state->deferred_library)
        state->free_library(instance, state->deferred_library);
    if (InterlockedIncrement(&state->completed) == state->target)
        SetEvent(state->completed_event);
}

static DWORD WINAPI wait_worker(void *opaque)
{
    wait_state *state = (wait_state *)opaque;
    state->wait(state->work, FALSE);
    SetEvent(state->done);
    return 0;
}

static VOID CALLBACK finalization_callback(PTP_CALLBACK_INSTANCE instance,
                                           PVOID opaque)
{
    (void)instance;
    (void)opaque;
    InterlockedIncrement(&finalization_count);
}

static void run_smoke(const api_ops *ops)
{
    gate_state state, blockers, victim, queued, closing, deferred;
    PTP_WORK work, blocked_work, victim_work, queued_work, closing_work, deferred_work;
    wait_state waiter;
    HANDLE waiter_thread;
    TP_CALLBACK_ENVIRON env;
    environment_v3 env3;
    HMODULE marker;
    int i;

    SetLastError(0);
    if (ops->create(NULL, NULL, NULL) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("null callback must fail explicitly");

    env.Version = 1;
    env.Pool = (PTP_POOL)(ULONG_PTR)1;
    env.CleanupGroup = NULL;
    env.CleanupGroupCancelCallback = NULL;
    env.RaceDll = NULL;
    env.ActivationContext = NULL;
    env.FinalizationCallback = NULL;
    env.u.Flags = 0;
    SetLastError(0);
    if (ops->create(gate_callback, NULL, &env) || GetLastError() != ERROR_NOT_SUPPORTED)
        fail("custom callback pool must fail explicitly");
    env.Pool = NULL;
    work = ops->create(gate_callback, NULL, &env);
    if (!work) fail("default V1 callback environment");
    ops->close(work);

    gate_open(&state, 1);
    SetEvent(state.release_event);
    finalization_count = 0;
    env3.prefix = env;
    env3.prefix.Version = 3;
    env3.prefix.FinalizationCallback = finalization_callback;
    env3.priority = TP_CALLBACK_PRIORITY_NORMAL;
    env3.size = sizeof(env3);
    work = ops->create(gate_callback, &state, (PTP_CALLBACK_ENVIRON)&env3);
    if (!work) fail("default V3 callback environment");
    ops->submit(work);
    ops->wait(work, FALSE);
    if (state.completed != 1 || finalization_count != 1)
        fail("V3 finalization callback did not run");
    ops->close(work);
    gate_close(&state);

    gate_open(&state, 2);
    work = ops->create(gate_callback, &state, NULL);
    if (!work) fail("create parallel work");
    ops->submit(work);
    ops->submit(work);
    if (WaitForSingleObject(state.started_event, 10000) != WAIT_OBJECT_0)
        fail("two callbacks did not run in parallel");
    waiter.wait = ops->wait;
    waiter.work = work;
    waiter.done = CreateEventA(NULL, TRUE, FALSE, NULL);
    waiter_thread = CreateThread(NULL, 0, wait_worker, &waiter, 0, NULL);
    if (!waiter.done || !waiter_thread) fail("waiter setup");
    if (WaitForSingleObject(waiter.done, 100) != WAIT_TIMEOUT)
        fail("wait returned before running callbacks completed");
    SetEvent(state.release_event);
    if (WaitForSingleObject(waiter.done, 10000) != WAIT_OBJECT_0 ||
        state.completed != 2)
        fail("wait did not join parallel callbacks");
    CloseHandle(waiter_thread);
    CloseHandle(waiter.done);
    ops->close(work);
    gate_close(&state);

    gate_open(&blockers, 4);
    gate_open(&victim, 1);
    gate_open(&queued, 2);
    blocked_work = ops->create(gate_callback, &blockers, NULL);
    victim_work = ops->create(gate_callback, &victim, NULL);
    queued_work = ops->create(gate_callback, &queued, NULL);
    if (!blocked_work || !victim_work || !queued_work)
        fail("cancel and close work setup");
    for (i = 0; i < 4; ++i) ops->submit(blocked_work);
    if (WaitForSingleObject(blockers.started_event, 10000) != WAIT_OBJECT_0)
        fail("four workers did not enter blocking callbacks");
    for (i = 0; i < 3; ++i) ops->submit(victim_work);
    ops->wait(victim_work, TRUE);
    if (victim.started || victim.completed)
        fail("wait(cancel) did not cancel pending callbacks");
    ops->close(victim_work);
    SetEvent(queued.release_event);
    ops->submit(queued_work);
    ops->submit(queued_work);
    ops->close(queued_work); /* Queued callbacks must still execute. */
    SetEvent(blockers.release_event);
    ops->wait(blocked_work, FALSE);
    if (WaitForSingleObject(queued.completed_event, 10000) != WAIT_OBJECT_0 ||
        queued.completed != 2 || blockers.completed != 4 || victim.started)
        fail("queued work after close or canceled work behavior");
    ops->close(blocked_work);
    gate_close(&queued);
    gate_close(&victim);
    gate_close(&blockers);

    gate_open(&closing, 1);
    closing_work = ops->create(gate_callback, &closing, NULL);
    if (!closing_work) fail("close work setup");
    ops->submit(closing_work);
    if (WaitForSingleObject(closing.started_event, 10000) != WAIT_OBJECT_0)
        fail("close callback did not start");
    ops->close(closing_work); /* Must return while the callback is active. */
    if (closing.completed) fail("close unexpectedly joined running callback");
    SetEvent(closing.release_event);
    if (WaitForSingleObject(closing.completed_event, 10000) != WAIT_OBJECT_0)
        fail("callback did not survive close");
    gate_close(&closing);

    marker = LoadLibraryA("TPMARK.DLL");
    if (!marker) {
        DWORD error = GetLastError();
        say("FAIL: load deferred-unload TPMARK.DLL error=");
        say_hex(error);
        say("\r\n");
        ExitProcess(1);
    }
    gate_open(&deferred, 1);
    deferred.deferred_library = marker;
    deferred.free_library = ops->free_library;
    deferred_work = ops->create(gate_callback, &deferred, NULL);
    if (!deferred_work) fail("deferred unload work setup");
    ops->submit(deferred_work);
    if (WaitForSingleObject(deferred.started_event, 10000) != WAIT_OBJECT_0)
        fail("deferred unload callback did not start");
    if (!GetModuleHandleA("TPMARK.DLL"))
        fail("module unloaded before callback returned");
    SetEvent(deferred.release_event);
    ops->wait(deferred_work, FALSE);
    if (GetModuleHandleA("TPMARK.DLL"))
        fail("FreeLibrary did not run after callback return");
    ops->close(deferred_work);
    gate_close(&deferred);
}

void mainCRTStartup(void)
{
#ifdef M98_STATIC
    api_ops ops = { CreateThreadpoolWork, SubmitThreadpoolWork,
                    WaitForThreadpoolWorkCallbacks, CloseThreadpoolWork,
                    FreeLibraryWhenCallbackReturns };
#else
#ifdef M98_INTEGRATED
    HMODULE module = LoadLibraryA("m98wrap.dll");
#else
    HMODULE module = LoadLibraryA("threadpool_fixture.dll");
#endif
    get_table_fn table_fn;
    const m98_api_table *table;
    api_ops ops;
    if (!module) fail("load threadpool provider DLL");
    table_fn = (get_table_fn)(ULONG_PTR)GetProcAddress(module, "get_api_table");
    if (!table_fn) fail("fixture API table export");
    table = table_fn();
    if (!table || !same(table->target_library, "KERNEL32.DLL") ||
#ifdef M98_INTEGRATED
        table->named_apis_count < 5)
#else
        table->named_apis_count != 12)
#endif
        fail("KERNEL32 API table");
    ops.close = (close_fn)find_api(table, "CloseThreadpoolWork");
    ops.create = (create_fn)find_api(table, "CreateThreadpoolWork");
    ops.free_library = (free_fn)find_api(table, "FreeLibraryWhenCallbackReturns");
    ops.submit = (submit_fn)find_api(table, "SubmitThreadpoolWork");
    ops.wait = (wait_fn)find_api(table, "WaitForThreadpoolWorkCallbacks");
    if (!ops.create || !ops.submit || !ops.close || !ops.wait ||
        !ops.free_library)
        fail("missing threadpool work API");
#endif
    run_smoke(&ops);
#ifdef M98_STATIC
    say("PASS: static Win98 threadpool lifecycle, parallelism, cancel, deferred unload\r\n");
#else
    say("PASS: Win98 threadpool work lifecycle, parallelism, cancel, deferred unload\r\n");
#endif
    ExitProcess(0);
}
