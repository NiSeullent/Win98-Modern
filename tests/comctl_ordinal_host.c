/* Direct x86 provider-table behavior probe. GPL-2.0-only. */
#include "../src/m98_comctl_ordinals.h"
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_table_t)(void);
typedef HRESULT (WINAPI *icon_t)(HINSTANCE, LPCWSTR, int, int, HICON *);
typedef HRESULT (WINAPI *dialog_t)(const m98_taskdialog_config *, int *,
                                   int *, BOOL *);

static int failures;

static void log_line(const char *line)
{
    DWORD written;
    const char *p = line;
    while (*p) ++p;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, (DWORD)(p-line),
              &written, NULL);
}

static void check(int condition, const char *message)
{
    if (!condition) { ++failures; log_line("FAIL: "); log_line(message); log_line("\r\n"); }
}

static void log_hex(DWORD value)
{
    char digits[] = "0123456789ABCDEF";
    char text[11] = "0x00000000";
    int i;
    for (i = 0; i < 8; ++i)
        text[2 + i] = digits[(value >> (28 - i * 4)) & 15];
    log_line(text);
}

static int icon_size(HICON icon, int expected)
{
    ICONINFO info;
    BITMAP bitmap;
    BOOL okay;
    if (!GetIconInfo(icon, &info)) return 0;
    okay = info.hbmColor &&
        GetObjectA(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap) &&
        bitmap.bmWidth == expected && bitmap.bmHeight == expected;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return okay;
}

static DWORD icon_center_rgb(HICON icon, int edge)
{
    ICONINFO info;
    BITMAPINFO bmi;
    HDC dc;
    DWORD *pixels;
    DWORD color = 0xffffffffUL;
    unsigned i;
    if (!GetIconInfo(icon, &info)) return color;
    pixels = (DWORD *)HeapAlloc(GetProcessHeap(), 0, edge * edge * 4);
    dc = CreateCompatibleDC(NULL);
    if (pixels && dc && info.hbmColor) {
        for (i = 0; i < sizeof(bmi)/sizeof(DWORD); ++i)
            ((DWORD *)&bmi)[i] = 0;
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = edge;
        bmi.bmiHeader.biHeight = edge;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        if (GetDIBits(dc, info.hbmColor, 0, edge, pixels, &bmi,
                       DIB_RGB_COLORS) == edge)
            color = pixels[(edge / 2) * edge + edge / 2] & 0xffffffUL;
    }
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    if (dc) DeleteDC(dc);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return color;
}

int mainCRTStartup(void)
{
    HMODULE provider = LoadLibraryA("M98CTL.DLL");
    get_table_t get_table;
    const m98_api_table *table;
    const m98_ordinal_api *ords;
    icon_t icon_api;
    dialog_t dialog_api;
    HICON icon = NULL;
    HRESULT hr;
    m98_taskdialog_config config;
    unsigned i;
    if (!provider) { log_line("FAIL: provider load\r\n"); ExitProcess(1); }
    get_table = (get_table_t)GetProcAddress(provider, "get_api_table");
    if (!get_table) { log_line("FAIL: provider table\r\n"); ExitProcess(1); }
    table = get_table();
    check(table && table[0].target_library &&
          lstrcmpA(table[0].target_library, "COMCTL32.DLL") == 0,
          "target DLL");
    check(table && !table[1].target_library, "terminating table");
    check(table[0].named_apis_count == 2 &&
          lstrcmpA(table[0].named_apis[0].name, "LoadIconWithScaleDown") == 0 &&
          lstrcmpA(table[0].named_apis[1].name, "TaskDialogIndirect") == 0,
          "sorted names");
    ords = (const m98_ordinal_api *)table[0].ordinal_apis;
    check(table[0].ordinal_apis_count == 2 && ords &&
          ords[0].ord == 345 && ords[1].ord == 381,
          "sorted ordinals");
    icon_api = (icon_t)ords[1].addr;
    dialog_api = (dialog_t)ords[0].addr;
    check(table[0].named_apis[0].addr == ords[1].addr &&
          table[0].named_apis[1].addr == ords[0].addr,
          "ordinal and name identity");
    check(icon_api(NULL, (LPCWSTR)IDI_APPLICATION, 16, 16, NULL) ==
          (HRESULT)0x80070057UL, "null icon output");
    check(icon_api(NULL, (LPCWSTR)IDI_APPLICATION, 0, 16, &icon) ==
          (HRESULT)0x80070057UL, "zero width");
    check(icon_api(NULL, NULL, 16, 16, &icon) ==
          (HRESULT)0x80070057UL, "null icon name");
    for (i = 0; i < 2; ++i) {
        int edge = i ? 32 : 16;
        icon = NULL;
        hr = icon_api(NULL, (LPCWSTR)IDI_APPLICATION, edge, edge, &icon);
        check(hr == S_OK && icon != NULL, "stock icon load");
        if (icon) { check(icon_size(icon, edge), "stock icon size"); DestroyIcon(icon); }
    }
    for (i = 0; i < 2; ++i) {
        int edge = i ? 40 : 16;
        DWORD expected_color = i ? 0x0000ffUL : 0xff0000UL;
        icon = NULL;
        hr = icon_api(GetModuleHandleA(NULL), MAKEINTRESOURCEW(101),
                      edge, edge, &icon);
        check(hr == S_OK && icon != NULL, "resource group icon load");
        if (icon) {
            DWORD color = icon_center_rgb(icon, edge);
            check(icon_size(icon, edge), "resource group icon size");
            if ((!i && ((color & 0xff0000UL) < 0xfa0000UL ||
                        (color & 0x00ffffUL) > 0x000202UL)) ||
                (i && ((color & 0xffUL) < 250 ||
                       (color & 0xffff00UL) > 0x020200UL))) {
                log_line("resource color = "); log_hex(color);
                log_line(" expected = "); log_hex(expected_color);
                log_line("\r\n");
                check(0, "resource group larger-icon choice");
            }
            DestroyIcon(icon);
        }
    }
    icon = (HICON)1;
    hr = icon_api(GetModuleHandleA(NULL), (LPCWSTR)MAKEINTRESOURCEW(65530),
                  16, 16, &icon);
    check(FAILED(hr) && icon == NULL, "missing group icon failure");
    check(dialog_api(NULL, NULL, NULL, NULL) ==
          (HRESULT)0x80070057UL, "null task dialog config");
    for (i = 0; i < sizeof(config)/sizeof(DWORD); ++i)
        ((DWORD *)&config)[i] = 0;
    config.cbSize = sizeof(config) - 4;
    check(dialog_api(&config, NULL, NULL, NULL) ==
          (HRESULT)0x80070057UL, "task dialog ABI size");
    config.cbSize = sizeof(config);
    config.dwFlags = 1;
    check(dialog_api(&config, NULL, NULL, NULL) ==
          (HRESULT)0x80004001UL, "unsupported callback flag");
    config.dwFlags = 0;
    config.dwCommonButtons = 0x20;
    check(dialog_api(&config, NULL, NULL, NULL) ==
          (HRESULT)0x80004001UL, "unsupported button layout");
    FreeLibrary(provider);
    if (!failures) log_line("PASS: COMCTL32 ordinal table, icon sizes and fail-closed task dialog subset\r\n");
    ExitProcess(failures ? 1 : 0);
}
