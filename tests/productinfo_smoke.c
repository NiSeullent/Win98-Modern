/* Direct KernelEx API-table behavior, using only Win98-native imports. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef BOOL (WINAPI *product_info_fn)(DWORD, DWORD, DWORD, DWORD, DWORD *);

static void say(const char *message)
{
    DWORD n = 0, written;
    while (message[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, n, &written, 0);
}

static BOOL same(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

void mainCRTStartup(void)
{
    HMODULE module = LoadLibraryA("m98wrap.dll");
    const m98_api_table *table;
    get_api_table_fn get_table;
    product_info_fn function = 0;
    DWORD product = 0xcccccccc;
    int i;
    if (!module) goto fail;
    get_table = (get_api_table_fn)GetProcAddress(module, "get_api_table");
    if (!get_table) goto fail;
    table = get_table();
    if (!table || !same(table->target_library, "KERNEL32.DLL")) goto fail;
    for (i = 0; i < table->named_apis_count; ++i)
        if (same(table->named_apis[i].name, "GetProductInfo"))
            function = (product_info_fn)table->named_apis[i].addr;
    if (!function) goto fail;
    if (function(5, 1, 0, 0, &product) || product != PRODUCT_UNDEFINED ||
        GetLastError() != ERROR_INVALID_PARAMETER) goto fail;
    if (!function(6, 1, 1, 0, &product) || product != PRODUCT_ULTIMATE)
        goto fail;
    if (!function(6, 3, 0, 0, &product) || product != PRODUCT_PROFESSIONAL)
        goto fail;
    if (!function(10, 0, 0, 0, &product) || product != PRODUCT_PROFESSIONAL)
        goto fail;
    if (function(10, 0, 0, 0, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER) goto fail;
    say("PASS: GetProductInfo Win98 profile behavior\r\n");
    FreeLibrary(module);
    ExitProcess(0);
fail:
    say("FAIL: GetProductInfo Win98 profile behavior\r\n");
    ExitProcess(1);
}
