/* Three genuine USER32 static imports. GPL-2.0-only.
 * This EXE requires KernelEx routing on Windows 98; no clipboard mutation. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) BOOL WINAPI AddClipboardFormatListener(HWND);
__declspec(dllimport) BOOL WINAPI RemoveClipboardFormatListener(HWND);
__declspec(dllimport) BOOL WINAPI GetUpdatedClipboardFormats(PUINT, UINT, PUINT);

static void say(const char *s)
{
    DWORD n = 0, written;
    while (s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
}

static void fail(const char *why)
{
    say("FAIL: "); say(why); say("\r\n");
    ExitProcess(1);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void __cdecl mainCRTStartup(void)
{
    static WNDCLASSA wc;
    HWND hwnd;
    static UINT formats[1024];
    UINT count = 0;
    DWORD before, after;
    BOOL ok;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "M98ClipboardStatic";
    if (!RegisterClassA(&wc)) fail("RegisterClassA");
    hwnd = CreateWindowExA(0, wc.lpszClassName, "M98ClipboardStatic", WS_POPUP,
                           0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) fail("CreateWindowExA");
    if (!AddClipboardFormatListener(hwnd)) fail("static AddClipboardFormatListener");
    before = GetClipboardSequenceNumber();
    ok = GetUpdatedClipboardFormats(formats, 1024, &count);
    after = GetClipboardSequenceNumber();
    if (!ok && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("static GetUpdatedClipboardFormats");
    if (ok && before == after && count != (UINT)CountClipboardFormats())
        fail("static format count differs from stable clipboard");
    if (!RemoveClipboardFormatListener(hwnd)) fail("static RemoveClipboardFormatListener");
    DestroyWindow(hwnd);
    say("PASS: three static USER32 clipboard imports and read-only behavior\r\n");
    ExitProcess(0);
}
