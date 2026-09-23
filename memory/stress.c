/*
 * Bounded cross-process memory experiment for Windows 98 SE.
 * This tests committed and page-touched virtual memory, NOT physical residency.
 * Only KERNEL32 imports; no C runtime or OS configuration changes.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

#define MAX_WORKERS 8
#define MAX_MIB_PER_WORKER 768
#define MAX_TOTAL_MIB 3584
#define CHUNK_MIB 4
#define MAX_CHUNKS (MAX_MIB_PER_WORKER / CHUNK_MIB)
#define MIB_BYTES 1048576UL
#define PAGE_BYTES 4096UL
#define PAGES_PER_CHUNK (CHUNK_MIB * MIB_BYTES / PAGE_BYTES)
#define READY_TIMEOUT_MS 60000UL
#define RELEASE_TIMEOUT_MS 600000UL

static void write_line(const char *line)
{
    DWORD length = 0, written;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    while (line[length]) ++length;
    if (output && output != INVALID_HANDLE_VALUE)
        WriteFile(output, line, length, &written, 0);
}

static char *append_text(char *out, const char *text)
{
    while (*text) *out++ = *text++;
    return out;
}

static char *append_number(char *out, DWORD value)
{
    char digits[11];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) *out++ = digits[--count];
    return out;
}

static char *append_hex(char *out, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    out = append_text(out, "0x");
    for (shift = 28; shift >= 0; shift -= 4)
        *out++ = digits[(value >> shift) & 15];
    return out;
}

static void finish_line(char *out)
{
    *out++ = '\r';
    *out++ = '\n';
    *out = 0;
}

static void report_error(const char *label, DWORD error)
{
    char line[128], *out = line;
    out = append_text(out, label);
    out = append_text(out, " error=");
    out = append_number(out, error);
    finish_line(out);
    write_line(line);
}

static void report_memory(const char *stage)
{
    MEMORYSTATUS status;
    char line[192], *out = line;
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);
    out = append_text(out, stage);
    out = append_text(out, " legacy_total_phys_bytes=");
    out = append_hex(out, status.dwTotalPhys);
    out = append_text(out, " legacy_avail_phys_bytes=");
    out = append_hex(out, status.dwAvailPhys);
    out = append_text(out, " legacy_avail_pagefile_bytes=");
    out = append_hex(out, status.dwAvailPageFile);
    finish_line(out);
    write_line(line);
}

/* Small command-line parser: quoted executable paths and unquoted arguments. */
static const char *next_argument(const char *cursor, char *out, DWORD capacity)
{
    DWORD count = 0;
    char quote = 0;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor == '"') { quote = '"'; ++cursor; }
    while (*cursor && count + 1 < capacity) {
        if (quote && *cursor == quote) { ++cursor; break; }
        if (!quote && (*cursor == ' ' || *cursor == '\t')) break;
        out[count++] = *cursor++;
    }
    out[count] = 0;
    while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
    return cursor;
}

static int equal_text(const char *left, const char *right)
{
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static int parse_number(const char *text, DWORD *result)
{
    DWORD value = 0, digit;
    if (!*text) return 0;
    while (*text) {
        if (*text < '0' || *text > '9') return 0;
        digit = (DWORD)(*text++ - '0');
        if (value > (0xFFFFFFFFUL - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    *result = value;
    return 1;
}

static void make_event_name(char *out, const char *type, DWORD pid,
                            DWORD tick, DWORD index)
{
    char *end = out;
    end = append_text(end, "m98mem_");
    end = append_text(end, type);
    *end++ = '_';
    end = append_number(end, pid);
    *end++ = '_';
    end = append_number(end, tick);
    *end++ = '_';
    end = append_number(end, index);
    *end = 0;
}

static void report_worker(DWORD id, DWORD requested, DWORD allocated,
                          DWORD verified_pages, DWORD error, int retained)
{
    char line[192], *out = line;
    out = append_text(out, "worker=");
    out = append_number(out, id);
    out = append_text(out, " requested_mib=");
    out = append_number(out, requested);
    out = append_text(out, " allocated_mib=");
    out = append_number(out, allocated);
    out = append_text(out, " pages_verified=");
    out = append_number(out, verified_pages);
    out = append_text(out, " allocation_error=");
    out = append_number(out, error);
    out = append_text(out, " retained_until_release=");
    out = append_number(out, (DWORD)retained);
    finish_line(out);
    write_line(line);
}

/* The odd multiplier keeps distinct page indexes distinct, including pages
 * exactly 1 MiB apart, over the bounded range of this experiment. */
static DWORD page_pattern(DWORD id, DWORD chunk, DWORD page)
{
    DWORD index = chunk * PAGES_PER_CHUNK + page + 1;
    return index * 0x9E3779B9UL ^ id * 0x85EBCA6BUL;
}

static void write_page(volatile BYTE *memory, DWORD page, DWORD pattern)
{
    DWORD offset = page * PAGE_BYTES;
    memory[offset] = (BYTE)pattern;
    memory[offset + 1] = (BYTE)(pattern >> 8);
    memory[offset + PAGE_BYTES / 2] = (BYTE)(pattern >> 16);
    memory[offset + PAGE_BYTES - 1] = (BYTE)(pattern >> 24);
}

static int check_page(volatile BYTE *memory, DWORD page, DWORD pattern)
{
    DWORD offset = page * PAGE_BYTES;
    return memory[offset] == (BYTE)pattern &&
           memory[offset + 1] == (BYTE)(pattern >> 8) &&
           memory[offset + PAGE_BYTES / 2] == (BYTE)(pattern >> 16) &&
           memory[offset + PAGE_BYTES - 1] == (BYTE)(pattern >> 24);
}

static void run_worker(DWORD mib, DWORD id, const char *ready_name,
                       const char *verify_name, const char *release_name)
{
    void *chunks[MAX_CHUNKS];
    DWORD chunk_size[MAX_CHUNKS];
    DWORD chunk_count = 0, allocated = 0, verified_pages = 0;
    DWORD error = 0, i, page, bytes;
    DWORD result = 0, wait_result;
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, ready_name);
    HANDLE verify = OpenEventA(SYNCHRONIZE, FALSE, verify_name);
    HANDLE release = OpenEventA(SYNCHRONIZE, FALSE, release_name);

    if (!ready || !verify || !release) {
        report_error("worker_event_open_failed", GetLastError());
        if (ready) CloseHandle(ready);
        if (verify) CloseHandle(verify);
        if (release) CloseHandle(release);
        ExitProcess(0);
    }

    report_memory("worker_before");
    while (allocated < mib && chunk_count < MAX_CHUNKS) {
        DWORD part = mib - allocated;
        volatile BYTE *memory;
        if (part > CHUNK_MIB) part = CHUNK_MIB;
        bytes = part * MIB_BYTES;
        memory = (volatile BYTE *)VirtualAlloc(0, bytes,
                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!memory) { error = GetLastError(); break; }
        chunks[chunk_count] = (void *)memory;
        chunk_size[chunk_count] = bytes;
        for (page = 0; page < bytes / PAGE_BYTES; ++page)
            write_page(memory, page, page_pattern(id, chunk_count, page));
        ++chunk_count;
        allocated += part;
    }

    /* The parent releases this barrier only after every worker has committed
     * and touched its pages. No worker frees a page before all verify. */
    SetEvent(ready);
    wait_result = WaitForSingleObject(verify, RELEASE_TIMEOUT_MS);
    if (wait_result != WAIT_OBJECT_0) {
        report_error("worker_verify_timeout_or_failure", wait_result);
        error = ERROR_TIMEOUT;
    }
    for (i = 0; i < chunk_count && error != ERROR_TIMEOUT; ++i) {
        volatile BYTE *memory = (volatile BYTE *)chunks[i];
        for (page = 0; page < chunk_size[i] / PAGE_BYTES; ++page) {
            if (!check_page(memory, page, page_pattern(id, i, page))) {
                error = ERROR_CRC;
                break;
            }
            ++verified_pages;
        }
        if (error == ERROR_CRC) break;
    }
    /* Partial allocations are still retained to make their overlap measurable. */
    if (error != ERROR_CRC && verified_pages == allocated * (MIB_BYTES / PAGE_BYTES))
        result = allocated;
    report_worker(id, mib, allocated, verified_pages, error, 1);
    report_memory("worker_after_verify");
    /* The parent resets the ready event before releasing the verify barrier. */
    SetEvent(ready);
    wait_result = WaitForSingleObject(release, RELEASE_TIMEOUT_MS);
    if (wait_result != WAIT_OBJECT_0) {
        report_error("worker_release_timeout_or_failure", wait_result);
        result = 0;
    }
    for (i = 0; i < chunk_count; ++i) VirtualFree(chunks[i], 0, MEM_RELEASE);
    CloseHandle(release);
    CloseHandle(verify);
    CloseHandle(ready);
    ExitProcess(result);
}

static void report_parent_result(DWORD requested, DWORD workers,
                                 DWORD verified_sum, DWORD ready_count,
                                 DWORD live_count)
{
    char line[192], *out = line;
    out = append_text(out, "result requested_aggregate_mib=");
    out = append_number(out, requested * workers);
    out = append_text(out, " verified_aggregate_mib=");
    out = append_number(out, verified_sum);
    out = append_text(out, " workers_ready=");
    out = append_number(out, ready_count);
    out = append_text(out, " workers_live_at_sample=");
    out = append_number(out, live_count);
    finish_line(out);
    write_line(line);
}

static void run_parent(DWORD mib, DWORD workers)
{
    char path[MAX_PATH], command[MAX_PATH + 384], name[80], verify_name[80], release_name[80];
    HANDLE ready[MAX_WORKERS], verify = 0, release = 0;
    PROCESS_INFORMATION process[MAX_WORKERS];
    STARTUPINFOA startup;
    DWORD id, path_length, pid = GetCurrentProcessId(), tick = GetTickCount();
    DWORD launched = 0, ready_count = 0, live_count = 0;
    DWORD total_verified = 0, code, wait_result;
    DWORD status = 0;
    char *out;
    BYTE *cursor = (BYTE *)&startup;

    path_length = GetModuleFileNameA(0, path, MAX_PATH);
    if (!path_length || path_length >= MAX_PATH) {
        write_line("executable_path_unavailable\r\n");
        ExitProcess(2);
    }
    for (id = 0; id < sizeof(startup); ++id) cursor[id] = 0;
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    report_memory("parent_before");
    make_event_name(verify_name, "verify", pid, tick, 0);
    verify = CreateEventA(0, TRUE, FALSE, verify_name);
    if (!verify) {
        report_error("verify_event_create_failed", GetLastError());
        ExitProcess(2);
    }
    make_event_name(release_name, "release", pid, tick, 0);
    release = CreateEventA(0, TRUE, FALSE, release_name);
    if (!release) {
        report_error("release_event_create_failed", GetLastError());
        CloseHandle(verify);
        ExitProcess(2);
    }

    for (id = 0; id < workers; ++id) {
        make_event_name(name, "ready", pid, tick, id);
        ready[id] = CreateEventA(0, TRUE, FALSE, name);
        if (!ready[id]) {
            report_error("ready_event_create_failed", GetLastError());
            status = 2;
            break;
        }
        out = command;
        *out++ = '"';
        out = append_text(out, path);
        *out++ = '"';
        out = append_text(out, " --worker ");
        out = append_number(out, mib);
        *out++ = ' ';
        out = append_number(out, id);
        *out++ = ' ';
        out = append_text(out, name);
        *out++ = ' ';
        out = append_text(out, verify_name);
        *out++ = ' ';
        out = append_text(out, release_name);
        *out = 0;
        if (!CreateProcessA(path, command, 0, 0, TRUE, 0, 0, 0,
                            &startup, &process[id])) {
            report_error("worker_create_failed", GetLastError());
            CloseHandle(ready[id]);
            status = 2;
            break;
        }
        ++launched;
    }

    for (id = 0; id < launched; ++id) {
        HANDLE pair[2];
        pair[0] = ready[id];
        pair[1] = process[id].hProcess;
        wait_result = WaitForMultipleObjects(2, pair, FALSE, READY_TIMEOUT_MS);
        if (wait_result == WAIT_OBJECT_0) ++ready_count;
        else {
            report_error("worker_not_ready", wait_result);
            status = 2;
        }
    }
    /* All workers are parked on verify; reuse each ready event as its
     * post-overlap verification acknowledgment. */
    for (id = 0; id < launched; ++id)
        if (!ResetEvent(ready[id])) status = 2;
    if (!SetEvent(verify)) status = 2;
    for (id = 0; id < launched; ++id) {
        HANDLE pair[2];
        pair[0] = ready[id];
        pair[1] = process[id].hProcess;
        wait_result = WaitForMultipleObjects(2, pair, FALSE, READY_TIMEOUT_MS);
        if (wait_result != WAIT_OBJECT_0) {
            report_error("worker_not_verified", wait_result);
            status = 2;
        }
    }
    for (id = 0; id < launched; ++id)
        if (WaitForSingleObject(process[id].hProcess, 0) == WAIT_TIMEOUT)
            ++live_count;
    report_memory("parent_during_overlap");
    SetEvent(release);
    for (id = 0; id < launched; ++id) {
        wait_result = WaitForSingleObject(process[id].hProcess, 15000UL);
        if (wait_result == WAIT_OBJECT_0 &&
            GetExitCodeProcess(process[id].hProcess, &code) && code <= mib)
            total_verified += code;
        else status = 2;
        CloseHandle(process[id].hThread);
        CloseHandle(process[id].hProcess);
        CloseHandle(ready[id]);
    }
    CloseHandle(release);
    CloseHandle(verify);
    report_memory("parent_after_release");
    report_parent_result(mib, workers, total_verified, ready_count, live_count);
    if (launched != workers || ready_count != workers || live_count != workers ||
        total_verified != mib * workers) status = 2;
    if (status) write_line("result_status=INCOMPLETE\r\n");
    else write_line("result_status=ALL_REQUESTED_PAGES_VERIFIED\r\n");
    ExitProcess(status);
}

void mainCRTStartup(void)
{
    const char *cursor = GetCommandLineA();
    char executable[MAX_PATH], first[80], second[80], third[80], fourth[80], fifth[80], sixth[80];
    DWORD mib = 16, workers = 1, id;
    cursor = next_argument(cursor, executable, sizeof(executable));
    cursor = next_argument(cursor, first, sizeof(first));
    if (equal_text(first, "--worker")) {
        cursor = next_argument(cursor, second, sizeof(second));
        cursor = next_argument(cursor, third, sizeof(third));
        cursor = next_argument(cursor, fourth, sizeof(fourth));
        cursor = next_argument(cursor, fifth, sizeof(fifth));
        cursor = next_argument(cursor, sixth, sizeof(sixth));
        /* Event names fit in 80 bytes and contain no spaces. */
        if (!parse_number(second, &mib) || mib < 1 || mib > MAX_MIB_PER_WORKER ||
            !parse_number(third, &id) || id >= MAX_WORKERS || !*fourth || !*fifth || !*sixth)
            ExitProcess(2);
        run_worker(mib, id, fourth, fifth, sixth);
    }
    if (equal_text(first, "--help") || equal_text(first, "/?")) {
        write_line("Usage: memstress.exe [MiB_per_worker 1..768] [workers 1..8]\r\n");
        write_line("Default: 16 MiB x 1. Aggregate limit: 3584 MiB.\r\n");
        ExitProcess(0);
    }
    if (*first) {
        if (!parse_number(first, &mib)) ExitProcess(2);
        cursor = next_argument(cursor, second, sizeof(second));
        if (*second && !parse_number(second, &workers)) ExitProcess(2);
        cursor = next_argument(cursor, third, sizeof(third));
        if (*third) ExitProcess(2);
    }
    if (mib < 1 || mib > MAX_MIB_PER_WORKER || workers < 1 ||
        workers > MAX_WORKERS || mib * workers > MAX_TOTAL_MIB) {
        write_line("Invalid limits. Use --help.\r\n");
        ExitProcess(2);
    }
    run_parent(mib, workers);
}
