/* Isolated seven KERNEL32 + eight NTDLL API-table fixture. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98_slist.h"
#define API(n, f) { n, (unsigned long)(f) }
static const m98_named_api kernel32[] = {
    API("InitializeSListHead", m98_InitializeSListHead),
    API("InterlockedFlushSList", m98_InterlockedFlushSList),
    API("InterlockedPopEntrySList", m98_InterlockedPopEntrySList),
    API("InterlockedPushEntrySList", m98_InterlockedPushEntrySList),
    API("InterlockedPushListSList", m98_InterlockedPushListSList),
    API("InterlockedPushListSListEx", m98_InterlockedPushListSListEx),
    API("QueryDepthSList", m98_QueryDepthSList)
};
static const m98_named_api ntdll[] = {
    API("RtlFirstEntrySList", m98_RtlFirstEntrySList),
    API("RtlInitializeSListHead", m98_RtlInitializeSListHead),
    API("RtlInterlockedFlushSList", m98_RtlInterlockedFlushSList),
    API("RtlInterlockedPopEntrySList", m98_RtlInterlockedPopEntrySList),
    API("RtlInterlockedPushEntrySList", m98_RtlInterlockedPushEntrySList),
    API("RtlInterlockedPushListSList", m98_RtlInterlockedPushListSList),
    API("RtlInterlockedPushListSListEx", m98_RtlInterlockedPushListSListEx),
    API("RtlQueryDepthSList", m98_RtlQueryDepthSList)
};
static const m98_api_table tables[] = {
    { "KERNEL32.DLL", kernel32, 7, 0, 0 },
    { "NTDLL.DLL", ntdll, 8, 0, 0 }, { 0, 0, 0, 0, 0 }
};
__declspec(dllexport) const m98_api_table *get_api_table(void) { return tables; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{ (void)instance; (void)reason; (void)reserved; return TRUE; }
