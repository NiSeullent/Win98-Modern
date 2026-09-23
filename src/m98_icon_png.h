/* PNG-in-RT_ICON bridge for Win98. GPL-2.0-only. */
#ifndef M98_ICON_PNG_H
#define M98_ICON_PNG_H

#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

HRESULT m98_icon_from_png(const BYTE *png, DWORD length, int cx, int cy,
                          HICON *icon);

#endif
