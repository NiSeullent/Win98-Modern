/* KernelEx USER32 clipboard API library. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "kex_abi.h"
#include "m98_clipboard.h"

#define M98_API(name, implementation) { name, (unsigned long)(implementation) }
static const m98_named_api entries[] = {
    M98_API("AddClipboardFormatListener", m98_AddClipboardFormatListener),
    M98_API("GetUpdatedClipboardFormats", m98_GetUpdatedClipboardFormats),
    M98_API("RemoveClipboardFormatListener", m98_RemoveClipboardFormatListener)
};
static const m98_api_table tables[] = {
    { "USER32.DLL", entries, sizeof(entries) / sizeof(entries[0]), 0, 0 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return tables;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) m98_clipboard_attach(instance);
    return TRUE;
}
