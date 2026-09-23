/* Isolated KernelEx ABI fixture. The four FLS APIs and lifecycle hooks share
 * one process-local backend; no Windows FLS export is imported by this DLL. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98_fls.h"

#define API(name, fn) { name, (unsigned long)(fn) }
static const m98_named_api entries[] = {
    API("FlsAlloc", m98_FlsAlloc),
    API("FlsFree", m98_FlsFree),
    API("FlsGetValue", m98_FlsGetValue),
    API("FlsSetValue", m98_FlsSetValue),
    API("CreateFiber", m98_CreateFiber),
    API("CreateFiberEx", m98_CreateFiberEx),
    API("ConvertThreadToFiber", m98_ConvertThreadToFiber),
    API("ConvertThreadToFiberEx", m98_ConvertThreadToFiberEx),
    API("SwitchToFiber", m98_SwitchToFiber),
    API("DeleteFiber", m98_DeleteFiber),
    API("CreateThread", m98_CreateThread),
    API("ExitThread", m98_ExitThread),
    API("FreeLibraryAndExitThread", m98_FreeLibraryAndExitThread)
};
static const m98_api_table tables[] = {
    { "KERNEL32.DLL", entries, sizeof(entries) / sizeof(entries[0]), 0, 0 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return tables;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return TRUE; /* No user callback from DllMain or loader lock. */
}
