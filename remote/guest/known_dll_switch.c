/*
 * Guarded Win98 UXTHEME KnownDLL mapping switch for a disposable test guest.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * `inspect` only reads the mapping. `switch` accepts UXTHEME.DLL as the old
 * value and writes UXTNEW.DLL; `upgrade` accepts only UXTNEW.DLL and writes
 * UXTNEW2.DLL; `downgrade` reverses that; `restore` accepts only UXTNEW.DLL
 * and writes UXTHEME.DLL. No arbitrary registry value or path is accepted. This tool
 * does not replace or remove the original KernelEx UXTHEME.DLL.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define UX_KEY "Software\\KernelEx\\KnownDLLs"
#define UX_VALUE "UXTHEME"
#define ORIGINAL "UXTHEME.DLL"
#define CANDIDATE "UXTNEW.DLL"
#define SECOND "UXTNEW2.DLL"

static unsigned text_length(const char *value)
{
    unsigned length = 0;
    while (value[length]) ++length;
    return length;
}

static char upper_ascii(char value)
{
    if (value >= 'a' && value <= 'z') return (char)(value - 'a' + 'A');
    return value;
}

static BOOL same_name(const char *left, const char *right)
{
    while (*left && *right) {
        if (upper_ascii(*left++) != upper_ascii(*right++)) return FALSE;
    }
    return *left == '\0' && *right == '\0';
}

static void output(const char *message)
{
    DWORD written;
    HANDLE standard = GetStdHandle(STD_OUTPUT_HANDLE);
    if (standard != INVALID_HANDLE_VALUE && standard != NULL)
        WriteFile(standard, message, text_length(message), &written, NULL);
}

static void output_error(const char *tag, DWORD code)
{
    char decimal[16];
    unsigned offset = sizeof(decimal) - 1;
    decimal[offset] = '\0';
    do {
        decimal[--offset] = (char)('0' + code % 10u);
        code /= 10u;
    } while (code && offset);
    output(tag);
    output(decimal + offset);
    output("\r\n");
}

/* GetCommandLineA includes the executable, possibly quoted. Accept one
 * hard-coded action only; this avoids a CRT command-line parser dependency. */
static const char *action(void)
{
    const char *cursor = GetCommandLineA();
    BOOL quoted = FALSE;
    while (*cursor) {
        if (*cursor == '"') quoted = !quoted;
        else if (!quoted && (*cursor == ' ' || *cursor == '\t')) break;
        ++cursor;
    }
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    return cursor;
}

static BOOL command_is(const char *command, const char *expected)
{
    while (*expected) {
        if (upper_ascii(*command++) != upper_ascii(*expected++)) return FALSE;
    }
    while (*command == ' ' || *command == '\t') ++command;
    return *command == '\0';
}

static LONG read_mapping(HKEY key, char *value, DWORD capacity)
{
    DWORD type = 0;
    DWORD length = capacity;
    LONG result = RegQueryValueExA(key, UX_VALUE, NULL, &type,
                                   (BYTE *)value, &length);
    if (result != ERROR_SUCCESS) return result;
    if (type != REG_SZ || length == 0 || length > capacity ||
        value[length - 1] != '\0') return ERROR_INVALID_DATA;
    if (text_length(value) + 1u != length) return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
}

static LONG write_mapping(HKEY key, const char *target)
{
    LONG result = RegSetValueExA(key, UX_VALUE, 0, REG_SZ,
                                 (const BYTE *)target, text_length(target) + 1u);
    if (result != ERROR_SUCCESS) return result;
    return RegFlushKey(key);
}

void __cdecl mainCRTStartup(void)
{
    const char *mode = action();
    const char *expected;
    const char *target;
    BOOL inspect = command_is(mode, "inspect");
    BOOL switch_to_new = command_is(mode, "switch");
    BOOL restore = command_is(mode, "restore");
    BOOL upgrade = command_is(mode, "upgrade");
    BOOL downgrade = command_is(mode, "downgrade");
    HKEY key = NULL;
    char old_value[64];
    char readback[64];
    LONG result;

    if (!inspect && !switch_to_new && !restore && !upgrade && !downgrade) {
        output("USAGE: known_dll_switch.exe inspect|switch|restore|upgrade|downgrade\r\n");
        ExitProcess(2u);
    }
    expected = switch_to_new ? ORIGINAL : upgrade ? CANDIDATE :
               downgrade ? SECOND : CANDIDATE;
    target = switch_to_new ? CANDIDATE : upgrade ? SECOND :
             downgrade ? CANDIDATE : ORIGINAL;
    result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, UX_KEY, 0,
                           KEY_QUERY_VALUE | (inspect ? 0 : KEY_SET_VALUE), &key);
    if (result != ERROR_SUCCESS) {
        output_error("OPEN_ERROR=", (DWORD)result);
        ExitProcess(3u);
    }
    result = read_mapping(key, old_value, sizeof(old_value));
    if (result != ERROR_SUCCESS) {
        output_error("READ_ERROR=", (DWORD)result);
        RegCloseKey(key);
        ExitProcess(3u);
    }
    output("VALUE=");
    output(old_value);
    output("\r\n");
    if (inspect) {
        RegCloseKey(key);
        ExitProcess(0u);
    }
    if (!same_name(old_value, expected)) {
        output("GUARD_REJECTED: unexpected old value\r\n");
        RegCloseKey(key);
        ExitProcess(4u);
    }
    result = write_mapping(key, target);
    if (result != ERROR_SUCCESS) {
        output_error("WRITE_ERROR=", (DWORD)result);
        /* The registry write may have succeeded before a flush error. */
        result = write_mapping(key, old_value);
        output_error("ROLLBACK_RESULT=", (DWORD)result);
        if (read_mapping(key, readback, sizeof(readback)) == ERROR_SUCCESS) {
            output("ROLLBACK_VALUE=");
            output(readback);
            output("\r\n");
        }
        RegCloseKey(key);
        ExitProcess(5u);
    }
    result = read_mapping(key, readback, sizeof(readback));
    if (result != ERROR_SUCCESS || !same_name(readback, target)) {
        if (result != ERROR_SUCCESS) output_error("READBACK_ERROR=", (DWORD)result);
        else {
            output("READBACK_MISMATCH=");
            output(readback);
            output("\r\n");
        }
        result = write_mapping(key, old_value);
        output_error("ROLLBACK_RESULT=", (DWORD)result);
        if (read_mapping(key, readback, sizeof(readback)) == ERROR_SUCCESS) {
            output("ROLLBACK_VALUE=");
            output(readback);
            output("\r\n");
        }
        RegCloseKey(key);
        ExitProcess(6u);
    }
    output("NEW_VALUE=");
    output(readback);
    output("\r\n");
    RegCloseKey(key);
    ExitProcess(0u);
}
