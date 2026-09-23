/* Direct KernelEx API-table test; imports only original Win98 KERNEL32. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef DWORD (WINAPI *final_path_fn)(HANDLE, WCHAR *, DWORD, DWORD);

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

static void say_hex(DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char output[9];
    int i;
    for (i = 7; i >= 0; --i) {
        output[i] = digits[value & 15];
        value >>= 4;
    }
    output[8] = 0;
    say(output);
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
    final_path_fn final_path = 0;
    HANDLE image_file, other_file;
    char image_name[MAX_PATH], other_name[MAX_PATH];
    WCHAR full[512], root_relative[512];
    BY_HANDLE_FILE_INFORMATION image_info;
    DWORD need, written, root_written;
    int i;

    if (!dll) fail("LoadLibrary m98wrap.dll");
    get_table = (get_api_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !same_ascii(table->target_library, "KERNEL32.DLL"))
        fail("KernelEx KERNEL32 table");
    for (i = 0; i < table->named_apis_count; ++i)
        if (same_ascii(table->named_apis[i].name,
                       "GetFinalPathNameByHandleW"))
            final_path = (final_path_fn)table->named_apis[i].addr;
    if (!final_path) fail("GetFinalPathNameByHandleW pointer");

    if (!GetModuleFileNameA(0, image_name, MAX_PATH))
        fail("locate process image");
    image_file = CreateFileA(image_name, GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (image_file == INVALID_HANDLE_VALUE) fail("open process image");
    if (!GetFileInformationByHandle(image_file, &image_info))
        fail("read process-image file identity");
    say("IMAGE_ID serial=");
    say_hex(image_info.dwVolumeSerialNumber);
    say(" index=");
    say_hex(image_info.nFileIndexHigh);
    say(":");
    say_hex(image_info.nFileIndexLow);
    say(" links=");
    say_hex(image_info.nNumberOfLinks);
    say("\r\n");
    need = final_path(image_file, 0, 0, 0);
    if (need < 8 || need >= 512) fail("path size query");
    written = final_path(image_file, full, need, 0);
    if (written != need - 1 || full[0] != L'\\' || full[1] != L'\\' ||
        full[2] != L'?' || full[3] != L'\\' ||
        full[5] != L':' || full[6] != L'\\' || full[written] != 0)
        fail("verified process-image DOS path");
    root_written = final_path(image_file, root_relative, 512, 4);
    if (!root_written || root_relative[0] != L'\\' ||
        !same_wide(root_relative, full + 6))
        fail("root-relative volume-none path");

    full[0] = L'X';
    if (final_path(image_file, full, need - 1, 0) != need ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || full[0] != L'X')
        fail("short buffer remains untouched");
    if (final_path(image_file, 0, 1, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("null output with nonzero capacity");
    if (final_path(image_file, full, 512, 8) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("opened-name mode fails explicitly");
    if (final_path(image_file, full, 512, 1) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("volume GUID mode fails explicitly");
    if (final_path(image_file, full, 512, 2) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("NT-device mode fails explicitly");
    if (final_path(image_file, full, 512, 3) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("mutually exclusive volume flags");
    if (final_path(image_file, full, 512, 0x10) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("unknown flag");
    if (final_path(INVALID_HANDLE_VALUE, full, 512, 0) ||
        GetLastError() != ERROR_INVALID_HANDLE)
        fail("invalid handle");

    if (!GetModuleFileNameA(dll, other_name, MAX_PATH))
        fail("locate provider image");
    other_file = CreateFileA(other_name, GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (other_file == INVALID_HANDLE_VALUE) fail("open provider image");
    if (final_path(other_file, full, 512, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("unrelated handle must not receive invented path");
    CloseHandle(other_file);
    CloseHandle(image_file);

    say("PASS: GetFinalPathNameByHandleW verified subset\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
