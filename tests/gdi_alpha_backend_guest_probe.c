/* Diagnose fail-closed results when a provider has no sibling MSIMG32.
 * No CORE route or drawing surface is used. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(__cdecl *table_fn)(void);
typedef BOOL (WINAPI *alpha_fn)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);
typedef BOOL (WINAPI *transparent_fn)(HDC,int,int,int,int,HDC,int,int,int,int,UINT);
typedef BOOL (WINAPI *gradient_fn)(HDC,PTRIVERTEX,ULONG,PVOID,ULONG,ULONG);

static void say(const char *s)
{
    DWORD n = 0, written;
    while (s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
}

static void hex(DWORD value)
{
    char word[11] = "0x00000000";
    unsigned i;
    for (i = 0; i < 8; ++i) word[9-i] = "0123456789abcdef"[(value >> (i*4)) & 15];
    say(word);
}

static void record(const char *name, BOOL result)
{
    say("case="); say(name); say(" result="); hex((DWORD)result);
    say(" gle="); hex(GetLastError()); say("\r\n");
}

void __cdecl mainCRTStartup(void)
{
    HMODULE module = LoadLibraryA("C:\\M98LAB\\GDIFIX.DLL");
    table_fn get_table;
    const m98_api_table *table;
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, 0};
    TRIVERTEX vertices[2] = {0};
    GRADIENT_RECT rect = {0,1};
    BOOL result;
    if (!module) { say("FAIL: GDIFIX load\r\n"); ExitProcess(2); }
    get_table = (table_fn)GetProcAddress(module, "get_api_table");
    if (!get_table) { say("FAIL: table export\r\n"); ExitProcess(3); }
    table = get_table();
    if (!table || !table->target_library || table->named_apis_count != 3)
        { say("FAIL: table shape\r\n"); ExitProcess(4); }
    SetLastError(0x4938d001);
    result = ((alpha_fn)table->named_apis[0].addr)((HDC)1,0,0,1,1,(HDC)1,0,0,1,1,blend);
    record("alpha_without_sibling", result);
    SetLastError(0x4938d002);
    result = ((gradient_fn)table->named_apis[1].addr)((HDC)1,vertices,2,&rect,1,GRADIENT_FILL_RECT_H);
    record("gradient_without_sibling", result);
    SetLastError(0x4938d003);
    result = ((transparent_fn)table->named_apis[2].addr)((HDC)1,0,0,1,1,(HDC)1,0,0,1,1,0);
    record("transparent_without_sibling", result);
    ExitProcess(0);
}
