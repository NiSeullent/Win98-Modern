/* True static COMCTL32 ordinal import probe for the installed Win98 guest. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) HRESULT WINAPI LoadIconWithScaleDown(
    HINSTANCE, LPCWSTR, int, int, HICON *);
__declspec(dllimport) HRESULT WINAPI TaskDialogIndirect(
    const void *, int *, int *, BOOL *);

static void line(const char *value)
{
    DWORD written;
    const char *end = value;
    while (*end) ++end;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), value, (DWORD)(end-value),
              &written, NULL);
}

int mainCRTStartup(void)
{
    HICON icon = NULL;
    HRESULT hr;
    if (TaskDialogIndirect(NULL, NULL, NULL, NULL) != (HRESULT)0x80070057UL) {
        line("FAIL: #345 null config\r\n");
        ExitProcess(1);
    }
    hr = LoadIconWithScaleDown(NULL, (LPCWSTR)IDI_APPLICATION, 16, 16,
                               &icon);
    if (hr != S_OK || !icon) {
        line("FAIL: #381 stock icon\r\n");
        ExitProcess(1);
    }
    DestroyIcon(icon);
    line("PASS: static COMCTL32 #345/#381 imports and #381 icon call\r\n");
    ExitProcess(0);
}
