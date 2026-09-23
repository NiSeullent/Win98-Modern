/* Original Win98 KERNEL32 fiber/TLS baseline; no FLS or KernelEx import.
 * Run this console PE32 binary in the installed guest before implementing
 * a fiber-local store. GetCurrentFiber/GetFiberData are header inlines. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

static LPVOID main_fiber;
static LPVOID created_fiber;
static DWORD main_tid;
static DWORD tls_index;
static DWORD worker_tid;
static LPVOID worker_raw_fiber;
static volatile LONG fiber_ran;
static int marker_main, marker_child, marker_param;

static void say(const char *s)
{
    DWORD length = 0, written;
    while (s[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, length, &written, 0);
}

static void hex32(const char *name, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char line[11];
    unsigned int i;
    say(name);
    line[0] = '0'; line[1] = 'x';
    for (i = 0; i < 8; ++i)
        line[2 + i] = digits[(value >> (28 - i * 4)) & 15];
    line[10] = 0;
    say(line);
    say("\r\n");
}

static void fail(const char *why)
{
    say("FAIL: "); say(why); say("\r\n");
    ExitProcess(1);
}

static DWORD WINAPI worker_proc(LPVOID unused)
{
    (void)unused;
    worker_tid = GetCurrentThreadId();
    worker_raw_fiber = GetCurrentFiber();
    return 0;
}

static VOID WINAPI fiber_proc(LPVOID parameter)
{
    hex32("child_current_fiber=", (DWORD)(ULONG_PTR)GetCurrentFiber());
    hex32("child_get_fiber_data=", (DWORD)(ULONG_PTR)GetFiberData());
    if (GetCurrentFiber() != created_fiber || GetFiberData() != parameter ||
        parameter != &marker_param || GetCurrentThreadId() != main_tid)
        fail("native fiber identity or parameter");
    if (TlsGetValue(tls_index) != &marker_main)
        fail("native TLS was not shared by fibers on one thread");
    if (!TlsSetValue(tls_index, &marker_child))
        fail("native TLS set in child fiber");
    fiber_ran = 1;
    SwitchToFiber(main_fiber);
    fail("deleted child fiber resumed");
}

void mainCRTStartup(void)
{
    HANDLE worker;
    DWORD wait_result;
    LPVOID before;
    LPVOID reused;

    main_tid = GetCurrentThreadId();
    before = GetCurrentFiber();
    hex32("main_thread_id=", main_tid);
    hex32("main_before_convert_raw_fiber=", (DWORD)(ULONG_PTR)before);
    worker = CreateThread(0, 0, worker_proc, 0, 0, 0);
    if (!worker) fail("CreateThread");
    wait_result = WaitForSingleObject(worker, 10000);
    if (wait_result != WAIT_OBJECT_0) fail("worker wait");
    if (!CloseHandle(worker)) fail("CloseHandle worker");
    hex32("worker_thread_id=", worker_tid);
    hex32("worker_unconverted_raw_fiber=", (DWORD)(ULONG_PTR)worker_raw_fiber);
    if (worker_tid == main_tid) fail("different thread identity");

    tls_index = TlsAlloc();
    if (tls_index == TLS_OUT_OF_INDEXES) fail("TlsAlloc");
    if (!TlsSetValue(tls_index, &marker_main)) fail("TlsSetValue main");
    main_fiber = ConvertThreadToFiber(&marker_main);
    if (!main_fiber) fail("ConvertThreadToFiber");
    hex32("converted_main_fiber=", (DWORD)(ULONG_PTR)main_fiber);
    hex32("converted_get_current=", (DWORD)(ULONG_PTR)GetCurrentFiber());
    if (GetCurrentFiber() != main_fiber || GetFiberData() != &marker_main)
        fail("converted fiber identity or parameter");

    created_fiber = CreateFiber(0, fiber_proc, &marker_param);
    if (!created_fiber) fail("CreateFiber");
    hex32("created_child_fiber=", (DWORD)(ULONG_PTR)created_fiber);
    if (created_fiber == main_fiber) fail("distinct fiber identity");
    SwitchToFiber(created_fiber);
    if (fiber_ran != 1 || GetCurrentFiber() != main_fiber)
        fail("SwitchToFiber return identity");
    if (TlsGetValue(tls_index) != &marker_child)
        fail("native TLS changed value not visible in parent fiber");
    DeleteFiber(created_fiber);
    say("OBSERVED: native TLS value shared across child and parent fiber\r\n");

    reused = CreateFiber(0, fiber_proc, &marker_param);
    if (!reused) fail("CreateFiber after DeleteFiber");
    hex32("new_child_fiber=", (DWORD)(ULONG_PTR)reused);
    if (reused == created_fiber)
        say("OBSERVED: native fiber address reused after deletion\r\n");
    DeleteFiber(reused);
    if (!TlsFree(tls_index)) fail("TlsFree");
    say("PASS: original KERNEL32 fiber identity and TLS sharing baseline\r\n");
    ExitProcess(0);
}
