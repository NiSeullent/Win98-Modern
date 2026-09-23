/* Isolated KernelEx API-table fixture for the Win98 work-object module. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98_threadpool.h"

#define API(name, impl) { name, (unsigned long)(impl) }
static const m98_named_api work_apis[] = {
    API("CloseThreadpoolWork", m98_CloseThreadpoolWork),
    API("CreateThreadpoolWork", m98_CreateThreadpoolWork),
    API("FreeLibraryWhenCallbackReturns", m98_FreeLibraryWhenCallbackReturns),
    API("SubmitThreadpoolWork", m98_SubmitThreadpoolWork),
    API("WaitForThreadpoolWorkCallbacks", m98_WaitForThreadpoolWorkCallbacks)
};
static const m98_api_table tables[] = {
    { "KERNEL32.DLL", work_apis, 5, 0, 0 },
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
    if (reason == DLL_PROCESS_ATTACH) return m98_tp_initialize();
    if (reason == DLL_PROCESS_DETACH) m98_tp_process_detach();
    return TRUE;
}
