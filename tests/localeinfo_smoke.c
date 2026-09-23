/* Direct KernelEx table test of the bounded GetLocaleInfoEx bridge. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

#ifndef LOCALE_SNAME
#define LOCALE_SNAME 0x0000005cUL
#endif
#ifndef LOCALE_SPARENT
#define LOCALE_SPARENT 0x0000006dUL
#endif
#ifndef LOCALE_RETURN_GENITIVE_NAMES
#define LOCALE_RETURN_GENITIVE_NAMES 0x10000000UL
#endif
#ifndef LOCALE_FONTSIGNATURE
#define LOCALE_FONTSIGNATURE 0x00000058UL
#endif

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef int (WINAPI *locale_info_ex_fn)(const WCHAR *, LCTYPE, WCHAR *, int);

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

static BOOL same_ascii(const char *left, const char *right)
{
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static BOOL same_wide(const WCHAR *left, const WCHAR *right)
{
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

void mainCRTStartup(void)
{
    HMODULE dll = LoadLibraryA("m98wrap.dll");
    get_api_table_fn get_table;
    const m98_api_table *table;
    locale_info_ex_fn info = 0;
    WCHAR output[128];
    WCHAR number[2];
    BYTE native_signature[32];
    int i, size;

    if (!dll) fail("LoadLibrary m98wrap.dll");
    get_table = (get_api_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !same_ascii(table->target_library, "KERNEL32.DLL"))
        fail("KERNEL32 table");
    for (i = 0; i < table->named_apis_count; ++i)
        if (same_ascii(table->named_apis[i].name, "GetLocaleInfoEx"))
            info = (locale_info_ex_fn)table->named_apis[i].addr;
    if (!info) fail("GetLocaleInfoEx table pointer");

    if (info(L"en-US", LOCALE_SDECIMAL | LOCALE_NOUSEROVERRIDE,
             output, 128) != 2 || !same_wide(output, L"."))
        fail("en-US decimal string");
    if (info(L"en-US", LOCALE_SDECIMAL | LOCALE_NOUSEROVERRIDE,
             0, 0) != 2)
        fail("string size query includes NUL");
    output[0] = L'X';
    if (info(L"en-US", LOCALE_SDECIMAL | LOCALE_NOUSEROVERRIDE,
             output, 1) || GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        output[0] != L'X')
        fail("short text buffer stays untouched");
    if (info(L"en-US", LOCALE_SDECIMAL, 0, 1) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("null output with nonzero capacity");
    if (info(L"en-US", LOCALE_SDECIMAL, output, -1) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("negative capacity");

    if (info(L"ko-KR", LOCALE_SNATIVELANGNAME, output, 128) != 4 ||
        !same_wide(output, L"\xD55C\xAD6D\xC5B4"))
        fail("Korean ANSI-to-UTF16 conversion");
    if (info(L"ko-KR", LOCALE_SNATIVELANGNAME, 0, 0) != 4)
        fail("Korean size is WCHARs, not DBCS bytes");

    if (info(L"en-US", LOCALE_IDIGITS | LOCALE_NOUSEROVERRIDE |
             LOCALE_RETURN_NUMBER, 0, 0) != 2)
        fail("numeric size is two WCHARs");
    number[0] = 0xffff;
    number[1] = 0xffff;
    if (info(L"en-US", LOCALE_IDIGITS | LOCALE_NOUSEROVERRIDE |
             LOCALE_RETURN_NUMBER, number, 2) != 2 ||
        ((DWORD)number[0] | ((DWORD)number[1] << 16)) != 2)
        fail("numeric DWORD result");
    number[0] = 0xffff;
    if (info(L"en-US", LOCALE_IDIGITS | LOCALE_RETURN_NUMBER,
             number, 1) || GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        number[0] != 0xffff)
        fail("short number buffer stays untouched");
    output[0] = L'X';
    if (info(L"en-US", LOCALE_SDECIMAL | LOCALE_RETURN_NUMBER,
             output, 128) || output[0] != L'X')
        fail("numeric flag on string field fails without output");

    if (GetLocaleInfoA(0x0409, LOCALE_FONTSIGNATURE,
                       (char *)native_signature, sizeof(native_signature)) !=
        (int)sizeof(native_signature))
        fail("native font-signature fixture");
    if (info(L"en-US", LOCALE_FONTSIGNATURE, 0, 0) != 16 ||
        info(L"en-US", LOCALE_FONTSIGNATURE, output, 128) != 16)
        fail("binary font-signature length");
    for (i = 0; i < (int)sizeof(native_signature); ++i)
        if (((BYTE *)output)[i] != native_signature[i])
            fail("binary font-signature content");
    output[0] = L'X';
    if (info(L"en-US", LOCALE_FONTSIGNATURE, output, 15) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || output[0] != L'X')
        fail("short font-signature buffer stays untouched");

    if (info(L"en", LOCALE_SNAME, output, 128) != 3 ||
        !same_wide(output, L"en"))
        fail("neutral locale name");
    if (info(L"en", LOCALE_SPARENT, output, 128) != 1 || output[0])
        fail("neutral locale has empty parent");
    if (info(L"en-US", LOCALE_SNAME, output, 128) != 6 ||
        !same_wide(output, L"en-US"))
        fail("regional locale name");
    if (info(L"en-US", LOCALE_SPARENT, output, 128) != 3 ||
        !same_wide(output, L"en"))
        fail("regional locale parent");
    if (!info(0, LOCALE_SDECIMAL, output, 128) || !output[0])
        fail("user default locale");
    if (!info(L"!x-sys-default-locale", LOCALE_SDECIMAL,
              output, 128) || !output[0])
        fail("system default locale");

    output[0] = L'X';
    if (info(L"", LOCALE_SDECIMAL, output, 128) ||
        GetLastError() != ERROR_NOT_SUPPORTED || output[0] != L'X')
        fail("invariant locale is explicitly unsupported");
    if (info(L"zz-ZZ", LOCALE_SDECIMAL, output, 128) ||
        GetLastError() != ERROR_INVALID_PARAMETER || output[0] != L'X')
        fail("unknown locale");
    if (info(L"zh-Hans-CN", LOCALE_SDECIMAL, output, 128) ||
        GetLastError() != ERROR_INVALID_PARAMETER || output[0] != L'X')
        fail("script-qualified locale is not guessed");
    if (info(L"en-US", LOCALE_SDECIMAL | LOCALE_RETURN_GENITIVE_NAMES,
             output, 128) || GetLastError() != ERROR_INVALID_FLAGS ||
        output[0] != L'X')
        fail("genitive flag is explicitly unsupported");
    if (info(L"en-US", LOCALE_SDECIMAL | 0x00010000UL,
             output, 128) || GetLastError() != ERROR_INVALID_FLAGS ||
        output[0] != L'X')
        fail("unknown flag rejected");
    size = info(L"en-US", LOCALE_SDECIMAL | LOCALE_USE_CP_ACP,
                output, 128);
    if (size != 2 || !same_wide(output, L"."))
        fail("ACP flag cannot change Unicode result");

    say("PASS: GetLocaleInfoEx direct Win98 NLS subset\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
