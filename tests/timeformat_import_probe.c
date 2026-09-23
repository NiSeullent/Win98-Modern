/* Static import proves KernelEx maps KERNEL32.GetTimeFormatEx at load time.
 * This image is for a KernelEx-enabled guest; stock Win98 cannot load it. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) int WINAPI GetTimeFormatEx(const WCHAR *, DWORD,
    const SYSTEMTIME *, const WCHAR *, WCHAR *, int);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

void mainCRTStartup(void)
{
    SYSTEMTIME time = { 2026, 9, 0, 23, 13, 5, 9, 0 };
    WCHAR output[32];
    int result = GetTimeFormatEx(L"en-US", 0, &time,
                                 L"HH':'mm':'ss", output, 32);
    if (result != 9 || output[0] != L'1' || output[1] != L'3' ||
        output[2] != L':' || output[8] != 0) {
        say("FAIL: KERNEL32 static GetTimeFormatEx behavior\r\n");
        ExitProcess(1);
    }
    say("PASS: KERNEL32 static GetTimeFormatEx behavior\r\n");
    ExitProcess(0);
}
