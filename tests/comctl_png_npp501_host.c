/* Pinned Notepad++ 501 resource smoke probe. GPL-2.0-only. */
#include "../src/m98_comctl_ordinals.h"
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_table_t)(void);
typedef HRESULT (WINAPI *icon_t)(HINSTANCE, LPCWSTR, int, int, HICON *);

static void line(const char *message)
{
    const char *end = message;
    DWORD written;
    while (*end) ++end;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, (DWORD)(end-message),
              &written, NULL);
}

int mainCRTStartup(void)
{
    HMODULE provider = LoadLibraryA("M98CTLP.DLL");
    get_table_t get_table;
    const m98_api_table *table;
    const m98_ordinal_api *ordinals;
    icon_t load_icon;
    HICON icon = NULL;
    ICONINFO info = {0};
    BITMAP bitmap;
    HRESULT hr;
    int okay;
    if (!provider) {
        line("FAIL: provider load\r\n");
        ExitProcess(1);
    }
    get_table = (get_table_t)GetProcAddress(provider, "get_api_table");
    if (!get_table) {
        line("FAIL: API table\r\n");
        ExitProcess(1);
    }
    table = get_table();
    if (!table || table[0].ordinal_apis_count != 2) {
        line("FAIL: ordinal table\r\n");
        ExitProcess(1);
    }
    ordinals = (const m98_ordinal_api *)table[0].ordinal_apis;
    load_icon = (icon_t)ordinals[1].addr;
    hr = load_icon(GetModuleHandleA(NULL), MAKEINTRESOURCEW(501), 16, 16,
                   &icon);
    if (hr != S_OK || !icon) {
        line("FAIL: pinned Notepad++ RT_GROUP_ICON 501\r\n");
        ExitProcess(1);
    }
    okay = GetIconInfo(icon, &info) && info.hbmColor &&
           GetObjectA(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap) &&
           bitmap.bmWidth == 16 && bitmap.bmHeight == 16;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    DestroyIcon(icon);
    FreeLibrary(provider);
    if (okay) line("PASS: pinned Notepad++ PNG icon 501 decoded at 16px\r\n");
    else line("FAIL: pinned Notepad++ decoded icon dimensions\r\n");
    ExitProcess(okay ? 0 : 1);
}
