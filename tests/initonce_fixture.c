/* Isolated API-table fixture; no process-global initialization required. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98_initonce.h"

#define API(name, implementation) { name, (unsigned long)(implementation) }
static const m98_named_api entries[] = {
    API("InitOnceBeginInitialize", m98_InitOnceBeginInitialize),
    API("InitOnceComplete", m98_InitOnceComplete),
    API("InitOnceExecuteOnce", m98_InitOnceExecuteOnce),
    API("InitOnceInitialize", m98_InitOnceInitialize)
};
static const m98_api_table tables[] = {
    { "KERNEL32.DLL", entries, 4, 0, 0 }, { 0, 0, 0, 0, 0 }
};
__declspec(dllexport) const m98_api_table *get_api_table(void) { return tables; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return TRUE;
}
