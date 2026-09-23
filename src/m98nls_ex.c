/*
 * Windows 98 SE locale-name string mapping and comparison bridge.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Source review: Wine df15af365251 dlls/kernelbase/locale.c
 * (LCMapStringEx / CompareStringEx), ReactOS 9dc3ca8720
 * dll/win32/kernel32/winnls/string/locale.c (same APIs), and the Microsoft
 * contracts linked in docs/NLS_EX_PORT.md. No upstream code is copied.
 *
 * Wine/ReactOS carry Unicode sorting tables that are not the installed
 * Win98 NLS database. This adapter resolves a supported locale name using
 * m98wrap's validated resolver, then obtains the Unicode W implementations
 * from the installed KEXBASES.DLL API table. An API library's
 * own KERNEL32 import can still bind to the original Win98 implementation
 * stub even when an application's import is redirected by KernelEx. The
 * original-name import is only a fallback outside the KernelEx installation
 * (e.g. a direct test on a modern host). Current NLS behavior and modern collation
 * equivalence need separate guest validation.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include "m98nls_ex.h"
#include "kex_abi.h"

#ifndef LINGUISTIC_IGNORECASE
#define LINGUISTIC_IGNORECASE 0x00000010UL
#endif
#ifndef LINGUISTIC_IGNOREDIACRITIC
#define LINGUISTIC_IGNOREDIACRITIC 0x00000020UL
#endif
#ifndef SORT_DIGITSASNUMBERS
#define SORT_DIGITSASNUMBERS 0x00000008UL
#endif
#ifndef NORM_LINGUISTIC_CASING
#define NORM_LINGUISTIC_CASING 0x08000000UL
#endif
#ifndef LCMAP_LINGUISTIC_CASING
#define LCMAP_LINGUISTIC_CASING 0x01000000UL
#endif
#ifndef LCMAP_HASH
#define LCMAP_HASH 0x00040000UL
#endif
#ifndef LCMAP_SORTHANDLE
#define LCMAP_SORTHANDLE 0x20000000UL
#endif

static m98_nls_locale_resolver m98_resolve_locale;
static HMODULE m98_self_module;
/* A successful absolute-path load is kept for the lifetime of this module.
 * It owns the W function pointers returned by get_api_table. */
static LONG volatile m98_pinned_provider;

typedef int (WINAPI *m98_map_w_fn)(LCID, DWORD, const WCHAR *, int,
                                   WCHAR *, int);
typedef int (WINAPI *m98_compare_w_fn)(LCID, DWORD, const WCHAR *, int,
                                       const WCHAR *, int);
typedef const m98_api_table *(*m98_get_api_table_fn)(void);

typedef struct m98_nls_backend {
    m98_map_w_fn map;
    m98_compare_w_fn compare;
} m98_nls_backend;

static BOOL m98_same_name(const char *left, const char *right)
{
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static BOOL m98_same_path(const char *left, const char *right)
{
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return FALSE;
    }
    return !*left && !*right;
}

static BOOL m98_is_separator(char c)
{
    return c == '\\' || c == '/';
}

/* Return 1 for a module in a KernelEx directory, 0 for an independent
 * fixture, and -1 for a malformed/truncated path. A production provider is
 * always loaded from this exact sibling path, never via the DLL search path. */
static int m98_sibling_provider_path(char path[MAX_PATH])
{
    static const char provider_name[] = "KEXBASES.DLL";
    DWORD length;
    DWORD file_start, directory_start, directory_end;
    DWORD i;
    if (!m98_self_module) return 0;
    length = GetModuleFileNameA(m98_self_module, path, MAX_PATH);
    if (!length) return -1;
    if (length >= MAX_PATH) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return -1;
    }
    file_start = length;
    while (file_start && !m98_is_separator(path[file_start - 1]))
        --file_start;
    if (!file_start) {
        SetLastError(ERROR_BAD_PATHNAME);
        return -1;
    }
    directory_end = file_start - 1;
    directory_start = directory_end;
    while (directory_start &&
           !m98_is_separator(path[directory_start - 1]))
        --directory_start;
    if (directory_end - directory_start != 8) return 0;
    /* The directory name is not null-terminated; compare its eight bytes. */
    {
        static const char kernel_ex[] = "KernelEx";
        for (i = 0; i < 8; ++i) {
            unsigned char a = (unsigned char)path[directory_start + i];
            unsigned char b = (unsigned char)kernel_ex[i];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) return 0;
        }
    }
    if (file_start + sizeof(provider_name) > MAX_PATH) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return -1;
    }
    for (i = 0; i < sizeof(provider_name); ++i)
        path[file_start + i] = provider_name[i];
    return 1;
}

static HMODULE m98_load_sibling_provider(const char *path)
{
    HMODULE loaded;
    HMODULE actual_module;
    char actual_path[MAX_PATH];
    DWORD actual_length;
    LONG previous;
    LONG cached = InterlockedCompareExchange(&m98_pinned_provider, 0, 0);
    if (cached) return (HMODULE)(ULONG_PTR)cached;
    loaded = LoadLibraryA(path);
    if (!loaded) return 0;
    actual_length = GetModuleFileNameA(loaded, actual_path, MAX_PATH);
    if (!actual_length || actual_length >= MAX_PATH ||
        !m98_same_path(actual_path, path)) {
        FreeLibrary(loaded);
        SetLastError(ERROR_BAD_FORMAT);
        return 0;
    }
    previous = InterlockedCompareExchange(&m98_pinned_provider,
                                           (LONG)(ULONG_PTR)loaded, 0);
    if (previous) {
        FreeLibrary(loaded);
        actual_module = (HMODULE)(ULONG_PTR)previous;
    } else {
        actual_module = loaded;
    }
    return actual_module;
}

static int m98_name_order(const char *left, const char *right)
{
    while (*left && *right && *left == *right) { ++left; ++right; }
    return (unsigned char)*left - (unsigned char)*right;
}

/* KernelEx requires each named table to be sorted; binary search keeps a
 * collation call from scanning and probing hundreds of unrelated pointers. */
static unsigned long m98_find_named_api(const m98_api_table *table,
                                         const char *name)
{
    int first = 0, end = table->named_apis_count;
    while (first < end) {
        int middle = first + (end - first) / 2;
        const m98_named_api *api = &table->named_apis[middle];
        int order;
        if (!api->name || IsBadStringPtrA(api->name, 32)) return 0;
        order = m98_name_order(api->name, name);
        if (order < 0) first = middle + 1;
        else if (order > 0) end = middle;
        else return api->addr;
    }
    return 0;
}

/* Pinned KernelEx 31cdfc3: apilibs/kexbases/main.c returns a NULL-terminated
 * apilib_api_table[]; Kernel32/_kernel32_apilist.c registers exactly these
 * two W names with CompareStringW_new and LCMapStringW_new from locale.c.
 * See common/kexcoresdk.h for the 32-bit table layout mirrored in kex_abi.h.
 * For a module installed in KernelEx, load the sibling DLL by absolute path
 * at runtime. A KEXBASES provider need not already be in the process when a
 * statically imported Ex API is first called. This must not run in DllMain.
 * A missing or bad production provider is a hard failure, never a reason to
 * call the native Win98 stub. Independent host/guest fixtures may fall back
 * to an already-loaded provider or to the host's native W implementation. */
static BOOL m98_select_nls_backend(m98_nls_backend *backend)
{
    char provider_path[MAX_PATH];
    int sibling = m98_sibling_provider_path(provider_path);
    HMODULE module;
    m98_get_api_table_fn get_table;
    const m98_api_table *tables;
    const m98_api_table *table = 0;
    int table_index;

    backend->map = 0;
    backend->compare = 0;
    if (sibling < 0) return FALSE;
    if (sibling)
        module = m98_load_sibling_provider(provider_path);
    else
        module = GetModuleHandleA("KEXBASES.DLL");
    if (!module) {
        if (sibling) return FALSE;
        backend->map = LCMapStringW;
        backend->compare = CompareStringW;
        return TRUE;
    }
    get_table = (m98_get_api_table_fn)GetProcAddress(module, "get_api_table");
    if (!get_table) goto invalid;
    tables = get_table();
    if (!tables) goto invalid;
    for (table_index = 0; table_index < 16; ++table_index) {
        const m98_api_table *candidate = &tables[table_index];
        if (IsBadReadPtr(candidate, sizeof(*candidate))) goto invalid;
        if (!candidate->target_library) break;
        if (IsBadStringPtrA(candidate->target_library, 32)) goto invalid;
        if (m98_same_name(candidate->target_library, "KERNEL32.DLL")) {
            table = candidate;
            break;
        }
    }
    if (!table || !table->named_apis || table->named_apis_count < 1 ||
        table->named_apis_count > 1024 ||
        IsBadReadPtr(table->named_apis,
                     (UINT)table->named_apis_count * sizeof(m98_named_api)))
        goto invalid;
    backend->map = (m98_map_w_fn)m98_find_named_api(table, "LCMapStringW");
    backend->compare = (m98_compare_w_fn)m98_find_named_api(table,
                                                             "CompareStringW");
    if (backend->map && backend->compare) return TRUE;
invalid:
    backend->map = 0;
    backend->compare = 0;
    SetLastError(ERROR_BAD_FORMAT);
    return FALSE;
}

void m98_nls_ex_set_resolver(m98_nls_locale_resolver resolver)
{
    m98_resolve_locale = resolver;
}

void m98_nls_ex_set_module(HMODULE module)
{
    m98_self_module = module;
}

static LCID m98_resolve(const WCHAR *name)
{
    LCID lcid;
    BOOL invariant = FALSE;

    if (!m98_resolve_locale) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return 0;
    }
    lcid = m98_resolve_locale(name, &invariant);
    if (invariant) {
        /* Win98 has no invariant Unicode NLS record. en-US is not the
         * invariant locale and must never silently substitute for it. */
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (!lcid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return lcid;
}

int WINAPI m98_LCMapStringEx(const WCHAR *locale_name, DWORD flags,
                             const WCHAR *source, int source_length,
                             WCHAR *destination, int destination_length,
                             void *version, void *reserved, LPARAM sort_handle)
{
    const DWORD legacy = LCMAP_LOWERCASE | LCMAP_UPPERCASE | LCMAP_SORTKEY |
                         NORM_IGNORECASE | NORM_IGNORESYMBOLS |
                         LOCALE_USE_CP_ACP;
    /* The active KernelEx W implementation accepts several historical
     * constants but ignores their requested transformation. Do not report
     * success for such a no-op. See locale.c and locale_sortkey.c in the
     * pinned KernelEx tree, plus the guest baseline in NLS_EX_PORT.md. */
    const DWORD unavailable = LCMAP_BYTEREV | LCMAP_HIRAGANA |
                              LCMAP_KATAKANA | LCMAP_HALFWIDTH |
                              LCMAP_FULLWIDTH | LCMAP_SIMPLIFIED_CHINESE |
                              LCMAP_TRADITIONAL_CHINESE |
                              NORM_IGNORENONSPACE | NORM_IGNOREKANATYPE |
                              NORM_IGNOREWIDTH | SORT_STRINGSORT;
    const DWORD modern = LCMAP_HASH | LCMAP_SORTHANDLE |
                         LCMAP_LINGUISTIC_CASING |
                         LINGUISTIC_IGNORECASE |
                         LINGUISTIC_IGNOREDIACRITIC |
                         SORT_DIGITSASNUMBERS;
    const DWORD sort_options = NORM_IGNORECASE | NORM_IGNORESYMBOLS;
    LCID lcid;
    int queried, written;
    HANDLE heap;
    BYTE *scratch;
    DWORD native_error;
    m98_nls_backend backend;

    if (!source || !source_length || destination_length < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (destination_length && !destination) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    if (reserved || sort_handle) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (version) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (flags & ~(legacy | unavailable | modern)) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (!(flags & LCMAP_SORTKEY) &&
        (flags & (sort_options | SORT_STRINGSORT))) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (flags & (unavailable | modern) ||
        (flags & (LCMAP_UPPERCASE | LCMAP_LOWERCASE)) ==
            (LCMAP_UPPERCASE | LCMAP_LOWERCASE)) {
        /* LCMAP_TITLECASE shares the UPPERCASE|LOWERCASE bit pattern.
         * Win98 cannot provide the Windows 7 titlecase contract. */
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (!(flags & (LCMAP_SORTKEY | LCMAP_UPPERCASE | LCMAP_LOWERCASE))) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if ((flags & LCMAP_SORTKEY) &&
        (flags & (LCMAP_UPPERCASE | LCMAP_LOWERCASE))) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (source == destination &&
        (flags & ~(LCMAP_UPPERCASE | LCMAP_LOWERCASE |
                   LOCALE_USE_CP_ACP))) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }

    lcid = m98_resolve(locale_name);
    if (!lcid) return 0;
    if (!m98_select_nls_backend(&backend)) return 0;
    /* The Unicode W API does not use an ANSI code page. For sort keys the
     * destination length and return value are bytes, not WCHAR elements.
     * With the installed KernelEx provider on the Win98 guest,
     * LCMapStringW's sort-key size query returned 21 while filling returned
     * 20 bytes for the same input. Query by filling a private buffer so Ex
     * returns the actual byte count on either Win98 or a modern host. */
    flags &= ~LOCALE_USE_CP_ACP;
    if ((flags & LCMAP_SORTKEY) && !destination_length) {
        queried = backend.map(lcid, flags, source, source_length, 0, 0);
        if (!queried) return 0;
        heap = GetProcessHeap();
        if (!heap) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        scratch = (BYTE *)HeapAlloc(heap, 0, (SIZE_T)queried);
        if (!scratch) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        written = backend.map(lcid, flags, source, source_length,
                              (WCHAR *)scratch, queried);
        native_error = GetLastError();
        HeapFree(heap, 0, scratch);
        if (written <= 0 || written > queried) {
            if (written > queried) SetLastError(ERROR_INVALID_DATA);
            else SetLastError(native_error);
            return 0;
        }
        return written;
    }
    return backend.map(lcid, flags, source, source_length,
                       destination, destination_length);
}

int WINAPI m98_CompareStringEx(const WCHAR *locale_name, DWORD flags,
                               const WCHAR *first, int first_length,
                               const WCHAR *second, int second_length,
                               void *version, void *reserved, LPARAM sort_handle)
{
    const DWORD legacy = NORM_IGNORECASE | NORM_IGNORENONSPACE |
                         NORM_IGNORESYMBOLS | SORT_STRINGSORT |
                         LOCALE_USE_CP_ACP;
    const DWORD unavailable = NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH;
    const DWORD modern = NORM_LINGUISTIC_CASING |
                         LINGUISTIC_IGNORECASE |
                         LINGUISTIC_IGNOREDIACRITIC |
                         SORT_DIGITSASNUMBERS;
    LCID lcid;
    int result;
    m98_nls_backend backend;

    if (!first || !second) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (reserved || sort_handle) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (version) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (flags & ~(legacy | unavailable | modern)) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (flags & (unavailable | modern)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    lcid = m98_resolve(locale_name);
    if (!lcid) return 0;
    if (!m98_select_nls_backend(&backend)) return 0;
    result = backend.compare(lcid, flags & ~LOCALE_USE_CP_ACP,
                             first, first_length, second, second_length);
    if (result && result != CSTR_LESS_THAN && result != CSTR_EQUAL &&
        result != CSTR_GREATER_THAN) {
        SetLastError(ERROR_INVALID_DATA);
        return 0;
    }
    return result;
}
