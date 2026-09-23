/* Direct KernelEx table test for bounded process image path support. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef BOOL (WINAPI *path_a_fn)(HANDLE, DWORD, char *, DWORD *);
typedef BOOL (WINAPI *path_w_fn)(HANDLE, DWORD, WCHAR *, DWORD *);

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

static BOOL same(const char *left, const char *right)
{
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static void clear_bytes(void *target, DWORD count)
{
    volatile BYTE *bytes = (volatile BYTE *)target;
    DWORD index;
    for (index = 0; index < count; ++index) bytes[index] = 0;
}

void mainCRTStartup(void)
{
    HMODULE dll = LoadLibraryA("m98wrap.dll");
    get_api_table_fn get_table;
    const m98_api_table *table;
    path_a_fn query_a = 0;
    path_w_fn query_w = 0;
    char path_a[MAX_PATH], converted[MAX_PATH], original_a[MAX_PATH];
    char expected_a[MAX_PATH];
    WCHAR path_w[MAX_PATH];
    DWORD count_a = MAX_PATH, count_w = MAX_PATH;
    DWORD size, original_size, dummy;
    HANDLE self;
    STARTUPINFOA start;
    PROCESS_INFORMATION child;
    int i;

    if (!dll) fail("LoadLibrary m98wrap.dll");
    get_table = (get_api_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !same(table->target_library, "KERNEL32.DLL") ||
        table->named_apis_count < 48) fail("process-path KERNEL32 table");
    for (i = 0; i < table->named_apis_count; ++i) {
        const m98_named_api *api = &table->named_apis[i];
        if (same(api->name, "QueryFullProcessImageNameA"))
            query_a = (path_a_fn)api->addr;
        if (same(api->name, "QueryFullProcessImageNameW"))
            query_w = (path_w_fn)api->addr;
    }
    if (!query_a || !query_w) fail("A/W table entries");
    if (!query_a(GetCurrentProcess(), 0, path_a, &count_a) ||
        !query_w(GetCurrentProcess(), 0, path_w, &count_w))
        fail("pseudo-handle A/W path");
    if (!count_a || !count_w || count_a >= MAX_PATH ||
        count_w >= MAX_PATH || path_a[count_a] || path_w[count_w] ||
        !WideCharToMultiByte(CP_ACP, 0, path_w, -1, converted,
                             MAX_PATH, 0, 0) ||
        !same(path_a, converted))
        fail("path content and returned lengths");
    if (!GetModuleFileNameA(0, original_a, MAX_PATH))
        fail("GetModuleFileNameA self");
    original_size = GetLongPathNameA(original_a, expected_a, MAX_PATH);
    if (!original_size || original_size >= MAX_PATH) {
        for (i = 0; original_a[i]; ++i) expected_a[i] = original_a[i];
        expected_a[i] = 0;
    }
    if (lstrcmpiA(path_a, expected_a) != 0)
        fail("current executable identity");

    /* The caller's size and contents stay intact on insufficient capacity. */
    size = count_w;
    path_w[0] = L'X';
    if (query_w(GetCurrentProcess(), 0, path_w, &size) ||
        size != count_w || path_w[0] != L'X' ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("W exact-size boundary");
    size = count_a;
    path_a[0] = 'X';
    if (query_a(GetCurrentProcess(), 0, path_a, &size) ||
        size != count_a || path_a[0] != 'X' ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("A exact-size boundary");
    size = 0;
    if (query_w(GetCurrentProcess(), 0, 0, &size) || size != 0 ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("zero-sized W buffer");
    size = MAX_PATH;
    if (query_w(GetCurrentProcess(), 0, 0, &size) ||
        size != MAX_PATH || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("null W output with capacity");
    if (query_w(GetCurrentProcess(), 0, path_w, 0) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("null W size pointer");
    size = MAX_PATH;
    path_w[0] = L'X';
    if (query_w(GetCurrentProcess(), 2, path_w, &size) ||
        size != MAX_PATH || path_w[0] != L'X' ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("invalid flags");
    size = MAX_PATH;
    if (query_w(GetCurrentProcess(), 1, path_w, &size) ||
        size != MAX_PATH || GetLastError() != ERROR_NOT_SUPPORTED)
        fail("native NT path is unsupported");
    size = MAX_PATH;
    if (query_w((HANDLE)0x5badf00d, 0, path_w, &size) ||
        GetLastError() != ERROR_INVALID_HANDLE)
        fail("invalid process handle");

    self = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE,
                       GetCurrentProcessId());
    if (!self) fail("OpenProcess self");
    original_size = MAX_PATH;
    if (!query_w(self, 0, path_w, &original_size)) {
        /* A stock Win98 KERNEL32 without KernelEx GetProcessId cannot
         * prove a real handle belongs to this process. */
        if (GetLastError() != ERROR_NOT_SUPPORTED)
            fail("real self handle error");
        say("INFO: real self handle requires KernelEx GetProcessId\r\n");
    } else if (original_size != count_w ||
               !WideCharToMultiByte(CP_ACP, 0, path_w, -1, converted,
                                    MAX_PATH, 0, 0) ||
               lstrcmpiA(converted, expected_a) != 0) {
        fail("real self handle path");
    }
    CloseHandle(self);

    /* A different process running this same image must not receive our
     * path merely because GetModuleFileNameA(0) returns one. */
    clear_bytes(&start, sizeof(start));
    clear_bytes(&child, sizeof(child));
    start.cb = sizeof(start);
    if (!CreateProcessA(original_a, 0, 0, 0, FALSE, CREATE_SUSPENDED,
                        0, 0, &start, &child))
        fail("CreateProcess suspended child");
    size = MAX_PATH;
    dummy = query_w(child.hProcess, 0, path_w, &size);
    original_size = GetLastError();
    TerminateProcess(child.hProcess, 0);
    WaitForSingleObject(child.hProcess, 5000);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    if (dummy || original_size != ERROR_NOT_SUPPORTED)
        fail("other process rejected");

    say("PASS: bounded process image path A/W contract\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
