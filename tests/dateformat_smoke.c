/* Direct KernelEx API-table test; only Win98 KERNEL32 imports, no CRT. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

#ifndef DATE_YEARMONTH
#define DATE_YEARMONTH 0x00000008
#endif

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef int (WINAPI *date_format_ex_fn)(const WCHAR *, DWORD,
    const SYSTEMTIME *, const WCHAR *, WCHAR *, int, const WCHAR *);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

static void fail(const char *message)
{
    say("FAIL: ");
    say(message);
    say("\r\n");
    ExitProcess(1);
}

static BOOL same_ascii(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static BOOL same_wide(const WCHAR *a, const WCHAR *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

void mainCRTStartup(void)
{
    HMODULE dll = LoadLibraryA("m98wrap.dll");
    get_api_table_fn get_table;
    const m98_api_table *table;
    date_format_ex_fn format_date = 0;
    SYSTEMTIME date = { 2026, 9, 0, 23, 25, 61, 61, 1000 };
    WCHAR output[80];
    int i, size;

    if (!dll) fail("LoadLibrary m98wrap.dll");
    get_table = (get_api_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !same_ascii(table->target_library, "KERNEL32.DLL"))
        fail("KernelEx KERNEL32 table");
    for (i = 0; i < table->named_apis_count; ++i)
        if (same_ascii(table->named_apis[i].name, "GetDateFormatEx"))
            format_date = (date_format_ex_fn)table->named_apis[i].addr;
    if (!format_date) fail("GetDateFormatEx pointer");

    if (format_date(L"en-US", 0, &date, L"yyyy'-'MM'-'dd",
                    output, 80, 0) != 11 ||
        !same_wide(output, L"2026-09-23"))
        fail("en-US picture and ignored time members");
    if (format_date(L"en", 0, &date, L"dddd",
                    output, 80, 0) != 10 ||
        !same_wide(output, L"Wednesday"))
        fail("neutral locale and corrected weekday");
    if (format_date(L"ko-KR", 0, &date,
                    L"yyyy'\xB144'MM'\xC6D4'dd'\xC77C'",
                    output, 80, 0) != 12 ||
        !same_wide(output, L"2026\xB144" L"09\xC6D4" L"23\xC77C"))
        fail("Korean DBCS picture returns UTF-16 character count");
    if (format_date(L"ko-KR", 0, &date,
                    L"yyyy'\xB144'MM'\xC6D4'dd'\xC77C'",
                    0, 0, 0) != 12)
        fail("DBCS length query counts WCHARs, including NUL");
    if (format_date(L"", 0, &date, 0, output, 80, 0) != 11 ||
        !same_wide(output, L"09/23/2026"))
        fail("invariant short date");
    if (format_date(L"", LOCALE_NOUSEROVERRIDE, &date, 0,
                    output, 80, 0) != 11 ||
        !same_wide(output, L"09/23/2026"))
        fail("invariant date ignores user override");
    if (!format_date(0, 0, &date, 0, output, 80, 0) || !output[0])
        fail("user default locale");
    if (!format_date(L"!x-sys-default-locale", 0, &date,
                     0, output, 80, 0) || !output[0])
        fail("system default locale");
    if (!format_date(L"en-US", 0, 0, 0, output, 80, 0) || !output[0])
        fail("null SYSTEMTIME uses local date");
    if (!format_date(L"en-US", DATE_LONGDATE, &date,
                     0, output, 80, 0) || !output[0])
        fail("native long date");
    size = format_date(L"en-US", DATE_YEARMONTH, &date,
                       0, output, 80, 0);
    if (!size && GetLastError() != ERROR_INVALID_FLAGS)
        fail("year-month available or explicitly unsupported");

    output[0] = L'X';
    if (format_date(L"en-US", 0, &date, L"yyyy'-'MM'-'dd",
                    output, 10, 0) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || output[0] != L'X')
        fail("short output buffer is untouched");
    if (format_date(L"en-US", 0, &date, 0, 0, 1, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("null output with nonzero capacity");
    if (format_date(L"en-US", 0, &date, 0, output, -1, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("negative output capacity");
    if (format_date(L"en-US", 0, &date, 0, output, 80, L"") ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("reserved calendar pointer");
    if (format_date(L"zz-ZZ", 0, &date, 0, output, 80, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("unknown locale");
    if (format_date(L"zh-Hans-CN", 0, &date, 0, output, 80, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("script-qualified locale cannot be represented");
    if (format_date(L"en-US", 0, &date, L"yyyy'\xB144'",
                    output, 80, 0) ||
        GetLastError() != ERROR_NO_UNICODE_TRANSLATION)
        fail("unrepresentable Unicode picture");
    if (format_date(L"en-US", 0x10000000, &date, 0,
                    output, 80, 0) || GetLastError() != ERROR_INVALID_FLAGS)
        fail("unknown flags");
    if (format_date(L"en-US", DATE_SHORTDATE | DATE_LONGDATE, &date,
                    0, output, 80, 0) ||
        GetLastError() != ERROR_INVALID_FLAGS)
        fail("mutually exclusive styles");
    if (format_date(L"en-US", DATE_LONGDATE, &date,
                    L"yyyy", output, 80, 0) ||
        GetLastError() != ERROR_INVALID_FLAGS)
        fail("date style with custom picture");
    if (format_date(L"en-US", LOCALE_NOUSEROVERRIDE, &date,
                    L"yyyy", output, 80, 0) ||
        GetLastError() != ERROR_INVALID_FLAGS)
        fail("no-user-override with custom picture");
    date.wYear = 2025;
    date.wMonth = 2;
    date.wDay = 29;
    if (format_date(L"en-US", 0, &date, 0, output, 80, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("invalid leap day");
    date.wYear = 1600;
    date.wMonth = 9;
    date.wDay = 23;
    if (format_date(L"en-US", 0, &date, 0, output, 80, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("date before 1601");

    say("PASS: GetDateFormatEx Win98 direct behavior\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
