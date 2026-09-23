/*
 * Windows 98 SE modernization experiment: first KernelEx API library.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Source lineage for the adapted functions is in THIRD_PARTY.md.
 * Only Windows 98-era KERNEL32 entry points are imported by this DLL.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include <limits.h>
#include "kex_abi.h"
#include "m98nls_ex.h"
#include "m98_threadpool.h"
#include "m98_initonce.h"
#include "m98_slist.h"
#include "m98_fls.h"

#ifndef ALL_PROCESSOR_GROUPS
#define ALL_PROCESSOR_GROUPS 0xffff
#endif
#ifndef CRITICAL_SECTION_NO_DEBUG_INFO
#define CRITICAL_SECTION_NO_DEBUG_INFO 0x01000000
#endif
#ifndef LOCALE_IDEFAULTANSICODEPAGE
#define LOCALE_IDEFAULTANSICODEPAGE 0x00001004
#endif
#ifndef RESTART_MAX_CMD_LINE
#define RESTART_MAX_CMD_LINE 1024
#endif
#ifndef CONDITION_VARIABLE_LOCKMODE_SHARED
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1
#endif

/* The Ex date/time formatters take a locale name, whereas the Win98 NLS
 * formatters take an LCID. A process-wide search context is protected below
 * because EnumSystemLocalesA has no caller-context parameter. */
typedef struct m98_locale_search {
    char language[4];
    char region[4];
    LCID first;
    LCID preferred;
} m98_locale_search;

static CRITICAL_SECTION m98_locale_lock;
static m98_locale_search m98_locale_query;

static char m98_ascii_lower(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
}

static BOOL m98_ascii_equal(const char *left, const char *right)
{
    while (*left && *right) {
        if (m98_ascii_lower(*left++) != m98_ascii_lower(*right++)) return FALSE;
    }
    return !*left && !*right;
}

static BOOL m98_ascii_equal_wide(const WCHAR *left, const char *right)
{
    while (*left && *right) {
        if (*left > 0x7f || m98_ascii_lower((char)*left++) !=
            m98_ascii_lower(*right++)) return FALSE;
    }
    return !*left && !*right;
}

static LCID m98_hex_lcid(const char *text)
{
    LCID id = 0;
    int count = 0;
    while (*text && count < 8) {
        char c = m98_ascii_lower(*text++);
        int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0) return 0;
        id = id * 16 + (LCID)digit;
        ++count;
    }
    return *text || !count ? 0 : id;
}

static BOOL CALLBACK m98_find_locale(char *hex_lcid)
{
    char language[8], region[8];
    LCID id = m98_hex_lcid(hex_lcid);
    if (!id || !GetLocaleInfoA(id, LOCALE_SISO639LANGNAME, language,
                               sizeof(language)) ||
        !m98_ascii_equal(language, m98_locale_query.language)) return TRUE;
    if (m98_locale_query.region[0] &&
        (!GetLocaleInfoA(id, LOCALE_SISO3166CTRYNAME, region,
                         sizeof(region)) ||
         !m98_ascii_equal(region, m98_locale_query.region))) return TRUE;
    if (!m98_locale_query.first) m98_locale_query.first = id;
    if (SUBLANGID(LANGIDFROMLCID(id)) == SUBLANG_DEFAULT)
        m98_locale_query.preferred = id;
    return m98_locale_query.region[0] ? FALSE : TRUE;
}

/* Reviewed Wine df15af365251 dlls/kernelbase/locale.c GetTimeFormatEx and
 * GetDateFormatEx, and
 * ReactOS 9dc3ca8720 dll/win32/kernel32/winnls/string/lcformat.c plus
 * dll/win32/kernel32/kernel32_vista/LocaleNameToLCID.c. Those rely on NT
 * NLS locale tables, so
 * this independent Win98 implementation matches ISO language/region metadata
 * exposed by the installed native locale database. Unknown or unavailable
 * modern locales fail explicitly instead of silently using the user locale. */
static LCID m98_locale_name_to_lcid(const WCHAR *name, BOOL *invariant)
{
    char language[4] = { 0 }, region[4] = { 0 };
    int position = 0, length = 0, index;
    LCID result;
    *invariant = FALSE;
    if (!name) return GetUserDefaultLCID();
    if (!*name) {
        *invariant = TRUE;
        return 0x0409; /* invariant markers and Gregorian digits are English */
    }
    if (m98_ascii_equal_wide(name, "!x-sys-default-locale"))
        return GetSystemDefaultLCID();
    while (name[position] && name[position] != '-' && length < 3) {
        WCHAR ch = name[position++];
        if (ch > 0x7f || m98_ascii_lower((char)ch) < 'a' ||
            m98_ascii_lower((char)ch) > 'z') return 0;
        language[length++] = m98_ascii_lower((char)ch);
    }
    if (length < 2 || (name[position] && name[position] != '-')) return 0;
    if (name[position] == '-') {
        int start;
        ++position;
        start = position;
        while (name[position] && name[position] != '-' && position - start < 5)
            ++position;
        /* Win98's ISO metadata has no script code. Accepting a tagged
         * variant by region alone could silently choose the wrong script. */
        if (position - start == 4 && name[position] == '-') return 0;
        length = position - start;
        if ((length != 2 && length != 3) || name[position]) return 0;
        for (index = 0; index < length; ++index) {
            WCHAR ch = name[start + index];
            if (ch > 0x7f || !((ch >= 'A' && ch <= 'Z') ||
                               (ch >= 'a' && ch <= 'z') ||
                               (ch >= '0' && ch <= '9'))) return 0;
            region[index] = m98_ascii_lower((char)ch);
        }
    }
    EnterCriticalSection(&m98_locale_lock);
    for (index = 0; index < 4; ++index) {
        m98_locale_query.language[index] = language[index];
        m98_locale_query.region[index] = region[index];
    }
    m98_locale_query.first = 0;
    m98_locale_query.preferred = 0;
    if (!EnumSystemLocalesA(m98_find_locale, LCID_SUPPORTED)) result = 0;
    else result = m98_locale_query.preferred ? m98_locale_query.preferred :
                  m98_locale_query.first;
    LeaveCriticalSection(&m98_locale_lock);
    return result;
}

static UINT m98_locale_ansi_codepage(LCID lcid, DWORD flags)
{
    char digits[12];
    UINT cp = 0;
    int index;
    if (flags & LOCALE_USE_CP_ACP) return GetACP();
    if (!GetLocaleInfoA(lcid, LOCALE_IDEFAULTANSICODEPAGE,
                        digits, sizeof(digits))) return GetACP();
    for (index = 0; digits[index]; ++index) {
        if (digits[index] < '0' || digits[index] > '9') return GetACP();
        cp = cp * 10 + (UINT)(digits[index] - '0');
    }
    return cp && IsValidCodePage(cp) ? cp : GetACP();
}

/* Unlike the older date/time bridge, locale information must not silently
 * decode a different locale's ANSI bytes with the process ACP. */
static UINT m98_checked_locale_codepage(LCID lcid)
{
    char digits[12];
    UINT cp = 0;
    int index;
    if (!GetLocaleInfoA(lcid, LOCALE_IDEFAULTANSICODEPAGE,
                        digits, sizeof(digits))) return 0;
    for (index = 0; digits[index]; ++index) {
        if (digits[index] < '0' || digits[index] > '9' ||
            cp > (UINT_MAX - (UINT)(digits[index] - '0')) / 10) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return 0;
        }
        cp = cp * 10 + (UINT)(digits[index] - '0');
    }
    if (!cp || !IsValidCodePage(cp)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    return cp;
}

#ifndef LOCALE_RETURN_GENITIVE_NAMES
#define LOCALE_RETURN_GENITIVE_NAMES 0x10000000UL
#endif
#ifndef LOCALE_FONTSIGNATURE
#define LOCALE_FONTSIGNATURE 0x00000058UL
#endif
#ifndef LOCALE_SNAME
#define LOCALE_SNAME 0x0000005cUL
#endif
#ifndef LOCALE_SPARENT
#define LOCALE_SPARENT 0x0000006dUL
#endif

/* The Win98 locale database has ISO components but no locale-name LCType.
 * They are enough to answer a validated language or language-region name. */
static int m98_locale_name_field(LCID lcid, const WCHAR *requested,
                                 LCTYPE type, WCHAR *output, int capacity)
{
    char language[4], region[4];
    WCHAR result[8];
    BOOL neutral = requested && *requested &&
                   !m98_ascii_equal_wide(requested, "!x-sys-default-locale");
    int index = 0, cursor = 0;
    if (neutral) {
        while (requested[index]) {
            if (requested[index] == '-') { neutral = FALSE; break; }
            ++index;
        }
    }
    if (!GetLocaleInfoA(lcid, LOCALE_SISO639LANGNAME,
                        language, sizeof(language))) return 0;
    if (!neutral && type == LOCALE_SNAME &&
        !GetLocaleInfoA(lcid, LOCALE_SISO3166CTRYNAME,
                        region, sizeof(region))) return 0;
    if (type == LOCALE_SPARENT && neutral) {
        result[0] = 0;
        index = 1;
    } else {
        for (index = 0; language[index] && index < 3; ++index)
            result[cursor++] = (WCHAR)language[index];
        if (language[index]) {
            SetLastError(ERROR_INVALID_DATA);
            return 0;
        }
        if (type == LOCALE_SNAME && !neutral) {
            result[cursor++] = '-';
            for (index = 0; region[index] && index < 3; ++index) {
                char c = region[index];
                result[cursor++] = (WCHAR)(c >= 'a' && c <= 'z' ?
                                            c - ('a' - 'A') : c);
            }
            if (region[index]) {
                SetLastError(ERROR_INVALID_DATA);
                return 0;
            }
        }
        result[cursor] = 0;
        index = cursor + 1;
    }
    if (!capacity) return index;
    if (capacity < index) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (cursor = 0; cursor < index; ++cursor) output[cursor] = result[cursor];
    return index;
}

/* Reviewed Wine df15af365251 dlls/kernelbase/locale.c GetLocaleInfoEx and
 * ReactOS 9dc3ca8720 dll/win32/kernel32/kernel32_vista/GetLocaleInfoEx.c.
 * Their NT Unicode NLS tables are unavailable to Win98. This independent
 * bridge resolves installed locale names to LCIDs, asks original Win98
 * GetLocaleInfoA for legacy fields, and sizes string results in WCHARs.
 * Numeric and font-signature results are copied as binary, never transcoded.
 * LOCALE_INVARIANT appeared after Win98; en-US is not a substitute. */
static int WINAPI m98_GetLocaleInfoEx(const WCHAR *name, LCTYPE type,
                                      WCHAR *output, int capacity)
{
    BOOL invariant;
    LCID lcid;
    LCTYPE native_type;
    UINT cp;
    HANDLE heap;
    char *ansi = 0;
    WCHAR *wide = 0;
    int ansi_size, returned, wide_size, index, result = 0;
    DWORD number;
    BYTE signature[32];

    if (capacity < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (capacity && !output) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    if ((type & 0x0fff0000UL) ||
        (type & LOCALE_RETURN_GENITIVE_NAMES)) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    lcid = m98_locale_name_to_lcid(name, &invariant);
    if (!lcid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (invariant) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    /* LOCALE_USE_CP_ACP affects the ANSI API's encoding only. Ex is Unicode;
     * keep the locale's own ANSI code page for a faithful round trip. */
    native_type = type & ~LOCALE_USE_CP_ACP;
    if ((type & 0xffff) == LOCALE_SNAME ||
        (type & 0xffff) == LOCALE_SPARENT) {
        if (type & LOCALE_RETURN_NUMBER) {
            SetLastError(ERROR_INVALID_FLAGS);
            return 0;
        }
        return m98_locale_name_field(lcid, name, type & 0xffff,
                                     output, capacity);
    }
    if (type & LOCALE_RETURN_NUMBER) {
        if ((type & 0xffff) == LOCALE_FONTSIGNATURE) {
            SetLastError(ERROR_INVALID_FLAGS);
            return 0;
        }
        returned = GetLocaleInfoA(lcid, native_type, (char *)&number,
                                  sizeof(number));
        if (!returned) return 0;
        if (returned != (int)sizeof(number)) {
            SetLastError(ERROR_INVALID_DATA);
            return 0;
        }
        if (!capacity) return 2;
        if (capacity < 2) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        output[0] = (WCHAR)(number & 0xffff);
        output[1] = (WCHAR)(number >> 16);
        return 2;
    }
    if ((type & 0xffff) == LOCALE_FONTSIGNATURE) {
        returned = GetLocaleInfoA(lcid, native_type, (char *)signature,
                                  sizeof(signature));
        if (!returned) return 0;
        if (returned != (int)sizeof(signature)) {
            SetLastError(ERROR_INVALID_DATA);
            return 0;
        }
        if (!capacity) return sizeof(signature) / sizeof(WCHAR);
        if (capacity < (int)(sizeof(signature) / sizeof(WCHAR))) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        for (index = 0; index < (int)sizeof(signature); ++index)
            ((BYTE *)output)[index] = signature[index];
        return sizeof(signature) / sizeof(WCHAR);
    }
    cp = m98_checked_locale_codepage(lcid);
    if (!cp) return 0;
    ansi_size = GetLocaleInfoA(lcid, native_type, 0, 0);
    if (!ansi_size) return 0;
    heap = GetProcessHeap();
    ansi = (char *)HeapAlloc(heap, 0, (SIZE_T)ansi_size);
    if (!ansi) {
        SetLastError(ERROR_OUTOFMEMORY);
        return 0;
    }
    returned = GetLocaleInfoA(lcid, native_type, ansi, ansi_size);
    if (!returned) goto done;
    if (returned > ansi_size || ansi[returned - 1] != 0) {
        SetLastError(ERROR_INVALID_DATA);
        goto done;
    }
    wide_size = MultiByteToWideChar(cp, 0, ansi, returned, 0, 0);
    if (!wide_size) goto done;
    if (!capacity) {
        result = wide_size;
        goto done;
    }
    if (capacity < wide_size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto done;
    }
    if ((SIZE_T)wide_size > ((SIZE_T)-1) / sizeof(WCHAR)) {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    wide = (WCHAR *)HeapAlloc(heap, 0, (SIZE_T)wide_size * sizeof(WCHAR));
    if (!wide) {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    if (MultiByteToWideChar(cp, 0, ansi, returned, wide, wide_size) !=
        wide_size) goto done;
    for (index = 0; index < wide_size; ++index) output[index] = wide[index];
    result = wide_size;
done:
    if (wide) HeapFree(heap, 0, wide);
    if (ansi) HeapFree(heap, 0, ansi);
    return result;
}

/* An NT product SKU has no counterpart in Windows 98. Expose a consistent
 * desktop edition to programs using a Vista-or-newer KernelEx personality.
 * The requested version controls which desktop SKU name existed then; this
 * reports a compatibility profile, not a Windows license or kernel feature.
 * Wine's RtlGetProductInfo also rejects versions below 6, while ReactOS
 * forwards Kernel32.GetProductInfo to its NT-specific implementation. */
static BOOL WINAPI m98_GetProductInfo(DWORD major, DWORD minor,
                                      DWORD sp_major, DWORD sp_minor,
                                      DWORD *product)
{
    (void)sp_major;
    (void)sp_minor;
    if (!product) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (major < 6) {
        *product = PRODUCT_UNDEFINED;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* Vista and Windows 7 report Ultimate; later desktop profiles report
     * Professional. The profile intentionally does not claim server roles. */
    *product = (major == 6 && minor < 2) ? PRODUCT_ULTIMATE :
               PRODUCT_PROFESSIONAL;
    return TRUE;
}

/* The Win98 OEM KERNEL32 has GetTimeFormatA, whose native formatter already
 * implements locale preferences, time pictures, flags, and time validation.
 * Convert at the selected locale's ANSI code page, then size in WCHARs, not
 * ANSI bytes. KernelEx's historical GetTimeFormatW_new allocates cchTime
 * bytes and returns the ANSI byte count, which is unsafe for DBCS output.
 * A custom Unicode picture that cannot round-trip through that code page is
 * reported as unsupported rather than silently corrupting literal text. */
static int WINAPI m98_GetTimeFormatEx(const WCHAR *name, DWORD flags,
                                     const SYSTEMTIME *time, const WCHAR *format,
                                     WCHAR *output, int capacity)
{
    BOOL invariant, used_default = FALSE;
    LCID lcid;
    UINT cp;
    DWORD native_flags = flags;
    HANDLE heap;
    char *format_a = 0, *output_a = 0;
    const WCHAR *selected_format = format;
    int format_size = 0, output_size, wide_size, result = 0;
    if (capacity < 0 || (capacity && !output)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & ~(TIME_NOMINUTESORSECONDS | TIME_NOSECONDS |
                  TIME_NOTIMEMARKER | TIME_FORCE24HOURFORMAT |
                  LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP) ||
        (format && (flags & LOCALE_NOUSEROVERRIDE))) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (time && (time->wHour > 23 || time->wMinute > 59 ||
                 time->wSecond > 59 || time->wMilliseconds > 999)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    lcid = m98_locale_name_to_lcid(name, &invariant);
    if (!lcid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (invariant && !format) {
        selected_format = L"HH':'mm':'ss";
        /* The synthetic invariant picture is internal; the caller did not
         * combine a custom picture with LOCALE_NOUSEROVERRIDE. */
        native_flags &= ~LOCALE_NOUSEROVERRIDE;
    }
    cp = m98_locale_ansi_codepage(lcid, flags);
    heap = GetProcessHeap();
    if (selected_format) {
        format_size = WideCharToMultiByte(cp, 0, selected_format, -1,
                                          0, 0, 0, 0);
        if (!format_size) return 0;
        format_a = (char *)HeapAlloc(heap, 0, (SIZE_T)format_size);
        if (!format_a) {
            SetLastError(ERROR_OUTOFMEMORY);
            return 0;
        }
        if (!WideCharToMultiByte(cp, 0, selected_format, -1, format_a,
                                 format_size, 0, &used_default)) goto done;
        if (used_default) {
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
            goto done;
        }
    }
    output_size = GetTimeFormatA(lcid, native_flags, time, format_a, 0, 0);
    if (!output_size) goto done;
    output_a = (char *)HeapAlloc(heap, 0, (SIZE_T)output_size);
    if (!output_a) {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    if (!GetTimeFormatA(lcid, native_flags, time, format_a, output_a,
                        output_size))
        goto done;
    wide_size = MultiByteToWideChar(cp, 0, output_a, -1, 0, 0);
    if (!wide_size) goto done;
    if (capacity && capacity < wide_size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto done;
    }
    if (!capacity) result = wide_size;
    else if (MultiByteToWideChar(cp, 0, output_a, -1, output, capacity))
        result = wide_size;
done:
    if (output_a) HeapFree(heap, 0, output_a);
    if (format_a) HeapFree(heap, 0, format_a);
    return result;
}

#ifndef DATE_YEARMONTH
#define DATE_YEARMONTH 0x00000008
#endif
#ifndef LOCALE_SYEARMONTH
#define LOCALE_SYEARMONTH 0x00001006
#endif

/* Wine df15af365251 dlls/kernelbase/locale.c and ReactOS 9dc3ca8720
 * dll/win32/kernel32/winnls/string/lcformat.c were reviewed, not copied.
 * Their Unicode NLS engines cannot run on Win98. The native GetDateFormatA
 * handles locale names after our LCID lookup; the bridge allocates by ANSI
 * byte count and reports UTF-16 units. A custom picture is checked for ANSI
 * round-trip loss before Win98 parses it, including on DBCS code pages. */
static int WINAPI m98_GetDateFormatEx(const WCHAR *name, DWORD flags,
                                     const SYSTEMTIME *date, const WCHAR *format,
                                     WCHAR *output, int capacity,
                                     const WCHAR *calendar)
{
    BOOL invariant, used_default = FALSE;
    LCID lcid;
    UINT cp;
    DWORD native_flags = flags;
    HANDLE heap;
    char *format_a = 0, *output_a = 0;
    WCHAR *roundtrip = 0;
    const WCHAR *selected_format = format;
    SYSTEMTIME valid_date;
    FILETIME date_filetime;
    const SYSTEMTIME *native_date = date;
    int format_size = 0, format_wide_size, output_size, wide_size;
    int index, result = 0;
    DWORD date_style = flags & (DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH);

    if (capacity < 0 || (capacity && !output) || calendar) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & ~(DATE_SHORTDATE | DATE_LONGDATE | DATE_USE_ALT_CALENDAR |
                  DATE_YEARMONTH | LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP) ||
        (date_style && date_style != DATE_SHORTDATE &&
         date_style != DATE_LONGDATE && date_style != DATE_YEARMONTH) ||
        (format && (date_style || (flags & LOCALE_NOUSEROVERRIDE)))) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (date) {
        valid_date = *date;
        valid_date.wDayOfWeek = 0;
        valid_date.wHour = valid_date.wMinute = 0;
        valid_date.wSecond = valid_date.wMilliseconds = 0;
        if (valid_date.wYear < 1601 || valid_date.wYear > 9999 ||
            !SystemTimeToFileTime(&valid_date, &date_filetime) ||
            !FileTimeToSystemTime(&date_filetime, &valid_date)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        native_date = &valid_date;
    }
    lcid = m98_locale_name_to_lcid(name, &invariant);
    if (!lcid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (invariant && !format) {
        selected_format = date_style == DATE_LONGDATE ?
            L"dddd',' dd MMMM yyyy" : date_style == DATE_YEARMONTH ?
            L"yyyy MMMM" : L"MM'/'dd'/'yyyy";
        /* The internal invariant picture does not use locale overrides or
         * alternate-calendar and default-picture flags. */
        native_flags &= ~(DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH |
                          DATE_USE_ALT_CALENDAR | LOCALE_NOUSEROVERRIDE);
    }
    cp = m98_locale_ansi_codepage(lcid, flags);
    heap = GetProcessHeap();
    if (date_style == DATE_YEARMONTH && !invariant && !format) {
        /* Win98's GetDateFormatA predates DATE_YEARMONTH. Use its installed
         * locale's year-month picture if that NLS value is available. */
        format_size = GetLocaleInfoA(lcid, LOCALE_SYEARMONTH |
                                     (flags & LOCALE_NOUSEROVERRIDE), 0, 0);
        if (!format_size) {
            SetLastError(ERROR_INVALID_FLAGS);
            return 0;
        }
        format_a = (char *)HeapAlloc(heap, 0, (SIZE_T)format_size);
        if (!format_a) {
            SetLastError(ERROR_OUTOFMEMORY);
            return 0;
        }
        if (!GetLocaleInfoA(lcid, LOCALE_SYEARMONTH |
                            (flags & LOCALE_NOUSEROVERRIDE), format_a,
                            format_size)) goto done;
        native_flags &= ~(DATE_YEARMONTH | LOCALE_NOUSEROVERRIDE);
    } else if (selected_format) {
        format_size = WideCharToMultiByte(cp, 0, selected_format, -1,
                                          0, 0, 0, 0);
        if (!format_size) return 0;
        format_a = (char *)HeapAlloc(heap, 0, (SIZE_T)format_size);
        if (!format_a) {
            SetLastError(ERROR_OUTOFMEMORY);
            return 0;
        }
        if (!WideCharToMultiByte(cp, 0, selected_format, -1, format_a,
                                 format_size, 0, &used_default)) goto done;
        if (used_default) {
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
            goto done;
        }
        format_wide_size = MultiByteToWideChar(cp, 0, format_a, -1, 0, 0);
        if (!format_wide_size) goto done;
        if ((SIZE_T)format_wide_size > ((SIZE_T)-1) / sizeof(WCHAR)) {
            SetLastError(ERROR_OUTOFMEMORY);
            goto done;
        }
        roundtrip = (WCHAR *)HeapAlloc(heap, 0,
                                     (SIZE_T)format_wide_size * sizeof(WCHAR));
        if (!roundtrip) {
            SetLastError(ERROR_OUTOFMEMORY);
            goto done;
        }
        if (!MultiByteToWideChar(cp, 0, format_a, -1, roundtrip,
                                 format_wide_size)) goto done;
        for (index = 0; index < format_wide_size; ++index) {
            if (roundtrip[index] != selected_format[index]) {
                SetLastError(ERROR_NO_UNICODE_TRANSLATION);
                goto done;
            }
        }
    }
    output_size = GetDateFormatA(lcid, native_flags, native_date,
                                 format_a, 0, 0);
    if (!output_size) goto done;
    output_a = (char *)HeapAlloc(heap, 0, (SIZE_T)output_size);
    if (!output_a) {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    if (!GetDateFormatA(lcid, native_flags, native_date, format_a,
                        output_a, output_size)) goto done;
    wide_size = MultiByteToWideChar(cp, 0, output_a, -1, 0, 0);
    if (!wide_size) goto done;
    if (capacity && capacity < wide_size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto done;
    }
    if (!capacity) result = wide_size;
    else if (MultiByteToWideChar(cp, 0, output_a, -1, output, capacity))
        result = wide_size;
done:
    if (roundtrip) HeapFree(heap, 0, roundtrip);
    if (output_a) HeapFree(heap, 0, output_a);
    if (format_a) HeapFree(heap, 0, format_a);
    return result;
}

/* Vista/Win7 ABI layouts, declared locally so our import target stays 98. */
typedef struct m98_processor_number {
    WORD Group;
    BYTE Number;
    BYTE Reserved;
} m98_processor_number;

typedef struct m98_group_affinity {
    ULONG_PTR Mask;
    WORD Group;
    WORD Reserved[3];
} m98_group_affinity;

typedef struct m98_file_basic_info {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD FileAttributes;
} m98_file_basic_info;

typedef struct m98_file_standard_info {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    DWORD NumberOfLinks;
    BYTE DeletePending;
    BYTE Directory;
} m98_file_standard_info;

#define M98_FILE_BASIC_INFO_CLASS 0
#define M98_FILE_STANDARD_INFO_CLASS 1
#define M98_APPMODEL_ERROR_NO_PACKAGE 15700

extern const WCHAR wine_casemap_upper[3582];

static WCHAR m98_uppercase(WCHAR ch)
{
    return ch + wine_casemap_upper[wine_casemap_upper[ch >> 8] + (ch & 0xff)];
}

/* Wine CompareStringOrdinal and RtlCompareUnicodeStrings adapted to use
 * Wine's portable Unicode upper-case map already carried by KernelEx. */
static int WINAPI m98_CompareStringOrdinal(const WCHAR *left, int left_len,
                                           const WCHAR *right, int right_len,
                                           BOOL ignore_case)
{
    int i, length;
    WCHAR a, b;
    if (!left || !right || left_len < -1 || right_len < -1 ||
        (ignore_case != FALSE && ignore_case != TRUE)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (left_len < 0) {
        for (left_len = 0; left[left_len]; ++left_len) { }
    }
    if (right_len < 0) {
        for (right_len = 0; right[right_len]; ++right_len) { }
    }
    length = left_len < right_len ? left_len : right_len;
    for (i = 0; i < length; ++i) {
        a = left[i];
        b = right[i];
        if (ignore_case) {
            a = m98_uppercase(a);
            b = m98_uppercase(b);
        }
        if (a < b) return CSTR_LESS_THAN;
        if (a > b) return CSTR_GREATER_THAN;
    }
    if (left_len < right_len) return CSTR_LESS_THAN;
    if (left_len > right_len) return CSTR_GREATER_THAN;
    return CSTR_EQUAL;
}

/* Win98 has no packaged application identity. Wine/ReactOS use this result
 * for ordinary unpackaged processes; this does not emulate app packages. */
static LONG WINAPI m98_GetCurrentPackageId(UINT32 *length, BYTE *buffer)
{
    (void)buffer;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetCurrentPackageInfo(UINT32 flags, UINT32 *length,
                                              BYTE *buffer, UINT32 *count)
{
    (void)flags; (void)buffer; (void)count;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetCurrentPackageFullName(UINT32 *length, WCHAR *name)
{
    (void)name;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetCurrentPackageFamilyName(UINT32 *length, WCHAR *name)
{
    (void)name;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetCurrentPackagePath(UINT32 *length, WCHAR *path)
{
    (void)path;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

/* Every process on Win98 is unpackaged. Validate required arguments and
 * process handles before reporting an ordinary unpackaged process.
 * GetExitCodeProcess accepts the same process-query access on Win98. */
static LONG m98_package_result_for_process(HANDLE process, UINT32 *length)
{
    DWORD exit_code;
    if (!process || !length) return ERROR_INVALID_PARAMETER;
    if (!GetExitCodeProcess(process, &exit_code)) return (LONG)GetLastError();
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetPackageFamilyName(HANDLE process,
                                            UINT32 *length, WCHAR *name)
{
    (void)name;
    return m98_package_result_for_process(process, length);
}

static LONG WINAPI m98_GetPackageFullName(HANDLE process,
                                          UINT32 *length, WCHAR *name)
{
    (void)name;
    return m98_package_result_for_process(process, length);
}

static LONG WINAPI m98_GetPackageId(HANDLE process, UINT32 *length,
                                    BYTE *buffer)
{
    (void)buffer;
    return m98_package_result_for_process(process, length);
}

/* Wine's group-selection logic, specialized for Win98's single CPU/group. */
static DWORD WINAPI m98_GetActiveProcessorCount(WORD group)
{
    if (group == 0 || group == ALL_PROCESSOR_GROUPS) return 1;
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

static WORD WINAPI m98_GetActiveProcessorGroupCount(void)
{
    return 1;
}

static DWORD WINAPI m98_GetMaximumProcessorCount(WORD group)
{
    if (group == 0 || group == ALL_PROCESSOR_GROUPS) return 1;
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

static WORD WINAPI m98_GetMaximumProcessorGroupCount(void)
{
    return 1;
}

static DWORD WINAPI m98_GetCurrentProcessorNumber(void)
{
    return 0;
}

static void WINAPI m98_GetCurrentProcessorNumberEx(m98_processor_number *number)
{
    if (!number) return;
    number->Group = 0;
    number->Number = 0;
    number->Reserved = 0;
}

static BOOL WINAPI m98_GetNumaHighestNodeNumber(PULONG highest)
{
    if (!highest) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *highest = 0;
    return TRUE;
}

/* One Win98 memory domain. This reports memory visible to Win98, not
 * necessarily all RAM installed in the physical machine. */
static BOOL WINAPI m98_GetNumaAvailableMemoryNode(UCHAR node,
                                                   PULONGLONG available)
{
    MEMORYSTATUS status;
    if (node != 0 || !available) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);
    *available = status.dwAvailPhys;
    return TRUE;
}

static BOOL WINAPI m98_GetNumaAvailableMemoryNodeEx(USHORT node,
                                                     PULONGLONG available)
{
    if (node != 0 || !available) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return m98_GetNumaAvailableMemoryNode(0, available);
}

static BOOL WINAPI m98_GetNumaProcessorNode(UCHAR processor, PUCHAR node)
{
    if (!node) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (processor != 0) {
        *node = 0xff;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *node = 0;
    return TRUE;
}

static BOOL WINAPI m98_GetNumaProcessorNodeEx(
    const m98_processor_number *processor, PUSHORT node)
{
    if (!processor || !node) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (processor->Group != 0 || processor->Number != 0) {
        *node = 0xffff;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *node = 0;
    return TRUE;
}

static BOOL WINAPI m98_GetNumaNodeProcessorMask(UCHAR node, PULONGLONG mask)
{
    if (!mask || node != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *mask = 1;
    return TRUE;
}

static BOOL WINAPI m98_GetNumaNodeProcessorMaskEx(USHORT node,
                                                   m98_group_affinity *mask)
{
    int i;
    if (node != 0 || !mask) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    mask->Mask = 1;
    mask->Group = 0;
    for (i = 0; i < 3; ++i) mask->Reserved[i] = 0;
    return TRUE;
}

/* A Win98 process can only run in processor group 0. Preserve the modern
 * buffer-size protocol so callers can make their usual two-call query. */
static BOOL WINAPI m98_GetProcessGroupAffinity(HANDLE process,
                                               PUSHORT group_count,
                                               PUSHORT group_array)
{
    DWORD exit_code;
    if (!GetExitCodeProcess(process, &exit_code)) return FALSE;
    if (!group_count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (*group_count == 0) {
        *group_count = 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!group_array) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    group_array[0] = 0;
    *group_count = 1;
    return TRUE;
}

/* Win98 boots through a BIOS interface, including on a UEFI machine using
 * its compatibility boot path. It does not enable operating-system DEP. */
static BOOL WINAPI m98_GetFirmwareType(DWORD *firmware_type)
{
    if (!firmware_type) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *firmware_type = 1; /* FirmwareTypeBios */
    return TRUE;
}

static DWORD WINAPI m98_GetSystemDEPPolicy(void)
{
    return 0; /* AlwaysOff */
}

/* ReactOS's RtlInitializeCriticalSectionEx flag checks, with Win9x's
 * uniprocessor spin-count behavior. NO_DEBUG_INFO is advisory here. */
static BOOL WINAPI m98_InitializeCriticalSectionEx(LPCRITICAL_SECTION section,
                                                    DWORD spin_count,
                                                    DWORD flags)
{
    if (!section || (flags & ~CRITICAL_SECTION_NO_DEBUG_INFO) ||
        (spin_count & 0xff000000UL)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    InitializeCriticalSection(section);
    return TRUE;
}

/* SRWLOCK is one pointer wide in the 32-bit ABI.  A
 * single Win98 interlocked word is sufficient for its basic contracts.
 * Sources checked: Wine dlls/ntdll/sync.c (df15af3) and ReactOS
 * sdk/lib/rtl/srw.c (9dc3ca8).  Their writer-waiter/owner split and atomic
 * transitions inform this new Win98 layout.  Bits 0..14 count readers, bit 15
 * marks an exclusive owner, and bits 16..31 count waiting writers.  Pending
 * writers stop new readers, avoiding writer starvation.  Contended waits
 * sleep for a tick so a lower-priority owner can run on the one CPU.
 * Like Windows SRW locks, these locks are not recursive. */
typedef struct m98_srwlock {
    volatile LONG state;
} m98_srwlock;

#define M98_SRW_READERS 0x00007fffUL
#define M98_SRW_EXCLUSIVE 0x00008000UL
#define M98_SRW_WRITER 0x00010000UL
#define M98_SRW_WRITERS 0xffff0000UL

static void WINAPI m98_InitializeSRWLock(m98_srwlock *lock)
{
    lock->state = 0;
}

static DWORD m98_srw_state(m98_srwlock *lock)
{
    return (DWORD)InterlockedCompareExchange(&lock->state, 0, 0);
}

static BOOLEAN WINAPI m98_TryAcquireSRWLockExclusive(m98_srwlock *lock)
{
    DWORD old_state;
    for (;;) {
        old_state = m98_srw_state(lock);
        if (old_state & (M98_SRW_READERS | M98_SRW_EXCLUSIVE)) return FALSE;
        if ((DWORD)InterlockedCompareExchange(&lock->state,
                (LONG)(old_state | M98_SRW_EXCLUSIVE), (LONG)old_state) == old_state)
            return TRUE;
    }
}

static void WINAPI m98_AcquireSRWLockExclusive(m98_srwlock *lock)
{
    DWORD old_state;
    /* Register before waiting, so arriving readers leave room for writers. */
    for (;;) {
        old_state = m98_srw_state(lock);
        if ((old_state & M98_SRW_WRITERS) != M98_SRW_WRITERS &&
            (DWORD)InterlockedCompareExchange(&lock->state,
                (LONG)(old_state + M98_SRW_WRITER), (LONG)old_state) == old_state)
            break;
        Sleep(1);
    }
    for (;;) {
        old_state = m98_srw_state(lock);
        if (!(old_state & (M98_SRW_READERS | M98_SRW_EXCLUSIVE)) &&
            (DWORD)InterlockedCompareExchange(&lock->state,
                (LONG)((old_state - M98_SRW_WRITER) | M98_SRW_EXCLUSIVE),
                (LONG)old_state) == old_state)
            return;
        Sleep(1);
    }
}

static void WINAPI m98_ReleaseSRWLockExclusive(m98_srwlock *lock)
{
    DWORD old_state;
    do {
        old_state = m98_srw_state(lock);
    } while ((DWORD)InterlockedCompareExchange(&lock->state,
                 (LONG)(old_state & ~M98_SRW_EXCLUSIVE),
                 (LONG)old_state) != old_state);
}

static BOOLEAN WINAPI m98_TryAcquireSRWLockShared(m98_srwlock *lock)
{
    DWORD old_state;
    for (;;) {
        old_state = m98_srw_state(lock);
        if ((old_state & (M98_SRW_WRITERS | M98_SRW_EXCLUSIVE)) ||
            (old_state & M98_SRW_READERS) == M98_SRW_READERS)
            return FALSE;
        if ((DWORD)InterlockedCompareExchange(&lock->state,
                (LONG)(old_state + 1), (LONG)old_state) == old_state)
            return TRUE;
    }
}

static void WINAPI m98_AcquireSRWLockShared(m98_srwlock *lock)
{
    while (!m98_TryAcquireSRWLockShared(lock)) Sleep(1);
}

static void WINAPI m98_ReleaseSRWLockShared(m98_srwlock *lock)
{
    DWORD old_state;
    do {
        old_state = m98_srw_state(lock);
    } while ((DWORD)InterlockedCompareExchange(&lock->state,
                 (LONG)(old_state - 1), (LONG)old_state) != old_state);
}

/* A Win32 CONDITION_VARIABLE is one pointer wide and supports static zero
 * initialization. Wine df15af3 dlls/ntdll/sync.c waits on a changing word;
 * ReactOS 9dc3ca8 sdk/lib/rtl/condvar.c links a waiter before releasing the
 * caller's SRW lock, then reacquires the same mode even after timeout. Win98
 * has neither wait-on-address nor keyed events. Keep stack-owned event waiters
 * in a process-local queue instead. The queue lock spans registration and SRW
 * release, so a wake cannot disappear between the two. The event is signaled
 * only for a waiter already in the queue; wakes are not retained as credits. */
typedef struct m98_condition_variable {
    void *state;
} m98_condition_variable;

typedef struct m98_condition_waiter {
    struct m98_condition_waiter *previous;
    struct m98_condition_waiter *next;
    m98_condition_variable *variable;
    HANDLE event;
    BOOL notified;
} m98_condition_waiter;

static CRITICAL_SECTION m98_condition_lock;
static m98_condition_waiter *m98_condition_first;
static m98_condition_waiter *m98_condition_last;

static void m98_condition_unlink(m98_condition_waiter *waiter)
{
    if (waiter->previous) waiter->previous->next = waiter->next;
    else m98_condition_first = waiter->next;
    if (waiter->next) waiter->next->previous = waiter->previous;
    else m98_condition_last = waiter->previous;
}

static void WINAPI m98_InitializeConditionVariable(m98_condition_variable *variable)
{
    variable->state = 0;
}

static BOOL WINAPI m98_SleepConditionVariableSRW(m98_condition_variable *variable,
                                                  m98_srwlock *lock,
                                                  DWORD milliseconds,
                                                  ULONG flags)
{
    m98_condition_waiter waiter;
    DWORD wait_result, wait_error = ERROR_SUCCESS;
    BOOL notified;
    /* As on Windows, the caller must already hold the SRW lock in the
     * indicated mode. The ownership and invalid-pointer cases are undefined. */
    waiter.previous = 0;
    waiter.next = 0;
    waiter.variable = variable;
    waiter.notified = FALSE;
    waiter.event = CreateEventA(0, FALSE, FALSE, 0);
    if (!waiter.event) return FALSE; /* The caller still owns its SRW lock. */

    EnterCriticalSection(&m98_condition_lock);
    waiter.previous = m98_condition_last;
    if (m98_condition_last) m98_condition_last->next = &waiter;
    else m98_condition_first = &waiter;
    m98_condition_last = &waiter;
    if (flags & CONDITION_VARIABLE_LOCKMODE_SHARED)
        m98_ReleaseSRWLockShared(lock);
    else
        m98_ReleaseSRWLockExclusive(lock);
    LeaveCriticalSection(&m98_condition_lock);

    wait_result = WaitForSingleObject(waiter.event, milliseconds);
    if (wait_result == WAIT_FAILED) wait_error = GetLastError();
    EnterCriticalSection(&m98_condition_lock);
    notified = waiter.notified;
    if (!notified) m98_condition_unlink(&waiter);
    LeaveCriticalSection(&m98_condition_lock);
    CloseHandle(waiter.event);

    if (flags & CONDITION_VARIABLE_LOCKMODE_SHARED)
        m98_AcquireSRWLockShared(lock);
    else
        m98_AcquireSRWLockExclusive(lock);
    if (notified) return TRUE;
    SetLastError(wait_result == WAIT_TIMEOUT ? ERROR_TIMEOUT : wait_error);
    return FALSE;
}

static void m98_condition_wake(m98_condition_variable *variable, BOOL all)
{
    m98_condition_waiter *waiter;
    EnterCriticalSection(&m98_condition_lock);
    waiter = m98_condition_first;
    while (waiter) {
        m98_condition_waiter *next = waiter->next;
        if (waiter->variable == variable && SetEvent(waiter->event)) {
            waiter->notified = TRUE;
            m98_condition_unlink(waiter);
            if (!all) break;
        }
        waiter = next;
    }
    LeaveCriticalSection(&m98_condition_lock);
}

static void WINAPI m98_WakeConditionVariable(m98_condition_variable *variable)
{
    m98_condition_wake(variable, FALSE);
}

static void WINAPI m98_WakeAllConditionVariable(m98_condition_variable *variable)
{
    m98_condition_wake(variable, TRUE);
}

/* GetTickCount is 32-bit on Win98. Extend it while this process is alive.
 * The high word is unknowable if the DLL first loads after an uptime wrap. */
static CRITICAL_SECTION tick_lock;
static DWORD last_tick;
static ULONGLONG tick_high;

static ULONGLONG WINAPI m98_GetTickCount64(void)
{
    DWORD current;
    ULONGLONG result;
    EnterCriticalSection(&tick_lock);
    current = GetTickCount();
    if (current < last_tick) tick_high += 0x100000000ULL;
    last_tick = current;
    result = tick_high | current;
    LeaveCriticalSection(&tick_lock);
    return result;
}

/* Import bridge only: Win98 cannot provide the sub-microsecond precision
 * promised by the Windows 8 API. */
static void WINAPI m98_GetSystemTimePreciseAsFileTime(LPFILETIME time)
{
    GetSystemTimeAsFileTime(time);
}

/* Basic and standard file information can be derived from a Win98 handle.
 * ChangeTime is approximated by LastWriteTime; allocation size by EOF. */
static BOOL WINAPI m98_GetFileInformationByHandleEx(HANDLE file,
                                                     int kind,
                                                     LPVOID output,
                                                     DWORD output_size)
{
    BY_HANDLE_FILE_INFORMATION source;
    if (!output) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (kind == M98_FILE_BASIC_INFO_CLASS) {
        m98_file_basic_info *basic;
        if (output_size < sizeof(m98_file_basic_info)) {
            SetLastError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        if (!GetFileInformationByHandle(file, &source)) return FALSE;
        basic = (m98_file_basic_info *)output;
        basic->CreationTime.LowPart = source.ftCreationTime.dwLowDateTime;
        basic->CreationTime.HighPart = source.ftCreationTime.dwHighDateTime;
        basic->LastAccessTime.LowPart = source.ftLastAccessTime.dwLowDateTime;
        basic->LastAccessTime.HighPart = source.ftLastAccessTime.dwHighDateTime;
        basic->LastWriteTime.LowPart = source.ftLastWriteTime.dwLowDateTime;
        basic->LastWriteTime.HighPart = source.ftLastWriteTime.dwHighDateTime;
        basic->ChangeTime = basic->LastWriteTime;
        basic->FileAttributes = source.dwFileAttributes;
        return TRUE;
    }
    if (kind == M98_FILE_STANDARD_INFO_CLASS) {
        m98_file_standard_info *standard;
        if (output_size < sizeof(m98_file_standard_info)) {
            SetLastError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        if (!GetFileInformationByHandle(file, &source)) return FALSE;
        standard = (m98_file_standard_info *)output;
        standard->EndOfFile.LowPart = source.nFileSizeLow;
        standard->EndOfFile.HighPart = source.nFileSizeHigh;
        standard->AllocationSize = standard->EndOfFile;
        standard->NumberOfLinks = source.nNumberOfLinks;
        standard->DeletePending = FALSE;
        standard->Directory = (source.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return TRUE;
    }
    /* GetFileInformationByHandleEx rejects invalid and set-only classes.
     * Keep valid query classes distinguishable from unsupported values so
     * callers probing for a feature can handle the failure correctly. */
    switch (kind) {
    case 2:  /* FileNameInfo */
    case 7:  /* FileStreamInfo */
    case 8:  /* FileCompressionInfo */
    case 9:  /* FileAttributeTagInfo */
    case 10: /* FileIdBothDirectoryInfo */
    case 11: /* FileIdBothDirectoryRestartInfo */
    case 13: /* FileRemoteProtocolInfo */
    case 14: /* FileFullDirectoryInfo */
    case 15: /* FileFullDirectoryRestartInfo */
    case 16: /* FileStorageInfo */
    case 17: /* FileAlignmentInfo */
    case 18: /* FileIdInfo */
    case 19: /* FileIdExtdDirectoryInfo */
    case 20: /* FileIdExtdDirectoryRestartInfo */
    case 23: /* FileCaseSensitiveInfo */
    case 24: /* FileNormalizedNameInfo */
        SetLastError(ERROR_NOT_SUPPORTED);
        break;
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        break;
    }
    return FALSE;
}

/* Wine df15af365251 dlls/kernelbase/file.c and ReactOS 9dc3ca8720
 * dll/win32/kernel32/kernel32_vista/GetFinalPathNameByHandle.c obtain an
 * NT object/file name from a handle. Win98 has neither NT query interface.
 * A path is returned only for the current process image when two independently
 * opened handles have the same nonzero volume serial/file ID and one link.
 * Other files fail explicitly: scanning drives or guessing from a file index
 * would not recover the path by which the caller opened them. */
#define M98_FILE_NAME_OPENED 0x00000008UL
#define M98_VOLUME_NAME_GUID 0x00000001UL
#define M98_VOLUME_NAME_NT 0x00000002UL
#define M98_VOLUME_NAME_NONE 0x00000004UL

static DWORD WINAPI m98_GetFinalPathNameByHandleW(HANDLE file, WCHAR *path,
                                                   DWORD capacity, DWORD flags)
{
    BY_HANDLE_FILE_INFORMATION target, image;
    char image_path[MAX_PATH], long_path[MAX_PATH];
    const char *selected;
    HANDLE image_file;
    DWORD type, length, volume, needed, prefix;
    int wide_count;

    volume = flags & (M98_VOLUME_NAME_GUID | M98_VOLUME_NAME_NT |
                      M98_VOLUME_NAME_NONE);
    if (flags & ~(M98_FILE_NAME_OPENED | M98_VOLUME_NAME_GUID |
                  M98_VOLUME_NAME_NT | M98_VOLUME_NAME_NONE) ||
        (volume && (volume & (volume - 1)))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (capacity && !path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!file || file == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if ((flags & M98_FILE_NAME_OPENED) ||
        volume == M98_VOLUME_NAME_GUID || volume == M98_VOLUME_NAME_NT) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    SetLastError(ERROR_SUCCESS);
    type = GetFileType(file);
    if (type == FILE_TYPE_UNKNOWN) {
        if (!GetLastError()) SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (type != FILE_TYPE_DISK) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (!GetFileInformationByHandle(file, &target)) return 0;
    if (!target.dwVolumeSerialNumber ||
        (!target.nFileIndexHigh && !target.nFileIndexLow) ||
        target.nNumberOfLinks != 1) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    length = GetModuleFileNameA(0, image_path, MAX_PATH);
    if (!length || length >= MAX_PATH - 1) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    length = GetLongPathNameA(image_path, long_path, MAX_PATH);
    if (!length) return 0;
    if (length >= MAX_PATH - 1) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    if (!((long_path[0] >= 'A' && long_path[0] <= 'Z') ||
          (long_path[0] >= 'a' && long_path[0] <= 'z')) ||
        long_path[1] != ':' || long_path[2] != '\\') {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    image_file = CreateFileA(long_path, GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (image_file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileInformationByHandle(image_file, &image)) {
        DWORD error = GetLastError();
        CloseHandle(image_file);
        SetLastError(error);
        return 0;
    }
    CloseHandle(image_file);
    if (image.nNumberOfLinks != 1 ||
        target.dwVolumeSerialNumber != image.dwVolumeSerialNumber ||
        target.nFileIndexHigh != image.nFileIndexHigh ||
        target.nFileIndexLow != image.nFileIndexLow ||
        target.nFileSizeHigh != image.nFileSizeHigh ||
        target.nFileSizeLow != image.nFileSizeLow ||
        target.ftCreationTime.dwHighDateTime !=
            image.ftCreationTime.dwHighDateTime ||
        target.ftCreationTime.dwLowDateTime !=
            image.ftCreationTime.dwLowDateTime) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    selected = volume == M98_VOLUME_NAME_NONE ? long_path + 2 : long_path;
    prefix = volume == M98_VOLUME_NAME_NONE ? 0 : 4;
    wide_count = MultiByteToWideChar(CP_ACP, 0, selected, -1, 0, 0);
    if (!wide_count) return 0;
    needed = (DWORD)wide_count + prefix;
    if (capacity < needed) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return needed;
    }
    if (prefix) {
        path[0] = L'\\';
        path[1] = L'\\';
        path[2] = L'?';
        path[3] = L'\\';
    }
    if (!MultiByteToWideChar(CP_ACP, 0, selected, -1, path + prefix,
                             wide_count)) return 0;
    return needed - 1;
}

/* Microsoft documents ERROR_INVALID_PARAMETER for FindFirstStreamW on a
 * filesystem without streams. FAT/FAT32 have no named streams; returning a
 * made-up "::$DATA" search handle would violate that contract. Pinned Wine
 * df15af365251 dlls/kernelbase/file.c returns EOF unconditionally, whereas
 * ReactOS 9dc3ca8720 dll/win32/kernel32/client/file/find.c queries NT
 * FileStreamInformation. Neither implementation can be used on Win98. This
 * original bridge verifies a local FAT/FAT32 path and returns only the
 * documented unsupported-filesystem error. It never creates a search handle. */
#ifndef FILE_NAMED_STREAMS
#define FILE_NAMED_STREAMS 0x00040000UL
#endif

static HANDLE WINAPI m98_FindFirstStreamW(const WCHAR *wide_path,
                                          DWORD level, void *stream_data,
                                          DWORD flags)
{
    char path[MAX_PATH], root[4], filesystem[16];
    WCHAR roundtrip[MAX_PATH];
    DWORD filesystem_flags, attributes;
    int bytes, chars, index, original_length;

    if (!wide_path || !*wide_path || !stream_data || level || flags) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    for (index = 0; index < MAX_PATH && wide_path[index]; ++index) { }
    if (index == MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return INVALID_HANDLE_VALUE;
    }
    original_length = index;
    bytes = WideCharToMultiByte(CP_ACP, 0, wide_path, -1, 0, 0, 0, 0);
    if (!bytes) return INVALID_HANDLE_VALUE;
    if (bytes > MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return INVALID_HANDLE_VALUE;
    }
    if (!WideCharToMultiByte(CP_ACP, 0, wide_path, -1, path,
                             MAX_PATH, 0, 0)) return INVALID_HANDLE_VALUE;
    chars = MultiByteToWideChar(CP_ACP, 0, path, -1, roundtrip, MAX_PATH);
    if (!chars) return INVALID_HANDLE_VALUE;
    if (chars != original_length + 1) {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return INVALID_HANDLE_VALUE;
    }
    for (index = 0; index < chars; ++index) {
        if (wide_path[index] != roundtrip[index]) {
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
            return INVALID_HANDLE_VALUE;
        }
    }
    if (!((path[0] >= 'A' && path[0] <= 'Z') ||
          (path[0] >= 'a' && path[0] <= 'z')) ||
        path[1] != ':' || path[2] != '\\') {
        SetLastError(path[0] == '\\' ? ERROR_NOT_SUPPORTED : ERROR_INVALID_NAME);
        return INVALID_HANDLE_VALUE;
    }
    root[0] = path[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = 0;
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return INVALID_HANDLE_VALUE;
    if (GetDriveTypeA(root) == DRIVE_REMOTE) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return INVALID_HANDLE_VALUE;
    }
    if (!GetVolumeInformationA(root, 0, 0, 0, 0, &filesystem_flags,
                               filesystem, sizeof(filesystem)))
        return INVALID_HANDLE_VALUE;
    if (!(filesystem_flags & FILE_NAMED_STREAMS) &&
        (m98_ascii_equal(filesystem, "FAT") ||
         m98_ascii_equal(filesystem, "FAT32"))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;
}

/* Reviewed Wine df15af365251 dlls/kernelbase/process.c and
 * dlls/kernel32/process.c, and ReactOS 9dc3ca8720
 * dll/win32/kernel32/kernel32_vista/vista.c plus kernel32.spec. Both trees
 * lack a working restart registry; Wine's Register stub reports S_OK anyway.
 * Win98 has no WER application-restart service, so do not claim registration
 * succeeded. The observable unregistered query has a documented HRESULT. */
static HRESULT WINAPI m98_GetApplicationRestartSettings(HANDLE process,
                                                         WCHAR *command_line,
                                                         DWORD *size,
                                                         DWORD *flags)
{
    DWORD exit_code, error;
    (void)flags;
    if (!process || !size || (!command_line && *size)) return E_INVALIDARG;
    if (!GetExitCodeProcess(process, &exit_code)) {
        error = GetLastError();
        return HRESULT_FROM_WIN32(error ? error : ERROR_INVALID_HANDLE);
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

static HRESULT WINAPI m98_RegisterApplicationRestart(const WCHAR *command_line,
                                                      DWORD flags)
{
    DWORD length = 0;
    /* The documented maximum includes the terminating WCHAR. Bounded
     * scanning avoids overflow if the caller supplies a long string. */
    if (command_line) {
        while (length < RESTART_MAX_CMD_LINE && command_line[length]) ++length;
        if (length == RESTART_MAX_CMD_LINE) return E_INVALIDARG;
    }
    if (flags & ~0x0fUL) return E_INVALIDARG;
    return E_FAIL;
}

static HRESULT WINAPI m98_UnregisterApplicationRestart(void)
{
    /* No registration can exist while Register fails; removing nothing is
     * successful and is also what current Windows returns in that state. */
    return S_OK;
}

/* Pinned Wine df15af365251 dlls/kernelbase/debug.c and ReactOS
 * 9dc3ca87209f dll/win32/kernel32/kernel32_vista/vista.c query NT process
 * image information. Win98 does not expose that NT query or native device
 * path namespace. This independent bridge reports only this process's DOS
 * image path, verified by the pseudo-handle or, where KernelEx supplies
 * GetProcessId, by an actual process handle with our PID. It never returns
 * our path for another process. See docs/NPP_PROCESSPATH_PORT.md. */
#define M98_PROCESS_NAME_NATIVE 0x00000001UL
typedef DWORD (WINAPI *m98_get_process_id_fn)(HANDLE);

static BOOL m98_query_current_process_path(HANDLE process, DWORD flags,
                                           char *path, DWORD capacity)
{
    DWORD exit_code, length, pid;
    HMODULE kernel32;
    m98_get_process_id_fn get_process_id;

    if (flags & ~M98_PROCESS_NAME_NATIVE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!process) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!GetExitCodeProcess(process, &exit_code)) return FALSE;
    if (flags & M98_PROCESS_NAME_NATIVE) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (process != GetCurrentProcess()) {
        /* GetProcessId is supplied by KernelEx on Win98, but is deliberately
         * resolved at run time so the DLL has only original Win98 imports. */
        kernel32 = GetModuleHandleA("KERNEL32.DLL");
        get_process_id = kernel32 ? (m98_get_process_id_fn)GetProcAddress(
            kernel32, "GetProcessId") : 0;
        pid = get_process_id ? get_process_id(process) : 0;
        if (pid != GetCurrentProcessId()) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }
    }
    length = GetModuleFileNameA(0, path, capacity);
    if (!length) return FALSE;
    if (length >= capacity - 1) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    if (!((path[0] >= 'A' && path[0] <= 'Z') ||
          (path[0] >= 'a' && path[0] <= 'z')) ||
        path[1] != ':' || path[2] != '\\') {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return TRUE;
}

static BOOL WINAPI m98_QueryFullProcessImageNameA(HANDLE process, DWORD flags,
                                                   char *name, DWORD *size)
{
    char path[MAX_PATH], long_path[MAX_PATH];
    const char *selected = path;
    DWORD length;

    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!m98_query_current_process_path(process, flags, path, MAX_PATH))
        return FALSE;
    length = GetLongPathNameA(path, long_path, MAX_PATH);
    if (length && length < MAX_PATH) selected = long_path;
    else if (length >= MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    for (length = 0; selected[length]; ++length) { }
    if (*size <= length) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (DWORD i = 0; i <= length; ++i) name[i] = selected[i];
    *size = length;
    return TRUE;
}

static BOOL WINAPI m98_QueryFullProcessImageNameW(HANDLE process, DWORD flags,
                                                   WCHAR *name, DWORD *size)
{
    char path[MAX_PATH], long_path[MAX_PATH];
    const char *selected = path;
    int required;
    DWORD length;

    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!m98_query_current_process_path(process, flags, path, MAX_PATH))
        return FALSE;
    length = GetLongPathNameA(path, long_path, MAX_PATH);
    if (length && length < MAX_PATH) selected = long_path;
    else if (length >= MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    required = MultiByteToWideChar(CP_ACP, 0, selected, -1, 0, 0);
    if (!required) return FALSE;
    if (*size < (DWORD)required) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!MultiByteToWideChar(CP_ACP, 0, selected, -1, name, *size))
        return FALSE;
    *size = (DWORD)required - 1;
    return TRUE;
}

/* Names must stay sorted: KernelEx validates this before loading an API lib. */
#define M98_API(name, impl) { name, (unsigned long)(impl) }
static const m98_named_api kernel32_apis[] = {
    M98_API("AcquireSRWLockExclusive", m98_AcquireSRWLockExclusive),
    M98_API("AcquireSRWLockShared", m98_AcquireSRWLockShared),
    M98_API("CallbackMayRunLong", m98_CallbackMayRunLong),
    M98_API("CloseThreadpoolWork", m98_CloseThreadpoolWork),
    M98_API("CompareStringEx", m98_CompareStringEx),
    M98_API("CompareStringOrdinal", m98_CompareStringOrdinal),
    M98_API("ConvertThreadToFiber", m98_ConvertThreadToFiber),
    M98_API("ConvertThreadToFiberEx", m98_ConvertThreadToFiberEx),
    M98_API("CreateFiber", m98_CreateFiber),
    M98_API("CreateFiberEx", m98_CreateFiberEx),
    M98_API("CreateThread", m98_CreateThread),
    M98_API("CreateThreadpoolWork", m98_CreateThreadpoolWork),
    M98_API("DeleteFiber", m98_DeleteFiber),
    M98_API("DisassociateCurrentThreadFromCallback", m98_DisassociateCurrentThreadFromCallback),
    M98_API("ExitThread", m98_ExitThread),
    M98_API("FindFirstStreamW", m98_FindFirstStreamW),
    M98_API("FlsAlloc", m98_FlsAlloc),
    M98_API("FlsFree", m98_FlsFree),
    M98_API("FlsGetValue", m98_FlsGetValue),
    M98_API("FlsSetValue", m98_FlsSetValue),
    M98_API("FreeLibraryAndExitThread", m98_FreeLibraryAndExitThread),
    M98_API("FreeLibraryWhenCallbackReturns", m98_FreeLibraryWhenCallbackReturns),
    M98_API("GetActiveProcessorCount", m98_GetActiveProcessorCount),
    M98_API("GetActiveProcessorGroupCount", m98_GetActiveProcessorGroupCount),
    M98_API("GetApplicationRestartSettings", m98_GetApplicationRestartSettings),
    M98_API("GetCurrentPackageFamilyName", m98_GetCurrentPackageFamilyName),
    M98_API("GetCurrentPackageFullName", m98_GetCurrentPackageFullName),
    M98_API("GetCurrentPackageId", m98_GetCurrentPackageId),
    M98_API("GetCurrentPackageInfo", m98_GetCurrentPackageInfo),
    M98_API("GetCurrentPackagePath", m98_GetCurrentPackagePath),
    M98_API("GetCurrentProcessorNumber", m98_GetCurrentProcessorNumber),
    M98_API("GetCurrentProcessorNumberEx", m98_GetCurrentProcessorNumberEx),
    M98_API("GetDateFormatEx", m98_GetDateFormatEx),
    M98_API("GetFileInformationByHandleEx", m98_GetFileInformationByHandleEx),
    M98_API("GetFinalPathNameByHandleW", m98_GetFinalPathNameByHandleW),
    M98_API("GetFirmwareType", m98_GetFirmwareType),
    M98_API("GetLocaleInfoEx", m98_GetLocaleInfoEx),
    M98_API("GetMaximumProcessorCount", m98_GetMaximumProcessorCount),
    M98_API("GetMaximumProcessorGroupCount", m98_GetMaximumProcessorGroupCount),
    M98_API("GetNumaAvailableMemoryNode", m98_GetNumaAvailableMemoryNode),
    M98_API("GetNumaAvailableMemoryNodeEx", m98_GetNumaAvailableMemoryNodeEx),
    M98_API("GetNumaHighestNodeNumber", m98_GetNumaHighestNodeNumber),
    M98_API("GetNumaNodeProcessorMask", m98_GetNumaNodeProcessorMask),
    M98_API("GetNumaNodeProcessorMaskEx", m98_GetNumaNodeProcessorMaskEx),
    M98_API("GetNumaProcessorNode", m98_GetNumaProcessorNode),
    M98_API("GetNumaProcessorNodeEx", m98_GetNumaProcessorNodeEx),
    M98_API("GetPackageFamilyName", m98_GetPackageFamilyName),
    M98_API("GetPackageFullName", m98_GetPackageFullName),
    M98_API("GetPackageId", m98_GetPackageId),
    M98_API("GetProcessGroupAffinity", m98_GetProcessGroupAffinity),
    M98_API("GetProductInfo", m98_GetProductInfo),
    M98_API("GetSystemDEPPolicy", m98_GetSystemDEPPolicy),
    M98_API("GetSystemTimePreciseAsFileTime", m98_GetSystemTimePreciseAsFileTime),
    M98_API("GetTickCount64", m98_GetTickCount64),
    M98_API("GetTimeFormatEx", m98_GetTimeFormatEx),
    M98_API("InitOnceBeginInitialize", m98_InitOnceBeginInitialize),
    M98_API("InitOnceComplete", m98_InitOnceComplete),
    M98_API("InitOnceExecuteOnce", m98_InitOnceExecuteOnce),
    M98_API("InitOnceInitialize", m98_InitOnceInitialize),
    M98_API("InitializeConditionVariable", m98_InitializeConditionVariable),
    M98_API("InitializeCriticalSectionEx", m98_InitializeCriticalSectionEx),
    M98_API("InitializeSListHead", m98_InitializeSListHead),
    M98_API("InitializeSRWLock", m98_InitializeSRWLock),
    M98_API("InterlockedFlushSList", m98_InterlockedFlushSList),
    M98_API("InterlockedPopEntrySList", m98_InterlockedPopEntrySList),
    M98_API("InterlockedPushEntrySList", m98_InterlockedPushEntrySList),
    M98_API("InterlockedPushListSList", m98_InterlockedPushListSList),
    M98_API("InterlockedPushListSListEx", m98_InterlockedPushListSListEx),
    M98_API("LCMapStringEx", m98_LCMapStringEx),
    M98_API("LeaveCriticalSectionWhenCallbackReturns", m98_LeaveCriticalSectionWhenCallbackReturns),
    M98_API("QueryDepthSList", m98_QueryDepthSList),
    M98_API("QueryFullProcessImageNameA", m98_QueryFullProcessImageNameA),
    M98_API("QueryFullProcessImageNameW", m98_QueryFullProcessImageNameW),
    M98_API("RegisterApplicationRestart", m98_RegisterApplicationRestart),
    M98_API("ReleaseMutexWhenCallbackReturns", m98_ReleaseMutexWhenCallbackReturns),
    M98_API("ReleaseSRWLockExclusive", m98_ReleaseSRWLockExclusive),
    M98_API("ReleaseSRWLockShared", m98_ReleaseSRWLockShared),
    M98_API("ReleaseSemaphoreWhenCallbackReturns", m98_ReleaseSemaphoreWhenCallbackReturns),
    M98_API("SetEventWhenCallbackReturns", m98_SetEventWhenCallbackReturns),
    M98_API("SleepConditionVariableSRW", m98_SleepConditionVariableSRW),
    M98_API("SubmitThreadpoolWork", m98_SubmitThreadpoolWork),
    M98_API("SwitchToFiber", m98_SwitchToFiber),
    M98_API("TryAcquireSRWLockExclusive", m98_TryAcquireSRWLockExclusive),
    M98_API("TryAcquireSRWLockShared", m98_TryAcquireSRWLockShared),
    M98_API("TrySubmitThreadpoolCallback", m98_TrySubmitThreadpoolCallback),
    M98_API("UnregisterApplicationRestart", m98_UnregisterApplicationRestart),
    M98_API("WaitForThreadpoolWorkCallbacks", m98_WaitForThreadpoolWorkCallbacks),
    M98_API("WakeAllConditionVariable", m98_WakeAllConditionVariable),
    M98_API("WakeConditionVariable", m98_WakeConditionVariable)
};

static const m98_api_table api_tables[] = {
    { "KERNEL32.DLL", kernel32_apis,
      sizeof(kernel32_apis) / sizeof(kernel32_apis[0]), 0, 0 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return api_tables;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&tick_lock);
        InitializeCriticalSection(&m98_locale_lock);
        m98_nls_ex_set_module(instance);
        m98_nls_ex_set_resolver(m98_locale_name_to_lcid);
        InitializeCriticalSection(&m98_condition_lock);
        m98_condition_first = 0;
        m98_condition_last = 0;
        if (!m98_tp_initialize()) {
            DeleteCriticalSection(&m98_condition_lock);
            m98_nls_ex_set_resolver(0);
            m98_nls_ex_set_module(0);
            DeleteCriticalSection(&m98_locale_lock);
            DeleteCriticalSection(&tick_lock);
            return FALSE;
        }
        last_tick = GetTickCount();
        tick_high = 0;
    } else if (reason == DLL_PROCESS_DETACH) {
        /* ExitProcess already killed other threads, possibly while they held
         * any of our locks. Leave all process resources to the OS here. */
        if (reserved) return TRUE;
        m98_tp_process_detach(reserved != NULL);
        DeleteCriticalSection(&m98_condition_lock);
        m98_nls_ex_set_resolver(0);
        m98_nls_ex_set_module(0);
        DeleteCriticalSection(&m98_locale_lock);
        DeleteCriticalSection(&tick_lock);
    }
    return TRUE;
}
