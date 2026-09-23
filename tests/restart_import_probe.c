/* Static KernelEx imports; stock Windows 98 lacks these three exports. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

#ifndef RESTART_MAX_CMD_LINE
#define RESTART_MAX_CMD_LINE 1024
#endif

__declspec(dllimport) HRESULT WINAPI GetApplicationRestartSettings(
    HANDLE, WCHAR *, DWORD *, DWORD *);
__declspec(dllimport) HRESULT WINAPI RegisterApplicationRestart(
    const WCHAR *, DWORD);
__declspec(dllimport) HRESULT WINAPI UnregisterApplicationRestart(void);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

void mainCRTStartup(void)
{
    WCHAR oversized[RESTART_MAX_CMD_LINE + 1];
    DWORD size = 0;
    int index;
    for (index = 0; index < RESTART_MAX_CMD_LINE; ++index)
        oversized[index] = L'x';
    oversized[RESTART_MAX_CMD_LINE] = 0;
    if (RegisterApplicationRestart(oversized, 0) != E_INVALIDARG ||
        GetApplicationRestartSettings(GetCurrentProcess(), 0, &size, 0) !=
            HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ||
        UnregisterApplicationRestart() != S_OK ||
        GetApplicationRestartSettings(0, 0, &size, 0) != E_INVALIDARG) {
        say("FAIL: static KERNEL32 restart imports\r\n");
        ExitProcess(1);
    }
    say("PASS: static KERNEL32 restart imports\r\n");
    ExitProcess(0);
}
