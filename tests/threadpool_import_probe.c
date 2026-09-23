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
    state.marker = LoadLibraryA("TPMARK.DLL");
    if (!state.marker) {
        DWORD error = GetLastError();
        say("FAIL: static TPMARK.DLL load error=");
        say_hex(error);
        say("\r\n");
        ExitProcess(1);
    }
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
    if (state.count != 1 || GetModuleHandleA("TPMARK.DLL")) {
        say("FAIL: static threadpool callback or deferred unload\r\n");
        ExitProcess(1);
    }
    say("PASS: static KERNEL32 threadpool work imports and deferred unload\r\n");
    ExitProcess(0);
}
