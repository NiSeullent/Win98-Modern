/* Static KernelEx import probe; stock Win98 cannot resolve this import. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) int WINAPI GetLocaleInfoEx(const WCHAR *, LCTYPE,
                                                   WCHAR *, int);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

void mainCRTStartup(void)
{
    WCHAR value[4] = { L'X', L'X', L'X', L'X' };
    if (GetLocaleInfoEx(L"en-US", LOCALE_SDECIMAL | LOCALE_NOUSEROVERRIDE,
                        value, 4) != 2 || value[0] != L'.' || value[1]) {
        say("FAIL: KERNEL32 static GetLocaleInfoEx behavior\r\n");
        ExitProcess(1);
    }
    say("PASS: KERNEL32 static GetLocaleInfoEx behavior\r\n");
    ExitProcess(0);
}
