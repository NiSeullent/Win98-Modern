/*
 * Visible Windows 98 SE COM1 test agent; wire contract: remote/PROTOCOL.md.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 * Only native Windows 98 KERNEL32 entry points may be imported.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define FRAME_HEADER_SIZE 20u
#define MAX_PAYLOAD 131072u
#define MAX_CHUNK 65536u
#define MAX_PATH_BYTES 259u
#define MAX_COMMAND_BYTES 4095u
#define MAX_EXEC_MS 300000u
#define FRAME_DEADLINE_MS 30000u
#define SERIAL_READ_SLICE_MS 1000u
#define SERIAL_WRITE_SLICE_MS 5000u
#define OP_PING 1u
#define OP_EXEC 2u
#define OP_GET 3u
#define OP_PUT 4u
#define OP_PROMOTE 5u

typedef struct frame_clock {
    DWORD began;
    BOOL active;
} FRAME_CLOCK;

static BYTE request_data[MAX_PAYLOAD];
static BYTE reply_data[MAX_PAYLOAD];
static char agent_directory[MAX_PATH];
static char exec_command[MAX_COMMAND_BYTES + 1u];

static void clear_bytes(void *destination, DWORD count)
{
    BYTE *bytes = (BYTE *)destination;
    DWORD i;
    for (i = 0; i < count; ++i) bytes[i] = 0;
}

static DWORD text_length(const char *text)
{
    DWORD length = 0;
    while (text[length]) ++length;
    return length;
}

static DWORD load_u32(const BYTE *data)
{
    return (DWORD)data[0] | ((DWORD)data[1] << 8) |
           ((DWORD)data[2] << 16) | ((DWORD)data[3] << 24);
}

static void store_u32(BYTE *data, DWORD value)
{
    data[0] = (BYTE)value;
    data[1] = (BYTE)(value >> 8);
    data[2] = (BYTE)(value >> 16);
    data[3] = (BYTE)(value >> 24);
}

static DWORD append_text(BYTE *destination, DWORD position, const char *value)
{
    DWORD i = 0;
    while (value[i]) destination[position++] = (BYTE)value[i++];
    return position;
}

static DWORD append_decimal(BYTE *destination, DWORD position, DWORD value)
{
    BYTE reverse[10];
    DWORD count = 0;
    do {
        reverse[count++] = (BYTE)('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (count) destination[position++] = reverse[--count];
    return position;
}

static void console_text(const char *message)
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD count;
    if (output != INVALID_HANDLE_VALUE && output != NULL)
        WriteFile(output, message, text_length(message), &count, NULL);
}

static void console_error(const char *prefix, DWORD error)
{
    BYTE line[96];
    DWORD position = append_text(line, 0u, prefix);
    DWORD written;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    position = append_decimal(line, position, error);
    line[position++] = '\r';
    line[position++] = '\n';
    if (output != INVALID_HANDLE_VALUE && output != NULL)
        WriteFile(output, line, position, &written, NULL);
}

/* DWORD subtraction keeps a short deadline valid across GetTickCount wrap. */
static BOOL frame_expired(DWORD began)
{
    return (DWORD)(GetTickCount() - began) >= FRAME_DEADLINE_MS;
}

/* Idle is unlimited only until the first header byte of each frame arrives. */
static BOOL read_exact(HANDLE channel, BYTE *data, DWORD size,
                       FRAME_CLOCK *clock, BOOL idle_ok, DWORD *io_error)
{
    while (size) {
        DWORD done = 0;
        DWORD wanted = size > 4096u ? 4096u : size;
        if (clock->active && frame_expired(clock->began)) {
            *io_error = ERROR_TIMEOUT;
            return FALSE;
        }
        if (!ReadFile(channel, data, wanted, &done, NULL)) {
            *io_error = GetLastError();
            return FALSE;
        }
        if (done == 0u) {
            if (!clock->active && !idle_ok) {
                *io_error = ERROR_TIMEOUT;
                return FALSE;
            }
            Sleep(1u);
            continue;
        }
        if (!clock->active) {
            clock->began = GetTickCount();
            clock->active = TRUE;
        }
        data += done;
        size -= done;
        if (frame_expired(clock->began)) {
            *io_error = ERROR_TIMEOUT;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL write_exact(HANDLE channel, const BYTE *data, DWORD size,
                        DWORD began, DWORD *io_error)
{
    while (size) {
        DWORD done = 0;
        DWORD wanted = size > 4096u ? 4096u : size;
        if (frame_expired(began)) {
            *io_error = ERROR_TIMEOUT;
            return FALSE;
        }
        if (!WriteFile(channel, data, wanted, &done, NULL)) {
            *io_error = GetLastError();
            return FALSE;
        }
        if (done == 0u) {
            Sleep(1u);
            continue;
        }
        data += done;
        size -= done;
        if (frame_expired(began)) {
            *io_error = ERROR_TIMEOUT;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL send_reply(HANDLE channel, DWORD opcode, DWORD request_id,
                       DWORD payload_size, DWORD *io_error)
{
    BYTE header[FRAME_HEADER_SIZE];
    DWORD began = GetTickCount();
    header[0] = 'M';
    header[1] = '9';
    header[2] = '8';
    header[3] = 'R';
    store_u32(header + 4, 1u);
    store_u32(header + 8, opcode | 0x80000000u);
    store_u32(header + 12, request_id);
    store_u32(header + 16, payload_size);
    return write_exact(channel, header, FRAME_HEADER_SIZE, began, io_error) &&
           write_exact(channel, reply_data, payload_size, began, io_error);
}

static BOOL has_nul(const BYTE *bytes, DWORD count)
{
    DWORD i;
    for (i = 0; i < count; ++i)
        if (!bytes[i]) return TRUE;
    return FALSE;
}

static BOOL transfer_path(const BYTE *bytes, DWORD count, char path[MAX_PATH])
{
    DWORD i;
    if (count < 3u || count > MAX_PATH_BYTES || has_nul(bytes, count))
        return FALSE;
    if (!((bytes[0] >= 'A' && bytes[0] <= 'Z') ||
          (bytes[0] >= 'a' && bytes[0] <= 'z')) ||
        bytes[1] != ':' || (bytes[2] != '\\' && bytes[2] != '/'))
        return FALSE;
    for (i = 0; i < count; ++i) path[i] = (char)bytes[i];
    path[count] = 0;
    return TRUE;
}

static DWORD file_size32(HANDLE file, DWORD *size)
{
    DWORD high = 0;
    DWORD low;
    SetLastError(NO_ERROR);
    low = GetFileSize(file, &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
        return GetLastError();
    if (high != 0u) return ERROR_FILE_TOO_LARGE;
    *size = low;
    return NO_ERROR;
}

static DWORD seek_to(HANDLE file, DWORD offset)
{
    LONG high = 0;
    DWORD low;
    SetLastError(NO_ERROR);
    low = SetFilePointer(file, (LONG)offset, &high, FILE_BEGIN);
    if (low == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
        return GetLastError();
    if (high != 0 || low != offset) return ERROR_SEEK;
    return NO_ERROR;
}

static DWORD read_file_exact(HANDLE file, BYTE *bytes, DWORD size)
{
    while (size) {
        DWORD done = 0;
        DWORD wanted = size > 4096u ? 4096u : size;
        if (!ReadFile(file, bytes, wanted, &done, NULL)) return GetLastError();
        if (done == 0u) return ERROR_HANDLE_EOF;
        bytes += done;
        size -= done;
    }
    return NO_ERROR;
}

static DWORD write_file_exact(HANDLE file, const BYTE *bytes, DWORD size)
{
    while (size) {
        DWORD done = 0;
        DWORD wanted = size > 4096u ? 4096u : size;
        if (!WriteFile(file, bytes, wanted, &done, NULL)) return GetLastError();
        if (done == 0u) return ERROR_WRITE_FAULT;
        bytes += done;
        size -= done;
    }
    return NO_ERROR;
}

static DWORD handle_ping(DWORD size, DWORD *reply_size)
{
    OSVERSIONINFOA version;
    DWORD position = 4u;
    if (size != 0u) return ERROR_INVALID_PARAMETER;
    clear_bytes(&version, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    if (!GetVersionExA(&version)) return GetLastError();
    position = append_text(reply_data, position, "m98agent/v1 protocol=1 os=");
    position = append_decimal(reply_data, position, version.dwMajorVersion);
    reply_data[position++] = '.';
    position = append_decimal(reply_data, position, version.dwMinorVersion);
    position = append_text(reply_data, position, " build=");
    position = append_decimal(reply_data, position, version.dwBuildNumber);
    *reply_size = position;
    return NO_ERROR;
}

static DWORD handle_get(const BYTE *payload, DWORD size, DWORD *reply_size)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    char path[MAX_PATH];
    DWORD offset, requested, total, count, error;
    if (size < 11u) return ERROR_INVALID_PARAMETER;
    offset = load_u32(payload);
    requested = load_u32(payload + 4);
    if (requested < 1u || requested > MAX_CHUNK ||
        !transfer_path(payload + 8, size - 8u, path))
        return ERROR_INVALID_PARAMETER;
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    error = file_size32(file, &total);
    if (error) goto done;
    if (offset > total) { error = ERROR_INVALID_PARAMETER; goto done; }
    count = total - offset;
    if (count > requested) count = requested;
    error = seek_to(file, offset);
    if (error) goto done;
    error = read_file_exact(file, reply_data + 8, count);
    if (error) goto done;
    store_u32(reply_data + 4, total);
    *reply_size = 8u + count;
done:
    CloseHandle(file);
    return error;
}

static DWORD handle_put(const BYTE *payload, DWORD size, DWORD *reply_size)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    char path[MAX_PATH];
    DWORD offset, flags, path_size, chunk_size, total, error;
    if (size < 15u) return ERROR_INVALID_PARAMETER;
    offset = load_u32(payload);
    flags = load_u32(payload + 4);
    path_size = load_u32(payload + 8);
    if (path_size > MAX_PATH_BYTES || path_size > size - 12u ||
        !transfer_path(payload + 12, path_size, path))
        return ERROR_INVALID_PARAMETER;
    chunk_size = size - 12u - path_size;
    if (chunk_size > MAX_CHUNK || flags > 2u ||
        (flags != 0u && offset != 0u))
        return ERROR_INVALID_PARAMETER;
    if (chunk_size > 0xffffffffu - offset) return ERROR_FILE_TOO_LARGE;
    file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                       flags == 1u ? CREATE_ALWAYS :
                       flags == 2u ? CREATE_NEW : OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    if (!flags) {
        error = file_size32(file, &total);
        if (error) goto done;
        if (total != offset) { error = ERROR_INVALID_PARAMETER; goto done; }
    }
    error = seek_to(file, offset);
    if (error) goto done;
    error = write_file_exact(file, payload + 12u + path_size, chunk_size);
    if (error) goto done;
    store_u32(reply_data + 4, chunk_size);
    *reply_size = 8u;
done:
    if (!CloseHandle(file) && !error) error = GetLastError();
    return error;
}

/* Normalize drive paths before comparing PROMOTE identities and parents. */
static DWORD promote_path(const BYTE *bytes, DWORD count, char path[MAX_PATH])
{
    char raw[MAX_PATH];
    DWORD length, i;
    if (!transfer_path(bytes, count, raw)) return ERROR_INVALID_PARAMETER;
    length = GetFullPathNameA(raw, MAX_PATH, path, NULL);
    if (length == 0u) return GetLastError();
    if (length > MAX_PATH_BYTES || length >= MAX_PATH)
        return ERROR_BUFFER_OVERFLOW;
    for (i = 0u; i < length; ++i)
        if (path[i] == '/') path[i] = '\\';
    if (length < 4u || path[length - 1u] == '\\')
        return ERROR_INVALID_PARAMETER;
    return NO_ERROR;
}

static BOOL parent_directory(const char path[MAX_PATH], char parent[MAX_PATH])
{
    DWORD length = text_length(path);
    DWORD i, j, parent_length;
    for (i = length; i > 2u; --i) {
        if (path[i - 1u] == '\\') {
            /* Keep C:\ intact; omit a non-root trailing slash for attributes. */
            parent_length = i == 3u ? i : i - 1u;
            for (j = 0u; j < parent_length; ++j) parent[j] = path[j];
            parent[parent_length] = 0;
            return i < length;
        }
    }
    return FALSE;
}

static DWORD handle_promote(const BYTE *payload, DWORD size)
{
    char source[MAX_PATH], destination[MAX_PATH], backup[MAX_PATH];
    char parent[MAX_PATH], other_parent[MAX_PATH];
    HANDLE source_file;
    DWORD source_length, destination_length, backup_length;
    DWORD attributes, error, destination_attributes;
    BOOL had_destination;
    if (size < 12u) return ERROR_INVALID_PARAMETER;
    source_length = load_u32(payload);
    destination_length = load_u32(payload + 4);
    backup_length = load_u32(payload + 8);
    if (source_length < 3u || source_length > MAX_PATH_BYTES ||
        destination_length < 3u || destination_length > MAX_PATH_BYTES ||
        backup_length < 3u || backup_length > MAX_PATH_BYTES ||
        source_length + destination_length + backup_length != size - 12u)
        return ERROR_INVALID_PARAMETER;
    error = promote_path(payload + 12u, source_length, source);
    if (error) return error;
    error = promote_path(payload + 12u + source_length,
                         destination_length, destination);
    if (error) return error;
    error = promote_path(payload + 12u + source_length + destination_length,
                         backup_length, backup);
    if (error) return error;
    if (lstrcmpiA(source, destination) == 0 ||
        lstrcmpiA(source, backup) == 0 ||
        lstrcmpiA(destination, backup) == 0 ||
        !parent_directory(source, parent) ||
        !parent_directory(destination, other_parent) ||
        lstrcmpiA(parent, other_parent) != 0 ||
        !parent_directory(backup, other_parent) ||
        lstrcmpiA(parent, other_parent) != 0)
        return ERROR_INVALID_PARAMETER;
    attributes = GetFileAttributesA(parent);
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u)
        return ERROR_PATH_NOT_FOUND;
    attributes = GetFileAttributesA(source);
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) return ERROR_DIRECTORY;
    source_file = CreateFileA(source, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (source_file == INVALID_HANDLE_VALUE) return GetLastError();
    attributes = GetFileType(source_file);
    error = attributes == FILE_TYPE_DISK ? NO_ERROR : ERROR_INVALID_PARAMETER;
    if (!CloseHandle(source_file) && !error) error = GetLastError();
    if (error) return error;
    attributes = GetFileAttributesA(backup);
    if (attributes != INVALID_FILE_ATTRIBUTES) return ERROR_ALREADY_EXISTS;
    error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND) return error;
    destination_attributes = GetFileAttributesA(destination);
    had_destination = destination_attributes != INVALID_FILE_ATTRIBUTES;
    /* Both names are in one directory; MoveFileA refuses an existing target. */
    if (had_destination) {
        if ((destination_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
            return ERROR_DIRECTORY;
        if (!MoveFileA(destination, backup)) return GetLastError();
    } else {
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) return error;
    }
    if (!MoveFileA(source, destination)) {
        error = GetLastError();
        /* A failed rollback leaves the old destination in backup for recovery. */
        if (had_destination && !MoveFileA(backup, destination)) {
            DWORD rollback_error = GetLastError();
            console_text("M98 agent: PROMOTE rollback FAILED; backup remains at ");
            console_text(backup);
            console_text("\r\n");
            console_error("M98 agent: source move Win32 error=", error);
            console_error("M98 agent: rollback Win32 error=", rollback_error);
            return ERROR_GEN_FAILURE;
        }
        return error;
    }
    return NO_ERROR;
}

static DWORD capture_output(const char *path, DWORD *reply_size)
{
    HANDLE file;
    DWORD high = 0, low, count, error;
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    SetLastError(NO_ERROR);
    low = GetFileSize(file, &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        error = GetLastError();
        CloseHandle(file);
        return error;
    }
    count = high != 0u || low > MAX_CHUNK ? MAX_CHUNK : low;
    store_u32(reply_data + 12, high != 0u || low > MAX_CHUNK ? 1u : 0u);
    error = read_file_exact(file, reply_data + 16, count);
    CloseHandle(file);
    if (!error) *reply_size = 16u + count;
    return error;
}

static DWORD handle_exec(const BYTE *payload, DWORD size, DWORD *reply_size)
{
    char directory[MAX_PATH];
    char temp_directory[MAX_PATH], temp_file[MAX_PATH];
    SECURITY_ATTRIBUTES attributes;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
    DWORD timeout, directory_size, command_size, i, wait_result, exit_code;
    DWORD error = NO_ERROR;
    BOOL created = FALSE, temp_created = FALSE;
    if (size < 12u) return ERROR_INVALID_PARAMETER;
    timeout = load_u32(payload);
    directory_size = load_u32(payload + 4);
    command_size = load_u32(payload + 8);
    if (timeout < 1u || timeout > MAX_EXEC_MS ||
        directory_size > MAX_PATH_BYTES ||
        command_size < 1u || command_size > MAX_COMMAND_BYTES ||
        directory_size > size - 12u ||
        command_size != size - 12u - directory_size ||
        has_nul(payload + 12u, directory_size + command_size))
        return ERROR_INVALID_PARAMETER;
    for (i = 0; i < directory_size; ++i)
        directory[i] = (char)payload[12u + i];
    directory[directory_size] = 0;
    for (i = 0; i < command_size; ++i)
        exec_command[i] = (char)payload[12u + directory_size + i];
    exec_command[command_size] = 0;
    if (directory_size == 0u) {
        for (i = 0; agent_directory[i]; ++i)
            directory[i] = agent_directory[i];
        directory[i] = 0;
    }
    i = GetTempPathA(MAX_PATH, temp_directory);
    if (i == 0u) return GetLastError();
    if (i >= MAX_PATH) return ERROR_BUFFER_OVERFLOW;
    if (!GetTempFileNameA(temp_directory, "M98", 0u, temp_file))
        return GetLastError();
    temp_created = TRUE;
    clear_bytes(&attributes, sizeof(attributes));
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    output = CreateFileA(temp_file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (output == INVALID_HANDLE_VALUE) { error = GetLastError(); goto done; }
    input = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE) { error = GetLastError(); goto done; }
    clear_bytes(&startup, sizeof(startup));
    clear_bytes(&process, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    if (!CreateProcessA(NULL, exec_command, NULL, NULL, TRUE, 0u, NULL,
                        directory, &startup, &process)) {
        error = GetLastError();
        goto done;
    }
    created = TRUE;
    CloseHandle(input);
    input = INVALID_HANDLE_VALUE;
    CloseHandle(output);
    output = INVALID_HANDLE_VALUE;
    wait_result = WaitForSingleObject(process.hProcess, timeout);
    if (wait_result == WAIT_TIMEOUT) {
        store_u32(reply_data + 8, 1u);
        if (!TerminateProcess(process.hProcess, ERROR_TIMEOUT)) {
            DWORD kill_error = GetLastError();
            if (WaitForSingleObject(process.hProcess, 0u) != WAIT_OBJECT_0) {
                error = kill_error;
                goto done;
            }
        }
        wait_result = WaitForSingleObject(process.hProcess, 5000u);
    } else {
        store_u32(reply_data + 8, 0u);
    }
    if (wait_result != WAIT_OBJECT_0) {
        error = wait_result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        goto done;
    }
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        error = GetLastError();
        goto done;
    }
    store_u32(reply_data + 4, exit_code);
    error = capture_output(temp_file, reply_size);
done:
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (created) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (temp_created) DeleteFileA(temp_file);
    return error;
}

static DWORD dispatch(DWORD opcode, const BYTE *payload, DWORD size,
                      DWORD *reply_size)
{
    DWORD error;
    *reply_size = 4u;
    switch (opcode) {
    case OP_PING: error = handle_ping(size, reply_size); break;
    case OP_EXEC: error = handle_exec(payload, size, reply_size); break;
    case OP_GET: error = handle_get(payload, size, reply_size); break;
    case OP_PUT: error = handle_put(payload, size, reply_size); break;
    case OP_PROMOTE: error = handle_promote(payload, size); break;
    default: error = ERROR_INVALID_FUNCTION; break;
    }
    store_u32(reply_data, error);
    if (error) *reply_size = 4u;
    return error;
}

static BOOL init_agent_directory(void)
{
    DWORD length = GetModuleFileNameA(NULL, agent_directory, MAX_PATH);
    DWORD i;
    if (length == 0u || length >= MAX_PATH) return FALSE;
    for (i = length; i; --i) {
        if (agent_directory[i - 1u] == '\\' ||
            agent_directory[i - 1u] == '/') {
            agent_directory[i] = 0;
            return TRUE;
        }
    }
    return FALSE;
}

static HANDLE open_serial(DWORD *open_error)
{
    HANDLE channel;
    DCB state;
    COMMTIMEOUTS timeouts;
    channel = CreateFileA("COM1", GENERIC_READ | GENERIC_WRITE, 0,
                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (channel == INVALID_HANDLE_VALUE) {
        *open_error = GetLastError();
        return channel;
    }
    if (!SetupComm(channel, MAX_CHUNK, MAX_CHUNK))
        console_error("M98 agent: SetupComm warning, Win32 error=", GetLastError());
    clear_bytes(&state, sizeof(state));
    state.DCBlength = sizeof(state);
    if (!GetCommState(channel, &state)) goto fail;
    state.BaudRate = CBR_115200;
    state.ByteSize = 8;
    state.Parity = NOPARITY;
    state.StopBits = ONESTOPBIT;
    state.fBinary = TRUE;
    state.fParity = FALSE;
    state.fOutxCtsFlow = FALSE;
    state.fOutxDsrFlow = FALSE;
    state.fDtrControl = DTR_CONTROL_ENABLE;
    state.fDsrSensitivity = FALSE;
    state.fTXContinueOnXoff = TRUE;
    state.fOutX = FALSE;
    state.fInX = FALSE;
    state.fErrorChar = FALSE;
    state.fNull = FALSE;
    state.fRtsControl = RTS_CONTROL_ENABLE;
    state.fAbortOnError = FALSE;
    if (!SetCommState(channel, &state)) goto fail;
    clear_bytes(&timeouts, sizeof(timeouts));
    /* Finite driver waits let the frame deadline handle partial transfers. */
    timeouts.ReadTotalTimeoutConstant = SERIAL_READ_SLICE_MS;
    timeouts.WriteTotalTimeoutConstant = SERIAL_WRITE_SLICE_MS;
    if (!SetCommTimeouts(channel, &timeouts)) goto fail;
    return channel;
fail:
    *open_error = GetLastError();
    CloseHandle(channel);
    return INVALID_HANDLE_VALUE;
}

void __cdecl mainCRTStartup(void)
{
    HANDLE channel;
    BYTE header[FRAME_HEADER_SIZE];
    DWORD opcode, request_id, size, reply_size, open_error = NO_ERROR;
    DWORD io_error;
    FRAME_CLOCK clock;
    if (!init_agent_directory()) {
        console_text("M98 agent: cannot locate executable directory\r\n");
        ExitProcess(1u);
    }
    console_text("M98 remote agent v1: opening COM1 at 115200 8N1\r\n");
    channel = open_serial(&open_error);
    if (channel == INVALID_HANDLE_VALUE) {
        console_error("M98 agent: COM1 open/configure failed, Win32 error=",
                      open_error);
        ExitProcess(2u);
    }
    console_text("M98 agent: ready; waiting for host frames\r\n");
    for (;;) {
        clock.began = 0u;
        clock.active = FALSE;
        io_error = NO_ERROR;
        if (!read_exact(channel, header, FRAME_HEADER_SIZE,
                        &clock, TRUE, &io_error)) {
            console_error("M98 agent: broken frame header, Win32 error=", io_error);
            break;
        }
        if (header[0] != 'M' || header[1] != '9' ||
            header[2] != '8' || header[3] != 'R' ||
            load_u32(header + 4) != 1u) {
            console_text("M98 agent: invalid frame magic/version\r\n");
            break;
        }
        opcode = load_u32(header + 8);
        request_id = load_u32(header + 12);
        size = load_u32(header + 16);
        if (opcode < OP_PING || opcode > OP_PROMOTE || size > MAX_PAYLOAD) {
            console_text("M98 agent: invalid frame opcode/length\r\n");
            break;
        }
        if (!read_exact(channel, request_data, size,
                        &clock, FALSE, &io_error)) {
            console_error("M98 agent: broken frame payload, Win32 error=", io_error);
            break;
        }
        dispatch(opcode, request_data, size, &reply_size);
        if (!send_reply(channel, opcode, request_id, reply_size, &io_error)) {
            console_error("M98 agent: reply write failed, Win32 error=", io_error);
            break;
        }
    }
    console_text("M98 agent: connection closed; restart for a new session\r\n");
    CloseHandle(channel);
    ExitProcess(3u);
}
