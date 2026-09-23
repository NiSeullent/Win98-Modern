/*
 * App-local UXTHEME.DLL for an unthemed Windows 98 SE desktop.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Behavior was checked against Wine dlls/uxtheme/{draw,system,buffer}.c at
 * df15af3652511150490934682202d45af892f887 and ReactOS
 * dll/win32/uxtheme/{draw,system,buffer}.c at
 * 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8. Those LGPL-2.1-or-later
 * implementations were not copied. This file implements the Win98 branch:
 * no theme service exists, so OpenThemeData cannot return a valid HTHEME.
 * A failed BeginBufferedAnimation tells callers to paint without animation.
 * DrawThemeParentBackground paints by asking the real parent window.
 */
#define WIN32_LEAN_AND_MEAN
/* Header declarations only. The emitted PE subsystem and native imports are
 * constrained separately to Windows 98 SE by the build and PE gate. */
#define _WIN32_WINNT 0x0600
#define NTDDI_VERSION 0x06000000
#include <windows.h>
#include <uxtheme.h>

static volatile HINSTANCE module_instance;

static HRESULT m98_last_gdi_failure(void)
{
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_INVALID_HANDLE);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) module_instance = instance;
    else if (reason == DLL_PROCESS_DETACH) module_instance = NULL;
    return TRUE;
}

HTHEME WINAPI m98_OpenThemeData(HWND window, LPCWSTR class_list)
{
    (void)window;
    if (!class_list) SetLastError(ERROR_INVALID_PARAMETER);
    else SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}

HRESULT WINAPI m98_CloseThemeData(HTHEME theme)
{
    (void)theme;
    return E_HANDLE;
}

BOOL WINAPI m98_IsThemeActive(void)
{
    return FALSE;
}

BOOL WINAPI m98_IsAppThemed(void)
{
    return FALSE;
}

HTHEME WINAPI m98_GetWindowTheme(HWND window)
{
    (void)window;
    return NULL;
}

HRESULT WINAPI m98_DrawThemeTextEx(HTHEME theme, HDC dc, int part, int state,
                                   LPCWSTR text, int text_length, DWORD flags,
                                   LPRECT rect, const DTTOPTS *options)
{
    (void)theme; (void)dc; (void)part; (void)state; (void)text;
    (void)text_length; (void)flags; (void)rect; (void)options;
    /* Neither Wine nor ReactOS draws text when hTheme is NULL. This DLL
     * never issues a valid theme handle, so preserve the caller's DC/RECT. */
    return E_HANDLE;
}

HRESULT WINAPI m98_DrawThemeBackground(HTHEME theme, HDC dc, int part,
                                       int state, const RECT *rect,
                                       const RECT *clip)
{
    (void)theme; (void)dc; (void)part; (void)state;
    (void)rect; (void)clip;
    return E_HANDLE;
}

HRESULT WINAPI m98_GetThemeBackgroundContentRect(HTHEME theme, HDC dc,
                                                 int part, int state,
                                                 const RECT *bounds,
                                                 RECT *content)
{
    (void)theme; (void)dc; (void)part; (void)state;
    (void)bounds; (void)content;
    return E_HANDLE;
}

HRESULT WINAPI m98_GetThemeColor(HTHEME theme, int part, int state,
                                 int property, COLORREF *color)
{
    (void)theme; (void)part; (void)state; (void)property; (void)color;
    return E_HANDLE;
}

HRESULT WINAPI m98_GetThemeFont(HTHEME theme, HDC dc, int part, int state,
                                int property, LOGFONTW *font)
{
    (void)theme; (void)dc; (void)part; (void)state;
    (void)property; (void)font;
    return E_HANDLE;
}

HRESULT WINAPI m98_GetThemePartSize(HTHEME theme, HDC dc, int part, int state,
                                    RECT *bounds, enum THEMESIZE size_kind,
                                    SIZE *size)
{
    (void)theme; (void)dc; (void)part; (void)state;
    (void)bounds; (void)size_kind; (void)size;
    return E_HANDLE;
}

HRESULT WINAPI m98_GetThemeTransitionDuration(HTHEME theme, int part,
                                               int from_state, int to_state,
                                               int property, DWORD *duration)
{
    (void)theme; (void)part; (void)from_state; (void)to_state;
    (void)property; (void)duration;
    return E_HANDLE;
}

HRESULT WINAPI m98_DrawThemeParentBackground(HWND window, HDC dc, RECT *rect)
{
    HWND parent;
    POINT child_origin = {0, 0};
    POINT old_viewport;
    int saved;

    if (!IsWindow(window) || !dc) return E_HANDLE;
    if (rect && (rect->right < rect->left || rect->bottom < rect->top))
        return E_INVALIDARG;
    parent = GetParent(window);
    if (!parent) return S_OK;

    saved = SaveDC(dc);
    if (!saved) return m98_last_gdi_failure();
    if (rect && IntersectClipRect(dc, rect->left, rect->top,
                                  rect->right, rect->bottom) == ERROR) {
        HRESULT failure = m98_last_gdi_failure();
        RestoreDC(dc, saved);
        return failure;
    }
    /* The parent draws in its own client coordinates. Translate the child's
     * existing DC origin before WM_ERASEBKGND/WM_PRINTCLIENT. These messages
     * carry no Unicode text, so the Win98 ANSI entrypoint is sufficient. */
    /* Zero means either no offset or failure; the Win32 API does not expose
     * an unambiguous failure value here. Both cases leave a usable origin. */
    MapWindowPoints(window, parent, &child_origin, 1);
    if (!GetViewportOrgEx(dc, &old_viewport) ||
        !SetViewportOrgEx(dc, old_viewport.x - child_origin.x,
                            old_viewport.y - child_origin.y, NULL)) {
        HRESULT failure = m98_last_gdi_failure();
        RestoreDC(dc, saved);
        return failure;
    }
    SendMessageA(parent, WM_ERASEBKGND, (WPARAM)dc, 0);
    SendMessageA(parent, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    RestoreDC(dc, saved);
    return S_OK;
}

HRESULT WINAPI m98_EnableThemeDialogTexture(HWND window, DWORD flags)
{
    if (!IsWindow(window)) return E_HANDLE;
    /* There is no theme texture to enable. Disabling it is already true. */
    if (flags == 0 || (flags & ETDT_DISABLE)) return S_OK;
    return E_NOTIMPL;
}

HRESULT WINAPI m98_SetWindowTheme(HWND window, LPCWSTR app_name,
                                  LPCWSTR class_list)
{
    if (!IsWindow(window)) return E_HANDLE;
    /* Null/empty overrides request the existing unthemed appearance. */
    if ((!app_name || !*app_name) && (!class_list || !*class_list))
        return S_OK;
    return E_NOTIMPL;
}

HANIMATIONBUFFER WINAPI m98_BeginBufferedAnimation(HWND window, HDC target,
                                                    const RECT *bounds,
                                                    BP_BUFFERFORMAT format,
                                                    BP_PAINTPARAMS *paint,
                                                    BP_ANIMATIONPARAMS *animation,
                                                    HDC *from, HDC *to)
{
    (void)window; (void)target; (void)bounds; (void)format;
    (void)paint; (void)animation;
    if (from) *from = NULL;
    if (to) *to = NULL;
    /* A NULL return is the documented request for the caller's normal
     * unbuffered drawing path. No animation handle is created. */
    return NULL;
}

BOOL WINAPI m98_BufferedPaintRenderAnimation(HWND window, HDC target)
{
    (void)window; (void)target;
    return FALSE;
}

HRESULT WINAPI m98_BufferedPaintStopAllAnimations(HWND window)
{
    if (!IsWindow(window)) return E_HANDLE;
    /* There are no active animations after BeginBufferedAnimation failed. */
    return S_OK;
}

HRESULT WINAPI m98_EndBufferedAnimation(HANIMATIONBUFFER animation,
                                        BOOL update_target)
{
    (void)animation; (void)update_target;
    return E_INVALIDARG;
}
