/* Repeated load/call/unload probe for provider TLS-slot cleanup.
 * Runs with GDIFIX.DLL beside this EXE and no auxiliary MSIMG32 there.
 * GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(__cdecl *table_fn)(void);
typedef BOOL (WINAPI *alpha_fn)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);

static void say(const char *s)
{
    DWORD n = 0, written;
    while (s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
}

static void fail(const char *s)
{
    say("FAIL: "); say(s); say("\r\n"); ExitProcess(1);
}

void __cdecl mainCRTStartup(void)
{
    static const char fixture[] = "GDIFIX.DLL";
    char path[MAX_PATH];
    DWORD length, baseline, after, error, i, j;
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, 0 };
    HMODULE module;
    table_fn get_table;
    const m98_api_table *table;
    alpha_fn alpha;

    length = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!length || length >= sizeof(path)) fail("self path");
    while (length && path[length - 1] != '\\' && path[length - 1] != '/') --length;
    if (!length || length + sizeof(fixture) > sizeof(path)) fail("fixture path");
    for (j = 0; j < sizeof(fixture); ++j) path[length + j] = fixture[j];
    baseline = TlsAlloc();
    if (baseline == TLS_OUT_OF_INDEXES || !TlsFree(baseline))
        fail("baseline TLS allocation");
    for (i = 0; i < 16; ++i) {
        module = LoadLibraryA(path);
        if (!module) fail("fixture load");
        get_table = (table_fn)GetProcAddress(module, "get_api_table");
        if (!get_table) fail("API table export");
        table = get_table();
        if (!table || !table->target_library || table->named_apis_count != 3)
            fail("API table shape");
        alpha = (alpha_fn)(ULONG_PTR)table->named_apis[0].addr;
        if (!alpha) fail("alpha pointer");
        SetLastError(0);
        if (alpha((HDC)1,0,0,1,1,(HDC)1,0,0,1,1,blend))
            fail("absent backend accepted");
        error = GetLastError();
        if (error != ERROR_MOD_NOT_FOUND && error != ERROR_DLL_NOT_FOUND)
            fail("unexpected absent backend error");
        if (!FreeLibrary(module)) fail("fixture unload");
        after = TlsAlloc();
        if (after == TLS_OUT_OF_INDEXES) fail("TLS slots exhausted");
        if (!TlsFree(after)) fail("TLS slot release");
        if (after != baseline) fail("TLS slot leaked across unload");
    }
    say("PASS: 16 provider load/call/unload cycles reuse TLS slot\r\n");
    ExitProcess(0);
}
