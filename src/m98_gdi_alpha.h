/* Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#ifndef M98_GDI_ALPHA_H
#define M98_GDI_ALPHA_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI m98_GdiAlphaBlend(HDC dest, int dx, int dy, int dw, int dh,
                               HDC source, int sx, int sy, int sw, int sh,
                               BLENDFUNCTION blend);
BOOL WINAPI m98_GdiTransparentBlt(HDC dest, int dx, int dy, int dw, int dh,
                                   HDC source, int sx, int sy, int sw, int sh,
                                   UINT transparent);
BOOL WINAPI m98_GdiGradientFill(HDC dc, PTRIVERTEX vertices, ULONG vertex_count,
                                 PVOID mesh, ULONG mesh_count, ULONG mode);
void m98_gdi_alpha_attach(HINSTANCE instance);
void m98_gdi_alpha_detach(void);

#endif
