/* Direct KernelEx API-table test; only Win98 KERNEL32 imports, no CRT. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef int (WINAPI *time_format_ex_fn)(const WCHAR *, DWORD,
                                       const SYSTEMTIME *, const WCHAR *,
                                       WCHAR *, int);

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

static void show_wide(const WCHAR *value)
{
    char ascii[64];
    int i = 0;
    while (value[i] && i < 62) {
        ascii[i] = value[i] <= 0x7f ? (char)value[i] : '?';
        ++i;
    }
    ascii[i] = 0;
    say("OBSERVED: ");
    say(ascii);
    say("\r\n");
}

void mainCRTStartup(void)
{
    HMODULE dll = LoadLibraryA("m98wrap.dll");
    get_api_table_fn get_table;
    const m98_api_table *table;
    time_format_ex_fn format_time = 0;
    SYSTEMTIME time = { 2026, 9, 0, 23, 13, 5, 9, 0 };
    WCHAR output[64];
    int i;

    if (!dll) fail("LoadLibrary m98wrap.dll");
    get_table = (get_api_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !same_ascii(table->target_library, "KERNEL32.DLL"))
        fail("KernelEx KERNEL32 table");
    for (i = 0; i < table->named_apis_count; ++i)
        if (same_ascii(table->named_apis[i].name, "GetTimeFormatEx"))
            format_time = (time_format_ex_fn)table->named_apis[i].addr;
    if (!format_time) fail("GetTimeFormatEx pointer");

    if (format_time(L"en-US", 0, &time, L"HH':'mm':'ss", output, 64) != 9 ||
        !same_wide(output, L"13:05:09"))
        fail("en-US 24-hour picture");
    if (format_time(L"en", 0, &time, L"hh':'mm tt", output, 64) != 9 ||
        !same_wide(output, L"01:05 PM")) {
        show_wide(output);
        fail("neutral en LCID selection and PM marker");
    }
    if (format_time(L"ko-KR", 0, &time, L"HH':'mm", output, 64) != 6 ||
        !same_wide(output, L"13:05"))
        fail("ko-KR ISO locale");
    if (format_time(L"ko-KR", 0, &time, L"HH'\xC2DC'mm", output, 64) != 6 ||
        !same_wide(output, L"13\xC2DC" L"05"))
        fail("ko-KR DBCS output counted in WCHARs");
    if (format_time(0, 0, &time, L"HH':'mm", output, 64) != 6 ||
        !same_wide(output, L"13:05"))
        fail("user default locale");
    if (format_time(L"!x-sys-default-locale", 0, &time,
                    L"HH':'mm", output, 64) != 6 ||
        !same_wide(output, L"13:05"))
        fail("system default locale");
    if (format_time(L"", 0, &time, 0, output, 64) != 9 ||
        !same_wide(output, L"13:05:09"))
        fail("invariant default picture");
    if (format_time(L"", LOCALE_NOUSEROVERRIDE, &time, 0,
                    output, 64) != 9 || !same_wide(output, L"13:05:09"))
        fail("invariant default ignores user overrides");
    if (format_time(L"en-US", TIME_NOSECONDS, &time,
                    L"HH:mm:ss", output, 64) != 6 ||
        !same_wide(output, L"13:05")) {
        show_wide(output);
        fail("TIME_NOSECONDS removes seconds and separator");
    }
    if (format_time(L"en-US", TIME_FORCE24HOURFORMAT, &time,
                    L"h':'mm", output, 64) != 6 ||
        !same_wide(output, L"13:05"))
        fail("TIME_FORCE24HOURFORMAT uses 24-hour clock");
    if (!format_time(L"en-US", 0, 0, 0, output, 64) || !output[0])
        fail("null SYSTEMTIME uses local time");
    if (format_time(L"en-US", 0, &time, L"HH':'mm':'ss", 0, 0) != 9)
        fail("WCHAR length query including NUL");

    output[0] = L'X';
    if (format_time(L"en-US", 0, &time, L"HH':'mm':'ss", output, 8) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || output[0] != L'X')
        fail("short output buffer");
    if (format_time(L"en-US", 0, &time, L"HH':'mm", 0, 6) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("null output with nonzero size");
    if (format_time(L"en-US", 0, &time, L"HH':'mm", output, -1) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("negative output size");
    if (format_time(L"zz-ZZ", 0, &time, 0, output, 64) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("unknown locale rejected");
    if (format_time(L"zh-Hans-CN", 0, &time, 0, output, 64) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("unrepresentable script-qualified locale rejected");
    if (format_time(L"en-US", 0, &time, L"HH'\xC2DC'mm", output, 64) ||
        GetLastError() != ERROR_NO_UNICODE_TRANSLATION)
        fail("unrepresentable Unicode picture rejected");
    if (format_time(L"en-US", 0x10000000, &time, 0, output, 64) ||
        GetLastError() != ERROR_INVALID_FLAGS)
        fail("unknown flag rejected");
    if (format_time(L"en-US", LOCALE_NOUSEROVERRIDE, &time,
                    L"HH", output, 64) ||
        GetLastError() != ERROR_INVALID_FLAGS)
        fail("locale override flag with custom picture");
    time.wHour = 24;
    if (format_time(L"en-US", 0, &time, 0, output, 64) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("invalid SYSTEMTIME rejected");

    say("PASS: GetTimeFormatEx Win98 direct behavior\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
