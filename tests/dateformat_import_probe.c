/* Static import proves KernelEx maps KERNEL32.GetDateFormatEx at load time.
 * This image is for a KernelEx-enabled guest; stock Win98 cannot load it. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) int WINAPI GetDateFormatEx(const WCHAR *, DWORD,
    const SYSTEMTIME *, const WCHAR *, WCHAR *, int, const WCHAR *);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

void mainCRTStartup(void)
{
    SYSTEMTIME date = { 2026, 9, 0, 23, 0, 0, 0, 0 };
    WCHAR output[32];
    int result = GetDateFormatEx(L"en-US", 0, &date,
                                 L"yyyy'-'MM'-'dd", output, 32, 0);
    if (result != 11 || output[0] != L'2' || output[4] != L'-' ||
        output[7] != L'-' || output[10] != 0) {
        say("FAIL: KERNEL32 static GetDateFormatEx behavior\r\n");
        ExitProcess(1);
    }
    say("PASS: KERNEL32 static GetDateFormatEx behavior\r\n");
    ExitProcess(0);
}
