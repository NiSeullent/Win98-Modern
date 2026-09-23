/*
 * Bounded GUI startup diagnostic for the real Windows 98 SE guest.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 * Native KERNEL32/USER32 only; no CRT or KernelEx-dependent imports.
 * A visible window is evidence of a window, never proof that the app works.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

#define MAX_COMMAND 4095u
#define MAX_WINDOWS 32u
#define MAX_OBSERVE_MS 30000u
#define SAMPLE_MS 200u
#define CLOSE_GRACE_MS 2000u
#define KILL_REAP_MS 3000u

typedef struct probe_state {
    DWORD child_pid;
    DWORD began;
    HWND seen[MAX_WINDOWS];
    DWORD seen_count;
    DWORD final_count;
    DWORD close_count;
    BOOL seen_overflow;
    BOOL final_overflow;
    BOOL close_failed;
    DWORD close_error;
    HWND close_error_window;
    int mode;
} PROBE_STATE;

static char command_copy[MAX_COMMAND + 1u];
static char application_path[MAX_PATH];
static BOOL output_ok = TRUE;

static DWORD text_length(const char *value)
{
    DWORD size = 0u;
    while (value[size]) ++size;
    return size;
}

static void clear_bytes(void *value, DWORD size)
{
    BYTE *bytes = (BYTE *)value;
    DWORD index;
    for (index = 0u; index < size; ++index) bytes[index] = 0u;
}

static void output(const char *value, DWORD size)
{
    HANDLE stream = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stream == INVALID_HANDLE_VALUE || !stream) {
        output_ok = FALSE;
        return;
    }
    while (size) {
        DWORD done = 0u;
        if (!WriteFile(stream, value, size, &done, NULL) || !done) {
            output_ok = FALSE;
            return;
        }
        value += done;
        size -= done;
    }
}

static void output_text(const char *value)
{
    output(value, text_length(value));
}

static DWORD append_text(char *line, DWORD offset, DWORD capacity, const char *value)
{
    while (*value && offset + 1u < capacity) line[offset++] = *value++;
    line[offset] = 0;
    return offset;
}

static DWORD append_decimal(char *line, DWORD offset, DWORD capacity, DWORD value)
{
    char digits[10];
    DWORD count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (count && offset + 1u < capacity) line[offset++] = digits[--count];
    line[offset] = 0;
    return offset;
}

static DWORD append_hex(char *line, DWORD offset, DWORD capacity, DWORD value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    offset = append_text(line, offset, capacity, "0x");
    for (shift = 28; shift >= 0 && offset + 1u < capacity; shift -= 4)
        line[offset++] = hex[(value >> shift) & 15u];
    line[offset] = 0;
    return offset;
}

static DWORD append_quoted(char *line, DWORD offset, DWORD capacity,
                           const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    const BYTE *bytes = (const BYTE *)value;
    if (offset + 1u < capacity) line[offset++] = '"';
    while (*bytes && offset + 5u < capacity) {
        BYTE ch = *bytes++;
        if (ch == '"' || ch == '\\') {
            line[offset++] = '\\';
            line[offset++] = (char)ch;
        } else if (ch < 32u || ch == 127u) {
            line[offset++] = '\\';
            line[offset++] = 'x';
            line[offset++] = hex[ch >> 4];
            line[offset++] = hex[ch & 15u];
        } else line[offset++] = (char)ch;
    }
    if (offset + 1u < capacity) line[offset++] = '"';
    line[offset] = 0;
    return offset;
}

static void output_number(const char *label, DWORD value)
{
    char line[128];
    DWORD offset = append_text(line, 0u, sizeof(line), label);
    offset = append_decimal(line, offset, sizeof(line), value);
    offset = append_text(line, offset, sizeof(line), "\r\n");
    output(line, offset);
}

static void output_hexnumber(const char *label, DWORD value)
{
    char line[128];
    DWORD offset = append_text(line, 0u, sizeof(line), label);
    offset = append_hex(line, offset, sizeof(line), value);
    offset = append_text(line, offset, sizeof(line), "\r\n");
    output(line, offset);
}

static BOOL is_space(char value)
{
    return value == ' ' || value == '\t';
}

/* The executable's argv[0] can be quoted; preserve everything after the
 * duration argument, including the target application's own quotes. */
static BOOL parse_command(DWORD *duration, const char **target_command)
{
    const char *cursor = GetCommandLineA();
    const char *path_begin;
    DWORD value = 0u, path_size = 0u, command_size, i;
    if (!cursor) return FALSE;
    while (is_space(*cursor)) ++cursor;
    if (*cursor == '"') {
        ++cursor;
        while (*cursor && *cursor != '"') ++cursor;
        if (*cursor != '"') return FALSE;
        ++cursor;
    } else {
        while (*cursor && !is_space(*cursor)) ++cursor;
    }
    if (!is_space(*cursor)) return FALSE;
    while (is_space(*cursor)) ++cursor;
    if (*cursor < '0' || *cursor > '9') return FALSE;
    while (*cursor >= '0' && *cursor <= '9') {
        DWORD digit = (DWORD)(*cursor++ - '0');
        if (value > (MAX_OBSERVE_MS - digit) / 10u) return FALSE;
        value = value * 10u + digit;
    }
    if (!value || !is_space(*cursor)) return FALSE;
    while (is_space(*cursor)) ++cursor;
    if (!*cursor) return FALSE;
    *target_command = cursor;
    command_size = text_length(cursor);
    if (command_size > MAX_COMMAND) return FALSE;
    if (*cursor == '"') {
        path_begin = ++cursor;
        while (*cursor && *cursor != '"') ++cursor;
        if (*cursor != '"' || (cursor[1] && !is_space(cursor[1]))) return FALSE;
        path_size = (DWORD)(cursor - path_begin);
    } else {
        path_begin = cursor;
        while (*cursor && !is_space(*cursor)) ++cursor;
        path_size = (DWORD)(cursor - path_begin);
    }
    if (path_size < 3u || path_size >= MAX_PATH) return FALSE;
    for (i = 0u; i < path_size; ++i) application_path[i] = path_begin[i];
    application_path[path_size] = 0;
    if (!((application_path[0] >= 'A' && application_path[0] <= 'Z') ||
          (application_path[0] >= 'a' && application_path[0] <= 'z')) ||
        application_path[1] != ':' ||
        (application_path[2] != '\\' && application_path[2] != '/')) return FALSE;
    *duration = value;
    return TRUE;
}

static void print_window(const char *prefix, HWND window, DWORD elapsed)
{
    /* Worst-case escaped class/title text is 4 * (127 + 255) bytes. */
    char class_name[128], title[256], line[2048];
    DWORD offset;
    int class_count, title_count;
    clear_bytes(class_name, sizeof(class_name));
    clear_bytes(title, sizeof(title));
    class_count = GetClassNameA(window, class_name, sizeof(class_name));
    title_count = GetWindowTextA(window, title, sizeof(title));
    offset = append_text(line, 0u, sizeof(line), prefix);
    offset = append_text(line, offset, sizeof(line), " hwnd=");
    offset = append_hex(line, offset, sizeof(line), (DWORD)(UINT_PTR)window);
    offset = append_text(line, offset, sizeof(line), " elapsed_ms=");
    offset = append_decimal(line, offset, sizeof(line), elapsed);
    offset = append_text(line, offset, sizeof(line), " class=");
    offset = append_quoted(line, offset, sizeof(line), class_name);
    offset = append_text(line, offset, sizeof(line), " title=");
    offset = append_quoted(line, offset, sizeof(line), title);
    if (class_count >= (int)sizeof(class_name) - 1 ||
        title_count >= (int)sizeof(title) - 1)
        offset = append_text(line, offset, sizeof(line), " text_may_be_truncated=1");
    offset = append_text(line, offset, sizeof(line), "\r\n");
    output(line, offset);
}

static BOOL CALLBACK visit_window(HWND window, LPARAM parameter)
{
    PROBE_STATE *state = (PROBE_STATE *)parameter;
    DWORD owner = 0u, index;
    GetWindowThreadProcessId(window, &owner);
    if (owner != state->child_pid || !IsWindowVisible(window)) return TRUE;
    if (state->mode == 2) {
        if (!PostMessageA(window, WM_CLOSE, 0u, 0)) {
            state->close_failed = TRUE;
            state->close_error = GetLastError();
            state->close_error_window = window;
        } else ++state->close_count;
        return TRUE;
    }
    if (state->mode == 1) {
        if (state->final_count < MAX_WINDOWS)
            print_window("FINAL_WINDOW", window,
                         (DWORD)(GetTickCount() - state->began));
        else state->final_overflow = TRUE;
        ++state->final_count;
        return TRUE;
    }
    for (index = 0u; index < state->seen_count; ++index)
        if (state->seen[index] == window) return TRUE;
    if (state->seen_count < MAX_WINDOWS) {
        state->seen[state->seen_count++] = window;
        print_window("WINDOW_FIRST_SEEN", window,
                     (DWORD)(GetTickCount() - state->began));
    } else state->seen_overflow = TRUE;
    return TRUE;
}

static BOOL enumerate(PROBE_STATE *state, int mode)
{
    state->mode = mode;
    return EnumWindows(visit_window, (LPARAM)state);
}

void __cdecl mainCRTStartup(void)
{
    DWORD duration, elapsed, wait_result, exit_code = 0u, error;
    const char *target_command;
    STARTUPINFOA startup;
    PROCESS_INFORMATION child;
    PROBE_STATE state;
    DWORD i;

    if (!parse_command(&duration, &target_command)) {
        output_text("USAGE app_probe.exe <1..30000 ms> <absolute target.exe [arguments]>\r\n");
        output_text("QUOTE the target path if it contains spaces. No functionality PASS is inferred.\r\n");
        ExitProcess(1u);
    }
    for (i = 0u; target_command[i]; ++i) command_copy[i] = target_command[i];
    command_copy[i] = 0;
    clear_bytes(&startup, sizeof(startup));
    clear_bytes(&child, sizeof(child));
    clear_bytes(&state, sizeof(state));
    startup.cb = sizeof(startup);
    if (!CreateProcessA(application_path, command_copy, NULL, NULL, FALSE, 0u,
                        NULL, NULL, &startup, &child)) {
        output_number("LAUNCH_FAIL win32_error=", GetLastError());
        ExitProcess(2u);
    }
    state.child_pid = child.dwProcessId;
    state.began = GetTickCount();
    output_number("LAUNCHED child_pid=", state.child_pid);
    output_number("OBSERVATION_LIMIT_MS=", duration);
    for (;;) {
        /* Never sample a new window after the requested deadline. The
         * scheduler may wake us a little late, which OBSERVED_MS records. */
        elapsed = (DWORD)(GetTickCount() - state.began);
        if (elapsed >= duration) break;
        if (!enumerate(&state, 0)) {
            output_number("ENUM_WINDOWS_ERROR win32_error=", GetLastError());
            break;
        }
        wait_result = WaitForSingleObject(child.hProcess, 0u);
        if (wait_result == WAIT_OBJECT_0) break;
        if (wait_result != WAIT_TIMEOUT) {
            output_number("PROCESS_WAIT_ERROR win32_error=", GetLastError());
            break;
        }
        elapsed = (DWORD)(GetTickCount() - state.began);
        if (elapsed >= duration) break;
        Sleep(duration - elapsed > SAMPLE_MS ? SAMPLE_MS : duration - elapsed);
    }
    elapsed = (DWORD)(GetTickCount() - state.began);
    output_number("OBSERVED_MS=", elapsed);
    if (state.seen_overflow) output_text("WINDOW_FIRST_SEEN_LIMIT_REACHED=1\r\n");
    if (!enumerate(&state, 1))
        output_number("FINAL_ENUM_WINDOWS_ERROR win32_error=", GetLastError());
    output_number("FINAL_VISIBLE_WINDOW_COUNT=", state.final_count);
    if (state.final_overflow) output_text("FINAL_WINDOW_DETAIL_LIMIT_REACHED=1\r\n");

    wait_result = WaitForSingleObject(child.hProcess, 0u);
    if (wait_result == WAIT_OBJECT_0) {
        output_text("ALIVE_AFTER_OBSERVATION=0\r\n");
    } else {
        if (wait_result == WAIT_TIMEOUT)
            output_text("ALIVE_AFTER_OBSERVATION=1\r\n");
        else {
            output_text("ALIVE_AFTER_OBSERVATION=unknown\r\n");
            output_number("FINAL_WAIT_ERROR win32_error=", GetLastError());
        }
        if (!enumerate(&state, 2))
            output_number("CLOSE_ENUM_WINDOWS_ERROR win32_error=", GetLastError());
        output_number("WM_CLOSE_POSTED_COUNT=", state.close_count);
        if (state.close_failed) {
            output_number("WM_CLOSE_POST_ERROR win32_error=", state.close_error);
            output_hexnumber("WM_CLOSE_POST_ERROR hwnd=", (DWORD)(UINT_PTR)state.close_error_window);
        }
        wait_result = WaitForSingleObject(child.hProcess, CLOSE_GRACE_MS);
        if (wait_result != WAIT_OBJECT_0) {
            if (wait_result != WAIT_TIMEOUT)
                output_number("CLOSE_WAIT_ERROR win32_error=", GetLastError());
            if (TerminateProcess(child.hProcess, ERROR_TIMEOUT)) {
                output_text("TERMINATED_BY_PROBE=1\r\n");
                wait_result = WaitForSingleObject(child.hProcess, KILL_REAP_MS);
            } else {
                error = GetLastError();
                output_number("TERMINATE_ERROR win32_error=", error);
                wait_result = WaitForSingleObject(child.hProcess, 0u);
            }
        }
    }

    if (wait_result == WAIT_OBJECT_0) {
        if (GetExitCodeProcess(child.hProcess, &exit_code))
            output_number("TARGET_EXIT_CODE=", exit_code);
        else output_number("EXIT_CODE_ERROR win32_error=", GetLastError());
        output_text("IMMEDIATE_CHILD_REAPED=1\r\n");
    } else {
        output_text("IMMEDIATE_CHILD_REAPED=0\r\n");
        if (wait_result == WAIT_TIMEOUT)
            output_text("REAP_TIMEOUT=1\r\n");
    }
    output_text("DESCENDANTS_NOT_TRACKED=1\r\n");
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    ExitProcess(wait_result == WAIT_OBJECT_0 && output_ok ? 0u : 3u);
}
