/* Run directly on the installed Win98 SE guest before linking the bridge.
 * It probes active KERNEL32 W behavior, which KernelEx may route to kexbases. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

static void say(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, 0);
}

static void fail(const char *message)
{
    say("FAIL: "); say(message); say("\r\n"); ExitProcess(1);
}

static void number(const char *label, DWORD value)
{
    char digits[11];
    int count = 0;
    say(label);
    do { digits[count++] = (char)('0' + value % 10); value /= 10; }
    while (value && count < 10);
    while (count) {
        char one[2];
        one[0] = digits[--count]; one[1] = 0;
        say(one);
    }
    say("\r\n");
}

static void hex_tail(const BYTE *key, int length)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;
    say("sort_key_tail=");
    for (index = length > 8 ? length - 8 : 0; index < length; ++index) {
        char pair[4];
        pair[0] = digits[key[index] >> 4];
        pair[1] = digits[key[index] & 15];
        pair[2] = ' '; pair[3] = 0;
        say(pair);
    }
    say("\r\n");
}

void mainCRTStartup(void)
{
    WCHAR output[16];
    BYTE key[128];
    int length, written;

    if (CompareStringW(0x0409, 0, L"abc", -1, L"abd", -1) !=
        CSTR_LESS_THAN)
        fail("Win98 CompareStringW en-US ordering");
    if (CompareStringW(0x0409, NORM_IGNORECASE, L"Abc", -1,
                       L"aBC", -1) != CSTR_EQUAL)
        fail("Win98 CompareStringW ignore case");
    if (LCMapStringW(0x0409, LCMAP_UPPERCASE, L"Abc", -1,
                     output, 16) != 4 ||
        output[0] != L'A' || output[1] != L'B' ||
        output[2] != L'C' || output[3])
        fail("Win98 LCMapStringW uppercase and NUL count");
    if (LCMapStringW(0x0412, LCMAP_UPPERCASE, L"\xD55C\xAE00", -1,
                     output, 16) != 3 ||
        output[0] != 0xD55C || output[1] != 0xAE00 || output[2])
        fail("Win98 LCMapStringW Korean Unicode round trip");
    length = LCMapStringW(0x0409, LCMAP_SORTKEY, L"abc", -1, 0, 0);
    if (length < 2 || length > (int)sizeof(key))
        fail("Win98 sort key byte size");
    written = LCMapStringW(0x0409, LCMAP_SORTKEY, L"abc", -1,
                           (WCHAR *)key, length);
    number("sort_key_query=", (DWORD)length);
    number("sort_key_written=", (DWORD)written);
    number("sort_key_last_error=", GetLastError());
    hex_tail(key, written > 0 && written <= (int)sizeof(key) ? written : 0);
    if (written <= 0 || written > length || key[written - 1] != 0)
        fail("Win98 sort key byte payload");
    if (written + 1 == length)
        say("OBSERVED: sort key size query exceeds bytes written by one\r\n");
    else if (written != length)
        fail("unexpected sort key query/payload difference");
    say("PASS: active Win98 KERNEL32 Unicode NLS baseline\r\n");
    ExitProcess(0);
}
