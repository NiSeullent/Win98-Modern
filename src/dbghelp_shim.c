/*
 * DBGHELP.DLL bridge for the ImageNtHeader import used by Notepad++ 8.9.8.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * The Korean Win98 SE OEM IMAGEHLP.DLL (SHA-256 60b564852ca292d523b48e1152ea61a7f1f169dcb3d4cdf970907efdb4eaee23)
 * exports ImageNtHeader. This bridge calls that native implementation rather
 * than reimplementing or copying Wine/ReactOS PE parsing. The function has the
 * 32-bit IMAGEAPI/WINAPI calling convention and returns NULL on failure.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef PIMAGE_NT_HEADERS (WINAPI *image_nt_header_fn)(PVOID);

/* Resolve from the installed OS directory, not the application's DLL search
 * path, so an app-local IMAGEHLP.DLL cannot accidentally replace the Win98 API. */
static HMODULE load_native_imagehlp(void)
{
    static const char name[] = "IMAGEHLP.DLL";
    char path[MAX_PATH];
    UINT length = GetSystemDirectoryA(path, sizeof(path));
    UINT i;

    if (!length) return NULL;
    if (length >= sizeof(path)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return NULL;
    }
    if (path[length - 1] != '\\' && path[length - 1] != '/') {
        if (length + 1 + sizeof(name) > sizeof(path)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return NULL;
        }
        path[length++] = '\\';
    } else if (length + sizeof(name) > sizeof(path)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return NULL;
    }
    for (i = 0; i < sizeof(name); ++i)
        path[length + i] = name[i];
    return LoadLibraryA(path);
}

PIMAGE_NT_HEADERS WINAPI m98_ImageNtHeader(PVOID base)
{
    DWORD incoming_error = GetLastError();
    HMODULE imagehlp = load_native_imagehlp();
    image_nt_header_fn native;
    PIMAGE_NT_HEADERS header;
    DWORD error;

    if (!imagehlp) return NULL;
    native = (image_nt_header_fn)GetProcAddress(imagehlp, "ImageNtHeader");
    if (!native) {
        error = GetLastError();
        FreeLibrary(imagehlp);
        SetLastError(error);
        return NULL;
    }
    /* Successful module lookup must not change the error observed by the
     * native call when it leaves GetLastError untouched. */
    SetLastError(incoming_error);
    header = native(base);
    error = GetLastError();
    FreeLibrary(imagehlp);
    SetLastError(error);
    return header;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
