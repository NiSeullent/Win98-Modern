/*
 * COMCTL32 ordinal 381 and 345 adaptations for the Win98/KernelEx ABI.
 * Copyright (C) 2026 Win98 Modern contributors. GPL-2.0-only.
 *
 * Pinned Wine df15af3 dlls/comctl32/comctl32.spec has neither ordinal.
 * Pinned ReactOS 9dc3ca8 dll/win32/comctl32/taskdialog.c implements the
 * full NT/W dialog; its COMCTL32 spec omits the export. No upstream body is
 * copied. The original Win98 USER32 resource and dialog APIs are used here.
 */
#include "m98_comctl_ordinals.h"
#ifdef M98_WITH_PNG
#include "m98_icon_choice.h"
#include "m98_icon_png.h"
#endif

#ifndef LR_DEFAULTCOLOR
#define LR_DEFAULTCOLOR 0
#endif
#ifndef ERROR_RESOURCE_NAME_NOT_FOUND
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814
#endif

#define M98_E_NOTIMPL ((HRESULT)0x80004001UL)
#define M98_E_INVALIDARG ((HRESULT)0x80070057UL)
#define M98_E_FAIL ((HRESULT)0x80004005UL)
#define M98_MAX_ICON_EDGE 1024

static HRESULT m98_last_error(void)
{
    DWORD e = GetLastError();
    return e ? HRESULT_FROM_WIN32(e) : M98_E_FAIL;
}

static WORD m98_u16(const BYTE *p)
{
    return (WORD)(p[0] | ((WORD)p[1] << 8));
}

static DWORD m98_u32(const BYTE *p)
{
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) |
           ((DWORD)p[3] << 24);
}

static int m98_icon_edge(BYTE value)
{
    return value ? value : 256;
}

#ifdef M98_WITH_PNG
static int m98_standard_icon_edge(int edge)
{
    return edge == 16 || edge == 32 || edge == 48 || edge == 256;
}

static int m98_display_bpp(void)
{
    HDC dc = GetDC(NULL);
    int depth = 32;
    if (dc) {
        int bits = GetDeviceCaps(dc, BITSPIXEL);
        int planes = GetDeviceCaps(dc, PLANES);
        if (bits > 0 && planes > 0 && bits <= 64 && planes <= 64 &&
            bits * planes <= 64)
            depth = bits * planes;
        ReleaseDC(NULL, dc);
    }
    return depth;
}
#endif

/* Validate the resource's full directory before any selection. The PNG
 * provider uses normal closest-smaller behavior for standard sizes; all
 * builds prefer a larger source for nonstandard sizes. */
static int m98_icon_group_choice(const BYTE *group, DWORD bytes, int cx, int cy)
{
    unsigned i, count;
    int best = -1, best_class = 4;
    unsigned long best_area = 0, best_penalty = 0, best_bpp = 0;
    if (!group || bytes < 6 || m98_u16(group) != 0 ||
        m98_u16(group + 2) != 1)
        return -1;
    count = m98_u16(group + 4);
    if (!count || count > (bytes - 6) / 14) return -1;
#ifdef M98_WITH_PNG
    if (m98_standard_icon_edge(cx) && m98_standard_icon_edge(cy))
        return m98_choose_standard_icon(group, bytes, cx, cy,
                                        m98_display_bpp());
#endif
    for (i = 0; i < count; ++i) {
        const BYTE *p = group + 6 + i * 14;
        int w = m98_icon_edge(p[0]), h = m98_icon_edge(p[1]);
        int kind = w == cx && h == cy ? 0 :
                   (w >= cx && h >= cy ? 1 : 2);
        unsigned long area = (unsigned long)w * (unsigned long)h;
        unsigned long penalty = kind == 1 ? area - (unsigned long)cx * cy :
                                (kind == 2 ? (unsigned long)
                                 (w > cx ? w - cx : cx - w) +
                                 (unsigned long)(h > cy ? h - cy : cy - h) : 0);
        unsigned long bpp = m98_u16(p + 6);
        if (best < 0 || kind < best_class ||
            (kind == best_class &&
             ((kind == 0 && bpp > best_bpp) ||
              (kind == 1 && (penalty < best_penalty ||
                (penalty == best_penalty && bpp > best_bpp))) ||
              (kind == 2 && (area > best_area ||
                (area == best_area && bpp > best_bpp)))))) {
            best = (int)i;
            best_class = kind;
            best_area = area;
            best_penalty = penalty;
            best_bpp = bpp;
        }
    }
    return best;
}

/* Win98 resource names are ANSI. Reject a lossy conversion instead of
 * accidentally opening a different icon resource or pathname. */
static HRESULT m98_resource_name(LPCWSTR name, char *buffer, int capacity,
                                 LPCSTR *ansi)
{
    BOOL replaced = FALSE;
    int length;
    if (((DWORD)name >> 16) == 0) {
        *ansi = MAKEINTRESOURCEA((WORD)(DWORD)name);
        return S_OK;
    }
    length = WideCharToMultiByte(CP_ACP, 0, name, -1, buffer, capacity,
                                 NULL, &replaced);
    if (!length) return m98_last_error();
    if (replaced) return M98_E_NOTIMPL;
    *ansi = buffer;
    return S_OK;
}

HRESULT WINAPI m98_LoadIconWithScaleDown(HINSTANCE hinst, LPCWSTR name,
                                         int cx, int cy, HICON *icon)
{
    char ansi_buffer[1024];
    LPCSTR ansi;
    HRESULT hr;
    HRSRC group_res, icon_res;
    HGLOBAL group_handle, icon_handle;
    const BYTE *group, *bits, *entry;
    DWORD group_bytes, icon_bytes;
    int index;
    HICON result;
    if (!icon || !name || cx <= 0 || cy <= 0 ||
        cx > M98_MAX_ICON_EDGE || cy > M98_MAX_ICON_EDGE)
        return M98_E_INVALIDARG;
    *icon = NULL;
    hr = m98_resource_name(name, ansi_buffer, sizeof(ansi_buffer), &ansi);
    if (FAILED(hr)) return hr;
    if (!hinst) {
        DWORD flags = LR_DEFAULTCOLOR;
        if (((DWORD)name >> 16) != 0)
            flags |= LR_LOADFROMFILE;
        else
            flags |= LR_SHARED; /* Win32 stock icons require shared lookup. */
        result = (HICON)LoadImageA(NULL, ansi, IMAGE_ICON, cx, cy, flags);
        if (!result) return m98_last_error();
        if (!(flags & LR_LOADFROMFILE)) {
            HICON owned = (HICON)CopyImage(result, IMAGE_ICON, cx, cy, 0);
            if (!owned) return m98_last_error();
            result = owned;
        }
        *icon = result;
        return S_OK;
    }
    group_res = FindResourceA(hinst, ansi, RT_GROUP_ICON);
    if (!group_res) return m98_last_error();
    group_bytes = SizeofResource(hinst, group_res);
    group_handle = LoadResource(hinst, group_res);
    group = group_handle ? (const BYTE *)LockResource(group_handle) : NULL;
    index = m98_icon_group_choice(group, group_bytes, cx, cy);
    if (index < 0) return M98_E_FAIL;
    entry = group + 6 + index * 14;
    icon_res = FindResourceA(hinst, MAKEINTRESOURCEA(m98_u16(entry + 12)),
                             RT_ICON);
    if (!icon_res) return m98_last_error();
    icon_bytes = SizeofResource(hinst, icon_res);
    icon_handle = LoadResource(hinst, icon_res);
    bits = icon_handle ? (const BYTE *)LockResource(icon_handle) : NULL;
    if (!bits || !icon_bytes || !m98_u32(entry + 8)) return M98_E_FAIL;
    #ifdef M98_WITH_PNG
    if (icon_bytes >= 8 && bits[0] == 137 && bits[1] == 'P' &&
        bits[2] == 'N' && bits[3] == 'G')
        return m98_icon_from_png(bits, icon_bytes, cx, cy, icon);
    #endif
    result = CreateIconFromResourceEx((PBYTE)bits, icon_bytes, TRUE,
                                      0x00030000, cx, cy, LR_DEFAULTCOLOR);
    if (!result) return m98_last_error();
    *icon = result;
    return S_OK;
}

/* Minimal, explicit TaskDialog subset. All features requiring task-dialog
 * child controls, callbacks, or custom layout fail before showing any UI. */
static HRESULT m98_dialog_text(HINSTANCE hinst, LPCWSTR source, char *out,
                               int capacity)
{
    BOOL replaced = FALSE;
    int count;
    if (!source) { out[0] = 0; return S_OK; }
    if (((DWORD)source >> 16) == 0) {
        if (!hinst) return M98_E_INVALIDARG;
        count = LoadStringA(hinst, (UINT)(WORD)(DWORD)source, out, capacity);
        return count > 0 ? S_OK : m98_last_error();
    }
    count = WideCharToMultiByte(CP_ACP, 0, source, -1, out, capacity,
                                NULL, &replaced);
    if (!count) return m98_last_error();
    return replaced ? M98_E_NOTIMPL : S_OK;
}

static int m98_length(const char *text)
{
    int count = 0;
    while (text[count]) ++count;
    return count;
}

static void m98_append(char *dest, const char *source)
{
    int end = m98_length(dest), i = 0;
    while (source[i]) dest[end++] = source[i++];
    dest[end] = 0;
}

typedef struct m98_dialog_buffers {
    char title[512];
    char instruction[2048];
    char content[2048];
    char text[4100];
} m98_dialog_buffers;

static HRESULT m98_task_dialog_core(const m98_taskdialog_config *config,
                                    int *button, int *radio, BOOL *verified,
                                    m98_dialog_buffers *buffers)
{
    char *title = buffers->title;
    char *instruction = buffers->instruction;
    char *content = buffers->content;
    char *text = buffers->text;
    HRESULT hr;
    UINT style = 0;
    int result;
    DWORD flags;
    if (!config || config->cbSize != sizeof(*config)) return M98_E_INVALIDARG;
    flags = config->dwCommonButtons;
    if (config->dwFlags || config->cButtons || config->pButtons ||
        config->cRadioButtons || config->pRadioButtons ||
        config->pszVerificationText || config->pszExpandedInformation ||
        config->pszExpandedControlText || config->pszCollapsedControlText ||
        config->pszMainIcon || config->pszFooterIcon || config->pszFooter ||
        config->pfCallback || config->cxWidth)
        return M98_E_NOTIMPL;
    switch (flags) {
    case 0: case 1: style = MB_OK; break;
    case 1 | 8: style = MB_OKCANCEL; break;
    case 2 | 4: style = MB_YESNO; break;
    case 2 | 4 | 8: style = MB_YESNOCANCEL; break;
    case 8 | 16: style = MB_RETRYCANCEL; break;
    default: return M98_E_NOTIMPL;
    }
    if (config->nDefaultButton) {
        if ((config->nDefaultButton == IDOK &&
             (style == MB_OK || style == MB_OKCANCEL)) ||
            (config->nDefaultButton == IDYES &&
             (style == MB_YESNO || style == MB_YESNOCANCEL)) ||
            (config->nDefaultButton == IDRETRY && style == MB_RETRYCANCEL)) {
            /* First button is the native MessageBox default. */
        } else if (config->nDefaultButton == IDCANCEL &&
                   (style == MB_OKCANCEL || style == MB_RETRYCANCEL)) {
            style |= MB_DEFBUTTON2;
        } else if (config->nDefaultButton == IDNO &&
                   (style == MB_YESNO || style == MB_YESNOCANCEL)) {
            style |= MB_DEFBUTTON2;
        } else if (config->nDefaultButton == IDCANCEL &&
                   style == MB_YESNOCANCEL) {
            style |= MB_DEFBUTTON3;
        } else return M98_E_NOTIMPL;
    }
    if (!config->pszWindowTitle) {
        char *p;
        DWORD length = GetModuleFileNameA(NULL, title, sizeof(buffers->title));
        if (!length || length >= sizeof(buffers->title))
            return m98_last_error();
        p = title + m98_length(title);
        while (p > title && p[-1] != '\\' && p[-1] != '/') --p;
        if (p != title) {
            int i = 0;
            while (p[i]) { title[i] = p[i]; ++i; }
            title[i] = 0;
        }
    } else {
        hr = m98_dialog_text(config->hInstance, config->pszWindowTitle,
                             title, sizeof(buffers->title));
        if (FAILED(hr)) return hr;
    }
    hr = m98_dialog_text(config->hInstance, config->pszMainInstruction,
                         instruction, sizeof(buffers->instruction));
    if (FAILED(hr)) return hr;
    hr = m98_dialog_text(config->hInstance, config->pszContent,
                         content, sizeof(buffers->content));
    if (FAILED(hr)) return hr;
    text[0] = 0;
    m98_append(text, instruction);
    if (instruction[0] && content[0]) m98_append(text, "\r\n\r\n");
    m98_append(text, content);
    result = MessageBoxA(config->hwndParent, text, title, style);
    if (!result) return m98_last_error();
    if (button) *button = result;
    if (radio) *radio = 0;
    if (verified) *verified = FALSE;
    return S_OK;
}

HRESULT WINAPI m98_TaskDialogIndirect(const m98_taskdialog_config *config,
                                      int *button, int *radio, BOOL *verified)
{
    m98_dialog_buffers *buffers;
    HRESULT hr;
    if (!config || config->cbSize != sizeof(*config)) return M98_E_INVALIDARG;
    buffers = (m98_dialog_buffers *)HeapAlloc(GetProcessHeap(), 0,
                                               sizeof(*buffers));
    if (!buffers) return E_OUTOFMEMORY;
    hr = m98_task_dialog_core(config, button, radio, verified, buffers);
    HeapFree(GetProcessHeap(), 0, buffers);
    return hr;
}
