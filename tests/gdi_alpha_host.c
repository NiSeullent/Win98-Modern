/* Host-side provider table and absent-backend fail-closed probe.
 * This does not certify Win98 pixel behavior. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(__cdecl *table_fn)(void);
typedef BOOL (WINAPI *alpha_fn)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);
typedef BOOL (WINAPI *transparent_fn)(HDC,int,int,int,int,HDC,int,int,int,int,UINT);
typedef BOOL (WINAPI *gradient_fn)(HDC,PTRIVERTEX,ULONG,PVOID,ULONG,ULONG);

static void say(const char *message)
{
    DWORD length = 0, done;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &done, NULL);
}

static void fail(const char *message)
{
    say("FAIL: "); say(message); say("\r\n"); ExitProcess(1);
}

static BOOL missing_backend_error(DWORD error)
{
    /* NT returns MOD_NOT_FOUND for an absent full path. The installed Win98
     * loader returned DLL_NOT_FOUND (1157) for the same sibling lookup. */
    return error == ERROR_MOD_NOT_FOUND || error == ERROR_DLL_NOT_FOUND;
}

void __cdecl mainCRTStartup(void)
{
    static const char fixture[] = "GDIFIX.DLL";
    static const char *expected[] = {
        "GdiAlphaBlend", "GdiGradientFill", "GdiTransparentBlt"
    };
    char path[MAX_PATH];
    DWORD length, i;
    HMODULE module;
    table_fn get_table;
    const m98_api_table *table;
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, 0 };
    TRIVERTEX vertices[2] = {0};
    GRADIENT_RECT rect = {0, 1};

    length = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!length || length >= sizeof(path)) fail("self path");
    while (length && path[length - 1] != '\\' && path[length - 1] != '/') --length;
    if (!length || length + sizeof(fixture) > sizeof(path)) fail("fixture path");
    for (i = 0; i < sizeof(fixture); ++i) path[length + i] = fixture[i];
    module = LoadLibraryA(path);
    if (!module) fail("fixture load");
    get_table = (table_fn)GetProcAddress(module, "get_api_table");
    if (!get_table) fail("get_api_table export");
    table = get_table();
    if (!table || !table->target_library ||
        lstrcmpA(table->target_library, "GDI32.DLL") ||
        table->named_apis_count != 3 || table->ordinal_apis ||
        table->ordinal_apis_count || table[1].target_library)
        fail("provider table shape");
    for (i = 0; i < 3; ++i)
        if (lstrcmpA(table->named_apis[i].name, expected[i]) ||
            !table->named_apis[i].addr) fail("provider entry name/order/pointer");
    /* The build directory must not contain an MSIMG32 DLL copy. These calls
     * prove that an unavailable backend is reported, never forged as success. */
    SetLastError(0);
    if (((alpha_fn)table->named_apis[0].addr)((HDC)1,0,0,1,1,(HDC)1,0,0,1,1,blend) ||
        !missing_backend_error(GetLastError())) fail("alpha missing backend");
    SetLastError(0);
    if (((gradient_fn)table->named_apis[1].addr)((HDC)1,vertices,2,&rect,1,0) ||
        !missing_backend_error(GetLastError())) fail("gradient missing backend");
    /* The boundary rectangle passes argument validation and reaches the
     * deliberately missing backend. A one-row increase must be rejected
     * before DLL loading, catching an old strip-only work calculation. */
    vertices[1].x = 1024;
    vertices[1].y = 1024;
    SetLastError(0);
    if (((gradient_fn)table->named_apis[1].addr)((HDC)1,vertices,2,&rect,1,
                                                  GRADIENT_FILL_RECT_H) ||
        !missing_backend_error(GetLastError()))
        fail("gradient rectangle budget boundary");
    vertices[1].y = 1025;
    SetLastError(0);
    if (((gradient_fn)table->named_apis[1].addr)((HDC)1,vertices,2,&rect,1,
                                                  GRADIENT_FILL_RECT_H) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("gradient rectangle pixel-area budget");
    SetLastError(0);
    if (((gradient_fn)table->named_apis[1].addr)((HDC)1,vertices,2,&rect,1,
                                                  GRADIENT_FILL_RECT_V) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("gradient vertical pixel-area budget");
    SetLastError(0);
    if (((transparent_fn)table->named_apis[2].addr)((HDC)1,0,0,1,1,(HDC)1,0,0,1,1,0) ||
        !missing_backend_error(GetLastError())) fail("transparent missing backend");
    FreeLibrary(module);
    say("PASS: GDI32 table and absent auxiliary backend fail closed\r\n");
    ExitProcess(0);
}
