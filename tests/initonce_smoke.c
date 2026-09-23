/* Same semantic suite built for fixture, integrated table, and static imports.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef BOOL (WINAPI *begin_fn)(LPINIT_ONCE, DWORD, PBOOL, LPVOID *);
typedef BOOL (WINAPI *complete_fn)(LPINIT_ONCE, DWORD, LPVOID);
typedef BOOL (WINAPI *execute_fn)(PINIT_ONCE, PINIT_ONCE_FN, PVOID, LPVOID *);
typedef VOID (WINAPI *initialize_fn)(PINIT_ONCE);
static begin_fn once_begin;
static complete_fn once_complete;
static execute_fn once_execute;
static initialize_fn once_initialize;

#ifdef M98_STATIC_IMPORT
__declspec(dllimport) BOOL WINAPI InitOnceBeginInitialize(LPINIT_ONCE, DWORD, PBOOL, LPVOID *);
__declspec(dllimport) BOOL WINAPI InitOnceComplete(LPINIT_ONCE, DWORD, LPVOID);
__declspec(dllimport) BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE, PINIT_ONCE_FN, PVOID, LPVOID *);
__declspec(dllimport) VOID WINAPI InitOnceInitialize(PINIT_ONCE);
#endif

static void say(const char *text)
{
    DWORD count = 0, written;
    while (text[count]) ++count;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, count, &written, NULL);
}

static void require(BOOL good, const char *what)
{
    if (!good) {
        say("FAIL: "); say(what); say("\r\n");
        ExitProcess(1);
    }
}

static void report_hex(DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[11] = "0x00000000";
    int i;
    for (i = 0; i < 8; ++i) text[9 - i] = digits[(value >> (4 * i)) & 15];
    say(text);
}

static void load_functions(void)
{
#ifdef M98_STATIC_IMPORT
    once_begin = InitOnceBeginInitialize;
    once_complete = InitOnceComplete;
    once_execute = InitOnceExecuteOnce;
    once_initialize = InitOnceInitialize;
#else
    HMODULE module;
    typedef const m98_api_table *(__cdecl *table_fn)(void);
    table_fn get_table;
    const m98_api_table *table;
    int i;
#ifdef M98_INTEGRATED
    module = LoadLibraryA("M98WRAP.DLL");
#else
    module = LoadLibraryA("ONCEFIX.DLL");
#endif
    require(module != NULL, "load InitOnce table DLL");
    get_table = (table_fn)(ULONG_PTR)GetProcAddress(module, "get_api_table");
    require(get_table != NULL, "InitOnce get_api_table export");
    table = get_table();
    require(table != NULL && !lstrcmpA(table->target_library, "KERNEL32.DLL"),
            "KERNEL32 InitOnce table");
    for (i = 0; i < table->named_apis_count; ++i) {
        const m98_named_api *entry = &table->named_apis[i];
        if (!lstrcmpA(entry->name, "InitOnceBeginInitialize"))
            once_begin = (begin_fn)(ULONG_PTR)entry->addr;
        if (!lstrcmpA(entry->name, "InitOnceComplete"))
            once_complete = (complete_fn)(ULONG_PTR)entry->addr;
        if (!lstrcmpA(entry->name, "InitOnceExecuteOnce"))
            once_execute = (execute_fn)(ULONG_PTR)entry->addr;
        if (!lstrcmpA(entry->name, "InitOnceInitialize"))
            once_initialize = (initialize_fn)(ULONG_PTR)entry->addr;
    }
    require(once_begin && once_complete && once_execute && once_initialize,
            "four interoperable InitOnce APIs");
#endif
}

static LONG calls;
static BOOL WINAPI count_callback(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    InterlockedIncrement(&calls);
    if (context) *context = parameter;
    return TRUE;
}

static BOOL WINAPI fail_callback(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once; (void)parameter;
    InterlockedIncrement(&calls);
    if (context) *context = (PVOID)0x80001230UL;
    SetLastError(12345);
    return FALSE;
}

static BOOL WINAPI null_callback(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    require(context == NULL, "ExecuteOnce preserves NULL callback context argument");
    *(LONG *)parameter = 1;
    return TRUE;
}

static void basic_contracts(void)
{
    INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    BOOL pending = 123;
    PVOID context = (PVOID)0x1234;
    LONG null_called = 0;
    static const DWORD bad_begin[] = { 3, 4, 8, 0xffffffffUL };
    static const DWORD bad_complete[] = { 1, 3, 6, 8, 0xffffffffUL };
    unsigned i;
    once.Ptr = (PVOID)0xdeadbeefUL;
    SetLastError(4321);
    once_initialize(&once);
    require(once.Ptr == NULL && GetLastError() == 4321,
            "InitOnceInitialize clears storage and preserves LastError");
    for (i = 0; i < sizeof(bad_begin) / sizeof(bad_begin[0]); ++i) {
        SetLastError(0);
        require(!once_begin(&once, bad_begin[i], &pending, &context) &&
                GetLastError() == ERROR_INVALID_PARAMETER && pending == 123 &&
                context == (PVOID)0x1234 && once.Ptr == NULL,
                "invalid Begin flags preserve once and outputs");
    }
    require(!once_begin(&once, INIT_ONCE_CHECK_ONLY, &pending, &context) &&
            GetLastError() == ERROR_GEN_FAILURE && pending == 123 &&
            context == (PVOID)0x1234 && once.Ptr == NULL,
            "CHECK_ONLY uninitialized returns ERROR_GEN_FAILURE and preserves outputs");
    require(!once_complete(&once, 0, NULL) && GetLastError() == ERROR_GEN_FAILURE,
            "Complete without Begin fails");
    SetLastError(4321);
    require(once_begin(&once, 0, &pending, &context) && pending &&
            context == (PVOID)0x1234 && GetLastError() == 4321,
            "Begin claims initialization and leaves context and LastError untouched");
    pending = 123;
    require(!once_begin(&once, INIT_ONCE_CHECK_ONLY, &pending, &context) &&
            pending == 123 && context == (PVOID)0x1234 && GetLastError() == ERROR_GEN_FAILURE,
            "CHECK_ONLY active initialization does not wait or change outputs");
    require(!once_begin(&once, INIT_ONCE_ASYNC, &pending, &context) &&
            GetLastError() == ERROR_INVALID_PARAMETER,
            "async Begin cannot join active synchronous initialization");
    for (i = 0; i < sizeof(bad_complete) / sizeof(bad_complete[0]); ++i)
        require(!once_complete(&once, bad_complete[i], NULL) &&
                GetLastError() == ERROR_INVALID_PARAMETER,
                "invalid Complete flags rejected");
    require(!once_complete(&once, INIT_ONCE_ASYNC, NULL) &&
            GetLastError() == ERROR_INVALID_PARAMETER,
            "async Complete cannot finish synchronous Begin");
    require(!once_complete(&once, INIT_ONCE_INIT_FAILED, (PVOID)0x1234) &&
            GetLastError() == ERROR_INVALID_PARAMETER,
            "failed Complete cannot publish a non-NULL context");
    for (i = 1; i < 4; ++i)
        require(!once_complete(&once, 0, (PVOID)(ULONG_PTR)(0x1234 + i)) &&
                GetLastError() == ERROR_INVALID_PARAMETER,
                "all misaligned context values rejected without completing");
    SetLastError(4321);
    require(once_complete(&once, 0, (PVOID)0x80001230UL) && GetLastError() == 4321,
            "Complete accepts aligned high-bit context and preserves LastError");
    require(once_begin(&once, INIT_ONCE_CHECK_ONLY, &pending, &context) &&
            !pending && context == (PVOID)0x80001230UL,
            "CHECK_ONLY returns published aligned high-bit context");
    calls = 0;
    require(once_execute(&once, count_callback, NULL, &context) && calls == 0 &&
            context == (PVOID)0x80001230UL,
            "ExecuteOnce consumes prior Begin/Complete result without callback");
    require(!once_complete(&once, 0, NULL) && GetLastError() == ERROR_GEN_FAILURE,
            "duplicate completion fails");

    once_initialize(&once);
    context = NULL;
    calls = 0;
    require(!once_execute(&once, fail_callback, NULL, &context) && calls == 1 &&
            GetLastError() == 12345 && once.Ptr == NULL,
            "callback failure preserves LastError and permits retry");
    require(once_execute(&once, count_callback, (PVOID)0x4000, &context) &&
            calls == 2 && context == (PVOID)0x4000,
            "callback retry publishes context");
    require(once_begin(&once, 0, &pending, &context) && !pending &&
            context == (PVOID)0x4000,
            "Begin observes ExecuteOnce result");
    once_initialize(&once);
    require(once_execute(&once, null_callback, &null_called, NULL) && null_called,
            "ExecuteOnce accepts omitted context output");
    context = (PVOID)0x1234;
    require(once_begin(&once, INIT_ONCE_CHECK_ONLY, &pending, &context) &&
            !pending && context == NULL, "NULL context is a completed initialization");

    once_initialize(&once);
    require(once_begin(&once, INIT_ONCE_ASYNC, &pending, NULL) && pending,
            "async Begin starts without context output");
    require(!once_begin(&once, 0, &pending, NULL) &&
            GetLastError() == ERROR_INVALID_PARAMETER,
            "synchronous Begin rejects active async initialization");
#ifndef M98_STATIC_IMPORT
    /* Current native Windows terminates an isolated probe with
     * STATUS_INVALID_PARAMETER_2 for this invalid ExecuteOnce use. This port
     * reports ERROR_INVALID_PARAMETER without invoking the callback. */
    require(!once_execute(&once, count_callback, NULL, NULL) && calls == 2 &&
            GetLastError() == ERROR_INVALID_PARAMETER,
            "ExecuteOnce rejects active async initialization without callback");
#endif
    require(!once_complete(&once, 0, NULL) && GetLastError() == ERROR_INVALID_PARAMETER,
            "synchronous Complete rejects active async initialization");
    require(!once_complete(&once, INIT_ONCE_INIT_FAILED, NULL) &&
            GetLastError() == ERROR_INVALID_PARAMETER,
            "async initialization cannot be reset with INIT_FAILED");
    require(once_complete(&once, INIT_ONCE_ASYNC, NULL), "async Complete succeeds");
    require(once_begin(&once, 0, &pending, NULL) && !pending,
            "completed async result can be read synchronously");
}

#define WORKERS 12
typedef struct worker_arg { DWORD id; BOOL async_mode; } worker_arg;
static INIT_ONCE race_once;
static HANDLE start_event, complete_event;
static volatile LONG ready, finished, owners, winners;
static LONG payload, candidates[WORKERS];
static PVOID winning_context;

static BOOL WINAPI racing_callback(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once; (void)parameter;
    InterlockedIncrement(&owners);
    Sleep(2);
    payload = 0x2468;
    *context = &payload;
    return TRUE;
}

static DWORD WINAPI race_worker(LPVOID opaque)
{
    worker_arg *arg = (worker_arg *)opaque;
    BOOL pending;
    PVOID context = NULL;
    require(WaitForSingleObject(start_event, 5000) == WAIT_OBJECT_0, "worker start event");
    if (arg->async_mode) {
        require(once_begin(&race_once, INIT_ONCE_ASYNC, &pending, &context) && pending,
                "parallel async Begin succeeds before any completion");
        InterlockedIncrement(&ready);
        require(WaitForSingleObject(complete_event, 5000) == WAIT_OBJECT_0,
                "async completion barrier");
        if (once_complete(&race_once, INIT_ONCE_ASYNC, &candidates[arg->id])) {
            winning_context = &candidates[arg->id];
            InterlockedIncrement(&winners);
        } else {
            DWORD error = GetLastError();
            if (error != ERROR_GEN_FAILURE && error != ERROR_ALREADY_EXISTS) {
                say("async loser error="); report_hex(error); say("\r\n");
            }
            require(error == ERROR_GEN_FAILURE || error == ERROR_ALREADY_EXISTS,
                    "losing async completion returns completed-state or CAS-race error");
        }
        require(once_begin(&race_once, INIT_ONCE_CHECK_ONLY, &pending, &context) && !pending,
                "async contenders can retrieve published result");
    } else {
        InterlockedIncrement(&ready);
        if (arg->id & 1) {
            require(once_begin(&race_once, 0, &pending, &context), "mixed worker Begin");
            if (pending) {
                require(racing_callback(&race_once, NULL, &context), "mixed owner initializer");
                require(once_complete(&race_once, 0, context), "mixed owner completion");
            }
        } else {
            require(once_execute(&race_once, racing_callback, NULL, &context),
                    "mixed worker ExecuteOnce");
        }
        require(context == &payload && payload == 0x2468,
                "synchronous waiter sees fully published context and data");
    }
    InterlockedIncrement(&finished);
    return 0;
}

static void wait_count(volatile LONG *counter, LONG count)
{
    DWORD started = GetTickCount();
    while (InterlockedCompareExchange(counter, 0, 0) != count) {
        require(GetTickCount() - started < 5000, "workers reach bounded barrier");
        Sleep(1);
    }
}

static void concurrency(BOOL asynchronous)
{
    HANDLE threads[WORKERS];
    worker_arg args[WORKERS];
    DWORD i;
    BOOL pending;
    PVOID context;
    once_initialize(&race_once);
    ready = finished = owners = winners = 0;
    payload = 0;
    winning_context = NULL;
    start_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    complete_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    require(start_event && complete_event, "race barrier allocation");
    if (!asynchronous)
        require(once_begin(&race_once, 0, &pending, NULL) && pending,
                "main owns initial synchronous attempt");
    for (i = 0; i < WORKERS; ++i) {
        args[i].id = i;
        args[i].async_mode = asynchronous;
        candidates[i] = (LONG)i;
        threads[i] = CreateThread(NULL, 0, race_worker, &args[i], 0, NULL);
        require(threads[i] != NULL, "race worker creation");
    }
    SetEvent(start_event);
    wait_count(&ready, WORKERS);
    if (!asynchronous) {
        Sleep(30);
        require(!finished && !owners, "synchronous contenders wait for original owner");
        require(once_complete(&race_once, INIT_ONCE_INIT_FAILED, NULL),
                "failed synchronous owner hands initialization to waiting contenders");
    } else SetEvent(complete_event);
    for (i = 0; i < WORKERS; ++i) {
        require(WaitForSingleObject(threads[i], 5000) == WAIT_OBJECT_0, "race worker joined");
        CloseHandle(threads[i]);
    }
    require(finished == WORKERS, "all contenders return");
    if (asynchronous) {
        require(winners == 1 && winning_context, "exactly one async completion wins");
        require(once_begin(&race_once, INIT_ONCE_CHECK_ONLY, &pending, &context) &&
                !pending && context == winning_context,
                "async winner context remains stable after all contenders finish");
    } else require(owners == 1, "mixed Begin and ExecuteOnce run one successful initializer");
    CloseHandle(start_event);
    CloseHandle(complete_event);
}

void mainCRTStartup(void)
{
    int round;
    load_functions();
    basic_contracts();
    for (round = 0; round < 8; ++round) {
        concurrency(FALSE);
        concurrency(TRUE);
    }
#ifdef M98_STATIC_IMPORT
    say("PASS: static KERNEL32 InitOnce four-API contracts and 192 worker races\r\n");
#elif defined(M98_INTEGRATED)
    say("PASS: integrated InitOnce four-API contracts and 192 worker races\r\n");
#else
    say("PASS: fixture InitOnce four-API contracts and 192 worker races\r\n");
#endif
    ExitProcess(0);
}
