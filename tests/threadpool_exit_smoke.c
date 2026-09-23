/* Exercise the Win98 threadpool detach path with its lock held by a thread
 * that ExitProcess will terminate. Including the backend exposes only the
 * private lock to this deliberately isolated regression executable. */
#include "../src/m98_threadpool.c"

typedef struct lock_context {
    HANDLE locked;
    HANDLE blocker;
} lock_context;

static void say(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, NULL);
}

static void fail(const char *message)
{
    say("FAIL: ");
    say(message);
    say("\r\n");
    ExitProcess(1);
}

static BOOL has_child_argument(const char *line)
{
    static const char argument[] = " --child";
    const char *cursor;
    while (*line) {
        for (cursor = argument; *cursor && line[cursor - argument] == *cursor;
             ++cursor) {}
        if (!*cursor) {
            char next = line[cursor - argument];
            if (!next || next == ' ' || next == '\t') return TRUE;
        }
        ++line;
    }
    return FALSE;
}

static DWORD WINAPI hold_private_lock(void *opaque)
{
    lock_context *context = (lock_context *)opaque;
    EnterCriticalSection(&m98_tp_lock);
    SetEvent(context->locked);
    WaitForSingleObject(context->blocker, INFINITE);
    LeaveCriticalSection(&m98_tp_lock);
    return 0;
}

static void child(void)
{
    lock_context context;
    DWORD thread_id;
    HANDLE thread;

    if (!m98_tp_initialize()) fail("threadpool init in child");
    context.locked = CreateEventA(NULL, TRUE, FALSE, NULL);
    context.blocker = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!context.locked || !context.blocker) fail("child event allocation");
    thread = CreateThread(NULL, 0, hold_private_lock, &context, 0, &thread_id);
    if (!thread) fail("child lock holder creation");
    if (WaitForSingleObject(context.locked, 10000) != WAIT_OBJECT_0)
        fail("child lock holder did not start");

    /* The holder cannot release this lock. Any EnterCriticalSection in the
     * process-termination branch hangs here; the parent kills us on timeout. */
    m98_tp_process_detach(TRUE);
    ExitProcess(0);
}

static void parent(void)
{
    char path[MAX_PATH], command[MAX_PATH + 16];
    DWORD length, i, exit_code;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    volatile unsigned char *bytes;
    DWORD status;

    length = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) fail("own executable path");
    command[0] = '"';
    for (i = 0; i < length; ++i) command[i + 1] = path[i];
    command[length + 1] = '"';
    command[length + 2] = ' ';
    command[length + 3] = '-';
    command[length + 4] = '-';
    command[length + 5] = 'c';
    command[length + 6] = 'h';
    command[length + 7] = 'i';
    command[length + 8] = 'l';
    command[length + 9] = 'd';
    command[length + 10] = 0;

    bytes = (volatile unsigned char *)&startup;
    for (i = 0; i < sizeof(startup); ++i) bytes[i] = 0;
    bytes = (volatile unsigned char *)&process;
    for (i = 0; i < sizeof(process); ++i) bytes[i] = 0;
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (!CreateProcessA(path, command, NULL, NULL, FALSE, 0, NULL, NULL,
                        &startup, &process))
        fail("create lock-holder child");
    status = WaitForSingleObject(process.hProcess, 20000);
    if (status != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 2);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        fail(status == WAIT_TIMEOUT ? "process detach blocked on lock" :
                                   "wait for child process");
    }
    if (!GetExitCodeProcess(process.hProcess, &exit_code))
        fail("read child exit status");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code) fail("child process failed");

    /* Dynamic-unload-like path remains available when no worker holds the
     * lock; it should set shutdown for the normal signal path. */
    if (!m98_tp_initialize()) fail("threadpool init in parent");
    m98_tp_process_detach(FALSE);
    if (!m98_tp_shutdown) fail("ordinary detach did not set shutdown");
    say("PASS: process-termination detach skips held lock; ordinary detach signals shutdown\r\n");
    ExitProcess(0);
}

void mainCRTStartup(void)
{
    if (has_child_argument(GetCommandLineA())) child();
    else parent();
}
