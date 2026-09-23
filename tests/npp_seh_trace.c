/*
 * Bounded first-chance exception recorder for the installed Win98 guest.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 * This does not patch Notepad++ or handle its access violation: it resumes
 * exception search with DBG_EXCEPTION_NOT_HANDLED, preserving app SEH.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

#define TRACE_MS 20000u
#define DRAIN_MS 4000u
#define MAX_EVENTS 256u
#define MAX_MODULES 128u
#define MAX_THREADS 64u

typedef struct loaded_module {
    DWORD base;
    DWORD size;
    DWORD stamp;
} LOADED_MODULE;

typedef struct thread_record {
    DWORD tid;
    HANDLE handle;
} THREAD_RECORD;

static LOADED_MODULE modules[MAX_MODULES];
static THREAD_RECORD threads[MAX_THREADS];
static DWORD module_count, thread_count;
static BOOL module_overflow, thread_overflow;
static char target[MAX_PATH], command[MAX_PATH + 4u], directory[MAX_PATH];
static BOOL output_ok = TRUE;

static void clear_bytes(void *memory, DWORD count)
{
    volatile BYTE *bytes = (volatile BYTE *)memory;
    DWORD i;
    for (i = 0u; i < count; ++i) bytes[i] = 0u;
}

static DWORD length(const char *value)
{
    DWORD n = 0u;
    while (value[n]) ++n;
    return n;
}

static void write_text(const char *text)
{
    HANDLE stream = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD left = length(text);
    while (left) {
        DWORD done = 0u;
        if (!stream || stream == INVALID_HANDLE_VALUE ||
            !WriteFile(stream, text, left, &done, NULL) || !done) {
            output_ok = FALSE;
            return;
        }
        text += done;
        left -= done;
    }
}

static void write_hex(DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char buffer[11];
    DWORD i;
    buffer[0] = '0';
    buffer[1] = 'x';
    for (i = 0u; i < 8u; ++i)
        buffer[2u + i] = digits[(value >> (28u - i * 4u)) & 15u];
    buffer[10] = 0;
    write_text(buffer);
}

static void write_decimal(DWORD value)
{
    char buffer[11];
    DWORD i = 10u;
    buffer[i] = 0;
    do {
        buffer[--i] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value);
    write_text(buffer + i);
}

static BOOL space(char value) { return value == ' ' || value == '\t'; }

static BOOL parse_target(void)
{
    const char *cursor = GetCommandLineA();
    DWORD n = 0u, i;
    if (!cursor) return FALSE;
    while (space(*cursor)) ++cursor;
    if (*cursor == '"') {
        ++cursor;
        while (*cursor && *cursor != '"') ++cursor;
        if (*cursor != '"') return FALSE;
        ++cursor;
    } else {
        while (*cursor && !space(*cursor)) ++cursor;
    }
    while (space(*cursor)) ++cursor;
    if (!*cursor) return FALSE;
    if (*cursor == '"') {
        ++cursor;
        while (*cursor && *cursor != '"' && n + 1u < MAX_PATH)
            target[n++] = *cursor++;
        if (*cursor != '"') return FALSE;
        ++cursor;
    } else {
        while (*cursor && !space(*cursor) && n + 1u < MAX_PATH)
            target[n++] = *cursor++;
    }
    while (space(*cursor)) ++cursor;
    if (*cursor || n < 4u) return FALSE;
    target[n] = 0;
    if (target[1] != ':' || (target[2] != '\\' && target[2] != '/'))
        return FALSE;
    command[0] = '"';
    for (i = 0u; i < n; ++i) command[i + 1u] = target[i];
    command[n + 1u] = '"';
    command[n + 2u] = 0;
    for (i = 0u; i < n; ++i) directory[i] = target[i];
    directory[n] = 0;
    while (n > 3u && directory[n - 1u] != '\\' && directory[n - 1u] != '/')
        --n;
    if (n <= 3u) directory[3] = 0;
    else directory[n - 1u] = 0;
    return TRUE;
}

static void remember_thread(DWORD tid, HANDLE handle)
{
    if (thread_count < MAX_THREADS) {
        threads[thread_count].tid = tid;
        threads[thread_count].handle = handle;
        ++thread_count;
    } else if (handle) {
        thread_overflow = TRUE;
        CloseHandle(handle);
    }
}

static HANDLE thread_handle(DWORD tid)
{
    DWORD i;
    for (i = 0u; i < thread_count; ++i)
        if (threads[i].tid == tid) return threads[i].handle;
    return NULL;
}

static void forget_thread(DWORD tid)
{
    DWORD i;
    for (i = 0u; i < thread_count; ++i) {
        if (threads[i].tid != tid) continue;
        if (threads[i].handle) CloseHandle(threads[i].handle);
        threads[i] = threads[--thread_count];
        return;
    }
}

static void remember_image(HANDLE process, LPVOID address, const char *label)
{
    BYTE header[512];
    SIZE_T got = 0u;
    DWORD base = (DWORD)(UINT_PTR)address;
    DWORD pe_offset, stamp = 0u, size = 0u;
    if (ReadProcessMemory(process, address, header, sizeof(header), &got) &&
        got == sizeof(header) && header[0] == 'M' && header[1] == 'Z') {
        pe_offset = *(DWORD *)(void *)(header + 0x3Cu);
        if (pe_offset + 24u + 60u <= sizeof(header) &&
            header[pe_offset] == 'P' && header[pe_offset + 1u] == 'E') {
            stamp = *(DWORD *)(void *)(header + pe_offset + 8u);
            size = *(DWORD *)(void *)(header + pe_offset + 24u + 56u);
        }
    }
    if (module_count < MAX_MODULES) {
        modules[module_count].base = base;
        modules[module_count].size = size;
        modules[module_count].stamp = stamp;
        ++module_count;
    } else module_overflow = TRUE;
    write_text(label);
    write_text(" base="); write_hex(base);
    write_text(" size="); write_hex(size);
    write_text(" stamp="); write_hex(stamp);
    write_text("\r\n");
}

static void forget_image(LPVOID address)
{
    DWORD i, base = (DWORD)(UINT_PTR)address;
    for (i = 0u; i < module_count; ++i) {
        if (modules[i].base != base) continue;
        modules[i] = modules[--module_count];
        return;
    }
}

static void write_module(DWORD address)
{
    DWORD i;
    for (i = 0u; i < module_count; ++i) {
        if (address < modules[i].base || !modules[i].size ||
            address - modules[i].base >= modules[i].size) continue;
        write_text(" module_base="); write_hex(modules[i].base);
        write_text(" module_offset="); write_hex(address - modules[i].base);
        write_text(" module_stamp="); write_hex(modules[i].stamp);
        return;
    }
    write_text(" module=unmapped");
}

static void write_exception(const DEBUG_EVENT *event, HANDLE process)
{
    const EXCEPTION_DEBUG_INFO *info = &event->u.Exception;
    const EXCEPTION_RECORD *record = &info->ExceptionRecord;
    DWORD address = (DWORD)(UINT_PTR)record->ExceptionAddress;
    HANDLE thread = thread_handle(event->dwThreadId);
    CONTEXT context;
    DWORD stack[8];
    SIZE_T got = 0u;
    DWORD i;
    if (record->ExceptionCode == EXCEPTION_BREAKPOINT ||
        record->ExceptionCode == EXCEPTION_SINGLE_STEP) return;
    write_text("EXCEPTION code="); write_hex(record->ExceptionCode);
    write_text(" first_chance="); write_decimal(info->dwFirstChance);
    write_text(" pid="); write_decimal(event->dwProcessId);
    write_text(" tid="); write_decimal(event->dwThreadId);
    write_text(" address="); write_hex(address);
    write_module(address);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2u) {
        write_text(" access_kind=");
        write_decimal((DWORD)record->ExceptionInformation[0]);
        write_text(" fault_address=");
        write_hex((DWORD)record->ExceptionInformation[1]);
    }
    if (thread) {
        clear_bytes(&context, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(thread, &context)) {
            write_text(" eip="); write_hex(context.Eip);
            write_text(" esp="); write_hex(context.Esp);
            write_text(" ebp="); write_hex(context.Ebp);
            if (ReadProcessMemory(process, (LPCVOID)(UINT_PTR)context.Esp,
                                  stack, sizeof(stack), &got) && got == sizeof(stack)) {
                write_text(" stack8=");
                for (i = 0u; i < 8u; ++i) {
                    if (i) write_text(",");
                    write_hex(stack[i]);
                }
            }
        } else {
            write_text(" context_error="); write_decimal(GetLastError());
        }
    }
    write_text("\r\n");
}

void __cdecl mainCRTStartup(void)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION child;
    DWORD began, event_count = 0u, av_count = 0u, exit_code = 0u;
    HANDLE initial_debug_thread = NULL;
    BOOL terminated = FALSE, exited = FALSE;
    if (!parse_target()) {
        write_text("USAGE: NPP_SEH_TRACE.EXE C:\\path\\notepad++.exe\r\n");
        ExitProcess(2u);
    }
    clear_bytes(&startup, sizeof(startup));
    clear_bytes(&child, sizeof(child));
    startup.cb = sizeof(startup);
    if (!CreateProcessA(target, command, NULL, NULL, FALSE,
                        DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS,
                        NULL, directory, &startup, &child)) {
        write_text("LAUNCH_ERROR win32_error="); write_decimal(GetLastError());
        write_text("\r\n");
        ExitProcess(2u);
    }
    write_text("DEBUG_LAUNCHED pid="); write_decimal(child.dwProcessId);
    write_text(" target="); write_text(target); write_text("\r\n");
    began = GetTickCount();
    while (event_count < MAX_EVENTS) {
        DEBUG_EVENT event;
        DWORD status = DBG_CONTINUE, elapsed = (DWORD)(GetTickCount() - began);
        if (elapsed >= TRACE_MS && !terminated) {
            if (TerminateProcess(child.hProcess, ERROR_TIMEOUT)) {
                terminated = TRUE;
                write_text("BOUNDED_TERMINATION=1\r\n");
            } else {
                write_text("TERMINATE_ERROR win32_error=");
                write_decimal(GetLastError()); write_text("\r\n");
                break;
            }
        }
        if (elapsed >= TRACE_MS + DRAIN_MS) break;
        if (!WaitForDebugEvent(&event, 200u)) {
            DWORD error = GetLastError();
            if (error == ERROR_SEM_TIMEOUT || error == ERROR_TIMEOUT) continue;
            write_text("WAIT_DEBUG_ERROR win32_error=");
            write_decimal(error); write_text("\r\n");
            break;
        }
        ++event_count;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            initial_debug_thread = event.u.CreateProcessInfo.hThread;
            remember_thread(event.dwThreadId, event.u.CreateProcessInfo.hThread);
            remember_image(child.hProcess, event.u.CreateProcessInfo.lpBaseOfImage,
                           "CREATE_IMAGE");
            if (event.u.CreateProcessInfo.hFile)
                CloseHandle(event.u.CreateProcessInfo.hFile);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            remember_thread(event.dwThreadId, event.u.CreateThread.hThread);
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            forget_thread(event.dwThreadId);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            remember_image(child.hProcess, event.u.LoadDll.lpBaseOfDll,
                           "LOAD_IMAGE");
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            break;
        case UNLOAD_DLL_DEBUG_EVENT:
            forget_image(event.u.UnloadDll.lpBaseOfDll);
            break;
        case EXCEPTION_DEBUG_EVENT:
            if (event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
                ++av_count;
            write_exception(&event, child.hProcess);
            if (event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT &&
                event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_SINGLE_STEP)
                status = DBG_EXCEPTION_NOT_HANDLED;
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            exit_code = event.u.ExitProcess.dwExitCode;
            exited = TRUE;
            break;
        default:
            break;
        }
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)) {
            write_text("CONTINUE_ERROR win32_error="); write_decimal(GetLastError());
            write_text("\r\n");
            break;
        }
        if (exited) break;
    }
    if (!exited && !terminated) {
        if (TerminateProcess(child.hProcess, ERROR_TIMEOUT)) terminated = TRUE;
    }
    write_text("SUMMARY events="); write_decimal(event_count);
    write_text(" av_events="); write_decimal(av_count);
    write_text(" exited="); write_decimal(exited);
    write_text(" terminated="); write_decimal(terminated);
    write_text(" module_overflow="); write_decimal(module_overflow);
    write_text(" thread_overflow="); write_decimal(thread_overflow);
    if (exited) { write_text(" exit_code="); write_decimal(exit_code); }
    write_text("\r\n");
    while (thread_count) forget_thread(threads[0].tid);
    if (child.hThread != initial_debug_thread) CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    ExitProcess(output_ok && event_count ? 0u : 3u);
}
