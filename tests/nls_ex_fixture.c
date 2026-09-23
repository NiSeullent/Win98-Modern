/* Isolated KernelEx-table fixture; production must bind m98wrap's resolver. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98nls_ex.h"

static BOOL name_is(const WCHAR *wide, const char *ascii)
{
    while (*wide && *ascii) {
        WCHAR a = *wide++;
        char b = *ascii++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != (WCHAR)b) return FALSE;
    }
    return !*wide && !*ascii;
}

static LCID test_resolve(const WCHAR *name, BOOL *invariant)
{
    *invariant = FALSE;
    if (!name) return GetUserDefaultLCID();
    if (!*name) {
        *invariant = TRUE;
        return 0x0409;
    }
    if (name_is(name, "en-US")) return 0x0409;
    if (name_is(name, "ko-KR")) return 0x0412;
    if (name_is(name, "!x-sys-default-locale"))
        return GetSystemDefaultLCID();
    return 0;
}

#define API(name, impl) { name, (unsigned long)(impl) }
static const m98_named_api nls_apis[] = {
    API("CompareStringEx", m98_CompareStringEx),
    API("LCMapStringEx", m98_LCMapStringEx)
};
static const m98_api_table tables[] = {
    { "KERNEL32.DLL", nls_apis, 2, 0, 0 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return tables;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        m98_nls_ex_set_module(instance);
        m98_nls_ex_set_resolver(test_resolve);
    } else if (reason == DLL_PROCESS_DETACH) {
        m98_nls_ex_set_resolver(0);
        m98_nls_ex_set_module(0);
    }
    return TRUE;
}
