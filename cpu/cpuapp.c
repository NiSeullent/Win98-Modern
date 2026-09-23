/*
 * Win98 CPU profile launcher: declaration and child environment only.
 * No CRT, CPUID hook, ISA emulator, speed limiter or SMP support.
 * The entry point and all imported functions are from KERNEL32.DLL.
 */
#include <windows.h>

#define COMMAND_CAPACITY 32768UL

static STARTUPINFOA startup;
static PROCESS_INFORMATION process;

static DWORD text_length(const char *value)
{
    DWORD length;
    length = 0;
    while (value[length] != '\0')
        ++length;
    return length;
}

static int append_char(char *buffer, DWORD capacity, DWORD *length, char value)
{
    if (*length + 1 >= capacity)
        return 0;
    buffer[*length] = value;
    ++*length;
    buffer[*length] = '\0';
    return 1;
}

static int append_text(char *buffer, DWORD capacity, DWORD *length,
                       const char *value)
{
    while (*value != '\0') {
        if (!append_char(buffer, capacity, length, *value))
            return 0;
        ++value;
    }
    return 1;
}

static int copy_text(char *buffer, DWORD capacity, const char *value)
{
    DWORD length;
    if (capacity == 0)
        return 0;
    buffer[0] = '\0';
    length = 0;
    return append_text(buffer, capacity, &length, value);
}

static char lower_ascii(char value)
{
    if (value >= 'A' && value <= 'Z')
        return (char)(value - 'A' + 'a');
    return value;
}

static int equal_ascii_no_case(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (lower_ascii(*left) != lower_ascii(*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == *right;
}

static int known_profile(const char *value)
{
    static const char *const profiles[] = {
        "i386", "i486", "pentium_mmx", "pentium_ii", "pentium_iii", "modern"
    };
    DWORD index;
    for (index = 0; index < sizeof(profiles) / sizeof(profiles[0]); ++index) {
        if (equal_ascii_no_case(value, profiles[index]))
            return 1;
    }
    return 0;
}

static void write_error(const char *message)
{
    HANDLE output;
    DWORD written;
    output = GetStdHandle(STD_ERROR_HANDLE);
    if (output != INVALID_HANDLE_VALUE && output != NULL)
        WriteFile(output, message, text_length(message), &written, NULL);
}

static int is_space(char value)
{
    return value == ' ' || value == '\t';
}

static const char *skip_space(const char *cursor)
{
    while (is_space(*cursor))
        ++cursor;
    return cursor;
}

/* The executable's first command-line token is not used as a path. */
static const char *skip_launcher_token(const char *cursor)
{
    cursor = skip_space(cursor);
    if (*cursor == '"') {
        ++cursor;
        while (*cursor != '\0' && *cursor != '"')
            ++cursor;
        if (*cursor == '"')
            ++cursor;
    } else {
        while (*cursor != '\0' && !is_space(*cursor))
            ++cursor;
    }
    return cursor;
}

/* Executable file names cannot contain a quote; child arguments stay raw. */
static int parse_target(const char *cursor, char *target, DWORD capacity,
                        const char **remaining)
{
    DWORD length;
    int quoted;
    cursor = skip_space(cursor);
    if (*cursor == '\0' || capacity == 0)
        return 0;
    quoted = *cursor == '"';
    if (quoted)
        ++cursor;
    target[0] = '\0';
    length = 0;
    while (*cursor != '\0' &&
           (quoted ? *cursor != '"' : !is_space(*cursor))) {
        if (!append_char(target, capacity, &length, *cursor))
            return 0;
        ++cursor;
    }
    if (length == 0 || (quoted && *cursor != '"'))
        return 0;
    if (quoted)
        ++cursor;
    if (*cursor != '\0' && !is_space(*cursor))
        return 0;
    *remaining = cursor;
    return 1;
}

static DWORD run_launcher(void)
{
    char ini[MAX_PATH];
    char input_target[MAX_PATH];
    char full_target[MAX_PATH];
    char app_section[256];
    char profile_section[96];
    char profile[64];
    char isa[96];
    char speed[32];
    char *last_slash;
    char *basename;
    char *command;
    const char *argument_tail;
    DWORD size;
    DWORD attributes;
    DWORD length;
    DWORD exit_code;
    DWORD wait_result;

    if (!parse_target(skip_launcher_token(GetCommandLineA()),
                      input_target, MAX_PATH, &argument_tail)) {
        write_error("Usage: CPUAPP.EXE <application.exe> [arguments...]\r\n");
        return 2;
    }

    size = GetModuleFileNameA(NULL, ini, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        write_error("Cannot locate CPUAPP.EXE.\r\n");
        return 2;
    }
    last_slash = ini;
    basename = ini;
    while (*basename != '\0') {
        if (*basename == '\\' || *basename == '/')
            last_slash = basename;
        ++basename;
    }
    if (last_slash == ini ||
        !copy_text(last_slash + 1,
                   MAX_PATH - (DWORD)(last_slash + 1 - ini),
                   "app-profiles.ini")) {
        write_error("Cannot locate app-profiles.ini.\r\n");
        return 2;
    }
    attributes = GetFileAttributesA(ini);
    if (attributes == 0xFFFFFFFFUL || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        write_error("app-profiles.ini is missing beside CPUAPP.EXE.\r\n");
        return 2;
    }

    size = GetFullPathNameA(input_target, MAX_PATH, full_target, NULL);
    if (size == 0 || size >= MAX_PATH) {
        write_error("Application path is invalid or too long.\r\n");
        return 2;
    }
    attributes = GetFileAttributesA(full_target);
    if (attributes == 0xFFFFFFFFUL || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        write_error("Application file does not exist.\r\n");
        return 2;
    }
    basename = full_target;
    last_slash = full_target;
    while (*last_slash != '\0') {
        if (*last_slash == '\\' || *last_slash == '/')
            basename = last_slash + 1;
        ++last_slash;
    }
    app_section[0] = '\0';
    length = 0;
    if (!append_text(app_section, sizeof(app_section), &length, "app.") ||
        !append_text(app_section, sizeof(app_section), &length, basename)) {
        write_error("Application filename is too long for profile lookup.\r\n");
        return 2;
    }
    GetPrivateProfileStringA(app_section, "profile", "", profile,
                             sizeof(profile), ini);
    if (!known_profile(profile)) {
        write_error("No supported app profile in app-profiles.ini.\r\n");
        return 2;
    }
    profile_section[0] = '\0';
    length = 0;
    if (!append_text(profile_section, sizeof(profile_section), &length,
                     "profile.") ||
        !append_text(profile_section, sizeof(profile_section), &length,
                     profile)) {
        write_error("Profile name is too long.\r\n");
        return 2;
    }
    GetPrivateProfileStringA(profile_section, "isa_hint", "", isa,
                             sizeof(isa), ini);
    GetPrivateProfileStringA(profile_section, "speed_hint_mhz", "", speed,
                             sizeof(speed), ini);
    if (isa[0] == '\0' || speed[0] == '\0') {
        write_error("Selected profile has incomplete metadata.\r\n");
        return 2;
    }
    if (!SetEnvironmentVariableA("W98MOD_CPU_PROFILE", profile) ||
        !SetEnvironmentVariableA("W98MOD_CPU_ISA_HINT", isa) ||
        !SetEnvironmentVariableA("W98MOD_CPU_SPEED_HINT_MHZ", speed)) {
        write_error("Could not set child profile metadata.\r\n");
        return 2;
    }

    command = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                COMMAND_CAPACITY);
    if (command == NULL) {
        write_error("Out of memory for child command line.\r\n");
        return 2;
    }
    length = 0;
    if (!append_char(command, COMMAND_CAPACITY, &length, '"') ||
        !append_text(command, COMMAND_CAPACITY, &length, full_target) ||
        !append_char(command, COMMAND_CAPACITY, &length, '"') ||
        !append_text(command, COMMAND_CAPACITY, &length, argument_tail)) {
        HeapFree(GetProcessHeap(), 0, command);
        write_error("Child command line is too long.\r\n");
        return 2;
    }

    write_error("CPU profile: ");
    write_error(profile);
    write_error(" (ISA hint: ");
    write_error(isa);
    write_error(", speed hint: ");
    write_error(speed);
    write_error(" MHz). Metadata only; CPUID, instructions and speed unchanged.\r\n");
    startup.cb = sizeof(startup);
    if (!CreateProcessA(full_target, command, NULL, NULL, FALSE, 0,
                        NULL, NULL, &startup, &process)) {
        HeapFree(GetProcessHeap(), 0, command);
        write_error("Cannot start child application.\r\n");
        return 2;
    }
    HeapFree(GetProcessHeap(), 0, command);
    wait_result = WaitForSingleObject(process.hProcess, INFINITE);
    if (wait_result != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &exit_code)) {
        write_error("Cannot read child application exit code.\r\n");
        exit_code = 2;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
}

void WINAPI cpuapp_entry(void)
{
    ExitProcess(run_launcher());
}
