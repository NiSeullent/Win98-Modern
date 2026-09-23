/* KernelEx GDI32 alpha/transparent/gradient API library. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "kex_abi.h"
#include "m98_gdi_alpha.h"

#define M98_API(name, implementation) { name, (unsigned long)(implementation) }
static const m98_named_api entries[] = {
    M98_API("GdiAlphaBlend", m98_GdiAlphaBlend),
    M98_API("GdiGradientFill", m98_GdiGradientFill),
    M98_API("GdiTransparentBlt", m98_GdiTransparentBlt)
};
static const m98_api_table tables[] = {
    { "GDI32.DLL", entries, sizeof(entries) / sizeof(entries[0]), 0, 0 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return tables;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) m98_gdi_alpha_attach(instance);
    if (reason == DLL_PROCESS_DETACH) m98_gdi_alpha_detach();
    (void)reserved;
    /* MSIMG32 loading and PE inspection happen only after DllMain. */
    return TRUE;
}
