/* Win98 NLS bridges for the locale-name comparison and mapping APIs.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 */
#ifndef M98_NLS_EX_H
#define M98_NLS_EX_H

#include <windows.h>

/* The owner of the API table must register the existing, synchronized
 * locale-name resolver during DLL_PROCESS_ATTACH, after its lock is ready.
 * The resolver returns zero for an unavailable name and marks L"" as the
 * unsupported invariant locale. Neither entry point guesses a fallback. */
typedef LCID (*m98_nls_locale_resolver)(const WCHAR *name, BOOL *invariant);
void m98_nls_ex_set_resolver(m98_nls_locale_resolver resolver);
/* Pass DllMain's own hinstDLL. This stores a handle only; it never loads a
 * DLL while the loader lock is held. Runtime calls may load a verified
 * KEXBASES.DLL by absolute path from the same KernelEx installation. */
void m98_nls_ex_set_module(HMODULE module);

/* These signatures match the 32-bit KERNEL32 stdcall exports. The version
 * argument is an LPNLSVERSIONINFO; void* avoids requiring Vista headers. */
int WINAPI m98_LCMapStringEx(const WCHAR *locale_name, DWORD flags,
                             const WCHAR *source, int source_length,
                             WCHAR *destination, int destination_length,
                             void *version, void *reserved, LPARAM sort_handle);
int WINAPI m98_CompareStringEx(const WCHAR *locale_name, DWORD flags,
                               const WCHAR *first, int first_length,
                               const WCHAR *second, int second_length,
                               void *version, void *reserved, LPARAM sort_handle);

#endif
