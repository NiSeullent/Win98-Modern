/* Direct PNG-compressed icon resource probe for the x86 provider. GPL-2.0-only. */
#include "../src/m98_comctl_ordinals.h"
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_table_t)(void);
typedef HRESULT (WINAPI *icon_t)(HINSTANCE, LPCWSTR, int, int, HICON *);

static int failures;

static void line(const char *message)
{
    const char *end = message;
    DWORD written;
    while (*end) ++end;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, (DWORD)(end-message),
              &written, NULL);
}

static void check(int condition, const char *message)
{
    if (!condition) {
        ++failures;
        line("FAIL: ");
        line(message);
        line("\r\n");
    }
}

static DWORD icon_color(HICON icon, int edge)
{
    ICONINFO info;
    BITMAPINFO bmi;
    HDC dc = NULL;
    DWORD *pixels = NULL;
    DWORD result = 0xffffffffUL;
    unsigned i;
    if (!GetIconInfo(icon, &info)) return result;
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
            result = pixels[(edge / 2) * edge + edge / 2] & 0xffffffUL;
    }
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    if (dc) DeleteDC(dc);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return result;
}

static int icon_size(HICON icon, int edge)
{
    ICONINFO info;
    BITMAP bitmap;
    int okay = 0;
    if (!GetIconInfo(icon, &info)) return 0;
    if (info.hbmColor &&
        GetObjectA(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap))
        okay = bitmap.bmWidth == edge && bitmap.bmHeight == edge;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return okay;
}

int mainCRTStartup(void)
{
    HMODULE provider = LoadLibraryA("M98CTLP.DLL");
    get_table_t get_table;
    const m98_api_table *table;
    const m98_ordinal_api *ordinals;
    icon_t load_icon;
    HRSRC image_resource;
    HGLOBAL image_handle;
    const BYTE *image;
    HICON icon;
    HRESULT hr;
    int i;
    if (!provider) {
        line("FAIL: PNG provider load\r\n");
        ExitProcess(1);
    }
    get_table = (get_table_t)GetProcAddress(provider, "get_api_table");
    if (!get_table) {
        line("FAIL: PNG provider API table\r\n");
        ExitProcess(1);
    }
    table = get_table();
    check(table && table[0].ordinal_apis_count == 2, "ordinal table");
    if (!table || table[0].ordinal_apis_count != 2) ExitProcess(1);
    ordinals = (const m98_ordinal_api *)table[0].ordinal_apis;
    check(ordinals[0].ord == 345 && ordinals[1].ord == 381,
          "ordinal order");
    load_icon = (icon_t)ordinals[1].addr;
    image_resource = FindResourceA(GetModuleHandleA(NULL),
                                   MAKEINTRESOURCEA(1), RT_ICON);
    check(image_resource != NULL, "first PNG RT_ICON exists");
    image_handle = image_resource ? LoadResource(GetModuleHandleA(NULL),
                                                 image_resource) : NULL;
    image = image_handle ? (const BYTE *)LockResource(image_handle) : NULL;
    check(image && image[0] == 137 && image[1] == 'P' &&
          image[2] == 'N' && image[3] == 'G', "fixture is true PNG RT_ICON");
    for (i = 0; i < 4; ++i) {
        int edge = i == 0 ? 16 : (i == 1 ? 32 : (i == 2 ? 40 : 48));
        DWORD color;
        icon = NULL;
        hr = load_icon(GetModuleHandleA(NULL), MAKEINTRESOURCEW(501),
                       edge, edge, &icon);
        check(hr == S_OK && icon != NULL, "PNG group icon load");
        if (!icon) continue;
        check(icon_size(icon, edge), "PNG icon requested size");
        color = icon_color(icon, edge);
        if (i < 2)
            check((color & 0xff0000UL) >= 0xfa0000UL &&
                  (color & 0x00ffffUL) <= 0x000202UL,
                  "standard size chooses 16px red image");
        else
            check((color & 0x0000ffUL) >= 250UL &&
                  (color & 0xffff00UL) <= 0x020200UL,
                  "nonstandard or exact size chooses 48px blue image");
        DestroyIcon(icon);
    }
    FreeLibrary(provider);
    if (!failures) line("PASS: PNG RT_ICON ordinal 381 standard and scaled sizes\r\n");
    ExitProcess(failures ? 1 : 0);
}
