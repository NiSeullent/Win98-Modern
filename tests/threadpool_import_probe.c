/* Static KERNEL32 import probe; Win98 needs the KernelEx work-object routes. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) PTP_WORK WINAPI CreateThreadpoolWork(
    PTP_WORK_CALLBACK, PVOID, PTP_CALLBACK_ENVIRON);
__declspec(dllimport) void WINAPI SubmitThreadpoolWork(PTP_WORK);
__declspec(dllimport) void WINAPI WaitForThreadpoolWorkCallbacks(PTP_WORK, BOOL);
__declspec(dllimport) void WINAPI CloseThreadpoolWork(PTP_WORK);
__declspec(dllimport) void WINAPI FreeLibraryWhenCallbackReturns(
    PTP_CALLBACK_INSTANCE, HMODULE);
extern void *m98_create_work_iat __asm__("__imp__CreateThreadpoolWork@12");

typedef struct probe_state {
    volatile LONG count;
    HMODULE marker;
} probe_state;
typedef void (*marker_set_unload_event_fn)(HANDLE);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, NULL);
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

static VOID CALLBACK probe_callback(PTP_CALLBACK_INSTANCE instance, PVOID opaque,
                                    PTP_WORK work)
{
    probe_state *state = (probe_state *)opaque;
    (void)work;
    InterlockedIncrement(&state->count);
    FreeLibraryWhenCallbackReturns(instance, state->marker);
}

void mainCRTStartup(void)
{
    probe_state state = { 0, NULL };
    PTP_WORK work;
    HANDLE unloaded;
    marker_set_unload_event_fn set_unload_event;
    DWORD start;
    state.marker = LoadLibraryA("TPMARK.DLL");
    if (!state.marker) {
        DWORD error = GetLastError();
        say("FAIL: static TPMARK.DLL load error=");
        say_hex(error);
        say("\r\n");
        ExitProcess(1);
    }
    unloaded = CreateEventA(NULL, TRUE, FALSE, NULL);
    set_unload_event = (marker_set_unload_event_fn)(ULONG_PTR)
        GetProcAddress(state.marker, "marker_set_unload_event");
    if (!unloaded || !set_unload_event) {
        say("FAIL: static marker unload notification setup\r\n");
        ExitProcess(1);
    }
    set_unload_event(unloaded);
    work = CreateThreadpoolWork(probe_callback, &state, NULL);
    if (!work) {
        DWORD error = GetLastError();
        say("FAIL: static CreateThreadpoolWork error=");
        say_hex(error);
        say(" IAT=");
        say_hex((DWORD)(ULONG_PTR)m98_create_work_iat);
        say("\r\n");
        ExitProcess(1);
    }
    SubmitThreadpoolWork(work);
    WaitForThreadpoolWorkCallbacks(work, FALSE);
    CloseThreadpoolWork(work);
    if (state.count != 1) {
        say("FAIL: static threadpool callback count=");
        say_hex((DWORD)state.count);
        say("\r\n");
        ExitProcess(1);
    }
    /* Native WaitForThreadpoolWorkCallbacks can return before its deferred
     * FreeLibrary reaches the loader. Observe that separate completion via
     * TPMARK's DLL_PROCESS_DETACH notification, then check module absence. */
    if (WaitForSingleObject(unloaded, 10000) != WAIT_OBJECT_0) {
        say("FAIL: static deferred DLL detach was not observed\r\n");
        ExitProcess(1);
    }
    start = GetTickCount();
    while (GetModuleHandleA("TPMARK.DLL")) {
        if (GetTickCount() - start >= 5000) {
            say("FAIL: static marker still loaded after DLL detach\r\n");
            ExitProcess(1);
        }
        Sleep(1);
    }
    CloseHandle(unloaded);
    say("PASS: static KERNEL32 threadpool work imports and deferred unload\r\n");
    ExitProcess(0);
}
