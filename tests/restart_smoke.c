/* Direct KernelEx table test of the Win98 no-restart-service result. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

#ifndef RESTART_MAX_CMD_LINE
#define RESTART_MAX_CMD_LINE 1024
#endif

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef HRESULT (WINAPI *get_restart_fn)(HANDLE, WCHAR *, DWORD *, DWORD *);
typedef HRESULT (WINAPI *register_restart_fn)(const WCHAR *, DWORD);
typedef HRESULT (WINAPI *unregister_restart_fn)(void);

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

void mainCRTStartup(void)
{
    HMODULE dll = LoadLibraryA("m98wrap.dll");
    get_api_table_fn get_table;
    const m98_api_table *table;
    get_restart_fn get_restart = 0;
    register_restart_fn register_restart = 0;
    unregister_restart_fn unregister_restart = 0;
    WCHAR command_line[RESTART_MAX_CMD_LINE + 1];
    WCHAR output[8] = { L'X', L'Y', 0 };
    DWORD size = 8, flags = 0x12345678, exit_code, invalid_error;
    int index;

    if (!dll) fail("LoadLibrary m98wrap.dll");
    get_table = (get_api_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !same(table->target_library, "KERNEL32.DLL") ||
        table->named_apis_count < 46) fail("restart KERNEL32 table");
    for (index = 0; index < table->named_apis_count; ++index) {
        const m98_named_api *api = &table->named_apis[index];
        if (same(api->name, "GetApplicationRestartSettings"))
            get_restart = (get_restart_fn)api->addr;
        if (same(api->name, "RegisterApplicationRestart"))
            register_restart = (register_restart_fn)api->addr;
        if (same(api->name, "UnregisterApplicationRestart"))
            unregister_restart = (unregister_restart_fn)api->addr;
    }
    if (!get_restart || !register_restart || !unregister_restart)
        fail("all three restart table entries");

    if (get_restart(GetCurrentProcess(), output, &size, &flags) !=
            HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ||
        output[0] != L'X' || output[1] != L'Y' ||
        size != 8 || flags != 0x12345678)
        fail("valid process has no registration or fabricated output");
    size = 0;
    if (get_restart(GetCurrentProcess(), 0, &size, 0) !=
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        fail("zero-sized query without registration");
    size = 1;
    if (get_restart(GetCurrentProcess(), 0, &size, 0) != E_INVALIDARG ||
        get_restart(GetCurrentProcess(), output, 0, 0) != E_INVALIDARG ||
        get_restart(0, output, &size, 0) != E_INVALIDARG)
        fail("invalid query arguments");
    if (GetExitCodeProcess((HANDLE)0x5badf00d, &exit_code))
        fail("test handle unexpectedly valid");
    invalid_error = GetLastError();
    if (get_restart((HANDLE)0x5badf00d, output, &size, 0) !=
        HRESULT_FROM_WIN32(invalid_error))
        fail("invalid process handle HRESULT");

    if (register_restart(0, 0) != E_FAIL ||
        register_restart(L"", 0) != E_FAIL ||
        register_restart(L"/restart", 0) != E_FAIL ||
        register_restart(L"/restart", 0x0f) != E_FAIL ||
        register_restart(L"/restart", 0x10) != E_INVALIDARG ||
        register_restart(L"/restart", 0x80000000UL) != E_INVALIDARG)
        fail("documented flags and unsupported registration");
    for (index = 0; index < RESTART_MAX_CMD_LINE; ++index)
        command_line[index] = L'x';
    command_line[RESTART_MAX_CMD_LINE - 1] = 0;
    if (register_restart(command_line, 0) != E_FAIL)
        fail("1023-character command line boundary");
    command_line[RESTART_MAX_CMD_LINE - 1] = L'x';
    command_line[RESTART_MAX_CMD_LINE] = 0;
    if (register_restart(command_line, 0) != E_INVALIDARG)
        fail("1024-character command line rejected");

    if (unregister_restart() != S_OK || unregister_restart() != S_OK ||
        get_restart(GetCurrentProcess(), output, &size, &flags) !=
            HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        fail("idempotent no-registration removal");
    say("PASS: application restart Win98 no-service contract\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
