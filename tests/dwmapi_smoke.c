/* Host smoke test: app-local DWM reports absent composition truthfully. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

#define NO_COMPOSITION ((HRESULT)0x80263001L)

typedef HRESULT (WINAPI *get_color_fn)(DWORD *, BOOL *);
typedef HRESULT (WINAPI *set_attribute_fn)(HWND, DWORD, LPCVOID, DWORD);

static void report(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, NULL);
}

static void fail(const char *message)
{
    report("FAIL: ");
    report(message);
    report("\r\n");
    ExitProcess(1);
}

void mainCRTStartup(void)
{
    HMODULE shim = LoadLibraryA("dwmapi.dll");
    get_color_fn get_color;
    set_attribute_fn set_attribute;
    DWORD color = 0x12345678;
    BOOL opaque = 0x13572468;
    BOOL dark = TRUE;

    if (!shim) fail("load app-local dwmapi.dll");
    get_color = (get_color_fn)GetProcAddress(shim, "DwmGetColorizationColor");
    set_attribute = (set_attribute_fn)GetProcAddress(shim, "DwmSetWindowAttribute");
    if (!get_color || !set_attribute ||
        GetProcAddress(shim, "DwmGetColorizationColor@8") ||
        GetProcAddress(shim, "DwmSetWindowAttribute@16"))
        fail("exact undecorated DWM exports");
    if (get_color(&color, &opaque) != NO_COMPOSITION ||
        color != 0x12345678 || opaque != 0x13572468)
        fail("no glass color and untouched outputs");
    if (get_color(NULL, &opaque) != E_INVALIDARG ||
        get_color(&color, NULL) != E_INVALIDARG ||
        color != 0x12345678 || opaque != 0x13572468)
        fail("invalid color output arguments");
    if (set_attribute((HWND)1, 20, &dark, sizeof(dark)) != NO_COMPOSITION ||
        set_attribute(NULL, 0, NULL, 0) != NO_COMPOSITION || dark != TRUE)
        fail("DWM window attribute is unavailable");
    report("PASS: dwmapi reports no desktop composition\r\n");
    FreeLibrary(shim);
    ExitProcess(0);
}
