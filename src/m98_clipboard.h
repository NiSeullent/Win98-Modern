/* Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#ifndef M98_CLIPBOARD_H
#define M98_CLIPBOARD_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI m98_AddClipboardFormatListener(HWND window);
BOOL WINAPI m98_RemoveClipboardFormatListener(HWND window);
BOOL WINAPI m98_GetUpdatedClipboardFormats(PUINT formats, UINT capacity,
                                           PUINT count_out);

/* DllMain may only publish the handle. Worker creation, pinning and native
 * viewer-chain operations happen later in normal API-call context. */
void m98_clipboard_attach(HINSTANCE instance);
#endif
