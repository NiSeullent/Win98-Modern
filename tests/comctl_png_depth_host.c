/* Direct guest screen-depth tie-break probe for ordinal 381. GPL-2.0-only. */
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

static void number(unsigned value)
{
    char digits[12];
    char out[12];
    unsigned used = 0, i;
    DWORD written;
    do { digits[used++] = (char)('0' + value % 10); value /= 10; }
    while (value && used < sizeof(digits));
    for (i = 0; i < used; ++i) out[i] = digits[used - i - 1];
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out, used, &written, NULL);
}

static DWORD center_rgb(HICON icon)
{
    ICONINFO info = {0};
    BITMAPINFO bmi = {0};
    DWORD *pixels = NULL;
    DWORD result = 0xffffffffUL;
    HDC dc = NULL;
    if (!GetIconInfo(icon, &info)) return result;
    pixels = (DWORD *)HeapAlloc(GetProcessHeap(), 0, 32 * 32 * 4);
    dc = CreateCompatibleDC(NULL);
    if (pixels && dc && info.hbmColor) {
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 32;
        bmi.bmiHeader.biHeight = 32;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        if (GetDIBits(dc, info.hbmColor, 0, 32, pixels, &bmi,
                      DIB_RGB_COLORS) == 32)
            result = pixels[16 * 32 + 16] & 0xffffffUL;
    }
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    if (dc) DeleteDC(dc);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return result;
}

int mainCRTStartup(void)
{
    HMODULE provider = LoadLibraryA("M98CTLP.DLL");
    get_table_t get_table;
    const m98_api_table *table;
    const m98_ordinal_api *ordinals;
    icon_t load_icon;
    HICON icon = NULL;
    HDC screen;
    int bits, planes, depth;
    DWORD rgb;
    HRESULT hr;
    int red;
    if (!provider) { line("FAIL: provider load\r\n"); ExitProcess(1); }
    get_table = (get_table_t)GetProcAddress(provider, "get_api_table");
    if (!get_table) { line("FAIL: API table\r\n"); ExitProcess(1); }
    table = get_table();
    if (!table || table[0].ordinal_apis_count != 2) {
        line("FAIL: ordinal table\r\n"); ExitProcess(1);
    }
    ordinals = (const m98_ordinal_api *)table[0].ordinal_apis;
    load_icon = (icon_t)ordinals[1].addr;
    screen = GetDC(NULL);
    if (!screen) { line("FAIL: screen DC\r\n"); ExitProcess(1); }
    bits = GetDeviceCaps(screen, BITSPIXEL);
    planes = GetDeviceCaps(screen, PLANES);
    ReleaseDC(NULL, screen);
    if (bits <= 0 || planes <= 0 || bits * planes > 64) {
        line("FAIL: screen bit depth\r\n"); ExitProcess(1);
    }
    depth = bits * planes;
    line("DISPLAY_BPP="); number((unsigned)depth); line("\r\n");
    hr = load_icon(GetModuleHandleA(NULL), MAKEINTRESOURCEW(601), 32, 32,
                   &icon);
    if (hr != S_OK || !icon) {
        line("FAIL: duplicate-size PNG group load\r\n"); ExitProcess(1);
    }
    rgb = center_rgb(icon);
    DestroyIcon(icon);
    FreeLibrary(provider);
    red = depth < 32;
    if (red && (rgb & 0xff0000UL) >= 0xfa0000UL &&
        (rgb & 0x00ffffUL) <= 0x000202UL) {
        line("PASS: selected 16bpp red icon for display\r\n");
        ExitProcess(0);
    }
    if (!red && (rgb & 0x0000ffUL) >= 250UL &&
        (rgb & 0xffff00UL) <= 0x020200UL) {
        line("PASS: selected 32bpp blue icon for display\r\n");
        ExitProcess(0);
    }
    line("FAIL: display-depth icon color mismatch\r\n");
    ExitProcess(1);
}
