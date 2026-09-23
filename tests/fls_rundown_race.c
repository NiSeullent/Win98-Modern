/* Deterministic FlsFree versus already-detached thread-rundown callback.
 * Original Win98 imports only; dynamic native FLS is an observational oracle.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98_fls.h"

typedef DWORD (WINAPI *alloc_fn)(M98_FLS_CALLBACK);
typedef BOOL (WINAPI *free_fn)(DWORD);
typedef BOOL (WINAPI *set_fn)(DWORD, PVOID);
typedef HANDLE (WINAPI *thread_fn)(LPSECURITY_ATTRIBUTES, SIZE_T,
                                  LPTHREAD_START_ROUTINE, LPVOID, DWORD,
                                  LPDWORD);
typedef LPVOID (WINAPI *convert_fn)(LPVOID);
typedef LPVOID (WINAPI *fiber_fn)(SIZE_T, LPFIBER_START_ROUTINE, LPVOID);
typedef VOID (WINAPI *switch_fn)(LPVOID);
typedef VOID (WINAPI *delete_fn)(LPVOID);

static alloc_fn fls_alloc;
static free_fn fls_free;
static set_fn fls_set;
static thread_fn create_thread;
static DWORD slot;
static int marker;
static HANDLE entered, release, free_entered, returned;
static volatile LONG callback_count, callback_done, free_ok, free_saw_done;
static convert_fn bridge_convert;
static fiber_fn bridge_create_fiber;
static switch_fn bridge_switch;
static delete_fn bridge_delete;
static LPVOID primary_fiber, child_fiber;
static DWORD child_delete_error;
static volatile LONG fiber_callback_count, fiber_callback_resumed;
static int fiber_marker;

static void say(const char *s)
{
    DWORD n = 0, written;
    while (s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
}

static void fail(const char *s)
{
    say("FAIL: "); say(s); say("\r\n");
    ExitProcess(1);
}

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void *find(const m98_api_table *table, const char *name)
{
    int i;
    for (i = 0; i < table->named_apis_count; ++i)
        if (equal(table->named_apis[i].name, name))
            return (void *)table->named_apis[i].addr;
    return NULL;
}

static VOID WINAPI callback(PVOID value)
{
    if (value != &marker) fail("callback value identity");
    InterlockedIncrement(&callback_count);
    SetEvent(entered);
    if (WaitForSingleObject(release, 10000) != WAIT_OBJECT_0)
        fail("blocked callback was not released");
    InterlockedExchange(&callback_done, 1);
}

static DWORD WINAPI owner_worker(LPVOID ignored)
{
    (void)ignored;
    if (!fls_set(slot, &marker)) fail("set FLS in owner thread");
    return 0x1357UL; /* Routed/native thread return begins rundown. */
}

static DWORD WINAPI free_worker(LPVOID ignored)
{
    (void)ignored;
    SetEvent(free_entered);
    free_ok = fls_free(slot);
    free_saw_done = callback_done;
    SetEvent(returned);
    return 0;
}

static BOOL run_case(const char *label, BOOL require_wait)
{
    HANDLE owner, freer;
    DWORD owner_id, free_id;
    BOOL early, okay;
    say("CASE: "); say(label); say("\r\n");
    callback_count = callback_done = free_ok = free_saw_done = 0;
    entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    release = CreateEventA(NULL, TRUE, FALSE, NULL);
    free_entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    returned = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!entered || !release || !free_entered || !returned)
        fail("create race events");
    slot = fls_alloc(callback);
    if (slot == FLS_OUT_OF_INDEXES) fail("FlsAlloc");
    owner = create_thread(NULL, 0, owner_worker, NULL, 0, &owner_id);
    if (!owner || WaitForSingleObject(entered, 10000) != WAIT_OBJECT_0)
        fail("owner callback entry");
    /* Callback has already detached its value and is blocked. The freeing
     * thread must not return while that in-flight callback still runs. */
    freer = create_thread(NULL, 0, free_worker, NULL, 0, &free_id);
    if (!freer) fail("create freeing thread");
    if (WaitForSingleObject(free_entered, 10000) != WAIT_OBJECT_0)
        fail("free worker did not reach FlsFree");
    early = WaitForSingleObject(returned, 250) == WAIT_OBJECT_0;
    say(early ? "OBSERVED: FlsFree returned while rundown callback blocked\r\n"
              : "OBSERVED: FlsFree waited for rundown callback\r\n");
    if (!SetEvent(release)) fail("release callback");
    if (WaitForSingleObject(owner, 10000) != WAIT_OBJECT_0 ||
        WaitForSingleObject(freer, 10000) != WAIT_OBJECT_0)
        fail("race threads completion");
    okay = free_ok && callback_count == 1 && callback_done && free_saw_done;
    CloseHandle(owner); CloseHandle(freer);
    CloseHandle(entered); CloseHandle(release);
    CloseHandle(free_entered); CloseHandle(returned);
    if (require_wait && (early || !okay)) fail("bridge FlsFree returned before callback completion");
    if (!require_wait && !okay)
        say("OBSERVED: native callback/free order differs from bridge policy\r\n");
    return !early && okay;
}

static VOID WINAPI fiber_delete_attempt(LPVOID ignored)
{
    (void)ignored;
    SetLastError(0);
    bridge_delete(primary_fiber);
    child_delete_error = GetLastError();
    bridge_switch(primary_fiber);
    fail("child fiber resumed after parent resumed");
}

static VOID WINAPI switching_callback(PVOID value)
{
    if (value != &fiber_marker || GetCurrentFiber() != primary_fiber)
        fail("fiber callback identity");
    InterlockedIncrement(&fiber_callback_count);
    bridge_switch(child_fiber);
    InterlockedIncrement(&fiber_callback_resumed);
}

static void run_paused_fiber_case(void)
{
    DWORD fiber_slot;
    primary_fiber = bridge_convert(NULL);
    if (!primary_fiber || GetCurrentFiber() != primary_fiber)
        fail("convert primary thread to fiber");
    child_fiber = bridge_create_fiber(0, fiber_delete_attempt, NULL);
    if (!child_fiber) fail("create child fiber");
    fiber_slot = fls_alloc(switching_callback);
    if (fiber_slot == FLS_OUT_OF_INDEXES ||
        !fls_set(fiber_slot, &fiber_marker))
        fail("fiber cleanup value");
    if (!fls_free(fiber_slot)) fail("FlsFree across fiber switch");
    if (fiber_callback_count != 1 || fiber_callback_resumed != 1 ||
        child_delete_error != ERROR_BUSY)
        fail("deletion of fiber with suspended cleanup was not rejected");
    bridge_delete(child_fiber);
    say("PASS: suspended cleanup fiber deletion rejected; callback resumed\r\n");
}

void mainCRTStartup(void)
{
    HMODULE fixture, kernel;
    typedef const m98_api_table *(*table_fn)(void);
    table_fn get_table;
    const m98_api_table *table;
    fixture = LoadLibraryA("FLSFIX.DLL");
    if (!fixture) fail("load FLSFIX.DLL");
    get_table = (table_fn)GetProcAddress(fixture, "get_api_table");
    if (!get_table) fail("fixture get_api_table");
    table = get_table();
    if (!table || !equal(table->target_library, "KERNEL32.DLL"))
        fail("fixture KERNEL32 table");
    fls_alloc = (alloc_fn)find(table, "FlsAlloc");
    fls_free = (free_fn)find(table, "FlsFree");
    fls_set = (set_fn)find(table, "FlsSetValue");
    create_thread = (thread_fn)find(table, "CreateThread");
    bridge_convert = (convert_fn)find(table, "ConvertThreadToFiber");
    bridge_create_fiber = (fiber_fn)find(table, "CreateFiber");
    bridge_switch = (switch_fn)find(table, "SwitchToFiber");
    bridge_delete = (delete_fn)find(table, "DeleteFiber");
    if (!fls_alloc || !fls_free || !fls_set || !create_thread ||
        !bridge_convert || !bridge_create_fiber || !bridge_switch ||
        !bridge_delete)
        fail("fixture FLS/thread API entries");
    run_case("bridge", TRUE);
    run_paused_fiber_case();
    kernel = GetModuleHandleA("KERNEL32.DLL");
    if (!kernel) fail("KERNEL32 handle");
    fls_alloc = (alloc_fn)GetProcAddress(kernel, "FlsAlloc");
    fls_free = (free_fn)GetProcAddress(kernel, "FlsFree");
    fls_set = (set_fn)GetProcAddress(kernel, "FlsSetValue");
    create_thread = CreateThread;
    if (fls_alloc && fls_free && fls_set) {
        (void)run_case("dynamic KERNEL32 FLS (native or routed)", FALSE);
    } else say("OBSERVED: native FLS exports absent; oracle skipped\r\n");
    say("PASS: bridge retirement waits for in-flight rundown callback\r\n");
    ExitProcess(0);
}
