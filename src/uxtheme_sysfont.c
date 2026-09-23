/* Windows 98 system-font metric bridge for KernelEx UXTHEME.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Reviewed Wine dlls/uxtheme/metric.c at
 * df15af3652511150490934682202d45af892f887 and ReactOS
 * dll/win32/uxtheme/metric.c at 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8.
 * Their LGPL-2.1-or-later implementations were consulted, not copied.
 * Pinned KernelEx auxiliary/uxtheme/metric.c passes &plf for the icon-title
 * case, so USER32 writes over its local pointer rather than the caller's
 * LOGFONTW. This replacement uses Win98's ANSI metrics API, then converts
 * only the face name to UTF-16 while preserving every numeric font field.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>

static HRESULT m98_font_from_ansi(const LOGFONTA *source, LOGFONTW *destination)
{
    LOGFONTA ansi = *source;
    LOGFONTW wide = {0};
    DWORD error;

    /* Win98 returns a bounded face-name array. Force termination before
     * asking Kernel32 to find its length in the active ANSI code page. */
    ansi.lfFaceName[LF_FACESIZE - 1] = 0;
    wide.lfHeight = ansi.lfHeight;
    wide.lfWidth = ansi.lfWidth;
    wide.lfEscapement = ansi.lfEscapement;
    wide.lfOrientation = ansi.lfOrientation;
    wide.lfWeight = ansi.lfWeight;
    wide.lfItalic = ansi.lfItalic;
    wide.lfUnderline = ansi.lfUnderline;
    wide.lfStrikeOut = ansi.lfStrikeOut;
    wide.lfCharSet = ansi.lfCharSet;
    wide.lfOutPrecision = ansi.lfOutPrecision;
    wide.lfClipPrecision = ansi.lfClipPrecision;
    wide.lfQuality = ansi.lfQuality;
    wide.lfPitchAndFamily = ansi.lfPitchAndFamily;
    if (!MultiByteToWideChar(CP_ACP, 0, ansi.lfFaceName, -1,
                             wide.lfFaceName, LF_FACESIZE)) {
        error = GetLastError();
        return HRESULT_FROM_WIN32(error ? error : ERROR_INVALID_DATA);
    }
    *destination = wide;
    return S_OK;
}

HRESULT WINAPI m98_GetThemeSysFont(HTHEME theme, int font_id, LOGFONTW *font)
{
    LOGFONTA icon;
    NONCLIENTMETRICSA metrics;
    const LOGFONTA *selected;
    DWORD error;

    if (!font) return E_POINTER;
    /* This KnownDLL cannot create an HTHEME: OpenThemeData returns NULL.
     * A non-null handle is therefore not a valid source of theme metrics. */
    if (theme) return E_HANDLE;

    /* Reject unknown IDs before calling USER32, leaving caller storage alone
     * for every failure path. The six IDs are the documented system fonts. */
    switch (font_id) {
    case TMT_ICONTITLEFONT:
    case TMT_CAPTIONFONT:
    case TMT_SMALLCAPTIONFONT:
    case TMT_MENUFONT:
    case TMT_STATUSFONT:
    case TMT_MSGBOXFONT:
        break;
    default:
        return STG_E_INVALIDPARAMETER;
    }

    if (font_id == TMT_ICONTITLEFONT) {
        if (!SystemParametersInfoA(SPI_GETICONTITLELOGFONT,
                                   sizeof(icon), &icon, 0)) {
            error = GetLastError();
            return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
        }
        return m98_font_from_ansi(&icon, font);
    }

    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoA(SPI_GETNONCLIENTMETRICS,
                               sizeof(metrics), &metrics, 0)) {
        error = GetLastError();
        return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
    }
    switch (font_id) {
    case TMT_CAPTIONFONT: selected = &metrics.lfCaptionFont; break;
    case TMT_SMALLCAPTIONFONT: selected = &metrics.lfSmCaptionFont; break;
    case TMT_MENUFONT: selected = &metrics.lfMenuFont; break;
    case TMT_STATUSFONT: selected = &metrics.lfStatusFont; break;
    default: selected = &metrics.lfMessageFont; break;
    }
    return m98_font_from_ansi(selected, font);
}
