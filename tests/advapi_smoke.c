/* Direct KernelEx API-table test, with isolated HKCU registry fixtures. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

#define RT_SZ       0x00000002UL
#define RT_EXPAND   0x00000004UL
#define RT_DWORD    0x00000018UL
#define RT_MULTI    0x00000020UL
#define RT_ANY      0x0000ffffUL
#define VIEW64      0x00010000UL
#define VIEW32      0x00020000UL
#define NOEXPAND    0x10000000UL
#define ZEROFAIL    0x20000000UL

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef LONG (WINAPI *get_value_fn)(HKEY, const WCHAR *, const WCHAR *,
                                     DWORD, DWORD *, void *, DWORD *);
typedef LONG (WINAPI *get_value_a_fn)(HKEY, const char *, const char *,
                                       DWORD, DWORD *, void *, DWORD *);

static void say(const char *text)
{
    DWORD length = 0, written;
    while (text[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, NULL);
}

static void fail(const char *why)
{
    say("FAIL: RegGetValueA/W ");
    say(why);
    say("\r\n");
    ExitProcess(1);
}

#define CHECK(test, why) do { if (!(test)) fail(why); } while (0)

static int same(const void *a, const void *b, DWORD size)
{
    const BYTE *left = (const BYTE *)a, *right = (const BYTE *)b;
    DWORD i;
    for (i = 0; i < size; ++i) if (left[i] != right[i]) return 0;
    return 1;
}

static int same_ascii(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void fill(BYTE *data, DWORD size, BYTE value)
{
    DWORD i;
    for (i = 0; i < size; ++i) data[i] = value;
}

static void set_value(HKEY key, const char *name, DWORD type,
                      const void *data, DWORD size)
{
    CHECK(RegSetValueExA(key, name, 0, type, (const BYTE *)data, size) ==
          ERROR_SUCCESS, "RegSetValueExA fixture");
}

typedef struct reader_context {
    get_value_fn get_value;
    HKEY key;
    volatile LONG failed;
} reader_context;

static DWORD WINAPI concurrent_reader(void *argument)
{
    reader_context *context = (reader_context *)argument;
    WCHAR text[16];
    DWORD size, type;
    int i;
    for (i = 0; i < 100; ++i) {
        size = sizeof(text);
        type = 0;
        if (context->get_value(context->key, NULL, L"plain", RT_ANY,
                               &type, text, &size) != ERROR_SUCCESS ||
            type != REG_SZ || size != sizeof(L"hello") ||
            !same(text, L"hello", size)) {
            InterlockedExchange(&context->failed, 1);
            return 1;
        }
    }
    return 0;
}

void mainCRTStartup(void)
{
    HMODULE library = LoadLibraryA("m98advapi.dll");
    get_api_table_fn get_table;
    const m98_api_table *table;
    get_value_fn get_value = NULL;
    get_value_a_fn get_value_a = NULL;
    HKEY key, child;
    DWORD disposition, size, type, number = 0x12345678;
    BYTE buffer[128], binary4[] = { 0x11, 0x22, 0x33, 0x44 };
    BYTE binary3[] = { 0x11, 0x22, 0x33 };
    const BYTE multi[] = { 'a', 0, 'b', 0 }; /* deliberately no second NUL */
    const WCHAR multi_expected[] = { 'a', 0, 'b', 0, 0 };
    const char expand[] = "%M98_REG_TEST%/x";
    reader_context readers;
    HANDLE threads[4];
    int i;

    CHECK(library != NULL, "LoadLibrary m98advapi.dll");
    get_table = (get_api_table_fn)GetProcAddress(library, "get_api_table");
    CHECK(get_table != NULL, "get_api_table export");
    table = get_table();
    CHECK(table && same_ascii(table->target_library, "ADVAPI32.DLL") &&
          table->named_apis_count == 2 && table[1].target_library == NULL,
          "KernelEx ADVAPI32 table shape");
    for (i = 0; i < table->named_apis_count; ++i) {
        if (same_ascii(table->named_apis[i].name, "RegGetValueA"))
            get_value_a = (get_value_a_fn)table->named_apis[i].addr;
        if (same_ascii(table->named_apis[i].name, "RegGetValueW"))
            get_value = (get_value_fn)table->named_apis[i].addr;
    }
    CHECK(get_value != NULL && get_value_a != NULL &&
          same_ascii(table->named_apis[0].name, "RegGetValueA"),
          "sorted RegGetValueA/W table entries");

    CHECK(RegCreateKeyExA(HKEY_CURRENT_USER,
          "Software\\Win98Modern-RegGetValue-Smoke", 0, NULL,
          REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &key,
          &disposition) == ERROR_SUCCESS, "create isolated key");
    set_value(key, "plain", REG_SZ, "hello", 5); /* no native NUL */
    type = 0; size = 0;
    CHECK(get_value_a(key, NULL, "plain", RT_ANY, &type, NULL, &size) ==
          ERROR_SUCCESS && type == REG_SZ && size == 6,
          "ANSI size query repairs unterminated REG_SZ");
    size = sizeof(buffer);
    CHECK(get_value_a(key, "", "plain", RT_SZ, &type, buffer, &size) ==
          ERROR_SUCCESS && size == 6 && same(buffer, "hello", size),
          "ANSI REG_SZ data and empty subkey");
    fill(buffer, sizeof(buffer), 0x7a); size = 3;
    CHECK(get_value_a(key, NULL, "plain", RT_ANY | ZEROFAIL,
                      &type, buffer, &size) == ERROR_MORE_DATA && size == 6 &&
          buffer[0] == 0 && buffer[2] == 0 && buffer[3] == 0x7a,
          "ANSI required size and bounded failure zeroing");
    type = 0; size = 0;
    CHECK(get_value(key, NULL, L"plain", RT_ANY, &type, NULL, &size) ==
          ERROR_SUCCESS && type == REG_SZ && size == 12,
          "size query repairs unterminated REG_SZ as UTF-16");
    size = sizeof(buffer);
    CHECK(get_value(key, L"", L"plain", RT_SZ, &type, buffer, &size) ==
          ERROR_SUCCESS && size == 12 &&
          same(buffer, L"hello", size), "REG_SZ data and empty subkey");
    fill(buffer, sizeof(buffer), 0x7a);
    size = 4;
    CHECK(get_value(key, NULL, L"plain", RT_ANY | ZEROFAIL,
                    &type, buffer, &size) == ERROR_MORE_DATA && size == 12 &&
          buffer[0] == 0 && buffer[3] == 0 && buffer[4] == 0x7a,
          "required size and ZEROONFAILURE buffer bounds");
    fill(buffer, sizeof(buffer), 0x7a);
    size = 8;
    CHECK(get_value(key, NULL, L"plain", RT_DWORD | ZEROFAIL,
                    &type, buffer, &size) == ERROR_UNSUPPORTED_TYPE &&
          type == REG_SZ && size == 12 && buffer[0] == 0 &&
          buffer[7] == 0 && buffer[8] == 0x7a,
          "type restriction and ZEROONFAILURE");
    CHECK(get_value(key, NULL, L"plain", 0, NULL, NULL, NULL) ==
          ERROR_UNSUPPORTED_TYPE, "zero type mask restricts all types");
    CHECK(get_value(key, NULL, L"plain", RT_ANY, NULL,
                    buffer, NULL) == ERROR_INVALID_PARAMETER,
          "data requires pcbData");

    set_value(key, "bin4", REG_BINARY, binary4, sizeof(binary4));
    size = sizeof(buffer); type = 0;
    CHECK(get_value_a(key, NULL, "bin4", RT_DWORD, &type, buffer, &size) ==
          ERROR_SUCCESS && type == REG_BINARY && size == 4 &&
          same(buffer, binary4, 4), "ANSI binary value remains binary");
    size = sizeof(buffer); type = 0;
    CHECK(get_value(key, NULL, L"bin4", RT_DWORD, &type, buffer, &size) ==
          ERROR_SUCCESS && type == REG_BINARY && size == 4 &&
          same(buffer, binary4, 4), "binary DWORD-sized value stays binary");
    set_value(key, "bin3", REG_BINARY, binary3, sizeof(binary3));
    size = sizeof(buffer);
    CHECK(get_value(key, NULL, L"bin3", RT_DWORD, &type, buffer, &size) ==
          ERROR_DATATYPE_MISMATCH && type == REG_BINARY && size == 3,
          "binary size mismatch");
    set_value(key, "integer", REG_DWORD, &number, sizeof(number));
    size = sizeof(buffer);
    CHECK(get_value(key, NULL, L"integer", RT_DWORD, &type, buffer, &size) ==
          ERROR_SUCCESS && type == REG_DWORD && size == 4 &&
          same(buffer, &number, 4), "REG_DWORD native bytes");

    CHECK(SetEnvironmentVariableA("M98_REG_TEST", "target"),
          "test environment variable");
    set_value(key, "expand", REG_EXPAND_SZ, expand, sizeof(expand) - 1);
    size = sizeof(buffer);
    CHECK(get_value_a(key, NULL, "expand", RT_ANY, &type, buffer, &size) ==
          ERROR_SUCCESS && type == REG_SZ && size == sizeof("target/x") &&
          same(buffer, "target/x", size), "ANSI expansion to REG_SZ");
    size = sizeof(buffer);
    CHECK(get_value_a(key, NULL, "expand", RT_EXPAND | NOEXPAND,
                      &type, buffer, &size) == ERROR_SUCCESS &&
          type == REG_EXPAND_SZ && size == sizeof(expand) &&
          same(buffer, expand, size), "ANSI NOEXPAND terminator repair");
    size = sizeof(buffer);
    CHECK(get_value(key, NULL, L"expand", RT_ANY, &type, buffer, &size) ==
          ERROR_SUCCESS && type == REG_SZ && size == sizeof(L"target/x") &&
          same(buffer, L"target/x", size), "REG_EXPAND_SZ expands to REG_SZ");
    size = sizeof(buffer);
    CHECK(get_value(key, NULL, L"expand", RT_EXPAND | NOEXPAND,
                    &type, buffer, &size) == ERROR_SUCCESS &&
          type == REG_EXPAND_SZ && size == sizeof(L"%M98_REG_TEST%/x") &&
          same(buffer, L"%M98_REG_TEST%/x", size),
          "NOEXPAND keeps original type and UTF-16 text");
    CHECK(get_value(key, NULL, L"expand", RT_EXPAND,
                    NULL, NULL, NULL) == ERROR_INVALID_PARAMETER,
          "REG_EXPAND_SZ restriction requires NOEXPAND");
    size = 2;
    CHECK(get_value(key, NULL, L"expand", RT_ANY, &type, buffer, &size) ==
          ERROR_MORE_DATA && type == REG_SZ && size == sizeof(L"target/x"),
          "expanded required UTF-16 size");

    set_value(key, "multi", REG_MULTI_SZ, multi, sizeof(multi));
    size = sizeof(buffer);
    CHECK(get_value_a(key, NULL, "multi", RT_MULTI, &type, buffer, &size) ==
          ERROR_SUCCESS && type == REG_MULTI_SZ && size == 5 &&
          same(buffer, "a\0b\0\0", size), "ANSI MULTI_SZ double NUL");
    size = sizeof(buffer);
    CHECK(get_value(key, NULL, L"multi", RT_MULTI, &type, buffer, &size) ==
          ERROR_SUCCESS && type == REG_MULTI_SZ &&
          size == sizeof(multi_expected) &&
          same(buffer, multi_expected, size), "REG_MULTI_SZ double NUL");

    set_value(key, NULL, REG_SZ, "default", 8);
    size = sizeof(buffer);
    CHECK(get_value(key, NULL, NULL, RT_ANY, &type, buffer, &size) ==
          ERROR_SUCCESS && size == sizeof(L"default") &&
          same(buffer, L"default", size), "NULL default value name");
    size = sizeof(buffer);
    CHECK(get_value(key, NULL, L"", RT_ANY, &type, buffer, &size) ==
          ERROR_SUCCESS && same(buffer, L"default", size),
          "empty default value name");

    CHECK(RegCreateKeyExA(key, "Child", 0, NULL, REG_OPTION_NON_VOLATILE,
          KEY_ALL_ACCESS, NULL, &child, &disposition) == ERROR_SUCCESS,
          "create relative child key");
    set_value(child, "x", REG_SZ, "child", 6);
    RegCloseKey(child);
    size = sizeof(buffer);
    CHECK(get_value(key, L"Child", L"x", RT_ANY, &type, buffer, &size) ==
          ERROR_SUCCESS && same(buffer, L"child", size),
          "relative subkey resolution");
    size = sizeof(buffer);
    CHECK(get_value(key, L"Child", L"x", RT_ANY | VIEW32,
                    &type, buffer, &size) == ERROR_SUCCESS,
          "32-bit selector uses sole Win98 registry view");
    size = sizeof(buffer);
    CHECK(get_value(key, L"Child", L"x", RT_ANY | VIEW64,
                    &type, buffer, &size) == ERROR_SUCCESS,
          "64-bit selector uses sole Win98 registry view");
    CHECK(get_value(key, L"Child", L"x", RT_ANY | VIEW32 | VIEW64,
                    NULL, NULL, NULL) == ERROR_INVALID_PARAMETER,
          "conflicting WOW64 selectors");
    CHECK(get_value_a(key, "Child", "x", RT_ANY | VIEW32 | VIEW64,
                      NULL, NULL, NULL) == ERROR_INVALID_PARAMETER,
          "ANSI conflicting WOW64 selectors");
    fill(buffer, sizeof(buffer), 0x7a);
    size = 4;
    CHECK(get_value(key, NULL, L"missing", RT_ANY | ZEROFAIL,
                    NULL, buffer, &size) == ERROR_FILE_NOT_FOUND &&
          buffer[0] == 0 && buffer[3] == 0 && buffer[4] == 0x7a,
          "missing value and failure zeroing");

    readers.get_value = get_value;
    readers.key = key;
    readers.failed = 0;
    for (i = 0; i < 4; ++i) {
        threads[i] = CreateThread(NULL, 0, concurrent_reader, &readers, 0,
                                  NULL);
        CHECK(threads[i] != NULL, "create concurrent reader");
    }
    for (i = 0; i < 4; ++i) {
        CHECK(WaitForSingleObject(threads[i], 30000) == WAIT_OBJECT_0,
              "join concurrent reader");
        CloseHandle(threads[i]);
    }
    CHECK(readers.failed == 0, "four concurrent registry readers");

    RegDeleteKeyA(key, "Child");
    RegCloseKey(key);
    RegDeleteKeyA(HKEY_CURRENT_USER,
                  "Software\\Win98Modern-RegGetValue-Smoke");
    SetEnvironmentVariableA("M98_REG_TEST", NULL);
    say("PASS: RegGetValueA/W direct API-table registry behavior\r\n");
    ExitProcess(0);
}
