/* Static KernelEx imports; stock Windows 98 has neither symbol. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) BOOL WINAPI QueryFullProcessImageNameA(
    HANDLE, DWORD, char *, DWORD *);
__declspec(dllimport) BOOL WINAPI QueryFullProcessImageNameW(
    HANDLE, DWORD, WCHAR *, DWORD *);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

void mainCRTStartup(void)
{
    char ansi[MAX_PATH];
    WCHAR wide[MAX_PATH];
    DWORD size_a = MAX_PATH, size_w = MAX_PATH;
    if (!QueryFullProcessImageNameA(GetCurrentProcess(), 0,
                                    ansi, &size_a) ||
        !QueryFullProcessImageNameW(GetCurrentProcess(), 0,
                                    wide, &size_w) ||
        !size_a || !size_w || size_a >= MAX_PATH || size_w >= MAX_PATH ||
        ansi[size_a] || wide[size_w]) {
        say("FAIL: static KERNEL32 process image path imports\r\n");
        ExitProcess(1);
    }
    say("PASS: static KERNEL32 process image path imports\r\n");
    ExitProcess(0);
}
