/* Direct ABI/behavior test of the isolated NLS Ex KernelEx table. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

#ifndef SORT_DIGITSASNUMBERS
#define SORT_DIGITSASNUMBERS 0x00000008UL
#endif
#ifndef LCMAP_HASH
#define LCMAP_HASH 0x00040000UL
#endif

typedef const m98_api_table *(*table_fn)(void);
typedef int (WINAPI *map_fn)(const WCHAR *, DWORD, const WCHAR *, int,
                             WCHAR *, int, void *, void *, LPARAM);
typedef int (WINAPI *compare_fn)(const WCHAR *, DWORD, const WCHAR *, int,
                                 const WCHAR *, int, void *, void *, LPARAM);

static void say(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, 0);
}

static void fail(const char *message)
{
    say("FAIL: ");
    say(message);
    say("\r\n");
    ExitProcess(1);
}

static BOOL same_a(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static BOOL same_w(const WCHAR *a, const WCHAR *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

void mainCRTStartup(void)
{
#ifdef M98_INTEGRATED
    HMODULE dll = LoadLibraryA("m98wrap.dll");
#else
    HMODULE dll = LoadLibraryA("nls_ex_fixture.dll");
#endif
    table_fn get_table;
    const m98_api_table *table;
    map_fn map = 0;
    compare_fn compare = 0;
    WCHAR output[32], short_output[2];
    BYTE sort_key[128];
    int i, required;

    if (!dll) fail("LoadLibrary fixture");
    get_table = (table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table export");
    table = get_table();
    if (!table || !same_a(table->target_library, "KERNEL32.DLL") ||
#ifndef M98_INTEGRATED
        table->named_apis_count != 2 ||
        !same_a(table->named_apis[0].name, "CompareStringEx") ||
        !same_a(table->named_apis[1].name, "LCMapStringEx") ||
#endif
        table->named_apis_count < 2)
        fail("sorted KernelEx ABI table");
    for (i = 0; i < table->named_apis_count; ++i) {
        if (same_a(table->named_apis[i].name, "CompareStringEx"))
            compare = (compare_fn)table->named_apis[i].addr;
        if (same_a(table->named_apis[i].name, "LCMapStringEx"))
            map = (map_fn)table->named_apis[i].addr;
    }
    if (!map || !compare) fail("both table pointers");

    if (map(L"en-US", LCMAP_UPPERCASE, L"Abc", -1,
            output, 32, 0, 0, 0) != 4 || !same_w(output, L"ABC"))
        fail("uppercase mapping and terminator");
    if (map(L"en-US", LCMAP_UPPERCASE, L"Abc", -1,
            0, 0, 0, 0, 0) != 4)
        fail("mapping size query in WCHARs");
    output[3] = L'Z';
    if (map(L"en-US", LCMAP_UPPERCASE, L"Abc!", 3,
            output, 32, 0, 0, 0) != 3 ||
        output[0] != L'A' || output[1] != L'B' ||
        output[2] != L'C' || output[3] != L'Z')
        fail("explicit source length excludes terminator");
    if (map(L"ko-KR", LCMAP_UPPERCASE, L"\xD55C\xAE00", -1,
            output, 32, 0, 0, 0) != 3 ||
        !same_w(output, L"\xD55C\xAE00"))
        fail("Korean Unicode text preserved");
    if (map(L"en-US", LCMAP_UPPERCASE, L"Abc", -1,
            short_output, 2, 0, 0, 0) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("short mapped buffer");
    if (map(L"en-US", LCMAP_UPPERCASE, L"Abc", -1,
            0, 2, 0, 0, 0) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("null mapped buffer with nonzero size");

    required = map(L"en-US", LCMAP_SORTKEY, L"abc", -1,
                   0, 0, 0, 0, 0);
    if (required < 2 || required > (int)sizeof(sort_key))
        fail("sort key size query in bytes");
    if (map(L"en-US", LCMAP_SORTKEY, L"abc", -1,
            (WCHAR *)sort_key, required, 0, 0, 0) != required)
        fail("sort key byte buffer");
    if (map(L"en-US", LCMAP_SORTKEY, L"abc", -1,
            (WCHAR *)sort_key, required - 1, 0, 0, 0) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("short sort key byte buffer");

    if (compare(L"en-US", 0, L"abc", -1, L"abd", -1,
                0, 0, 0) != CSTR_LESS_THAN)
        fail("lexical less than");
    if (compare(L"en-US", NORM_IGNORECASE, L"Abc", -1,
                L"aBC", -1, 0, 0, 0) != CSTR_EQUAL)
        fail("case-insensitive equality");
    if (compare(L"en-US", 0, L"abcdef", 3, L"abcXYZ", 3,
                0, 0, 0) != CSTR_EQUAL)
        fail("explicit comparison length");
    if (compare(L"en-US", 0, L"abd", -1, L"abc", -1,
                0, 0, 0) != CSTR_GREATER_THAN)
        fail("lexical greater than");
    if (compare(0, 0, L"same", -1, L"same", -1,
                0, 0, 0) != CSTR_EQUAL)
        fail("user default locale");

    if (compare(L"zz-ZZ", 0, L"a", -1, L"b", -1,
                0, 0, 0) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("unknown locale rejected");
    if (map(L"zh-Hans-CN", LCMAP_UPPERCASE, L"a", -1,
            output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("script locale not guessed");
    if (compare(L"", 0, L"a", -1, L"a", -1,
                0, 0, 0) || GetLastError() != ERROR_NOT_SUPPORTED)
        fail("invariant locale explicit failure");
    if (map(L"", LCMAP_UPPERCASE, L"a", -1,
            output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("invariant mapping explicit failure");
    if (map(L"en-US", LCMAP_UPPERCASE, 0, -1,
            output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("null source rejected");
    if (map(L"en-US", LCMAP_UPPERCASE, L"a", 0,
            output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("zero source length rejected");
    if (compare(L"en-US", 0, 0, -1, L"a", -1,
                0, 0, 0) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("null comparison string rejected");
    if (map(L"en-US", LCMAP_UPPERCASE, L"a", -1,
            output, 32, output, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("NLS version unsupported");
    if (compare(L"en-US", 0, L"a", -1, L"a", -1,
                0, output, 0) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("reserved pointer rejected");
    if (compare(L"en-US", 0, L"a", -1, L"a", -1,
                0, 0, 1) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("sort handle rejected");
    if (map(L"en-US", LCMAP_HASH, L"a", -1,
            output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("modern hash flag unsupported");
    if (map(L"en-US", LCMAP_BYTEREV, L"a", -1,
            output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("active W byte reversal no-op rejected");
    if (map(L"en-US", LCMAP_HIRAGANA, L"a", -1,
            output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("active W kana no-op rejected");
    if (map(L"en-US", LCMAP_SORTKEY | NORM_IGNORENONSPACE,
            L"a", -1, output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("active W sort-key modifier no-op rejected");
    if (map(L"en-US", LCMAP_UPPERCASE | LCMAP_LOWERCASE,
            L"a", -1, output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("titlecase unsupported");
    if (compare(L"en-US", SORT_DIGITSASNUMBERS,
                L"2", -1, L"10", -1, 0, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("numeric collation unsupported");
    if (compare(L"en-US", NORM_IGNOREWIDTH,
                L"a", -1, L"a", -1, 0, 0, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("active W width modifier no-op rejected");
    if (compare(L"en-US", 0x10000000UL,
                L"a", -1, L"b", -1, 0, 0, 0) ||
        GetLastError() != ERROR_INVALID_FLAGS)
        fail("unknown compare flag rejected");
    if (map(L"en-US", SORT_STRINGSORT, L"a", -1,
            output, 32, 0, 0, 0) ||
        GetLastError() != ERROR_INVALID_FLAGS)
        fail("sort flag without sort key rejected");

    say("PASS: NLS Ex bridge host ABI and bounded behavior\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
