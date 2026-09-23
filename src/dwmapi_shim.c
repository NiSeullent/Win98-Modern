/*
 * App-local DWMAPI.DLL for Windows 98, which has no desktop compositor.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Consulted (no code copied): Wine dlls/dwmapi/dwmapi_main.c at
 * df15af3652511150490934682202d45af892f887 (LGPL-2.1-or-later), and
 * ReactOS dll/win32/dwmapi/dwmapi_main.c at
 * 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8 (LGPL-2.1-or-later file).
 * Both have placeholder implementations of these APIs. Microsoft's
 * DwmSetWindowAttribute contract specifies DWM_E_COMPOSITIONDISABLED when
 * composition is off; the Win98-specific result reports that actual state.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

#ifndef DWM_E_COMPOSITIONDISABLED
#define DWM_E_COMPOSITIONDISABLED ((HRESULT)0x80263001L)
#endif

/* DllMain records its module handle in image data. Besides being useful for
 * diagnostics, this makes the PE contain a base relocation: several app-local
 * shims otherwise share MinGW's 0x10000000 preferred base, and Win98 refuses
 * to load a later DLL with an empty relocation directory. */
static volatile HINSTANCE module_instance;

HRESULT WINAPI m98_DwmGetColorizationColor(DWORD *color, BOOL *opaque_blend)
{
    /* Output values are defined only on success. There is no Win98 glass
     * composition color, so leave caller storage unchanged on failure. */
    if (!color || !opaque_blend) return E_INVALIDARG;
    return DWM_E_COMPOSITIONDISABLED;
}

HRESULT WINAPI m98_DwmSetWindowAttribute(HWND window, DWORD attribute,
                                         LPCVOID value, DWORD size)
{
    (void)window;
    (void)attribute;
    (void)value;
    (void)size;
    return DWM_E_COMPOSITIONDISABLED;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) module_instance = instance;
    else if (reason == DLL_PROCESS_DETACH) module_instance = NULL;
    return TRUE;
}
