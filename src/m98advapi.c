/*
 * RegGetValueA/W for the Windows 98 SE KernelEx API-library ABI.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Contract and source analysis: see docs/ADVAPI_REGGETVALUE_PORT.md.
 * Reviewed Wine df15af3 dlls/kernelbase/registry.c (RegGetValueA/W and
 * apply_restrictions) and ReactOS 9dc3ca8 dll/win32/advapi32/reg/reg.c
 * (RegGetValueA/W and RegpApplyRestrictions). Both upstream files are LGPL
 * 2.1-or-later; this is independent code, not a copy of either routine.
 *
 * Win98 stores registry strings in its ANSI code page. Its W exports may be
 * compatibility stubs, so this DLL calls verified native A registry exports
 * and converts only registry string types to UTF-16. Binary data is never
 * transcoded. An unrepresentable Unicode key/value name fails explicitly.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "kex_abi.h"

#define M98_RRF_RT_REG_NONE      0x00000001UL
#define M98_RRF_RT_REG_SZ        0x00000002UL
#define M98_RRF_RT_REG_EXPAND_SZ 0x00000004UL
#define M98_RRF_RT_REG_BINARY    0x00000008UL
#define M98_RRF_RT_REG_DWORD     0x00000010UL
#define M98_RRF_RT_REG_MULTI_SZ  0x00000020UL
#define M98_RRF_RT_REG_QWORD     0x00000040UL
#define M98_RRF_RT_DWORD        0x00000018UL
#define M98_RRF_RT_QWORD        0x00000048UL
#define M98_RRF_RT_ANY          0x0000ffffUL
#define M98_RRF_WOW64_64KEY     0x00010000UL
#define M98_RRF_WOW64_32KEY     0x00020000UL
#define M98_RRF_WOW64_MASK      0x00030000UL
#define M98_RRF_NOEXPAND        0x10000000UL
#define M98_RRF_ZEROONFAILURE   0x20000000UL

static void m98_copy(void *target, const void *source, DWORD count)
{
    BYTE *to = (BYTE *)target;
    const BYTE *from = (const BYTE *)source;
    DWORD i;
    for (i = 0; i < count; ++i) to[i] = from[i];
}

static void m98_zero(void *target, DWORD count)
{
    volatile BYTE *to = (volatile BYTE *)target;
    DWORD i;
    for (i = 0; i < count; ++i) to[i] = 0;
}

static LONG m98_fail(LONG status, void *data, DWORD capacity, DWORD flags)
{
    if (status && data && (flags & M98_RRF_ZEROONFAILURE))
        m98_zero(data, capacity);
    return status;
}

static int m98_is_string(DWORD type)
{
    return type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ;
}

static DWORD m98_type_bit(DWORD type)
{
    switch (type) {
    case REG_NONE: return M98_RRF_RT_REG_NONE;
    case REG_SZ: return M98_RRF_RT_REG_SZ;
    case REG_EXPAND_SZ: return M98_RRF_RT_REG_EXPAND_SZ;
    case REG_BINARY: return M98_RRF_RT_REG_BINARY;
    case REG_DWORD: return M98_RRF_RT_REG_DWORD;
    case REG_MULTI_SZ: return M98_RRF_RT_REG_MULTI_SZ;
    case REG_QWORD: return M98_RRF_RT_REG_QWORD;
    default: return 0;
    }
}

static LONG m98_check_type(DWORD flags, DWORD type, DWORD bytes)
{
    DWORD allowed = flags & M98_RRF_RT_ANY;
    DWORD bit = m98_type_bit(type);
    if (allowed != M98_RRF_RT_ANY && (!bit || !(allowed & bit)))
        return ERROR_UNSUPPORTED_TYPE;
    if (type == REG_BINARY &&
        ((allowed == M98_RRF_RT_DWORD && bytes != sizeof(DWORD)) ||
         (allowed == M98_RRF_RT_QWORD && bytes != 8)))
        return ERROR_DATATYPE_MISMATCH;
    return ERROR_SUCCESS;
}

/* Win98 has a single ANSI registry view. The WOW64 single-view selectors
 * therefore both address that view; only their prohibited combination fails.
 * Check round-trip equality to avoid silently addressing a different key
 * when the active ACP substitutes a Unicode character. */
static LONG m98_name_to_acp(const WCHAR *wide, char **result)
{
    HANDLE heap = GetProcessHeap();
    char *ansi = NULL;
    WCHAR *again = NULL;
    int size, count, original_count = 0, i;
    *result = NULL;
    if (!wide) return ERROR_SUCCESS;
    size = WideCharToMultiByte(CP_ACP, 0, wide, -1, NULL, 0, NULL, NULL);
    if (!size) return GetLastError();
    ansi = (char *)HeapAlloc(heap, 0, (SIZE_T)size);
    if (!ansi) return ERROR_NOT_ENOUGH_MEMORY;
    if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, ansi, size, NULL, NULL)) {
        LONG status = GetLastError();
        HeapFree(heap, 0, ansi);
        return status;
    }
    count = MultiByteToWideChar(CP_ACP, 0, ansi, size, NULL, 0);
    if (!count) {
        LONG status = GetLastError();
        HeapFree(heap, 0, ansi);
        return status;
    }
    again = (WCHAR *)HeapAlloc(heap, 0, (SIZE_T)count * sizeof(WCHAR));
    if (!again) {
        HeapFree(heap, 0, ansi);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!MultiByteToWideChar(CP_ACP, 0, ansi, size, again, count)) {
        LONG status = GetLastError();
        HeapFree(heap, 0, again);
        HeapFree(heap, 0, ansi);
        return status;
    }
    while (wide[original_count]) {
        if (original_count == 0x7ffffffe) {
            HeapFree(heap, 0, again);
            HeapFree(heap, 0, ansi);
            return ERROR_INVALID_PARAMETER;
        }
        ++original_count;
    }
    ++original_count; /* include NUL, and never read past it below */
    if (count != original_count) {
        HeapFree(heap, 0, again);
        HeapFree(heap, 0, ansi);
        return ERROR_NO_UNICODE_TRANSLATION;
    }
    for (i = 0; i < count; ++i) {
        if (wide[i] != again[i]) {
            HeapFree(heap, 0, again);
            HeapFree(heap, 0, ansi);
            return ERROR_NO_UNICODE_TRANSLATION;
        }
    }
    HeapFree(heap, 0, again);
    *result = ansi;
    return ERROR_SUCCESS;
}

/* Query a private buffer so type filtering, string terminators and size-only
 * calls do not leak partial native data to the caller. The value can grow
 * between the size query and read, so retry boundedly on ERROR_MORE_DATA. */
static LONG m98_read_value(HKEY key, const char *name, DWORD *type,
                           BYTE **result, DWORD *result_bytes)
{
    HANDLE heap = GetProcessHeap();
    DWORD required = 0, bytes, i;
    BYTE *buffer;
    LONG status;
    *result = NULL;
    *result_bytes = 0;
    status = RegQueryValueExA(key, name, NULL, type, NULL, &required);
    if (status) return status;
    for (i = 0; i < 8; ++i) {
        if (required > 0xfffffff0UL) return ERROR_NOT_ENOUGH_MEMORY;
        buffer = (BYTE *)HeapAlloc(heap, 0, (SIZE_T)required + 2);
        if (!buffer) return ERROR_NOT_ENOUGH_MEMORY;
        bytes = required;
        status = RegQueryValueExA(key, name, NULL, type, buffer, &bytes);
        if (status == ERROR_SUCCESS) {
            *result = buffer;
            *result_bytes = bytes;
            return ERROR_SUCCESS;
        }
        HeapFree(heap, 0, buffer);
        if (status != ERROR_MORE_DATA) return status;
        required = bytes > required ? bytes : required + 64;
    }
    return ERROR_MORE_DATA;
}

static LONG m98_expand_ansi(BYTE **buffer, DWORD *bytes)
{
    HANDLE heap = GetProcessHeap();
    BYTE *expanded;
    DWORD required, written;
    DWORD attempt;
    (*buffer)[*bytes] = 0; /* allocation has two extra bytes */
    required = ExpandEnvironmentStringsA((const char *)*buffer, NULL, 0);
    if (!required || required > 0xfffffff0UL) return ERROR_NOT_ENOUGH_MEMORY;
    for (attempt = 0; attempt < 8; ++attempt) {
        expanded = (BYTE *)HeapAlloc(heap, 0, (SIZE_T)required);
        if (!expanded) return ERROR_NOT_ENOUGH_MEMORY;
        written = ExpandEnvironmentStringsA((const char *)*buffer,
                                             (char *)expanded, required);
        if (written && written <= required) {
            HeapFree(heap, 0, *buffer);
            *buffer = expanded;
            *bytes = written; /* includes the terminating NUL */
            return ERROR_SUCCESS;
        }
        HeapFree(heap, 0, expanded);
        if (!written || written > 0xfffffff0UL) return ERROR_NOT_ENOUGH_MEMORY;
        required = written;
    }
    return ERROR_MORE_DATA;
}

static LONG m98_string_to_wide(BYTE *source, DWORD bytes, DWORD type,
                               BYTE **result, DWORD *result_bytes)
{
    HANDLE heap = GetProcessHeap();
    WCHAR *wide;
    int count;
    DWORD length, required;
    if (bytes > 0x7ffffffcUL) return ERROR_NOT_ENOUGH_MEMORY;
    /* A Win98 ANSI registry value may omit its trailing NUL. Convert the
     * exact stored byte sequence, then normalize terminators in WCHARs. */
    count = bytes ? MultiByteToWideChar(CP_ACP, 0, (const char *)source,
                                      (int)bytes, NULL, 0) : 0;
    if (bytes && !count) return GetLastError();
    required = (DWORD)count + (type == REG_MULTI_SZ ? 2 : 1);
    if (required > 0x7fffffffUL / sizeof(WCHAR)) return ERROR_NOT_ENOUGH_MEMORY;
    wide = (WCHAR *)HeapAlloc(heap, 0, (SIZE_T)required * sizeof(WCHAR));
    if (!wide) return ERROR_NOT_ENOUGH_MEMORY;
    if (count && !MultiByteToWideChar(CP_ACP, 0, (const char *)source,
                                     (int)bytes, wide, count)) {
        LONG status = GetLastError();
        HeapFree(heap, 0, wide);
        return status;
    }
    length = (DWORD)count;
    if (type == REG_MULTI_SZ) {
        if (length < 2 || wide[length - 1] || wide[length - 2]) {
            wide[length++] = 0;
            if (length < 2 || wide[length - 2]) wide[length++] = 0;
        }
    } else if (!length || wide[length - 1]) {
        wide[length++] = 0;
    }
    *result = (BYTE *)wide;
    *result_bytes = length * sizeof(WCHAR);
    return ERROR_SUCCESS;
}

static LONG WINAPI m98_RegGetValueW(HKEY root, LPCWSTR subkey,
                                    LPCWSTR value, DWORD flags,
                                    LPDWORD type_out, PVOID data,
                                    LPDWORD bytes_out)
{
    HANDLE heap = GetProcessHeap();
    char *ansi_subkey = NULL, *ansi_value = NULL;
    HKEY key = root;
    BYTE *raw = NULL, *converted = NULL, *output = NULL;
    DWORD raw_bytes = 0, bytes = 0, type = REG_NONE;
    DWORD capacity = (data && bytes_out) ? *bytes_out : 0;
    LONG status;
    BOOL opened = FALSE;

    if (data && !bytes_out) return ERROR_INVALID_PARAMETER;
    if ((flags & M98_RRF_WOW64_MASK) == M98_RRF_WOW64_MASK ||
        ((flags & M98_RRF_RT_REG_EXPAND_SZ) && !(flags & M98_RRF_NOEXPAND) &&
         (flags & M98_RRF_RT_ANY) != M98_RRF_RT_ANY))
        return m98_fail(ERROR_INVALID_PARAMETER, data, capacity, flags);

    status = m98_name_to_acp(subkey, &ansi_subkey);
    if (status) goto done;
    status = m98_name_to_acp(value, &ansi_value);
    if (status) goto done;
    if (ansi_subkey && *ansi_subkey) {
        status = RegOpenKeyExA(root, ansi_subkey, 0, KEY_QUERY_VALUE, &key);
        if (status) goto done;
        opened = TRUE;
    }
    status = m98_read_value(key, ansi_value, &type, &raw, &raw_bytes);
    if (status) goto done;
    output = raw;
    bytes = raw_bytes;
    if (type == REG_EXPAND_SZ && !(flags & M98_RRF_NOEXPAND)) {
        status = m98_expand_ansi(&raw, &raw_bytes);
        if (status) goto done;
        output = raw;
        bytes = raw_bytes;
        type = REG_SZ;
    }
    if (m98_is_string(type)) {
        status = m98_string_to_wide(raw, raw_bytes, type, &converted, &bytes);
        if (status) goto done;
        output = converted;
    }
    status = m98_check_type(flags, type, bytes);
    if (status) goto done;
    if (data && capacity < bytes) status = ERROR_MORE_DATA;
    else if (data && bytes) m98_copy(data, output, bytes);

done:
    if (opened) RegCloseKey(key);
    if (ansi_subkey) HeapFree(heap, 0, ansi_subkey);
    if (ansi_value) HeapFree(heap, 0, ansi_value);
    if (converted) HeapFree(heap, 0, converted);
    if (raw) HeapFree(heap, 0, raw);
    if (type_out && (status == ERROR_SUCCESS || status == ERROR_MORE_DATA ||
                     status == ERROR_UNSUPPORTED_TYPE ||
                     status == ERROR_DATATYPE_MISMATCH)) *type_out = type;
    if (bytes_out && (status == ERROR_SUCCESS || status == ERROR_MORE_DATA ||
                      status == ERROR_UNSUPPORTED_TYPE ||
                      status == ERROR_DATATYPE_MISMATCH)) *bytes_out = bytes;
    return m98_fail(status, data, capacity, flags);
}

/* Win98's registry is natively ANSI. Unlike the W route, A preserves the
 * stored byte sequence and only repairs the documented string terminators.
 * Wine/ReactOS A paths were consulted for the seven-argument ABI, expansion,
 * type restrictions and size-on-error contract; no upstream code was copied. */
static LONG WINAPI m98_RegGetValueA(HKEY root, LPCSTR subkey,
                                    LPCSTR value, DWORD flags,
                                    LPDWORD type_out, PVOID data,
                                    LPDWORD bytes_out)
{
    HKEY key = root;
    BYTE *raw = NULL;
    DWORD raw_bytes = 0, bytes = 0, type = REG_NONE;
    DWORD capacity = (data && bytes_out) ? *bytes_out : 0;
    LONG status;
    BOOL opened = FALSE;

    if (data && !bytes_out) return ERROR_INVALID_PARAMETER;
    if ((flags & M98_RRF_WOW64_MASK) == M98_RRF_WOW64_MASK ||
        ((flags & M98_RRF_RT_REG_EXPAND_SZ) && !(flags & M98_RRF_NOEXPAND) &&
         (flags & M98_RRF_RT_ANY) != M98_RRF_RT_ANY))
        return m98_fail(ERROR_INVALID_PARAMETER, data, capacity, flags);
    if (subkey && *subkey) {
        status = RegOpenKeyExA(root, subkey, 0, KEY_QUERY_VALUE, &key);
        if (status) goto done;
        opened = TRUE;
    }
    status = m98_read_value(key, value, &type, &raw, &raw_bytes);
    if (status) goto done;
    bytes = raw_bytes;
    if (type == REG_EXPAND_SZ && !(flags & M98_RRF_NOEXPAND)) {
        status = m98_expand_ansi(&raw, &raw_bytes);
        if (status) goto done;
        bytes = raw_bytes;
        type = REG_SZ;
    } else if (m98_is_string(type)) {
        /* m98_read_value reserves two trailing bytes for this repair. */
        if (type == REG_MULTI_SZ) {
            if (!bytes || raw[bytes - 1]) raw[bytes++] = 0;
            if (bytes < 2 || raw[bytes - 2]) raw[bytes++] = 0;
        } else if (!bytes || raw[bytes - 1]) {
            raw[bytes++] = 0;
        }
    }
    status = m98_check_type(flags, type, bytes);
    if (status) goto done;
    if (data && capacity < bytes) status = ERROR_MORE_DATA;
    else if (data && bytes) m98_copy(data, raw, bytes);

done:
    if (opened) RegCloseKey(key);
    if (raw) HeapFree(GetProcessHeap(), 0, raw);
    if (type_out && (status == ERROR_SUCCESS || status == ERROR_MORE_DATA ||
                     status == ERROR_UNSUPPORTED_TYPE ||
                     status == ERROR_DATATYPE_MISMATCH)) *type_out = type;
    if (bytes_out && (status == ERROR_SUCCESS || status == ERROR_MORE_DATA ||
                      status == ERROR_UNSUPPORTED_TYPE ||
                      status == ERROR_DATATYPE_MISMATCH)) *bytes_out = bytes;
    return m98_fail(status, data, capacity, flags);
}

static const m98_named_api advapi32_apis[] = {
    { "RegGetValueA", (unsigned long)m98_RegGetValueA },
    { "RegGetValueW", (unsigned long)m98_RegGetValueW }
};
static const m98_api_table api_tables[] = {
    { "ADVAPI32.DLL", advapi32_apis, 2, 0, 0 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return api_tables;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
