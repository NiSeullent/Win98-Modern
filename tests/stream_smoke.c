/* Direct KernelEx table test of the verified no-stream filesystem subset. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef HANDLE (WINAPI *first_stream_fn)(const WCHAR *, DWORD, void *, DWORD);

typedef struct stream_data {
    LARGE_INTEGER stream_size;
    WCHAR stream_name[MAX_PATH + 36];
} stream_data;

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
    while (*left && *right) {
        char a = *left++, b = *right++;
        if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
        if (a != b) return FALSE;
    }
    return !*left && !*right;
}

static void expect_error(first_stream_fn function, const WCHAR *path,
                         DWORD level, BOOL with_data, DWORD flags,
                         DWORD expected, const char *label)
{
    stream_data data;
    unsigned char *bytes = (unsigned char *)&data;
    HANDLE result;
    DWORD error;
    unsigned int index;
    for (index = 0; index < sizeof(data); ++index) bytes[index] = 0xa5;
    SetLastError(ERROR_SUCCESS);
    result = function(path, level, with_data ? &data : 0, flags);
    error = GetLastError();
    if (result != INVALID_HANDLE_VALUE ||
        (expected ? error != expected :
         error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND))
        fail(label);
    for (index = 0; index < sizeof(data); ++index)
        if (bytes[index] != 0xa5) fail("failure changed stream data");
}

void mainCRTStartup(void)
{
    HMODULE dll = LoadLibraryA("m98wrap.dll");
    get_api_table_fn get_table;
    const m98_api_table *table;
    first_stream_fn first_stream = 0;
    char image_a[MAX_PATH], root[4], filesystem[16], missing_a[64];
    WCHAR image_w[MAX_PATH], missing_w[64], long_name[MAX_PATH + 1];
    DWORD filesystem_flags;
    DWORD expected;
    int i;

    if (!dll) fail("LoadLibrary m98wrap.dll");
    get_table = (get_api_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !same_ascii(table->target_library, "KERNEL32.DLL"))
        fail("KERNEL32 API table");
    for (i = 0; i < table->named_apis_count; ++i)
        if (same_ascii(table->named_apis[i].name, "FindFirstStreamW"))
            first_stream = (first_stream_fn)table->named_apis[i].addr;
    if (!first_stream) fail("FindFirstStreamW pointer");

    /* KERNEL32 resides on the installed system volume even if this probe
     * itself was launched from an optical test disc. */
    if (!GetModuleFileNameA(GetModuleHandleA("KERNEL32.DLL"), image_a,
                            MAX_PATH) ||
        !MultiByteToWideChar(CP_ACP, 0, image_a, -1, image_w, MAX_PATH))
        fail("system KERNEL32 image path");
    if (image_a[1] != ':' || image_a[2] != '\\')
        fail("system KERNEL32 is not on a drive");
    root[0] = image_a[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = 0;
    if (!GetVolumeInformationA(root, 0, 0, 0, 0, &filesystem_flags,
                               filesystem, sizeof(filesystem)))
        fail("volume filesystem query");
    say("FILESYSTEM=");
    say(filesystem);
    say("\r\n");
    expected = (same_ascii(filesystem, "FAT") ||
                same_ascii(filesystem, "FAT32")) &&
               GetDriveTypeA(root) != DRIVE_REMOTE &&
               !(filesystem_flags & 0x00040000UL)
               ? ERROR_INVALID_PARAMETER : ERROR_NOT_SUPPORTED;
    expect_error(first_stream, image_w, 0, TRUE, 0, expected,
                 "existing image filesystem result");
    expect_error(first_stream, image_w, 1, TRUE, 0, ERROR_INVALID_PARAMETER,
                 "unsupported info level");
    expect_error(first_stream, image_w, 0, TRUE, 1, ERROR_INVALID_PARAMETER,
                 "reserved flags");
    expect_error(first_stream, image_w, 0, FALSE, 0, ERROR_INVALID_PARAMETER,
                 "null data buffer");
    expect_error(first_stream, 0, 0, TRUE, 0, ERROR_INVALID_PARAMETER,
                 "null path");
    expect_error(first_stream, L"stream_smoke.exe", 0, TRUE, 0,
                 ERROR_INVALID_NAME, "relative path");
    expect_error(first_stream, L"\\\\server\\share\\file", 0, TRUE, 0,
                 ERROR_NOT_SUPPORTED, "UNC path outside local FAT scope");
    if (GetACP() != 65001)
        expect_error(first_stream, L"C:\\\xd83d\xde80", 0, TRUE, 0,
                     ERROR_NO_UNICODE_TRANSLATION,
                     "unrepresentable Unicode path");

    /* An absolute drive path whose root exists but whose file does not. */
    missing_a[0] = root[0];
    missing_a[1] = ':';
    missing_a[2] = '\\';
    {
        const char *suffix = "M98_STREAM_MISSING_30A1743E.FILE";
        for (i = 0; suffix[i]; ++i) missing_a[i + 3] = suffix[i];
        missing_a[i + 3] = 0;
    }
    if (GetFileAttributesA(missing_a) != INVALID_FILE_ATTRIBUTES)
        fail("missing-path fixture unexpectedly exists");
    if (!MultiByteToWideChar(CP_ACP, 0, missing_a, -1,
                             missing_w, 64)) fail("missing path conversion");
    expect_error(first_stream, missing_w, 0, TRUE, 0, 0,
                 "missing file preserves native error");

    long_name[0] = (WCHAR)root[0];
    long_name[1] = L':';
    long_name[2] = L'\\';
    for (i = 3; i < MAX_PATH; ++i) long_name[i] = L'A';
    long_name[MAX_PATH] = 0;
    expect_error(first_stream, long_name, 0, TRUE, 0,
                 ERROR_FILENAME_EXCED_RANGE, "too-long path");

    say("PASS: FindFirstStreamW verified no-stream subset\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
